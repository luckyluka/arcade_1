/* px.c -- M4.1c: the `.px` fragment parser (spec-next 02 §G).
 *
 * A direct port of tools/arcforge_ref.py::parse_px (lines 264-388): the
 * same statement machine, the same guards, the same PxError texts and
 * line numbers. Rules pinned by the differential:
 *
 *   * lines come from str.splitlines() (ASCII boundaries + the Unicode
 *     trio); comments are `#` after lstrip, blank lines skipped;
 *   * an unindented line resets the block, and only " " and "\t" count
 *     as indentation (exactly Python's startswith test -- a line that
 *     starts with any other control byte is a STATEMENT);
 *   * statement guards are Python's (token count + str.isdigit over
 *     code points); a guard that passes while int() then rejects
 *     (`anchor a --5 3`) crashes the oracle -- mirrored as
 *     err->crash;
 *   * row/legend checks count CODE POINTS (Python str), so a multi-byte
 *     pixel is one column;
 *   * v2's frame protocol and the row checks run after the loop, in the
 *     oracle's order, with its exact messages.
 *
 * No external libraries (M4 contract).
 */
#include "px.h"
#include "expr.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Python whitespace ------------------------------------------------ */

/* str.splitlines() boundaries: \r\n, \r, \n, \v, \f, \x1c-\x1e, \x85,
 * \u2028, \u2029. Returns the byte length, or 0 when none starts here. */
static size_t line_bnd(const char *s, size_t n, size_t i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\r') return (i + 1 < n && s[i + 1] == '\n') ? 2 : 1;
    if (c == '\n' || c == '\v' || c == '\f') return 1;
    if (c == 0x1c || c == 0x1d || c == 0x1e) return 1;
    if (c == 0xc2 && i + 1 < n && (unsigned char)s[i + 1] == 0x85) return 2;
    if (c == 0xe2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 &&
        ((unsigned char)s[i + 2] == 0xa8 || (unsigned char)s[i + 2] == 0xa9))
        return 3;
    return 0;
}

/* str.isspace() sequences (strip / split): ASCII whitespace plus the
 * Unicode ones a .px file can carry. Exotic spaces (\x1680, \x2000-\x200a,
 * \x202f, \x205f, \x3000) are the documented non-ASCII divergence. */
static size_t ws_seq(const char *s, size_t n, size_t i) {
    unsigned char c = (unsigned char)s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
        c == '\f' || (c >= 0x1c && c <= 0x1f))
        return 1;
    if (c == 0xc2 && i + 1 < n) {
        unsigned char d = (unsigned char)s[i + 1];
        if (d == 0x85 || d == 0xa0) return 2;
    }
    if (c == 0xe2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 &&
        ((unsigned char)s[i + 2] == 0xa8 || (unsigned char)s[i + 2] == 0xa9))
        return 3;
    return 0;
}

/* one UTF-8 code point (input is validated UTF-8; a stray byte counts 1) */
static size_t cp_len(const char *s, size_t n, size_t i) {
    unsigned char c = (unsigned char)s[i];
    size_t want = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
                  (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (want == 1) return 1;
    if (i + want > n) return 1;
    for (size_t k = 1; k < want; k++)
        if (((unsigned char)s[i + k] & 0xc0) != 0x80) return 1;
    return want;
}

/* number of code points in s[0..n) */
static size_t cp_count(const char *s, size_t n) {
    size_t i = 0, k = 0;
    while (i < n) { i += cp_len(s, n, i); k++; }
    return k;
}

/* ---- containers ------------------------------------------------------- */

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

typedef struct { int ln; char *text; size_t len; } px_line;

typedef struct { px_line *v; size_t n, cap; } rowvec;

static int row_push(rowvec *b, int ln, const char *s, size_t n) {
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 8;
        px_line *p = (px_line *)realloc(b->v, nc * sizeof *p);
        if (!p) return -1;
        b->v = p;
        b->cap = nc;
    }
    char *c = (char *)malloc(n + 1);
    if (!c) return -1;
    memcpy(c, s, n);
    c[n] = 0;
    b->v[b->n].ln = ln;
    b->v[b->n].text = c;
    b->v[b->n].len = n;
    b->n++;
    return 0;
}

static void rowvec_free(rowvec *b) {
    for (size_t i = 0; i < b->n; i++) free(b->v[i].text);
    free(b->v);
    b->v = NULL;
    b->n = b->cap = 0;
}

/* str.splitlines() over the whole file: out gets one entry per line */
static int split_lines(const char *s, size_t n, rowvec *out) {
    size_t i = 0;
    int ln = 1;
    for (;;) {
        size_t j = i, adv = 0;
        while (j < n && !(adv = line_bnd(s, n, j))) j++;
        if (row_push(out, ln++, s + i, j - i)) return -1;
        if (j >= n) break;
        i = j + adv;
    }
    return 0;
}

/* str.strip() over s[0..n): the [a,z) span of non-whitespace */
static void py_strip(const char *s, size_t n, size_t *a, size_t *z) {
    size_t i = 0;
    while (i < n) {
        size_t w = ws_seq(s, n, i);
        if (!w) break;
        i += w;
    }
    size_t j = n;
    while (j > i) {
        size_t take = 0;
        for (size_t t = 1; t <= 3 && t <= j - i; t++)
            if (ws_seq(s, n, j - t) == t) { take = t; break; }
        if (!take) break;
        j -= take;
    }
    *a = i;
    *z = j;
}

/* str.split(): whitespace-separated tokens (owned strings) */
typedef struct { char **v; size_t n, cap; } strvec;

static int sv_push(strvec *b, const char *s, size_t n) {
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4;
        char **p = (char **)realloc(b->v, nc * sizeof *p);
        if (!p) return -1;
        b->v = p;
        b->cap = nc;
    }
    char *c = (char *)malloc(n + 1);
    if (!c) return -1;
    memcpy(c, s, n);
    c[n] = 0;
    b->v[b->n++] = c;
    return 0;
}

static void sv_free(strvec *b) {
    for (size_t i = 0; i < b->n; i++) free(b->v[i]);
    free(b->v);
    b->v = NULL;
    b->n = b->cap = 0;
}

static int split_tok(const char *s, size_t n, strvec *out) {
    size_t i = 0;
    while (i < n) {
        size_t w = ws_seq(s, n, i);
        if (w) { i += w; continue; }
        size_t j = i;
        while (j < n && !ws_seq(s, n, j)) j++;
        if (sv_push(out, s + i, j - i)) return -1;
        i = j;
    }
    return 0;
}

/* ---- Python int() ----------------------------------------------------- */

static int all_digits(const char *s) {
    if (!*s) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* guard form: `s.lstrip("-").isdigit()` over code points */
static int digits_lstrip_minus(const char *s) {
    size_t i = 0;
    while (s[i] == '-') i++;
    if (!s[i]) return 0;
    for (const char *p = s + i; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* Python int() on a guarded digit string: arbitrary precision upstream,
 * so anything past int64 saturates (documented divergence). */
static int64_t parse_uint(const char *s) {
    uint64_t acc = 0;
    for (const char *p = s; *p; p++) {
        uint64_t d = (uint64_t)(*p - '0');
        if (acc > (UINT64_MAX - d) / 10) return INT64_MAX;
        acc = acc * 10 + d;
    }
    return acc > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)acc;
}

/* Python int() with an optional single '-': `--5` / `-` raise ValueError
 * (the caller mirrors that as a crash, like the oracle's traceback). */
static int parse_signed(const char *s, int64_t *out) {
    size_t i = 0;
    int neg = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    if (!s[i]) return -1;
    uint64_t acc = 0;
    int ovf = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        uint64_t d = (uint64_t)(s[i] - '0');
        if (acc > (UINT64_MAX - d) / 10) ovf = 1;
        else acc = acc * 10 + d;
    }
    if (ovf) *out = neg ? INT64_MIN : INT64_MAX;
    else if (neg) *out = acc > (uint64_t)INT64_MAX + 1u ? INT64_MIN
                      : acc == (uint64_t)INT64_MAX + 1u ? INT64_MIN
                      : -(int64_t)acc;
    else *out = acc > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)acc;
    return 0;
}

/* ---- error helper ----------------------------------------------------- */

static int px_fail(af_px_error *err, int line, const char *code,
                   const char *fmt, ...) {
    err->crash = 0;
    err->line = line;
    snprintf(err->code, sizeof err->code, "%s", code);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->msg, sizeof err->msg, fmt, ap);
    va_end(ap);
    return -1;
}

static int px_oom(af_px_error *err) {
    err->crash = 1;
    err->line = 1;
    err->code[0] = 0;
    err->msg[0] = 0;
    return -1;
}

/* ---- the parser ------------------------------------------------------- */

typedef struct { int64_t idx; rowvec rows; } frame_blk;

int af_parse_px(const char *text, size_t len, af_px **out, af_px_error *err) {
    rowvec lines = {0};
    *out = NULL;
    err->crash = 0;
    err->code[0] = 0;
    err->msg[0] = 0;
    err->line = 1;
    if (split_lines(text, len, &lines)) return px_oom(err);

    int version = 1, seen_header = 0;
    int block = 0;                      /* 0 none, 1 legend, 2 rows, 3 frame */
    int64_t frame_idx = 0;
    int64_t declared = -1;              /* None */
    int64_t w = -1, h = -1;             /* size absent */
    char *name = NULL;
    int64_t fps = 0;
    int has_fps = 0;
    rowvec rows = {0};                  /* v1 rows */
    frame_blk *fblks = NULL;
    size_t nf = 0, fcap = 0;
    char **leg_ch = NULL, **leg_val = NULL;
    size_t nleg = 0, lcap = 0;
    char **anc_name = NULL;
    int64_t *anc_x = NULL, *anc_y = NULL;
    size_t nanc = 0, acap = 0;
    int rc = -1;
    strvec parts = {0};
    /* declared before the statement loop: `goto done` from inside it must
     * see initialized values (skipping an initializer leaves garbage) */
    size_t nsets = 0;
    rowvec *sets = NULL;

    for (size_t li = 0; li < lines.n; li++) {
        const char *raw = lines.v[li].text;
        size_t rn = lines.v[li].len;
        int ln = lines.v[li].ln;
        size_t b0, b1;
        py_strip(raw, rn, &b0, &b1);
        if (b0 == b1) continue;                          /* blank */
        if (raw[b0] == '#') continue;                    /* comment */
        if (!(raw[0] == ' ' || raw[0] == '\t')) {        /* statement */
            block = 0;
            sv_free(&parts);
            if (split_tok(raw + b0, b1 - b0, &parts)) { rc = px_oom(err); goto done; }
            const char *kw = parts.n ? parts.v[0] : "";
            if (strcmp(kw, "px") == 0) {
                int ok = parts.n == 2 &&
                    (strcmp(parts.v[1], "1") == 0 || strcmp(parts.v[1], "2") == 0);
                if (!ok) {
                    rc = px_fail(err, ln, "E_PX_PARSE",
                                 "unsupported px version (expected 'px 1' or 'px 2')");
                    goto done;
                }
                version = parts.v[1][0] - '0';
                seen_header = 1;
            } else if (!seen_header) {
                rc = px_fail(err, ln, "E_PX_PARSE",
                             "first statement must be 'px 1' or 'px 2'");
                goto done;
            } else if (strcmp(kw, "name") == 0 && parts.n == 2) {
                free(name);
                name = xstrdup(parts.v[1]);
                if (!name) { rc = px_oom(err); goto done; }
            } else if (strcmp(kw, "size") == 0 && parts.n == 3 &&
                       all_digits(parts.v[1]) && all_digits(parts.v[2])) {
                w = parse_uint(parts.v[1]);
                h = parse_uint(parts.v[2]);
            } else if (strcmp(kw, "anchor") == 0 && parts.n == 4 &&
                       digits_lstrip_minus(parts.v[2]) &&
                       digits_lstrip_minus(parts.v[3])) {
                int64_t x, y;
                if (parse_signed(parts.v[2], &x) ||
                    parse_signed(parts.v[3], &y)) {
                    err->crash = 1;          /* uncaught ValueError */
                    rc = -1;
                    goto done;
                }
                /* dict semantics: a duplicate anchor name keeps the FIRST
                 * position and takes the LAST value (Python assigns into
                 * out["anchors"][name]) */
                size_t e;
                for (e = 0; e < nanc; e++)
                    if (strcmp(anc_name[e], parts.v[1]) == 0) break;
                if (e < nanc) {
                    anc_x[e] = x;
                    anc_y[e] = y;
                } else {
                    if (nanc == acap) {
                        size_t nc = acap ? acap * 2 : 4;
                        char **nn = (char **)realloc(anc_name, nc * sizeof *nn);
                        if (!nn) { rc = px_oom(err); goto done; }
                        anc_name = nn;
                        int64_t *nx = (int64_t *)realloc(anc_x, nc * sizeof *nx);
                        if (!nx) { rc = px_oom(err); goto done; }
                        anc_x = nx;
                        int64_t *ny = (int64_t *)realloc(anc_y, nc * sizeof *ny);
                        if (!ny) { rc = px_oom(err); goto done; }
                        anc_y = ny;
                        acap = nc;
                    }
                    anc_name[nanc] = xstrdup(parts.v[1]);
                    if (!anc_name[nanc]) { rc = px_oom(err); goto done; }
                    anc_x[nanc] = x;
                    anc_y[nanc] = y;
                    nanc++;
                }
            } else if (strcmp(kw, "frames") == 0 && parts.n == 2 &&
                       all_digits(parts.v[1])) {
                if (version == 1) {
                    rc = px_fail(err, ln, "E_PX_FRAMES", "'frames N' requires 'px 2'");
                    goto done;
                }
                declared = parse_uint(parts.v[1]);
            } else if (strcmp(kw, "fps") == 0 && parts.n == 2 &&
                       all_digits(parts.v[1])) {
                if (version == 1) {
                    rc = px_fail(err, ln, "E_PX_FRAMES", "'fps' requires 'px 2'");
                    goto done;
                }
                fps = parse_uint(parts.v[1]);
                has_fps = 1;
            } else if (strcmp(kw, "frame") == 0 && parts.n == 2 &&
                       all_digits(parts.v[1])) {
                if (version == 1) {
                    rc = px_fail(err, ln, "E_PX_FRAMES",
                                 "'frame i' blocks require 'px 2'");
                    goto done;
                }
                frame_idx = parse_uint(parts.v[1]);
                for (size_t k = 0; k < nf; k++)
                    if (fblks[k].idx == frame_idx) {
                        rc = px_fail(err, ln, "E_PX_FRAMES",
                                     "duplicate 'frame %lld' block",
                                     (long long)frame_idx);
                        goto done;
                    }
                if (nf == fcap) {
                    size_t nc = fcap ? fcap * 2 : 4;
                    frame_blk *p = (frame_blk *)realloc(fblks, nc * sizeof *p);
                    if (!p) { rc = px_oom(err); goto done; }
                    fblks = p; fcap = nc;
                }
                memset(&fblks[nf], 0, sizeof fblks[nf]);
                fblks[nf].idx = frame_idx;
                nf++;
                block = 3;
            } else if ((strcmp(kw, "legend") == 0 || strcmp(kw, "rows") == 0) &&
                       parts.n == 1) {
                if (strcmp(kw, "rows") == 0 && version == 2) {
                    rc = px_fail(err, ln, "E_PX_FRAMES",
                                 "'px 2' uses 'frame i' blocks, not 'rows'");
                    goto done;
                }
                block = strcmp(kw, "legend") == 0 ? 1 : 2;
            } else {
                char *r = af_py_repr_n(raw + b0, b1 - b0);
                if (!r) { rc = px_oom(err); goto done; }
                rc = px_fail(err, ln, "E_PX_PARSE",
                             "unknown or malformed statement %s", r);
                free(r);
                goto done;
            }
        } else {                                          /* indented line */
            if (block == 1) {                             /* legend */
                strvec p = {0};
                if (split_tok(raw + b0, b1 - b0, &p)) { rc = px_oom(err); goto done; }
                if (p.n != 2 || cp_count(p.v[0], strlen(p.v[0])) != 1) {
                    sv_free(&p);
                    rc = px_fail(err, ln, "E_PX_PARSE",
                                 "legend line must be '<char> <value>'");
                    goto done;
                }
                size_t k;
                for (k = 0; k < nleg; k++)
                    if (strcmp(leg_ch[k], p.v[0]) == 0) break;
                if (k == nleg) {
                    if (nleg == lcap) {
                        size_t nc = lcap ? lcap * 2 : 8;
                        char **cc = (char **)realloc(leg_ch, nc * sizeof *cc);
                        char **vv = (char **)realloc(leg_val, nc * sizeof *vv);
                        if (!cc || !vv) {
                            if (cc != leg_ch) leg_ch = cc;
                            if (vv != leg_val) leg_val = vv;
                            sv_free(&p);
                            rc = px_oom(err);
                            goto done;
                        }
                        leg_ch = cc; leg_val = vv; lcap = nc;
                    }
                    leg_ch[nleg] = xstrdup(p.v[0]);
                    leg_val[nleg] = xstrdup(p.v[1]);
                    if (!leg_ch[nleg] || !leg_val[nleg]) {
                        sv_free(&p);
                        rc = px_oom(err);
                        goto done;
                    }
                    nleg++;
                } else {
                    free(leg_val[k]);
                    leg_val[k] = xstrdup(p.v[1]);
                    if (!leg_val[k]) { sv_free(&p); rc = px_oom(err); goto done; }
                }
                sv_free(&p);
            } else if (block == 2) {                      /* v1 rows */
                if (row_push(&rows, ln, raw + b0, b1 - b0)) {
                    rc = px_oom(err);
                    goto done;
                }
            } else if (block == 3) {                      /* v2 frame rows */
                size_t k;
                for (k = 0; k < nf; k++)
                    if (fblks[k].idx == frame_idx) break;
                if (k >= nf || row_push(&fblks[k].rows, ln, raw + b0, b1 - b0)) {
                    rc = px_oom(err);
                    goto done;
                }
            } else {
                rc = px_fail(err, ln, "E_PX_PARSE", "indented line outside a block");
                goto done;
            }
        }
    }

    if (!seen_header) {
        rc = px_fail(err, 1, "E_PX_PARSE", "missing 'px 1' or 'px 2' header");
        goto done;
    }
    if (w < 0) {
        rc = px_fail(err, 1, "E_PX_PARSE", "missing 'size W H'");
        goto done;
    }

    /* frame sets: v1 uses the rows block, v2 the declared frame blocks */
    if (version == 2) {
        if (declared < 0) {
            rc = px_fail(err, 1, "E_PX_FRAMES", "px 2 requires 'frames N'");
            goto done;
        }
        if (declared < 1) {
            rc = px_fail(err, 1, "E_PX_FRAMES",
                         "'frames %lld' must be at least 1", (long long)declared);
            goto done;
        }
        int bad = (int64_t)nf != declared;
        for (int64_t k = 0; !bad && k < declared; k++) {
            size_t j;
            for (j = 0; j < nf; j++)
                if (fblks[j].idx == k) break;
            if (j >= nf) bad = 1;
        }
        if (bad) {
            int64_t *sorted = (int64_t *)malloc((nf ? nf : 1) * sizeof *sorted);
            if (!sorted) { rc = px_oom(err); goto done; }
            for (size_t k = 0; k < nf; k++) sorted[k] = fblks[k].idx;
            for (size_t k = 1; k < nf; k++) {
                int64_t v = sorted[k];
                size_t j = k;
                while (j && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; j--; }
                sorted[j] = v;
            }
            size_t cap = nf * 24 + 64;
            char *lst = (char *)malloc(cap);
            if (!lst) { free(sorted); rc = px_oom(err); goto done; }
            size_t pos = 0;
            lst[pos++] = '[';
            for (size_t k = 0; k < nf; k++) {
                if (k) pos += (size_t)snprintf(lst + pos, cap - pos, ", ");
                pos += (size_t)snprintf(lst + pos, cap - pos, "%lld",
                                        (long long)sorted[k]);
            }
            lst[pos++] = ']';
            lst[pos] = 0;
            free(sorted);
            rc = px_fail(err, 1, "E_PX_FRAMES",
                         "'frames %lld' but frame blocks found for %s "
                         "(need one each for 0..%lld)",
                         (long long)declared, lst, (long long)(declared - 1));
            free(lst);
            goto done;
        }
        nsets = (size_t)declared;
        sets = (rowvec *)calloc(nsets, sizeof *sets);
        if (!sets) { rc = px_oom(err); goto done; }
        for (size_t k = 0; k < nsets; k++) {
            size_t j;
            for (j = 0; j < nf; j++)
                if (fblks[j].idx == (int64_t)k) break;
            sets[k] = fblks[j].rows;        /* struct copy */
            memset(&fblks[j].rows, 0, sizeof fblks[j].rows);
        }
    } else {
        nsets = 1;
        sets = (rowvec *)calloc(1, sizeof *sets);
        if (!sets) { rc = px_oom(err); goto done; }
        sets[0] = rows;
        memset(&rows, 0, sizeof rows);
    }

    for (size_t fi = 0; fi < nsets; fi++) {
        if ((int64_t)sets[fi].n != h) {
            int atln = sets[fi].n ? sets[fi].v[0].ln : 1;
            rc = px_fail(err, atln, "E_PX_PARSE",
                         "frame %zu: expected %lld rows, found %zu",
                         fi, (long long)h, sets[fi].n);
            goto done;
        }
        for (size_t r = 0; r < sets[fi].n; r++) {
            const char *row = sets[fi].v[r].text;
            size_t rn2 = sets[fi].v[r].len;
            int rln = sets[fi].v[r].ln;
            size_t cols = cp_count(row, rn2);
            if ((int64_t)cols != w) {
                rc = px_fail(err, rln, "E_PX_PARSE",
                             "frame %zu: row has %zu pixels, expected %lld",
                             fi, cols, (long long)w);
                goto done;
            }
            size_t ci = 0;
            while (ci < rn2) {
                size_t cl = cp_len(row, rn2, ci);
                size_t k;
                for (k = 0; k < nleg; k++)
                    if (strlen(leg_ch[k]) == cl &&
                        memcmp(leg_ch[k], row + ci, cl) == 0)
                        break;
                if (k >= nleg) {
                    char *rp = af_py_repr_n(row + ci, cl);
                    if (!rp) { rc = px_oom(err); goto done; }
                    rc = px_fail(err, rln, "E_PX_PARSE",
                                 "pixel %s not in legend", rp);
                    free(rp);
                    goto done;
                }
                ci += cl;
            }
        }
    }

    /* attach anchor */
    {
        size_t k;
        for (k = 0; k < nanc; k++)
            if (strcmp(anc_name[k], "attach") == 0) break;
        if (k >= nanc) {
            rc = px_fail(err, 1, "E_PX_PARSE", "missing 'anchor attach X Y'");
            goto done;
        }
        if (anc_x[k] < 0 || anc_x[k] >= w || anc_y[k] < 0 || anc_y[k] >= h) {
            rc = px_fail(err, 1, "E_PX_PARSE",
                         "anchor attach lies outside the fragment");
            goto done;
        }
    }

    /* success: assemble the result */
    {
        af_px *p = (af_px *)calloc(1, sizeof *p);
        if (!p) { rc = px_oom(err); goto done; }
        p->name = name;
        name = NULL;
        p->w = w;
        p->h = h;
        p->version = version;
        p->fps = fps;
        p->has_fps = has_fps;
        p->frame_count = version == 2 ? (int)declared : 1;
        p->leg_ch = leg_ch;
        p->leg_val = leg_val;
        p->nleg = nleg;
        leg_ch = leg_val = NULL;
        nleg = lcap = 0;
        p->anc_name = anc_name;
        p->anc_x = anc_x;
        p->anc_y = anc_y;
        p->nanc = nanc;
        anc_name = NULL;
        anc_x = anc_y = NULL;
        nanc = acap = 0;
        p->frame_rows = (char ***)calloc((size_t)p->frame_count, sizeof(char **));
        p->frame_nrows = (size_t *)calloc((size_t)p->frame_count, sizeof(size_t));
        if (!p->frame_rows || !p->frame_nrows) {
            af_px_free(p);
            rc = px_oom(err);
            goto done;
        }
        for (int f = 0; f < p->frame_count; f++) {
            p->frame_nrows[f] = sets[f].n;
            p->frame_rows[f] = (char **)calloc(sets[f].n ? sets[f].n : 1,
                                               sizeof(char *));
            if (!p->frame_rows[f]) {
                af_px_free(p);
                rc = px_oom(err);
                goto done;
            }
            for (size_t r = 0; r < sets[f].n; r++) {
                p->frame_rows[f][r] = sets[f].v[r].text;
                sets[f].v[r].text = NULL;     /* ownership moves */
            }
        }
        *out = p;
        rc = 0;
    }

done:
    sv_free(&parts);
    free(name);
    for (size_t k = 0; k < nleg; k++) { free(leg_ch[k]); free(leg_val[k]); }
    free(leg_ch);
    free(leg_val);
    for (size_t k = 0; k < nanc; k++) free(anc_name[k]);
    free(anc_name);
    free(anc_x);
    free(anc_y);
    rowvec_free(&rows);
    if (fblks) {
        for (size_t k = 0; k < nf; k++) rowvec_free(&fblks[k].rows);
        free(fblks);
    }
    if (sets) {
        for (size_t k = 0; k < nsets; k++) rowvec_free(&sets[k]);
        free(sets);
    }
    for (size_t k = 0; k < lines.n; k++) free(lines.v[k].text);
    free(lines.v);
    return rc;
}

void af_px_free(af_px *px) {
    if (!px) return;
    free(px->name);
    for (size_t i = 0; i < px->nleg; i++) {
        free(px->leg_ch[i]);
        free(px->leg_val[i]);
    }
    free(px->leg_ch);
    free(px->leg_val);
    for (size_t i = 0; i < px->nanc; i++) free(px->anc_name[i]);
    free(px->anc_name);
    free(px->anc_x);
    free(px->anc_y);
    if (px->frame_rows) {
        for (size_t f = 0; f < (size_t)px->frame_count; f++) {
            if (px->frame_rows[f]) {
                for (size_t r = 0; r < px->frame_nrows[f]; r++)
                    free(px->frame_rows[f][r]);
                free(px->frame_rows[f]);
            }
        }
    }
    free(px->frame_rows);
    free(px->frame_nrows);
    free(px);
}

/* ---- file entry point -------------------------------------------------- */

int af_px_read(const char *path, af_px **out, af_px_error *err) {
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { err->crash = 1; return -1; }      /* unreadable: crash */
    char *buf = NULL;
    size_t n = 0, cap = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 1 > cap) {
            size_t nc = cap ? cap * 2 : 4096;
            char *p = (char *)realloc(buf, nc);
            if (!p) { free(buf); fclose(f); err->crash = 1; return -1; }
            buf = p;
            cap = nc;
        }
        buf[n++] = (char)c;
    }
    int ferr = ferror(f);
    fclose(f);
    if (ferr) { free(buf); err->crash = 1; return -1; }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) { err->crash = 1; return -1; }
    }
    buf[n] = 0;
    /* read_text() decodes UTF-8 and raises on anything else */
    size_t i = 0;
    int bad = 0;
    while (i < n) {
        unsigned char c2 = (unsigned char)buf[i];
        size_t want = c2 < 0x80 ? 1 : (c2 & 0xe0) == 0xc0 ? 2 :
                      (c2 & 0xf0) == 0xe0 ? 3 : (c2 & 0xf8) == 0xf0 ? 4 : 0;
        if (!want || i + want > n) { bad = 1; break; }
        for (size_t k = 1; k < want; k++)
            if (((unsigned char)buf[i + k] & 0xc0) != 0x80) { bad = 1; break; }
        if (bad) break;
        i += want;
    }
    if (bad) { free(buf); err->crash = 1; return -1; }
    int rc = af_parse_px(buf, n, out, err);
    free(buf);
    return rc;
}
