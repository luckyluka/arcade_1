/* json.c -- bundled JSON parser + writer (M4.1a). See json.h. */
#include "json.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AF_MAX_DEPTH 512

/* ------------------------------------------------------------------ */
/* growable byte buffer                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    char *d;
    size_t n, cap;
} af_buf;

static int buf_reserve(af_buf *b, size_t extra) {
    if (b->n + extra + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 32;
        while (cap < b->n + extra + 1) cap *= 2;
        char *nd = (char *)realloc(b->d, cap);
        if (!nd) return -1;
        b->d = nd;
        b->cap = cap;
    }
    return 0;
}

static int buf_putc(af_buf *b, char c) {
    if (buf_reserve(b, 1)) return -1;
    b->d[b->n++] = c;
    b->d[b->n] = '\0';
    return 0;
}

static int buf_puts(af_buf *b, const char *s, size_t n) {
    if (buf_reserve(b, n)) return -1;
    memcpy(b->d + b->n, s, n);
    b->n += n;
    b->d[b->n] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* node construction                                                   */
/* ------------------------------------------------------------------ */
static af_json *node_new(af_json_type t) {
    af_json *v = (af_json *)calloc(1, sizeof *v);
    if (v) v->type = t;
    return v;
}

af_json *af_json_null(void) { return node_new(AF_JSON_NULL); }
af_json *af_json_bool(int b) {
    af_json *v = node_new(AF_JSON_BOOL);
    if (v) v->b = b != 0;
    return v;
}
af_json *af_json_int(int64_t i) {
    af_json *v = node_new(AF_JSON_INT);
    if (v) v->i = i;
    return v;
}
af_json *af_json_float(double d) {
    af_json *v = node_new(AF_JSON_FLOAT);
    if (v) v->d = d;
    return v;
}

af_json *af_json_strn(const char *utf8, size_t n) {
    af_json *v = node_new(AF_JSON_STRING);
    if (!v) return NULL;
    v->s = (char *)malloc(n + 1);
    if (!v->s) { free(v); return NULL; }
    memcpy(v->s, utf8, n);
    v->s[n] = '\0';
    v->slen = n;
    return v;
}

af_json *af_json_str(const char *utf8) {
    return af_json_strn(utf8, strlen(utf8));
}

af_json *af_json_arr(void) { return node_new(AF_JSON_ARRAY); }
af_json *af_json_obj(void) { return node_new(AF_JSON_OBJECT); }

static int grow_items(af_json *v) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        af_json **ni = (af_json **)realloc(v->items, cap * sizeof *ni);
        if (!ni) return -1;
        v->items = ni;
        v->cap = cap;
    }
    return 0;
}

int af_json_arr_push(af_json *a, af_json *v) {
    if (!a || !v || a->type != AF_JSON_ARRAY) return -1;
    if (grow_items(a)) return -1;
    a->items[a->n++] = v;
    return 0;
}

int af_json_obj_put(af_json *o, const char *key, af_json *v) {
    if (!o || !key || !v || o->type != AF_JSON_OBJECT) return -1;
    af_json *k = af_json_str(key);
    if (!k) return -1;
    if (grow_items(o) || grow_items(o)) { free(k); return -1; }
    o->items[o->n++] = k;
    o->items[o->n++] = v;
    return 0;
}

const af_json *af_json_get(const af_json *obj, const char *key) {
    if (!obj || obj->type != AF_JSON_OBJECT) return NULL;
    for (size_t i = 0; i + 1 < obj->n; i += 2)
        if (obj->items[i]->type == AF_JSON_STRING &&
            strcmp(obj->items[i]->s, key) == 0)
            return obj->items[i + 1];
    return NULL;
}

/* index of the pair with this key, or -1 */
static long obj_index(const af_json *o, const char *key) {
    for (size_t i = 0; i + 1 < o->n; i += 2)
        if (o->items[i]->type == AF_JSON_STRING &&
            strcmp(o->items[i]->s, key) == 0)
            return (long)i;
    return -1;
}

af_json *af_json_clone(const af_json *v) {
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
            af_json *c = af_json_clone(v->items[i]);
            if (!c || af_json_arr_push(a, c)) { af_json_free(c); af_json_free(a); return NULL; }
        }
        return a;
    }
    case AF_JSON_OBJECT: {
        af_json *o = af_json_obj();
        if (!o) return NULL;
        for (size_t i = 0; i + 1 < v->n; i += 2) {
            af_json *c = af_json_clone(v->items[i + 1]);
            if (!c || af_json_obj_put(o, v->items[i]->s, c)) {
                af_json_free(c); af_json_free(o); return NULL;
            }
        }
        return o;
    }
    }
    return NULL;
}

int af_json_obj_set(af_json *o, const char *key, af_json *v) {
    if (!o || !key || !v || o->type != AF_JSON_OBJECT) { af_json_free(v); return -1; }
    long at = obj_index(o, key);
    if (at >= 0) {
        af_json_free(o->items[at + 1]);
        o->items[at + 1] = v;
        return 0;
    }
    return af_json_obj_put(o, key, v);
}

int af_json_obj_del(af_json *o, const char *key) {
    if (!o || !key || o->type != AF_JSON_OBJECT) return -1;
    long at = obj_index(o, key);
    if (at < 0) return 0;
    af_json_free(o->items[at]);
    af_json_free(o->items[at + 1]);
    for (size_t i = (size_t)at + 2; i < o->n; i++) o->items[i - 2] = o->items[i];
    o->n -= 2;
    return 0;
}

static int num_bits_eq(double a, double b) {
    uint64_t x, y;
    memcpy(&x, &a, sizeof x);
    memcpy(&y, &b, sizeof y);
    return x == y;
}

int af_json_equal(const af_json *a, const af_json *b) {
    if (a == b) return 1;
    if (!a || !b || a->type != b->type) return 0;
    switch (a->type) {
    case AF_JSON_NULL: return 1;
    case AF_JSON_BOOL: return a->b == b->b;
    case AF_JSON_INT: return a->i == b->i;
    case AF_JSON_FLOAT: return num_bits_eq(a->d, b->d);
    case AF_JSON_STRING:
        return a->slen == b->slen && memcmp(a->s, b->s, a->slen) == 0;
    case AF_JSON_ARRAY:
        if (a->n != b->n) return 0;
        for (size_t i = 0; i < a->n; i++)
            if (!af_json_equal(a->items[i], b->items[i])) return 0;
        return 1;
    case AF_JSON_OBJECT: {
        if (a->n != b->n) return 0;
        for (size_t i = 0; i + 1 < a->n; i += 2) {
            const af_json *bv = af_json_get(b, a->items[i]->s);
            if (!bv || !af_json_equal(a->items[i + 1], bv)) return 0;
        }
        return 1;
    }
    }
    return 0;
}

void af_json_free(af_json *v) {
    if (!v) return;
    free(v->s);
    for (size_t i = 0; i < v->n; i++) af_json_free(v->items[i]);
    free(v->items);
    free(v);
}

/* ------------------------------------------------------------------ */
/* parser                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *s;
    size_t n, i;
    int depth;
    char *err;
    size_t errsz;
} af_parser;

static int perr(af_parser *p, const char *msg) {
    if (p->err && p->errsz)
        snprintf(p->err, p->errsz, "offset %zu: %s", p->i, msg);
    return -1;
}

static int at_end(const af_parser *p) { return p->i >= p->n; }
static char peek(const af_parser *p) { return p->i < p->n ? p->s[p->i] : '\0'; }

static int skip_ws(af_parser *p) {
    while (p->i < p->n) {
        char c = p->s[p->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->i++;
        else break;
    }
    return 0;
}

static int match_lit(af_parser *p, const char *lit) {
    size_t n = strlen(lit);
    if (p->n - p->i >= n && memcmp(p->s + p->i, lit, n) == 0) {
        p->i += n;
        return 0;
    }
    return -1;
}

/* hex4: exactly four hex digits -> value; advances on success */
static int hex4(af_parser *p, unsigned *out) {
    if (p->n - p->i < 4) return -1;
    unsigned v = 0;
    for (int k = 0; k < 4; k++) {
        char c = p->s[p->i + k];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return -1;
        v = (v << 4) | d;
    }
    p->i += 4;
    *out = v;
    return 0;
}

/* append one code point as UTF-8 (or WTF-8 for lone surrogates) */
static int put_cp(af_buf *b, unsigned cp) {
    if (cp < 0x80) {
        return buf_putc(b, (char)cp);
    } else if (cp < 0x800) {
        char t[2] = {(char)(0xC0 | (cp >> 6)), (char)(0x80 | (cp & 0x3F))};
        return buf_puts(b, t, 2);
    } else if (cp < 0x10000) {
        char t[3] = {(char)(0xE0 | (cp >> 12)),
                     (char)(0x80 | ((cp >> 6) & 0x3F)),
                     (char)(0x80 | (cp & 0x3F))};
        return buf_puts(b, t, 3);
    }
    char t[4] = {(char)(0xF0 | (cp >> 18)),
                 (char)(0x80 | ((cp >> 12) & 0x3F)),
                 (char)(0x80 | ((cp >> 6) & 0x3F)),
                 (char)(0x80 | (cp & 0x3F))};
    return buf_puts(b, t, 4);
}

/* parse the raw UTF-8 body of a string, validating.
 * returns 0 when the closing quote was consumed, 1 when stopped at a
 * backslash (not consumed -- the caller processes the escape), -1 error */
static int parse_string_raw(af_parser *p, af_buf *b) {
    while (!at_end(p)) {
        unsigned char c = (unsigned char)p->s[p->i];
        if (c == '"') { p->i++; return 0; }
        if (c == '\\') return 1;
        if (c < 0x20) return perr(p, "raw control character in string");
        if (c < 0x80) {
            if (buf_putc(b, (char)c)) return perr(p, "out of memory");
            p->i++;
            continue;
        }
        /* multibyte UTF-8 with overlong/surrogate/range guards */
        size_t start = p->i;
        unsigned cp;
        int len;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
        else if (c == 0xE0)          { len = 3; cp = 0; }
        else if (c >= 0xE1 && c <= 0xEC) { len = 3; cp = c & 0x0F; }
        else if (c == 0xED)          { len = 3; cp = 0x0D; } /* surrogates rejected below */
        else if (c >= 0xEE && c <= 0xEF) { len = 3; cp = c & 0x0F; }
        else if (c == 0xF0)          { len = 4; cp = 0; }
        else if (c >= 0xF1 && c <= 0xF3) { len = 4; cp = c & 0x07; }
        else if (c == 0xF4)          { len = 4; cp = 0x04; }
        else return perr(p, "invalid UTF-8");
        if (p->n - p->i < (size_t)len) return perr(p, "truncated UTF-8");
        for (int k = 1; k < len; k++) {
            unsigned char cc = (unsigned char)p->s[p->i + k];
            if ((cc & 0xC0) != 0x80) return perr(p, "invalid UTF-8");
            cp = (cp << 6) | (cc & 0x3F);
        }
        /* overlong, surrogate and > U+10FFFF guards */
        if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)))
            return perr(p, "invalid UTF-8");
        if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))
            return perr(p, "invalid UTF-8");
        if (buf_puts(b, p->s + start, (size_t)len)) return perr(p, "out of memory");
        p->i += (size_t)len;
    }
    return perr(p, "unterminated string");
}

static int parse_string(af_parser *p, af_json **out) {
    if (peek(p) != '"') return perr(p, "expected string");
    p->i++;
    af_buf b = {NULL, 0, 0};
    int closed = 0;
    while (!at_end(p)) {
        char c = p->s[p->i];
        if (c == '"') { p->i++; closed = 1; break; }
        if (c == '\\') {
            p->i++;
            if (at_end(p)) { free(b.d); return perr(p, "unterminated escape"); }
            char e = p->s[p->i++];
            switch (e) {
            case '"': case '\\': case '/':
                if (buf_putc(&b, e)) goto oom;
                break;
            case 'b': if (buf_putc(&b, '\b')) goto oom; break;
            case 'f': if (buf_putc(&b, '\f')) goto oom; break;
            case 'n': if (buf_putc(&b, '\n')) goto oom; break;
            case 'r': if (buf_putc(&b, '\r')) goto oom; break;
            case 't': if (buf_putc(&b, '\t')) goto oom; break;
            case 'u': {
                unsigned cp;
                if (hex4(p, &cp)) { free(b.d); return perr(p, "bad \\u escape"); }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    /* high surrogate: must be followed by a low pair */
                    if (p->n - p->i >= 6 && p->s[p->i] == '\\' &&
                        p->s[p->i + 1] == 'u') {
                        size_t save = p->i;
                        p->i += 2;
                        unsigned lo;
                        if (hex4(p, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            p->i = save; /* lone high surrogate: keep the unit */
                        }
                    }
                    /* else: lone high surrogate, keep the unit (WTF-8) */
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    /* lone low surrogate: keep the unit (WTF-8) */
                }
                if (put_cp(&b, cp)) goto oom;
                break;
            }
            default:
                free(b.d);
                return perr(p, "unknown escape");
            }
            continue;
        }
        int rc = parse_string_raw(p, &b);
        if (rc < 0) { free(b.d); return -1; }
        if (rc == 0) { closed = 1; break; } /* raw consumed the closing quote */
        /* rc == 1: stopped at a backslash -- loop handles the escape */
    }
    if (!closed) { free(b.d); return perr(p, "unterminated string"); }
    if (!b.d) {
        /* empty string: keep a valid allocation for uniform ownership */
        b.d = (char *)malloc(1);
        if (!b.d) return perr(p, "out of memory");
        b.d[0] = '\0';
        b.n = 0;
    }
    af_json *v = node_new(AF_JSON_STRING);
    if (!v) { free(b.d); return perr(p, "out of memory"); }
    v->s = b.d;
    v->slen = b.n;
    *out = v;
    return 0;
oom:
    free(b.d);
    return perr(p, "out of memory");
}

static int parse_number(af_parser *p, af_json **out) {
    size_t start = p->i;
    int neg = 0;
    if (peek(p) == '-') { neg = 1; p->i++; }
    if (at_end(p)) return perr(p, "truncated number");
    char c = p->s[p->i];
    if (c == '0') {
        p->i++;
        if (!at_end(p) && p->s[p->i] >= '0' && p->s[p->i] <= '9')
            return perr(p, "leading zero in number");
    } else if (c >= '1' && c <= '9') {
        while (!at_end(p) && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
    } else {
        return perr(p, "invalid number");
    }
    int is_float = 0;
    if (!at_end(p) && p->s[p->i] == '.') {
        is_float = 1;
        p->i++;
        if (at_end(p) || p->s[p->i] < '0' || p->s[p->i] > '9')
            return perr(p, "digit required after '.'");
        while (!at_end(p) && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
    }
    if (!at_end(p) && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
        is_float = 1;
        p->i++;
        if (!at_end(p) && (p->s[p->i] == '+' || p->s[p->i] == '-')) p->i++;
        if (at_end(p) || p->s[p->i] < '0' || p->s[p->i] > '9')
            return perr(p, "digit required in exponent");
        while (!at_end(p) && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
    }
    size_t len = p->i - start;
    if (!is_float) {
        /* integer: accumulate with an int64 overflow guard */
        uint64_t v = 0;
        uint64_t lim = neg ? (uint64_t)9223372036854775808ULL
                           : (uint64_t)9223372036854775807ULL;
        size_t k = neg ? start + 1 : start;
        for (; k < p->i; k++) {
            uint64_t d = (uint64_t)(p->s[k] - '0');
            /* pre-check: v*10+d must not wrap uint64 */
            if (v > lim / 10 || (v == lim / 10 && d > lim % 10))
                return perr(p, "integer out of int64 range");
            v = v * 10 + d;
        }
        int64_t outv;
        if (neg)
            outv = (v == (uint64_t)9223372036854775808ULL)
                       ? INT64_MIN : -(int64_t)v;
        else
            outv = (int64_t)v;
        af_json *node = af_json_int(outv);
        if (!node) return perr(p, "out of memory");
        *out = node;
        return 0;
    }
    /* float: the token is locale-independent ASCII; the program never
     * calls setlocale, so strtod runs in the C locale ("." only) */
    char *tok = (char *)malloc(len + 1);
    if (!tok) return perr(p, "out of memory");
    memcpy(tok, p->s + start, len);
    tok[len] = '\0';
    char *end = NULL;
    double d = strtod(tok, &end);
    int bad = (end == tok || *end != '\0');
    free(tok);
    if (bad) return perr(p, "invalid number");
    af_json *node = af_json_float(d);
    if (!node) return perr(p, "out of memory");
    *out = node;
    return 0;
}

static int parse_value(af_parser *p, af_json **out);

static int parse_array(af_parser *p, af_json **out) {
    p->i++; /* '[' */
    af_json *a = af_json_arr();
    if (!a) return perr(p, "out of memory");
    skip_ws(p);
    if (peek(p) == ']') { p->i++; *out = a; return 0; }
    for (;;) {
        af_json *v = NULL;
        if (parse_value(p, &v)) { af_json_free(a); return -1; }
        if (af_json_arr_push(a, v)) { af_json_free(v); af_json_free(a);
                                      return perr(p, "out of memory"); }
        skip_ws(p);
        char c = peek(p);
        if (c == ',') { p->i++; skip_ws(p); continue; }
        if (c == ']') { p->i++; *out = a; return 0; }
        af_json_free(a);
        return perr(p, at_end(p) ? "unterminated array" : "expected ',' or ']'");
    }
}

static int parse_object(af_parser *p, af_json **out) {
    p->i++; /* '{' */
    af_json *o = af_json_obj();
    if (!o) return perr(p, "out of memory");
    skip_ws(p);
    if (peek(p) == '}') { p->i++; *out = o; return 0; }
    for (;;) {
        skip_ws(p);
        if (peek(p) != '"') {
            af_json_free(o);
            return perr(p, at_end(p) ? "unterminated object"
                                     : "object key must be a string");
        }
        af_json *k = NULL;
        if (parse_string(p, &k)) { af_json_free(o); return -1; }
        for (size_t i = 0; i + 1 < o->n; i += 2) {
            if (strcmp(o->items[i]->s, k->s) == 0) {
                af_json_free(k);
                af_json_free(o);
                return perr(p, "duplicate object key");
            }
        }
        skip_ws(p);
        if (peek(p) != ':') {
            af_json_free(k);
            af_json_free(o);
            return perr(p, "expected ':' after object key");
        }
        p->i++;
        af_json *v = NULL;
        if (parse_value(p, &v)) { af_json_free(k); af_json_free(o); return -1; }
        if (grow_items(o) || grow_items(o)) {
            af_json_free(k); af_json_free(v); af_json_free(o);
            return perr(p, "out of memory");
        }
        o->items[o->n++] = k;
        o->items[o->n++] = v;
        skip_ws(p);
        char c = peek(p);
        if (c == ',') { p->i++; continue; }
        if (c == '}') { p->i++; *out = o; return 0; }
        af_json_free(o);
        return perr(p, at_end(p) ? "unterminated object" : "expected ',' or '}'");
    }
}

static int parse_value(af_parser *p, af_json **out) {
    if (++p->depth > AF_MAX_DEPTH) return perr(p, "nesting too deep");
    skip_ws(p);
    int rc;
    char c = peek(p);
    if (at_end(p)) {
        rc = perr(p, "unexpected end of input");
    } else if (c == '"') {
        rc = parse_string(p, out);
    } else if (c == '{') {
        rc = parse_object(p, out);
    } else if (c == '[') {
        rc = parse_array(p, out);
    } else if (c == 't') {
        rc = match_lit(p, "true") ? perr(p, "invalid literal")
                                  : (*out = af_json_bool(1), 0);
        if (rc == 0 && !*out) rc = perr(p, "out of memory");
    } else if (c == 'f') {
        rc = match_lit(p, "false") ? perr(p, "invalid literal")
                                   : (*out = af_json_bool(0), 0);
        if (rc == 0 && !*out) rc = perr(p, "out of memory");
    } else if (c == 'n') {
        rc = match_lit(p, "null") ? perr(p, "invalid literal")
                                  : (*out = af_json_null(), 0);
        if (rc == 0 && !*out) rc = perr(p, "out of memory");
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        rc = parse_number(p, out);
    } else {
        rc = perr(p, "unexpected character");
    }
    p->depth--;
    return rc;
}

int af_json_parse(const char *text, size_t len, af_json **out,
                  char *err, size_t errsz) {
    if (err && errsz) err[0] = '\0';
    if (!text || !out) {
        if (err && errsz) snprintf(err, errsz, "null argument");
        return -1;
    }
    af_parser p = {text, len, 0, 0, err, errsz};
    af_json *v = NULL;
    if (parse_value(&p, &v)) return -1;
    skip_ws(&p);
    if (p.i != p.n) {
        af_json_free(v);
        return perr(&p, "trailing characters after value");
    }
    *out = v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* writer (docs/OUTPUT_FORMAT.md)                                      */
/* ------------------------------------------------------------------ */
static int write_string(af_buf *b, const char *s, size_t n) {
    if (buf_putc(b, '"')) return -1;
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            if (buf_putc(b, '\\') || buf_putc(b, (char)c)) return -1;
            i++;
        } else if (c == '\b') { if (buf_puts(b, "\\b", 2)) return -1; i++; }
        else if (c == '\f')   { if (buf_puts(b, "\\f", 2)) return -1; i++; }
        else if (c == '\n')   { if (buf_puts(b, "\\n", 2)) return -1; i++; }
        else if (c == '\r')   { if (buf_puts(b, "\\r", 2)) return -1; i++; }
        else if (c == '\t')   { if (buf_puts(b, "\\t", 2)) return -1; i++; }
        else if (c < 0x20) {
            char t[16];
            snprintf(t, sizeof t, "\\u%04x", c);
            if (buf_puts(b, t, 6)) return -1;
            i++;
        } else if (c < 0x80) {
            if (buf_putc(b, (char)c)) return -1;
            i++;
        } else {
            /* decode UTF-8 (or WTF-8 lone surrogate) to a code point */
            unsigned cp;
            int len;
            if ((c & 0xE0) == 0xC0)      { len = 2; cp = c & 0x1Fu; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0Fu; }
            else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07u; }
            else return -1; /* invalid UTF-8: refuse, never guess */
            if ((size_t)len > n - i) return -1;
            for (int k = 1; k < len; k++) {
                unsigned char cc = (unsigned char)s[i + k];
                if ((cc & 0xC0) != 0x80) return -1;
                cp = (cp << 6) | (cc & 0x3Fu);
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) {
                /* WTF-8 lone surrogate from a \u escape: re-escape */
                char t[16];
                snprintf(t, sizeof t, "\\u%04x", cp);
                if (buf_puts(b, t, 6)) return -1;
            } else if (cp >= 0x10000) {
                unsigned hi = 0xD800 + ((cp - 0x10000) >> 10);
                unsigned lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                char t[16];
                snprintf(t, sizeof t, "\\u%04x\\u%04x", hi, lo);
                if (buf_puts(b, t, 12)) return -1;
            } else {
                char t[16];
                snprintf(t, sizeof t, "\\u%04x", cp);
                if (buf_puts(b, t, 6)) return -1;
            }
            i += (size_t)len;
        }
    }
    return buf_putc(b, '"');
}

static int write_value(af_buf *b, const af_json *v, int indent, int depth) {
    char tmp[64];
    switch (v->type) {
    case AF_JSON_NULL:  return buf_puts(b, "null", 4);
    case AF_JSON_BOOL:  return v->b ? buf_puts(b, "true", 4)
                                    : buf_puts(b, "false", 5);
    case AF_JSON_INT: {
        int k = snprintf(tmp, sizeof tmp, "%" PRId64, v->i);
        return k < 0 ? -1 : buf_puts(b, tmp, (size_t)k);
    }
    case AF_JSON_FLOAT: {
        /* exactly four decimals, correctly rounded (round-half-even on
         * the exact binary value -- printf's default), never an
         * exponent; non-finite values print as inf/nan exactly as
         * Python's format(v, '.4f') does, and cannot occur in a report
         * (load_json rejects the tokens, P0.5) */
        int k = snprintf(tmp, sizeof tmp, "%.4f", v->d);
        return k < 0 ? -1 : buf_puts(b, tmp, (size_t)k);
    }
    case AF_JSON_STRING:
        return write_string(b, v->s, v->slen);
    case AF_JSON_ARRAY: {
        if (v->n == 0) return buf_puts(b, "[]", 2);
        if (indent < 0) {
            if (buf_putc(b, '[')) return -1;
            for (size_t i = 0; i < v->n; i++) {
                if (i && buf_puts(b, ", ", 2)) return -1;
                if (write_value(b, v->items[i], indent, depth + 1)) return -1;
            }
            return buf_putc(b, ']');
        }
        if (buf_puts(b, "[\n", 2)) return -1;
        char pad[2048];
        int plen = indent * (depth + 1);
        if (plen >= (int)sizeof pad) return -1;
        memset(pad, ' ', (size_t)plen);
        for (size_t i = 0; i < v->n; i++) {
            if (i && buf_puts(b, ",\n", 2)) return -1;
            if (buf_puts(b, pad, (size_t)plen)) return -1;
            if (write_value(b, v->items[i], indent, depth + 1)) return -1;
        }
        if (buf_putc(b, '\n')) return -1;
        int blen = indent * depth;
        if (blen > 0 && buf_puts(b, pad, (size_t)blen)) return -1;
        return buf_putc(b, ']');
    }
    case AF_JSON_OBJECT: {
        if (v->n == 0) return buf_puts(b, "{}", 2);
        if (indent < 0) {
            if (buf_putc(b, '{')) return -1;
            for (size_t i = 0; i + 1 < v->n; i += 2) {
                if (i && buf_puts(b, ", ", 2)) return -1;
                if (write_string(b, v->items[i]->s, v->items[i]->slen)) return -1;
                if (buf_puts(b, ": ", 2)) return -1;
                if (write_value(b, v->items[i + 1], indent, depth + 1)) return -1;
            }
            return buf_putc(b, '}');
        }
        if (buf_puts(b, "{\n", 2)) return -1;
        char pad[2048];
        int plen = indent * (depth + 1);
        if (plen >= (int)sizeof pad) return -1;
        memset(pad, ' ', (size_t)plen);
        for (size_t i = 0; i + 1 < v->n; i += 2) {
            if (i && buf_puts(b, ",\n", 2)) return -1;
            if (buf_puts(b, pad, (size_t)plen)) return -1;
            if (write_string(b, v->items[i]->s, v->items[i]->slen)) return -1;
            if (buf_puts(b, ": ", 2)) return -1;
            if (write_value(b, v->items[i + 1], indent, depth + 1)) return -1;
        }
        if (buf_putc(b, '\n')) return -1;
        int blen = indent * depth;
        if (blen > 0 && buf_puts(b, pad, (size_t)blen)) return -1;
        return buf_putc(b, '}');
    }
    }
    return -1;
}

char *af_json_write(const af_json *v, int indent) {
    if (!v) return NULL;
    af_buf b = {NULL, 0, 0};
    if (write_value(&b, v, indent, 0)) {
        free(b.d);
        return NULL;
    }
    return b.d ? b.d : NULL;
}
