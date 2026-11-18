/* schema.c -- M4.1b: the hand-coded, table-driven schema validator.
 *
 * The oracle is tools/arcforge_ref.py::schema_diags (arcforge AGENTS rule 8:
 * the Python tool stays the oracle until M4.3 flips the gate). It has three
 * sources, and this file reproduces each one byte-for-byte:
 *
 *   1. the ERROR SET of jsonschema 4.10.3 (Draft 2020-12) for the keyword
 *      subset that the schema documents use -- see the dispatch table in
 *      validate() -- including its exact type checker, its two equality
 *      functions (equal() vs Python ==), its enum/uniq special cases, its
 *      document-order keyword iteration and its oneOf branch semantics;
 *   2. the REWORDING of _collect_schema_error / _collect_oneof (jsonschema's
 *      own message text is never used: the oracle composes every sentence);
 *   3. the P0.2 format-vocabulary walk (_collect_formats) and the P0.3 int32
 *      range walk (_collect_int_range), then the (path-tuple, rank, kind,
 *      message) stable sort and the (code, path, message) dedup.
 *
 * Keyword boundary (a finding, not an accident): the schema documents
 * contain no
 * anyOf / not / pattern / contains / multipleOf / minLength / maxLength /
 * dependent* / unevaluated* / patternProperties, and no bare boolean
 * subschema. Those rows are absent from the table on purpose -- unexercised
 * code cannot be differentially tested. Adding such a keyword to a schema
 * requires its handler here in the same change (arcforge AGENTS rule 7).
 * `format` is absent too: jsonschema's checker only knows the standard
 * formats, so its handler never yields for our vocabulary; the vocabulary
 * itself is checked by collect_formats() below.
 *
 * Known-unreachable divergences, documented where they sit: a `false`
 * subschema would yield a validator=None error in jsonschema (ours: skip),
 * an unresolvable $ref raises there (ours: fatal), and anyOf's context
 * fan-out is not plumbed (oneOf carries no context in the collector).
 * The corpus and the mutation differential prove none of them fire.
 *
 * Determinism: no locale, no clock, no hash order -- file globs are sorted
 * by byte order (Python's sorted(Path)), object iteration follows document
 * order, and the sort carries the emission index as its last key.
 */
#include "schema.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- */
/* string builder                                                     */
/* ---------------------------------------------------------------- */
typedef struct {
    char *s;
    size_t n, cap;
} sb;

static int s_oom; /* sticky: any builder failure fails the whole check */

static int sb_reserve(sb *b, size_t extra) {
    if (b->n + extra + 1 <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->n + extra + 1) cap *= 2;
    char *s = (char *)realloc(b->s, cap);
    if (!s) { s_oom = 1; return -1; }
    b->s = s;
    b->cap = cap;
    return 0;
}

static int sb_putc(sb *b, char c) {
    if (sb_reserve(b, 1)) return -1;
    b->s[b->n++] = c;
    b->s[b->n] = '\0';
    return 0;
}

static int sb_putsn(sb *b, const char *s, size_t n) {
    if (sb_reserve(b, n)) return -1;
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
    return 0;
}

static int sb_puts(sb *b, const char *s) { return sb_putsn(b, s, strlen(s)); }

static int sb_i64(sb *b, int64_t v) {
    char tmp[32];
    int k = snprintf(tmp, sizeof tmp, "%lld", (long long)v);
    return k < 0 ? -1 : sb_putsn(b, tmp, (size_t)k);
}

/* Consume the built string (NUL-terminated); the builder restarts empty. */
static char *sb_take(sb *b) {
    if (!b->s) {
        b->s = (char *)malloc(1);
        if (!b->s) { s_oom = 1; return NULL; }
        b->s[0] = '\0';
        b->cap = 1;
    }
    char *s = b->s;
    b->s = NULL;
    b->n = b->cap = 0;
    return s;
}

static void sb_free(sb *b) {
    free(b->s);
    b->s = NULL;
    b->n = b->cap = 0;
}

/* ---------------------------------------------------------------- */
/* JSON pointer (arcforge_ref.py::ptr): "/" + part, "~"→"~0", "/"→"~1" */
/* ---------------------------------------------------------------- */
static int ptr_part(sb *b, const char *part) {
    if (sb_putc(b, '/')) return -1;
    for (const char *p = part; *p; p++) {
        if (*p == '~') { if (sb_puts(b, "~0")) return -1; }
        else if (*p == '/') { if (sb_puts(b, "~1")) return -1; }
        else if (sb_putc(b, *p)) return -1;
    }
    return 0;
}

static int ptr_i64(sb *b, int64_t i) {
    if (sb_putc(b, '/')) return -1;
    return sb_i64(b, i);
}

/* ---------------------------------------------------------------- */
/* value predicates (the draft202012 type checker of jsonschema._types) */
/* ---------------------------------------------------------------- */
static int is_obj(const af_json *v) { return v && v->type == AF_JSON_OBJECT; }
static int is_arr(const af_json *v) { return v && v->type == AF_JSON_ARRAY; }
static int is_str(const af_json *v) { return v && v->type == AF_JSON_STRING; }
static int is_int(const af_json *v) { return v && v->type == AF_JSON_INT; }
static int is_float(const af_json *v) { return v && v->type == AF_JSON_FLOAT; }
static int is_booln(const af_json *v) { return v && v->type == AF_JSON_BOOL; }
static int is_null(const af_json *v) { return v && v->type == AF_JSON_NULL; }

/* "number": int or float, bools excluded (they inherit from int in Python). */
static int is_number(const af_json *v) { return is_int(v) || is_float(v); }

/* Object key count (Python len(dict)). */
static size_t obj_count(const af_json *o) { return o->n / 2; }

static const af_json *obj_get(const af_json *o, const char *key) {
    return is_obj(o) ? af_json_get(o, key) : NULL;
}

static int obj_has(const af_json *o, const char *key) {
    return obj_get(o, key) != NULL;
}

/* "integer": bools never, ints yes, floats iff integral (float.is_integer
 * -- every double >= 2^52 is integral because its spacing is >= 1). */
static int is_integer_value(const af_json *v) {
    if (is_int(v)) return 1;
    if (!is_float(v)) return 0;
    double d = v->d;
    if (d >= 4503599627370496.0 || d <= -4503599627370496.0) return 1;
    return (double)(int64_t)d == d; /* |d| < 2^52: in range, truncation ok */
}

/* is_type(instance, name) for the eight names a schema may use. An unknown
 * name raises UndefinedTypeCheck in Python (schema bug); no schema has one. */
static int af_is_type(const af_json *inst, const char *name) {
    if (strcmp(name, "array") == 0) return is_arr(inst);
    if (strcmp(name, "boolean") == 0) return is_booln(inst);
    if (strcmp(name, "integer") == 0) return is_integer_value(inst);
    if (strcmp(name, "number") == 0) return is_number(inst);
    if (strcmp(name, "object") == 0) return is_obj(inst);
    if (strcmp(name, "string") == 0) return is_str(inst);
    if (strcmp(name, "null") == 0) return is_null(inst);
    return 0;
}

/* Python truthiness (used for keyword values like uniqueItems: true). */
static int truthy(const af_json *v) {
    if (!v) return 0;
    switch (v->type) {
    case AF_JSON_NULL: return 0;
    case AF_JSON_BOOL: return v->b;
    case AF_JSON_INT: return v->i != 0;
    case AF_JSON_FLOAT: return v->d != 0;
    case AF_JSON_STRING: return v->slen != 0;
    default: return v->n != 0;
    }
}

/* ---------------------------------------------------------------- */
/* exact numeric comparison (an int64 never rounds when Python compares */
/* it with a float; no libm -- floor/isnan are avoided on purpose)     */
/* ---------------------------------------------------------------- */
static int cmp_i_d(int64_t i, double d) {
    if (d >= 9223372036854775808.0) return -1;  /* d >= 2^63 > INT64_MAX */
    if (d < -9223372036854775808.0) return 1;   /* d < -2^63 <= INT64_MIN */
    int64_t t = (int64_t)d;                     /* in range: defined */
    if ((double)t == d) return i < t ? -1 : i > t ? 1 : 0;
    if (d > 0.0) return i <= t ? -1 : 1;        /* floor(d) = trunc(d) */
    return i < t ? -1 : 1;                      /* floor(d) = trunc(d)-1 */
}

/* a < b for two JSON numbers, exactly. */
static int num_lt(const af_json *a, const af_json *b) {
    if (is_int(a) && is_int(b)) return a->i < b->i;
    if (is_int(a)) return cmp_i_d(a->i, b->d) < 0;
    if (is_int(b)) return cmp_i_d(b->i, a->d) > 0;
    return a->d < b->d;
}

/* Python == for JSON values: bools collapse into 0/1, containers compare
 * element-wise with the same rule (used by enum's `in`, required's `not
 * in`, and the collector's duplicate scan). */
static int py_eq(const af_json *a, const af_json *b) {
    if (a == b) return 1;
    if (is_str(a) || is_str(b))
        return is_str(a) && is_str(b) && a->slen == b->slen &&
               memcmp(a->s, b->s, a->slen) == 0;
    if (is_arr(a) && is_arr(b)) {
        if (a->n != b->n) return 0;
        for (size_t i = 0; i < a->n; i++)
            if (!py_eq(a->items[i], b->items[i])) return 0;
        return 1;
    }
    if (is_obj(a) && is_obj(b)) {
        if (obj_count(a) != obj_count(b)) return 0;
        for (size_t i = 0; i + 1 < a->n; i += 2) {
            const af_json *v = obj_get(b, a->items[i]->s);
            if (!v || !py_eq(a->items[i + 1], v)) return 0;
        }
        return 1;
    }
    if (is_booln(a) && is_booln(b)) return a->b == b->b;
    if (is_booln(a)) { /* True == 1, False == 0 */
        if (is_int(b)) return b->i == (a->b ? 1 : 0);
        if (is_float(b)) return b->d == (a->b ? 1.0 : 0.0);
        return 0;
    }
    if (is_booln(b)) {
        if (is_int(a)) return a->i == (b->b ? 1 : 0);
        if (is_float(a)) return a->d == (b->b ? 1.0 : 0.0);
        return 0;
    }
    if (is_int(a) && is_int(b)) return a->i == b->i;
    if (is_number(a) && is_number(b))
        return is_int(a) ? cmp_i_d(a->i, b->d) == 0
                         : cmp_i_d(b->i, a->d) == 0;
    if (is_null(a) && is_null(b)) return 1;
    return 0; /* null vs number, container vs scalar, ... */
}

/* jsonschema._utils.unbool: True/False become distinct sentinels, so
 * equal() never lets a bool stand in for 0/1 (unlike Python ==). */
static int unbool_eq(const af_json *a, const af_json *b) {
    if (is_booln(a) && is_booln(b)) return a->b == b->b;
    if (is_booln(a) || is_booln(b)) return 0;
    return py_eq(a, b);
}

/* jsonschema._utils.equal(): str branch, then sequences, then mappings,
 * then unbool equality. */
static int js_eq(const af_json *one, const af_json *two) {
    if (is_str(one) || is_str(two)) return py_eq(one, two);
    if (is_arr(one) && is_arr(two)) {
        if (one->n != two->n) return 0;
        for (size_t i = 0; i < one->n; i++)
            if (!js_eq(one->items[i], two->items[i])) return 0;
        return 1;
    }
    if (is_obj(one) && is_obj(two)) {
        if (obj_count(one) != obj_count(two)) return 0;
        for (size_t i = 0; i + 1 < one->n; i += 2) {
            const af_json *v = obj_get(two, one->items[i]->s);
            if (!v || !js_eq(one->items[i + 1], v)) return 0;
        }
        return 1;
    }
    return unbool_eq(one, two);
}

/* jsonschema._utils.uniq, the uniqueItems trigger. Python first tries a
 * sorted() pass and falls back to a pairwise scan on TypeError; for JSON
 * values the two agree (sort order clusters exactly the pairs equal()
 * equates, and every non-sortable pair lands in the fallback), so the
 * pairwise form is the implementation -- with equal(), not ==. */
static int af_uniq(const af_json *arr) {
    for (size_t i = 0; i < arr->n; i++)
        for (size_t j = 0; j < i; j++)
            if (js_eq(arr->items[j], arr->items[i])) return 0;
    return 1;
}

/* The collector's duplicate scan runs on the ERROR path with Python ==,
 * not equal(): first element that matches any earlier one. */
static const af_json *find_dup(const af_json *arr) {
    for (size_t i = 0; i < arr->n; i++)
        for (size_t j = 0; j < i; j++)
            if (py_eq(arr->items[j], arr->items[i])) return arr->items[i];
    return NULL;
}

/* ---------------------------------------------------------------- */
/* P0.2 vocabulary (arcforge_ref.py::VOCABULARY) -- hand-run, no regex */
/* ---------------------------------------------------------------- */
/* len() counts code points, not bytes; identifiers stay ASCII in practice
 * but the walk is UTF-8-correct either way. */
static size_t cp_len(const char *s) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

static int is_ascii_ident_char(char c) {
    return c == '_' || ('a' <= c && c <= 'z') || ('0' <= c && c <= '9');
}

static int digits_only(const char *s) {
    if (!*s) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* `identifier`: 1..64 characters of [a-z0-9_]. */
static int vocab_identifier(const char *s) {
    size_t n = cp_len(s);
    if (n == 0 || n > 64) return 0;
    for (const char *p = s; *p; p++)
        if (!is_ascii_ident_char(*p)) return 0;
    return 1;
}

/* Split s on sep the way str.split does (no empties dropped). The parts
 * are written into *out as one NUL-separated buffer of count strings. */
static int split_count(const char *s, char sep, char ***out, size_t *count) {
    size_t n = 1;
    for (const char *p = s; *p; p++) if (*p == sep) n++;
    char **parts = (char **)calloc(n, sizeof *parts);
    if (!parts) { s_oom = 1; return -1; }
    size_t k = 0;
    const char *start = s;
    for (const char *p = s;; p++) {
        if (*p == sep || *p == '\0') {
            parts[k] = (char *)malloc((size_t)(p - start) + 1);
            if (!parts[k]) {
                while (k) free(parts[--k]);
                free(parts);
                s_oom = 1;
                return -1;
            }
            memcpy(parts[k], start, (size_t)(p - start));
            parts[k][p - start] = '\0';
            k++;
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    *out = parts;
    *count = k;
    return 0;
}

static void split_free(char **parts, size_t count) {
    for (size_t i = 0; i < count; i++) free(parts[i]);
    free(parts);
}

/* `dotted_identifier`: identifiers joined by "." (at least one part). */
static int vocab_dotted(const char *s) {
    char **parts; size_t n;
    if (split_count(s, '.', &parts, &n)) return 0;
    int ok = 1;
    for (size_t i = 0; i < n; i++)
        if (!vocab_identifier(parts[i])) { ok = 0; break; }
    split_free(parts, n);
    return ok;
}

/* `semver`: exactly three groups of digits joined by ".". */
static int vocab_semver(const char *s) {
    char **parts; size_t n;
    if (split_count(s, '.', &parts, &n)) return 0;
    int ok = n == 3;
    for (size_t i = 0; ok && i < n; i++)
        if (!digits_only(parts[i])) ok = 0;
    split_free(parts, n);
    return ok;
}

/* `category_path`: identifiers joined by "/" (at least one part). */
static int vocab_category_path(const char *s) {
    char **parts; size_t n;
    if (split_count(s, '/', &parts, &n)) return 0;
    int ok = 1;
    for (size_t i = 0; i < n; i++)
        if (!vocab_identifier(parts[i])) { ok = 0; break; }
    split_free(parts, n);
    return ok;
}

/* `parameter_name`: "p_" + identifier, 64 characters total. */
static int vocab_parameter_name(const char *s) {
    if (s[0] != 'p' || s[1] != '_') return 0;
    if (cp_len(s) > 64) return 0;
    return vocab_identifier(s + 2);
}

/* `diagnostic_code`: "E_"/"W_" followed by 1..n of [A-Z_]. */
static int vocab_diagnostic_code(const char *s) {
    if (cp_len(s) < 3) return 0;
    if (!(s[0] == 'E' || s[0] == 'W') || s[1] != '_') return 0;
    for (const char *p = s + 2; *p; p++)
        if (!(('A' <= *p && *p <= 'Z') || *p == '_')) return 0;
    return 1;
}

/* fmt.replace("_", " ") is applied to the message, not to the predicate. */
static int vocab_check(const char *fmt, const char *s) {
    if (strcmp(fmt, "identifier") == 0) return vocab_identifier(s);
    if (strcmp(fmt, "dotted_identifier") == 0) return vocab_dotted(s);
    if (strcmp(fmt, "semver") == 0) return vocab_semver(s);
    if (strcmp(fmt, "category_path") == 0) return vocab_category_path(s);
    if (strcmp(fmt, "parameter_name") == 0) return vocab_parameter_name(s);
    if (strcmp(fmt, "diagnostic_code") == 0) return vocab_diagnostic_code(s);
    return 1; /* not in the vocabulary: jsonschema knows no such format */
}

/* ---------------------------------------------------------------- */
/* kind ranks (arcforge_ref.py::KIND_RANK) -- half of the final sort   */
/* ---------------------------------------------------------------- */
static int kind_rank(const char *kind) {
    static const struct { const char *k; int r; } tab[] = {
        {"additionalProperties", 0}, {"required", 1}, {"type", 2},
        {"enum", 3}, {"const", 4}, {"pattern", 5}, {"minimum", 6},
        {"maximum", 7}, {"minItems", 8}, {"maxItems", 9},
        {"minProperties", 10}, {"maxProperties", 11}, {"uniqueItems", 12},
        {"items", 13}, {"oneOf", 14}, {"anyOf", 15}, {"allOf", 16},
        {"int_range", 17},
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(tab[i].k, kind) == 0) return tab[i].r;
    return 99; /* KIND_UNKNOWN */
}

/* ---------------------------------------------------------------- */
/* raw errors + the validator (jsonschema.iter_errors / descend)       */
/* ---------------------------------------------------------------- */
/* One failure of one keyword. ppath is ptr(*absolute_path): the path
 * from the schema root down to the instance the keyword rejected, with
 * the empty string at the root (prefix is added by the collector). */
typedef struct af_err {
    const char *kind;    /* keyword name, static storage */
    const af_json *vval; /* the keyword's value, inside the schema doc */
    const af_json *inst; /* the instance it rejected */
    const af_json *node; /* the schema node the keyword came from */
    char *ppath;
} af_err;

static void errs_free(af_err *e, size_t n) {
    for (size_t i = 0; i < n; i++) free(e[i].ppath);
    free(e);
}

typedef struct {
    const af_json *root; /* schema root, for $ref */
    char **parts;        /* path stack: raw keys / decimal indices */
    size_t np, cap;
    af_err *errs;
    size_t ne, cape;
    int fatal;
    const char *why;
} vctx;

static void v_fail(vctx *v, const char *why) {
    if (!v->fatal) { v->fatal = 1; v->why = why; }
}

static void v_push(vctx *v, const char *part) {
    if (v->fatal) return;
    if (v->np == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 16;
        char **p = (char **)realloc(v->parts, cap * sizeof *p);
        if (!p) { v_fail(v, "out of memory"); return; }
        v->parts = p;
        v->cap = cap;
    }
    char *copy = (char *)malloc(strlen(part) + 1);
    if (!copy) { v_fail(v, "out of memory"); return; }
    strcpy(copy, part);
    v->parts[v->np++] = copy;
}

static void v_pop(vctx *v) {
    if (v->np) free(v->parts[--v->np]);
}

/* ptr(*absolute_path) for the current stack. */
static char *v_ppath(const vctx *v) {
    sb b = {0};
    for (size_t i = 0; i < v->np; i++)
        if (ptr_part(&b, v->parts[i])) return (sb_free(&b), NULL);
    return sb_take(&b);
}

static void verr(vctx *v, const char *kind, const af_json *vval,
                 const af_json *inst, const af_json *node) {
    if (v->fatal) return;
    if (v->ne == v->cape) {
        size_t cap = v->cape ? v->cape * 2 : 32;
        af_err *p = (af_err *)realloc(v->errs, cap * sizeof *p);
        if (!p) { v_fail(v, "out of memory"); return; }
        v->errs = p;
        v->cape = cap;
    }
    af_err *e = &v->errs[v->ne];
    e->kind = kind;
    e->vval = vval;
    e->inst = inst;
    e->node = node;
    e->ppath = v_ppath(v);
    if (!e->ppath) { v_fail(v, "out of memory"); return; }
    v->ne++;
}

/* Resolve a local "#/..." pointer (arcforge_ref.py::_resolve_ref): hops
 * are unescaped ~1→"/" then ~0→"~", a missing hop or a non-object result
 * yields NULL (jsonschema would raise instead -- unreachable for schema/,
 * whose refs all point at $defs). */
static const af_json *resolve_ref(const af_json *root, const char *ref) {
    if (strncmp(ref, "#/", 2) != 0) return NULL;
    const af_json *node = root;
    const char *p = ref + 2;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        char *part = (char *)malloc(len + 1);
        if (!part) { s_oom = 1; return NULL; }
        size_t k = 0;
        for (size_t i = 0; i < len; i++) {
            if (p[i] == '~' && i + 1 < len && p[i + 1] == '1') {
                part[k++] = '/';
                i++;
            } else if (p[i] == '~' && i + 1 < len && p[i + 1] == '0') {
                part[k++] = '~';
                i++;
            } else {
                part[k++] = p[i];
            }
        }
        part[k] = '\0';
        const af_json *next = is_obj(node) ? af_json_get(node, part) : NULL;
        free(part);
        if (!next) return NULL;
        node = next;
        if (!slash) break;
        p = slash + 1;
    }
    return is_obj(node) ? node : NULL;
}

static void validate(vctx *v, const af_json *inst, const af_json *sch);

static void descend(vctx *v, const af_json *inst, const af_json *sch,
                    const char *part) {
    int pushed = 0;
    if (part && !v->fatal) { v_push(v, part); pushed = 1; }
    validate(v, inst, sch);
    if (pushed) v_pop(v);
}

static void descend_idx(vctx *v, const af_json *inst, const af_json *sch,
                        int64_t idx) {
    char tmp[32];
    int k = snprintf(tmp, sizeof tmp, "%lld", (long long)idx);
    if (k < 0) { v_fail(v, "format"); return; }
    descend(v, inst, sch, tmp);
}

/* Run validate() in a fresh context (oneOf/if branch checks, and the
 * collector's re-checks). Returns 0 with the errors transferred to
 * *errs (caller frees), or -1 with *why set. */
static int v_run(const af_json *root, const af_json *inst, const af_json *sch,
                 af_err **errs, size_t *n, const char **why) {
    vctx sub;
    memset(&sub, 0, sizeof sub);
    sub.root = root;
    validate(&sub, inst, sch);
    *errs = sub.errs;
    *n = sub.ne;
    free(sub.parts);
    if (sub.fatal) {
        errs_free(*errs, *n);
        *errs = NULL;
        *n = 0;
        *why = sub.why;
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* keyword handlers -- the exact control flow of jsonschema._validators */
/* ---------------------------------------------------------------- */

/* $ref: descend with no path part (jsonschema's ref handler). */
static void k_ref(vctx *v, const af_json *vval, const af_json *inst,
                  const af_json *node) {
    (void)node;
    if (!is_str(vval)) { v_fail(v, "$ref is not a string"); return; }
    const af_json *target = resolve_ref(v->root, vval->s);
    if (!target) { v_fail(v, "unresolvable $ref"); return; }
    descend(v, inst, target, NULL);
}

/* properties: only for objects, only for declared keys present. */
static void k_properties(vctx *v, const af_json *vval, const af_json *inst,
                         const af_json *node) {
    (void)node;
    if (!is_obj(inst) || !is_obj(vval)) return;
    for (size_t i = 0; i + 1 < vval->n; i += 2) {
        const char *key = vval->items[i]->s;
        const af_json *sub = vval->items[i + 1];
        if (obj_has(inst, key)) descend(v, obj_get(inst, key), sub, key);
        if (v->fatal) return;
    }
}

/* required: one error per missing key, in list order. */
static void k_required(vctx *v, const af_json *vval, const af_json *inst,
                       const af_json *node) {
    if (!is_obj(inst) || !is_arr(vval)) return;
    for (size_t i = 0; i < vval->n; i++) {
        const af_json *k = vval->items[i];
        if (is_str(k) && !obj_has(inst, k->s))
            verr(v, "required", vval, inst, node);
    }
}

/* type: ensure_list(str) semantics; the draft6+ checker lets an integral
 * float pass "integer". */
static void k_type(vctx *v, const af_json *vval, const af_json *inst,
                   const af_json *node) {
    int ok = 0;
    if (is_str(vval)) {
        ok = af_is_type(inst, vval->s);
    } else if (is_arr(vval)) {
        for (size_t i = 0; i < vval->n && !ok; i++)
            if (is_str(vval->items[i]) && af_is_type(inst, vval->items[i]->s))
                ok = 1;
    } else {
        return; /* invalid schema; Python would raise -- unreachable */
    }
    if (!ok) verr(v, "type", vval, inst, node);
}

/* enum: instance == 0 or == 1 takes the unbool() branch (True/False are
 * NOT in [1]/[0]); every other instance uses plain Python ==. */
static int py_is_01(const af_json *inst) {
    if (is_booln(inst)) return 1; /* False == 0, True == 1 */
    if (is_int(inst)) return inst->i == 0 || inst->i == 1;
    if (is_float(inst)) return inst->d == 0.0 || inst->d == 1.0;
    return 0;
}

static int enum_unbool_match(const af_json *inst, const af_json *each) {
    if (is_booln(inst)) return is_booln(each) && each->b == inst->b;
    if (is_booln(each)) return 0; /* unbool sentinels never equal 0/1 */
    if (is_number(inst) && is_number(each)) return py_eq(inst, each);
    return 0;
}

static void k_enum(vctx *v, const af_json *vval, const af_json *inst,
                   const af_json *node) {
    if (!is_arr(vval)) return;
    int match = 0;
    if (py_is_01(inst)) {
        for (size_t i = 0; i < vval->n && !match; i++)
            if (enum_unbool_match(inst, vval->items[i])) match = 1;
    } else {
        for (size_t i = 0; i < vval->n && !match; i++)
            if (py_eq(vval->items[i], inst)) match = 1;
    }
    if (!match) verr(v, "enum", vval, inst, node);
}

/* const: jsonschema uses equal(), so a bool never stands in for 0/1. */
static void k_const(vctx *v, const af_json *vval, const af_json *inst,
                    const af_json *node) {
    if (!js_eq(inst, vval)) verr(v, "const", vval, inst, node);
}

/* minimum / maximum: numbers only, compared exactly. */
static void k_minimum(vctx *v, const af_json *vval, const af_json *inst,
                      const af_json *node) {
    if (is_number(inst) && is_number(vval) && num_lt(inst, vval))
        verr(v, "minimum", vval, inst, node);
}

static void k_maximum(vctx *v, const af_json *vval, const af_json *inst,
                      const af_json *node) {
    if (is_number(inst) && is_number(vval) && num_lt(vval, inst))
        verr(v, "maximum", vval, inst, node);
}

/* len() against an integer bound (float bounds would be a schema bug). */
static int len_lt(size_t n, const af_json *bound) {
    if (is_int(bound)) return (int64_t)n < bound->i;
    if (is_float(bound)) return (double)n < bound->d;
    return 0;
}

static int len_gt(size_t n, const af_json *bound) {
    if (is_int(bound)) return (int64_t)n > bound->i;
    if (is_float(bound)) return (double)n > bound->d;
    return 0;
}

static void k_minItems(vctx *v, const af_json *vval, const af_json *inst,
                       const af_json *node) {
    if (is_arr(inst) && len_lt(inst->n, vval))
        verr(v, "minItems", vval, inst, node);
}

static void k_maxItems(vctx *v, const af_json *vval, const af_json *inst,
                       const af_json *node) {
    if (is_arr(inst) && len_gt(inst->n, vval))
        verr(v, "maxItems", vval, inst, node);
}

static void k_minProperties(vctx *v, const af_json *vval, const af_json *inst,
                            const af_json *node) {
    if (is_obj(inst) && len_lt(obj_count(inst), vval))
        verr(v, "minProperties", vval, inst, node);
}

static void k_maxProperties(vctx *v, const af_json *vval, const af_json *inst,
                            const af_json *node) {
    if (is_obj(inst) && len_gt(obj_count(inst), vval))
        verr(v, "maxProperties", vval, inst, node);
}

/* uniqueItems: the trigger is uniq() (equal()-based), while the message's
 * duplicate scan below uses Python == -- both as the oracle does. */
static void k_uniqueItems(vctx *v, const af_json *vval, const af_json *inst,
                          const af_json *node) {
    if (truthy(vval) && is_arr(inst) && !af_uniq(inst))
        verr(v, "uniqueItems", vval, inst, node);
}

/* items: `false` behind prefixItems is the "array may not grow" form; any
 * other value validates the tail. prefixItems is validated by its own
 * keyword (document order in the schema decides which runs first). */
static void k_items(vctx *v, const af_json *vval, const af_json *inst,
                    const af_json *node) {
    if (!is_arr(inst)) return;
    const af_json *pfx = obj_get(node, "prefixItems");
    size_t prefix = is_arr(pfx) ? pfx->n : 0;
    if (is_booln(vval) && !vval->b) {
        if (inst->n > prefix) verr(v, "items", vval, inst, node);
        return;
    }
    for (size_t i = prefix; i < inst->n; i++) {
        descend_idx(v, inst->items[i], vval, (int64_t)i);
        if (v->fatal) return;
    }
}

/* prefixItems: zip(instance, schema). */
static void k_prefixItems(vctx *v, const af_json *vval, const af_json *inst,
                          const af_json *node) {
    (void)node;
    if (!is_arr(inst) || !is_arr(vval)) return;
    size_t n = inst->n < vval->n ? inst->n : vval->n;
    for (size_t i = 0; i < n; i++) {
        descend_idx(v, inst->items[i], vval->items[i], (int64_t)i);
        if (v->fatal) return;
    }
}

/* additionalProperties: a dict-valued keyword descends into each extra;
 * `false` yields ONE error over the extras set (the collector fans it out
 * in sorted order -- jsonschema hands us a set, whose order is hash-random
 * and therefore deliberately not used here). */
static void k_additionalProperties(vctx *v, const af_json *vval,
                                   const af_json *inst, const af_json *node) {
    if (!is_obj(inst)) return;
    const af_json *props = obj_get(node, "properties");
    if (is_obj(vval)) {
        for (size_t i = 0; i < inst->n; i += 2) {
            const char *key = inst->items[i]->s;
            if (!props || !obj_has(props, key))
                descend(v, inst->items[i + 1], vval, key);
            if (v->fatal) return;
        }
    } else if (!truthy(vval)) {
        for (size_t i = 0; i < inst->n; i += 2) {
            const char *key = inst->items[i]->s;
            if (!props || !obj_has(props, key)) {
                verr(v, "additionalProperties", vval, inst, node);
                return;
            }
        }
    }
}

/* propertyNames: each KEY is the instance of the subschema, and descends
 * without a path part -- so failures land on the object, not the key. */
static void k_propertyNames(vctx *v, const af_json *vval, const af_json *inst,
                            const af_json *node) {
    (void)node;
    if (!is_obj(inst)) return;
    for (size_t i = 0; i < inst->n; i += 2) {
        descend(v, inst->items[i], vval, NULL);
        if (v->fatal) return;
    }
}

/* allOf: descend into each branch, no path part (schema_path only). */
static void k_allOf(vctx *v, const af_json *vval, const af_json *inst,
                    const af_json *node) {
    (void)node;
    if (!is_arr(vval)) return;
    for (size_t i = 0; i < vval->n; i++) {
        descend(v, inst, vval->items[i], NULL);
        if (v->fatal) return;
    }
}

/* oneOf: the FIRST valid branch breaks the scan; zero valid yields the
 * "not valid under any" error, two-or-more valid yields the "valid under
 * each" one (later branches only -- jsonschema's generator resumes after
 * the break). Exactly one valid yields nothing. Both errors carry kind
 * "oneOf"; the collector re-checks the branches itself. */
static int branch_valid(const vctx *v, const af_json *inst,
                        const af_json *sch, const char **why) {
    af_err *sub;
    size_t nsub;
    if (v_run(v->root, inst, sch, &sub, &nsub, why)) return -1;
    int ok = nsub == 0;
    errs_free(sub, nsub);
    return ok;
}

static void k_oneOf(vctx *v, const af_json *vval, const af_json *inst,
                    const af_json *node) {
    if (!is_arr(vval)) return;
    int first_valid = -1;
    for (size_t i = 0; i < vval->n; i++) {
        int ok = branch_valid(v, inst, vval->items[i], &v->why);
        if (ok < 0) { v_fail(v, v->why); return; }
        if (ok) { first_valid = (int)i; break; }
    }
    if (first_valid < 0) {
        verr(v, "oneOf", vval, inst, node);
        return;
    }
    for (size_t i = (size_t)first_valid + 1; i < vval->n; i++) {
        int ok = branch_valid(v, inst, vval->items[i], &v->why);
        if (ok < 0) { v_fail(v, v->why); return; }
        if (ok) { verr(v, "oneOf", vval, inst, node); return; }
    }
}

/* if/then/else: the condition is checked as its own schema (the oracle
 * does the same for the format walk; schema/ conditions are flat, no $ref,
 * so rooting at the condition matches jsonschema's resolver). */
static void k_if(vctx *v, const af_json *vval, const af_json *inst,
                 const af_json *node) {
    const char *why = NULL;
    int ok = branch_valid(v, inst, vval, &why);
    if (ok < 0) { v_fail(v, why); return; }
    const af_json *branch = obj_get(node, ok ? "then" : "else");
    if (branch) descend(v, inst, branch, NULL);
}

/* ---------------------------------------------------------------- */
/* dispatch: document order of the node's keys, like jsonschema's       */
/* applicable_validators(_schema) = _schema.items()                     */
/* ---------------------------------------------------------------- */
typedef void (*kwfn)(vctx *, const af_json *, const af_json *, const af_json *);

static kwfn table_find(const char *key) {
    static const struct { const char *k; kwfn fn; } tab[] = {
        {"$ref", k_ref},
        {"additionalProperties", k_additionalProperties},
        {"allOf", k_allOf},
        {"const", k_const},
        {"enum", k_enum},
        {"if", k_if},
        {"items", k_items},
        {"maxItems", k_maxItems},
        {"maxProperties", k_maxProperties},
        {"maximum", k_maximum},
        {"minItems", k_minItems},
        {"minProperties", k_minProperties},
        {"minimum", k_minimum},
        {"oneOf", k_oneOf},
        {"prefixItems", k_prefixItems},
        {"properties", k_properties},
        {"propertyNames", k_propertyNames},
        {"required", k_required},
        {"type", k_type},
        {"uniqueItems", k_uniqueItems},
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(tab[i].k, key) == 0) return tab[i].fn;
    return NULL; /* title/description/$schema/$defs/format/...: no errors */
}

/* _schema is True → nothing to check; False and non-object schemas never
 * occur as subschemas in schema/ (scanned), where jsonschema would yield
 * a validator=None error. Documented at the top of this file. */
static void validate(vctx *v, const af_json *inst, const af_json *sch) {
    if (!sch || sch->type != AF_JSON_OBJECT) return;
    for (size_t i = 0; i + 1 < sch->n; i += 2) {
        const af_json *key = sch->items[i];
        kwfn fn = key->type == AF_JSON_STRING ? table_find(key->s) : NULL;
        if (fn) fn(v, sch->items[i + 1], inst, sch);
        if (v->fatal) return;
    }
}

/* ---------------------------------------------------------------- */
/* diagnostics: D(), _emit and the (path, rank, kind, message) order   */
/* ---------------------------------------------------------------- */
typedef struct {
    char *path;
    int rank;
    const char *kind;
    char *message;
    const char *code;
    af_json *d;   /* the D() object */
    size_t seq;   /* emission index: Python's sort is stable, this is it */
} diag;

typedef struct {
    diag *v;
    size_t n, cap;
    int fatal;
    char why[256];
} out_t;

static void out_fail(out_t *o, const char *msg) {
    if (!o->fatal) {
        o->fatal = 1;
        snprintf(o->why, sizeof o->why, "%s", msg && *msg ? msg : "error");
    }
}

/* D()'s optional tail: NULL means the key is absent (Python's
 * `is not None` -- a JSON null never reaches observed/expected). The
 * nodes are consumed by emit_diag. */
typedef struct {
    af_json *observed;
    af_json *expected;
    af_json *suggestions;
} extra_t;

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) { s_oom = 1; return NULL; }
    memcpy(p, s, n + 1);
    return p;
}

/* _scalar: only scalars travel in observed (null/array/object → absent). */
static af_json *scalar_clone(const af_json *v) {
    switch (v->type) {
    case AF_JSON_STRING: return af_json_strn(v->s, v->slen);
    case AF_JSON_INT: return af_json_int(v->i);
    case AF_JSON_FLOAT: return af_json_float(v->d);
    case AF_JSON_BOOL: return af_json_bool(v->b);
    default: return NULL;
    }
}

static af_json *json_clone(const af_json *v) {
    if (!v) return NULL;
    switch (v->type) {
    case AF_JSON_NULL: return af_json_null();
    case AF_JSON_BOOL: return af_json_bool(v->b);
    case AF_JSON_INT: return af_json_int(v->i);
    case AF_JSON_FLOAT: return af_json_float(v->d);
    case AF_JSON_STRING: return af_json_strn(v->s, v->slen);
    case AF_JSON_ARRAY: {
        af_json *a = af_json_arr();
        if (!a) return NULL;
        for (size_t i = 0; i < v->n; i++) {
            af_json *c = json_clone(v->items[i]);
            if (!c || af_json_arr_push(a, c)) { af_json_free(c); af_json_free(a); return NULL; }
        }
        return a;
    }
    case AF_JSON_OBJECT: {
        af_json *o = af_json_obj();
        if (!o) return NULL;
        for (size_t i = 0; i + 1 < v->n; i += 2) {
            af_json *c = json_clone(v->items[i + 1]);
            if (!c || af_json_obj_put(o, v->items[i]->s, c)) {
                af_json_free(c); af_json_free(o); return NULL;
            }
        }
        return o;
    }
    }
    return NULL;
}

/* _lit: the compact writer (byte-identical to dump_json(indent=None)). */
static char *lit(const af_json *v) {
    return af_json_write(v, -1);
}

static char *path_join(const char *prefix, const char *ppath) {
    sb b = {0};
    if (sb_puts(&b, prefix) || sb_puts(&b, ppath)) {
        sb_free(&b);
        return NULL;
    }
    return sb_take(&b);
}

/* the constant ["features", "custom", "metadata"] tail of E_UNKNOWN_FIELD */
static af_json *af_json_obj_arr_suggestions(void);

static void emit_diag(out_t *o, const char *code, const char *kind,
                      const char *path, const char *message, const extra_t *x) {
    if (o->fatal || s_oom) return;
    if (o->n == o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 64;
        diag *p = (diag *)realloc(o->v, cap * sizeof *p);
        if (!p) { out_fail(o, "out of memory"); return; }
        o->v = p;
        o->cap = cap;
    }
    diag *g = &o->v[o->n];
    memset(g, 0, sizeof *g);
    g->path = xstrdup(path);
    g->message = xstrdup(message);
    if (!g->path || !g->message) { out_fail(o, "out of memory"); return; }
    g->rank = kind_rank(kind);
    g->kind = kind;
    g->code = code;
    g->seq = o->n;
    af_json *d = af_json_obj();
    int bad = !d;
    if (!bad) bad = af_json_obj_put(d, "code", af_json_str(code)) ||
                    af_json_obj_put(d, "severity", af_json_str("error")) ||
                    af_json_obj_put(d, "path", af_json_str(path)) ||
                    af_json_obj_put(d, "message", af_json_str(message));
    if (!bad && x && x->observed)
        bad = af_json_obj_put(d, "observed", x->observed);
    if (!bad && x && x->expected)
        bad = af_json_obj_put(d, "expected", x->expected);
    if (!bad && x && x->suggestions)
        bad = af_json_obj_put(d, "suggestions", x->suggestions);
    if (bad) { af_json_free(d); out_fail(o, "out of memory"); return; }
    g->d = d;
    o->n++;
}

static const char *typename_of(const af_json *v) {
    switch (v->type) {
    case AF_JSON_NULL: return "null";
    case AF_JSON_BOOL: return "boolean";
    case AF_JSON_INT: return "integer";
    case AF_JSON_FLOAT: return "number";
    case AF_JSON_STRING: return "string";
    case AF_JSON_ARRAY: return "array";
    default: return "object";
    }
}

static const char *INT_RANGE_STR = "-2147483648..2147483647";

/* ---------------------------------------------------------------- */
/* _collect_oneof: explain a oneOf failure with the form it reached for */
/* ---------------------------------------------------------------- */
static void collect_err(out_t *o, const af_err *e, const char *prefix,
                        const af_json *root);

typedef struct {
    const char *p;       /* branch-relative ptr of the member */
    const af_err *e;
} located_t;

typedef struct {
    const char *p;
    const char *kind;
    const af_err **m;
    size_t n;
} group_t;

static int group_cmp(const void *pa, const void *pb) {
    const group_t *a = (const group_t *)pa, *b = (const group_t *)pb;
    int c = strcmp(a->p, b->p);
    if (c) return c;
    int ra = kind_rank(a->kind), rb = kind_rank(b->kind);
    if (ra != rb) return ra < rb ? -1 : 1;
    return strcmp(a->kind, b->kind);
}

/* "x or y" over a type value: a string stands alone, a list joins its
 * members with " or " first (jsonschema's reprs, recomposed by us). */
static int join_type_value(sb *b, const af_json *v, int *first) {
    if (is_str(v)) {
        if (!*first && sb_puts(b, " or ")) return -1;
        *first = 0;
        return sb_puts(b, v->s);
    }
    if (is_arr(v)) {
        for (size_t i = 0; i < v->n; i++) {
            if (!is_str(v->items[i])) return -1; /* schema bug: unreachable */
            if (!*first && sb_puts(b, " or ")) return -1;
            *first = 0;
            if (sb_puts(b, v->items[i]->s)) return -1;
        }
        return 0;
    }
    return -1;
}

static void collect_oneof(out_t *o, const af_err *e, const char *prefix,
                          const af_json *root) {
    char *path = path_join(prefix, e->ppath);
    if (!path) { out_fail(o, "out of memory"); return; }
    const af_json *branches = is_obj(e->node) ? obj_get(e->node, "oneOf") : NULL;
    if (!is_arr(branches) || branches->n == 0) {
        extra_t x = {0};
        x.observed = scalar_clone(e->inst);
        emit_diag(o, "E_SCHEMA", "oneOf", path,
                  "value does not satisfy one of the allowed forms", &x);
        free(path);
        return;
    }

    /* Branches are re-checked structurally: jsonschema only says that
     * none fits, and the wrapper carrying $defs is what lets a branch
     * $ref them (equivalent to checking the branch with the same root). */
    size_t nb = branches->n;
    af_err **checked = (af_err **)calloc(nb, sizeof *checked);
    size_t *nchecked = (size_t *)calloc(nb, sizeof *nchecked);
    if (!checked || !nchecked) {
        free(checked);
        free(nchecked);
        out_fail(o, "out of memory");
        free(path);
        return;
    }
    size_t nvalid = 0;
    for (size_t i = 0; i < nb; i++) {
        const char *why = NULL;
        if (v_run(root, e->inst, branches->items[i], &checked[i],
                  &nchecked[i], &why)) {
            out_fail(o, why);
            goto cleanup;
        }
        if (nchecked[i] == 0) nvalid++;
    }
    if (nvalid > 1) {
        extra_t x = {0};
        x.observed = scalar_clone(e->inst);
        emit_diag(o, "E_SCHEMA", "oneOf", path,
                  "value matches more than one allowed form", &x);
        goto cleanup;
    }

    /* 1. a branch failing solely because keys are missing at the value
     *    itself names the intended form (first such branch wins). */
    for (size_t i = 0; i < nb; i++) {
        if (nchecked[i] == 0) continue;
        int all_required = 1;
        for (size_t j = 0; j < nchecked[i] && all_required; j++)
            if (strcmp(checked[i][j].kind, "required") != 0 ||
                checked[i][j].ppath[0] != '\0')
                all_required = 0;
        if (all_required) {
            for (size_t j = 0; j < nchecked[i]; j++)
                collect_err(o, &checked[i][j], path, root);
            goto cleanup;
        }
    }

    /* 2. otherwise the deepest disagreement beats a bare "wrong shape",
     *    and branches that disagree at the same spot merge into one. */
    size_t total = 0;
    for (size_t i = 0; i < nb; i++) total += nchecked[i];
    located_t *located = total ? (located_t *)calloc(total, sizeof *located)
                               : NULL;
    group_t *groups = NULL;
    size_t ng = 0, capg = 0;
    if (total && !located) { out_fail(o, "out of memory"); goto cleanup; }
    size_t depth = 0;
    for (size_t i = 0, k = 0; i < nb; i++)
        for (size_t j = 0; j < nchecked[i]; j++, k++) {
            located[k].p = checked[i][j].ppath;
            located[k].e = &checked[i][j];
            size_t slashes = 0;
            for (const char *s = checked[i][j].ppath; *s; s++)
                if (*s == '/') slashes++;
            if (slashes > depth) depth = slashes;
        }
    size_t npool = 0;
    located_t *pool = total ? (located_t *)calloc(total, sizeof *pool) : NULL;
    if (total && !pool) { free(located); out_fail(o, "out of memory"); goto cleanup; }
    for (size_t k = 0; k < total; k++) {
        const located_t *L = &located[k];
        size_t slashes = 0;
        for (const char *s = L->p; *s; s++) if (*s == '/') slashes++;
        int keep;
        if (depth) keep = slashes == depth;
        else keep = strcmp(L->e->kind, "type") != 0;
        if (keep) pool[npool++] = *L;
    }
    if (!npool) for (size_t k = 0; k < total; k++) pool[npool++] = located[k];
    for (size_t k = 0; k < npool; k++) {
        group_t *g = NULL;
        for (size_t gi = 0; gi < ng && !g; gi++)
            if (strcmp(groups[gi].p, pool[k].p) == 0 &&
                strcmp(groups[gi].kind, pool[k].e->kind) == 0)
                g = &groups[gi];
        if (!g) {
            if (ng == capg) {
                size_t cap = capg ? capg * 2 : 8;
                group_t *p = (group_t *)realloc(groups, cap * sizeof *p);
                if (!p) { out_fail(o, "out of memory"); goto free_pool; }
                groups = p;
                capg = cap;
            }
            g = &groups[ng++];
            g->p = pool[k].p;
            g->kind = pool[k].e->kind;
            g->m = NULL;
            g->n = 0;
        }
        const af_err **m = (const af_err **)realloc(
            (void *)g->m, (g->n + 1) * sizeof *m);
        if (!m) { out_fail(o, "out of memory"); goto free_pool; }
        g->m = m;
        g->m[g->n++] = pool[k].e;
    }
    if (ng > 1) qsort(groups, ng, sizeof *groups, group_cmp);

    for (size_t gi = 0; gi < ng; gi++) {
        group_t *g = &groups[gi];
        int merged = g->n > 1 &&
                     (strcmp(g->kind, "const") == 0 ||
                      strcmp(g->kind, "type") == 0);
        if (merged) {
            /* exps: the distinct validator values, first-seen order */
            const af_json **exps =
                (const af_json **)calloc(g->n, sizeof *exps);
            size_t nexps = 0;
            if (!exps) { out_fail(o, "out of memory"); break; }
            for (size_t mi = 0; mi < g->n; mi++) {
                int seen = 0;
                for (size_t xi = 0; xi < nexps && !seen; xi++)
                    if (py_eq(exps[xi], g->m[mi]->vval)) seen = 1;
                if (!seen) exps[nexps++] = g->m[mi]->vval;
            }
            sb gp = {0};
            /* oracle: gp = path + key[0], where key[0] is already a
             * pointer string -- append raw, never re-escape */
            int bad = sb_puts(&gp, path) || sb_puts(&gp, g->p);
            char *gpath = bad ? NULL : sb_take(&gp);
            sb_free(&gp);
            if (!gpath) { free(exps); out_fail(o, "out of memory"); break; }
            const af_json *inst0 = g->m[0]->inst;
            if (strcmp(g->kind, "type") == 0) {
                sb sh = {0};
                int first = 1;
                for (size_t xi = 0; xi < nexps; xi++)
                    if (join_type_value(&sh, exps[xi], &first)) break;
                char *shape = sb_take(&sh);
                sb_free(&sh);
                if (!shape) { free(exps); free(gpath); out_fail(o, "out of memory"); break; }
                sb msg = {0};
                int mbad = sb_puts(&msg, "expected ") || sb_puts(&msg, shape) ||
                           sb_puts(&msg, ", got ") ||
                           sb_puts(&msg, typename_of(inst0));
                char *m = mbad ? NULL : sb_take(&msg);
                sb_free(&msg);
                extra_t x = {0};
                x.observed = scalar_clone(inst0);
                x.expected = af_json_str(shape);
                emit_diag(o, "E_SCHEMA", "type", gpath,
                          m ? m : "expected type", &x);
                free(m);
                free(shape);
            } else if (nexps == 1) {
                char *litv = lit(exps[0]);
                char *liti = lit(inst0);
                sb msg = {0};
                int mbad = !litv || !liti || sb_puts(&msg, "expected ") ||
                           sb_puts(&msg, litv) || sb_puts(&msg, ", got ") ||
                           sb_puts(&msg, liti);
                char *m = mbad ? NULL : sb_take(&msg);
                sb_free(&msg);
                extra_t x = {0};
                x.observed = scalar_clone(inst0);
                x.expected = json_clone(exps[0]);
                emit_diag(o, "E_SCHEMA", "const", gpath,
                          m ? m : "expected const", &x);
                free(m);
                free(litv);
                free(liti);
            } else {
                af_json *arr = af_json_arr();
                int obad = !arr;
                for (size_t xi = 0; xi < nexps && !obad; xi++) {
                    af_json *c = json_clone(exps[xi]);
                    if (!c || af_json_arr_push(arr, c)) {
                        af_json_free(c);
                        obad = 1;
                    }
                }
                char *litv = obad ? NULL : lit(arr);
                char *liti = lit(inst0);
                sb msg = {0};
                int mbad = !litv || !liti || sb_puts(&msg, "expected one of ") ||
                           sb_puts(&msg, litv) || sb_puts(&msg, ", got ") ||
                           sb_puts(&msg, liti);
                char *m = mbad ? NULL : sb_take(&msg);
                sb_free(&msg);
                extra_t x = {0};
                x.observed = scalar_clone(inst0);
                x.expected = arr;
                emit_diag(o, "E_SCHEMA", "const", gpath,
                          m ? m : "expected one of", &x);
                if (obad) af_json_free(arr);
                free(m);
                free(litv);
                free(liti);
            }
            free(gpath);
            free(exps);
        } else {
            for (size_t mi = 0; mi < g->n; mi++)
                collect_err(o, g->m[mi], path, root);
        }
        if (o->fatal || s_oom) break;
    }

free_pool:
    for (size_t gi = 0; gi < ng; gi++) free((void *)groups[gi].m);
    free(groups);
    free(pool);
    free(located);
cleanup:
    for (size_t i = 0; i < nb; i++) errs_free(checked[i], nchecked[i]);
    free(checked);
    free(nchecked);
    free(path);
}

/* ---------------------------------------------------------------- */
/* _collect_schema_error: never jsonschema's prose, always our tables   */
/* ---------------------------------------------------------------- */
static void collect_err(out_t *o, const af_err *e, const char *prefix,
                        const af_json *root) {
    const char *kind = e->kind;
    char *path = path_join(prefix, e->ppath);
    if (!path) { out_fail(o, "out of memory"); return; }

    if (strcmp(kind, "additionalProperties") == 0) {
        /* structural: the instance's keys the object schema does not
         * declare -- jsonschema would only tell us this in prose. */
        const af_json *props = is_obj(e->node) ? obj_get(e->node, "properties") : NULL;
        size_t nkeys = 0;
        const char **keys = NULL;
        if (is_obj(e->inst)) {
            keys = (const char **)calloc(obj_count(e->inst), sizeof *keys);
            if (!keys && obj_count(e->inst)) { free(path); out_fail(o, "out of memory"); return; }
            for (size_t i = 0; i < e->inst->n; i += 2) {
                const char *k = e->inst->items[i]->s;
                if (!props || !obj_has(props, k)) keys[nkeys++] = k;
            }
            /* sorted(): UTF-8 byte order == Python's code-point order */
            for (size_t i = 1; i < nkeys; i++) {
                const char *k = keys[i];
                size_t j = i;
                while (j && strcmp(keys[j - 1], k) > 0) {
                    keys[j] = keys[j - 1];
                    j--;
                }
                keys[j] = k;
            }
        }
        for (size_t i = 0; i < nkeys; i++) {
            const char *k = keys[i];
            sb msg = {0};
            int bad = sb_puts(&msg, "unknown field '") || sb_puts(&msg, k) ||
                      sb_puts(&msg,
                              "'. Free text is metadata, never executable "
                              "visual intent; use archetype/features/params "
                              "or a custom px_fragment");
            char *m = bad ? NULL : sb_take(&msg);
            sb_free(&msg);
            sb kp = {0};
            int kbad = sb_puts(&kp, path) || ptr_part(&kp, k);
            char *kpath = kbad ? NULL : sb_take(&kp);
            sb_free(&kp);
            if (!m || !kpath) {
                free(m);
                free(kpath);
                out_fail(o, "out of memory");
                break;
            }
            extra_t x = {0};
            af_json *sug = af_json_obj_arr_suggestions();
            x.suggestions = sug;
            if (!sug) { free(m); free(kpath); out_fail(o, "out of memory"); break; }
            emit_diag(o, "E_UNKNOWN_FIELD", kind, kpath, m, &x);
            free(m);
            free(kpath);
        }
        free(keys);
        free(path);
        return;
    }

    if (strcmp(kind, "oneOf") == 0) {
        free(path);
        collect_oneof(o, e, prefix, root);
        return;
    }

    if (strcmp(kind, "required") == 0 && is_obj(e->inst)) {
        /* one diagnostic per missing key; the set is recomputed from the
         * required list and the instance instead of parsed out of prose. */
        if (is_arr(e->vval))
            for (size_t i = 0; i < e->vval->n; i++) {
                const af_json *k = e->vval->items[i];
                if (!is_str(k) || obj_has(e->inst, k->s)) continue;
                sb msg = {0};
                int bad = sb_puts(&msg, "'") || sb_puts(&msg, k->s) ||
                          sb_puts(&msg, "' is a required property");
                char *m = bad ? NULL : sb_take(&msg);
                sb_free(&msg);
                extra_t x = {0};
                x.expected = af_json_str(k->s);
                emit_diag(o, "E_SCHEMA", kind, path,
                          m ? m : "required property", &x);
                free(m);
            }
        free(path);
        return;
    }

    if (strcmp(kind, "type") == 0) {
        sb sh = {0};
        int first = 1;
        int bad = 0;
        if (is_str(e->vval)) {
            bad = sb_puts(&sh, e->vval->s);
            first = 0;
        } else if (is_arr(e->vval)) {
            bad = join_type_value(&sh, e->vval, &first);
        } else {
            bad = 1; /* invalid schema: Python would raise */
        }
        char *shape = bad ? NULL : sb_take(&sh);
        sb_free(&sh);
        if (!shape) { free(path); out_fail(o, "out of memory"); return; }
        sb msg = {0};
        int mbad = sb_puts(&msg, "expected ") || sb_puts(&msg, shape) ||
                   sb_puts(&msg, ", got ") ||
                   sb_puts(&msg, typename_of(e->inst));
        char *m = mbad ? NULL : sb_take(&msg);
        sb_free(&msg);
        extra_t x = {0};
        x.observed = scalar_clone(e->inst);
        x.expected = af_json_str(shape);
        emit_diag(o, "E_SCHEMA", kind, path, m ? m : "expected type", &x);
        free(m);
        free(shape);
        free(path);
        return;
    }

    if (strcmp(kind, "enum") == 0) {
        char *litv = is_arr(e->vval) ? lit(e->vval) : NULL;
        char *liti = lit(e->inst);
        sb msg = {0};
        int bad = !litv || !liti || sb_puts(&msg, "expected one of ") ||
                  sb_puts(&msg, litv) || sb_puts(&msg, ", got ") ||
                  sb_puts(&msg, liti);
        char *m = bad ? NULL : sb_take(&msg);
        sb_free(&msg);
        extra_t x = {0};
        x.observed = scalar_clone(e->inst);
        x.expected = litv ? json_clone(e->vval) : NULL;
        emit_diag(o, "E_SCHEMA", kind, path, m ? m : "expected enum", &x);
        free(m);
        free(litv);
        free(liti);
        free(path);
        return;
    }

    if (strcmp(kind, "const") == 0) {
        char *litv = lit(e->vval);
        char *liti = lit(e->inst);
        sb msg = {0};
        int bad = !litv || !liti || sb_puts(&msg, "expected ") ||
                  sb_puts(&msg, litv) || sb_puts(&msg, ", got ") ||
                  sb_puts(&msg, liti);
        char *m = bad ? NULL : sb_take(&msg);
        sb_free(&msg);
        extra_t x = {0};
        x.observed = scalar_clone(e->inst);
        x.expected = json_clone(e->vval);
        emit_diag(o, "E_SCHEMA", kind, path, m ? m : "expected const", &x);
        free(m);
        free(litv);
        free(liti);
        free(path);
        return;
    }

    if (strcmp(kind, "minimum") == 0 || strcmp(kind, "maximum") == 0) {
        const char *op = strcmp(kind, "minimum") == 0 ? ">=" : "<=";
        char *litv = lit(e->vval);
        char *liti = lit(e->inst);
        sb msg = {0};
        int bad = !litv || !liti || sb_puts(&msg, "expected ") ||
                  sb_puts(&msg, op) || sb_puts(&msg, " ") ||
                  sb_puts(&msg, litv) || sb_puts(&msg, ", got ") ||
                  sb_puts(&msg, liti);
        char *m = bad ? NULL : sb_take(&msg);
        sb_free(&msg);
        sb ex = {0};
        int ebad = !litv || sb_puts(&ex, op) || sb_puts(&ex, " ") ||
                   sb_puts(&ex, litv);
        char *expected = ebad ? NULL : sb_take(&ex);
        sb_free(&ex);
        extra_t x = {0};
        x.observed = scalar_clone(e->inst);
        x.expected = expected ? af_json_str(expected) : NULL;
        emit_diag(o, "E_SCHEMA", kind, path, m ? m : "expected bound", &x);
        free(expected);
        free(m);
        free(litv);
        free(liti);
        free(path);
        return;
    }

    if (strcmp(kind, "minItems") == 0 || strcmp(kind, "maxItems") == 0 ||
        strcmp(kind, "minProperties") == 0 ||
        strcmp(kind, "maxProperties") == 0) {
        int is_min = strncmp(kind, "min", 3) == 0; /* minItems/minProperties */
        int is_items = strstr(kind, "Items") != NULL;
        size_t n = is_items && is_arr(e->inst) ? e->inst->n
                   : is_obj(e->inst) ? obj_count(e->inst) : 0;
        const af_json *b = e->vval;
        int one = (is_int(b) && b->i == 1) || (is_float(b) && b->d == 1.0);
        const char *unit = is_items ? (one ? "entry" : "entries")
                                    : (one ? "key" : "keys");
        char *litb = lit(b);
        sb msg = {0};
        int bad = !litb || sb_puts(&msg, "expected ") ||
                  sb_puts(&msg, is_min ? "at least" : "at most") ||
                  sb_puts(&msg, " ") || sb_puts(&msg, litb) ||
                  sb_puts(&msg, " ") || sb_puts(&msg, unit) ||
                  sb_puts(&msg, ", got ");
        if (!bad) bad = sb_i64(&msg, (int64_t)n);
        char *m = bad ? NULL : sb_take(&msg);
        sb_free(&msg);
        sb ex = {0};
        int ebad = !litb || sb_puts(&ex, is_min ? "at least" : "at most") ||
                   sb_puts(&ex, " ") || sb_puts(&ex, litb);
        char *expected = ebad ? NULL : sb_take(&ex);
        sb_free(&ex);
        extra_t x = {0};
        x.observed = af_json_int((int64_t)n);
        x.expected = expected ? af_json_str(expected) : NULL;
        emit_diag(o, "E_SCHEMA", kind, path, m ? m : "expected count", &x);
        free(expected);
        free(m);
        free(litb);
        free(path);
        return;
    }

    if (strcmp(kind, "uniqueItems") == 0) {
        const af_json *dup = is_arr(e->inst) ? find_dup(e->inst) : NULL;
        extra_t x = {0};
        char *m = NULL;
        if (dup) {
            char *litd = lit(dup);
            sb msg = {0};
            int bad = !litd || sb_puts(&msg, "duplicate entry ") ||
                      sb_puts(&msg, litd);
            m = bad ? NULL : sb_take(&msg);
            sb_free(&msg);
            free(litd);
            x.observed = scalar_clone(dup);
        } else {
            m = xstrdup("array entries must be unique");
        }
        emit_diag(o, "E_SCHEMA", kind, path,
                  m ? m : "array entries must be unique", &x);
        free(m);
        free(path);
        return;
    }

    if (strcmp(kind, "items") == 0 && is_arr(e->inst) &&
        is_obj(e->node)) {
        const af_json *pfx = obj_get(e->node, "prefixItems");
        if (is_arr(pfx)) {
            /* `items: false` behind prefixItems: the array may not grow */
            sb msg = {0};
            int bad = sb_puts(&msg, "expected at most ");
            if (!bad) bad = sb_i64(&msg, (int64_t)pfx->n);
            if (!bad) bad = sb_puts(&msg, " entries, got ");
            if (!bad) bad = sb_i64(&msg, (int64_t)e->inst->n);
            char *m = bad ? NULL : sb_take(&msg);
            sb_free(&msg);
            sb ex = {0};
            int ebad = sb_puts(&ex, "at most ");
            if (!ebad) ebad = sb_i64(&ex, (int64_t)pfx->n);
            char *expected = ebad ? NULL : sb_take(&ex);
            sb_free(&ex);
            extra_t x = {0};
            x.observed = af_json_int((int64_t)e->inst->n);
            x.expected = expected ? af_json_str(expected) : NULL;
            emit_diag(o, "E_SCHEMA", kind, path,
                      m ? m : "expected at most entries", &x);
            free(expected);
            free(m);
            free(path);
            return;
        }
    }

    /* generic fallback (anyOf's context fan-out is the only other path
     * in Python; schema/ has no anyOf, so this is the end of the chain) */
    sb msg = {0};
    int bad = sb_puts(&msg, "value does not satisfy the '") ||
              sb_puts(&msg, kind) || sb_puts(&msg, "' constraint");
    char *m = bad ? NULL : sb_take(&msg);
    sb_free(&msg);
    extra_t x = {0};
    x.observed = scalar_clone(e->inst);
    emit_diag(o, "E_SCHEMA", kind, path,
              m ? m : "value does not satisfy constraint", &x);
    free(m);
    free(path);
}

/* ---------------------------------------------------------------- */
/* _collect_formats: the P0.2 vocabulary, run by hand                 */
/* ---------------------------------------------------------------- */
static af_json *af_json_obj_arr_suggestions(void) {
    af_json *a = af_json_arr();
    if (!a) return NULL;
    const char *names[3] = {"features", "custom", "metadata"};
    for (int i = 0; i < 3; i++) {
        af_json *s = af_json_str(names[i]);
        if (!s || af_json_arr_push(a, s)) { af_json_free(s); af_json_free(a); return NULL; }
    }
    return a;
}

static void collect_formats(out_t *o, const af_json *doc, const af_json *sch,
                            const af_json *root, const char *prefix,
                            const char *dpath) {
    if (!is_obj(sch) || o->fatal || s_oom) return;
    const af_json *ref = obj_get(sch, "$ref");
    if (ref) {
        /* refs in schema/ point at $defs; a broken one is a schema bug */
        if (is_str(ref)) {
            const af_json *target = resolve_ref(root, ref->s);
            if (target) collect_formats(o, doc, target, root, prefix, dpath);
        }
        return; /* siblings never run here (the oracle returns early) */
    }
    const af_json *fmt = obj_get(sch, "format");
    if (is_str(fmt) && is_str(doc) && !vocab_check(fmt->s, doc->s)) {
        char *litv = lit(doc);
        sb msg = {0};
        int bad = !litv || sb_puts(&msg, litv) ||
                  sb_puts(&msg, " is not a valid ");
        for (const char *p = fmt->s; !bad && *p; p++)
            bad = sb_putc(&msg, *p == '_' ? ' ' : *p);
        char *m = bad ? NULL : sb_take(&msg);
        sb_free(&msg);
        char *fpath = path_join(prefix, dpath);
        if (!m || !fpath) {
            free(m);
            free(fpath);
            free(litv);
            out_fail(o, "out of memory");
            return;
        }
        extra_t x = {0};
        x.observed = json_clone(doc);
        x.expected = af_json_str(fmt->s);
        emit_diag(o, "E_SCHEMA", "pattern", fpath, m, &x);
        free(m);
        free(fpath);
        free(litv);
    }
    if (is_obj(doc)) {
        const af_json *props = obj_get(sch, "properties");
        const af_json *extra = obj_get(sch, "additionalProperties");
        for (size_t i = 0; i < doc->n; i += 2) {
            const char *k = doc->items[i]->s;
            sb dp = {0};
            int bad = sb_puts(&dp, dpath) || ptr_part(&dp, k);
            char *kdpath = bad ? NULL : sb_take(&dp);
            sb_free(&dp);
            if (!kdpath) { out_fail(o, "out of memory"); return; }
            if (is_obj(props) && obj_has(props, k))
                collect_formats(o, doc->items[i + 1], obj_get(props, k),
                                root, prefix, kdpath);
            /* every key is walked through a dict-valued additionalProperties
             * too -- the oracle's double walk, kept deliberately */
            if (is_obj(extra))
                collect_formats(o, doc->items[i + 1], extra, root, prefix,
                                kdpath);
            free(kdpath);
            if (o->fatal || s_oom) return;
        }
        const af_json *names = obj_get(sch, "propertyNames");
        if (is_obj(names))
            for (size_t i = 0; i < doc->n; i += 2) {
                sb dp = {0};
                int bad = sb_puts(&dp, dpath) ||
                          ptr_part(&dp, doc->items[i]->s);
                char *kdpath = bad ? NULL : sb_take(&dp);
                sb_free(&dp);
                if (!kdpath) { out_fail(o, "out of memory"); return; }
                collect_formats(o, doc->items[i], names, root, prefix, kdpath);
                free(kdpath);
                if (o->fatal || s_oom) return;
            }
    } else if (is_arr(doc)) {
        size_t start = 0;
        const af_json *pfx = obj_get(sch, "prefixItems");
        if (is_arr(pfx)) {
            size_t n = doc->n < pfx->n ? doc->n : pfx->n;
            for (size_t i = 0; i < n; i++) {
                sb dp = {0};
                int bad = sb_puts(&dp, dpath);
                if (!bad) bad = ptr_i64(&dp, (int64_t)i);
                char *idpath = bad ? NULL : sb_take(&dp);
                sb_free(&dp);
                if (!idpath) { out_fail(o, "out of memory"); return; }
                collect_formats(o, doc->items[i], pfx->items[i], root, prefix,
                                idpath);
                free(idpath);
                if (o->fatal || s_oom) return;
            }
            start = pfx->n;
        }
        const af_json *items = obj_get(sch, "items");
        if (is_obj(items))
            for (size_t i = start; i < doc->n; i++) {
                sb dp = {0};
                int bad = sb_puts(&dp, dpath);
                if (!bad) bad = ptr_i64(&dp, (int64_t)i);
                char *idpath = bad ? NULL : sb_take(&dp);
                sb_free(&dp);
                if (!idpath) { out_fail(o, "out of memory"); return; }
                collect_formats(o, doc->items[i], items, root, prefix, idpath);
                free(idpath);
                if (o->fatal || s_oom) return;
            }
    }
    static const char *branches[3] = {"oneOf", "allOf", "anyOf"};
    for (int bi = 0; bi < 3; bi++) {
        const af_json *br = obj_get(sch, branches[bi]);
        if (!is_arr(br)) continue;
        for (size_t i = 0; i < br->n; i++) {
            collect_formats(o, doc, br->items[i], root, prefix, dpath);
            if (o->fatal || s_oom) return;
        }
    }
    const af_json *cond = obj_get(sch, "if");
    if (is_obj(cond)) {
        /* if/then/else: assert `then` only when the condition holds (and
         * `else` only when it does not). The oracle roots the check at the
         * condition itself; schema/'s conditions are flat, no $ref. */
        af_err *sub;
        size_t nsub;
        const char *why = NULL;
        if (v_run(cond, doc, cond, &sub, &nsub, &why)) {
            out_fail(o, why);
            return;
        }
        int matched = nsub == 0;
        errs_free(sub, nsub);
        const af_json *branch = obj_get(sch, matched ? "then" : "else");
        if (is_obj(branch))
            collect_formats(o, doc, branch, root, prefix, dpath);
    }
    /* `not` is deliberately not walked: a format inside it must NOT hold,
     * so asserting it there would invert the meaning (schema/ has no `not`). */
}

/* ---------------------------------------------------------------- */
/* _collect_int_range: P0.3 -- every integer in the document, free-form */
/* metadata included (jsonschema has no keyword for this).             */
/* ---------------------------------------------------------------- */
static void collect_int_range(out_t *o, const af_json *doc, const char *prefix,
                              const char *dpath) {
    if (o->fatal || s_oom) return;
    if (is_obj(doc)) {
        for (size_t i = 0; i < doc->n; i += 2) {
            sb dp = {0};
            int bad = sb_puts(&dp, dpath) || ptr_part(&dp, doc->items[i]->s);
            char *kdpath = bad ? NULL : sb_take(&dp);
            sb_free(&dp);
            if (!kdpath) { out_fail(o, "out of memory"); return; }
            collect_int_range(o, doc->items[i + 1], prefix, kdpath);
            free(kdpath);
        }
    } else if (is_arr(doc)) {
        for (size_t i = 0; i < doc->n; i++) {
            sb dp = {0};
            int bad = sb_puts(&dp, dpath);
            if (!bad) bad = ptr_i64(&dp, (int64_t)i);
            char *idpath = bad ? NULL : sb_take(&dp);
            sb_free(&dp);
            if (!idpath) { out_fail(o, "out of memory"); return; }
            collect_int_range(o, doc->items[i], prefix, idpath);
            free(idpath);
        }
    } else if (is_int(doc) && (doc->i < INT32_MIN || doc->i > INT32_MAX)) {
        /* int_range_message(): "integer N is outside the valid range .." */
        char *litv = lit(doc);
        sb msg = {0};
        int bad = !litv || sb_puts(&msg, "integer ") || sb_puts(&msg, litv) ||
                  sb_puts(&msg, " is outside the valid range ") ||
                  sb_puts(&msg, INT_RANGE_STR);
        char *m = bad ? NULL : sb_take(&msg);
        sb_free(&msg);
        char *fpath = path_join(prefix, dpath);
        if (!m || !fpath) {
            free(m);
            free(fpath);
            free(litv);
            out_fail(o, "out of memory");
            return;
        }
        extra_t x = {0};
        x.observed = af_json_int(doc->i);
        x.expected = af_json_str(INT_RANGE_STR);
        emit_diag(o, "E_INT_RANGE", "int_range", fpath, m, &x);
        free(m);
        free(fpath);
        free(litv);
    }
}

/* ---------------------------------------------------------------- */
/* order + dedup (schema_diags' entries.sort + seen-set)              */
/* ---------------------------------------------------------------- */
/* tuple(path.split("/")): segment-wise, and a prefix is less than the
 * longer path it starts -- exactly Python's tuple-of-strings compare. */
static int seg_cmp(const char *a, const char *b) {
    for (;;) {
        const char *ea = strchr(a, '/');
        const char *eb = strchr(b, '/');
        size_t la = ea ? (size_t)(ea - a) : strlen(a);
        size_t lb = eb ? (size_t)(eb - b) : strlen(b);
        size_t lm = la < lb ? la : lb;
        int c = memcmp(a, b, lm);
        if (c) return c < 0 ? -1 : 1;
        if (la != lb) return la < lb ? -1 : 1;
        if (!ea && !eb) return 0;
        if (!ea) return -1;
        if (!eb) return 1;
        a = ea + 1;
        b = eb + 1;
    }
}

static int diag_cmp(const void *pa, const void *pb) {
    const diag *a = (const diag *)pa, *b = (const diag *)pb;
    int c = seg_cmp(a->path, b->path);
    if (c) return c;
    if (a->rank != b->rank) return a->rank < b->rank ? -1 : 1;
    c = strcmp(a->kind, b->kind);
    if (c) return c;
    c = strcmp(a->message, b->message);
    if (c) return c;
    return a->seq < b->seq ? -1 : a->seq > b->seq; /* stable */
}

/* ---------------------------------------------------------------- */
/* entry points                                                        */
/* ---------------------------------------------------------------- */
af_json *af_schema_diags(const af_json *doc, const af_json *schema_root,
                         const char *prefix, char *err, size_t errsz) {
    out_t o;
    memset(&o, 0, sizeof o);
    s_oom = 0;

    /* 1. jsonschema's error set, in emission order (document order of
     *    each node's keywords, depth-first through descends). */
    vctx v;
    memset(&v, 0, sizeof v);
    v.root = schema_root;
    validate(&v, doc, schema_root);
    if (v.fatal) {
        snprintf(err, errsz, "%s", v.why ? v.why : "validation failed");
        errs_free(v.errs, v.ne);
        free(v.parts);
        return NULL;
    }
    for (size_t i = 0; i < v.ne; i++)
        collect_err(&o, &v.errs[i], prefix, schema_root);
    errs_free(v.errs, v.ne);
    free(v.parts);

    /* 2. the hand-run vocabulary walk, 3. the int32 walk. */
    if (!o.fatal) collect_formats(&o, doc, schema_root, schema_root, prefix, "");
    if (!o.fatal) collect_int_range(&o, doc, prefix, "");
    if (o.fatal || s_oom) {
        snprintf(err, errsz, "%s", o.fatal ? o.why : "out of memory");
        for (size_t i = 0; i < o.n; i++) {
            free(o.v[i].path);
            free(o.v[i].message);
            af_json_free(o.v[i].d);
        }
        free(o.v);
        return NULL;
    }

    /* 4. sort, then dedup by (code, path, message) keeping the first.
     * The walk-back only ever reads the COMPACTED region [0, w): slots
     * at >= w hold already-freed dup structs whose path/message bytes
     * the allocator may have reused (a tcache fd lands right there), so
     * touching them would break the same-path walk early. The sort keeps
     * equal paths adjacent, so [0, w) still contains the first kept
     * occurrence of every (path, code, message). */
    if (o.n > 1) qsort(o.v, o.n, sizeof *o.v, diag_cmp);
    size_t w = 0;
    for (size_t i = 0; i < o.n; i++) {
        int dup = 0;
        for (size_t j = w; j > 0 && !dup; j--) {
            const diag *p = &o.v[j - 1];
            if (strcmp(p->path, o.v[i].path) != 0) break; /* path block */
            if (strcmp(p->code, o.v[i].code) == 0 &&
                strcmp(p->message, o.v[i].message) == 0)
                dup = 1;
        }
        if (dup) {
            free(o.v[i].path);
            free(o.v[i].message);
            af_json_free(o.v[i].d);
        } else {
            o.v[w++] = o.v[i];
        }
    }
    o.n = w;

    af_json *out = af_json_arr();
    if (!out) {
        snprintf(err, errsz, "out of memory");
        for (size_t i = 0; i < o.n; i++) {
            free(o.v[i].path);
            free(o.v[i].message);
            af_json_free(o.v[i].d);
        }
        free(o.v);
        return NULL;
    }
    for (size_t i = 0; i < o.n; i++) {
        af_json_arr_push(out, o.v[i].d); /* transfers ownership */
        free(o.v[i].path);
        free(o.v[i].message);
    }
    free(o.v);
    return out;
}

/* ---------------------------------------------------------------- */
/* registry load (arcforge_ref.py::load_registry)                     */
/* ---------------------------------------------------------------- */
/* Sort order for registry file paths: Python's sorted(Path) compares
 * PARTS tuples, not the joined bytes ("arms" + "/x" sorts before
 * "arms-2" + "/x", while raw bytes put '-' before '/'), so compare
 * segment-wise: UTF-8 byte order within a segment equals Python's code
 * point order, and a path whose segments run out first is smaller. */
static int path_cmp(const void *pa, const void *pb) {
    return seg_cmp(*(const char *const *)pa, *(const char *const *)pb);
}

/* Read one glob level: files in dir `path` whose name ends with `suffix`
 * (one directory level, like "archetypes/ + *.archetype.json"). Returns the
 * sorted full paths (byte order == Python's sorted(Path)), or NULL with
 * *count == (size_t)-1 on allocation failure (NULL with *count == 0 is an
 * empty match, which is what a missing directory yields in Python too). */
static char **list_level(const char *path, const char *suffix, size_t *count) {
    *count = 0;
    DIR *d = opendir(path);
    if (!d) return NULL;
    char **out = NULL;
    size_t n = 0, cap = 0;
    struct dirent *ent;
    size_t slen = strlen(suffix);
    while ((ent = readdir(d))) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < slen || strcmp(name + len - slen, suffix) != 0) continue;
        if (n == cap) {
            size_t nc = cap ? cap * 2 : 8;
            char **p = (char **)realloc(out, nc * sizeof *p);
            if (!p) goto fail;
            out = p;
            cap = nc;
        }
        size_t full = strlen(path) + 1 + len;
        char *fp = (char *)malloc(full + 1);
        if (!fp) goto fail;
        sprintf(fp, "%s/%s", path, name);
        out[n++] = fp;
    }
    closedir(d);
    if (n > 1) qsort(out, n, sizeof *out, path_cmp);
    *count = n;
    return out;
fail:
    closedir(d);
    for (size_t i = 0; i < n; i++) free(out[i]);
    free(out);
    *count = (size_t)-1;
    return NULL;
}

/* The recipes glob: `**` means "zero or more directories", so a recipe
 * file may sit at any depth under recipes/ (pathlib semantics; the corpus
 * sits at depth 1). NULL with *count == (size_t)-1 means allocation failure. */
static char **list_recursive(const char *path, const char *suffix,
                             size_t *count) {
    *count = 0;
    char **out = NULL;
    size_t n = 0, cap = 0;
    DIR *d = opendir(path);
    if (!d) return NULL;
    struct dirent *ent;
    size_t slen = strlen(suffix);
    while ((ent = readdir(d))) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        size_t plen = strlen(path);
        size_t nlen = strlen(name);
        char *full = (char *)malloc(plen + 1 + nlen + 1);
        if (!full) goto fail;
        sprintf(full, "%s/%s", path, name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t sub = 0;
            char **more = list_recursive(full, suffix, &sub);
            free(full);
            if (!more && sub == (size_t)-1) goto fail; /* sub-allocation */
            for (size_t i = 0; i < sub; i++) {
                if (n == cap) {
                    size_t nc = cap ? cap * 2 : 8;
                    char **p = (char **)realloc(out, nc * sizeof *p);
                    if (!p) {
                        for (size_t k = i; k < sub; k++) free(more[k]);
                        free(more);
                        goto fail;
                    }
                    out = p;
                    cap = nc;
                }
                out[n++] = more[i];
            }
            free(more);
            continue;
        }
        free(full);
        if (nlen >= slen && strcmp(name + nlen - slen, suffix) == 0) {
            if (n == cap) {
                size_t nc = cap ? cap * 2 : 8;
                char **p = (char **)realloc(out, nc * sizeof *p);
                if (!p) goto fail;
                out = p;
                cap = nc;
            }
            char *fp = (char *)malloc(plen + 1 + nlen + 1);
            if (!fp) goto fail;
            sprintf(fp, "%s/%s", path, name);
            out[n++] = fp;
        }
    }
    closedir(d);
    if (n > 1) qsort(out, n, sizeof *out, path_cmp);
    *count = n;
    return out;
fail:
    closedir(d);
    for (size_t i = 0; i < n; i++) free(out[i]);
    free(out);
    *count = (size_t)-1;
    return NULL;
}

static int load_group(const char *reg_dir, const char *sub, const char *suffix,
                      int recursive, const char *name_field,
                      af_reg_entry **out, size_t *count, char *err,
                      size_t errsz) {
    *out = NULL;
    *count = 0;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/%s", reg_dir, sub);
    size_t n = 0;
    char **files = recursive ? list_recursive(dir, suffix, &n)
                             : list_level(dir, suffix, &n);
    if (!files) {
        if (n == (size_t)-1) { /* allocation failure inside the walker */
            snprintf(err, errsz, "out of memory");
            return -1;
        }
        /* an empty or missing directory: Python's glob yields nothing */
        DIR *probe = opendir(dir);
        if (!probe) { snprintf(err, errsz, "cannot read %s", dir); return -1; }
        closedir(probe);
        return 0;
    }
    af_reg_entry *ents = n ? (af_reg_entry *)calloc(n, sizeof *ents) : NULL;
    if (n && !ents) {
        for (size_t i = 0; i < n; i++) free(files[i]);
        free(files);
        snprintf(err, errsz, "out of memory");
        return -1;
    }
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        FILE *f = fopen(files[i], "r");
        if (!f) {
            snprintf(err, errsz, "cannot open %s", files[i]);
            goto fail;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *text = len >= 0 ? (char *)malloc((size_t)len + 1) : NULL;
        if (!text) { fclose(f); snprintf(err, errsz, "out of memory"); goto fail; }
        size_t got = fread(text, 1, (size_t)len, f);
        fclose(f);
        af_json *doc = NULL;
        char jerr[256];
        int rc = af_json_parse(text, got, &doc, jerr, sizeof jerr);
        free(text);
        if (rc) {
            snprintf(err, errsz, "%s: %s", files[i], jerr);
            goto fail;
        }
        const af_json *name = af_json_get(doc, name_field);
        if (!is_str(name)) {
            af_json_free(doc);
            snprintf(err, errsz, "%s: no %s field", files[i], name_field);
            goto fail;
        }
        ents[k].rel = files[i]; /* ownership moves to the entry */
        ents[k].doc = doc;
        files[i] = NULL;
        k++;
    }
    for (size_t i = 0; i < n; i++) free(files[i]);
    free(files);
    *out = ents;
    *count = k;
    return 0;
fail:
    for (size_t i = 0; i < n; i++) free(files[i]);
    free(files);
    for (size_t i = 0; i < k; i++) {
        free(ents[i].rel);
        af_json_free(ents[i].doc);
    }
    free(ents);
    return -1;
}

af_registry *af_registry_load(const char *reg_dir, char *err, size_t errsz) {
    af_registry *r = (af_registry *)calloc(1, sizeof *r);
    if (!r) { snprintf(err, errsz, "out of memory"); return NULL; }
    if (load_group(reg_dir, "archetypes", ".archetype.json", 0, "archetype",
                   &r->archetypes, &r->n_archetypes, err, errsz) ||
        load_group(reg_dir, "recipes", ".recipe.json", 1, "recipe",
                   &r->recipes, &r->n_recipes, err, errsz) ||
        load_group(reg_dir, "styles", ".style.json", 0, "style_pack",
                   &r->styles, &r->n_styles, err, errsz)) {
        af_registry_free(r);
        return NULL;
    }
    return r;
}

void af_registry_free(af_registry *r) {
    if (!r) return;
    af_reg_entry *groups[3] = {r->archetypes, r->recipes, r->styles};
    size_t counts[3] = {r->n_archetypes, r->n_recipes, r->n_styles};
    for (int g = 0; g < 3; g++)
        for (size_t i = 0; i < counts[g]; i++) {
            free(groups[g][i].rel);
            af_json_free(groups[g][i].doc);
        }
    free(r->archetypes);
    free(r->recipes);
    free(r->styles);
    free(r);
}

/* One group of load_registry(): name-keyed, files in path order, a
 * repeated name overwrites the value but keeps the key's position. */
static af_json *registry_group(const af_reg_entry *ents, size_t n,
                               const char *name_field, char *err,
                               size_t errsz) {
    af_json *o = af_json_obj();
    if (!o) { snprintf(err, errsz, "out of memory"); return NULL; }
    for (size_t i = 0; i < n; i++) {
        const af_json *name = af_json_get(ents[i].doc, name_field);
        if (!is_str(name)) { /* load_group already checked; defensive */
            af_json_free(o);
            snprintf(err, errsz, "%s: no %s field", ents[i].rel, name_field);
            return NULL;
        }
        af_json *doc = json_clone(ents[i].doc);
        if (!doc) { af_json_free(o); snprintf(err, errsz, "out of memory"); return NULL; }
        /* overwrite in place, like a Python dict assignment */
        int replaced = 0;
        for (size_t k = 0; k + 1 < o->n && !replaced; k += 2)
            if (strcmp(o->items[k]->s, name->s) == 0) {
                af_json_free(o->items[k + 1]);
                o->items[k + 1] = doc;
                replaced = 1;
            }
        if (!replaced && af_json_obj_put(o, name->s, doc)) {
            af_json_free(doc);
            af_json_free(o);
            snprintf(err, errsz, "out of memory");
            return NULL;
        }
    }
    return o;
}

af_json *af_registry_json(const af_registry *r, char *err, size_t errsz) {
    af_json *out = af_json_obj();
    if (!out) { snprintf(err, errsz, "out of memory"); return NULL; }
    const char *fields[3] = {"archetypes", "recipes", "styles"};
    const char *name_fields[3] = {"archetype", "recipe", "style_pack"};
    const af_reg_entry *ents[3] = {r->archetypes, r->recipes, r->styles};
    size_t counts[3] = {r->n_archetypes, r->n_recipes, r->n_styles};
    for (int g = 0; g < 3; g++) {
        af_json *grp = registry_group(ents[g], counts[g], name_fields[g],
                                      err, errsz);
        if (!grp) { af_json_free(out); return NULL; }
        if (af_json_obj_put(out, fields[g], grp)) {
            af_json_free(grp);
            af_json_free(out);
            snprintf(err, errsz, "out of memory");
            return NULL;
        }
    }
    return out;
}
