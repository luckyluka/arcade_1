/* validate.c -- M4.1c: the `validate` pipeline, ported from the oracle.
 *
 * The oracle is tools/arcforge_ref.py: cmd_validate + load_asset /
 * load_asset_docs / resolve_extends / expand_variants, check_recipe_doc /
 * check_style_pack, check_params / resolve_fill, build_shape / build_line /
 * build_px / build_repeat / build_one / compile_shapes, compile_asset and
 * report. This file reproduces the report BYTE-FOR-BYTE (same stdout, same
 * exit code) with no external libraries (M4 contract):
 *
 *   * diagnostics are built through D()/ptr() exactly like the oracle's,
 *     in insertion order, with the same observed/expected/suggestions/fix
 *     fields (omitted iff the oracle omits them);
 *   * compile_asset runs its FULL pipeline -- every phase that can append
 *     a diagnostic runs -- but does not materialise the IR (drawables,
 *     states, hitboxes), because validate discards it;
 *   * Python's uncaught exceptions (a missing/unparseable asset, a bad
 *     registry, an unreadable px fragment, a TypeError in the evaluator)
 *     mean exit 1 with EMPTY stdout: af_validate() returns NULL and the
 *     caller must do the same. argparse usage errors are exit 2 (main.c).
 *
 * Documented C-vs-Python divergences are listed in the M4.1c commit
 * message (int64 saturation, JSONDecodeError text on an unreadable
 * `extends` target, ...). No machine behaviour changes: host-side
 * tooling only.
 */
#define _XOPEN_SOURCE 700         /* realpath(), getcwd() under -std=c11 */
#include "validate.h"

#include "expr.h"
#include "json.h"
#include "px.h"
#include "schema.h"
#include "sim.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- crash plumbing ---------------------------------------------------- */
/* g_crash set => the oracle would traceback: exit 1, no stdout. */

static int g_crash;
static char g_why[128];
static char g_root[1024];        /* the arcforge dir; set by af_validate() */

static void crash(const char *why) {
    if (!g_crash) {
        g_crash = 1;
        snprintf(g_why, sizeof g_why, "%s", why);
    }
}

static void *xoom(void *p) {
    if (!p) crash("out of memory");
    return p;
}

/* document KeyError parity: a required key that is not there raises */
static const af_json *need(const af_json *o, const char *k) {
    const af_json *v = o ? af_json_get(o, k) : NULL;
    if (!v) crash("bad document");
    return v;
}

static const af_json *need_str(const af_json *o, const char *k) {
    const af_json *v = need(o, k);
    if (v && v->type != AF_JSON_STRING) { crash("bad document"); return NULL; }
    return v;
}

/* ---- Python bits ------------------------------------------------------- */

static int64_t py_floordiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* type(x).__name__ (NOT _typename: None -> "NoneType", list -> "list") */
static const char *py_typename(const af_json *v) {
    if (!v) return "NoneType";
    switch (v->type) {
    case AF_JSON_NULL:   return "NoneType";
    case AF_JSON_BOOL:   return "bool";
    case AF_JSON_INT:    return "int";
    case AF_JSON_FLOAT:  return "float";
    case AF_JSON_STRING: return "str";
    case AF_JSON_ARRAY:  return "list";
    default:             return "dict";
    }
}

/* str(v) where v is a scalar (f-string interpolation); float formatting is
 * af_py_repr_float() (expr.c), the shared Python str() implementation. */
static const char *py_str(const af_json *v, char *buf, size_t sz) {
    if (!v || v->type == AF_JSON_NULL) return "None";
    if (v->type == AF_JSON_BOOL) return v->b ? "True" : "False";
    if (v->type == AF_JSON_INT) {
        snprintf(buf, sz, "%lld", (long long)v->i);
        return buf;
    }
    if (v->type == AF_JSON_FLOAT) {
        af_py_repr_float(v->d, buf, sz);
        return buf;
    }
    if (v->type == AF_JSON_STRING) return v->s;
    return v->type == AF_JSON_ARRAY ? "[...]" : "{...}";
}

/* num4(): a number as it appears in report text */
static void num4_str(const af_json *v, char *buf, size_t sz) {
    if (v && v->type == AF_JSON_BOOL) {
        snprintf(buf, sz, "%s", v->b ? "true" : "false");
        return;
    }
    if (v && v->type == AF_JSON_FLOAT) {
        snprintf(buf, sz, "%.4f", v->d);
        return;
    }
    if (v && v->type == AF_JSON_INT) {
        snprintf(buf, sz, "%lld", (long long)v->i);
        return;
    }
    buf[0] = 0;
}

/* JSON pointer: "/" + str(p) with ~0/~1 escaping; str(int) for ints */
static void ptr_cat(char *buf, size_t sz, const char *part) {
    size_t n = strlen(buf);
    if (n + 1 < sz) buf[n++] = '/';             /* ptr() joins with "/" */
    for (const char *p = part; *p && n + 1 < sz; p++) {
        if (*p == '~') {
            if (n + 2 < sz) { buf[n++] = '~'; buf[n++] = '0'; }
        } else if (*p == '/') {
            if (n + 2 < sz) { buf[n++] = '~'; buf[n++] = '1'; }
        } else {
            buf[n++] = *p;
        }
    }
    buf[n] = 0;
}

static void ptr_int(char *buf, size_t sz, long long v) {
    char t[32];
    snprintf(t, sizeof t, "%lld", v);
    ptr_cat(buf, sz, t);
}

/* ---- diagnostics ------------------------------------------------------- */

/* D(): severity always "error" in this command; observed/expected are
 * omitted when NULL or Python None, suggestions/fix when empty. TAKES
 * ownership of observed/expected/suggestions/fix. */
static af_json *D(const char *code, const char *path, const char *message,
                  af_json *observed, af_json *expected,
                  af_json *suggestions, af_json *fix) {
    if (observed && observed->type == AF_JSON_NULL) { af_json_free(observed); observed = NULL; }
    if (expected && expected->type == AF_JSON_NULL) { af_json_free(expected); expected = NULL; }
    af_json *d = af_json_obj();
    if (!d) {
        af_json_free(observed); af_json_free(expected);
        af_json_free(suggestions); af_json_free(fix);
        crash("out of memory");
        return NULL;
    }
    af_json_obj_put(d, "code", af_json_str(code));
    af_json_obj_put(d, "severity", af_json_str("error"));
    af_json_obj_put(d, "path", af_json_str(path));
    af_json_obj_put(d, "message", af_json_str(message));
    if (observed) af_json_obj_put(d, "observed", observed);
    if (expected) af_json_obj_put(d, "expected", expected);
    if (suggestions && suggestions->n)
        af_json_obj_put(d, "suggestions", suggestions);
    else
        af_json_free(suggestions);
    if (fix && fix->n)
        af_json_obj_put(d, "fix", fix);
    else
        af_json_free(fix);
    if (g_crash) { af_json_free(d); return NULL; }
    return d;
}

/* observed/expected carry Python-None as "omit"; returns an OWNED clone */
static af_json *opt(const af_json *v) {
    if (!v || v->type == AF_JSON_NULL) return NULL;
    return af_json_clone(v);
}

static void diags_push(af_json *diags, af_json *d) {
    if (d) af_json_arr_push(diags, d);
    else if (!g_crash) crash("out of memory");
}

/* fix fragments: Python's `fix` is a LIST of JSON-Patch operations
 * ({"op": ..., "path": ...} [, {"value": ...}]) -- OUTPUT_FORMAT.md */
static af_json *fix_op(const char *op, const char *path) {
    af_json *f = xoom(af_json_arr());
    if (!f) return NULL;
    af_json *o = xoom(af_json_obj());
    if (!o) { af_json_free(f); crash("out of memory"); return NULL; }
    af_json_obj_put(o, "op", xoom(af_json_str(op)));
    af_json_obj_put(o, "path", xoom(af_json_str(path)));
    af_json_arr_push(f, o);
    if (g_crash) { af_json_free(f); return NULL; }
    return f;
}

static void fix_value(af_json *f, af_json *v) {   /* takes ownership */
    if (f && f->type == AF_JSON_ARRAY && f->n)
        af_json_obj_put(f->items[f->n - 1], "value", v);
    else
        af_json_free(v);
}

/* ---- similarity -------------------------------------------------------- */

/* sorted(x): keys of an object, or items of an array, in Python byte
 * order (UTF-8 order equals code-point order for str) */
static af_json *sorted_strs(const af_json *v) {
    af_json *out = xoom(af_json_arr());
    if (!out || !v) return out;
    size_t n;
    const af_json **src = NULL;
    if (v->type == AF_JSON_OBJECT) {
        n = v->n / 2;   /* pairs (n counts entries) */
        src = (const af_json **)malloc(n * sizeof *src);
        if (!src) { crash("out of memory"); return out; }
        for (size_t i = 0; i < n; i++) src[i] = v->items[2 * i];
    } else if (v->type == AF_JSON_ARRAY) {
        n = v->n;
        src = (const af_json **)malloc((n ? n : 1) * sizeof *src);
        if (!src) { crash("out of memory"); return out; }
        for (size_t i = 0; i < n; i++) src[i] = v->items[i];
    } else {
        return out;
    }
    /* insertion sort by byte order (stable, lists are small) */
    for (size_t i = 1; i < n; i++) {
        const af_json *key = src[i];
        size_t j = i;
        while (j && strcmp(src[j - 1]->s, key->s) > 0) { src[j] = src[j - 1]; j--; }
        src[j] = key;
    }
    for (size_t i = 0; i < n; i++) {
        if (src[i]->type != AF_JSON_STRING) continue;
        af_json_arr_push(out, af_json_clone(src[i]));
    }
    free(src);
    if (g_crash) { af_json_free(out); return NULL; }
    return out;
}

/* difflib.get_close_matches(str(word), list(options), 3, 0.6) --
 * options: an object (keys) or an array (string items); the candidate
 * order cannot influence the result (nlargest sorts by (ratio, text)) */
static af_json *close_arr(const af_json *word, const af_json *options) {
    char wbuf[64];
    const char *w;
    if (word && word->type == AF_JSON_STRING) w = word->s;
    else w = py_str(word, wbuf, sizeof wbuf);
    size_t n = 0;
    const char **poss = NULL;
    if (options && options->type == AF_JSON_OBJECT) {
        n = options->n / 2;   /* pairs */
        poss = (const char **)xoom(malloc((n ? n : 1) * sizeof *poss));
        if (!poss) return NULL;
        for (size_t i = 0; i < n; i++) poss[i] = options->items[2 * i]->s;
    } else if (options && options->type == AF_JSON_ARRAY) {
        n = options->n;
        poss = (const char **)xoom(malloc((n ? n : 1) * sizeof *poss));
        if (!poss) return NULL;
        for (size_t i = 0; i < n; i++)
            poss[i] = options->items[i]->type == AF_JSON_STRING
                          ? options->items[i]->s : "";
    }
    if (!poss) return xoom(af_json_arr());
    size_t idx[3];
    size_t k = af_close_matches(w, poss, n, idx, 3);
    af_json *out = xoom(af_json_arr());
    for (size_t i = 0; i < k && out; i++)
        af_json_arr_push(out, xoom(af_json_str(poss[idx[i]])));
    free(poss);
    if (!out) crash("out of memory");
    return out;
}

/* ---- file loading ------------------------------------------------------ */

/* json.loads(Path(p).read_text()): any failure is a traceback (crash) */
static af_json *load_json_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { crash("unreadable file"); return NULL; }
    char *buf = NULL;
    size_t n = 0, cap = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 1 > cap) {
            size_t nc = cap ? cap * 2 : 4096;
            char *p = (char *)realloc(buf, nc);
            if (!p) { free(buf); fclose(f); crash("out of memory"); return NULL; }
            buf = p;
            cap = nc;
        }
        buf[n++] = (char)c;
    }
    int ferr = ferror(f);
    fclose(f);
    if (ferr) { free(buf); crash("unreadable file"); return NULL; }
    if (!buf) { buf = (char *)malloc(1); if (!buf) { crash("out of memory"); return NULL; } }
    char err[256];
    af_json *doc = NULL;
    if (af_json_parse(buf, n, &doc, err, sizeof err) != 0) {
        free(buf);
        crash("json error");
        return NULL;
    }
    free(buf);
    return doc;
}

/* schema_diags(doc, name, base_path): load schema/<name> per call */
static af_json *schema_diags(const af_json *doc, const char *name,
                             const char *base_path) {
    char path[1400];
    snprintf(path, sizeof path, "%s/schema/%s", g_root[0] ? g_root : ".", name);
    af_json *schema = load_json_file(path);
    if (!schema) return NULL;
    char err[256];
    af_json *d = af_schema_diags(doc, schema, base_path, err, sizeof err);
    af_json_free(schema);
    if (!d) { crash("schema failure"); return NULL; }
    return d;
}

/* ---- expression evaluation -------------------------------------------- */

/* ev(expr, env): the whole of the oracle's ev() -- scalars pass through,
 * strings go through _prep/parse/eval, anything else is the oracle's
 * uncaught TypeError in _prep (AF_EV_CRASH). *is_err = ExprError with
 * its text in msg; -1 status only via g_crash. */
static af_json *ev_v(const af_json *expr, const af_json *env,
                     int *is_err, char *msg, size_t msgsz) {
    *is_err = 0;
    msg[0] = 0;
    af_json *out = NULL;
    char *m = NULL;
    af_ev_status st = af_ev(expr, env, &out, &m);
    if (st == AF_EV_ERR) {
        *is_err = 1;
        snprintf(msg, msgsz, "%s", m ? m : "");
        free(m);
        af_json_free(out);
        return NULL;
    }
    free(m);
    if (st == AF_EV_CRASH || !out) { crash("expression crash"); return NULL; }
    return out;
}

/* to_int(ev(...)): full wrapper; *is_err = ExprError (caught by the
 * phase's handler), crash = uncaught. */
static int ev_i(const af_json *expr, const af_json *env, int64_t *out,
                char *msg, size_t msgsz) {
    int is_err = 0;
    af_json *v = ev_v(expr, env, &is_err, msg, msgsz);
    if (g_crash) return -1;
    if (!v) {
        if (is_err) return 1;            /* ExprError text already in msg */
        crash("expression crash");
        return -1;
    }
    char *m = NULL;
    af_ev_status st = af_to_int(v, out, &m);
    af_json_free(v);
    if (st == AF_EV_ERR) {
        snprintf(msg, msgsz, "%s", m ? m : "");
        free(m);
        return 1;
    }
    free(m);
    if (st == AF_EV_CRASH) { crash("to_int crash"); return -1; }
    return 0;
}

/* ---- check_params ------------------------------------------------------ */

/* check_params(specs, given, path, style, diags, label, env): mirrors the
 * oracle line-for-line; returns the resolved parameters object (owned),
 * or NULL with g_crash. Diagnostics append to `diags`. */
static af_json *check_params(const af_json *specs, const af_json *given,
                             const char *path, const af_json *style,
                             af_json *diags, const char *label,
                             const af_json *env) {
    af_json *out = xoom(af_json_obj());
    if (!out) return NULL;
    if (given && given->type != AF_JSON_OBJECT) {
        crash("bad document");          /* for k in None / .get on a list */
        af_json_free(out);
        return NULL;
    }

    if (given) {
        for (size_t gi = 0; gi < given->n / 2; gi++) {
            const char *k = given->items[2 * gi]->s;
            if (af_json_get(specs, k)) continue;
            char p[768];
            snprintf(p, sizeof p, "%s/%s", path, k);
            char m[768];
            snprintf(m, sizeof m, "%s has no parameter '%s'", label, k);
            diags_push(diags, D("E_UNKNOWN_PARAM", p, m, NULL,
                                sorted_strs(specs),
                                close_arr(given->items[2 * gi], specs), NULL));
            if (g_crash) { af_json_free(out); return NULL; }
        }
    }

    if (!specs) return out;
    if (specs->type != AF_JSON_OBJECT) {
        crash("bad document");          /* .items() on a non-dict */
        af_json_free(out);
        return NULL;
    }
    for (size_t si = 0; si < specs->n / 2; si++) {
        const af_json *knode = specs->items[2 * si];
        const af_json *spec = specs->items[2 * si + 1];
        if (!spec || spec->type != AF_JSON_OBJECT) { crash("bad document"); af_json_free(out); return NULL; }
        const char *k = knode->s;
        const af_json *gv = af_json_get(given, k);
        const af_json *dflt = need(spec, "default");   /* evaluated eagerly */
        const af_json *v = gv ? gv : dflt;
        if (!v) { af_json_free(out); return NULL; }
        const af_json *tnode = need(spec, "type");
        if (!tnode) { af_json_free(out); return NULL; }
        const char *t = tnode->type == AF_JSON_STRING ? tnode->s : "";
        char pp[768];
        snprintf(pp, sizeof pp, "%s/%s", path, k);

        if (!strcmp(t, "percent")) {
            const af_json *ofn = af_json_get(spec, "of");
            const af_json *dim = NULL;
            if (ofn && ofn->type == AF_JSON_STRING && env)
                dim = af_json_get(env, ofn->s);
            /* Python: env.get(spec.get("of")) -- a missing "of" reads None */
            int available = dim && dim->type == AF_JSON_INT;
            if (!available) {
                if (gv) {
                    char m[768];
                    char ofbuf[64], nb[64];
                    const char *ofs = ofn ? py_str(ofn, ofbuf, sizeof ofbuf) : "None";
                    snprintf(m, sizeof m,
                             "parameter '%s' is a percent of '%s', which this archetype does not provide",
                             k, ofs);
                    af_json *sug = NULL;
                    if (env && env->n) {
                        af_json *ek = sorted_strs(env);
                        sug = close_arr(ofn, ek);
                        af_json_free(ek);
                    }
                    char exp[768];
                    snprintf(exp, sizeof exp, "archetype dimension '%s'", ofs);
                    (void)nb;
                    diags_push(diags, D("E_UNKNOWN_SIZE_PARAM", pp, m,
                                        opt(gv), af_json_str(exp), sug, NULL));
                }
                continue;
            }
            int is_int = v && v->type == AF_JSON_INT;
            if (!is_int) {
                char m[768];
                snprintf(m, sizeof m, "'%s' must be an integer percent", k);
                diags_push(diags, D("E_PARAM_TYPE", pp, m, opt(v),
                                    af_json_str("integer"), NULL, NULL));
                continue;
            }
            const af_json *lo_n = af_json_get(spec, "min");
            const af_json *hi_n = af_json_get(spec, "max");
            int64_t lo = lo_n ? lo_n->i : 0;
            int64_t hi = hi_n ? hi_n->i : 100;
            if (v->i < lo || v->i > hi) {
                int64_t c = v->i < lo ? lo : (v->i > hi ? hi : v->i);
                char m[256], exp[160];
                char a[64], b[64];
                if (lo_n) num4_str(lo_n, a, sizeof a);
                else snprintf(a, sizeof a, "0");
                if (hi_n) num4_str(hi_n, b, sizeof b);
                else snprintf(b, sizeof b, "100");
                snprintf(m, sizeof m, "'%s' out of range", k);
                snprintf(exp, sizeof exp, "%s..%s", a, b);
                af_json *fx = fix_op("replace", pp);
                fix_value(fx, af_json_int(c));
                diags_push(diags, D("E_PARAM_RANGE", pp, m, af_json_int(v->i),
                                    af_json_str(exp), NULL, fx));
                continue;
            }
            const af_json *mp = af_json_get(spec, "min_px");
            int64_t px = mp && mp->type == AF_JSON_INT ? mp->i : 0;
            int64_t got = py_floordiv(dim->i * v->i + 50, 100);
            af_json *val = af_json_int(got > px ? got : px);
            if (!val || af_json_obj_set(out, k, val)) crash("out of memory");
            continue;
        }

        if (!strcmp(t, "int")) {
            int is_int = v && v->type == AF_JSON_INT;
            if (!is_int) {
                char m[256];
                snprintf(m, sizeof m, "'%s' must be an integer", k);
                diags_push(diags, D("E_PARAM_TYPE", pp, m, opt(v),
                                    af_json_str("integer"), NULL, NULL));
                continue;
            }
            const af_json *lon = af_json_get(spec, "min");
            const af_json *hin = af_json_get(spec, "max");
            if (!lon || !hin) { crash("bad schema"); af_json_free(out); return NULL; }
            if (v->i < lon->i || v->i > hin->i) {
                int64_t c = v->i < lon->i ? lon->i : (v->i > hin->i ? hin->i : v->i);
                char m[256], exp[160], a[64], b[64];
                num4_str(lon, a, sizeof a);
                num4_str(hin, b, sizeof b);
                snprintf(m, sizeof m, "'%s' out of range", k);
                snprintf(exp, sizeof exp, "%s..%s", a, b);
                af_json *fx = fix_op("replace", pp);
                fix_value(fx, af_json_int(c));
                diags_push(diags, D("E_PARAM_RANGE", pp, m, af_json_int(v->i),
                                    af_json_str(exp), NULL, fx));
                continue;
            }
        } else if (!strcmp(t, "bool")) {
            if (!v || v->type != AF_JSON_BOOL) {
                char m[256];
                snprintf(m, sizeof m, "'%s' must be true or false", k);
                diags_push(diags, D("E_PARAM_TYPE", pp, m, opt(v),
                                    af_json_str("boolean"), NULL, NULL));
                continue;
            }
        } else if (!strcmp(t, "enum")) {
            const af_json *vals = need(spec, "values");
            if (!vals) { af_json_free(out); return NULL; }
            int found = 0;
            if (v)
                for (size_t i = 0; i < vals->n; i++)
                    if (af_json_equal(vals->items[i], v)) { found = 1; break; }
            if (!found) {
                char m[256];
                snprintf(m, sizeof m, "'%s' is not an allowed value", k);
                diags_push(diags, D("E_PARAM_ENUM", pp, m, opt(v),
                                    af_json_clone(vals),
                                    close_arr(v, vals), NULL));
                continue;
            }
        } else if (!strcmp(t, "ramp")) {
            const af_json *allow = af_json_get(spec, "allow");
            af_json *allowed = NULL;
            if (allow && allow->type == AF_JSON_ARRAY && allow->n) {
                allowed = af_json_clone(allow);
            } else {
                /* spec.get("allow") or [r for r in style["ramps"] if ...]:
                 * style["ramps"] is a KeyError, and style["outline"]["ramp"]
                 * is only reached when the loop runs at least once */
                const af_json *ramps0 = af_json_get(style, "ramps");
                if (!ramps0 || ramps0->type != AF_JSON_OBJECT) {
                    crash("bad document");
                    af_json_free(out);
                    return NULL;
                }
                allowed = af_json_arr();
                if (!allowed) { crash("out of memory"); af_json_free(out); return NULL; }
                if (ramps0->n) {
                    const af_json *outline = af_json_get(style, "outline");
                    const af_json *orramp = outline ? af_json_get(outline, "ramp") : NULL;
                    if (!outline || !orramp || orramp->type != AF_JSON_STRING) {
                        crash("bad document");
                        af_json_free(allowed);
                        af_json_free(out);
                        return NULL;
                    }
                    for (size_t i = 0; i < ramps0->n / 2; i++)
                        if (strcmp(ramps0->items[2 * i]->s, orramp->s) != 0)
                            af_json_arr_push(allowed, af_json_clone(ramps0->items[2 * i]));
                }
            }
            int in_allowed = 0;
            if (v)
                for (size_t i = 0; i < allowed->n; i++)
                    if (af_json_equal(allowed->items[i], v)) { in_allowed = 1; break; }
            /* `v not in style["ramps"]` is only evaluated when v IS allowed */
            int in_ramps = 1;
            if (v && in_allowed) {
                if (v->type == AF_JSON_ARRAY || v->type == AF_JSON_OBJECT) {
                    crash("type error");        /* unhashable dict key */
                    af_json_free(allowed);
                    af_json_free(out);
                    return NULL;
                }
                const af_json *ramps = af_json_get(style, "ramps");
                if (!ramps || ramps->type != AF_JSON_OBJECT) {
                    crash("bad document");
                    af_json_free(allowed);
                    af_json_free(out);
                    return NULL;
                }
                in_ramps = v->type == AF_JSON_STRING ? af_json_get(ramps, v->s) != NULL : 0;
            }
            if (!in_allowed || !in_ramps) {
                char m[256];
                snprintf(m, sizeof m, "'%s' is not an available colour ramp", k);
                diags_push(diags, D("E_PARAM_ENUM", pp, m, opt(v), allowed,
                                    close_arr(v, allowed), NULL));
                continue;               /* D took allowed */
            }
            af_json_free(allowed);
        }

        if (v) {
            af_json *c = af_json_clone(v);
            if (!c || af_json_obj_set(out, k, c)) crash("out of memory");
        }
        if (g_crash) { af_json_free(out); return NULL; }
    }
    if (g_crash) { af_json_free(out); return NULL; }
    return out;
}

/* ---- resolve_fill ------------------------------------------------------ */

/* full-match of ($?name).(deep|shadow|base|light|high|*) */
static int fill_match(const char *s, char *ramp, size_t rsz, char *tone,
                      size_t tsz) {
    size_t i = 0, r = 0;
    if (s[i] == '$') {
        if (r + 1 < rsz) ramp[r] = '$';
        r++;
        i++;
    }
    if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
          s[i] == '_'))
        return 0;
    if (r + 1 < rsz) ramp[r] = s[i];
    r++;
    i++;
    for (;;) {
        char c = s[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '_';
        if (!ok) break;
        if (r + 1 < rsz) ramp[r] = c;
        r++;
        i++;
    }
    ramp[r < rsz ? r : rsz - 1] = 0;
    if (r >= rsz) return 0;
    if (s[i] != '.') return 0;
    i++;
    static const char *const tones[] = {"deep", "shadow", "base", "light", "high"};
    for (size_t t = 0; t < 5; t++) {
        size_t n = strlen(tones[t]);
        if (strncmp(s + i, tones[t], n) == 0 && s[i + n] == 0) {
            snprintf(tone, tsz, "%s", tones[t]);
            return 1;
        }
    }
    if (s[i] == '*' && s[i + 1] == 0) {
        snprintf(tone, tsz, "*");
        return 1;
    }
    return 0;
}

/* resolve_fill(fill, env, ramps, swap): 0 ok (ramp/tone filled),
 * 1 ExprError (msg filled), crash via g_crash. */
static int resolve_fill(const char *fill, const af_json *env,
                        const af_json *ramps, const af_json *swap,
                        char *ramp, size_t rsz, char *tone, size_t tsz,
                        char *msg, size_t msgsz) {
    if (!strcmp(fill, "outline")) {
        snprintf(ramp, rsz, "outline");
        snprintf(tone, tsz, "base");
        return 0;
    }
    char tbuf[64];
    if (!fill_match(fill, ramp, rsz, tbuf, sizeof tbuf)) {
        char *rp = af_py_repr(fill);
        snprintf(msg, msgsz, "bad fill %s", rp ? rp : "''");
        free(rp);
        return 1;
    }
    if (ramp[0] == '$') {
        const af_json *val = env ? af_json_get(env, ramp + 1) : NULL;
        if (!val) {
            snprintf(msg, msgsz, "unknown variable %s", ramp);
            return 1;
        }
        char vbuf[64];
        snprintf(ramp, rsz, "%s", py_str(val, vbuf, sizeof vbuf));
    }
    if (swap && swap->type == AF_JSON_OBJECT && swap->n) {
        const af_json *sv = af_json_get(swap, ramp);
        if (sv) {
            char vbuf[64];
            snprintf(ramp, rsz, "%s", py_str(sv, vbuf, sizeof vbuf));
        }
    }
    const af_json *rr = ramps && ramps->type == AF_JSON_OBJECT
                            ? af_json_get(ramps, ramp) : NULL;
    int tone_ok = 0;
    if (rr) {
        if (!strcmp(tbuf, "*")) tone_ok = 1;
        else if (rr->type == AF_JSON_OBJECT)
            tone_ok = af_json_get(rr, tbuf) != NULL;   /* key membership */
        else if (rr->type == AF_JSON_ARRAY)
            for (size_t i = 0; i < rr->n && !tone_ok; i++)
                tone_ok = rr->items[i]->type == AF_JSON_STRING &&
                          !strcmp(rr->items[i]->s, tbuf);
        else if (rr->type == AF_JSON_STRING)
            tone_ok = strstr(rr->s, tbuf) != NULL;     /* substring */
    }
    if (!rr || !tone_ok) {
        snprintf(msg, msgsz, "ramp/tone '%s.%s' does not exist in the style pack",
                 ramp, tbuf);
        return 1;
    }
    snprintf(tone, tsz, "%s", tbuf);
    return 0;
}

/* ---- shape checking ---------------------------------------------------- */

/* Result of one build_one()/compile_shapes() call: ShapeErr is reported
 * per shape by compile_shapes, ExprError propagates to the phase handler. */
typedef struct {
    int st;                     /* 0 ok, 1 ShapeErr, 2 ExprError */
    char code[24];
    char msg[768];
    af_json *obs, *exp;         /* owned while st == 1 */
} bres;

static void bres_clear(bres *r) {
    af_json_free(r->obs);
    af_json_free(r->exp);
    r->obs = r->exp = NULL;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)xoom(malloc(n + 1));
    if (p) memcpy(p, s, n + 1);
    return p;
}

static void shape_err(bres *r, const char *code, const char *msg,
                      af_json *obs, af_json *exp) {   /* consumes obs/exp */
    r->st = 1;
    snprintf(r->code, sizeof r->code, "%s", code);
    snprintf(r->msg, sizeof r->msg, "%s", msg);
    r->obs = obs;
    r->exp = exp;
}

static void expr_err(bres *r, const char *fmt, ...) {
    r->st = 2;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->msg, sizeof r->msg, fmt, ap);
    va_end(ap);
}

/* ValueError (a tuple/list unpack inside a shape): propagates like
 * ExprError but is caught only at the sites whose except clause names it. */
static void verr(bres *r, const char *fmt, ...) {
    r->st = 3;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->msg, sizeof r->msg, fmt, ap);
    va_end(ap);
}

typedef struct {
    int recipe;                 /* mode == "recipe" */
    const af_json *ramps, *swap, *layers, *anchors;
    int64_t axis;
    const char *base_dir;
    af_json *diags;
    const char *part;
    /* recipe-repeat transients: the repeat's own sh fields (NULL = None) */
    const af_json *t_anchor, *t_layer, *t_fill;
    /* propagated ExprError text for E_ARCHETYPE_EXPR / E_RECIPE_EXPR */
    char emsg[1024];
} gctx;

/* build_shape(): evaluate everything the oracle evaluates before it
 * builds geometry; the geometry itself is discarded (validate drops IR).
 * `fill` is the resolved-by-caller string (own or inherited from the
 * repeat). Order of evaluation is the oracle's, so errors surface
 * identically. */
static int build_shape(const af_json *sh, const af_json *env, gctx *g,
                       const char *fill, bres *r) {
    char msg[768], ramp[64], tone[64];
    if (resolve_fill(fill, env, g->ramps, g->swap, ramp, sizeof ramp,
                     tone, sizeof tone, msg, sizeof msg) == 1) {
        expr_err(r, "%s", msg);
        return 2;
    }
    int64_t deg = 0;
    const af_json *rot = af_json_get(sh, "rotate");
    if (rot) {
        int rc = ev_i(rot, env, &deg, msg, sizeof msg);
        if (rc) { expr_err(r, "%s", msg); return rc; }
    }
    if (deg < -180 || deg > 180) { expr_err(r, "rotate must be within -180..180 degrees"); return 2; }
    const char *kind = af_json_get(sh, "shape") &&
                       af_json_get(sh, "shape")->type == AF_JSON_STRING
                           ? af_json_get(sh, "shape")->s : NULL;
    if (!kind) { crash("bad shape"); return 2; }
    if (!strcmp(tone, "*") && strcmp(kind, "ellipse") && strcmp(kind, "capsule")) {
        expr_err(r, "only ellipse and capsule shapes can be shaded (fill 'ramp.*')");
        return 2;
    }
    int64_t vals[5];
    if (!strcmp(kind, "capsule")) {
        static const char *const keys[] = {"x1", "y1", "x2", "y2", "w"};
        for (int i = 0; i < 5; i++) {
            const af_json *e = af_json_get(sh, keys[i]);
            if (!e) { crash("bad shape"); return 2; }
            int rc = ev_i(e, env, &vals[i], msg, sizeof msg);
            if (rc) { expr_err(r, "%s", msg); return rc; }
        }
        if (vals[4] <= 0) return 0;        /* not drawn, no diagnostic */
        return 0;
    }
    if (!strcmp(kind, "ellipse")) {
        if (deg) { expr_err(r, "ellipse cannot be rotated"); return 2; }
        static const char *const keys[] = {"x", "y", "w", "h"};
        for (int i = 0; i < 4; i++) {
            const af_json *e = af_json_get(sh, keys[i]);
            if (!e) { crash("bad shape"); return 2; }
            int rc = ev_i(e, env, &vals[i], msg, sizeof msg);
            if (rc) { expr_err(r, "%s", msg); return rc; }
        }
        return 0;
    }
    if (!strcmp(kind, "rect")) {
        static const char *const keys[] = {"x", "y", "w", "h"};
        for (int i = 0; i < 4; i++) {
            const af_json *e = af_json_get(sh, keys[i]);
            if (!e) { crash("bad shape"); return 2; }
            int rc = ev_i(e, env, &vals[i], msg, sizeof msg);
            if (rc) { expr_err(r, "%s", msg); return rc; }
        }
        return 0;
    }
    /* polygon: unpack each point, then evaluate it (the oracle's list
     * comp unpacks element i before evaluating element i) */
    const af_json *pts = af_json_get(sh, "points");
    if (!pts) { crash("bad shape"); return 2; }        /* KeyError */
    if (pts->type != AF_JSON_ARRAY) { crash("bad shape"); return 2; }
    for (size_t i = 0; i < pts->n; i++) {
        const af_json *pt = pts->items[i];
        if (!pt || pt->type != AF_JSON_ARRAY) {
            verr(r, "cannot unpack non-iterable %s object", py_typename(pt));
            return 3;
        }
        if (pt->n != 2) {
            if (pt->n > 2) verr(r, "too many values to unpack (expected 2)");
            else verr(r, "not enough values to unpack (expected 2, got %zu)", pt->n);
            return 3;
        }
        for (int j = 0; j < 2; j++) {
            int rc = ev_i(pt->items[j], env, &vals[0], msg, sizeof msg);
            if (rc) { expr_err(r, "%s", msg); return rc < 0 ? 2 : rc; }
        }
    }
    return 0;
}

/* build_line(): rotate / endpoints / thickness / fill, then discard */
static int build_line(const af_json *sh, const af_json *env, gctx *g,
                      const char *fill, bres *r) {
    char msg[768], ramp[64], tone[64];
    int64_t deg = 0;
    const af_json *rot = af_json_get(sh, "rotate");
    if (rot) {
        int rc = ev_i(rot, env, &deg, msg, sizeof msg);
        if (rc) { expr_err(r, "%s", msg); return rc; }
        if (deg) {
            shape_err(r, "E_SHAPE_PARAM", "line cannot be rotated",
                      af_json_int(deg), af_json_int(0));
            return 1;
        }
    }
    static const char *const keys[] = {"x1", "y1", "x2", "y2"};
    for (int i = 0; i < 4; i++) {
        const af_json *e = af_json_get(sh, keys[i]);
        if (!e) { crash("bad shape"); return 2; }
        int rc = ev_i(e, env, &deg, msg, sizeof msg);
        if (rc) { expr_err(r, "%s", msg); return rc; }
    }
    int64_t th = 0;
    const af_json *tn = af_json_get(sh, "thickness");
    if (!tn) { crash("bad shape"); return 2; }
    int rc = ev_i(tn, env, &th, msg, sizeof msg);
    if (rc) { expr_err(r, "%s", msg); return rc; }
    if (th < 1 || th % 2 == 0) {
        char exp[32];
        snprintf(exp, sizeof exp, "odd >= 1");
        shape_err(r, "E_SHAPE_PARAM",
                  "line thickness must be a positive odd integer",
                  af_json_int(th), af_json_str(exp));
        return 1;
    }
    if (resolve_fill(fill, env, g->ramps, g->swap, ramp, sizeof ramp,
                     tone, sizeof tone, msg, sizeof msg) == 1) {
        expr_err(r, "%s", msg);
        return 2;
    }
    return 0;
}

/* shared px-file prelude: load + frame-select + legend resolve.
 * Appends its own diagnostics to g->diags (like the oracle) and returns
 * 1 when nothing may be drawn (already reported), 0 with px in hand. */
typedef struct {
    const af_px *px;            /* borrowed (caller frees the af_px) */
    int frame;
} pxuse;

static int px_prepare(const af_json *shfile, const char *path,
                      const af_json *frame_node, gctx *g, pxuse *use,
                      af_px **owned) {
    use->px = NULL;
    *owned = NULL;
    const char *file = shfile && shfile->type == AF_JSON_STRING ? shfile->s : NULL;
    if (!file) { crash("bad px"); return 1; }
    char fpath[1536];
    if (file[0] == '/')
        snprintf(fpath, sizeof fpath, "%s", file);       /* Path(base) / "/x" = "/x" */
    else if (g->base_dir && g->base_dir[0])
        snprintf(fpath, sizeof fpath, "%s/%s", g->base_dir, file);
    else
        snprintf(fpath, sizeof fpath, "%s", file);
    struct stat st;
    if (stat(fpath, &st) != 0) {
        char p[768], m[768];
        snprintf(p, sizeof p, "%s/file", path);
        snprintf(m, sizeof m, "fragment file not found: %s", file);
        diags_push(g->diags, D("E_PX_NOT_FOUND", p, m, NULL, NULL, NULL, NULL));
        return 1;
    }
    af_px *px = NULL;
    af_px_error err;
    if (af_px_read(fpath, &px, &err) != 0) {
        if (err.crash) { crash("px read"); return 1; }
        char p[768], m[1100];
        snprintf(p, sizeof p, "%s/file", path);
        snprintf(m, sizeof m, "%s line %d: %s", file, err.line, err.msg);
        diags_push(g->diags, D(err.code, p, m, NULL, NULL, NULL, NULL));
        return 1;
    }
    *owned = px;
    use->px = px;
    /* frame = sh.get("frame", 0); `0 <= frame < n` -- a non-number raises
     * TypeError (crash); an in-range float later raises TypeError in the
     * row indexing (also crash); an out-of-range float is REPORTED. */
    const af_json *fn2 = frame_node ? frame_node : NULL;
    double fd = 0;
    af_json *observed = NULL;
    if (!fn2) {
        observed = af_json_int(0);
    } else if (fn2->type == AF_JSON_INT) {
        observed = af_json_clone(fn2);
        fd = (double)fn2->i;
    } else if (fn2->type == AF_JSON_BOOL) {
        observed = af_json_clone(fn2);
        fd = fn2->b ? 1.0 : 0.0;
    } else if (fn2->type == AF_JSON_FLOAT) {
        observed = af_json_clone(fn2);
        fd = fn2->d;
    } else {
        af_px_free(px); *owned = NULL; use->px = NULL;
        crash("bad frame");
        return 1;
    }
    if (!observed) { crash("out of memory"); af_px_free(px); *owned = NULL; use->px = NULL; return 1; }
    if (!(fd >= 0 && fd < px->frame_count)) {
        char p[768], m[768], exp[64], t[64], tb[64];
        snprintf(t, sizeof t, "%s", py_str(observed, tb, sizeof tb));
        snprintf(p, sizeof p, "%s/frame", path);
        snprintf(m, sizeof m,
                 "fragment '%s' has %d frame(s), requested frame %s",
                 file, px->frame_count, t);
        snprintf(exp, sizeof exp, "0..%d", px->frame_count - 1);
        diags_push(g->diags,
                   D("E_PX_FRAMES", p, m, observed, af_json_str(exp), NULL, NULL));
        use->px = NULL;
        af_px_free(px);
        *owned = NULL;
        return 1;
    }
    if (fn2 && fn2->type == AF_JSON_FLOAT) {
        crash("bad frame");         /* px["rows"][0.5] -> TypeError */
        af_json_free(observed);
        af_px_free(px); *owned = NULL; use->px = NULL;
        return 1;
    }
    af_json_free(observed);
    use->frame = (int)fd;
    return 0;
}

/* legend entries -> (ramp,tone) pairs; appends E_PX_LEGEND like the
 * oracle and returns 1 when any entry failed (all are still checked) */
static int px_legend(const af_px *px, const char *file, const char *path,
                     gctx *g) {
    char msg[768], ramp[64], tone[64];
    int bad = 0;
    char p[768];
    snprintf(p, sizeof p, "%s/file", path);
    for (size_t i = 0; i < px->nleg; i++) {
        if (!strcmp(px->leg_val[i], "transparent")) continue;
        if (resolve_fill(px->leg_val[i], NULL, g->ramps, g->swap,
                         ramp, sizeof ramp, tone, sizeof tone,
                         msg, sizeof msg) == 1) {
            char m[1100];
            snprintf(m, sizeof m, "%s: legend '%s': %s", file, px->leg_ch[i], msg);
            diags_push(g->diags, D("E_PX_LEGEND", p, m, NULL, NULL, NULL, NULL));
            bad = 1;
        }
    }
    return bad;
}

/* build_px(): file + frame + legend checks only; the pixel list itself is
 * discarded (validate drops IR). */
static int build_px(const af_json *sh, gctx *g, const char *path, bres *r) {
    (void)r;
    const af_json *fn = af_json_get(sh, "file");
    if (!fn) { crash("bad px"); return 0; }
    pxuse use;
    af_px *owned = NULL;
    if (px_prepare(fn, path, af_json_get(sh, "frame"), g, &use, &owned))
        return 0;
    const char *file = fn->s;
    if (px_legend(owned, file, path, g)) {
        af_px_free(owned);
        return 0;
    }
    /* rows/attach/flip: pure geometry, no diagnostics after this point */
    af_px_free(owned);
    return 0;
}

/* ---- shape_layer / shape_anchor ---------------------------------------- */

/* shape_layer(sh, g): membership + presence; the layer name is heap (*out) */
static int shape_layer_of(const af_json *sh, gctx *g, char **out, bres *r) {
    const af_json *v = af_json_get(sh, "layer");
    if (!v) v = g->t_layer;
    if (!v || v->type == AF_JSON_NULL) { expr_err(r, "shape has no layer"); return 2; }
    char lbuf[64];
    const char *lname = v->type == AF_JSON_STRING ? v->s : py_str(v, lbuf, sizeof lbuf);
    int found = 0;
    if (g->layers && g->layers->type == AF_JSON_ARRAY)
        for (size_t i = 0; i < g->layers->n; i++)
            if (g->layers->items[i]->type == AF_JSON_STRING &&
                !strcmp(g->layers->items[i]->s, lname)) { found = 1; break; }
    if (!found) { expr_err(r, "unknown layer '%s'", lname); return 2; }
    *out = xstrdup(lname);
    if (!*out) { crash("out of memory"); return 2; }
    return 0;
}

/* shape_anchor(sh, g): same for anchors; 1 with *adef unset when no anchor
 * resolves (the caller raises "shape has no anchor" only where Python does). */
static int shape_anchor_of(const af_json *sh, gctx *g, const af_json **adef,
                           bres *r) {
    const af_json *v = af_json_get(sh, "anchor");
    if (!v) v = g->t_anchor;
    if (!v || v->type == AF_JSON_NULL) { expr_err(r, "shape has no anchor"); return 2; }
    char abuf[64];
    const char *aname = v->type == AF_JSON_STRING ? v->s : py_str(v, abuf, sizeof abuf);
    const af_json *a = g->anchors ? af_json_get(g->anchors, aname) : NULL;
    if (!a) { expr_err(r, "unknown anchor '%s'", aname); return 2; }
    *adef = a;
    return 0;
}

/* ---- compile_shapes / build_one / build_repeat -------------------------- */

static int build_one(const af_json *sh, const af_json *env, gctx *g,
                     const char *path, int depth, bres *r);
static int build_repeat(const af_json *sh, const af_json *env, gctx *g,
                        const char *path, int depth, bres *r);

/* compile_shapes(shapes, env, g, path, depth): ShapeErr is reported per
 * shape (the list keeps compiling); ExprError propagates (st == 2) to the
 * phase handler, which formats E_ARCHETYPE_EXPR / E_RECIPE_EXPR. */
static int compile_shapes(const af_json *shapes, const af_json *env, gctx *g,
                          const char *path, int depth) {
    if (!shapes) { crash("bad shapes"); return 2; }
    /* Python: `enumerate(shapes)`. A dict or str IS iterable: empty ones
     * iterate zero times (success, no shapes); a non-empty one feeds its
     * keys / characters to build_one, which does sh.get(...) on a str ->
     * AttributeError -> an uncaught traceback == our crash(). Everything
     * else (int/float/bool/null) raises TypeError == crash() too. */
    if (shapes->type == AF_JSON_OBJECT || shapes->type == AF_JSON_STRING) {
        if (shapes->type == AF_JSON_OBJECT ? shapes->n == 0 : shapes->slen == 0)
            return 0;
        crash("bad shapes");
        return 2;
    }
    if (shapes->type != AF_JSON_ARRAY) { crash("bad shapes"); return 2; }
    for (size_t i = 0; i < shapes->n; i++) {
        char p[1536];
        snprintf(p, sizeof p, "%s/%zu", path, i);
        bres r;
        memset(&r, 0, sizeof r);
        int st = build_one(shapes->items[i], env, g, p, depth, &r);
        if (st && !r.st) r.st = st;     /* propagated from a nested list */
        if (r.st == 1) {
            diags_push(g->diags, D(r.code, p, r.msg, opt(r.obs), opt(r.exp), NULL, NULL));
            bres_clear(&r);
        } else if (r.st == 2 || r.st == 3) {
            snprintf(g->emsg, sizeof g->emsg, "%s", r.msg);
            bres_clear(&r);
            return r.st;
        }
    }
    return 0;
}

/* build_repeat(): E_REPEAT_BOUND checks then `count` iterations of the
 * child list; each iteration binds var and $i in a cloned env. The tail of
 * appended diagnostics is deduplicated by JSON equality (the oracle's
 * json.dumps(sort_keys=True) key) -- ONLY when the loop completes: an
 * ExprError raised mid-loop propagates before the dedup runs. */
static int build_repeat(const af_json *sh, const af_json *env, gctx *g,
                        const char *path, int depth, bres *r) {
    char msg[1024];
    if (depth >= 2) {
        shape_err(r, "E_REPEAT_BOUND", "repeat nesting exceeds 2 levels",
                  af_json_int(depth + 1), af_json_str("<= 2"));
        return 1;
    }
    const af_json *mxn = af_json_get(sh, "max");
    if (!mxn) { crash("bad repeat"); return 2; }
    int64_t mx = 0;
    if (ev_i(mxn, env, &mx, msg, sizeof msg)) { expr_err(r, "%s", msg); return 2; }
    if (mx < 0 || mx > 256) {
        shape_err(r, "E_REPEAT_BOUND", "repeat max must be within 0..256",
                  af_json_int(mx), af_json_str("0..256"));
        return 1;
    }
    const af_json *cn = af_json_get(sh, "count");
    if (!cn) { crash("bad repeat"); return 2; }
    int64_t cnt = 0;
    if (ev_i(cn, env, &cnt, msg, sizeof msg)) { expr_err(r, "%s", msg); return 2; }
    if (cnt < 0 || cnt > mx) {
        char exp[64];
        snprintf(exp, sizeof exp, "0..%lld", (long long)mx);
        shape_err(r, "E_REPEAT_BOUND", "repeat count must be within 0..max",
                  af_json_int(cnt), af_json_str(exp));
        return 1;
    }
    const af_json *vn = af_json_get(sh, "var");
    if (!vn) { crash("bad repeat"); return 2; }
    if (vn->type == AF_JSON_ARRAY || vn->type == AF_JSON_OBJECT) {
        crash("type error");        /* unhashable dict key */
        return 2;
    }
    int64_t sx = 0, sy = 0;
    const af_json *sxn = af_json_get(sh, "step_x");
    if (sxn && ev_i(sxn, env, &sx, msg, sizeof msg)) { expr_err(r, "%s", msg); return 2; }
    const af_json *syn = af_json_get(sh, "step_y");
    if (syn && ev_i(syn, env, &sy, msg, sizeof msg)) { expr_err(r, "%s", msg); return 2; }

    /* child_g = dict(g) with the repeat's own transients in recipe mode */
    gctx cg = *g;
    if (g->recipe) {
        cg.t_anchor = af_json_get(sh, "anchor");
        cg.t_layer = af_json_get(sh, "layer");
        cg.t_fill = af_json_get(sh, "fill");
    }
    char childpath[2100];
    snprintf(childpath, sizeof childpath, "%s/shapes", path);

    size_t n0 = g->diags->n;
    for (int64_t i = 0; i < cnt; i++) {
        if (g_crash) return 2;
        if (!env) { crash("type error"); return 2; }   /* dict(None) */
        af_json *cenv = af_json_clone(env);
        if (!cenv) { crash("out of memory"); return 2; }
        if (vn->type == AF_JSON_STRING) af_json_obj_set(cenv, vn->s, af_json_int(i));
        af_json_obj_set(cenv, "i", af_json_int(i));
        int rc = compile_shapes(af_json_get(sh, "shapes"), cenv, &cg,
                                childpath, depth + 1);
        af_json_free(cenv);
        if (rc) return rc;          /* propagate: no dedup, Python never reaches it */
    }

    /* dedup the appended tail, keeping first occurrences */
    size_t keep = n0;
    for (size_t i = n0; i < g->diags->n; i++) {
        int dup = 0;
        for (size_t j = n0; j < i; j++)
            if (af_json_equal(g->diags->items[i], g->diags->items[j])) { dup = 1; break; }
        if (dup) { af_json_free(g->diags->items[i]); g->diags->items[i] = NULL; }
        else { g->diags->items[keep++] = g->diags->items[i]; }
    }
    g->diags->n = keep;
    return 0;
}

/* build_one(): kind -> when -> mirror/rotate -> (repeat | layer/anchor ->
 * px | fill -> line/shape). Geometry is discarded; every expression the
 * oracle evaluates is evaluated, in its order, so errors surface alike. */
static int build_one(const af_json *sh, const af_json *env, gctx *g,
                     const char *path, int depth, bres *r) {
    (void)path;
    if (!sh || sh->type != AF_JSON_OBJECT) { crash("bad shape"); return 2; }
    const af_json *kn = af_json_get(sh, "shape");
    if (!kn) { crash("bad shape"); return 2; }         /* KeyError */
    const char *kind = kn->type == AF_JSON_STRING ? kn->s : NULL;

    const af_json *wn = af_json_get(sh, "when");
    if (wn) {
        int is_err = 0;
        char msg[1024];
        af_json *v = ev_v(wn, env, &is_err, msg, sizeof msg);
        if (!v) { expr_err(r, "%s", msg); return 2; }
        int t = af_truthy(v);
        af_json_free(v);
        if (!t) return 0;
    }
    if (af_json_get(sh, "mirror") && af_json_get(sh, "rotate")) {
        shape_err(r, "E_SHAPE_PARAM", "mirror cannot be combined with rotate",
                  NULL, NULL);
        return 1;
    }

    if (kind && !strcmp(kind, "repeat")) {
        if (g->recipe) {
            char *lname = NULL;
            if (shape_layer_of(sh, g, &lname, r)) return r->st;
            free(lname);
            const af_json *a = NULL;
            if (shape_anchor_of(sh, g, &a, r)) return r->st;
        }
        return build_repeat(sh, env, g, path, depth, r);
    }

    char *lname = NULL;
    if (shape_layer_of(sh, g, &lname, r)) return r->st;
    const af_json *at = NULL;
    if (g->recipe) {
        const af_json *a = NULL;
        if (shape_anchor_of(sh, g, &a, r)) { free(lname); return r->st; }
        at = af_json_get(a, "at");
        if (!at || !af_json_get(a, "part")) { crash("bad anchor"); free(lname); return 2; }
    } else if (af_json_get(sh, "anchor")) {
        const af_json *a = NULL;
        if (shape_anchor_of(sh, g, &a, r)) { free(lname); return r->st; }
        at = af_json_get(a, "at");
        if (!at || !af_json_get(a, "part")) { crash("bad anchor"); free(lname); return 2; }
    }
    (void)at;

    int rc;
    if (kind && !strcmp(kind, "px")) {
        rc = build_px(sh, g, path, r);
    } else {
        const af_json *filln = af_json_get(sh, "fill");
        if (!filln) filln = g->t_fill;
        if (!filln || filln->type == AF_JSON_NULL) {
            shape_err(r, "E_SHAPE_PARAM",
                      "shape has no fill and its repeat declares none", NULL, NULL);
            free(lname);
            return 1;
        }
        if (filln->type != AF_JSON_STRING) { crash("bad fill"); free(lname); return 2; }
        if (kind && !strcmp(kind, "line")) rc = build_line(sh, env, g, filln->s, r);
        else rc = build_shape(sh, env, g, filln->s, r);
    }
    free(lname);
    return rc;
}

/* ---- compile_asset ------------------------------------------------------ */

static int has_error(const af_json *d) {
    for (size_t i = 0; i < d->n; i++) {
        const af_json *s = af_json_get(d->items[i], "severity");
        if (s && s->type == AF_JSON_STRING && !strcmp(s->s, "error")) return 1;
    }
    return 0;
}

/* ['a', 'b'] -- f-string of a sorted list of strings (E_PARAM_SHADOW) */
static void strlist_repr(const af_json *arr, char *buf, size_t sz) {
    size_t n = 0;
    buf[n++] = '[';
    for (size_t i = 0; i < arr->n && n + 8 < sz; i++) {
        if (i) { buf[n++] = ','; buf[n++] = ' '; }
        const af_json *it = arr->items[i];
        if (it->type == AF_JSON_STRING) {
            buf[n++] = '\'';
            for (const char *q = it->s; *q && n + 3 < sz; q++) {
                if (*q == '\'' || *q == '\\') buf[n++] = '\\';
                buf[n++] = *q;
            }
            buf[n++] = '\'';
        } else {
            char tb[64];
            const char *s = py_str(it, tb, sizeof tb);
            size_t l = strlen(s);
            if (n + l + 1 < sz) { memcpy(buf + n, s, l); n += l; }
        }
    }
    buf[n++] = ']';
    buf[n] = 0;
}

/* a two-integer array ([W, H]) */
static af_json *pair2(int64_t a, int64_t b) {
    af_json *v = af_json_arr();
    if (!v) { crash("out of memory"); return NULL; }
    af_json_arr_push(v, af_json_int(a));
    af_json_arr_push(v, af_json_int(b));
    return v;
}

/* num4(lo)..num4(hi) */
static void num4_range(const af_json *lo, const af_json *hi, char *buf, size_t sz) {
    char a[64], b[64];
    num4_str(lo, a, sizeof a);
    num4_str(hi, b, sizeof b);
    snprintf(buf, sz, "%s..%s", a, b);
}

/* {W,H} + size_env + class/class_idx + proportions[cls] -- the oracle's menv
 * and every frame-state fmenv (no diagnostics: selection cannot fail). */
static af_json *menv_build(const af_json *arch, int64_t W, int64_t H,
                           const af_json *size_specs, const af_json *size_env) {
    af_json *m = af_json_obj();
    if (!m) { crash("out of memory"); return NULL; }
    af_json_obj_set(m, "W", af_json_int(W));
    af_json_obj_set(m, "H", af_json_int(H));
    if (size_env)
        for (size_t i = 0; i < size_env->n / 2; i++)
            af_json_obj_set(m, size_env->items[2 * i]->s,
                            af_json_clone(size_env->items[2 * i + 1]));
    const af_json *classes = af_json_get(arch, "classes");
    const char *cls_name = "default";
    int64_t cls_idx = 0;
    if (size_specs && size_specs->n && classes && classes->n) {
        if (classes->type != AF_JSON_ARRAY) { crash("bad document"); af_json_free(m); return NULL; }
        const af_json *prim = size_specs->items[0];
        if (prim->type != AF_JSON_STRING) { crash("bad document"); af_json_free(m); return NULL; }
        const af_json *svn = af_json_get(size_env, prim->s);
        if (!svn || svn->type != AF_JSON_INT) { crash("bad document"); af_json_free(m); return NULL; }
        int64_t sv = svn->i;
        const af_json *pick = NULL;
        int64_t best = 0;
        for (size_t i = 0; i < classes->n; i++) {
            const af_json *mx = need(classes->items[i], "max");
            if (!mx || mx->type != AF_JSON_INT) { crash("bad document"); af_json_free(m); return NULL; }
            if (sv <= mx->i && (!pick || mx->i < best)) { pick = classes->items[i]; best = mx->i; }
        }
        if (!pick)
            for (size_t i = 0; i < classes->n; i++) {
                const af_json *mx = need(classes->items[i], "max");
                if (!mx || mx->type != AF_JSON_INT) { crash("bad document"); af_json_free(m); return NULL; }
                if (!pick || mx->i > best) { pick = classes->items[i]; best = mx->i; }
            }
        if (pick) {
            const af_json *nm = need(pick, "name");
            if (!nm || nm->type != AF_JSON_STRING) { crash("bad document"); af_json_free(m); return NULL; }
            cls_name = nm->s;
            for (size_t i = 0; i < classes->n; i++)
                if (af_json_equal(classes->items[i], pick)) { cls_idx = (int64_t)i; break; }
        }
    }
    af_json_obj_set(m, "class", af_json_str(cls_name));
    af_json_obj_set(m, "class_idx", af_json_int(cls_idx));
    const af_json *props = af_json_get(arch, "proportions");
    if (props) {
        if (props->type != AF_JSON_OBJECT) { crash("bad document"); af_json_free(m); return NULL; }
        const af_json *row = af_json_get(props, cls_name);
        if (row) {
            if (row->type != AF_JSON_OBJECT) { crash("bad document"); af_json_free(m); return NULL; }
            for (size_t i = 0; i < row->n / 2; i++)
                af_json_obj_set(m, row->items[2 * i]->s, af_json_clone(row->items[2 * i + 1]));
        }
    }
    if (g_crash) { af_json_free(m); return NULL; }
    return m;
}

/* one entry of compile_asset's used_recs */
typedef struct {
    char *fp;
    const char *name;
    const af_json *rec;
    const af_json *given;      /* feature params, NULL = {} */
} urec;

typedef struct { char *slot; size_t idx; const char *name; } uslot;
typedef struct { const char *tname; const af_json *tpl; } bnd;

/* everything track_env/make_state need from compile_asset */
typedef struct {
    const af_json *asset, *arch, *style;
    const af_json *size_specs;      /* NULL when the archetype is unsized */
    const af_json *size_env;
    const af_json *arch_params;
    const af_json *ramps, *layers, *anchors;
    const af_json *swap;            /* NULL when no palette_swap */
    const char *base_dir;
    int64_t W, H;
    af_json *diags;
    urec *urecs;
    size_t nurecs;
} cas;

/* sorted keys of an object as a C array (caller frees) */
static char **sorted_keys(const af_json *obj, size_t *n_out) {
    *n_out = 0;
    if (!obj || obj->type != AF_JSON_OBJECT || !obj->n) return NULL;
    char **ks = (char **)malloc(obj->n * sizeof *ks);
    if (!ks) { crash("out of memory"); return NULL; }
    for (size_t i = 0; i < obj->n / 2; i++) ks[i] = obj->items[2 * i]->s;
    for (size_t i = 1; i < obj->n / 2; i++) { /* insertion sort */
        char *key = ks[i];
        size_t j = i;
        while (j && strcmp(ks[j - 1], key) > 0) { ks[j] = ks[j - 1]; j--; }
        ks[j] = key;
    }
    *n_out = obj->n / 2;
    return ks;
}

/* track_env(track, tpath): (fenv, recipe_track) -- returns -1 on crash */
static int track_env(cas *c, const af_json *track, const char *tpath,
                     af_json **fenv_out, af_json **frec_out) {
    *fenv_out = NULL;
    *frec_out = af_json_obj();
    if (!*frec_out) { crash("out of memory"); return -1; }
    if (track && track->type != AF_JSON_OBJECT) { crash("bad document"); return -1; }
    af_json *fsize = c->size_env ? af_json_clone(c->size_env) : af_json_obj();
    af_json *arch_track = af_json_obj();
    if (!fsize || !arch_track) { crash("out of memory"); return -1; }
    size_t nk = 0;
    char **ks = sorted_keys(track, &nk);
    if (nk && !ks) return -1;
    for (size_t i = 0; i < nk; i++) {
        const char *k = ks[i];
        const af_json *v = af_json_get(track, k);
        if (c->size_specs && af_json_get(c->size_specs, k)) {
            if (!v || v->type != AF_JSON_INT) {
                char p[768], m[256];
                snprintf(p, sizeof p, "%s", tpath);
                ptr_cat(p, sizeof p, "params");
                ptr_cat(p, sizeof p, k);
                snprintf(m, sizeof m, "size track '%s' must be an integer", k);
                diags_push(c->diags, D("E_TRACK_PARAM", p, m, opt(v),
                                       af_json_str("integer"), NULL, NULL));
                if (g_crash) return -1;
                continue;
            }
            const af_json *sp = af_json_get(c->size_specs, k);
            const af_json *lo = need(sp, "min"), *hi = need(sp, "max");
            if (!lo || !hi) return -1;
            int64_t vv = v->i < lo->i ? lo->i : (v->i > hi->i ? hi->i : v->i);
            af_json_obj_set(fsize, k, af_json_int(vv));
        } else if (c->arch_params && af_json_get(c->arch_params, k)) {
            af_json_obj_set(arch_track, k, af_json_clone(v));
        } else {
            af_json_obj_set(*frec_out, k, af_json_clone(v));
        }
    }
    free(ks);

    af_json *fmenv = menv_build(c->arch, c->W, c->H, c->size_specs, fsize);
    af_json_free(fsize);
    if (!fmenv) return -1;
    /* given = asset params, then the archetype track values overwrite */
    const af_json *apn = af_json_get(need(c->asset, "archetype"), "params");
    if (apn && apn->type != AF_JSON_OBJECT) { crash("bad document"); return -1; }
    af_json *given = af_json_obj();
    if (!given) { crash("out of memory"); return -1; }
    if (apn)
        for (size_t i = 0; i < apn->n / 2; i++)
            af_json_obj_set(given, apn->items[2 * i]->s, af_json_clone(apn->items[2 * i + 1]));
    for (size_t i = 0; i < arch_track->n / 2; i++)
        af_json_obj_set(given, arch_track->items[2 * i]->s,
                        af_json_clone(arch_track->items[2 * i + 1]));
    af_json_free(arch_track);
    char pbase[768];
    snprintf(pbase, sizeof pbase, "%s", tpath);
    ptr_cat(pbase, sizeof pbase, "params");
    af_json *fparams = check_params(c->arch_params, given, pbase, c->style,
                                    c->diags, "frame track", fmenv);
    af_json_free(given);
    if (!fparams) { af_json_free(fmenv); return -1; }
    af_json *fenv = fmenv;
    for (size_t i = 0; i < fparams->n / 2; i++)
        af_json_obj_set(fenv, fparams->items[2 * i]->s,
                        af_json_clone(fparams->items[2 * i + 1]));
    af_json_free(fparams);
    /* metrics at this state's env */
    const af_json *metrics = af_json_get(c->arch, "metrics");
    if (metrics) {
        if (metrics->type != AF_JSON_ARRAY) { crash("bad document"); af_json_free(fenv); return -1; }
        for (size_t i = 0; i < metrics->n; i++) {
            const af_json *mdef = metrics->items[i];
            if (!mdef || mdef->type != AF_JSON_ARRAY || mdef->n < 2 ||
                mdef->items[0]->type != AF_JSON_STRING) {
                crash("bad document");
                af_json_free(fenv);
                return -1;
            }
            const char *mname = mdef->items[0]->s;
            int64_t outv = 0;
            char msg[1024];
            int rc = ev_i(mdef->items[1], fenv, &outv, msg, sizeof msg);
            if (rc == 1) {
                char p[768], m[1100];
                snprintf(p, sizeof p, "%s", tpath);
                ptr_cat(p, sizeof p, "params");
                snprintf(m, sizeof m,
                         "metric '%s' failed to evaluate in the frame state: %s",
                         mname, msg);
                diags_push(c->diags, D("E_ARCHETYPE_EXPR", p, m, NULL, NULL, NULL, NULL));
                af_json_obj_set(fenv, mname, af_json_int(0));
                if (g_crash) { af_json_free(fenv); return -1; }
            } else if (rc != 0) {
                af_json_free(fenv);
                return -1;
            } else {
                af_json_obj_set(fenv, mname, af_json_int(outv));
            }
        }
    }
    *fenv_out = fenv;
    return 0;
}

/* make_state(track, smap, tpath): the drawable set is discarded; every
 * diagnostic the oracle would append on the way is appended here. */
static void make_state(cas *c, const af_json *track, const af_json *smap,
                       const char *tpath) {
    af_json *fenv = NULL, *frec = NULL;
    if (track_env(c, track, tpath, &fenv, &frec)) return;
    if (smap && smap->type != AF_JSON_OBJECT) {
        crash("bad document");
        af_json_free(fenv); af_json_free(frec);
        return;
    }
    /* mirror axis re-derived at this state's env */
    const af_json *axis_name_n = af_json_get(c->arch, "mirror_axis");
    const char *axis_name = axis_name_n && axis_name_n->type == AF_JSON_STRING
                                ? axis_name_n->s : "cx";
    const af_json *av = af_json_get(fenv, axis_name);
    int64_t faxis = (av && av->type == AF_JSON_INT) ? av->i : c->W / 2;

    gctx g;
    memset(&g, 0, sizeof g);
    g.recipe = 0;
    g.ramps = c->ramps;
    g.swap = c->swap;
    g.axis = faxis;
    g.layers = c->layers;
    g.base_dir = c->base_dir;
    g.diags = c->diags;
    g.anchors = c->anchors;
    const af_json *parts = need(c->arch, "parts");
    if (g_crash) { af_json_free(fenv); af_json_free(frec); return; }
    for (size_t pi = 0; pi < parts->n / 2; pi++) {
        const char *part = parts->items[2 * pi]->s;
        const af_json *pdef = parts->items[2 * pi + 1];
        const af_json *shapes;
        char gpath[768];
        gpath[0] = 0;
        ptr_cat(gpath, sizeof gpath, "archetype");
        ptr_cat(gpath, sizeof gpath, "parts");
        ptr_cat(gpath, sizeof gpath, part);
        const af_json *smap_v = smap ? af_json_get(smap, part) : NULL;
        if (smap_v) {
            shapes = need(pdef, "variants");
            if (shapes) shapes = af_json_get(shapes, smap_v->type == AF_JSON_STRING
                                                    ? smap_v->s : "");
            if (!shapes) { crash("bad document"); break; }
            ptr_cat(gpath, sizeof gpath, "variants");
            ptr_cat(gpath, sizeof gpath,
                    smap_v->type == AF_JSON_STRING ? smap_v->s : "");
        } else {
            shapes = need(pdef, "shapes");
        }
        ptr_cat(gpath, sizeof gpath, "shapes");
        g.part = part;
        int rc = compile_shapes(shapes, fenv, &g, gpath, 0);
        if (rc == 2 || rc == 3) {
            char m[1100];
            snprintf(m, sizeof m, "archetype geometry failed in the frame state: %s", g.emsg);
            diags_push(c->diags, D("E_ARCHETYPE_EXPR", tpath, m, NULL, NULL, NULL, NULL));
        }
        if (g_crash) break;
    }
    /* recipe features rebuilt in this state's env */
    gctx gr;
    memset(&gr, 0, sizeof gr);
    gr.recipe = 1;
    gr.ramps = c->ramps;
    gr.swap = c->swap;
    gr.axis = faxis;
    gr.layers = c->layers;
    gr.base_dir = c->base_dir;
    gr.diags = c->diags;
    gr.anchors = c->anchors;
    for (size_t ui = 0; ui < c->nurecs && !g_crash; ui++) {
        urec *u = &c->urecs[ui];
        const af_json *rec = u->rec;
        /* rgiven = feature params, then frec entries this recipe declares */
        af_json *rgiven = u->given ? af_json_clone(u->given) : af_json_obj();
        const af_json *recpars = need(rec, "parameters");
        if (!rgiven) { crash("out of memory"); break; }
        if (frec)
            for (size_t i = 0; i < frec->n / 2; i++)
                if (recpars && af_json_get(recpars, frec->items[2 * i]->s))
                    af_json_obj_set(rgiven, frec->items[2 * i]->s,
                                    af_json_clone(frec->items[2 * i + 1]));
        size_t before = c->diags->n;
        char pbase[768];
        snprintf(pbase, sizeof pbase, "%s", tpath);
        ptr_cat(pbase, sizeof pbase, "params");
        char label[256];
        snprintf(label, sizeof label, "frame track of recipe '%s'", u->name);
        af_json *rparams = check_params(recpars, rgiven, pbase, c->style,
                                        c->diags, label, fenv);
        af_json_free(rgiven);
        if (!rparams) break;
        if (c->diags->n > before) { af_json_free(rparams); continue; }
        af_json *renv = af_json_clone(fenv);
        if (!renv) { crash("out of memory"); af_json_free(rparams); break; }
        for (size_t i = 0; i < rparams->n / 2; i++)
            af_json_obj_set(renv, rparams->items[2 * i]->s,
                            af_json_clone(rparams->items[2 * i + 1]));
        af_json_free(rparams);
        const af_json *geo = need(rec, "geometry");
        const af_json *geo_used = geo;
        if (geo && geo->type == AF_JSON_OBJECT) {
            geo_used = af_json_get(geo, need_str(c->arch, "archetype")->s);
            if (!geo_used) { af_json_free(renv); crash("bad document"); break; }
        }
        char gpath[768];
        snprintf(gpath, sizeof gpath, "%s", u->fp);
        ptr_cat(gpath, sizeof gpath, "geometry");
        if (geo && geo->type == AF_JSON_OBJECT) {
            const af_json *an = need_str(c->arch, "archetype");
            ptr_cat(gpath, sizeof gpath, an ? an->s : "");
        }
        /* locals: one try around the whole list */
        int local_fail = 0;
        char local_msg[1024] = "";
        const af_json *locs = af_json_get(rec, "locals");
        if (locs) {
            if (locs->type != AF_JSON_ARRAY) { crash("bad document"); af_json_free(renv); break; }
            for (size_t li = 0; li < locs->n && !local_fail; li++) {
                const af_json *e = locs->items[li];
                char msg[1024];
                msg[0] = 0;
                if (!e || e->type != AF_JSON_ARRAY || e->n != 2 ||
                    e->items[0]->type != AF_JSON_STRING) {
                    if (!e || e->type != AF_JSON_ARRAY)
                        snprintf(msg, sizeof msg, "cannot unpack non-iterable %s object",
                                 py_typename(e));
                    else if (e->n > 2)
                        snprintf(msg, sizeof msg, "too many values to unpack (expected 2)");
                    else
                        snprintf(msg, sizeof msg,
                                 "not enough values to unpack (expected 2, got %zu)", e->n);
                    local_fail = 1;
                } else {
                    int64_t outv = 0;
                    int rc = ev_i(e->items[1], renv, &outv, msg, sizeof msg);
                    if (rc == 1) local_fail = 1;
                    else if (rc != 0) { af_json_free(renv); break; }
                    else af_json_obj_set(renv, e->items[0]->s, af_json_int(outv));
                }
                if (local_fail) snprintf(local_msg, sizeof local_msg, "%s", msg);
            }
        }
        if (g_crash) { af_json_free(renv); break; }
        if (local_fail) {
            char m[1100];
            snprintf(m, sizeof m, "recipe '%s' local failed in the frame state: %s",
                     u->name, local_msg);
            diags_push(c->diags, D("E_RECIPE_EXPR", tpath, m, NULL, NULL, NULL, NULL));
            af_json_free(renv);
            continue;
        }
        int rc = compile_shapes(geo_used, renv, &gr, gpath, 0);
        af_json_free(renv);
        if (rc == 2 || rc == 3) {       /* the oracle catches (ExprError, ValueError) */
            char m[1100];
            snprintf(m, sizeof m, "recipe '%s' failed to evaluate in the frame state: %s",
                     u->name, gr.emsg);
            diags_push(c->diags, D("E_RECIPE_EXPR", tpath, m, NULL, NULL, NULL, NULL));
        }
        if (g_crash) break;
    }
    af_json_free(fenv);
    af_json_free(frec);
}

/* ---- number comparisons (Python semantics) ------------------------------ */

static int num_node(const af_json *v) {
    return v && (v->type == AF_JSON_INT || v->type == AF_JSON_BOOL ||
                 v->type == AF_JSON_FLOAT);
}

static double num_double(const af_json *v) {
    if (v->type == AF_JSON_INT) return (double)v->i;
    if (v->type == AF_JSON_BOOL) return v->b ? 1.0 : 0.0;
    return v->d;
}

static int64_t num_int(const af_json *v) {
    if (v->type == AF_JSON_BOOL) return v->b ? 1 : 0;
    if (v->type == AF_JSON_INT) return v->i;
    return (int64_t)v->d;
}

/* a < b / a > b over Python numbers; any other operand is TypeError */
static int py_lt(const af_json *a, const af_json *b) {
    if (!num_node(a) || !num_node(b)) { crash("bad document"); return 0; }
    if (a->type != AF_JSON_FLOAT && b->type != AF_JSON_FLOAT)
        return num_int(a) < num_int(b);
    return num_double(a) < num_double(b);
}

static int py_gt(const af_json *a, const af_json *b) {
    if (!num_node(a) || !num_node(b)) { crash("bad document"); return 0; }
    if (a->type != AF_JSON_FLOAT && b->type != AF_JSON_FLOAT)
        return num_int(a) > num_int(b);
    return num_double(a) > num_double(b);
}

/* max(lo, min(hi, v)) -- Python's min/max return one of their OPERANDS,
 * so a bound node wins over an equal bool/float exactly as theirs does */
static af_json *py_clamp(const af_json *v, const af_json *lo, const af_json *hi) {
    const af_json *m = py_lt(v, hi) ? v : hi;
    const af_json *c = py_gt(m, lo) ? m : lo;
    return af_json_clone(c);
}

/* `x in c`: objects by key, arrays by ==, strings by substring; a
 * non-container is TypeError (crash) */
static int in_cont(const af_json *c, const af_json *x) {
    if (!c) return 0;
    if (c->type == AF_JSON_OBJECT) {
        if (x->type == AF_JSON_ARRAY || x->type == AF_JSON_OBJECT) {
            crash("type error");
            return 0;
        }
        char b[64];
        return af_json_get(c, py_str(x, b, sizeof b)) != NULL;
    }
    if (c->type == AF_JSON_ARRAY) {
        for (size_t i = 0; i < c->n; i++)
            if (af_json_equal(c->items[i], x)) return 1;
        return 0;
    }
    if (c->type == AF_JSON_STRING && x->type == AF_JSON_STRING)
        return strstr(c->s, x->s) != NULL;
    crash("type error");
    return 0;
}

/* a parameter spec is overridable by a frame track when its type is one
 * of the four track kinds (a missing/other type is not) */
static int track_kind(const af_json *spec) {
    const af_json *t = spec ? af_json_get(spec, "type") : NULL;
    if (!t || t->type != AF_JSON_STRING) return 0;
    return !strcmp(t->s, "int") || !strcmp(t->s, "bool") ||
           !strcmp(t->s, "enum") || !strcmp(t->s, "percent");
}

static void urecs_free(urec *u, size_t n) {
    if (!u) return;
    for (size_t i = 0; i < n; i++) free(u[i].fp);
    free(u);
}

static void slots_free(uslot *s, size_t n) {
    if (!s) return;
    for (size_t i = 0; i < n; i++) free(s[i].slot);
    free(s);
}

/* set(params) & set(env), sorted -- the E_PARAM_SHADOW clash list */
static af_json *clash_set(const af_json *params, const af_json *env) {
    af_json *out = af_json_arr();
    if (!out) { crash("out of memory"); return NULL; }
    for (size_t i = 0; params && i < params->n / 2; i++) {
        const char *k = params->items[2 * i]->s;
        if (af_json_get(env, k)) af_json_arr_push(out, af_json_str(k));
    }
    for (size_t i = 1; i < out->n; i++) {          /* sorted(clash) */
        af_json *key = out->items[i];
        size_t j = i;
        while (j && strcmp(out->items[j - 1]->s, key->s) > 0) {
            out->items[j] = out->items[j - 1];
            j--;
        }
        out->items[j] = key;
    }
    if (g_crash) { af_json_free(out); return NULL; }
    return out;
}

/* compile_asset(asset, reg, base_dir): the oracle's compile_asset
 * diagnostics, in order. NULL means the oracle would traceback (exit 1,
 * no stdout). The IR Python returns (drawables, states, hitboxes) is
 * discarded: validate never prints it. */
static af_json *compile_asset(const af_json *asset, const af_json *reg,
                              const char *base_dir) {
    af_json *diags = schema_diags(asset, "asset.schema.json", "");
    if (!diags) return NULL;
    if (diags->n) return diags;

    /* ---- style pack / archetype lookup --------------------------------- */
    const af_json *style_name = need_str(asset, "style_pack");
    const af_json *archnode = need(asset, "archetype");
    const af_json *arch_name = archnode ? need_str(archnode, "name") : NULL;
    const af_json *styles = need(reg, "styles");
    const af_json *arches = need(reg, "archetypes");
    const af_json *recipes = need(reg, "recipes");
    if (g_crash) { af_json_free(diags); return NULL; }

    const af_json *style = af_json_get(styles, style_name->s);
    if (!style) {
        char m[768];
        snprintf(m, sizeof m, "no style pack '%s'", style_name->s);
        diags_push(diags, D("E_UNKNOWN_STYLE_PACK", "/style_pack", m, NULL,
                            sorted_strs(styles), close_arr(style_name, styles), NULL));
    }
    const af_json *arch = af_json_get(arches, arch_name->s);
    const af_json *aname = NULL;
    if (!arch) {
        char m[768];
        snprintf(m, sizeof m, "no archetype '%s'", arch_name->s);
        diags_push(diags, D("E_UNKNOWN_ARCHETYPE", "/archetype/name", m, NULL,
                            sorted_strs(arches), close_arr(arch_name, arches), NULL));
    } else {
        aname = need_str(arch, "archetype");
    }
    if (g_crash) { af_json_free(diags); return NULL; }
    if (diags->n) return diags;

    /* ---- everything the exit paths free -------------------------------- */
    af_json *size_env = NULL, *menv = NULL, *env = NULL, *params = NULL;
    af_json *anchors = NULL, **state_keys = NULL;
    af_json *overridable = NULL, *rec_kinds = NULL;
    urec *urecs = NULL;
    uslot *slots = NULL;
    bnd *bounds = NULL;
    size_t nurecs = 0, nslots = 0, nbounds = 0, nkeys = 0;
    int failed = 0;

    /* ---- cell: fixed [w,h] or 2.1 auto {mode,...} ---------------------- */
    const af_json *canvas = need(need(asset, "target"), "canvas");
    if (g_crash) { failed = 1; goto out; }
    int auto_mode = 0;
    int64_t W = 0, H = 0;
    if (canvas->type == AF_JSON_OBJECT) {
        auto_mode = 1;
        const af_json *cv = need(arch, "canvas");
        const af_json *def = cv ? need(cv, "default") : NULL;
        if (!def || def->type != AF_JSON_ARRAY || def->n != 2 ||
            def->items[0]->type != AF_JSON_INT || def->items[1]->type != AF_JSON_INT) {
            crash("bad document");
            failed = 1;
            goto out;
        }
        W = def->items[0]->i;
        H = def->items[1]->i;
    } else if (canvas->type == AF_JSON_ARRAY && canvas->n == 2 &&
               canvas->items[0]->type == AF_JSON_INT &&
               canvas->items[1]->type == AF_JSON_INT) {
        W = canvas->items[0]->i;
        H = canvas->items[1]->i;
    } else {
        crash("bad canvas");                /* W, H = canvas */
        failed = 1;
        goto out;
    }

    /* ---- size parameters (2.1) ----------------------------------------- */
    const af_json *ssn = af_json_get(arch, "size");
    const af_json *size_specs = NULL;
    if (ssn && ssn->type != AF_JSON_NULL && af_truthy(ssn)) {
        if (ssn->type != AF_JSON_OBJECT) {   /* .items() on a non-dict */
            crash("bad document");
            failed = 1;
            goto out;
        }
        size_specs = ssn;
    }
    const af_json *given_size = af_json_get(archnode, "size");
    if (given_size && (given_size->type == AF_JSON_NULL ||
                       given_size->type != AF_JSON_OBJECT)) {
        crash("bad document");              /* for k in None / .get on a list */
        failed = 1;
        goto out;
    }
    size_env = af_json_obj();
    if (!size_env) { crash("out of memory"); failed = 1; goto out; }

    if (given_size)
        for (size_t i = 0; i < given_size->n / 2; i++) {
            const af_json *kn = given_size->items[2 * i];
            if (size_specs && af_json_get(size_specs, kn->s)) continue;
            char p[768], m[768];
            p[0] = 0;
            ptr_cat(p, sizeof p, "archetype");
            ptr_cat(p, sizeof p, "size");
            ptr_cat(p, sizeof p, kn->s);
            snprintf(m, sizeof m, "archetype '%s' has no size parameter '%s'",
                     aname->s, kn->s);
            diags_push(diags, D("E_UNKNOWN_SIZE_PARAM", p, m, af_json_clone(kn),
                                sorted_strs(size_specs), close_arr(kn, size_specs),
                                fix_op("remove", p)));
            if (g_crash) { failed = 1; goto out; }
        }
    if (size_specs)
        for (size_t i = 0; i < size_specs->n / 2; i++) {
            const af_json *kn = size_specs->items[2 * i];
            const af_json *spec = size_specs->items[2 * i + 1];
            if (!spec || spec->type != AF_JSON_OBJECT) {
                crash("bad document");
                failed = 1;
                goto out;
            }
            const af_json *dflt = need(spec, "default");   /* eager KeyError */
            const af_json *v = given_size ? af_json_get(given_size, kn->s) : NULL;
            if (!v) v = dflt;
            const af_json *lo = need(spec, "min"), *hi = need(spec, "max");
            if (g_crash || !v || !lo || !hi) { failed = 1; goto out; }
            if (!num_node(v) || !num_node(lo) || !num_node(hi)) {
                crash("bad document");      /* v < min on a str / None */
                failed = 1;
                goto out;
            }
            int out_of = py_lt(v, lo) || py_gt(v, hi);
            char p[768], m[256], exp[160];
            p[0] = 0;
            ptr_cat(p, sizeof p, "archetype");
            ptr_cat(p, sizeof p, "size");
            ptr_cat(p, sizeof p, kn->s);
            af_json *outv;
            if (out_of) {
                snprintf(m, sizeof m, "size parameter '%s' out of range", kn->s);
                num4_range(lo, hi, exp, sizeof exp);
                af_json *fix = fix_op("replace", p);
                fix_value(fix, py_clamp(v, lo, hi));
                diags_push(diags, D("E_SIZE_RANGE", p, m, opt(v), af_json_str(exp),
                                    NULL, fix));
                outv = py_clamp(v, lo, hi);
            } else {
                outv = af_json_clone(v);
            }
            if (!outv || af_json_obj_set(size_env, kn->s, outv)) {
                crash("out of memory");
                af_json_free(outv);
                failed = 1;
                goto out;
            }
            if (g_crash) { failed = 1; goto out; }
        }

    /* a fixed canvas must be one the archetype supports (unsized only) */
    if (!auto_mode && !size_specs) {
        const af_json *cv = need(arch, "canvas");
        const af_json *sup = cv ? need(cv, "supported") : NULL;
        const af_json *cdef = cv ? need(cv, "default") : NULL;
        if (g_crash || !sup || sup->type != AF_JSON_ARRAY || !cdef) {
            failed = 1;
            goto out;
        }
        af_json *pr = pair2(W, H);
        int found = 0;
        if (pr)
            for (size_t i = 0; i < sup->n && !found; i++)
                if (af_json_equal(sup->items[i], pr)) found = 1;
        if (g_crash || !pr) { af_json_free(pr); failed = 1; goto out; }
        if (!found) {
            af_json *fix = fix_op("replace", "/target/canvas");
            fix_value(fix, af_json_clone(cdef));
            diags_push(diags, D("E_CANVAS_UNSUPPORTED", "/target/canvas",
                                "archetype does not support this canvas size",
                                pr, af_json_clone(sup), NULL, fix));
        } else {
            af_json_free(pr);
        }
        if (g_crash) { failed = 1; goto out; }
    }

    /* ---- expression environment: cell, size, class, proportions -------- */
    menv = menv_build(arch, W, H, size_specs, size_env);
    if (!menv) { failed = 1; goto out; }
    const af_json *arch_params = need(arch, "params");
    if (g_crash) { failed = 1; goto out; }
    char plabel[256];
    snprintf(plabel, sizeof plabel, "archetype '%s'", aname->s);
    params = check_params(arch_params, af_json_get(archnode, "params"),
                          "/archetype/params", style, diags, plabel, menv);
    if (!params || g_crash) { failed = 1; goto out; }
    env = af_json_clone(menv);
    if (!env) { crash("out of memory"); failed = 1; goto out; }
    for (size_t i = 0; i < params->n / 2; i++)
        af_json_obj_set(env, params->items[2 * i]->s,
                        af_json_clone(params->items[2 * i + 1]));
    af_json_free(params);
    params = NULL;
    if (g_crash) { failed = 1; goto out; }

    /* ---- metrics: ordered, every value floored to an integer ----------- */
    const af_json *metrics = af_json_get(arch, "metrics");
    if (metrics) {
        if (metrics->type != AF_JSON_ARRAY) { crash("bad document"); failed = 1; goto out; }
        for (size_t i = 0; i < metrics->n; i++) {
            const af_json *mdef = metrics->items[i];
            if (!mdef || mdef->type != AF_JSON_ARRAY || mdef->n < 2 ||
                mdef->items[0]->type != AF_JSON_STRING) {
                crash("bad document");      /* mname, mexpr = mdef[0], mdef[1] */
                failed = 1;
                goto out;
            }
            const char *mname = mdef->items[0]->s;
            int64_t outv = 0;
            char msg[1024];
            int rc = ev_i(mdef->items[1], env, &outv, msg, sizeof msg);
            if (rc == 1) {
                char p[768], m[1100];
                p[0] = 0;
                ptr_cat(p, sizeof p, "archetype");
                ptr_cat(p, sizeof p, "metrics");
                ptr_int(p, sizeof p, (long long)i);
                snprintf(m, sizeof m, "metric '%s' failed to evaluate: %s", mname, msg);
                diags_push(diags, D("E_ARCHETYPE_EXPR", p, m, NULL, NULL, NULL, NULL));
                af_json_obj_set(env, mname, af_json_int(0));
                if (g_crash) { failed = 1; goto out; }
            } else if (rc != 0) {
                failed = 1;
                goto out;
            } else if (af_json_obj_set(env, mname, af_json_int(outv))) {
                crash("out of memory");
                failed = 1;
                goto out;
            }
        }
    }

    /* ---- min_cell(size): a *fixed* canvas must contain the body -------- */
    const af_json *mcn = af_json_get(arch, "min_cell");
    if (size_specs && mcn && af_truthy(mcn)) {
        if (mcn->type != AF_JSON_ARRAY) { crash("bad document"); failed = 1; goto out; }
        int64_t mv[64];
        size_t nmv = 0;
        int failed_mc = 0;
        for (size_t i = 0; i < mcn->n && !failed_mc; i++) {
            int64_t outv = 0;
            char msg[1024];
            int rc = ev_i(mcn->items[i], env, &outv, msg, sizeof msg);
            if (rc == 1) {
                char p[768], m[1100];
                p[0] = 0;
                ptr_cat(p, sizeof p, "archetype");
                ptr_cat(p, sizeof p, "min_cell");
                snprintf(m, sizeof m, "min_cell failed to evaluate: %s", msg);
                diags_push(diags, D("E_ARCHETYPE_EXPR", p, m, NULL, NULL, NULL, NULL));
                failed_mc = 1;
                if (g_crash) { failed = 1; goto out; }
            } else if (rc != 0) {
                failed = 1;
                goto out;
            } else if (nmv < 64) {
                mv[nmv++] = outv;
            }
        }
        if (!failed_mc) {
            if (nmv < 2) { crash("bad document"); failed = 1; goto out; }  /* mc[1] */
            if (!auto_mode && (W < mv[0] || H < mv[1])) {
                af_json *mc = af_json_arr();
                int okfix = 1;
                for (size_t i = 0; i < nmv; i++) {
                    af_json_arr_push(mc, af_json_int(mv[i]));
                    if (mv[i] < 1 || mv[i] > 256) okfix = 0;
                }
                char m[256];
                snprintf(m, sizeof m,
                         "canvas %lldx%lld is smaller than the archetype minimum "
                         "%lldx%lld at this size",
                         (long long)W, (long long)H,
                         (long long)mv[0], (long long)mv[1]);
                af_json *fix = NULL;
                if (okfix) {
                    fix = fix_op("replace", "/target/canvas");
                    fix_value(fix, af_json_clone(mc));
                }
                diags_push(diags, D("E_CELL_TOO_SMALL", "/target/canvas", m,
                                    pair2(W, H), mc, NULL, fix));
                if (g_crash) { failed = 1; goto out; }
            }
        }
    }

    /* ---- layers / ramps / palette_swap --------------------------------- */
    const af_json *layers = need(arch, "layers");
    const af_json *ramps = need(style, "ramps");
    if (g_crash || !layers || !ramps) { failed = 1; goto out; }
    const af_json *swap_n = af_json_get(asset, "palette_swap");
    const af_json *swap = NULL;
    if (swap_n && af_truthy(swap_n)) {
        if (swap_n->type != AF_JSON_OBJECT) { crash("bad document"); failed = 1; goto out; }
        swap = swap_n;
    }
    if (swap) {
        size_t nk = 0;
        char **sk = sorted_keys(swap, &nk);
        if (nk && !sk) { failed = 1; goto out; }
        for (size_t i = 0; i < nk; i++) {
            const af_json *tn = af_json_get(swap, sk[i]);
            af_json *ends[2];
            ends[0] = af_json_str(sk[i]);
            ends[1] = (af_json *)tn;
            for (int e = 0; e < 2; e++) {
                if (!ends[e] || ends[e]->type == AF_JSON_ARRAY ||
                    ends[e]->type == AF_JSON_OBJECT) {
                    crash("type error");    /* unhashable ramp name */
                    af_json_free(ends[0]);
                    free(sk);
                    failed = 1;
                    goto out;
                }
                char nbuf[64];
                const char *nmv = py_str(ends[e], nbuf, sizeof nbuf);
                if (!af_json_get(ramps, nmv)) {
                    char p[768], m[768];
                    p[0] = 0;
                    ptr_cat(p, sizeof p, "palette_swap");
                    ptr_cat(p, sizeof p, sk[i]);
                    snprintf(m, sizeof m,
                             "palette_swap %s ramp '%s' does not exist in the style pack",
                             e == 0 ? "source" : "target", nmv);
                    af_json *ek = sorted_strs(ramps);
                    af_json *sug = close_arr(ends[e], ek);
                    diags_push(diags, D("E_UNKNOWN_RAMP", p, m,
                                        e == 0 ? af_json_str(sk[i]) : af_json_clone(tn),
                                        ek, sug, fix_op("remove", p)));
                    if (g_crash) { af_json_free(ends[0]); free(sk); failed = 1; goto out; }
                }
            }
            af_json_free(ends[0]);
        }
        free(sk);
        if (g_crash) { failed = 1; goto out; }
    }

    /* ---- anchors -------------------------------------------------------- */
    const af_json *arch_anchors = need(arch, "anchors");
    if (g_crash || !arch_anchors || arch_anchors->type != AF_JSON_OBJECT) {
        failed = 1;
        goto out;
    }
    anchors = af_json_obj();
    if (!anchors) { crash("out of memory"); failed = 1; goto out; }
    for (size_t i = 0; i < arch_anchors->n / 2; i++) {
        const char *an = arch_anchors->items[2 * i]->s;
        const af_json *adef = arch_anchors->items[2 * i + 1];
        if (!adef || adef->type != AF_JSON_OBJECT) { crash("bad document"); break; }
        const af_json *atn = need(adef, "at");
        const af_json *apart = need_str(adef, "part");
        if (g_crash || !atn || atn->type != AF_JSON_ARRAY || !apart) break;
        af_json *at = af_json_arr();
        int at_fail = 0;
        for (size_t j = 0; j < atn->n && !at_fail; j++) {
            int64_t outv = 0;
            char msg[1024];
            int rc = ev_i(atn->items[j], env, &outv, msg, sizeof msg);
            if (rc == 1) {
                char p[768], m[1100];
                p[0] = 0;
                ptr_cat(p, sizeof p, "archetype");
                ptr_cat(p, sizeof p, "anchors");
                ptr_cat(p, sizeof p, an);
                snprintf(m, sizeof m, "anchor '%s' failed to evaluate: %s", an, msg);
                diags_push(diags, D("E_ARCHETYPE_EXPR", p, m, NULL, NULL, NULL, NULL));
                at_fail = 1;
                if (g_crash) { af_json_free(at); break; }
            } else if (rc != 0) {
                af_json_free(at);
                at_fail = 2;
                break;
            } else {
                af_json_arr_push(at, af_json_int(outv));
            }
        }
        if (at_fail == 2) break;
        if (at_fail) {
            af_json_free(at);
            at = pair2(0, 0);
        }
        af_json *ad = af_json_obj();
        if (!ad || !at) { crash("out of memory"); af_json_free(ad); af_json_free(at); break; }
        af_json_obj_put(ad, "part", af_json_clone(apart));
        af_json_obj_put(ad, "at", at);
        af_json_obj_set(anchors, an, ad);
        if (g_crash) break;
    }
    if (g_crash) { failed = 1; goto out; }

    /* ---- mirror axis (2.1): archetype `mirror_axis`, else $cx, else W//2 */
    const af_json *axis_name_n = af_json_get(arch, "mirror_axis");
    const af_json *axis_val = NULL;
    if (axis_name_n && axis_name_n->type == AF_JSON_STRING)
        axis_val = af_json_get(env, axis_name_n->s);
    int64_t axis;
    if (axis_val && axis_val->type == AF_JSON_INT) {
        axis = axis_val->i;
    } else if (axis_name_n && axis_name_n->type != AF_JSON_NULL) {
        char p[768], nbuf[64], m[768];
        const char *axis_name = py_str(axis_name_n, nbuf, sizeof nbuf);
        p[0] = 0;
        ptr_cat(p, sizeof p, "archetype");
        ptr_cat(p, sizeof p, "mirror_axis");
        snprintf(m, sizeof m,
                 "mirror_axis '%s' is not an integer metric or parameter", axis_name);
        af_json *ek = sorted_strs(env);
        af_json *sug = close_arr(axis_name_n, ek);
        diags_push(diags, D("E_ARCHETYPE_EXPR", p, m, af_json_clone(axis_name_n),
                            ek, sug, NULL));
        axis = py_floordiv(W, 2);
        if (g_crash) { failed = 1; goto out; }
    } else if (axis_name_n) {
        /* present but null: env.get(None) is None, str(None) is "None" */
        char p[768], m[768];
        p[0] = 0;
        ptr_cat(p, sizeof p, "archetype");
        ptr_cat(p, sizeof p, "mirror_axis");
        snprintf(m, sizeof m,
                 "mirror_axis 'None' is not an integer metric or parameter");
        af_json *ek = sorted_strs(env);
        af_json *sug = close_arr(axis_name_n, ek);
        diags_push(diags, D("E_ARCHETYPE_EXPR", p, m, NULL, ek, sug, NULL));
        axis = py_floordiv(W, 2);
        if (g_crash) { failed = 1; goto out; }
    } else {
        axis = py_floordiv(W, 2);
    }

    /* ---- archetype body -------------------------------------------------- */
    const af_json *parts = need(arch, "parts");
    if (g_crash || !parts || parts->type != AF_JSON_OBJECT) { failed = 1; goto out; }
    gctx g;
    memset(&g, 0, sizeof g);
    g.recipe = 0;
    g.ramps = ramps;
    g.swap = swap;
    g.axis = axis;
    g.layers = layers;
    g.base_dir = base_dir;
    g.diags = diags;
    g.anchors = anchors;
    for (size_t pi = 0; pi < parts->n / 2; pi++) {
        const char *part = parts->items[2 * pi]->s;
        const af_json *pdef = parts->items[2 * pi + 1];
        const af_json *shapes = need(pdef, "shapes");
        if (g_crash) break;
        char gpath[768];
        gpath[0] = 0;
        ptr_cat(gpath, sizeof gpath, "archetype");
        ptr_cat(gpath, sizeof gpath, "parts");
        ptr_cat(gpath, sizeof gpath, part);
        ptr_cat(gpath, sizeof gpath, "shapes");
        g.part = part;
        int rc = compile_shapes(shapes, env, &g, gpath, 0);
        if (rc == 2 || rc == 3) {
            char m[1100];
            snprintf(m, sizeof m, "archetype geometry failed: %s", g.emsg);
            diags_push(diags, D("E_ARCHETYPE_EXPR", "/archetype", m, NULL, NULL, NULL, NULL));
            break;
        }
    }
    if (g_crash) { failed = 1; goto out; }

    /* ---- features -------------------------------------------------------- */
    const af_json *features = af_json_get(asset, "features");
    if (features && (features->type == AF_JSON_NULL ||
                     features->type != AF_JSON_ARRAY)) {
        crash("bad document");              /* enumerate(None) / of a str */
        failed = 1;
        goto out;
    }
    const af_json *arch_anims = need(arch, "animations");
    if (g_crash || !arch_anims || arch_anims->type != AF_JSON_OBJECT) {
        failed = 1;
        goto out;
    }
    gctx gr;
    memset(&gr, 0, sizeof gr);
    gr.recipe = 1;
    gr.ramps = ramps;
    gr.swap = swap;
    gr.axis = axis;
    gr.layers = layers;
    gr.base_dir = base_dir;
    gr.diags = diags;
    gr.anchors = anchors;

    size_t cap = features ? features->n : 0;
    if (cap) {
        urecs = (urec *)calloc(cap, sizeof *urecs);
        slots = (uslot *)calloc(cap, sizeof *slots);
        if (!urecs || !slots) { crash("out of memory"); failed = 1; goto out; }
    }

    for (size_t i = 0; features && i < features->n; i++) {
        const af_json *f = features->items[i];
        char fp[768], rp[768];
        fp[0] = 0;
        ptr_cat(fp, sizeof fp, "features");
        ptr_int(fp, sizeof fp, (long long)i);
        const af_json *name_n, *given;
        if (f->type == AF_JSON_STRING) {
            name_n = f;
            given = NULL;
            snprintf(rp, sizeof rp, "%s", fp);
        } else if (f->type == AF_JSON_OBJECT) {
            name_n = need(f, "recipe");
            given = af_json_get(f, "params");
            snprintf(rp, sizeof rp, "%s/recipe", fp);
        } else {
            crash("bad document");          /* f["recipe"] on a scalar */
            failed = 1;
            goto out;
        }
        if (g_crash) { failed = 1; goto out; }
        char namebuf[256];
        const char *namestr = py_str(name_n, namebuf, sizeof namebuf);

        if (name_n->type == AF_JSON_ARRAY || name_n->type == AF_JSON_OBJECT) {
            crash("type error");            /* unhashable dict key */
            failed = 1;
            goto out;
        }
        const af_json *rec = name_n->type == AF_JSON_STRING
                                 ? af_json_get(recipes, name_n->s) : NULL;
        if (!rec) {
            char m[768];
            snprintf(m, sizeof m, "NO RECIPE EXISTS named '%s'", namestr);
            af_json *sug = close_arr(name_n, recipes);
            af_json *sugx;
            if (sug && sug->n) {
                sugx = sug;
            } else {
                sugx = af_json_arr();
                if (sugx) af_json_arr_push(sugx, af_json_str("use a custom px_fragment"));
                af_json_free(sug);
                sug = NULL;
            }
            int dofix = sug && sug->n == 1;
            if (!dofix && sug && sug->n > 1 &&
                af_ratio(namestr, sug->items[0]->s) > 0.85)
                dofix = 1;
            af_json *fix = NULL;
            if (dofix) {
                fix = fix_op("replace", rp);
                fix_value(fix, af_json_str(sug->items[0]->s));
            }
            diags_push(diags, D("E_UNKNOWN_RECIPE", rp, m, NULL,
                                sorted_strs(recipes), sugx, fix));
            if (g_crash) { failed = 1; goto out; }
            continue;
        }

        const af_json *applies = need(rec, "applies_to");
        if (g_crash) { failed = 1; goto out; }
        if (!in_cont(applies, aname)) {
            char m[768];
            snprintf(m, sizeof m,
                     "recipe '%s' does not apply to archetype '%s'", namestr, aname->s);
            diags_push(diags, D("E_RECIPE_NOT_APPLICABLE", rp, m, NULL,
                                af_json_clone(applies), NULL, NULL));
            if (g_crash) { failed = 1; goto out; }
            continue;
        }

        /* geometry: one array for every member, or keyed by archetype */
        const af_json *geo = need(rec, "geometry");
        if (g_crash) { failed = 1; goto out; }
        const af_json *geo_used = geo;
        if (geo->type == AF_JSON_OBJECT) {
            geo_used = af_json_get(geo, aname->s);
            if (!geo_used || geo_used->type == AF_JSON_NULL) {
                char m[768];
                snprintf(m, sizeof m,
                         "recipe '%s' has no geometry keyed for archetype '%s'",
                         namestr, aname->s);
                diags_push(diags, D("E_RECIPE_GEOMETRY", fp, m, sorted_strs(geo),
                                    af_json_clone(applies), close_arr(arch_name, geo),
                                    NULL));
                if (g_crash) { failed = 1; goto out; }
                continue;
            }
        }

        const af_json *req = need(rec, "requires_anchors");
        if (g_crash || !req || req->type != AF_JSON_ARRAY) {
            if (!g_crash) crash("bad document");
            failed = 1;
            goto out;
        }
        af_json *miss = af_json_arr();
        if (!miss) { crash("out of memory"); failed = 1; goto out; }
        for (size_t ri = 0; ri < req->n; ri++)
            if (!in_cont(anchors, req->items[ri]))
                af_json_arr_push(miss, af_json_clone(req->items[ri]));
        if (g_crash) { af_json_free(miss); failed = 1; goto out; }
        if (miss->n) {
            char m[768];
            snprintf(m, sizeof m, "archetype lacks anchors required by '%s'", namestr);
            diags_push(diags, D("E_MISSING_ANCHOR", rp, m, NULL, miss, NULL, NULL));
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        af_json_free(miss);

        const af_json *slotn = need(rec, "slot");
        if (g_crash) { failed = 1; goto out; }
        if (slotn->type != AF_JSON_STRING) { crash("bad document"); failed = 1; goto out; }
        const af_json *stackable = af_json_get(rec, "stackable");
        uslot *sl = NULL;
        for (size_t si = 0; si < nslots; si++)
            if (!strcmp(slots[si].slot, slotn->s)) { sl = &slots[si]; break; }
        if (sl && !(stackable && af_truthy(stackable))) {
            char m[768];
            snprintf(m, sizeof m,
                     "slot '%s' already used by feature #%zu ('%s')",
                     slotn->s, sl->idx, sl->name);
            diags_push(diags, D("E_SLOT_CONFLICT", rp, m, af_json_clone(name_n),
                                af_json_str("one recipe per slot"), NULL,
                                fix_op("remove", fp)));
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        if (sl) {
            sl->idx = i;
            sl->name = namestr;
        } else {
            slots[nslots].slot = xstrdup(slotn->s);
            slots[nslots].idx = i;
            slots[nslots].name = namestr;
            if (!slots[nslots].slot) { crash("out of memory"); failed = 1; goto out; }
            nslots++;
        }

        /* a recipe may declare the primary size range it supports */
        const af_json *sizes = af_json_get(rec, "sizes");
        if (sizes && af_truthy(sizes) && size_specs) {
            const af_json *prim = size_specs->items[0];
            const af_json *svn = af_json_get(size_env, prim->s);
            const af_json *lo = need(sizes, "min"), *hi = need(sizes, "max");
            if (g_crash || !svn || !lo || !hi) { failed = 1; goto out; }
            if (py_lt(svn, lo) || py_gt(svn, hi)) {
                char n1[64], n2[64], svb[64], m[768], exp[160];
                const char *los = py_str(lo, n1, sizeof n1);
                const char *his = py_str(hi, n2, sizeof n2);
                const char *svs = py_str(svn, svb, sizeof svb);
                snprintf(m, sizeof m,
                         "recipe '%s' supports %s %s..%s; this asset uses %s",
                         namestr, prim->s, los, his, svs);
                num4_range(lo, hi, exp, sizeof exp);
                char p[768];
                p[0] = 0;
                ptr_cat(p, sizeof p, "archetype");
                ptr_cat(p, sizeof p, "size");
                ptr_cat(p, sizeof p, prim->s);
                af_json *fix = fix_op("replace", p);
                fix_value(fix, py_clamp(svn, lo, hi));
                diags_push(diags, D("E_RECIPE_SIZE_RANGE", fp, m, opt(svn),
                                    af_json_str(exp), NULL, fix));
                if (g_crash) { failed = 1; goto out; }
                continue;
            }
        }

        urecs[nurecs].fp = xstrdup(fp);
        urecs[nurecs].name = xstrdup(namestr);
        urecs[nurecs].rec = rec;
        urecs[nurecs].given = given;
        if (!urecs[nurecs].fp || !urecs[nurecs].name) {
            crash("out of memory");
            failed = 1;
            goto out;
        }
        nurecs++;

        size_t before = diags->n;
        char ppath[768], label[256];
        snprintf(ppath, sizeof ppath, "%s/params", fp);
        snprintf(label, sizeof label, "recipe '%s'", namestr);
        params = check_params(need(rec, "parameters"), given, ppath, style,
                              diags, label, env);
        if (!params || g_crash) { failed = 1; goto out; }
        if (diags->n > before) {
            af_json_free(params);
            params = NULL;
            continue;
        }

        af_json *clash = clash_set(params, env);
        if (!clash) { failed = 1; goto out; }
        if (clash->n) {
            char list[512], m[1100];
            strlist_repr(clash, list, sizeof list);
            snprintf(m, sizeof m,
                     "recipe parameters shadow archetype names: %s", list);
            diags_push(diags, D("E_PARAM_SHADOW", fp, m, NULL, NULL, NULL, NULL));
            af_json_free(clash);
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        af_json_free(clash);

        af_json *renv = af_json_clone(env);
        if (!renv) { crash("out of memory"); failed = 1; goto out; }
        for (size_t i2 = 0; i2 < params->n / 2; i2++)
            af_json_obj_set(renv, params->items[2 * i2]->s,
                            af_json_clone(params->items[2 * i2 + 1]));
        af_json_free(params);
        params = NULL;

        /* locals: each in its own try, the loop keeps going after a
         * failure; a malformed ENTRY unpacks outside the try (crash) */
        int local_added = 0;
        const af_json *locs = af_json_get(rec, "locals");
        if (locs) {
            if (locs->type != AF_JSON_ARRAY) {
                crash("bad document");
                af_json_free(renv);
                failed = 1;
                goto out;
            }
            for (size_t li = 0; li < locs->n; li++) {
                const af_json *e = locs->items[li];
                if (!e || e->type != AF_JSON_ARRAY || e->n != 2 ||
                    e->items[0]->type != AF_JSON_STRING) {
                    crash("type error");    /* unpack ValueError, uncaught */
                    af_json_free(renv);
                    failed = 1;
                    goto out;
                }
                int64_t outv = 0;
                char msg[1024];
                int rc = ev_i(e->items[1], renv, &outv, msg, sizeof msg);
                if (rc == 1) {
                    char m[1100];
                    snprintf(m, sizeof m,
                             "recipe '%s' local '%s' failed to evaluate: %s",
                             namestr, e->items[0]->s, msg);
                    diags_push(diags, D("E_RECIPE_EXPR", fp, m, NULL, NULL, NULL, NULL));
                    local_added = 1;
                } else if (rc != 0) {
                    af_json_free(renv);
                    failed = 1;
                    goto out;
                } else {
                    af_json_obj_set(renv, e->items[0]->s, af_json_int(outv));
                }
            }
        }
        if (g_crash) { af_json_free(renv); failed = 1; goto out; }
        if (local_added) {
            af_json_free(renv);
            continue;
        }

        char gpath[768];
        snprintf(gpath, sizeof gpath, "%s/geometry", fp);
        if (geo->type == AF_JSON_OBJECT) ptr_cat(gpath, sizeof gpath, aname->s);
        int rc = compile_shapes(geo_used, renv, &gr, gpath, 0);
        if (rc == 2) {
            char m[1100];
            snprintf(m, sizeof m, "recipe '%s' failed to evaluate: %s",
                     namestr, gr.emsg);
            diags_push(diags, D("E_RECIPE_EXPR", fp, m, NULL, NULL, NULL, NULL));
            af_json_free(renv);
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        if (rc == 3) {
            crash("type error");            /* ValueError: not caught here */
            af_json_free(renv);
            failed = 1;
            goto out;
        }

        /* hitboxes: anchor lookup, then the rect (its unpack happens after
         * every ev() the generator runs), then id/active_in */
        const af_json *hbs = af_json_get(rec, "hitboxes");
        if (hbs && (hbs->type == AF_JSON_NULL || hbs->type != AF_JSON_ARRAY)) {
            crash("bad document");
            af_json_free(renv);
            failed = 1;
            goto out;
        }
        for (size_t hi2 = 0; hbs && hi2 < hbs->n; hi2++) {
            const af_json *hb = hbs->items[hi2];
            if (!hb || hb->type != AF_JSON_OBJECT) { crash("bad document"); break; }
            const af_json *hanchor = need(hb, "anchor");
            if (g_crash) break;
            if (!hanchor || hanchor->type != AF_JSON_STRING) { crash("bad document"); break; }
            const af_json *a = af_json_get(anchors, hanchor->s);
            if (!a) { crash("bad document"); break; }      /* KeyError */
            const af_json *rect = need(hb, "rect");
            if (g_crash) break;
            if (!rect || rect->type != AF_JSON_ARRAY) { crash("bad document"); break; }
            int64_t xywh[4] = {0, 0, 0, 0};
            int hb_err = 0, hb_crash = 0;
            size_t nr = rect->n < 4 ? rect->n : 4;
            for (size_t ri = 0; ri < nr && !hb_err && !hb_crash; ri++) {
                char msg[1024];
                int rrc = ev_i(rect->items[ri], renv, &xywh[ri], msg, sizeof msg);
                if (rrc == 1) {
                    char m[1100];
                    snprintf(m, sizeof m, "recipe '%s' failed to evaluate: %s",
                             namestr, msg);
                    diags_push(diags, D("E_RECIPE_EXPR", fp, m, NULL, NULL, NULL, NULL));
                    hb_err = 1;
                } else if (rrc != 0) {
                    hb_crash = 1;
                }
            }
            if (hb_crash) break;
            if (hb_err) break;
            if (rect->n != 4) {
                crash("type error");        /* x, y, w, h = ... : ValueError */
                break;
            }
            if (g_crash) break;
            need(hb, "id");                 /* read after the unpack */
            need(hb, "active_in");
            if (g_crash) break;
            /* the drawable itself is IR: validate discards it */
        }
        af_json_free(renv);
        if (g_crash) { failed = 1; goto out; }
    }
    if (g_crash) { failed = 1; goto out; }

    /* ---- custom px fragments (env-independent) -------------------------- */
    const af_json *custom = af_json_get(asset, "custom");
    if (custom && (custom->type == AF_JSON_NULL || custom->type != AF_JSON_ARRAY)) {
        crash("bad document");
        failed = 1;
        goto out;
    }
    gctx gc;
    memset(&gc, 0, sizeof gc);
    gc.recipe = 0;
    gc.ramps = ramps;
    gc.swap = swap;
    gc.axis = axis;
    gc.layers = layers;
    gc.base_dir = base_dir;
    gc.diags = diags;
    gc.anchors = anchors;
    for (size_t i = 0; custom && i < custom->n; i++) {
        const af_json *c = custom->items[i];
        char cp[768];
        cp[0] = 0;
        ptr_cat(cp, sizeof cp, "custom");
        ptr_int(cp, sizeof cp, (long long)i);
        if (!c || c->type != AF_JSON_OBJECT) { crash("bad document"); failed = 1; goto out; }
        const af_json *canchor = need(c, "anchor");
        const af_json *clayer = need(c, "layer");
        const af_json *cfile = need(c, "file");
        if (g_crash) { failed = 1; goto out; }
        if (!canchor || canchor->type != AF_JSON_STRING ||
            !clayer || clayer->type != AF_JSON_STRING ||
            !cfile || cfile->type != AF_JSON_STRING) {
            crash("bad document");
            failed = 1;
            goto out;
        }
        if (!af_json_get(anchors, canchor->s)) {
            char p[768], m[768];
            snprintf(p, sizeof p, "%s/anchor", cp);
            snprintf(m, sizeof m, "archetype has no anchor '%s'", canchor->s);
            diags_push(diags, D("E_MISSING_ANCHOR", p, m, NULL,
                                sorted_strs(anchors), close_arr(canchor, anchors),
                                NULL));
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        if (!in_cont(layers, clayer)) {
            char p[768], m[768];
            snprintf(p, sizeof p, "%s/layer", cp);
            snprintf(m, sizeof m, "archetype has no layer '%s'", clayer->s);
            af_json *tmp = af_json_str(clayer->s);
            af_json *sug = close_arr(tmp, layers);
            af_json_free(tmp);
            diags_push(diags, D("E_UNKNOWN_LAYER", p, m, NULL,
                                af_json_clone(layers), sug, NULL));
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        pxuse use;
        af_px *owned = NULL;
        if (px_prepare(cfile, cp, af_json_get(c, "frame"), &gc, &use, &owned)) {
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        if (px_legend(owned, cfile->s, cp, &gc)) {
            af_px_free(owned);
            if (g_crash) { failed = 1; goto out; }
            continue;
        }
        af_px_free(owned);
        need(c, "id");                      /* f"custom:{c['id']}" */
        if (g_crash) { failed = 1; goto out; }
    }
    if (g_crash) { failed = 1; goto out; }

    /* ---- animations ------------------------------------------------------ */
    const af_json *anims_n = af_json_get(asset, "animations");
    if (anims_n && (anims_n->type == AF_JSON_NULL ||
                    anims_n->type != AF_JSON_OBJECT)) {
        crash("bad document");              /* .items() on None / a list */
        failed = 1;
        goto out;
    }
    if (anims_n) {
        bounds = (bnd *)calloc(anims_n->n ? anims_n->n : 1, sizeof *bounds);
        if (!bounds) { crash("out of memory"); failed = 1; goto out; }
        for (size_t i = 0; i < anims_n->n / 2; i++) {
            const char *nm = anims_n->items[2 * i]->s;
            const af_json *a = anims_n->items[2 * i + 1];
            if (!a || a->type != AF_JSON_OBJECT) { crash("bad document"); failed = 1; goto out; }
            const af_json *tpln = need(a, "template");
            if (g_crash) { failed = 1; goto out; }
            char tbuf[64];
            const char *tname = py_str(tpln, tbuf, sizeof tbuf);
            const af_json *tpl = tpln->type == AF_JSON_STRING
                                     ? af_json_get(arch_anims, tpln->s) : NULL;
            if (!tpl) {
                char p[768], m[768];
                p[0] = 0;
                ptr_cat(p, sizeof p, "animations");
                ptr_cat(p, sizeof p, nm);
                ptr_cat(p, sizeof p, "template");
                snprintf(m, sizeof m,
                         "archetype has no animation template '%s'", tname);
                diags_push(diags, D("E_UNKNOWN_ANIMATION_TEMPLATE", p, m, NULL,
                                    sorted_strs(arch_anims),
                                    close_arr(tpln, arch_anims), NULL));
                if (g_crash) { failed = 1; goto out; }
                continue;
            }
            int seen = 0;
            for (size_t b = 0; b < nbounds; b++)
                if (!strcmp(bounds[b].tname, tname)) { seen = 1; break; }
            if (!seen) {
                bounds[nbounds].tname = xstrdup(tname);
                bounds[nbounds].tpl = tpl;
                if (!bounds[nbounds].tname) { crash("out of memory"); failed = 1; goto out; }
                nbounds++;
            }
        }
    }
    if (g_crash) { failed = 1; goto out; }

    /* ---- 02 §F: tracks/swap validated on the templates this asset binds - */
    overridable = af_json_obj();
    rec_kinds = af_json_obj();
    if (!overridable || !rec_kinds) { crash("out of memory"); failed = 1; goto out; }
    if (size_specs)
        for (size_t i = 0; i < size_specs->n / 2; i++)
            af_json_obj_set(overridable, size_specs->items[2 * i]->s,
                            af_json_bool(1));
    for (size_t i = 0; i < arch_params->n / 2; i++) {
        const af_json *sp = arch_params->items[2 * i + 1];
        if (track_kind(sp))
            af_json_obj_set(overridable, arch_params->items[2 * i]->s,
                            af_json_bool(1));
    }
    for (size_t ui = 0; ui < nurecs; ui++) {
        const af_json *rp2 = need(urecs[ui].rec, "parameters");
        if (g_crash || !rp2 || rp2->type != AF_JSON_OBJECT) {
            if (!g_crash) crash("bad document");
            failed = 1;
            goto out;
        }
        for (size_t i = 0; i < rp2->n / 2; i++) {
            const char *k = rp2->items[2 * i]->s;
            const af_json *cur = af_json_get(rec_kinds, k);
            int good = track_kind(rp2->items[2 * i + 1]);
            if (!cur) af_json_obj_set(rec_kinds, k, af_json_bool(good));
            else if (!good) af_json_obj_set(rec_kinds, k, af_json_bool(0));
        }
    }
    for (size_t i = 0; i < rec_kinds->n / 2; i++)
        if (rec_kinds->items[2 * i + 1]->type == AF_JSON_BOOL &&
            rec_kinds->items[2 * i + 1]->b)
            af_json_obj_set(overridable, rec_kinds->items[2 * i]->s,
                            af_json_bool(1));
    af_json_free(rec_kinds);
    rec_kinds = NULL;
    if (g_crash) { af_json_free(overridable); failed = 1; goto out; }

    for (size_t bi = 0; bi < nbounds; bi++) {
        const char *tname = bounds[bi].tname;
        const af_json *frames = need(bounds[bi].tpl, "frames");
        if (g_crash || !frames || frames->type != AF_JSON_ARRAY) {
            if (!g_crash) crash("bad document");
            af_json_free(overridable);
            failed = 1;
            goto out;
        }
        for (size_t fi = 0; fi < frames->n; fi++) {
            const af_json *fr = frames->items[fi];
            if (!fr || fr->type != AF_JSON_OBJECT) { crash("bad document"); break; }
            char tpath[768];
            tpath[0] = 0;
            ptr_cat(tpath, sizeof tpath, "archetype");
            ptr_cat(tpath, sizeof tpath, "animations");
            ptr_cat(tpath, sizeof tpath, tname);
            ptr_cat(tpath, sizeof tpath, "frames");
            ptr_int(tpath, sizeof tpath, (long long)fi);

            const af_json *fparams = af_json_get(fr, "params");
            if (fparams && !af_truthy(fparams)) fparams = NULL;
            if (fparams) {
                size_t npk = 0;
                char **pk = NULL;
                if (fparams->type == AF_JSON_OBJECT) {
                    pk = sorted_keys(fparams, &npk);
                } else if (fparams->type == AF_JSON_ARRAY) {
                    npk = fparams->n;
                    pk = (char **)malloc((npk ? npk : 1) * sizeof *pk);
                    if (pk)
                        for (size_t i = 0; i < npk; i++) {
                            if (fparams->items[i]->type != AF_JSON_STRING) {
                                free(pk);
                                pk = NULL;
                                crash("type error");   /* sorted()/set of mixed */
                                break;
                            }
                            pk[i] = fparams->items[i]->s;
                        }
                    if (pk)
                        for (size_t i = 1; i < npk; i++) {
                            char *key = pk[i];
                            size_t j = i;
                            while (j && strcmp(pk[j - 1], key) > 0) { pk[j] = pk[j - 1]; j--; }
                            pk[j] = key;
                        }
                } else {
                    crash("bad document");  /* sorted(5) */
                }
                if (g_crash || (npk && !pk)) {
                    free(pk);
                    af_json_free(overridable);
                    failed = 1;
                    goto out;
                }
                for (size_t i = 0; i < npk; i++) {
                    const char *k = pk[i];
                    if (af_json_get(overridable, k)) continue;
                    char p[768], m[768];
                    snprintf(p, sizeof p, "%s", tpath);
                    ptr_cat(p, sizeof p, "params");
                    ptr_cat(p, sizeof p, k);
                    snprintf(m, sizeof m,
                             "frame track '%s' is not an overridable parameter "
                             "of this archetype", k);
                    af_json *obs = af_json_str(k);
                    diags_push(diags, D("E_TRACK_PARAM", p, m, obs,
                                        sorted_strs(overridable),
                                        close_arr(obs, overridable),
                                        fix_op("remove", p)));
                    if (g_crash) {
                        free(pk);
                        af_json_free(overridable);
                        failed = 1;
                        goto out;
                    }
                }
                free(pk);
            }

            const af_json *fswap = af_json_get(fr, "swap");
            if (fswap && !af_truthy(fswap)) fswap = NULL;
            if (fswap) {
                if (fswap->type != AF_JSON_OBJECT) { crash("bad document"); break; }
                size_t nsk = 0;
                char **sk = sorted_keys(fswap, &nsk);
                if (nsk && !sk) { af_json_free(overridable); failed = 1; goto out; }
                for (size_t i = 0; i < nsk; i++) {
                    const char *part = sk[i];
                    const af_json *sset = af_json_get(fswap, part);
                    char spath[768];
                    snprintf(spath, sizeof spath, "%s", tpath);
                    ptr_cat(spath, sizeof spath, "swap");
                    ptr_cat(spath, sizeof spath, part);
                    const af_json *pnode = af_json_get(parts, part);
                    if (!pnode) {
                        char m[768];
                        snprintf(m, sizeof m, "archetype has no part '%s'", part);
                        af_json *obs = af_json_str(part);
                        diags_push(diags, D("E_UNKNOWN_PART", spath, m, obs,
                                            sorted_strs(parts),
                                            close_arr(obs, parts),
                                            fix_op("remove", spath)));
                        if (g_crash) { free(sk); af_json_free(overridable); failed = 1; goto out; }
                        continue;
                    }
                    const af_json *vn = af_json_get(pnode, "variants");
                    if (vn && vn->type == AF_JSON_NULL) vn = NULL;
                    if (vn && vn->type != AF_JSON_OBJECT && vn->type != AF_JSON_ARRAY) {
                        crash("bad document");
                        free(sk);
                        af_json_free(overridable);
                        failed = 1;
                        goto out;
                    }
                    af_json *vs = sorted_strs(vn);
                    if (!vs) { free(sk); af_json_free(overridable); failed = 1; goto out; }
                    int inset = 0;
                    if (vn && vn->type == AF_JSON_ARRAY) {
                        for (size_t vi = 0; vi < vn->n && !inset; vi++)
                            if (af_json_equal(vn->items[vi], sset)) inset = 1;
                    } else if (vn) {
                        char sbuf[64];
                        inset = af_json_get(vn, py_str(sset, sbuf, sizeof sbuf)) != NULL;
                    }
                    if (!inset) {
                        char sbuf[64], m[768];
                        snprintf(m, sizeof m, "part '%s' has no variant set '%s'",
                                 part, py_str(sset, sbuf, sizeof sbuf));
                        af_json *sug = close_arr(sset, vs);
                        af_json *fix;
                        if (sug && sug->n == 1) {
                            fix = fix_op("replace", spath);
                            fix_value(fix, af_json_str(sug->items[0]->s));
                        } else {
                            fix = fix_op("remove", spath);
                        }
                        diags_push(diags, D("E_UNKNOWN_SWAP_SET", spath, m,
                                            opt(sset), vs, sug, fix));
                        if (g_crash) { free(sk); af_json_free(overridable); failed = 1; goto out; }
                    } else {
                        af_json_free(vs);
                    }
                }
                free(sk);
                if (g_crash) { af_json_free(overridable); failed = 1; goto out; }
            }
        }
        if (g_crash) { af_json_free(overridable); failed = 1; goto out; }
    }
    af_json_free(overridable);
    overridable = NULL;
    if (g_crash) { failed = 1; goto out; }

    if (has_error(diags)) goto out;

    /* ---- 02 §F: per-frame geometry states (diagnostics only) ------------ */
    for (size_t bi = 0; bi < nbounds; bi++) {
        const char *tname = bounds[bi].tname;
        const af_json *frames = need(bounds[bi].tpl, "frames");
        if (g_crash || !frames || frames->type != AF_JSON_ARRAY) {
            if (!g_crash) crash("bad document");
            failed = 1;
            goto out;
        }
        for (size_t fi = 0; fi < frames->n; fi++) {
            const af_json *fr = frames->items[fi];
            if (!fr || fr->type != AF_JSON_OBJECT) { crash("bad document"); break; }
            const af_json *tr = af_json_get(fr, "params");
            if (tr && !af_truthy(tr)) tr = NULL;
            const af_json *sm = af_json_get(fr, "swap");
            if (sm && !af_truthy(sm)) sm = NULL;
            if (!tr && !sm) continue;       /* frame_state_key is None */
            af_json *key = af_json_obj();
            if (!key) { crash("out of memory"); failed = 1; goto out; }
            af_json_obj_put(key, "params", tr ? af_json_clone(tr) : af_json_obj());
            af_json_obj_put(key, "swap", sm ? af_json_clone(sm) : af_json_obj());
            int dup = 0;
            for (size_t ki = 0; ki < nkeys && !dup; ki++)
                if (af_json_equal(state_keys[ki], key)) dup = 1;
            if (dup) { af_json_free(key); continue; }
            if (nkeys % 8 == 0) {
                af_json **nk = (af_json **)realloc(state_keys,
                                                   (nkeys + 8) * sizeof *nk);
                if (!nk) { af_json_free(key); crash("out of memory"); failed = 1; goto out; }
                state_keys = nk;
            }
            state_keys[nkeys++] = key;
            char tpath[768];
            tpath[0] = 0;
            ptr_cat(tpath, sizeof tpath, "archetype");
            ptr_cat(tpath, sizeof tpath, "animations");
            ptr_cat(tpath, sizeof tpath, tname);
            ptr_cat(tpath, sizeof tpath, "frames");
            ptr_int(tpath, sizeof tpath, (long long)fi);
            make_state(&(cas){asset, arch, style, size_specs, size_env, arch_params,
                              ramps, layers, anchors, swap, base_dir, W, H, diags,
                              urecs, nurecs},
                       tr, sm, tpath);
            if (g_crash) { failed = 1; goto out; }
        }
        if (g_crash) { failed = 1; goto out; }
    }
    if (has_error(diags)) goto out;

out:
    for (size_t i = 0; i < nkeys; i++) af_json_free(state_keys[i]);
    free(state_keys);
    if (bounds)
        for (size_t i = 0; i < nbounds; i++) free((void *)bounds[i].tname);
    free(bounds);
    slots_free(slots, nslots);
    urecs_free(urecs, nurecs);
    af_json_free(overridable);
    af_json_free(rec_kinds);
    af_json_free(anchors);
    af_json_free(params);
    af_json_free(env);
    af_json_free(menv);
    af_json_free(size_env);
    if (failed) {
        af_json_free(diags);
        return NULL;
    }
    return diags;
}

/* ---- part 5: paths, extends, variants, command ------------------------ */

/* ---- root discovery --------------------------------------------------- */

static int dir_has(const char *dir, const char *name) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_arcforge_root(const char *dir) {
    return dir_has(dir, "schema") && dir_has(dir, "registry");
}

/* find_root(argv0): Python's ROOT is Path(__file__).parent.parent; the
 * binary's analogue is its own location (arcforge/build/arcforge ->
 * arcforge/), else an ancestor of the cwd, else the cwd. */
static void find_root(const char *argv0) {
    g_root[0] = 0;
    char *rp = argv0 ? realpath(argv0, NULL) : NULL;   /* heap: PATH_MAX */
    if (rp) {
        char *sl = strrchr(rp, '/');
        if (sl) {
            *sl = 0;
            sl = strrchr(rp, '/');
            if (sl) {
                *sl = 0;
                if (is_arcforge_root(rp)) {
                    snprintf(g_root, sizeof g_root, "%s", rp);
                    free(rp);
                    return;
                }
            }
        }
        free(rp);
    }
    char cwd[1024];
    if (getcwd(cwd, sizeof cwd)) {
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", cwd);
        for (;;) {
            if (is_arcforge_root(buf)) {
                snprintf(g_root, sizeof g_root, "%s", buf);
                return;
            }
            char *sl = strrchr(buf, '/');
            if (!sl) break;
            if (sl == buf) break;              /* "/" */
            *sl = 0;
        }
        snprintf(g_root, sizeof g_root, "%s", cwd);
    } else {
        snprintf(g_root, sizeof g_root, ".");
    }
}

/* ---- path helpers (Path semantics) ------------------------------------ */

/* Python `k in o` for a string k: dict key / list equality / str
 * substring; any other container type raises (crash). */
static int py_in(const af_json *o, const char *k) {
    if (!o) { crash("bad document"); return 0; }
    switch (o->type) {
    case AF_JSON_OBJECT:
        return af_json_get(o, k) != NULL;
    case AF_JSON_ARRAY: {
        af_json *ks = xoom(af_json_str(k));
        if (!ks) return 0;
        int found = 0;
        for (size_t i = 0; i < o->n && !found; i++)
            found = af_json_equal(o->items[i], ks);
        af_json_free(ks);
        return found;
    }
    case AF_JSON_STRING:
        return strstr(o->s, k) != NULL;
    default:
        crash("bad document");
        return 0;
    }
}

/* strict UTF-8: reject overlong forms, surrogates, > U+10FFFF (Python's
 * utf-8 decoder does the same in read_text()) */
static int utf8_valid(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t need;
        uint32_t cp;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xe0) == 0xc0) { need = 2; cp = c & 0x1f; }
        else if ((c & 0xf0) == 0xe0) { need = 3; cp = c & 0x0f; }
        else if ((c & 0xf8) == 0xf0) { need = 4; cp = c & 0x07; }
        else return 0;
        if (i + need > n) return 0;
        for (size_t j = 1; j < need; j++) {
            if ((s[i + j] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (s[i + j] & 0x3f);
        }
        if (need == 2 && cp < 0x80) return 0;
        if (need == 3 && cp < 0x800) return 0;
        if (need == 4 && cp < 0x10000) return 0;
        if (cp > 0x10ffff) return 0;
        if (cp >= 0xd800 && cp <= 0xdfff) return 0;
        i += need;
    }
    return 1;
}

/* lexically normalize an absolute path: drop "." and empty components,
 * pop ".." (never above "/"). Symlinks are resolved separately. */
static void norm_lexical(char *p, size_t osz) {
    if (!p || p[0] != '/') return;
    char out[1024];
    size_t on = 0;
    out[on++] = '/';
    const char *s = p + 1;
    while (*s) {
        const char *slash = strchr(s, '/');
        size_t tl = slash ? (size_t)(slash - s) : strlen(s);
        if (tl == 0 || (tl == 1 && s[0] == '.')) {
            /* drop */
        } else if (tl == 2 && s[0] == '.' && s[1] == '.') {
            if (on > 1) {
                while (on > 1 && out[on - 1] != '/') on--;
                if (on > 1) on--;
                else on = 1;
            }
        } else {
            if (on > 1 && on + tl + 2 < sizeof out) out[on++] = '/';
            if (on + tl + 1 < sizeof out) {
                memcpy(out + on, s, tl);
                on += tl;
            }
        }
        if (!slash) break;
        s = slash + 1;
    }
    out[on] = 0;
    snprintf(p, osz, "%s", out);
}

/* a/b joined with a single slash, snprintf("%s/%s") semantics without
 * the format-truncation noise (truncates the same way) */
static void join_slash(char *dst, size_t osz, const char *a, const char *b) {
    size_t n = 0;
    for (size_t i = 0; a[i] && n + 1 < osz; i++) dst[n++] = a[i];
    if (n && dst[n - 1] != '/' && n + 1 < osz) dst[n++] = '/';
    for (size_t i = 0; b[i] && n + 1 < osz; i++) dst[n++] = b[i];
    dst[n] = 0;
}

/* Path(p) for an absolute p: resolve() -- realpath when the path exists,
 * else lexical normalization plus realpath of the directory prefix. */
static void resolve_path(char *p, size_t osz) {
    char *rp = realpath(p, NULL);                   /* heap: PATH_MAX */
    if (rp) {
        snprintf(p, osz, "%s", rp);
        free(rp);
        return;
    }
    if (p[0] != '/') {
        char cwd[1024];
        if (getcwd(cwd, sizeof cwd)) {
            char tmp[1024];
            join_slash(tmp, sizeof tmp, cwd, p);
            snprintf(p, osz, "%s", tmp);
        }
    }
    norm_lexical(p, osz);
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", p);
    char *leaf = strrchr(dir, '/');
    if (leaf) {
        *leaf = 0;
        char *rdir = realpath(dir[0] ? dir : "/", NULL);
        if (rdir) {
            char tmp[1024];
            join_slash(tmp, sizeof tmp, rdir, leaf + 1);
            free(rdir);
            snprintf(p, osz, "%s", tmp);
        }
    }
}

/* Path(p).parent: no slash -> ".", "/x" -> "/", collapse "." and empty
 * components (PurePath normalizes them away). */
static void python_parent(const char *p, char *out, size_t osz) {
    const char *sl = strrchr(p, '/');
    if (!sl) { snprintf(out, osz, "."); return; }
    if (sl == p) { snprintf(out, osz, "/"); return; }
    size_t n = (size_t)(sl - p);
    char tmp[1024];
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, p, n);
    tmp[n] = 0;
    /* collapse "" and "." components */
    int abs = tmp[0] == '/';
    const char *s = abs ? tmp + 1 : tmp;
    char cols[1024];
    size_t cn = 0;
    if (abs) cols[cn++] = '/';
    while (*s) {
        const char *slash = strchr(s, '/');
        size_t tl = slash ? (size_t)(slash - s) : strlen(s);
        if (tl != 0 && !(tl == 1 && s[0] == '.')) {
            if (cn > (abs ? 1u : 0u) && cn + 1 < sizeof cols) cols[cn++] = '/';
            if (cn + tl < sizeof cols) { memcpy(cols + cn, s, tl); cn += tl; }
        }
        if (!slash) break;
        s = slash + 1;
    }
    cols[cn] = 0;
    if (cn == 0 || (abs && cn == 1)) snprintf(out, osz, abs ? "/" : ".");
    else snprintf(out, osz, "%s", cols);
}

/* target = (cur_file.parent / ref).resolve() */
static void join_resolve(const char *cur_file, const char *ref,
                         char *out, size_t osz) {
    char base[1024];
    python_parent(cur_file, base, sizeof base);
    if (ref[0] == '/') snprintf(out, osz, "%s", ref);
    else if (strcmp(base, "/") == 0) snprintf(out, osz, "/%s", ref);
    else if (strcmp(base, ".") == 0) snprintf(out, osz, "%s", ref);
    else join_slash(out, osz, base, ref);
    resolve_path(out, osz);
}

/* root containment: target == root, or a strict descendant (Path.parents
 * membership); handles root == "/". */
static int is_inside(const char *target, const char *root) {
    if (strcmp(target, root) == 0) return 1;
    size_t rl = strlen(root);
    if (strncmp(target, root, rl) != 0) return 0;
    if (rl == 1 && root[0] == '/') return 1;
    return target[rl] == '/';
}

static const char *base_name(const char *p) {
    const char *sl = strrchr(p, '/');
    return sl ? sl + 1 : p;
}

/* ---- RFC 7386 merge patch --------------------------------------------- */

/* Documents are never mutated in place: deep-clone based. Returns the
 * merged node (owned), or NULL with g_crash set. */
static af_json *merge_patch(const af_json *base, const af_json *patch) {
    if (!base || base->type != AF_JSON_OBJECT ||
        !patch || patch->type != AF_JSON_OBJECT)
        return af_json_clone(patch);
    af_json *out = af_json_clone(base);
    if (!out) { crash("out of memory"); return NULL; }
    for (size_t i = 0; i + 1 < patch->n; i += 2) {
        const af_json *k = patch->items[i];
        const af_json *v = patch->items[i + 1];
        if (k->type != AF_JSON_STRING) continue;    /* keys are strings */
        if (v->type == AF_JSON_NULL) {
            af_json_obj_del(out, k->s);
        } else if (af_json_get(out, k->s)) {
            af_json *nv = merge_patch(af_json_get(out, k->s), v);
            if (!nv || af_json_obj_set(out, k->s, nv) != 0) {
                af_json_free(nv);
                af_json_free(out);
                crash("out of memory");
                return NULL;
            }
        } else {
            af_json *c = af_json_clone(v);
            if (!c || af_json_obj_put(out, k->s, c) != 0) {
                af_json_free(c);
                af_json_free(out);
                crash("out of memory");
                return NULL;
            }
        }
    }
    return out;
}

/* ---- extends chain ----------------------------------------------------- */

/* E_EXTENDS_PATH family; all report at /extends of the given file. */

static void extends_path_diag(af_json *diags, const char *msg,
                              const char *ref, const char *expected,
                              int with_fix) {
    af_json *fix = with_fix ? fix_op("remove", "/extends") : NULL;
    diags_push(diags, D("E_EXTENDS_PATH", "/extends", msg,
                        af_json_str(ref), af_json_str(expected), NULL, fix));
}

/* read + json.loads(target.read_text()): returns an owned document.
 * OSError/JSONDecodeError -> *errmsg filled, returns NULL (reported);
 * invalid UTF-8 -> crash (Python's UnicodeDecodeError is uncaught). */
static af_json *read_extends_target(const char *path, char *errmsg,
                                    size_t emsz) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        int e = errno;
        snprintf(errmsg, emsz, "[Errno %d] %s: '%s'", e, strerror(e), path);
        return NULL;
    }
    char *buf = NULL;
    size_t n = 0, cap = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 1 > cap) {
            size_t nc = cap ? cap * 2 : 4096;
            char *p = (char *)realloc(buf, nc);
            if (!p) { free(buf); fclose(f); crash("out of memory"); return NULL; }
            buf = p;
            cap = nc;
        }
        buf[n++] = (char)c;
    }
    int ferr = ferror(f);
    fclose(f);
    if (ferr) {
        int e = errno;
        free(buf);
        snprintf(errmsg, emsz, "[Errno %d] %s: '%s'", e, strerror(e), path);
        return NULL;
    }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) { crash("out of memory"); return NULL; }
        n = 0;
    }
    if (!utf8_valid((const unsigned char *)buf, n)) {
        free(buf);
        crash("invalid utf-8");
        return NULL;
    }
    char err[256];
    af_json *doc = NULL;
    if (af_json_parse(buf, n, &doc, err, sizeof err) != 0) {
        free(buf);
        snprintf(errmsg, emsz, "%s", err);
        return NULL;
    }
    free(buf);
    return doc;
}

/* resolve_extends(doc, file_path, diags): the oracle's loop line-for-line.
 * visited holds ONLY the original resolved file (the oracle never adds to
 * it); longer cycles are caught by the hop limit. Returns an owned merged
 * document, or NULL with a diagnostic appended in diags (or g_crash). */
static af_json *resolve_extends(const af_json *doc, const char *file_path,
                                af_json *diags) {
    char orig[1024];
    snprintf(orig, sizeof orig, "%s", file_path);
    resolve_path(orig, sizeof orig);
    char cur_file[1024];
    snprintf(cur_file, sizeof cur_file, "%s", orig);

    const af_json *cur = doc;
    af_json *owned = NULL;
    int hops = 0;
    while (cur && cur->type == AF_JSON_OBJECT && py_in(cur, "extends")) {
        const af_json *refv = af_json_get(cur, "extends");
        if (!refv || refv->type != AF_JSON_STRING) {
            diags_push(diags, D("E_SCHEMA", "/extends",
                                "'extends' must be a string",
                                af_json_str(py_typename(refv)),
                                af_json_str("string"), NULL, NULL));
            af_json_free(owned);
            return NULL;
        }
        const char *ref = refv->s;
        char target[1024];
        join_resolve(cur_file, ref, target, sizeof target);
        char where[1200];
        if (strcmp(cur_file, orig) != 0)
            snprintf(where, sizeof where, " (at '%s')", base_name(cur_file));
        else
            where[0] = 0;

        if (strcmp(target, orig) == 0) {
            char m[1400];
            snprintf(m, sizeof m, "extends chain revisits '%s'%s: cycle",
                     ref, where);
            diags_push(diags, D("E_EXTENDS_CYCLE", "/extends", m,
                                af_json_str(ref),
                                af_json_str("a file not already in the chain"),
                                NULL, fix_op("remove", "/extends")));
            af_json_free(owned);
            return NULL;
        }
        if (hops >= 3) {
            char m[1400];
            snprintf(m, sizeof m, "extends chain exceeds 3 links%s", where);
            char ebuf[16];
            snprintf(ebuf, sizeof ebuf, "<= %d", 3);
            diags_push(diags, D("E_EXTENDS_DEPTH", "/extends", m,
                                af_json_int(hops + 1), af_json_str(ebuf),
                                NULL, fix_op("remove", "/extends")));
            af_json_free(owned);
            return NULL;
        }
        if (!is_inside(target, g_root)) {
            char m[1400], ebuf[1200];
            snprintf(m, sizeof m,
                     "extends target '%s' escapes the repo root%s", ref, where);
            snprintf(ebuf, sizeof ebuf, "a path inside %s", g_root);
            extends_path_diag(diags, m, ref, ebuf, 1);
            af_json_free(owned);
            return NULL;
        }
        struct stat st;
        if (stat(target, &st) != 0 || !S_ISREG(st.st_mode)) {
            char m[1400];
            snprintf(m, sizeof m, "extends target '%s' not found%s", ref, where);
            extends_path_diag(diags, m, ref, "an existing asset file", 1);
            af_json_free(owned);
            return NULL;
        }
        char emsg[1200];
        af_json *parent = read_extends_target(target, emsg, sizeof emsg);
        if (!parent && g_crash) { af_json_free(owned); return NULL; }
        if (!parent) {
            char m[2600];
            snprintf(m, sizeof m, "extends target '%s' cannot be read: %s%s",
                     ref, emsg, where);
            extends_path_diag(diags, m, ref, "a readable JSON asset", 0);
            af_json_free(owned);
            return NULL;
        }
        if (!(parent->type == AF_JSON_OBJECT && py_in(parent, "arcforge"))) {
            char m[1400];
            snprintf(m, sizeof m,
                     "extends target '%s' is not an asset document%s",
                     ref, where);
            extends_path_diag(diags, m, ref, "an asset document", 0);
            af_json_free(parent);
            af_json_free(owned);
            return NULL;
        }
        if (!py_in(parent, "extends")) {
            /* a complete document is checked here; a patch's own fields
             * are checked on the merged result by compile_asset */
            af_json *bad = schema_diags(parent, "asset.schema.json", "");
            if (bad && bad->n) {
                const af_json *d0 = bad->items[0];
                const char *code = "", *dpath = "", *dmsg = "";
                const af_json *jn = af_json_get(d0, "code");
                if (jn && jn->type == AF_JSON_STRING) code = jn->s;
                jn = af_json_get(d0, "path");
                if (jn && jn->type == AF_JSON_STRING) dpath = jn->s;
                jn = af_json_get(d0, "message");
                if (jn && jn->type == AF_JSON_STRING) dmsg = jn->s;
                char m[2600];
                snprintf(m, sizeof m,
                         "extends target '%s' is invalid: %s at %s: %s%s",
                         ref, code, dpath[0] ? dpath : "/", dmsg, where);
                extends_path_diag(diags, m, ref, "a valid asset document", 0);
                af_json_free(bad);
                af_json_free(parent);
                af_json_free(owned);
                return NULL;
            }
            if (g_crash) {
                af_json_free(bad);
                af_json_free(parent);
                af_json_free(owned);
                return NULL;
            }
            af_json_free(bad);
        }
        /* own = cur minus the extends key; cur = merge(parent, own) */
        af_json *own = af_json_obj();
        if (!own) { crash("out of memory"); af_json_free(parent);
                    af_json_free(owned); return NULL; }
        for (size_t i = 0; i + 1 < cur->n; i += 2) {
            const af_json *k = cur->items[i];
            if (k->type == AF_JSON_STRING && strcmp(k->s, "extends") == 0)
                continue;
            af_json *c = af_json_clone(cur->items[i + 1]);
            if (!c || af_json_obj_put(own, k->s, c) != 0) {
                af_json_free(c);
                af_json_free(own);
                af_json_free(parent);
                af_json_free(owned);
                crash("out of memory");
                return NULL;
            }
        }
        af_json *merged = merge_patch(parent, own);
        af_json_free(own);
        af_json_free(parent);
        if (!merged) { af_json_free(owned); return NULL; }
        af_json_free(owned);
        owned = merged;
        cur = merged;
        snprintf(cur_file, sizeof cur_file, "%s", target);
        hops++;
    }
    if (owned) return owned;
    af_json *c = af_json_clone(doc);
    if (!c) crash("out of memory");
    return c;
}

/* ---- enumerate (Python `enumerate(x)`) --------------------------------- */

/* Owned array of the iterated nodes: array items (cloned), object keys,
 * string code points. null/int/float/bool -> crash (TypeError). */
static af_json *enumerate_nodes(const af_json *v) {
    af_json *out = af_json_arr();
    if (!out) { crash("out of memory"); return NULL; }
    if (!v) return out;
    switch (v->type) {
    case AF_JSON_ARRAY:
        for (size_t i = 0; i < v->n; i++) {
            af_json *c = af_json_clone(v->items[i]);
            if (!c || af_json_arr_push(out, c) != 0) {
                af_json_free(c); af_json_free(out);
                crash("out of memory");
                return NULL;
            }
        }
        return out;
    case AF_JSON_OBJECT:
        for (size_t i = 0; i + 1 < v->n; i += 2) {
            af_json *c = af_json_clone(v->items[i]);
            if (!c || af_json_arr_push(out, c) != 0) {
                af_json_free(c); af_json_free(out);
                crash("out of memory");
                return NULL;
            }
        }
        return out;
    case AF_JSON_STRING: {
        const unsigned char *s = (const unsigned char *)v->s;
        size_t n = v->slen, i = 0;
        while (i < n) {
            size_t want = s[i] < 0x80 ? 1 : (s[i] & 0xe0) == 0xc0 ? 2 :
                          (s[i] & 0xf0) == 0xe0 ? 3 : 4;
            if (i + want > n) want = 1;         /* utf8_valid ran at parse */
            af_json *c = af_json_strn((const char *)s + i, want);
            if (!c || af_json_arr_push(out, c) != 0) {
                af_json_free(c); af_json_free(out);
                crash("out of memory");
                return NULL;
            }
            i += want;
        }
        return out;
    }
    default:
        af_json_free(out);
        crash("bad document");
        return NULL;
    }
}

/* ---- expand_variants --------------------------------------------------- */

typedef struct {
    af_json *id;                 /* cloned id node (usually a string) */
    af_json *doc;                /* owned compiled document */
    char pfx[48];                /* "" or "/variants/<i>" */
} adoc;

static void adocs_free(adoc *a, size_t n) {
    if (!a) return;
    for (size_t i = 0; i < n; i++) {
        af_json_free(a[i].id);
        af_json_free(a[i].doc);
    }
    free(a);
}

/* known[] membership for a STRING key: Python dict lookup with a str --
 * string keys compare by content, non-string keys never match a str
 * (Python == between str and int/float/bool/None is False). */
static int known_has(const af_json **keys, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++)
        if (keys[i]->type == AF_JSON_STRING &&
            strcmp(keys[i]->s, s) == 0)
            return 1;
    return 0;
}

static const af_json *known_get(const af_json **keys, const af_json **vals,
                                size_t n, const char *s) {
    for (size_t i = 0; i < n; i++)
        if (keys[i]->type == AF_JSON_STRING &&
            strcmp(keys[i]->s, s) == 0)
            return vals[i];
    return NULL;
}

/* sorted(known): sorted key nodes; ANY non-string key -> crash (Python's
 * sorted() on mixed types raises TypeError; premature crash == empty
 * stdout, which is what the oracle produces). */
static af_json *sorted_key_nodes(const af_json **keys, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (keys[i]->type != AF_JSON_STRING) { crash("bad document"); return NULL; }
    af_json *out = af_json_arr();
    if (!out) { crash("out of memory"); return NULL; }
    for (size_t i = 0; i < n; i++) {
        af_json *c = af_json_clone(keys[i]);
        if (!c || af_json_arr_push(out, c) != 0) {
            af_json_free(c); af_json_free(out);
            crash("out of memory");
            return NULL;
        }
    }
    /* insertion sort by byte order (stable, like sorted()) */
    for (size_t i = 1; i < out->n; i++) {
        af_json *key = out->items[i];
        size_t j = i;
        while (j && strcmp(out->items[j - 1]->s, key->s) > 0) {
            out->items[j] = out->items[j - 1];
            j--;
        }
        out->items[j] = key;
    }
    return out;
}

/* truthy non-dict `asset` in dict(doc.get("asset") or {}, id=vid): str /
 * int / float / bool / None -> TypeError in dict(); a list of 2-item
 * pairs works in Python (documented divergence: C crashes). */
static af_json *asset_with_id(const af_json *doc, const char *vid) {
    const af_json *a = af_json_get(doc, "asset");
    if (!a || !af_truthy(a)) {
        af_json *o = af_json_obj();
        if (!o) { crash("out of memory"); return NULL; }
        if (af_json_obj_put(o, "id", af_json_str(vid)) != 0) {
            af_json_free(o);
            crash("out of memory");
            return NULL;
        }
        return o;
    }
    if (a->type != AF_JSON_OBJECT) { crash("bad document"); return NULL; }
    af_json *c = af_json_clone(a);
    if (!c) { crash("out of memory"); return NULL; }
    af_json *id = af_json_str(vid);
    if (!id || af_json_obj_set(c, "id", id) != 0) {
        af_json_free(id);
        af_json_free(c);
        crash("out of memory");
        return NULL;
    }
    return c;
}

/* expand_variants(base, diags, ...): the oracle's expansion. Appends
 * file-level diagnostics to diags; on any appended diagnostic the docs
 * are discarded by the caller (the oracle returns ([], diags, base)).
 * Returns 0 with the docs array and its count filled, or -1 with
 * g_crash set. */
static int expand_variants(const af_json *base, af_json *diags,
                           adoc **docs, size_t *n_out) {
    *docs = NULL;
    *n_out = 0;
    if (!base || base->type != AF_JSON_OBJECT) { crash("bad document"); return -1; }

    /* entries = base.get("variants") or [] -- falsy coerces to empty */
    const af_json *ev = af_json_get(base, "variants");
    af_json *entries = NULL;
    if (ev && af_truthy(ev)) {
        entries = enumerate_nodes(ev);
        if (!entries) return -1;
    } else {
        entries = af_json_arr();
        if (!entries) { crash("out of memory"); return -1; }
    }

    /* base_doc = base minus the variants key (values cloned: the docs
     * handed out must not share nodes with `base`) */
    af_json *base_doc = af_json_obj();
    if (!base_doc) { af_json_free(entries); crash("out of memory"); return -1; }
    for (size_t i = 0; i + 1 < base->n; i += 2) {
        const af_json *k = base->items[i];
        if (k->type == AF_JSON_STRING && strcmp(k->s, "variants") == 0)
            continue;
        af_json *c = af_json_clone(base->items[i + 1]);
        if (!c || af_json_obj_put(base_doc, k->s, c) != 0) {
            af_json_free(c);
            af_json_free(base_doc);
            af_json_free(entries);
            crash("out of memory");
            return -1;
        }
    }

    /* base_id = base_doc["asset"]["id"] -- KeyError/TypeError -> crash */
    const af_json *asset = af_json_get(base_doc, "asset");
    if (!asset || asset->type != AF_JSON_OBJECT) {
        af_json_free(base_doc);
        af_json_free(entries);
        crash("bad document");
        return -1;
    }
    const af_json *idn = af_json_get(asset, "id");
    if (!idn) {
        af_json_free(base_doc);
        af_json_free(entries);
        crash("bad document");
        return -1;
    }
    if (idn->type == AF_JSON_ARRAY || idn->type == AF_JSON_OBJECT) {
        af_json_free(base_doc);
        af_json_free(entries);
        crash("bad document");          /* unhashable dict key */
        return -1;
    }

    size_t cap = 8, n = 0;
    adoc *out = (adoc *)calloc(cap, sizeof *out);
    if (!out) {
        af_json_free(base_doc);
        af_json_free(entries);
        crash("out of memory");
        return -1;
    }
    /* known maps compiled ids to their documents (insertion order
     * irrelevant: lookups are equality scans, sorting sorts) */
    const af_json **keys = (const af_json **)calloc(cap, sizeof *keys);
    const af_json **vals = (const af_json **)calloc(cap, sizeof *vals);
    if (!keys || !vals) {
        free(keys); free(vals); free(out);
        af_json_free(base_doc);
        af_json_free(entries);
        crash("out of memory");
        return -1;
    }
    af_json *base_id = af_json_clone(idn);
    if (!base_id) {
        free(keys); free(vals); free(out);
        af_json_free(base_doc);
        af_json_free(entries);
        crash("out of memory");
        return -1;
    }
    out[0].id = base_id;
    out[0].doc = base_doc;
    out[0].pfx[0] = 0;
    keys[0] = base_id;
    vals[0] = base_doc;
    n = 1;

    int rc = 0;
    for (size_t i = 0; i < entries->n && rc == 0; i++) {
        const af_json *e = entries->items[i];
        char p[48];
        snprintf(p, sizeof p, "/variants/%zu", i);

        if (!e || e->type != AF_JSON_OBJECT) {
            diags_push(diags, D("E_SCHEMA", p, "'id' is a required property",
                                NULL, af_json_str("string"), NULL, NULL));
            continue;
        }
        const af_json *idv = af_json_get(e, "id");
        if (!idv || idv->type != AF_JSON_STRING) {
            diags_push(diags, D("E_SCHEMA", p, "'id' is a required property",
                                NULL, af_json_str("string"), NULL, NULL));
            continue;
        }
        const char *vid = idv->s;

        const af_json *from = NULL;
        if (af_json_get(e, "from")) {
            from = af_json_get(e, "from");
            if (from->type != AF_JSON_STRING) {
                char pp[64];
                snprintf(pp, sizeof pp, "%s/from", p);
                diags_push(diags,
                           D("E_SCHEMA", pp, "'from' must be a string",
                             af_json_str(py_typename(from)),
                             af_json_str("string"), NULL, NULL));
                continue;
            }
        }
        const af_json *ovr = NULL;
        if (af_json_get(e, "override")) {
            ovr = af_json_get(e, "override");
            if (ovr->type != AF_JSON_OBJECT) {
                char pp[64];
                snprintf(pp, sizeof pp, "%s/override", p);
                diags_push(diags,
                           D("E_SCHEMA", pp, "'override' must be an object",
                             af_json_str(py_typename(ovr)),
                             af_json_str("object"), NULL, NULL));
                continue;
            }
        }

        if (known_has(keys, n, vid)) {
            af_json *sorted = sorted_key_nodes(keys, n);
            if (!sorted) { rc = -1; break; }
            char m[768];
            snprintf(m, sizeof m,
                     "compiled id '%s' is already used in this file", vid);
            char pp[64];
            snprintf(pp, sizeof pp, "%s/id", p);
            diags_push(diags,
                       D("E_DUPLICATE_ID", pp, m, af_json_str(vid), sorted,
                         NULL, fix_op("remove", p)));
            continue;
        }

        const af_json *src;
        if (from) {
            if (!known_has(keys, n, from->s)) {
                af_json *sorted = sorted_key_nodes(keys, n);
                if (!sorted) { rc = -1; break; }
                af_json *sug = close_arr(from, sorted);
                if (!sug) { af_json_free(sorted); rc = -1; break; }
                char m[768];
                snprintf(m, sizeof m,
                         "no variant '%s' earlier in this file to build from",
                         from->s);
                char pp[64];
                snprintf(pp, sizeof pp, "%s/from", p);
                diags_push(diags,
                           D("E_UNKNOWN_VARIANT", pp, m, af_json_str(from->s),
                             sorted, sug, fix_op("remove", pp)));
                continue;
            }
            src = known_get(keys, vals, n, from->s);
        } else {
            src = base_doc;
        }

        /* doc = merge_patch(src, override or {}) */
        af_json *empty = NULL;
        const af_json *patch = ovr;
        if (!patch) {
            empty = af_json_obj();
            if (!empty) { rc = -1; break; }
            patch = empty;
        }
        af_json *doc = merge_patch(src, patch);
        af_json_free(empty);
        if (!doc) { rc = -1; break; }

        /* palette_swap branch (key present incl. null) */
        const af_json *ps = af_json_get(e, "palette_swap");
        if (ps) {
            const af_json *cur = af_json_get(doc, "palette_swap");
            af_json *pbase = NULL;
            if (!cur || !af_truthy(cur)) {
                pbase = af_json_obj();
                if (!pbase) { af_json_free(doc); rc = -1; break; }
                cur = pbase;
            }
            af_json *merged = merge_patch(cur, ps);
            af_json_free(pbase);
            if (!merged) { af_json_free(doc); rc = -1; break; }
            if (af_json_obj_set(doc, "palette_swap", merged) != 0) {
                af_json_free(merged);
                af_json_free(doc);
                rc = -1;
                break;
            }
        }

        /* doc["asset"] = dict(doc.get("asset") or {}, id=vid) */
        af_json *na = asset_with_id(doc, vid);
        if (!na) { af_json_free(doc); rc = -1; break; }
        if (af_json_obj_set(doc, "asset", na) != 0) {
            af_json_free(na);
            af_json_free(doc);
            rc = -1;
            break;
        }
        af_json_obj_del(doc, "extends");
        af_json_obj_del(doc, "variants");

        if (n == cap) {
            size_t ncap = cap * 2;
            adoc *no = (adoc *)realloc(out, ncap * sizeof *no);
            if (!no) { af_json_free(doc); rc = -1; break; }
            out = no;
            const af_json **nk = (const af_json **)realloc((void *)keys, ncap * sizeof *nk);
            if (!nk) { af_json_free(doc); rc = -1; break; }
            keys = nk;
            const af_json **nv = (const af_json **)realloc((void *)vals, ncap * sizeof *nv);
            if (!nv) { af_json_free(doc); rc = -1; break; }
            vals = nv;
            memset(out + cap, 0, (ncap - cap) * sizeof *out);
            cap = ncap;
        }
        af_json *vidc = af_json_clone(idv);
        if (!vidc) { af_json_free(doc); rc = -1; break; }
        out[n].id = vidc;
        out[n].doc = doc;
        snprintf(out[n].pfx, sizeof out[n].pfx, "%s", p);
        keys[n] = vidc;
        vals[n] = doc;
        n++;
    }

    af_json_free(entries);
    free((void *)keys);
    free((void *)vals);
    if (rc != 0) {
        adocs_free(out, n);
        return -1;
    }
    *docs = out;
    *n_out = n;
    return 0;
}

/* ---- report ------------------------------------------------------------ */

/* asset_id(asset): `asset.get("asset", {}).get("id", "?")` -- a missing
 * asset block is "?", a present non-dict one is the oracle's AttributeError. */
static af_json *asset_id(const af_json *doc) {
    if (!doc || doc->type != AF_JSON_OBJECT) return af_json_str("?");
    const af_json *a = af_json_get(doc, "asset");
    if (!a) return af_json_str("?");
    if (a->type != AF_JSON_OBJECT) { crash("bad document"); return NULL; }
    const af_json *id = af_json_get(a, "id");
    if (!id) return af_json_str("?");
    af_json *c = af_json_clone(id);
    if (!c) crash("out of memory");
    return c;
}

/* report(stage, asset, diags, stats): key order status/asset/stage/
 * diagnostics[/stats]; status FAIL iff an error severity is present.
 * Takes ownership of asset (NULL -> "?"), diags and stats (NULL skips). */
static af_json *report_json(const char *stage, af_json *asset,
                            af_json *diags, af_json *stats) {
    af_json *r = af_json_obj();
    if (!asset) asset = af_json_str("?");
    if (!diags) diags = af_json_arr();
    if (!r || !asset || !diags) {
        af_json_free(r); af_json_free(asset); af_json_free(diags);
        af_json_free(stats);
        crash("out of memory");
        return NULL;
    }
    af_json *sv = af_json_str(has_error(diags) ? "FAIL" : "PASS");
    af_json *st = af_json_str(stage);
    if (!sv || af_json_obj_put(r, "status", sv) != 0) {
        af_json_free(sv);
        goto fail;
    }
    if (af_json_obj_put(r, "asset", asset) != 0) { af_json_free(asset); asset = NULL; goto fail; }
    asset = NULL;
    if (!st || af_json_obj_put(r, "stage", st) != 0) { af_json_free(st); goto fail; }
    st = NULL;
    if (af_json_obj_put(r, "diagnostics", diags) != 0) { af_json_free(diags); diags = NULL; goto fail; }
    diags = NULL;
    if (stats) {
        if (af_json_obj_put(r, "stats", stats) != 0) { af_json_free(stats); stats = NULL; goto fail; }
        stats = NULL;
    }
    return r;
fail:
    af_json_free(r); af_json_free(asset); af_json_free(diags); af_json_free(stats);
    crash("out of memory");
    return NULL;
}

/* prefixed(d3, pfx): a variant's diagnostics re-pathed under /variants/<i>
 * (dict(d, path=pfx + d["path"]) keeps the key position); the nodes move
 * into `out`, d3's shell is discarded. */
static void prefixed_push(af_json *out, af_json *d3, const char *pfx) {
    if (!d3) return;
    if (pfx && pfx[0]) {
        for (size_t i = 0; i < d3->n && !g_crash; i++) {
            af_json *d = d3->items[i];
            const af_json *p = af_json_get(d, "path");
            char buf[1700];
            snprintf(buf, sizeof buf, "%s%s", pfx,
                     p && p->type == AF_JSON_STRING ? p->s : "");
            af_json *np = af_json_str(buf);
            if (!np || af_json_obj_set(d, "path", np) != 0) {
                af_json_free(np);
                crash("out of memory");
            }
        }
    }
    for (size_t i = 0; i < d3->n; i++) {
        if (af_json_arr_push(out, d3->items[i]) != 0) {
            af_json_free(d3->items[i]);
            crash("out of memory");
        }
    }
    d3->n = 0;                      /* ownership moved to `out` */
    af_json_free(d3);
}

/* ---- known_parts(reg) -------------------------------------------------- */

/* one name into the set (deduped, insertion order irrelevant: sorted) */
static int add_name(af_json *names, const char *s, size_t len) {
    for (size_t i = 0; i < names->n; i++)
        if (names->items[i]->type == AF_JSON_STRING &&
            names->items[i]->slen == len &&
            memcmp(names->items[i]->s, s, len) == 0)
            return 0;
    af_json *c = af_json_strn(s, len);
    if (!c || af_json_arr_push(names, c) != 0) {
        af_json_free(c);
        crash("out of memory");
        return -1;
    }
    return 0;
}

/* set(arch["parts"]): dict keys / array of strings / string code points;
 * anything else is the oracle's TypeError/KeyError (crash). */
static int add_part_names(af_json *names, const af_json *v) {
    if (!v) { crash("bad registry"); return -1; }
    switch (v->type) {
    case AF_JSON_OBJECT:
        for (size_t i = 0; i + 1 < v->n; i += 2)
            if (add_name(names, v->items[i]->s, v->items[i]->slen) != 0)
                return -1;
        return 0;
    case AF_JSON_ARRAY:
        for (size_t i = 0; i < v->n; i++) {
            if (v->items[i]->type != AF_JSON_STRING) {
                crash("bad registry");       /* set() of a non-str member */
                return -1;
            }
            if (add_name(names, v->items[i]->s, v->items[i]->slen) != 0)
                return -1;
        }
        return 0;
    case AF_JSON_STRING: {
        const unsigned char *s = (const unsigned char *)v->s;
        size_t n = v->slen, i = 0;
        while (i < n) {
            size_t want = s[i] < 0x80 ? 1 : (s[i] & 0xe0) == 0xc0 ? 2 :
                          (s[i] & 0xf0) == 0xe0 ? 3 : 4;
            if (i + want > n) want = 1;
            if (add_name(names, (const char *)s + i, want) != 0) return -1;
            i += want;
        }
        return 0;
    }
    default:
        crash("bad registry");                /* set() of an unhashable */
        return -1;
    }
}

/* sorted(set-union over every archetype of parts + anchor targets) */
static af_json *known_parts(const af_json *reg) {
    const af_json *arches = need(reg, "archetypes");
    if (g_crash) return NULL;
    if (arches->type != AF_JSON_OBJECT) { crash("bad registry"); return NULL; }
    af_json *names = af_json_arr();
    if (!names) { crash("out of memory"); return NULL; }
    for (size_t i = 0; i + 1 < arches->n; i += 2) {
        const af_json *arch = arches->items[i + 1];
        if (add_part_names(names, af_json_get(arch, "parts")) != 0) {
            af_json_free(names);
            return NULL;
        }
        const af_json *anc = af_json_get(arch, "anchors");
        if (anc) {
            if (anc->type != AF_JSON_OBJECT) {
                crash("bad registry");        /* None.values() */
                af_json_free(names);
                return NULL;
            }
            for (size_t j = 0; j + 1 < anc->n; j += 2) {
                const af_json *a = anc->items[j + 1];
                if (!a || a->type != AF_JSON_OBJECT) {
                    crash("bad registry");
                    af_json_free(names);
                    return NULL;
                }
                const af_json *pt = af_json_get(a, "part");
                if (!pt || pt->type != AF_JSON_STRING) {
                    crash("bad registry");
                    af_json_free(names);
                    return NULL;
                }
                if (add_name(names, pt->s, pt->slen) != 0) {
                    af_json_free(names);
                    return NULL;
                }
            }
        }
    }
    af_json *sorted = sorted_strs(names);
    af_json_free(names);
    return sorted;
}

/* ---- document kind predicates ------------------------------------------ */

static int is_style_pack_doc(const af_json *doc) {
    return doc && doc->type == AF_JSON_OBJECT &&
           af_json_get(doc, "style_pack") && af_json_get(doc, "ramps") &&
           !af_json_get(doc, "arcforge");
}

static int is_recipe_doc(const af_json *doc) {
    return doc && doc->type == AF_JSON_OBJECT &&
           af_json_get(doc, "recipe") && af_json_get(doc, "geometry") &&
           !af_json_get(doc, "arcforge");
}

/* ---- check_recipe_doc --------------------------------------------------- */

/* schema + keyed-geometry cross-checks (base_path is "" from validate). */
static af_json *check_recipe_doc(const af_json *doc, const char *base_path) {
    af_json *diags = schema_diags(doc, "recipe.schema.json", base_path);
    if (!diags) return NULL;
    if (diags->n) return diags;
    af_json_free(diags);
    diags = af_json_arr();
    if (!diags) { crash("out of memory"); return NULL; }

    const af_json *geo = af_json_get(doc, "geometry");
    if (!geo || geo->type != AF_JSON_OBJECT) return diags;   /* not a dict */

    /* applies = doc.get("applies_to", []) -- absent defaults, present
     * null/other raises in `for a in applies` */
    af_json *applies_owned = NULL;
    const af_json *applies = af_json_get(doc, "applies_to");
    if (!applies) {
        applies_owned = af_json_arr();
        if (!applies_owned) { crash("out of memory"); af_json_free(diags); return NULL; }
        applies = applies_owned;
    }

    const af_json *rn = af_json_get(doc, "recipe");
    char rbuf[64];
    const char *recipe_name = rn ? py_str(rn, rbuf, sizeof rbuf) : "?";

    /* for a in applies: a not in geo */
    for (size_t i = 0; i < applies->n && !g_crash; i++) {
        const af_json *a = applies->items[i];
        if (a->type == AF_JSON_STRING && af_json_get(geo, a->s)) continue;
        char abuf[64];
        const char *as = a->type == AF_JSON_STRING ? a->s
                                                   : py_str(a, abuf, sizeof abuf);
        char m[800];
        snprintf(m, sizeof m,
                 "recipe '%s' has no geometry keyed for applies_to archetype '%s'",
                 recipe_name, as);
        char path[768];
        snprintf(path, sizeof path, "%s/geometry", base_path);
        af_json *obs = sorted_strs(geo);
        af_json *sug = close_arr(a, geo);
        af_json *fix = NULL;
        if (sug && sug->n) {
            char fpath[768];
            snprintf(fpath, sizeof fpath, "%s/geometry", base_path);
            ptr_cat(fpath, sizeof fpath, sug->items[0]->s);
            fix = fix_op("add", fpath);
            const af_json *gv = af_json_get(geo, sug->items[0]->s);
            if (fix && gv) fix_value(fix, af_json_clone(gv));
        }
        diags_push(diags,
                   D("E_RECIPE_GEOMETRY", path, m, obs, af_json_clone(a),
                     sug, fix));
    }

    /* for k in geo: k not in applies */
    for (size_t i = 0; i + 1 < geo->n && !g_crash; i += 2) {
        const af_json *k = geo->items[i];
        if (k->type == AF_JSON_STRING && py_in(applies, k->s)) continue;
        char path[768], m[800];
        snprintf(path, sizeof path, "%s/geometry", base_path);
        ptr_cat(path, sizeof path, k->s);
        snprintf(m, sizeof m, "geometry key '%s' is not in applies_to",
                 k->type == AF_JSON_STRING ? k->s : "?");
        diags_push(diags,
                   D("E_RECIPE_GEOMETRY", path, m, af_json_clone(k),
                     af_json_clone(applies), close_arr(k, applies),
                     fix_op("remove", path)));
    }
    af_json_free(applies_owned);
    if (g_crash) { af_json_free(diags); return NULL; }
    return diags;
}

/* ---- check_style_pack --------------------------------------------------- */

static af_json *check_style_pack(const af_json *doc, const af_json *reg,
                                 const char *base_path) {
    af_json *diags = schema_diags(doc, "style_pack.schema.json", base_path);
    if (!diags) return NULL;
    if (diags->n) return diags;
    af_json_free(diags);
    diags = af_json_arr();
    if (!diags) { crash("out of memory"); return NULL; }

    /* doc.get("outline", {}).get("inner", {}): a present non-dict raises */
    af_json *tmp = NULL;
    const af_json *inner;
    const af_json *outline = af_json_get(doc, "outline");
    if (!outline) {
        tmp = af_json_obj();
        inner = tmp;
    } else if (outline->type == AF_JSON_OBJECT) {
        const af_json *iv = af_json_get(outline, "inner");
        if (!iv) {
            tmp = af_json_obj();
            inner = tmp;
        } else if (iv->type == AF_JSON_OBJECT) {
            inner = iv;
        } else {
            crash("bad document");
            af_json_free(diags);
            return NULL;
        }
    } else {
        crash("bad document");              /* None.get / list.get */
        af_json_free(diags);
        return NULL;
    }
    if (!inner) { af_json_free(tmp); af_json_free(diags); crash("bad document");
                  return NULL; }

    const af_json *mode = af_json_get(inner, "mode");
    if (!mode || mode->type != AF_JSON_STRING || strcmp(mode->s, "part") != 0) {
        af_json_free(tmp);
        return diags;                       /* mode != "part" */
    }

    af_json *allp = known_parts(reg);
    if (!allp) { af_json_free(tmp); af_json_free(diags); return NULL; }

    /* inner.get("parts", []) -- absent defaults; enumerate() otherwise */
    af_json *parr = NULL;
    const af_json *parts = af_json_get(inner, "parts");
    if (!parts) {
        parr = af_json_arr();
        if (!parr) { crash("out of memory"); goto fail; }
        parts = parr;
    } else {
        parr = enumerate_nodes(parts);
        if (!parr) goto fail;
        parts = parr;
    }

    for (size_t i = 0; i < parts->n && !g_crash; i++) {
        const af_json *p = parts->items[i];
        int found = 0;
        if (p->type == AF_JSON_STRING)
            for (size_t j = 0; j < allp->n && !found; j++)
                found = af_json_equal(allp->items[j], p);
        if (found) continue;
        char path[768], m[800];
        snprintf(path, sizeof path, "%s", base_path);
        ptr_cat(path, sizeof path, "outline");
        ptr_cat(path, sizeof path, "inner");
        ptr_cat(path, sizeof path, "parts");
        ptr_int(path, sizeof path, (long long)i);
        char pbuf[64];
        const char *ps = p->type == AF_JSON_STRING ? p->s
                                                   : py_str(p, pbuf, sizeof pbuf);
        snprintf(m, sizeof m, "NO PART EXISTS named '%s' in any archetype", ps);
        af_json *sug = close_arr(p, allp);
        af_json *fix = NULL;
        if (sug && sug->n) {
            fix = fix_op("replace", path);
            if (fix) fix_value(fix, af_json_clone(sug->items[0]));
        } else {
            fix = fix_op("remove", path);
        }
        diags_push(diags,
                   D("E_UNKNOWN_PART", path, m, af_json_clone(p),
                     af_json_clone(allp), sug, fix));
    }
    af_json_free(parr);
    af_json_free(allp);
    af_json_free(tmp);
    if (g_crash) { af_json_free(diags); return NULL; }
    return diags;

fail:
    af_json_free(parr);
    af_json_free(allp);
    af_json_free(tmp);
    af_json_free(diags);
    return NULL;
}

/* ---- load_asset_docs ---------------------------------------------------- */

/* Load an asset file and expand extends + variants. Returns:
 *   0  -> docs array and count filled (owned);
 *   1  -> *xdiags filled (a file-level diagnostic was reported);
 *  -1  -> the oracle would traceback (g_crash). */
static int load_asset_docs(const af_json *doc, const char *path,
                           adoc **docs_out, size_t *n_out,
                           af_json **xdiags_out) {
    *docs_out = NULL;
    *n_out = 0;
    *xdiags_out = NULL;

    int has_ext = py_in(doc, "extends");
    if (g_crash) return -1;
    if (!has_ext) {
        af_json *raw = schema_diags(doc, "asset.schema.json", "");
        if (!raw) return -1;
        if (raw->n) { *xdiags_out = raw; return 1; }
        af_json_free(raw);
    }
    af_json *diags = af_json_arr();
    if (!diags) { crash("out of memory"); return -1; }

    af_json *merged = resolve_extends(doc, path, diags);
    if (!merged) {
        if (g_crash) { af_json_free(diags); return -1; }
        *xdiags_out = diags;
        return 1;
    }
    adoc *docs = NULL;
    size_t n = 0;
    int rc = expand_variants(merged, diags, &docs, &n);
    af_json_free(merged);
    if (rc != 0) { af_json_free(diags); return -1; }
    if (diags->n) {
        adocs_free(docs, n);
        *xdiags_out = diags;
        return 1;
    }
    af_json_free(diags);
    *docs_out = docs;
    *n_out = n;
    return 0;
}

/* ---- the command -------------------------------------------------------- */

af_json *af_validate(const char *asset_path, const char *argv0) {
    g_crash = 0;
    g_why[0] = 0;
    find_root(argv0);

    char regdir[1200];
    snprintf(regdir, sizeof regdir, "%s/registry", g_root);
    char err[512];
    af_registry *reg = af_registry_load(regdir, err, sizeof err);
    if (!reg) { crash("registry"); return NULL; }
    af_json *regj = af_registry_json(reg, err, sizeof err);
    af_registry_free(reg);
    if (!regj) { crash("registry"); return NULL; }

    af_json *doc = load_json_file(asset_path);
    if (!doc) { af_json_free(regj); return NULL; }

    char base[1024];
    python_parent(asset_path, base, sizeof base);

    af_json *report = NULL;
    adoc *docs = NULL;
    size_t nd = 0;

    if (is_style_pack_doc(doc)) {
        af_json *diags = check_style_pack(doc, regj, "");
        if (!diags) goto out;
        const af_json *sp = af_json_get(doc, "style_pack");
        af_json *aid = sp ? af_json_clone(sp) : af_json_str("?");
        report = report_json("validate", aid, diags, NULL);
    } else if (is_recipe_doc(doc)) {
        af_json *diags = check_recipe_doc(doc, "");
        if (!diags) goto out;
        const af_json *rv = af_json_get(doc, "recipe");
        af_json *aid = rv ? af_json_clone(rv) : af_json_str("?");
        report = report_json("validate", aid, diags, NULL);
    } else {
        af_json *xdiags = NULL;
        int rc = load_asset_docs(doc, asset_path, &docs, &nd, &xdiags);
        if (rc < 0) goto out;
        if (rc == 1) {
            report = report_json("validate", asset_id(doc), xdiags, NULL);
        } else {
            af_json *diags = af_json_arr();
            af_json *ids = af_json_arr();
            if (!diags || !ids) {
                af_json_free(diags);
                af_json_free(ids);
                crash("out of memory");
                goto out;
            }
            for (size_t i = 0; i < nd && !g_crash; i++) {
                af_json *d3 = compile_asset(docs[i].doc, regj, base);
                if (!d3) break;             /* g_crash set */
                prefixed_push(diags, d3, docs[i].pfx);
                af_json *idc = af_json_clone(docs[i].id);
                if (!idc || af_json_arr_push(ids, idc) != 0) {
                    af_json_free(idc);
                    crash("out of memory");
                    break;
                }
            }
            af_json *stats = NULL;
            if (!g_crash && nd > 1) {
                stats = af_json_obj();
                if (!stats || af_json_obj_put(stats, "ids", ids) != 0) {
                    af_json_free(stats);
                    stats = NULL;
                    af_json_free(ids);
                    ids = NULL;
                    crash("out of memory");
                } else {
                    ids = NULL;
                }
            }
            report = report_json("validate", g_crash ? NULL : asset_id(doc),
                                 diags, stats);
            af_json_free(ids);
        }
    }
out:
    adocs_free(docs, nd);
    af_json_free(regj);
    af_json_free(doc);
    if (!report || g_crash) {
        af_json_free(report);
        if (!g_crash) crash("report");
        return NULL;
    }
    return report;
}

const char *af_validate_why(void) {
    return g_why;
}

