/* expr.c -- M4.1c: prep, lexer, parser and evaluator for the oracle's
 * expression language. See expr.h for the contract; the oracle is
 * tools/arcforge_ref.py::_prep/ev/_ev/to_int plus CPython's ast.parse.
 *
 * PINNED SEMANTICS (probed against Python 3.12, see the commit and
 * docs/experiments if anything here is ever contradicted):
 *
 * _prep is byte-level, BEFORE lexing (so `$` inside a string literal is
 * rewritten too): `$ident` -> `__ident`, then `&&` -> " and ", then each
 * `!` not followed by `=` -> " not ".
 *
 * Parser accept set: full eval-mode Python grammar. Notes that cost
 * probes: leading blank/comment lines are skipped but a first content
 * line indented -> error; a newline at bracket depth 0 ends the
 * expression and only blank/comment lines may follow; `\f` is whitespace,
 * `\v`/`\x7f`/other C0 -> error, NUL -> ValueError (CRASH); decimal
 * numbers reject leading zeros unless all-zero (`00`, `0_0` ok, `0755`
 * not) but floats and complex are exempt (`0755j` ok); underscores must
 * sit between digits (`0x_1` ok right after the prefix, `0x1_` not); a
 * letter run right after a DECIMAL number splits when it is exactly a
 * keyword (`1and 2`, `1if 2 else 3`, `1in 1` parse!) and is an error
 * otherwise (`1and2`, `1abc`), while prefixed numbers never split
 * (`0x1and 2` -> error); `async` is a hard keyword (never an atom) but
 * `match`/`case` are ordinary names; `await` binds a primary and cannot
 * chain (`await await x` -> error); yield is allowed only inside `( )`;
 * a top-level `x := 1` is an error while `[x := 1]`, `(x := 1)`,
 * `f(x := 1)`, `a[x := 1]` are fine and a dict key is not; comprehension
 * iterables and `if` clauses are or_test (no bare ternary); string
 * escapes follow CPython (`\x`/`\u` truncated -> error, unknown `\q` ->
 * kept with a warning on stderr, `\N{...}` names are NOT resolved here --
 * a documented divergence); implicit adjacent string literals fold into
 * one constant, bytes may not mix with str.
 *
 * Evaluator: only Constant(int/float/str/bool)/Name/Unary/BinOp(add
 * sub mul div floordiv mod)/BoolOp/Compare/Call(FUNCS) evaluate; every
 * other node raises
 * `unsupported expression` WITHOUT evaluating children, except BinOp and
 * `~` which evaluate children first (probe: `2 ** nope` -> unknown
 * identifier, `nope if 1 else 2` -> unsupported expression). Compare
 * builds Python's eager six-comparison dict, so `'a' == 1` is a TYPE
 * error (the `<` in the dict raises) and `1 is 'a'` is a type error too
 * (dict before the op check), while `1 is 1` is `unsupported
 * comparison`. BoolOp evaluates ALL children then returns a bool. FUNCS
 * catch only TypeError -> `wrong number or type of arguments to f()`;
 * floor/ceil/round/odd/even/half on a non-finite float raise
 * OverflowError/ValueError UNCAUGHT -> CRASH (exit 1, no stdout), as do
 * to_int() on a str/null/array (TypeError) and on inf/nan. int literals
 * are range-checked (only Constants and folded `-literal`), arithmetic
 * results are not: `2147483647 + 1` is 2147483648.
 *
 * int64 wraps on overflow and a float floor beyond int64 saturates: the
 * P0.3 documented divergence, unreachable in the corpus.
 */
#include "expr.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parser depth (p_expr + p_atom entries): CPython's tokenizer/parser
 * rejects the 201st bracketed nesting level with a SyntaxError and
 * accepts 200 (probed: parens/list/call/dict all flip at exactly 201),
 * so this counter must accept 2*200+2 and reject 2*201+2 -> the syntax
 * error message the oracle reports. */
#define AF_PARSE_DEPTH 402
/* The tokenizer refuses more than 200 open brackets of nesting
 * (`(` x 201 -> "too many nested parentheses"; a call's `(` counts) */
#define AF_LEX_BRACKET_CAP 200
/* Raw frames for chains the depth counter cannot see (unary `---`, `not
 * not`): past this the C stack is at risk -> CRASH (the oracle's own
 * MemoryError/RecursionError at 5975 unary signs / 1000 eval frames is
 * also an uncaught exception -> exit 1 with no stdout). */
#define AF_FRAME_LIMIT 3000
/* Evaluator recursion: the oracle's _ev hits RecursionError at ~1000
 * nesting (990 OK, 1000 crashes when called directly) -> CRASH here too. */
#define AF_EVAL_DEPTH 990
#define AF_STR_REPEAT_CAP (64u << 20)  /* `'x' * N` past this -> CRASH
                                        * (the oracle would MemoryError) */

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char *p;
    size_t n, cap;
    int oom;
} xsb;

static int sb_reserve(xsb *b, size_t extra) {
    if (b->oom) return -1;
    if (b->n + extra + 1 <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->n + extra + 1) cap *= 2;
    char *p = (char *)realloc(b->p, cap);
    if (!p) { b->oom = 1; return -1; }
    b->p = p;
    b->cap = cap;
    return 0;
}

static int sb_add(xsb *b, const char *s, size_t n) {
    if (sb_reserve(b, n)) return -1;
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
    return 0;
}

static int sb_ch(xsb *b, char c) { return sb_add(b, &c, 1); }

static char *sb_take(xsb *b) {
    if (b->oom) { free(b->p); b->p = NULL; return NULL; }
    if (!b->p) {
        b->p = (char *)malloc(1);
        if (!b->p) return NULL;
        b->p[0] = 0;
    }
    char *p = b->p;
    b->p = NULL;
    b->n = b->cap = 0;
    return p;
}

static int is_sp(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\x0c'; /* space, tab, formfeed */
}
static int is_nl(unsigned char c) { return c == '\n' || c == '\r'; }
static int is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int is_id_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_id_char(unsigned char c) {
    return is_id_start(c) || is_digit(c);
}
static int is_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static void *xmalloc(size_t n) { return malloc(n ? n : 1); }

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

/* ------------------------------------------------------------------ */
/* _prep: three byte passes                                           */
/* ------------------------------------------------------------------ */

static char *prep(const char *src, size_t n) {
    xsb b = {0};
    /* pass 1: $ident -> __ident (re.sub over the whole string) */
    size_t i = 0;
    while (i < n) {
        if (src[i] == '$' && i + 1 < n && is_id_start((unsigned char)src[i + 1])) {
            size_t j = i + 1;
            while (j < n && is_id_char((unsigned char)src[j])) j++;
            if (sb_add(&b, "__", 2) || sb_add(&b, src + i + 1, j - i - 1)) goto fail;
            i = j;
        } else {
            if (sb_ch(&b, src[i])) goto fail;
            i++;
        }
    }
    /* pass 2: "&&" -> " and ", "||" -> " or " (str.replace: non-overlapping,
     * left to right, so "&&&" becomes " and &") */
    xsb b2 = {0};
    for (i = 0; i < b.n;) {
        if (i + 1 < b.n && b.p[i] == '&' && b.p[i + 1] == '&') {
            if (sb_add(&b2, " and ", 5)) { free(b.p); goto fail2; }
            i += 2;
        } else if (i + 1 < b.n && b.p[i] == '|' && b.p[i + 1] == '|') {
            if (sb_add(&b2, " or ", 4)) { free(b.p); goto fail2; }
            i += 2;
        } else {
            if (sb_ch(&b2, b.p[i])) { free(b.p); goto fail2; }
            i++;
        }
    }
    free(b.p);
    /* pass 3: !(?!=) -> " not " */
    xsb b3 = {0};
    for (i = 0; i < b2.n;) {
        if (b2.p[i] == '!' && !(i + 1 < b2.n && b2.p[i + 1] == '=')) {
            if (sb_add(&b3, " not ", 5)) { free(b2.p); goto fail3; }
            i++;
        } else {
            if (sb_ch(&b3, b2.p[i])) { free(b2.p); goto fail3; }
            i++;
        }
    }
    free(b2.p);
    return sb_take(&b3);
fail3:
    if (b3.p) free(b3.p);
    return NULL;
fail2:
    if (b2.p) free(b2.p);
    return NULL;
fail:
    if (b.p) free(b.p);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Python repr() of a str                                              */
/* ------------------------------------------------------------------ */

static char *repr_n(const char *s, size_t n) {
    int has_s = 0, has_d = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\'') has_s = 1;
        else if (s[i] == '"') has_d = 1;
    }
    char q = (has_s && !has_d) ? '"' : '\'';
    xsb b = {0};
    if (sb_ch(&b, q)) goto fail;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char tmp[8];
        if (c == (unsigned char)q || c == '\\') {
            tmp[0] = '\\'; tmp[1] = (char)c; tmp[2] = 0;
            if (sb_add(&b, tmp, 2)) goto fail;
        } else if (c == '\n') {
            if (sb_add(&b, "\\n", 2)) goto fail;
        } else if (c == '\r') {
            if (sb_add(&b, "\\r", 2)) goto fail;
        } else if (c == '\t') {
            if (sb_add(&b, "\\t", 2)) goto fail;
        } else if (c < 32 || c == 127) {
            snprintf(tmp, sizeof tmp, "\\x%02x", c);
            if (sb_add(&b, tmp, 4)) goto fail;
        } else {
            if (sb_ch(&b, (char)c)) goto fail;
        }
    }
    if (sb_ch(&b, q)) goto fail;
    return sb_take(&b);
fail:
    if (b.p) free(b.p);
    return NULL;
}

char *af_py_repr(const char *s) { return repr_n(s, strlen(s)); }
char *af_py_repr_n(const char *s, size_t n) { return repr_n(s, n); }

/* Python str()/repr() of a float: shortest round-trip digits, with ".0"
 * appended when the text carries no exponent or decimal point
 * (str(2.0) == "2.0"). Used by f-string messages in validate.c and by the
 * differential harness (tests/expr_harness.c) to format values. */
void af_py_repr_float(double d, char *buf, size_t sz) {
    buf[0] = 0;
    for (int prec = 15; prec <= 17; prec++) {
        snprintf(buf, sz, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) break;
    }
    if (buf[0] && !strpbrk(buf, ".eEnNiI")) {
        size_t n = strlen(buf);
        if (n + 2 < sz) { buf[n] = '.'; buf[n + 1] = '0'; buf[n + 2] = 0; }
    }
}

/* ------------------------------------------------------------------ */
/* keywords and operators                                              */
/* ------------------------------------------------------------------ */

enum {
    KW_NONE = 0,
    KW_AND, KW_OR, KW_NOT, KW_IN, KW_IS, KW_IF, KW_ELSE, KW_LAMBDA,
    KW_YIELD, KW_FROM, KW_AWAIT, KW_TRUE, KW_FALSE, KW_NIL,
    KW_AS, KW_ASSERT, KW_ASYNC, KW_BREAK, KW_CASE, KW_CLASS, KW_CONTINUE,
    KW_DEF, KW_DEL, KW_ELIF, KW_EXCEPT, KW_FINALLY, KW_FOR, KW_GLOBAL,
    KW_IMPORT, KW_MATCH, KW_NONLOCAL, KW_PASS, KW_RAISE, KW_RETURN,
    KW_TRY, KW_WHILE, KW_WITH
};

static const struct { const char *w; int id; } KWS[] = {
    {"and", KW_AND}, {"as", KW_AS}, {"assert", KW_ASSERT}, {"async", KW_ASYNC},
    {"await", KW_AWAIT}, {"break", KW_BREAK},
    {"class", KW_CLASS}, {"continue", KW_CONTINUE}, {"def", KW_DEF},
    {"del", KW_DEL}, {"elif", KW_ELIF}, {"else", KW_ELSE},
    {"except", KW_EXCEPT}, {"False", KW_FALSE}, {"finally", KW_FINALLY},
    {"for", KW_FOR}, {"from", KW_FROM}, {"global", KW_GLOBAL},
    {"if", KW_IF}, {"import", KW_IMPORT}, {"in", KW_IN}, {"is", KW_IS},
    {"lambda", KW_LAMBDA}, {"None", KW_NIL},
    {"nonlocal", KW_NONLOCAL}, {"not", KW_NOT}, {"or", KW_OR},
    {"pass", KW_PASS}, {"raise", KW_RAISE}, {"return", KW_RETURN},
    {"True", KW_TRUE}, {"try", KW_TRY}, {"while", KW_WHILE},
    {"with", KW_WITH}, {"yield", KW_YIELD},
};

static int kw_lookup(const char *s, size_t n) {
    for (size_t i = 0; i < sizeof KWS / sizeof KWS[0]; i++)
        if (strlen(KWS[i].w) == n && memcmp(KWS[i].w, s, n) == 0)
            return KWS[i].id;
    return KW_NONE;
}

enum {
    OP_NONE = 0,
    OP_ADD, OP_SUB, OP_MULT, OP_DIV, OP_FLOORDIV, OP_MOD, OP_MATMULT,
    OP_POW, OP_LSHIFT, OP_RSHIFT, OP_BITAND, OP_BITOR, OP_BITXOR,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE, OP_WALRUS,
    OP_LPAREN, OP_RPAREN, OP_LBRACK, OP_RBRACK, OP_LBRACE, OP_RBRACE,
    OP_COMMA, OP_DOT, OP_COLON, OP_ASSIGN, OP_TILDE, OP_ELLIPSIS
};

/* ------------------------------------------------------------------ */
/* lexer                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    TK_INT, TK_FLT, TK_CPLX, TK_STR, TK_FSTR, TK_NAME, TK_KW, TK_OP
} tk_kind;

typedef struct {
    tk_kind k;
    int64_t i;
    char *big;          /* decimal text when the int is out of int64 (owned) */
    double d;           /* TK_FLT value, TK_CPLX imaginary */
    char *s;            /* TK_STR decoded value, TK_FSTR raw body (owned) */
    size_t slen;
    int is_bytes;       /* TK_STR came from a bytes literal */
    const char *name;   /* TK_NAME: into the source buffer */
    size_t nlen;
    int kw, op;
} tok;

static void toks_free(tok *t, size_t n) {
    if (!t) return;
    for (size_t i = 0; i < n; i++) {
        free(t[i].big);
        free(t[i].s);
    }
    free(t);
}

/* big-decimal helpers: values out of int64 only ever feed the
 * range-check message, so a decimal digit string is enough. */
static int base_digit_val(unsigned char c, int base);

static char *big_from_digits(const char *d, size_t nd, int base) {
    /* little-endian decimal accumulation: for each source digit,
     * decimal = decimal * base + digit. Leading zeros collapse for free
     * (the accumulator starts at 0), which is exactly how Python
     * prints the int back in the range message. */
    size_t cap = nd * 4 + 16;
    unsigned char *acc = (unsigned char *)calloc(cap, 1);
    if (!acc) return NULL;
    size_t alen = 1; /* acc[0] = 0, least significant first */
    for (size_t i = 0; i < nd; i++) {
        int v = base_digit_val((unsigned char)d[i], base);
        if (v < 0) v = 0;
        unsigned carry = (unsigned)v;
        for (size_t j = 0; j < alen; j++) {
            unsigned x = (unsigned)acc[j] * (unsigned)base + carry;
            acc[j] = (unsigned char)(x % 10);
            carry = x / 10;
        }
        while (carry) {
            if (alen == cap) { free(acc); return NULL; }
            acc[alen++] = (unsigned char)(carry % 10);
            carry /= 10;
        }
    }
    char *out = (char *)malloc(alen + 2);
    if (!out) { free(acc); return NULL; }
    size_t k = 0;
    for (size_t i = alen; i-- > 0;) out[k++] = (char)('0' + acc[i]);
    out[k] = 0;
    free(acc);
    return out;
}

/* accumulate an unsigned integer in `acc`; returns 0 on overflow */
static int u64_mul_add(uint64_t *acc, uint64_t base, uint64_t digit) {
    if (*acc > (UINT64_MAX - digit) / base) return -1;
    *acc = *acc * base + digit;
    return 0;
}

static int base_digit_val(unsigned char c, int base) {
    if (c >= '0' && c <= '9') {
        int v = c - '0';
        return v < base ? v : -1;
    }
    if (base == 16) {
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    }
    return -1;
}

/* scan a number at *pi (caller established a digit or `.`digit start).
 * Returns 0 and fills *t, -1 on a Python SyntaxError-class failure,
 * -2 on out-of-memory. */
static int scan_number(const char *s, size_t n, size_t *pi, tok *t) {
    size_t i = *pi, dstart = i;
    int prefixed = 0, is_cplx = 0, pure_int = 1, nz = 0;
    int base = 10;                  /* digit base for the big-int path */
    uint64_t acc = 0;
    int overflow = 0;
    xsb dig = {0};               /* digit chars, no separators (big ints) */
    int rc = -1;

    memset(t, 0, sizeof *t);

    if (s[i] == '0' && i + 1 < n &&
        (s[i + 1] == 'x' || s[i + 1] == 'X' || s[i + 1] == 'b' || s[i + 1] == 'B' ||
         s[i + 1] == 'o' || s[i + 1] == 'O')) {
        base = (s[i + 1] == 'x' || s[i + 1] == 'X') ? 16
             : (s[i + 1] == 'b' || s[i + 1] == 'B') ? 2 : 8;
        prefixed = 1;
        pure_int = 0;
        i += 2;
        if (i < n && s[i] == '_') {
            /* one underscore allowed right after the prefix */
            if (i + 1 < n && base_digit_val((unsigned char)s[i + 1], base) >= 0) i++;
            else goto out;
        }
        for (;;) {
            int got = 0;
            while (i < n && base_digit_val((unsigned char)s[i], base) >= 0) {
                int v = base_digit_val((unsigned char)s[i], base);
                if (sb_ch(&dig, s[i])) goto oom;
                if (v) nz = 1;
                if (u64_mul_add(&acc, (uint64_t)base, (uint64_t)v)) overflow = 1;
                i++;
                got = 1;
            }
            if (!got) goto out;               /* no digits after the prefix */
            if (i < n && s[i] == '_') {
                if (i + 1 < n && base_digit_val((unsigned char)s[i + 1], base) >= 0) {
                    i++;                      /* underscore between digits */
                } else {
                    goto out;                 /* trailing underscore */
                }
            } else break;
        }
    } else {
        /* decimal int / float / complex */
        if (is_digit((unsigned char)s[i])) {
            for (;;) {
                int got = 0;
                while (i < n && is_digit((unsigned char)s[i])) {
                    if (sb_ch(&dig, s[i])) goto oom;
                    if (s[i] != '0') nz = 1;
                    if (u64_mul_add(&acc, 10, (uint64_t)(s[i] - '0'))) overflow = 1;
                    i++;
                    got = 1;
                }
                if (!got) goto out;
                if (i < n && s[i] == '_') {
                    if (i + 1 < n && is_digit((unsigned char)s[i + 1])) i++;
                    else goto out;            /* trailing/stray underscore */
                } else break;
            }
        }
        /* fraction (`5.` is valid with no fraction digits) */
        if (i < n && s[i] == '.') {
            pure_int = 0;
            i++;
            if (i < n && s[i] == '_') goto out;
            for (;;) {
                while (i < n && is_digit((unsigned char)s[i])) {
                    if (sb_ch(&dig, s[i])) goto oom;
                    if (s[i] != '0') nz = 1;
                    i++;
                }
                if (i < n && s[i] == '_') {
                    if (i + 1 < n && is_digit((unsigned char)s[i + 1])) { i++; continue; }
                    goto out;
                }
                break;
            }
        }
        /* exponent (only when it actually forms one) */
        if (i < n && (s[i] == 'e' || s[i] == 'E')) {
            size_t j = i + 1;
            if (j < n && (s[j] == '+' || s[j] == '-')) j++;
            if (j < n && is_digit((unsigned char)s[j])) {
                pure_int = 0;
                i = j;
                for (;;) {
                    while (i < n && is_digit((unsigned char)s[i])) i++;
                    if (i < n && s[i] == '_') {
                        if (i + 1 < n && is_digit((unsigned char)s[i + 1])) { i++; continue; }
                        goto out;
                    }
                    break;
                }
            }
            /* else: leave `e` for the letter-run check below */
        }
        /* imaginary suffix */
        if (i < n && (s[i] == 'j' || s[i] == 'J')) {
            pure_int = 0;
            is_cplx = 1;
            i++;
        }
        /* leading-zeros rule: pure decimal integers only, and only when a
         * non-zero digit follows the leading zero (`00`, `0_0` ok, `0755`
         * not); floats and complex are exempt (`0755j` ok) */
        if (pure_int && nz && s[dstart] == '0') goto out;
    }

    /* a letter run right after the number must be exactly a keyword:
     * `1and 2`, `1if 2 else 3`, `0o7and 2` split; `1and2`, `1abc`,
     * `0x1and 2` (the hex scanner ate the `a`) are errors. The keyword
     * itself is left for the main loop. */
    if (i < n && is_id_start((unsigned char)s[i])) {
        size_t j = i;
        while (j < n && is_id_char((unsigned char)s[j])) j++;
        if (kw_lookup(s + i, j - i) == KW_NONE) goto out;
    }
    *pi = i;

    if (is_cplx) {
        /* value is never read (a complex constant is `unsupported
         * expression` in the oracle) but the literal must parse */
        char *buf = (char *)malloc(i - dstart + 1);
        if (!buf) goto oom;
        size_t bn = 0;
        for (size_t q = dstart; q < i; q++)
            if (s[q] != '_') buf[bn++] = s[q];
        buf[bn] = 0;
        t->k = TK_CPLX;
        t->d = strtod(buf, NULL);
        free(buf);
    } else if ((overflow || acc > (uint64_t)INT64_MAX) && (prefixed || pure_int)) {
        /* a past-int64 INTEGER keeps its digits so the range message can
         * print Python's exact value (2^63 and up would otherwise read
         * back as a negative int64); a float literal with many digits
         * (`12345678901234567890123.0`) stays a float */
        t->k = TK_INT;
        t->big = big_from_digits(dig.p, dig.n, base);
        if (!t->big) goto oom;
        t->i = 0;
    } else if (prefixed || pure_int) {
        t->k = TK_INT;
        t->i = (int64_t)acc;
    } else {
        /* float: rebuild the source span without underscores (C strtod
         * matches Python's float parsing: `1e400` -> inf) */
        char *buf = (char *)malloc(i - dstart + 1);
        if (!buf) goto oom;
        size_t bn = 0;
        for (size_t q = dstart; q < i; q++)
            if (s[q] != '_') buf[bn++] = s[q];
        buf[bn] = 0;
        t->k = TK_FLT;
        t->d = strtod(buf, NULL);
        free(buf);
    }
    rc = 0;
    goto out;
oom:
    rc = -2;
out:
    if (dig.p) free(dig.p);
    return rc;
}

/* string prefixes */
static int str_prefix(const char *run, size_t m, int *raw, int *isb, int *isf) {
    *raw = *isb = *isf = 0;
    if (m == 1) {
        char c = run[0];
        if (c == 'r') *raw = 1;
        else if (c == 'R') *raw = 1;
        else if (c == 'b' || c == 'B') *isb = 1;
        else if (c == 'f' || c == 'F') *isf = 1;
        else if (c == 'u' || c == 'U') { /* unicode: plain */ }
        else return 0;
        return 1;
    }
    if (m == 2) {
        char a = run[0], b = run[1];
        int ra = (a == 'r' || a == 'R'), bb = (b == 'b' || b == 'B');
        int ff = (b == 'f' || b == 'F');
        int bf = (a == 'f' || a == 'F'), rf = (b == 'r' || b == 'R');
        if (ra && (bb || ff)) {
            *raw = 1;
            if (bb) *isb = 1; else *isf = 1;
            return 1;
        }
        if (bb && rf) { *isb = 1; *raw = 1; return 1; }
        if (bf && rf) { *isf = 1; *raw = 1; return 1; }
        return 0;
    }
    return 0;
}

/* decode a non-raw string body; returns 0, -1 (Python SyntaxError), -2 (oom) */
static int decode_body(const char *s, size_t n, int is_bytes, char **out, size_t *outn) {
    xsb b = {0};
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if (c != '\\') {
            if (sb_ch(&b, c)) goto fail;
            i++;
            continue;
        }
        if (i + 1 >= n) { /* trailing backslash: can't happen (lexer ended the literal) */
            if (sb_ch(&b, c)) goto fail;
            i++;
            continue;
        }
        char e = s[i + 1];
        switch (e) {
        case '\n':
            i += 2; break;                  /* line continuation: dropped */
        case 'n': if (sb_ch(&b, '\n')) goto fail; i += 2; break;
        case 'r': if (sb_ch(&b, '\r')) goto fail; i += 2; break;
        case 't': if (sb_ch(&b, '\t')) goto fail; i += 2; break;
        case 'a': if (sb_ch(&b, 7)) goto fail; i += 2; break;
        case 'b': if (sb_ch(&b, 8)) goto fail; i += 2; break;
        case 'f': if (sb_ch(&b, 12)) goto fail; i += 2; break;
        case 'v': if (sb_ch(&b, 11)) goto fail; i += 2; break;
        case '\\': case '\'': case '"':
            if (sb_ch(&b, e)) goto fail;
            i += 2;
            break;
        case 'x': {
            if (i + 3 >= n) return -1;
            int v = 0;
            for (int k = 0; k < 2; k++) {
                unsigned char h = (unsigned char)s[i + 2 + k];
                int d;
                if (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                else return -1;
                v = v * 16 + d;
            }
            if (sb_ch(&b, (char)v)) goto fail;
            i += 4;
            break;
        }
        case 'u': case 'U': {
            int want = e == 'u' ? 4 : 8;
            if (i + 2 + (size_t)want >= n) return -1;
            unsigned v = 0;
            for (int k = 0; k < want; k++) {
                unsigned char h = (unsigned char)s[i + 2 + k];
                int d;
                if (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                else return -1;
                v = v * 16 + (unsigned)d;
            }
            /* UTF-8 encode */
            if (v < 0x80) {
                if (sb_ch(&b, (char)v)) goto fail;
            } else if (v < 0x800) {
                if (sb_ch(&b, (char)(0xC0 | (v >> 6)))) goto fail;
                if (sb_ch(&b, (char)(0x80 | (v & 0x3F)))) goto fail;
            } else if (v < 0x10000) {
                if (sb_ch(&b, (char)(0xE0 | (v >> 12)))) goto fail;
                if (sb_ch(&b, (char)(0x80 | ((v >> 6) & 0x3F)))) goto fail;
                if (sb_ch(&b, (char)(0x80 | (v & 0x3F)))) goto fail;
            } else {
                if (sb_ch(&b, (char)(0xF0 | (v >> 18)))) goto fail;
                if (sb_ch(&b, (char)(0x80 | ((v >> 12) & 0x3F)))) goto fail;
                if (sb_ch(&b, (char)(0x80 | ((v >> 6) & 0x3F)))) goto fail;
                if (sb_ch(&b, (char)(0x80 | (v & 0x3F)))) goto fail;
            }
            i += 2 + (size_t)want;
            break;
        }
        case 'N': {
            if (is_bytes) return -1;
            /* \N{name}: names are NOT resolved (documented divergence);
             * an unknown name would be a Python SyntaxError, a known one
             * the character. Keep the raw escape text. */
            if (i + 3 >= n || s[i + 2] != '{') return -1;
            size_t j = i + 3;
            while (j < n && s[j] != '}') j++;
            if (j >= n) return -1;
            if (sb_add(&b, s + i, j + 1 - i)) goto fail;
            i = j + 1;
            break;
        }
        default:
            if (e >= '0' && e <= '7') {
                unsigned v = 0;
                size_t k = 0;
                while (k < 3 && i + 1 + k < n && s[i + 1 + k] >= '0' && s[i + 1 + k] <= '7') {
                    v = v * 8 + (unsigned)(s[i + 1 + k] - '0');
                    k++;
                }
                if (v > 255) return -1;
                if (sb_ch(&b, (char)v)) goto fail;
                i += 1 + k;
            } else {
                /* unknown escape: keep backslash and the character
                 * (SyntaxWarning on the oracle's stderr -- ignored) */
                if (sb_ch(&b, '\\')) goto fail;
                if (sb_ch(&b, e)) goto fail;
                i += 2;
            }
            break;
        }
    }
    size_t nout = b.n;                        /* sb_take resets b.n */
    *out = sb_take(&b);
    if (!*out) goto fail;
    *outn = nout;
    return 0;
fail:
    if (b.p) free(b.p);
    return -2;
}

/* scan a string literal at *pi (prefix already consumed into `pref`).
 * Returns 0, -1 syntax, -2 oom. For f-strings the raw body is stored. */
static int scan_string(const char *s, size_t n, size_t *pi, int raw, int is_bytes,
                       int is_f, tok *t) {
    size_t i = *pi;
    char q = s[i];
    int triple = (i + 2 < n && s[i + 1] == q && s[i + 2] == q);
    size_t body0 = i + (triple ? 3 : 1);
    size_t j = body0;
    int in_expr = 0;
    size_t expr_depth = 0;

    memset(t, 0, sizeof *t);

    while (j < n) {
        char c = s[j];
        if (is_f) {
            if (in_expr) {
                if (c == '\\') { j += 2; continue; }
                if (c == '\'' || c == '"') {
                    /* skip the nested string */
                    char qq = c;
                    int tt = (j + 2 < n && s[j + 1] == qq && s[j + 2] == qq);
                    size_t k = j + (tt ? 3 : 1);
                    while (k < n) {
                        if (s[k] == '\\') { k += 2; continue; }
                        if (s[k] == qq) break;
                        if (!triple && (s[k] == '\n' || s[k] == '\r')) break;
                        k++;
                    }
                    if (k >= n) return -1;
                    j = k + 1;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{') expr_depth++;
                else if (c == ')' || c == ']' || c == '}') {
                    if (c == '}' && expr_depth == 0) { in_expr = 0; j++; continue; }
                    if (expr_depth == 0) return -1;
                    expr_depth--;
                } else if (c == '=' && expr_depth == 0 && j + 1 < n && s[j + 1] != '=' &&
                           !(j > 0 && (s[j - 1] == '<' || s[j - 1] == '>' ||
                                       s[j - 1] == '!' || s[j - 1] == '='))) {
                    /* debug `=` inside the replacement field: keep scanning */
                } else if (c == '\n' && !triple) {
                    return -1;
                }
                j++;
                continue;
            }
            if (c == '{') {
                if (j + 1 < n && s[j + 1] == '{') { j += 2; continue; }
                in_expr = 1;
                expr_depth = 0;
                j++;
                continue;
            }
            if (c == '}') {
                if (j + 1 < n && s[j + 1] == '}') { j += 2; continue; }
                return -1;                  /* single '}' not allowed */
            }
        }
        if (c == '\\') {
            if (!raw) {
                if (j + 1 >= n) return -1;
                if (s[j + 1] == '\n') { j += 2; continue; }
                if (s[j + 1] == '\r') { j += (j + 2 < n && s[j + 2] == '\n') ? 3 : 2; continue; }
            } else {
                /* raw: a backslash escapes the quote for termination only */
                if (j + 1 >= n) return -1;
                if (s[j + 1] == q || (triple && s[j + 1] == q)) { j += 2; continue; }
                j += 2;
                continue;
            }
            j += 2;
            continue;
        }
        if (c == q) {
            if (triple) {
                if (j + 2 < n && s[j + 1] == q && s[j + 2] == q) { j += 3; goto done; }
                j++;
                continue;
            }
            j++;
            goto done;
        }
        if (!triple && (c == '\n' || c == '\r')) return -1;
        j++;
        continue;
    }
    return -1;                              /* unterminated */
done:
    if (is_f) {
        t->k = TK_FSTR;
        t->s = (char *)xmalloc(j - body0 + 1);
        if (!t->s) return -2;
        memcpy(t->s, s + body0, j - body0 - (triple ? 3 : 1));
        t->slen = j - body0 - (triple ? 3 : 1);
        t->s[t->slen] = 0;
    } else if (raw) {
        t->k = TK_STR;
        t->s = (char *)xmalloc(j - body0 + 1);
        if (!t->s) return -2;
        size_t k = 0;
        for (size_t q2 = body0; q2 < j - (triple ? 3 : 1); q2++) {
            /* raw: keep everything, but `\'` stays as two chars */
            t->s[k++] = s[q2];
        }
        t->s[k] = 0;
        t->slen = k;
        t->is_bytes = is_bytes;
    } else {
        size_t blen = j - body0 - (triple ? 3 : 1);
        int r = decode_body(s + body0, blen, is_bytes, &t->s, &t->slen);
        if (r == -1) return -1;
        if (r != 0) return -2;
        t->k = TK_STR;
        t->is_bytes = is_bytes;
    }
    *pi = j;
    return 0;
}

/* walk an f-string body, invoking cb for each replacement expression.
 * Returns 0, -1 (structure error), or the callback's negative result. */
static int walk_fstring(const char *s, size_t n,
                        int (*cb)(const char *, size_t, void *), void *ud) {
    size_t i = 0;
    while (i < n) {
        if (s[i] == '{') {
            if (i + 1 < n && s[i + 1] == '{') { i += 2; continue; }
            size_t j = i + 1;
            size_t depth = 1;
            while (j < n) {
                char c = s[j];
                if (c == '\'' || c == '"') {
                    char qq = c;
                    int tt = (j + 2 < n && s[j + 1] == qq && s[j + 2] == qq);
                    size_t k = j + (tt ? 3 : 1);
                    while (k < n) {
                        if (s[k] == '\\') { k += 2; continue; }
                        if (s[k] == qq) break;
                        if (s[k] == '\n' && !tt) break;
                        k++;
                    }
                    if (k >= n) return -1;
                    j = k + 1;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}') {
                    depth--;
                    if (depth == 0) break;
                }
                j++;
            }
            if (j >= n || s[j] != '}') return -1;
            /* Split the span into expression / format-spec. Post-prep a
             * `!conv` can never appear (`_prep` rewrites `!` to " not ",
             * which then fails to parse), so only `:` and the debug `=`
             * end the expression -- at depth 0, with CPython's own naive
             * scan (`f'{x:=1}'` splits at the colon; a walrus needs
             * parens). */
            size_t e0 = i + 1, e1 = j;
            int dbg = 0;                /* a debug `=` ended the expression */
            size_t d = 0;
            for (size_t k = e0; k < e1; k++) {
                char c = s[k];
                if (c == '\'' || c == '"') {
                    char qq = c;
                    size_t m = k + 1;
                    while (m < e1) {
                        if (s[m] == '\\') { m += 2; continue; }
                        if (s[m] == qq) break;
                        m++;
                    }
                    k = m;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{') d++;
                else if (c == ')' || c == ']' || c == '}') { if (d) d--; }
                else if (d == 0 && c == ':' && k > e0) { e1 = k; break; }
                else if (d == 0 && c == '=' && k > e0 &&
                         (k + 1 >= e1 || s[k + 1] != '=') &&
                         s[k - 1] != '<' && s[k - 1] != '>' &&
                         s[k - 1] != '!' && s[k - 1] != '=') {
                    e1 = k; dbg = 1; break;  /* debug `=` ends the expression */
                }
            }
            if (e1 == e0) return -1;         /* empty expression */
            /* after the expression: whitespace, then an optional spec.
             * After a debug `=` the `=` itself is skipped first. */
            size_t rest = dbg ? e1 + 1 : e1;
            while (rest < j && is_sp((unsigned char)s[rest])) rest++;
            if (rest < j && s[rest] == ':') {
                /* format spec: walk nested {expr} */
                size_t k = rest + 1;
                while (k < j) {
                    if (s[k] == '{') {
                        size_t m = k + 1, d2 = 1;
                        while (m < j) {
                            if (s[m] == '(' || s[m] == '[' || s[m] == '{') d2++;
                            else if (s[m] == ')' || s[m] == ']' || s[m] == '}') {
                                d2--;
                                if (d2 == 0) break;
                            }
                            m++;
                        }
                        if (m >= j) return -1;
                        int rc = cb(s + k + 1, m - (k + 1), ud);
                        if (rc) return rc;
                        k = m + 1;
                        continue;
                    }
                    k++;
                }
            } else if (rest != j) {
                return -1;                  /* trailing garbage in the field */
            }
            {
                int rc = cb(s + e0, e1 - e0, ud);
                if (rc) return rc;
            }
            i = j + 1;
            continue;
        }
        if (s[i] == '}') {
            if (i + 1 < n && s[i + 1] == '}') { i += 2; continue; }
            return -1;                      /* single '}' not allowed */
        }
        i++;
    }
    return 0;
}

/* lex the whole (prepped) source. Returns 0, -1 syntax, -2 crash/oom. */
static int lex(const char *s, size_t n, tok **out, size_t *outn) {
    tok *toks = NULL;
    size_t nt = 0, cap = 0;
    size_t i = 0;
    int rc = 0;
    size_t depth = 0;
    int ended = 0;

    /* phase 1: skip blank/comment lines; the first content line must
     * not be indented. A formfeed resets the indent column to 0 (the
     * tokenizer treats it as a line boundary), so ` \f1` is legal. */
    for (;;) {
        size_t j = i;
        size_t col = 0;
        while (j < n && is_sp((unsigned char)s[j])) {
            if (s[j] == '\x0c') col = 0;
            else col++;
            j++;
        }
        if (j < n && s[j] == '#') {
            while (j < n && !is_nl((unsigned char)s[j])) j++;
        }
        if (j >= n) { i = n; break; }             /* only blanks remain */
        if (is_nl((unsigned char)s[j])) {
            if (s[j] == '\r' && j + 1 < n && s[j + 1] == '\n') j += 2;
            else j++;
            i = j;
            continue;
        }
        if (col > 0) { rc = -1; goto out; }       /* unexpected indent */
        break;                                   /* first content line */
    }

#define PUSH() do { \
        if (nt == cap) { \
            size_t nc = cap ? cap * 2 : 32; \
            tok *p = (tok *)realloc(toks, nc * sizeof *p); \
            if (!p) { rc = -2; goto out; } \
            toks = p; cap = nc; \
        } \
    } while (0)

    while (!ended) {
        /* whitespace and comments */
        for (;;) {
            if (i < n && is_sp((unsigned char)s[i])) { i++; continue; }
            if (i < n && s[i] == '#') {
                while (i < n && !is_nl((unsigned char)s[i])) i++;
                continue;
            }
            break;
        }
        if (i >= n) break;
        unsigned char c = (unsigned char)s[i];
        if (is_nl(c)) {
            if (depth > 0) {
                if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') i += 2;
                else i++;
                continue;
            }
            /* the expression ends here: only blank/comment lines after */
            if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') i += 2;
            else i++;
            while (i < n) {
                size_t j = i;
                while (j < n && is_sp((unsigned char)s[j])) j++;
                if (j < n && s[j] == '#') {
                    while (j < n && !is_nl((unsigned char)s[j])) j++;
                }
                if (j >= n) { i = n; break; }
                if (is_nl((unsigned char)s[j])) {
                    if (s[j] == '\r' && j + 1 < n && s[j + 1] == '\n') j += 2;
                    else j++;
                    i = j;
                    continue;
                }
                rc = -1;                        /* content after the expression */
                goto out;
            }
            ended = 1;
            break;
        }
        if (c < 0x20 || c == 0x7f || c >= 0x80) { rc = -1; goto out; }

        if (is_digit(c) || (c == '.' && i + 1 < n && is_digit((unsigned char)s[i + 1]))) {
            PUSH();
            memset(&toks[nt], 0, sizeof toks[0]);
            size_t j = i;
            int r = scan_number(s, n, &j, &toks[nt]);
            if (r != 0) { rc = r == -2 ? -2 : -1; goto out; }
            nt++;
            i = j;
            continue;
        }

        if (is_id_start(c)) {
            /* possible string prefix */
            size_t j = i;
            while (j < n && is_alpha((unsigned char)s[j])) j++;
            if (j < n && (s[j] == '\'' || s[j] == '"')) {
                int raw, isb, isf;
                if (str_prefix(s + i, j - i, &raw, &isb, &isf)) {
                    PUSH();
                    memset(&toks[nt], 0, sizeof toks[0]);
                    size_t k = j;
                    int r = scan_string(s, n, &k, raw, isb, isf, &toks[nt]);
                    if (r != 0) { rc = r == -2 ? -2 : -1; goto out; }
                    nt++;
                    i = k;
                    continue;
                }
            }
            /* identifier or keyword */
            size_t k = i;
            while (k < n && is_id_char((unsigned char)s[k])) k++;
            int kw = kw_lookup(s + i, k - i);
            PUSH();
            memset(&toks[nt], 0, sizeof toks[0]);
            if (kw != KW_NONE) {
                toks[nt].k = TK_KW;
                toks[nt].kw = kw;
            } else {
                toks[nt].k = TK_NAME;
                toks[nt].name = s + i;
                toks[nt].nlen = k - i;
            }
            nt++;
            i = k;
            continue;
        }

        /* string without a prefix */
        if (c == '\'' || c == '"') {
            PUSH();
            memset(&toks[nt], 0, sizeof toks[0]);
            size_t k = i;
            int r = scan_string(s, n, &k, 0, 0, 0, &toks[nt]);
            if (r != 0) { rc = r == -2 ? -2 : -1; goto out; }
            nt++;
            i = k;
            continue;
        }

        /* operators */
        {
            int op = OP_NONE;
            size_t adv = 1;
            char a = s[i], b = i + 1 < n ? s[i + 1] : 0, cc = i + 2 < n ? s[i + 2] : 0;
            if (a == '=' && b == '=') { op = OP_EQ; adv = 2; }
            else if (a == '!' && b == '=') { op = OP_NE; adv = 2; }
            else if (a == '<' && b == '=') { op = OP_LE; adv = 2; }
            else if (a == '>' && b == '=') { op = OP_GE; adv = 2; }
            else if (a == ':' && b == '=') { op = OP_WALRUS; adv = 2; }
            else if (a == '*' && b == '*') { op = OP_POW; adv = 2; }
            else if (a == '/' && b == '/') { op = OP_FLOORDIV; adv = 2; }
            else if (a == '<' && b == '<') { op = OP_LSHIFT; adv = 2; }
            else if (a == '>' && b == '>') { op = OP_RSHIFT; adv = 2; }
            else if (a == '.' && b == '.' && cc == '.') { op = OP_ELLIPSIS; adv = 3; }
            else if (a == '+') op = OP_ADD;
            else if (a == '-') op = OP_SUB;
            else if (a == '*') op = OP_MULT;
            else if (a == '/') op = OP_DIV;
            else if (a == '%') op = OP_MOD;
            else if (a == '@') op = OP_MATMULT;
            else if (a == '&') op = OP_BITAND;
            else if (a == '|') op = OP_BITOR;
            else if (a == '^') op = OP_BITXOR;
            else if (a == '~') op = OP_TILDE;
            else if (a == '<') op = OP_LT;
            else if (a == '>') op = OP_GT;
            else if (a == '(') { op = OP_LPAREN; depth++; }
            else if (a == ')') { op = OP_RPAREN; if (depth) depth--; }
            else if (a == '[') { op = OP_LBRACK; depth++; }
            else if (a == ']') { op = OP_RBRACK; if (depth) depth--; }
            else if (a == '{') { op = OP_LBRACE; depth++; }
            else if (a == '}') { op = OP_RBRACE; if (depth) depth--; }
            else if (a == ',') op = OP_COMMA;
            else if (a == '.') op = OP_DOT;
            else if (a == ':') op = OP_COLON;
            else if (a == '=') op = OP_ASSIGN;
            if (op == OP_NONE) { rc = -1; goto out; }
            /* CPython's tokenizer refuses more than 200 open brackets of
             * nesting ("too many nested parentheses") */
            if (depth > AF_LEX_BRACKET_CAP) { rc = -1; goto out; }
            PUSH();
            memset(&toks[nt], 0, sizeof toks[0]);
            toks[nt].k = TK_OP;
            toks[nt].op = op;
            nt++;
            i += adv;
        }
    }
#undef PUSH
    *out = toks;
    *outn = nt;
    return 0;
out:
    toks_free(toks, nt);
    *out = NULL;
    *outn = 0;
    return rc;
}

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum {
    N_CONST, N_NAME, N_USUB, N_UADD, N_NOT, N_INVERT,
    N_ADD, N_SUB, N_MULT, N_DIV, N_FLOORDIV, N_MOD, N_POW, N_MATMULT,
    N_LSHIFT, N_RSHIFT, N_BITAND, N_BITOR, N_BITXOR,
    N_AND, N_OR, N_CMP, N_CALL, N_OPAQUE
};

enum { C_INT, C_FLT, C_STR, C_BOOL, C_BYTES, C_CPLX, C_ELL };

enum {
    CMP_EQ, CMP_NE, CMP_LT, CMP_LE, CMP_GT, CMP_GE,
    CMP_IS, CMP_ISNOT, CMP_IN, CMP_NOTIN
};

typedef struct enode enode;
struct enode {
    unsigned char k, sub;
    enode *a, *b;
    enode **v;
    size_t nv;
    unsigned char *ops;
    size_t nops;
    int64_t i;
    char *big;
    double d;
    char *s;
    size_t slen;
};

static void node_free(enode *n) {
    if (!n) return;
    free(n->big);
    free(n->s);
    for (size_t i = 0; i < n->nv; i++) node_free(n->v[i]);
    free(n->v);
    free(n->ops);
    node_free(n->a);
    node_free(n->b);
    free(n);
}

static enode *node_new(unsigned char k) {
    enode *n = (enode *)xmalloc(sizeof *n);
    if (!n) return NULL;
    memset(n, 0, sizeof *n);
    n->k = k;
    return n;
}

static int node_push(enode *n, enode *c) {
    if (!c) return -1;
    enode **p = (enode **)realloc(n->v, (n->nv + 1) * sizeof *p);
    if (!p) return -1;
    n->v = p;
    n->v[n->nv++] = c;
    return 0;
}

/* ------------------------------------------------------------------ */
/* the oracle's FUNCS keys: a call is only ever a Call node when the
 * callee is a plain name in this set and no keyword argument is present
 * (arcforge_ref.py::_ev); everything else parses but never evaluates. */
static const char *const FUNC_NAMES[] = {
    "floor", "ceil", "round", "min", "max", "abs", "clamp",
    "odd", "even", "half"
};
static int is_func_name(const char *s) {
    for (size_t i = 0; i < sizeof FUNC_NAMES / sizeof FUNC_NAMES[0]; i++)
        if (strcmp(FUNC_NAMES[i], s) == 0) return 1;
    return 0;
}

/* parser                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const tok *t;
    size_t nt, i;
    int fail;      /* Python SyntaxError -> `syntax error in expression` */
    int crash;     /* oom / recursion -> exit 1, no stdout */
    int depth;     /* bracketed nesting (AF_PARSE_DEPTH -> syntax error) */
    int raw;       /* unary/not chains (AF_FRAME_LIMIT -> crash) */
} par;

static const tok *at(par *p, size_t k) {
    return p->i + k < p->nt ? &p->t[p->i + k] : NULL;
}
static int is_op(const tok *t, int op) { return t && t->k == TK_OP && t->op == op; }
static int is_kw(const tok *t, int kw) { return t && t->k == TK_KW && t->kw == kw; }

static enode *p_expr(par *p);
static enode *p_named(par *p);
static enode *p_factor(par *p);
static int af_expr_check_fstring(par *p, const char *s, size_t n);

static void p_fail(par *p) { p->fail = 1; }

static enode *p_or(par *p);

static enode *p_lambda(par *p) {
    /* lambda [varargslist] ':' expression */
    p->i++; /* lambda */
    int seen_default = 0, seen_star = 0, seen_dstar = 0, seen_slash = 0;
    int count = 0;
    for (;;) {
        const tok *t = at(p, 0);
        if (is_op(t, OP_COLON)) break;
        if (!t || p->fail) { p_fail(p); return NULL; }
        if (is_op(t, OP_COMMA)) { p->i++; continue; }
        if (is_op(t, OP_ASSIGN)) { p_fail(p); return NULL; }
        if (is_op(t, OP_MULT)) {
            if (seen_star || seen_dstar || seen_slash) { p_fail(p); return NULL; }
            p->i++;
            const tok *nm = at(p, 0);
            if (!nm || nm->k != TK_NAME) { p_fail(p); return NULL; }
            p->i++;
            seen_star = 1;
            count++;
            if (is_op(at(p, 0), OP_ASSIGN)) { p->i++; enode *d = p_expr(p); if (!d) return NULL; node_free(d); seen_default = 1; }
            continue;
        }
        if (is_op(t, OP_POW)) {
            if (seen_dstar || seen_slash) { p_fail(p); return NULL; }
            p->i++;
            const tok *nm = at(p, 0);
            if (!nm || nm->k != TK_NAME) { p_fail(p); return NULL; }
            p->i++;
            seen_dstar = 1;
            count++;
            if (is_op(at(p, 0), OP_ASSIGN)) { p->i++; enode *d = p_expr(p); if (!d) return NULL; node_free(d); seen_default = 1; }
            continue;
        }
        if (t->k == TK_OP && t->op == OP_DOT && 0) { p_fail(p); return NULL; }
        if (t->k == TK_NAME) {
            if (seen_dstar) { p_fail(p); return NULL; }
            p->i++;
            count++;
            if (is_op(at(p, 0), OP_ASSIGN)) {
                p->i++;
                enode *d = p_expr(p);
                if (!d) return NULL;
                node_free(d);
                seen_default = 1;
            } else if (seen_default) {
                p_fail(p); return NULL; /* non-default after default */
            }
            continue;
        }
        p_fail(p);
        return NULL;
    }
    (void)count;
    if (!is_op(at(p, 0), OP_COLON)) { p_fail(p); return NULL; }
    p->i++; /* : */
    enode *body = p_expr(p);
    if (!body) return NULL;
    enode *n = node_new(N_OPAQUE);
    if (!n) { node_free(body); p->crash = 1; return NULL; }
    n->a = body;
    return n;
}

static int p_comp_tail(par *p) {
    /* ('for' targets 'in' or_test [if or_test]*)+ */
    int rounds = 0;
    while (is_kw(at(p, 0), KW_FOR)) {
        if (++rounds > 64) { p->crash = 1; return -1; }
        p->i++; /* for */
        /* target: NAME (',' NAME)* | '(' NAME (',' NAME)* ')' */
        const tok *t = at(p, 0);
        int paren = 0;
        if (is_op(t, OP_LPAREN)) { paren = 1; p->i++; t = at(p, 0); }
        if (!t || t->k != TK_NAME) { p_fail(p); return -1; }
        p->i++;
        while (is_op(at(p, 0), OP_COMMA)) {
            p->i++;
            const tok *nm = at(p, 0);
            if (!nm || nm->k != TK_NAME) { p_fail(p); return -1; }
            p->i++;
        }
        if (paren) {
            if (!is_op(at(p, 0), OP_RPAREN)) { p_fail(p); return -1; }
            p->i++;
        }
        if (!is_kw(at(p, 0), KW_IN)) { p_fail(p); return -1; }
        p->i++;
        enode *it = p_or(p);
        if (!it) return -1;
        node_free(it);
        while (is_kw(at(p, 0), KW_IF)) {
            p->i++;
            enode *c = p_or(p);
            if (!c) return -1;
            node_free(c);
        }
    }
    return 0;
}

static enode *p_atom(par *p) {
    if (p->crash || p->fail) return NULL;
    if (++p->depth > AF_PARSE_DEPTH) { p_fail(p); p->depth--; return NULL; }
    enode *r = NULL;
    const tok *t = at(p, 0);

    if (!t) { p_fail(p); goto out; }

    if (t->k == TK_INT || t->k == TK_FLT) {
        enode *n = node_new(N_CONST);
        if (!n) { p->crash = 1; goto out; }
        if (t->k == TK_INT) {
            n->sub = C_INT;
            n->i = t->i;
            if (t->big) {
                n->big = xstrdup(t->big);
                if (!n->big) { node_free(n); p->crash = 1; goto out; }
            }
        } else {
            n->sub = C_FLT;
            n->d = t->d;
        }
        p->i++;
        r = n;
        goto out;
    }
    if (t->k == TK_CPLX) {
        enode *n = node_new(N_CONST);
        if (!n) { p->crash = 1; goto out; }
        n->sub = C_CPLX;
        n->d = t->d;
        p->i++;
        r = n;
        goto out;
    }
    if (t->k == TK_STR || t->k == TK_FSTR) {
        /* implicit concatenation of adjacent literals */
        int any_f = 0, any_b = 0, any_s = 0;
        xsb b = {0};
        size_t save = p->i;
        while (p->i < p->nt && (p->t[p->i].k == TK_STR || p->t[p->i].k == TK_FSTR)) {
            const tok *u = &p->t[p->i];
            if (u->k == TK_FSTR) { any_f = 1; p->i++; continue; }
            if (u->is_bytes) any_b = 1; else any_s = 1;
            if (any_b && any_s) { p_fail(p); if (b.p) free(b.p); goto out; }
            if (sb_add(&b, u->s, u->slen)) { p->crash = 1; if (b.p) free(b.p); goto out; }
            p->i++;
        }
        if (any_b && any_f) { p_fail(p); if (b.p) free(b.p); goto out; }
        if (any_f) {
            /* validate each f-string's replacement fields, then the whole
             * concatenation is a JoinedStr -> `unsupported expression` */
            for (size_t k = save; k < p->i; k++) {
                if (p->t[k].k == TK_FSTR &&
                    af_expr_check_fstring(p, p->t[k].s, p->t[k].slen)) {
                    if (b.p) free(b.p);
                    goto out;               /* fail or crash already set */
                }
            }
            enode *n = node_new(N_OPAQUE);
            if (!n) { p->crash = 1; if (b.p) free(b.p); goto out; }
            if (b.p) free(b.p);
            r = n;
            goto out;
        }
        {
            enode *n = node_new(N_CONST);
            size_t blen = b.n;
            if (!n) { p->crash = 1; if (b.p) free(b.p); goto out; }
            if (any_b) n->sub = C_BYTES;
            else n->sub = C_STR;
            n->s = sb_take(&b);
            if (!n->s) { node_free(n); p->crash = 1; goto out; }
            n->slen = blen;
            r = n;
            goto out;
        }
    }
    if (t->k == TK_NAME) {
        enode *n = node_new(N_NAME);
        if (!n) { p->crash = 1; goto out; }
        n->s = (char *)xmalloc(t->nlen + 1);
        if (!n->s) { node_free(n); p->crash = 1; goto out; }
        memcpy(n->s, t->name, t->nlen);
        n->s[t->nlen] = 0;
        n->slen = t->nlen;
        p->i++;
        r = n;
        goto out;
    }
    if (is_kw(t, KW_TRUE) || is_kw(t, KW_FALSE)) {
        enode *n = node_new(N_CONST);
        if (!n) { p->crash = 1; goto out; }
        n->sub = C_BOOL;
        n->i = is_kw(t, KW_TRUE) ? 1 : 0;
        p->i++;
        r = n;
        goto out;
    }
    if (is_op(t, OP_ELLIPSIS)) {
        enode *n = node_new(N_CONST);
        if (!n) { p->crash = 1; goto out; }
        n->sub = C_ELL;
        p->i++;
        r = n;
        goto out;
    }
    if (is_op(t, OP_LPAREN)) {
        p->i++; /* ( */
        /* yield is legal only here */
        if (is_kw(at(p, 0), KW_YIELD)) {
            p->i++;
            enode *y = node_new(N_OPAQUE);
            if (!y) { p->crash = 1; goto out; }
            if (is_kw(at(p, 0), KW_FROM)) {
                p->i++;
                enode *e = p_expr(p);
                if (!e) { node_free(y); goto out; }
                y->a = e;
            } else if (!is_op(at(p, 0), OP_RPAREN)) {
                enode *e = p_expr(p);
                if (!e) { node_free(y); goto out; }
                y->a = e;
            }
            if (!is_op(at(p, 0), OP_RPAREN)) { node_free(y); p_fail(p); goto out; }
            p->i++;
            r = y;
            goto out;
        }
        if (is_op(at(p, 0), OP_RPAREN)) {
            enode *n = node_new(N_OPAQUE); /* empty tuple */
            if (!n) { p->crash = 1; goto out; }
            p->i++;
            r = n;
            goto out;
        }
        enode *first = p_named(p);
        if (!first) goto out;
        if (is_kw(at(p, 0), KW_FOR)) {
            if (p_comp_tail(p)) { node_free(first); goto out; }
            if (!is_op(at(p, 0), OP_RPAREN)) { node_free(first); p_fail(p); goto out; }
            p->i++;
            enode *g = node_new(N_OPAQUE);
            if (!g) { node_free(first); p->crash = 1; goto out; }
            g->a = first;
            r = g;
            goto out;
        }
        if (is_op(at(p, 0), OP_COMMA)) {
            enode *tup = node_new(N_OPAQUE);
            if (!tup) { node_free(first); p->crash = 1; goto out; }
            if (node_push(tup, first)) { node_free(tup); p->crash = 1; goto out; }
            while (is_op(at(p, 0), OP_COMMA)) {
                p->i++;
                if (is_op(at(p, 0), OP_RPAREN)) break;
                if (is_kw(at(p, 0), KW_FOR)) { node_free(tup); p_fail(p); goto out; }
                enode *e = p_named(p);
                if (!e) { node_free(tup); goto out; }
                if (node_push(tup, e)) { node_free(tup); p->crash = 1; goto out; }
            }
            if (!is_op(at(p, 0), OP_RPAREN)) { node_free(tup); p_fail(p); goto out; }
            p->i++;
            r = tup;
            goto out;
        }
        if (!is_op(at(p, 0), OP_RPAREN)) { node_free(first); p_fail(p); goto out; }
        p->i++;
        r = first;                          /* (x) is x */
        goto out;
    }
    if (is_op(t, OP_LBRACK)) {
        p->i++; /* [ */
        if (is_op(at(p, 0), OP_RBRACK)) {
            enode *n = node_new(N_OPAQUE);
            if (!n) { p->crash = 1; goto out; }
            p->i++;
            r = n;
            goto out;
        }
        enode *first = NULL;
        if (is_op(at(p, 0), OP_MULT)) {
            p->i++;
            enode *e = p_expr(p);
            if (!e) goto out;
            first = node_new(N_OPAQUE);
            if (!first) { node_free(e); p->crash = 1; goto out; }
            first->a = e;
        } else {
            first = p_named(p);
            if (!first) goto out;
        }
        if (is_kw(at(p, 0), KW_FOR)) {
            if (p_comp_tail(p)) { node_free(first); goto out; }
            if (!is_op(at(p, 0), OP_RBRACK)) { node_free(first); p_fail(p); goto out; }
            p->i++;
            enode *c = node_new(N_OPAQUE);
            if (!c) { node_free(first); p->crash = 1; goto out; }
            c->a = first;
            r = c;
            goto out;
        }
        if (is_op(at(p, 0), OP_COMMA)) {
            enode *lst = node_new(N_OPAQUE);
            if (!lst) { node_free(first); p->crash = 1; goto out; }
            if (node_push(lst, first)) { node_free(lst); p->crash = 1; goto out; }
            while (is_op(at(p, 0), OP_COMMA)) {
                p->i++;
                if (is_op(at(p, 0), OP_RBRACK)) break;
                if (is_kw(at(p, 0), KW_FOR)) { node_free(lst); p_fail(p); goto out; }
                enode *e = NULL;
                if (is_op(at(p, 0), OP_MULT)) {
                    p->i++;
                    enode *x = p_expr(p);
                    if (!x) { node_free(lst); goto out; }
                    e = node_new(N_OPAQUE);
                    if (!e) { node_free(x); node_free(lst); p->crash = 1; goto out; }
                    e->a = x;
                } else {
                    e = p_named(p);
                    if (!e) { node_free(lst); goto out; }
                }
                if (node_push(lst, e)) { node_free(lst); p->crash = 1; goto out; }
            }
            if (!is_op(at(p, 0), OP_RBRACK)) { node_free(lst); p_fail(p); goto out; }
            p->i++;
            r = lst;
            goto out;
        }
        if (!is_op(at(p, 0), OP_RBRACK)) { node_free(first); p_fail(p); goto out; }
        p->i++;
        /* `[x]` is a one-element list: `unsupported expression`, and its
         * item is never evaluated */
        r = node_new(N_OPAQUE);
        if (!r) { node_free(first); p->crash = 1; goto out; }
        r->a = first;
        goto out;
    }
    if (is_op(t, OP_LBRACE)) {
        p->i++; /* { */
        if (is_op(at(p, 0), OP_RBRACE)) {
            enode *n = node_new(N_OPAQUE);
            if (!n) { p->crash = 1; goto out; }
            p->i++;
            r = n;
            goto out;
        }
        /* ** unpacking opens a dict */
        if (is_op(at(p, 0), OP_POW)) {
            enode *d = node_new(N_OPAQUE);
            if (!d) { p->crash = 1; goto out; }
            p->i++;
            enode *e = p_expr(p);
            if (!e) { node_free(d); goto out; }
            if (node_push(d, e)) { node_free(d); p->crash = 1; goto out; }
            while (is_op(at(p, 0), OP_COMMA)) {
                p->i++;
                if (is_op(at(p, 0), OP_RBRACE)) break;
                if (!is_op(at(p, 0), OP_POW)) { node_free(d); p_fail(p); goto out; }
                p->i++;
                enode *x = p_expr(p);
                if (!x) { node_free(d); goto out; }
                if (node_push(d, x)) { node_free(d); p->crash = 1; goto out; }
            }
            if (!is_op(at(p, 0), OP_RBRACE)) { node_free(d); p_fail(p); goto out; }
            p->i++;
            r = d;
            goto out;
        }
        /* first element as an expression (dict keys cannot be walrus) */
        enode *first = p_expr(p);
        if (!first) goto out;
        if (is_op(at(p, 0), OP_COLON)) {
            /* dict pair / dictcomp */
            p->i++;
            enode *val = p_expr(p);
            if (!val) { node_free(first); goto out; }
            if (is_kw(at(p, 0), KW_FOR)) {
                if (p_comp_tail(p)) { node_free(first); node_free(val); goto out; }
                if (!is_op(at(p, 0), OP_RBRACE)) { node_free(first); node_free(val); p_fail(p); goto out; }
                p->i++;
                enode *c = node_new(N_OPAQUE);
                if (!c) { node_free(first); node_free(val); p->crash = 1; goto out; }
                node_free(first);
                node_free(val);
                r = c;
                goto out;
            }
            enode *d = node_new(N_OPAQUE);
            if (!d) { node_free(first); node_free(val); p->crash = 1; goto out; }
            if (node_push(d, first) || node_push(d, val)) {
                node_free(d); p->crash = 1; goto out;
            }
            while (is_op(at(p, 0), OP_COMMA)) {
                p->i++;
                if (is_op(at(p, 0), OP_RBRACE)) break;
                if (is_kw(at(p, 0), KW_FOR)) { node_free(d); p_fail(p); goto out; }
                enode *k2 = p_expr(p);
                if (!k2) { node_free(d); goto out; }
                if (!is_op(at(p, 0), OP_COLON)) { node_free(k2); node_free(d); p_fail(p); goto out; }
                p->i++;
                enode *v2 = p_expr(p);
                if (!v2) { node_free(k2); node_free(d); goto out; }
                if (node_push(d, k2) || node_push(d, v2)) {
                    node_free(d); p->crash = 1; goto out;
                }
            }
            if (!is_op(at(p, 0), OP_RBRACE)) { node_free(d); p_fail(p); goto out; }
            p->i++;
            r = d;
            goto out;
        }
        if (is_op(at(p, 0), OP_WALRUS)) {
            /* set element with a walrus: {x := 1, ...} / {x := 1 for ...} */
            p->i++;
            enode *val = p_expr(p);
            if (!val) { node_free(first); goto out; }
            node_free(first);
            enode *set = node_new(N_OPAQUE);
            if (!set) { node_free(val); p->crash = 1; goto out; }
            if (node_push(set, val)) { node_free(set); p->crash = 1; goto out; }
            if (is_kw(at(p, 0), KW_FOR)) {
                if (p_comp_tail(set ? p : p)) { node_free(set); goto out; }
                if (!is_op(at(p, 0), OP_RBRACE)) { node_free(set); p_fail(p); goto out; }
                p->i++;
                r = set;
                goto out;
            }
            while (is_op(at(p, 0), OP_COMMA)) {
                p->i++;
                if (is_op(at(p, 0), OP_RBRACE)) break;
                enode *e = p_named(p);
                if (!e) { node_free(set); goto out; }
                if (node_push(set, e)) { node_free(set); p->crash = 1; goto out; }
            }
            if (!is_op(at(p, 0), OP_RBRACE)) { node_free(set); p_fail(p); goto out; }
            p->i++;
            r = set;
            goto out;
        }
        if (is_kw(at(p, 0), KW_FOR)) {
            if (p_comp_tail(p)) { node_free(first); goto out; }
            if (!is_op(at(p, 0), OP_RBRACE)) { node_free(first); p_fail(p); goto out; }
            p->i++;
            enode *c = node_new(N_OPAQUE);
            if (!c) { node_free(first); p->crash = 1; goto out; }
            c->a = first;
            r = c;
            goto out;
        }
        if (is_op(at(p, 0), OP_COMMA)) {
            enode *set = node_new(N_OPAQUE);
            if (!set) { node_free(first); p->crash = 1; goto out; }
            if (node_push(set, first)) { node_free(set); p->crash = 1; goto out; }
            while (is_op(at(p, 0), OP_COMMA)) {
                p->i++;
                if (is_op(at(p, 0), OP_RBRACE)) break;
                if (is_kw(at(p, 0), KW_FOR)) { node_free(set); p_fail(p); goto out; }
                enode *e = p_named(p);
                if (!e) { node_free(set); goto out; }
                if (node_push(set, e)) { node_free(set); p->crash = 1; goto out; }
            }
            if (!is_op(at(p, 0), OP_RBRACE)) { node_free(set); p_fail(p); goto out; }
            p->i++;
            r = set;
            goto out;
        }
        if (!is_op(at(p, 0), OP_RBRACE)) { node_free(first); p_fail(p); goto out; }
        p->i++;
        /* `{x}` is a one-element set: `unsupported expression` */
        r = node_new(N_OPAQUE);
        if (!r) { node_free(first); p->crash = 1; goto out; }
        r->a = first;
        goto out;
    }

    if (is_kw(t, KW_NIL)) {
        /* Constant(None): not in the oracle's (int, float, str, bool)
         * tuple, so it is `unsupported expression` and never a value */
        enode *n = node_new(N_OPAQUE);
        if (!n) { p->crash = 1; goto out; }
        p->i++;
        r = n;
        goto out;
    }

    p_fail(p);                              /* not an atom start */
out:
    p->depth--;
    return r;
}

static enode *p_primary(par *p) {
    enode *a = p_atom(p);
    if (!a) return NULL;
    for (;;) {
        const tok *t = at(p, 0);
        if (is_op(t, OP_LPAREN)) {
            /* call: N_CALL only for a plain FUNCS name (the oracle's
             * `isinstance(n.func, ast.Name) and n.func.id in FUNCS and
             * not n.keywords`); anything else parses its argument list
             * into an N_OPAQUE node whose arguments are never evaluated */
            p->i++;
            int is_call = (a->k == N_NAME) && is_func_name(a->s);
            if (is_call) {
                enode *call = node_new(N_CALL);
                if (!call) { node_free(a); p->crash = 1; return NULL; }
                call->s = a->s;
                call->slen = a->slen;
                a->s = NULL;
                node_free(a);
                a = call;
            } else {
                enode *opq = node_new(N_OPAQUE);
                if (!opq) { node_free(a); p->crash = 1; return NULL; }
                opq->a = a;
                a = opq;
            }
            /* argument list */
            if (is_op(at(p, 0), OP_RPAREN)) { p->i++; continue; }
            int seen_star = 0, seen_dstar = 0, seen_kw = 0;
            for (;;) {
                if (is_op(at(p, 0), OP_RPAREN)) { p->i++; break; }  /* trailing comma */
                const tok *u = at(p, 0);
                if (!u) { p_fail(p); node_free(a); return NULL; }
                enode *e = NULL;
                if (is_op(u, OP_MULT)) {
                    if (seen_dstar) { p_fail(p); node_free(a); return NULL; }
                    p->i++;
                    e = p_expr(p);
                    if (!e) { node_free(a); return NULL; }
                    enode *wrap = node_new(N_OPAQUE);  /* Starred: the oracle
                                                        * raises `unsupported
                                                        * expression` on the
                                                        * marker itself */
                    if (!wrap) { node_free(e); node_free(a); p->crash = 1; return NULL; }
                    wrap->a = e;
                    e = wrap;
                    seen_star = 1;
                } else if (is_op(u, OP_POW)) {
                    p->i++;
                    e = p_expr(p);
                    if (!e) { node_free(a); return NULL; }
                    enode *wrap = node_new(N_OPAQUE);
                    if (!wrap) { node_free(e); node_free(a); p->crash = 1; return NULL; }
                    wrap->a = e;
                    e = wrap;
                    seen_dstar = 1;
                } else if (u->k == TK_NAME && is_op(at(p, 1), OP_ASSIGN)) {
                    /* keyword argument: with any keyword present the whole
                     * call is `unsupported expression` and NO argument is
                     * evaluated, so demote the node now */
                    if (seen_dstar) { p_fail(p); node_free(a); return NULL; }
                    p->i += 2;
                    e = p_expr(p);
                    if (!e) { node_free(a); return NULL; }
                    a->k = N_OPAQUE;
                    seen_kw = 1;
                } else {
                    if (seen_kw || seen_star || seen_dstar) {
                        p_fail(p); node_free(a); return NULL;
                    }
                    e = p_named(p);
                    if (!e) { node_free(a); return NULL; }
                    if (is_kw(at(p, 0), KW_FOR)) {
                        /* generator expression: the only argument, and it
                         * is itself `unsupported expression` */
                        if (p_comp_tail(p)) { node_free(e); node_free(a); return NULL; }
                        enode *wrap = node_new(N_OPAQUE);
                        if (!wrap) { node_free(e); node_free(a); p->crash = 1; return NULL; }
                        wrap->a = e;
                        if (!is_op(at(p, 0), OP_RPAREN)) {
                            node_free(wrap); p_fail(p); node_free(a); return NULL;
                        }
                        p->i++;
                        if (node_push(a, wrap)) { node_free(wrap); node_free(a); p->crash = 1; return NULL; }
                        break;
                    }
                }
                if (node_push(a, e)) { node_free(e); node_free(a); p->crash = 1; return NULL; }
                if (is_op(at(p, 0), OP_COMMA)) { p->i++; continue; }
                if (is_op(at(p, 0), OP_RPAREN)) { p->i++; break; }
                p_fail(p);
                node_free(a);
                return NULL;
            }
            continue;
        }
        if (is_op(t, OP_LBRACK)) {
            p->i++;
            enode *sub = node_new(N_OPAQUE);
            if (!sub) { node_free(a); p->crash = 1; return NULL; }
            sub->a = a;
            a = sub;
            if (is_op(at(p, 0), OP_RBRACK)) { p_fail(p); node_free(a); return NULL; }
            for (;;) {
                if (is_op(at(p, 0), OP_RBRACK)) { p->i++; break; }  /* trailing comma */
                const tok *u = at(p, 0);
                if (!u) { p_fail(p); node_free(a); return NULL; }
                /* item: expression or slice */
                enode *item = NULL;
                int is_slice = 0;
                if (is_op(u, OP_COLON)) {
                    is_slice = 1;
                } else {
                    item = p_named(p);
                    if (!item) { node_free(a); return NULL; }
                    if (is_op(at(p, 0), OP_COLON)) is_slice = 1;
                }
                if (is_slice) {
                    node_free(item);
                    item = NULL;
                    /* [start]:[stop][:step] -- each bound may be empty */
                    for (int part = 0; part < 3; part++) {
                        if (!is_op(at(p, 0), OP_COLON)) break;
                        p->i++;
                        const tok *w = at(p, 0);
                        if (is_op(w, OP_COMMA) || is_op(w, OP_RBRACK) ||
                            is_op(w, OP_COLON)) {
                            /* empty bound */
                        } else {
                            enode *b2 = p_named(p);
                            if (!b2) { node_free(a); return NULL; }
                            node_free(b2);
                        }
                    }
                    if (!is_op(at(p, 0), OP_COMMA) && !is_op(at(p, 0), OP_RBRACK)) {
                        p_fail(p); node_free(a); return NULL;
                    }
                }
                node_free(item);
                if (is_op(at(p, 0), OP_COMMA)) { p->i++; continue; }
                if (is_op(at(p, 0), OP_RBRACK)) { p->i++; break; }
                p_fail(p);
                node_free(a);
                return NULL;
            }
            continue;
        }
        if (is_op(t, OP_DOT)) {
            p->i++;
            const tok *nm = at(p, 0);
            if (!nm || nm->k != TK_NAME) { p_fail(p); node_free(a); return NULL; }
            p->i++;
            enode *attr = node_new(N_OPAQUE);
            if (!attr) { node_free(a); p->crash = 1; return NULL; }
            attr->a = a;
            a = attr;
            continue;
        }
        break;
    }
    return a;
}

static enode *p_power(par *p) {
    enode *a;
    if (is_kw(at(p, 0), KW_AWAIT)) {
        p->i++;
        a = p_primary(p);
        if (!a) return NULL;
        enode *aw = node_new(N_OPAQUE);
        if (!aw) { node_free(a); p->crash = 1; return NULL; }
        aw->a = a;
        a = aw;
    } else {
        a = p_primary(p);
        if (!a) return NULL;
    }
    if (is_op(at(p, 0), OP_POW)) {
        p->i++;
        enode *b = p_factor(p);
        if (!b) { node_free(a); return NULL; }
        enode *n = node_new(N_POW);
        if (!n) { node_free(a); node_free(b); p->crash = 1; return NULL; }
        n->a = a;
        n->b = b;
        return n;
    }
    return a;
}

static enode *p_factor(par *p) {
    const tok *t = at(p, 0);
    if (is_op(t, OP_SUB) || is_op(t, OP_ADD) || is_op(t, OP_TILDE)) {
        int op = t->op;
        p->i++;
        p->raw++;
        if (p->raw > AF_FRAME_LIMIT) { p->raw--; p->crash = 1; return NULL; }
        enode *a = p_factor(p);
        p->raw--;
        if (!a) return NULL;
        unsigned char k = op == OP_SUB ? N_USUB : op == OP_ADD ? N_UADD : N_INVERT;
        enode *n = node_new(k);
        if (!n) { node_free(a); p->crash = 1; return NULL; }
        n->a = a;
        return n;
    }
    return p_power(p);
}

static enode *p_term(par *p) {
    enode *a = p_factor(p);
    if (!a) return NULL;
    for (;;) {
        const tok *t = at(p, 0);
        int k = 0;
        if (is_op(t, OP_MULT)) k = N_MULT;
        else if (is_op(t, OP_DIV)) k = N_DIV;
        else if (is_op(t, OP_FLOORDIV)) k = N_FLOORDIV;
        else if (is_op(t, OP_MOD)) k = N_MOD;
        else if (is_op(t, OP_MATMULT)) k = N_MATMULT;
        else break;
        p->i++;
        enode *b = p_factor(p);
        if (!b) { node_free(a); return NULL; }
        enode *n = node_new((unsigned char)k);
        if (!n) { node_free(a); node_free(b); p->crash = 1; return NULL; }
        n->a = a;
        n->b = b;
        a = n;
    }
    return a;
}

static enode *p_arith(par *p) {
    enode *a = p_term(p);
    if (!a) return NULL;
    for (;;) {
        const tok *t = at(p, 0);
        int k = 0;
        if (is_op(t, OP_ADD)) k = N_ADD;
        else if (is_op(t, OP_SUB)) k = N_SUB;
        else break;
        p->i++;
        enode *b = p_term(p);
        if (!b) { node_free(a); return NULL; }
        enode *n = node_new((unsigned char)k);
        if (!n) { node_free(a); node_free(b); p->crash = 1; return NULL; }
        n->a = a;
        n->b = b;
        a = n;
    }
    return a;
}

#define SHIFT_LEVEL(fn, next, OP1, OP2, K1, K2) \
static enode *fn(par *p) { \
    enode *a = next(p); \
    if (!a) return NULL; \
    for (;;) { \
        const tok *t = at(p, 0); \
        int k = 0; \
        if (is_op(t, OP1)) k = K1; \
        else if (is_op(t, OP2)) k = K2; \
        else break; \
        p->i++; \
        enode *b = next(p); \
        if (!b) { node_free(a); return NULL; } \
        enode *n = node_new((unsigned char)k); \
        if (!n) { node_free(a); node_free(b); p->crash = 1; return NULL; } \
        n->a = a; n->b = b; a = n; \
    } \
    return a; \
}

SHIFT_LEVEL(p_shift, p_arith, OP_LSHIFT, OP_RSHIFT, N_LSHIFT, N_RSHIFT)
SHIFT_LEVEL(p_bitand, p_shift, OP_BITAND, OP_BITAND, N_BITAND, N_BITAND)
SHIFT_LEVEL(p_bitxor, p_bitand, OP_BITXOR, OP_BITXOR, N_BITXOR, N_BITXOR)
SHIFT_LEVEL(p_bitor, p_bitxor, OP_BITOR, OP_BITOR, N_BITOR, N_BITOR)

static enode *p_cmp(par *p) {
    enode *left = p_bitor(p);
    if (!left) return NULL;
    size_t nops = 0, cap = 0;
    unsigned char *ops = NULL;
    enode **comps = NULL;
    size_t nc = 0, ccap = 0;
    int failed = 0;

    for (;;) {
        const tok *t = at(p, 0);
        int op = -1;
        if (is_op(t, OP_EQ)) op = CMP_EQ;
        else if (is_op(t, OP_NE)) op = CMP_NE;
        else if (is_op(t, OP_LT)) op = CMP_LT;
        else if (is_op(t, OP_LE)) op = CMP_LE;
        else if (is_op(t, OP_GT)) op = CMP_GT;
        else if (is_op(t, OP_GE)) op = CMP_GE;
        else if (is_kw(t, KW_IN)) op = CMP_IN;
        else if (is_kw(t, KW_IS)) {
            p->i++;
            if (is_kw(at(p, 0), KW_NOT)) { p->i++; op = CMP_ISNOT; }
            else op = CMP_IS;
        } else if (is_kw(t, KW_NOT)) {
            if (!is_kw(at(p, 1), KW_IN)) { failed = 1; break; }
            p->i += 2;
            op = CMP_NOTIN;
        } else break;
        if (op >= 0 && !is_kw(t, KW_IS) && !is_kw(t, KW_NOT)) p->i++;
        enode *right = p_bitor(p);
        if (!right) { failed = 1; break; }
        if (nops == cap) {
            size_t nc2 = cap ? cap * 2 : 4;
            unsigned char *q = (unsigned char *)realloc(ops, nc2);
            if (!q) { free(ops); free(comps); node_free(right); p->crash = 1; return NULL; }
            ops = q; cap = nc2;
        }
        ops[nops++] = (unsigned char)op;
        if (nc == ccap) {
            size_t nc2 = ccap ? ccap * 2 : 4;
            enode **q = (enode **)realloc(comps, nc2 * sizeof *q);
            if (!q) { free(ops); free(comps); node_free(right); p->crash = 1; return NULL; }
            comps = q; ccap = nc2;
        }
        comps[nc++] = right;
    }

    if (failed || p->fail || p->crash) {
        free(ops);
        for (size_t i = 0; i < nc; i++) node_free(comps[i]);
        free(comps);
        if (!p->crash) p_fail(p);
        node_free(left);
        return NULL;
    }
    if (nops == 0) {
        free(ops);
        free(comps);
        return left;
    }
    enode *n = node_new(N_CMP);
    if (!n) {
        free(ops);
        node_free(left);
        for (size_t i = 0; i < nc; i++) node_free(comps[i]);
        free(comps);
        p->crash = 1;
        return NULL;
    }
    if (node_push(n, left)) goto oom;
    for (size_t i = 0; i < nc; i++)
        if (node_push(n, comps[i])) { comps[i] = NULL; goto oom; }
    free(comps);
    n->ops = ops;
    n->nops = nops;
    return n;
oom:
    free(ops);
    free(comps);
    node_free(n);
    p->crash = 1;
    return NULL;
}

static enode *p_not(par *p) {
    if (is_kw(at(p, 0), KW_NOT)) {
        p->i++;
        p->raw++;
        if (p->raw > AF_FRAME_LIMIT) { p->raw--; p->crash = 1; return NULL; }
        enode *a = p_not(p);
        p->raw--;
        if (!a) return NULL;
        enode *n = node_new(N_NOT);
        if (!n) { node_free(a); p->crash = 1; return NULL; }
        n->a = a;
        return n;
    }
    return p_cmp(p);
}

static enode *p_and(par *p) {
    enode *a = p_not(p);
    if (!a) return NULL;
    if (!is_kw(at(p, 0), KW_AND)) return a;
    enode *n = node_new(N_AND);
    if (!n) { node_free(a); p->crash = 1; return NULL; }
    if (node_push(n, a)) { node_free(n); p->crash = 1; return NULL; }
    while (is_kw(at(p, 0), KW_AND)) {
        p->i++;
        enode *b = p_not(p);
        if (!b) { node_free(n); return NULL; }
        if (node_push(n, b)) { node_free(n); p->crash = 1; return NULL; }
    }
    return n;
}

static enode *p_or(par *p) {
    enode *a = p_and(p);
    if (!a) return NULL;
    if (!is_kw(at(p, 0), KW_OR)) return a;
    enode *n = node_new(N_OR);
    if (!n) { node_free(a); p->crash = 1; return NULL; }
    if (node_push(n, a)) { node_free(n); p->crash = 1; return NULL; }
    while (is_kw(at(p, 0), KW_OR)) {
        p->i++;
        enode *b = p_and(p);
        if (!b) { node_free(n); return NULL; }
        if (node_push(n, b)) { node_free(n); p->crash = 1; return NULL; }
    }
    return n;
}

static enode *p_ternary(par *p) {
    enode *a = p_or(p);
    if (!a) return NULL;
    if (!is_kw(at(p, 0), KW_IF)) return a;
    p->i++;
    enode *cond = p_or(p);
    if (!cond) { node_free(a); return NULL; }
    if (!is_kw(at(p, 0), KW_ELSE)) {
        node_free(a); node_free(cond); p_fail(p); return NULL;
    }
    p->i++;
    enode *b = p_expr(p);
    if (!b) { node_free(a); node_free(cond); return NULL; }
    enode *n = node_new(N_OPAQUE);
    if (!n) { node_free(a); node_free(cond); node_free(b); p->crash = 1; return NULL; }
    node_free(a); node_free(cond); node_free(b);
    return n;
}

static enode *p_expr(par *p) {
    if (p->crash || p->fail) return NULL;
    if (++p->depth > AF_PARSE_DEPTH) { p_fail(p); p->depth--; return NULL; }
    enode *r;
    if (is_kw(at(p, 0), KW_LAMBDA)) r = p_lambda(p);
    else r = p_ternary(p);
    p->depth--;
    return r;
}

/* named expression: NAME ':=' expression, else a plain expression.
 * Legal only where the oracle allows it (containers, parens, call args,
 * subscripts); never at the top level or inside ternary/boolop. */
static enode *p_named(par *p) {
    if (p->crash || p->fail) return NULL;
    if (at(p, 0) && at(p, 0)->k == TK_NAME && is_op(at(p, 1), OP_WALRUS)) {
        const tok *nm = at(p, 0);
        p->i += 2;
        enode *v = p_expr(p);
        if (!v) return NULL;
        enode *n = node_new(N_OPAQUE);
        if (!n) { node_free(v); p->crash = 1; return NULL; }
        (void)nm;
        node_free(v);
        return n;
    }
    return p_expr(p);
}

/* Sub-parse one f-string replacement field (the source is already
 * prepped with the whole expression): lex, parse, require the whole
 * span consumed. Sets p->fail (syntax error) or p->crash; returns 0 or
 * -1. */
static int af_expr_sub_parse(par *p, const char *src, size_t n) {
    if (p->crash || p->fail) return -1;
    /* a replacement field carries no indent rule (`f'{ 1 }'` is legal) */
    while (n && is_sp((unsigned char)*src)) { src++; n--; }
    tok *t = NULL;
    size_t nt = 0;
    int rc = lex(src, n, &t, &nt);
    if (rc == -2) { p->crash = 1; return -1; }
    if (rc != 0) { p_fail(p); return -1; }
    par sub;
    memset(&sub, 0, sizeof sub);
    sub.t = t;
    sub.nt = nt;
    enode *e = p_expr(&sub);
    int bad = 0;
    if (!e || sub.fail) bad = 1;
    else if (sub.i != nt) bad = 1;
    if (sub.crash) { p->crash = 1; bad = 0; }   /* crash wins over syntax */
    node_free(e);
    toks_free(t, nt);
    if (bad) p_fail(p);
    return (p->crash || bad) ? -1 : 0;
}

typedef struct { par *p; int bad; } fstr_ctx;

static int fstr_field_cb(const char *e, size_t en, void *ud) {
    fstr_ctx *c = (fstr_ctx *)ud;
    if (c->bad) return 0;
    if (af_expr_sub_parse(c->p, e, en)) { c->bad = 1; return 1; }
    return 0;
}

/* validate every replacement field of an f-string literal */
static int af_expr_check_fstring(par *p, const char *s, size_t n) {
    fstr_ctx c;
    c.p = p;
    c.bad = 0;
    if (walk_fstring(s, n, fstr_field_cb, &c) < 0) c.bad = 1;
    return c.bad ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* evaluator (arcforge_ref.py::_ev)                                    */
/* ------------------------------------------------------------------ */

typedef enum { EV_OK = 0, EV_EXPR, EV_TYPE, EV_CRASH } evst;

typedef struct {
    evst st;
    char *msg;             /* heap: the ExprError text (EV_EXPR) */
    int depth;             /* _ev recursion (AF_EVAL_DEPTH -> CRASH) */
    const af_json *env;
} evc;

static void ev_err(evc *x, evst st, const char *msg) {
    if (x->st != EV_OK) return;
    x->st = st;
    if (msg) {
        x->msg = xstrdup(msg);
        if (!x->msg) x->st = EV_CRASH;
    }
}

static void ev_errf(evc *x, evst st, const char *fmt, ...) {
    if (x->st != EV_OK) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) { x->st = EV_CRASH; return; }
    if ((size_t)n < sizeof buf) { ev_err(x, st, buf); return; }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) { x->st = EV_CRASH; return; }
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    ev_err(x, st, big);
    free(big);
}

static void ev_wrong_args(evc *x, const char *f) {
    ev_errf(x, EV_EXPR, "wrong number or type of arguments to %s()", f);
}

/* `integer N is outside the valid range -2147483648..2147483647` */
static void ev_int_range_i(evc *x, int64_t v) {
    ev_errf(x, EV_EXPR, "integer %lld is outside the valid range "
            "-2147483648..2147483647", (long long)v);
}
static void ev_int_range_s(evc *x, const char *decimal) {
    ev_errf(x, EV_EXPR, "integer %s is outside the valid range "
            "-2147483648..2147483647", decimal);
}

/* ---- Python value model ----------------------------------------- */

static int py_is_int(const af_json *v) {
    return v->type == AF_JSON_INT || v->type == AF_JSON_BOOL;
}
static int py_is_float(const af_json *v) { return v->type == AF_JSON_FLOAT; }
static int py_is_num(const af_json *v) { return py_is_int(v) || py_is_float(v); }
static int64_t py_ival(const af_json *v) {
    return v->type == AF_JSON_BOOL ? (v->b ? 1 : 0) : v->i;
}
static double py_dval(const af_json *v) {
    return v->type == AF_JSON_FLOAT ? v->d : (double)py_ival(v);
}
static int py_is_zero(const af_json *v) {
    if (v->type == AF_JSON_BOOL) return !v->b;
    if (v->type == AF_JSON_INT) return v->i == 0;
    if (v->type == AF_JSON_FLOAT) return v->d == 0;   /* -0.0 yes, NaN no */
    return 0;
}

/* int64 -> double range for an exact conversion (saturating; the
 * documented divergence for floats whose floor leaves int64) */
static int64_t f64_to_i64(double f) {
    if (f >= 9223372036854775808.0) return INT64_MAX;
    if (f < -9223372036854775808.0) return INT64_MIN;
    return (int64_t)f;
}

/* exact int-vs-double equality (Python compares int and float exactly;
 * long double is not portable, so compare through the integral part) */
static int int_eq_d(int64_t i, double y) {
    if (isnan(y)) return 0;
    if (!(y >= -9223372036854775808.0 && y < 9223372036854775808.0)) return 0;
    double f = floor(y);
    if (f != y) return 0;                    /* fractional -> unequal */
    return i == (int64_t)y;
}

/* exact int-vs-double ordering; y must not be NaN.
 * returns 0 (i < y), 1 (i == y) or 2 (i > y) */
static int int_cmp_d(int64_t i, double y) {
    if (y >= 9223372036854775808.0) return 0;
    if (y < -9223372036854775808.0) return 2;
    double f = floor(y);
    if (f != y) {
        int64_t t = (int64_t)f;
        if (i < t) return 0;
        if (i > t) return 2;
        return 0;                            /* i == floor(y) < y */
    }
    int64_t t = (int64_t)y;
    if (i < t) return 0;
    if (i > t) return 2;
    return 1;
}

/* UTF-8 byte order equals Python's code-point order */
static int str_cmp(const af_json *a, const af_json *b) {
    size_t n = a->slen < b->slen ? a->slen : b->slen;
    int c = n ? memcmp(a->s, b->s, n) : 0;
    if (c) return c < 0 ? -1 : 1;
    if (a->slen == b->slen) return 0;
    return a->slen < b->slen ? -1 : 1;
}

static int py_eq(const af_json *a, const af_json *b);

static int arr_eq(const af_json *a, const af_json *b) {
    if (a->n != b->n) return 0;
    for (size_t i = 0; i < a->n; i++)
        if (!py_eq(a->items[i], b->items[i])) return 0;
    return 1;
}

static int obj_eq(const af_json *a, const af_json *b) {
    if (a->n != b->n) return 0;
    for (size_t i = 0; i < a->n / 2; i++) {   /* n counts entries */
        const af_json *k = a->items[2 * i];
        const af_json *v = a->items[2 * i + 1];
        const af_json *w = af_json_get(b, k->s);
        if (!w || !py_eq(v, w)) return 0;
    }
    return 1;
}

/* Python `==`: never raises. bool is an int; int and float compare
 * exactly; NaN equals nothing; other types compare only to their own. */
static int py_eq(const af_json *a, const af_json *b) {
    if (py_is_num(a) && py_is_num(b)) {
        if (py_is_float(a) || py_is_float(b)) {
            double x = py_dval(a), y = py_dval(b);
            if (isnan(x) || isnan(y)) return 0;
            if (py_is_float(a) && py_is_float(b)) return x == y;
            if (py_is_float(a)) return int_eq_d(py_ival(b), x);
            return int_eq_d(py_ival(a), y);
        }
        return py_ival(a) == py_ival(b);
    }
    if (a->type != b->type) return 0;
    switch (a->type) {
    case AF_JSON_NULL: return 1;
    case AF_JSON_STRING: return str_cmp(a, b) == 0;
    case AF_JSON_ARRAY: return arr_eq(a, b);
    case AF_JSON_OBJECT: return obj_eq(a, b);
    default: return 0;
    }
}

/* apply an ordered operator to a three-way result */
static int ord_apply(int op, int c) {          /* c <0 / 0 / >0 */
    switch (op) {
    case CMP_LT: return c < 0;
    case CMP_LE: return c <= 0;
    case CMP_GT: return c > 0;
    default:     return c >= 0;               /* CMP_GE */
    }
}

/* Python `<` `<=` `>` `>=`. Returns 0 with *res set, 1 when the pair is
 * unordered (NaN -> every ordered comparison is False), -1 on TypeError. */
static int py_ord(const af_json *a, const af_json *b, int op, int *res) {
    if (py_is_num(a) && py_is_num(b)) {
        if (py_is_float(a) || py_is_float(b)) {
            double x = py_dval(a), y = py_dval(b);
            if (isnan(x) || isnan(y)) return 1;
            if (py_is_float(a) && py_is_float(b)) {
                *res = ord_apply(op, x < y ? -1 : x > y ? 1 : 0);
                return 0;
            }
            if (py_is_float(a)) {            /* a float, b int */
                *res = ord_apply(op, 1 - int_cmp_d(py_ival(b), x));
                return 0;
            }
            *res = ord_apply(op, int_cmp_d(py_ival(a), y) - 1);
            return 0;
        }
        int64_t x = py_ival(a), y = py_ival(b);
        *res = ord_apply(op, x < y ? -1 : x > y ? 1 : 0);
        return 0;
    }
    if (a->type == AF_JSON_STRING && b->type == AF_JSON_STRING) {
        *res = ord_apply(op, str_cmp(a, b));
        return 0;
    }
    if (a->type == AF_JSON_ARRAY && b->type == AF_JSON_ARRAY) {
        /* Python's list ordering: == first, then < on the first pair
         * that differs (which may itself raise) */
        int sub_op = op == CMP_LE ? CMP_LT : op == CMP_GE ? CMP_GT : op;
        size_t n = a->n < b->n ? a->n : b->n;
        for (size_t i = 0; i < n; i++) {
            if (py_eq(a->items[i], b->items[i])) continue;
            int rc = py_ord(a->items[i], b->items[i], sub_op, res);
            if (rc) return rc;
            return 0;
        }
        if (a->n == b->n) { *res = (op == CMP_LE || op == CMP_GE); return 0; }
        *res = a->n < b->n ? (op == CMP_LT || op == CMP_LE)
                           : (op == CMP_GT || op == CMP_GE);
        return 0;
    }
    return -1;                                /* TypeError */
}

/* CPython's _float_div_mod (floatobject.c): the exact floor/remainder pair
 * for doubles, including the signed-zero rules -- a zero remainder takes
 * the sign of the denominator, a zero quotient the sign of vx/wx -- and
 * the roundoff correction for huge quotients. Both `//` and `%` on floats
 * read this pair (float_floor_div / float_rem pick q / r). */
static void float_divmod(double vx, double wx, double *q, double *r) {
    double mod = fmod(vx, wx);
    double div = (vx - mod) / wx;
    if (mod) {
        /* ensure the remainder has the same sign as the denominator */
        if ((wx < 0) != (mod < 0)) {
            mod += wx;
            div -= 1.0;
        }
    } else {
        /* fmod's signed zero is platform-dependent: normalize */
        mod = copysign(0.0, wx);
    }
    if (div) {
        /* snap the quotient to the nearest integral value */
        *q = floor(div);
        if (div - *q > 0.5) *q += 1.0;
    } else {
        /* zero quotient: same sign as the true quotient */
        *q = copysign(0.0, vx / wx);
    }
    *r = mod;
}

/* int // int and int % int with Python's sign rule (the remainder takes
 * the divisor's sign); y != 0 is checked by the caller */
static int64_t int_floordiv(int64_t x, int64_t y) {
    if (y == -1) return (int64_t)(0u - (uint64_t)x);   /* wraps: divergence */
    int64_t q = x / y, r = x % y;
    if (r != 0 && ((y < 0) != (r < 0))) q -= 1;
    return q;
}
static int64_t int_mod(int64_t x, int64_t y) {
    if (y == -1) return 0;
    int64_t r = x % y;
    if (r != 0 && ((y < 0) != (r < 0))) r += y;
    return r;
}

int af_truthy(const af_json *v) {
    switch (v->type) {
    case AF_JSON_NULL: return 0;
    case AF_JSON_BOOL: return v->b != 0;
    case AF_JSON_INT: return v->i != 0;
    case AF_JSON_FLOAT: return v->d != 0;      /* NaN is truthy */
    case AF_JSON_STRING: return v->slen != 0;
    default: return v->n != 0;
    }
}

static af_json *e_eval(evc *x, const enode *n);

/* message = prefix + suffix, heap-built (identifiers may be long) */
static void ev_errdup(evc *x, evst st, const char *prefix, const char *suffix) {
    size_t a = strlen(prefix), b = strlen(suffix);
    char *m = (char *)malloc(a + b + 1);
    if (!m) { x->st = EV_CRASH; return; }
    memcpy(m, prefix, a);
    memcpy(m + a, suffix, b);
    m[a + b] = 0;
    ev_err(x, st, m);
    free(m);
}

/* ---- literals and names ------------------------------------------ */

static af_json *e_const(evc *x, const enode *n) {
    switch (n->sub) {
    case C_INT:
        if (n->big) { ev_int_range_s(x, n->big); return NULL; }
        if (n->i < INT32_MIN || n->i > INT32_MAX) {
            ev_int_range_i(x, n->i);
            return NULL;
        }
        return af_json_int(n->i);
    case C_FLT: return af_json_float(n->d);
    case C_STR: return af_json_strn(n->s, n->slen);
    case C_BOOL: return af_json_bool(n->i != 0);
    default:    /* bytes / complex / Ellipsis: not in the oracle's tuple */
        ev_err(x, EV_EXPR, "unsupported expression");
        return NULL;
    }
}

static af_json *e_name(evc *x, const enode *n) {
    if (n->slen >= 2 && n->s[0] == '_' && n->s[1] == '_') {
        const char *k = n->s + 2;
        const af_json *v = (x->env && x->env->type == AF_JSON_OBJECT)
                         ? af_json_get(x->env, k) : NULL;
        if (!v) { ev_errdup(x, EV_EXPR, "unknown variable $", k); return NULL; }
        af_json *c = af_json_clone(v);
        if (!c) ev_err(x, EV_CRASH, NULL);
        return c;
    }
    if (strcmp(n->s, "true") == 0) return af_json_bool(1);
    if (strcmp(n->s, "false") == 0) return af_json_bool(0);
    char *r = af_py_repr(n->s);
    if (!r) { ev_err(x, EV_CRASH, NULL); return NULL; }
    ev_errdup(x, EV_EXPR, "unknown identifier ", r);
    free(r);
    return NULL;
}

/* ---- unary -------------------------------------------------------- */

static af_json *e_unary(evc *x, const enode *n) {
    if (n->k == N_USUB && n->a->k == N_CONST && n->a->sub == C_INT) {
        /* the oracle folds -(literal) first: the AST writes -2147483648
         * as -(2147483648), and the FOLDED value is range-checked */
        const enode *c = n->a;
        if (c->big) {
            char *m = (char *)malloc(strlen(c->big) + 2);
            if (!m) { ev_err(x, EV_CRASH, NULL); return NULL; }
            m[0] = '-';
            memcpy(m + 1, c->big, strlen(c->big) + 1);
            ev_int_range_s(x, m);
            free(m);
            return NULL;
        }
        int64_t v = (int64_t)(0u - (uint64_t)c->i);
        if (v < INT32_MIN || v > INT32_MAX) { ev_int_range_i(x, v); return NULL; }
        return af_json_int(v);
    }
    af_json *v = e_eval(x, n->a);
    if (!v) return NULL;
    af_json *r = NULL;
    switch (n->k) {
    case N_USUB:
        if (py_is_int(v)) r = af_json_int((int64_t)(0u - (uint64_t)py_ival(v)));
        else if (py_is_float(v)) r = af_json_float(-v->d);
        else ev_err(x, EV_TYPE, NULL);
        break;
    case N_UADD:
        if (py_is_int(v)) r = af_json_int(py_ival(v));
        else if (py_is_float(v)) r = af_json_float(v->d);
        else ev_err(x, EV_TYPE, NULL);
        break;
    case N_NOT:
        r = af_json_bool(!af_truthy(v));
        break;
    default:  /* N_INVERT: the operand IS evaluated, then unsupported */
        ev_err(x, EV_EXPR, "unsupported expression");
        break;
    }
    af_json_free(v);
    if (!r && x->st == EV_OK) ev_err(x, EV_CRASH, NULL);
    return r;
}

/* ---- sequences ---------------------------------------------------- */

static af_json *str_concat(const af_json *a, const af_json *b) {
    char *p = (char *)malloc(a->slen + b->slen + 1);
    if (!p) return NULL;
    memcpy(p, a->s, a->slen);
    memcpy(p + a->slen, b->s, b->slen);
    p[a->slen + b->slen] = 0;
    af_json *r = af_json_strn(p, a->slen + b->slen);
    free(p);
    return r;
}

/* `'x' * n`: past AF_STR_REPEAT_CAP the oracle would MemoryError -> NULL
 * (the caller reports CRASH) */
static af_json *str_repeat(const af_json *s, int64_t count) {
    if (count <= 0) return af_json_strn("", 0);
    if (s->slen == 0) return af_json_strn("", 0);
    if ((uint64_t)count > (uint64_t)AF_STR_REPEAT_CAP / s->slen) return NULL;
    size_t total = (size_t)s->slen * (size_t)count;
    char *p = (char *)malloc(total + 1);
    if (!p) return NULL;
    for (size_t i = 0; i < total; i += s->slen)
        memcpy(p + i, s->s, s->slen);
    p[total] = 0;
    af_json *r = af_json_strn(p, total);
    free(p);
    return r;
}

static af_json *arr_concat(const af_json *a, const af_json *b) {
    af_json *r = af_json_arr();
    if (!r) return NULL;
    for (size_t i = 0; i < a->n; i++) {
        af_json *c = af_json_clone(a->items[i]);
        if (!c || af_json_arr_push(r, c)) { af_json_free(c); af_json_free(r); return NULL; }
    }
    for (size_t i = 0; i < b->n; i++) {
        af_json *c = af_json_clone(b->items[i]);
        if (!c || af_json_arr_push(r, c)) { af_json_free(c); af_json_free(r); return NULL; }
    }
    return r;
}

static af_json *arr_repeat(const af_json *a, int64_t count) {
    if (count <= 0 || a->n == 0) return af_json_arr();
    if ((uint64_t)count > (uint64_t)AF_STR_REPEAT_CAP / a->n) return NULL;
    af_json *r = af_json_arr();
    if (!r) return NULL;
    for (int64_t k = 0; k < count; k++)
        for (size_t i = 0; i < a->n; i++) {
            af_json *c = af_json_clone(a->items[i]);
            if (!c || af_json_arr_push(r, c)) { af_json_free(c); af_json_free(r); return NULL; }
        }
    return r;
}

/* ---- binary ------------------------------------------------------- */

static af_json *e_binop(evc *x, const enode *n) {
    af_json *a = e_eval(x, n->a);
    if (!a) return NULL;
    af_json *b = e_eval(x, n->b);
    if (!b) { af_json_free(a); return NULL; }
    af_json *r = NULL;
    if ((n->k == N_DIV || n->k == N_FLOORDIV || n->k == N_MOD) && py_is_zero(b)) {
        ev_err(x, EV_EXPR, n->k == N_DIV ? "division by zero"
             : n->k == N_FLOORDIV ? "floor division by zero"
                                  : "modulo by zero");
        goto done;
    }
    switch (n->k) {
    case N_ADD:
        if (py_is_int(a) && py_is_int(b))
            r = af_json_int((int64_t)((uint64_t)py_ival(a) + (uint64_t)py_ival(b)));
        else if (py_is_num(a) && py_is_num(b))
            r = af_json_float(py_dval(a) + py_dval(b));
        else if (a->type == AF_JSON_STRING && b->type == AF_JSON_STRING)
            r = str_concat(a, b);
        else if (a->type == AF_JSON_ARRAY && b->type == AF_JSON_ARRAY)
            r = arr_concat(a, b);
        else ev_err(x, EV_TYPE, NULL);
        break;
    case N_SUB:
        if (py_is_int(a) && py_is_int(b))
            r = af_json_int((int64_t)((uint64_t)py_ival(a) - (uint64_t)py_ival(b)));
        else if (py_is_num(a) && py_is_num(b))
            r = af_json_float(py_dval(a) - py_dval(b));
        else ev_err(x, EV_TYPE, NULL);
        break;
    case N_MULT:
        if (py_is_int(a) && py_is_int(b))
            r = af_json_int((int64_t)((uint64_t)py_ival(a) * (uint64_t)py_ival(b)));
        else if (py_is_num(a) && py_is_num(b))
            r = af_json_float(py_dval(a) * py_dval(b));
        else if (a->type == AF_JSON_STRING && py_is_int(b))
            r = str_repeat(a, py_ival(b));
        else if (b->type == AF_JSON_STRING && py_is_int(a))
            r = str_repeat(b, py_ival(a));
        else if (a->type == AF_JSON_ARRAY && py_is_int(b))
            r = arr_repeat(a, py_ival(b));
        else if (b->type == AF_JSON_ARRAY && py_is_int(a))
            r = arr_repeat(b, py_ival(a));
        else ev_err(x, EV_TYPE, NULL);
        break;
    case N_DIV:
        if (py_is_num(a) && py_is_num(b))
            r = af_json_float(py_dval(a) / py_dval(b));
        else ev_err(x, EV_TYPE, NULL);
        break;
    case N_FLOORDIV:
        if (py_is_int(a) && py_is_int(b))
            r = af_json_int(int_floordiv(py_ival(a), py_ival(b)));
        else if (py_is_num(a) && py_is_num(b)) {
            double q, rem;
            float_divmod(py_dval(a), py_dval(b), &q, &rem);
            r = af_json_float(q);
        } else ev_err(x, EV_TYPE, NULL);
        break;
    case N_MOD:
        if (py_is_int(a) && py_is_int(b))
            r = af_json_int(int_mod(py_ival(a), py_ival(b)));
        else if (py_is_num(a) && py_is_num(b)) {
            double q, rem;
            float_divmod(py_dval(a), py_dval(b), &q, &rem);
            r = af_json_float(rem);
        } else ev_err(x, EV_TYPE, NULL);
        break;
    default:
        /* Pow / MatMult / shifts / bitwise: the oracle's BinOp has no
         * case for them -> unsupported expression, but only AFTER both
         * operands have been evaluated */
        ev_err(x, EV_EXPR, "unsupported expression");
        break;
    }
done:
    af_json_free(a);
    af_json_free(b);
    if (!r && x->st == EV_OK) ev_err(x, EV_CRASH, NULL);
    return r;
}

/* ---- comparisons -------------------------------------------------- */

/* one ordered operator: unordered (NaN) reads as False, TypeError up */
static int ord_try(const af_json *a, const af_json *b, int op, int *res) {
    int rc = py_ord(a, b, op, res);
    if (rc == 1) { *res = 0; return 0; }
    return rc;                       /* 0 or -1 */
}

static af_json *e_cmp(evc *x, const enode *n) {
    af_json *left = e_eval(x, n->v[0]);
    if (!left) return NULL;
    for (size_t i = 0; i < n->nops; i++) {
        af_json *right = e_eval(x, n->v[i + 1]);
        if (!right) { af_json_free(left); return NULL; }
        /* the oracle builds the whole {Eq, NotEq, Lt, LtE, Gt, GtE} dict
         * eagerly, so the ordered operators run even for == and != */
        int eq = py_eq(left, right), lt = 0, le = 0, gt = 0, ge = 0, ok;
        if (ord_try(left, right, CMP_LT, &lt) < 0 ||
            ord_try(left, right, CMP_LE, &le) < 0 ||
            ord_try(left, right, CMP_GT, &gt) < 0 ||
            ord_try(left, right, CMP_GE, &ge) < 0) {
            af_json_free(left);
            af_json_free(right);
            ev_err(x, EV_TYPE, NULL);
            return NULL;
        }
        switch (n->ops[i]) {
        case CMP_EQ: ok = eq; break;
        case CMP_NE: ok = !eq; break;
        case CMP_LT: ok = lt; break;
        case CMP_LE: ok = le; break;
        case CMP_GT: ok = gt; break;
        case CMP_GE: ok = ge; break;
        default:                                  /* is / is not / in / not in */
            af_json_free(left);
            af_json_free(right);
            ev_err(x, EV_EXPR, "unsupported comparison");
            return NULL;
        }
        af_json_free(left);
        if (!ok) return af_json_bool(0);
        left = right;
    }
    af_json_free(left);
    return af_json_bool(1);
}

/* ---- boolean operators -------------------------------------------- */

static af_json *e_boolop(evc *x, const enode *n) {
    /* every operand is evaluated (the oracle collects them in a list
     * first), and the result is all()/any() of their truthiness */
    int acc = (n->k == N_AND);
    for (size_t i = 0; i < n->nv; i++) {
        af_json *v = e_eval(x, n->v[i]);
        if (!v) return NULL;
        int t = af_truthy(v);
        af_json_free(v);
        if (n->k == N_AND) acc = acc && t;
        else acc = acc || t;
    }
    return af_json_bool(acc);
}

/* ---- the oracle's FUNCS ------------------------------------------- */

static af_json *fn_floorceil(const af_json *v, int ceil_mode, evc *x,
                             const char *fname) {
    if (py_is_int(v)) return af_json_int(py_ival(v));
    if (py_is_float(v)) {
        if (!isfinite(v->d)) { ev_err(x, EV_CRASH, NULL); return NULL; }
        return af_json_int(f64_to_i64(ceil_mode ? ceil(v->d) : floor(v->d)));
    }
    ev_wrong_args(x, fname);
    return NULL;
}

/* round(v) = math.floor(v + 0.5) */
static af_json *fn_round(const af_json *v, evc *x) {
    if (!py_is_num(v)) { ev_wrong_args(x, "round"); return NULL; }
    double d = py_dval(v) + 0.5;
    if (!isfinite(d)) { ev_err(x, EV_CRASH, NULL); return NULL; }
    return af_json_int(f64_to_i64(floor(d)));
}

static af_json *fn_abs(const af_json *v, evc *x) {
    if (py_is_int(v)) {
        int64_t i = py_ival(v);
        return af_json_int(i < 0 ? (int64_t)(0u - (uint64_t)i) : i);
    }
    if (py_is_float(v)) return af_json_float(fabs(v->d));
    ev_wrong_args(x, "abs");
    return NULL;
}

static int bytes_cmp(const char *a, size_t an, const char *b, size_t bn) {
    size_t n = an < bn ? an : bn;
    int c = n ? memcmp(a, b, n) : 0;
    if (c) return c < 0 ? -1 : 1;
    if (an == bn) return 0;
    return an < bn ? -1 : 1;
}

/* one-argument min/max: the argument is iterated (str -> characters in
 * code-point order, array -> items, object -> keys); an empty sequence
 * or a non-iterable mirrors Python's ValueError / TypeError */
static af_json *fn_minmax_one(const af_json *v, int want_max, evc *x,
                              const char *fname) {
    if (v->type == AF_JSON_STRING) {
        if (v->slen == 0) { ev_err(x, EV_CRASH, NULL); return NULL; }
        size_t bi = 0, bl = 1, i = 0;
        while (i < v->slen) {
            size_t j = i + 1;
            while (j < v->slen && ((unsigned char)v->s[j] & 0xC0) == 0x80) j++;
            int c = bytes_cmp(v->s + i, j - i, v->s + bi, bl);
            if (want_max ? c > 0 : c < 0) { bi = i; bl = j - i; }
            i = j;
        }
        return af_json_strn(v->s + bi, bl);
    }
    if (v->type == AF_JSON_ARRAY || v->type == AF_JSON_OBJECT) {
        size_t n = v->type == AF_JSON_OBJECT ? v->n / 2 : v->n;
        if (n == 0) { ev_err(x, EV_CRASH, NULL); return NULL; }
        const af_json *first = v->items[0];   /* item 0, or the first key */
        af_json *best = af_json_clone(first);
        if (!best) { ev_err(x, EV_CRASH, NULL); return NULL; }
        for (size_t i = 1; i < n; i++) {
            const af_json *it = v->type == AF_JSON_ARRAY ? v->items[i]
                                                         : v->items[2 * i];
            int res, rc = ord_try(it, best, want_max ? CMP_GT : CMP_LT, &res);
            if (rc < 0) { af_json_free(best); ev_wrong_args(x, fname); return NULL; }
            if (res) {
                af_json *c = af_json_clone(it);
                if (!c) { af_json_free(best); ev_err(x, EV_CRASH, NULL); return NULL; }
                af_json_free(best);
                best = c;
            }
        }
        return best;
    }
    ev_wrong_args(x, fname);
    return NULL;
}

/* two or more arguments: fold with `<` (min) / `>` (max) */
static af_json *fn_minmax_args(af_json **args, size_t nv, int want_max,
                               evc *x, const char *fname) {
    af_json *best = af_json_clone(args[0]);
    if (!best) { ev_err(x, EV_CRASH, NULL); return NULL; }
    for (size_t i = 1; i < nv; i++) {
        int res, rc = ord_try(args[i], best, want_max ? CMP_GT : CMP_LT, &res);
        if (rc < 0) { af_json_free(best); ev_wrong_args(x, fname); return NULL; }
        if (res) {
            af_json *c = af_json_clone(args[i]);
            if (!c) { af_json_free(best); ev_err(x, EV_CRASH, NULL); return NULL; }
            af_json_free(best);
            best = c;
        }
    }
    return best;
}

/* clamp(v, a, b) = max(a, min(b, v)) */
static af_json *fn_clamp(af_json **args, evc *x) {
    int res, rc = ord_try(args[0], args[2], CMP_LT, &res);   /* min(b, v) */
    if (rc < 0) { ev_wrong_args(x, "clamp"); return NULL; }
    af_json *low = af_json_clone(res ? args[0] : args[2]);
    if (!low) { ev_err(x, EV_CRASH, NULL); return NULL; }
    rc = ord_try(low, args[1], CMP_GT, &res);                /* max(a, low) */
    if (rc < 0) { af_json_free(low); ev_wrong_args(x, "clamp"); return NULL; }
    if (!res) {
        af_json *c = af_json_clone(args[1]);
        af_json_free(low);
        if (!c) { ev_err(x, EV_CRASH, NULL); return NULL; }
        low = c;
    }
    return low;
}

/* odd/even: _parity(floor(v), want) -- the smallest value of that
 * parity that is >= v (Python's % is non-negative for a positive
 * modulus) */
static af_json *fn_parity(const af_json *v, int want, evc *x,
                          const char *fname) {
    int64_t f;
    if (py_is_int(v)) f = py_ival(v);
    else if (py_is_float(v)) {
        if (!isfinite(v->d)) { ev_err(x, EV_CRASH, NULL); return NULL; }
        f = f64_to_i64(floor(v->d));
    } else { ev_wrong_args(x, fname); return NULL; }
    int64_t m = f % 2;
    if (m < 0) m += 2;
    return af_json_int(m == want ? f : (int64_t)((uint64_t)f + 1u));
}

/* half: math.floor(v / 2) -- true division, so always a float first */
static af_json *fn_half(const af_json *v, evc *x) {
    if (!py_is_num(v)) { ev_wrong_args(x, "half"); return NULL; }
    double d = py_dval(v) / 2.0;
    if (!isfinite(d)) { ev_err(x, EV_CRASH, NULL); return NULL; }
    return af_json_int(f64_to_i64(floor(d)));
}

static af_json *e_call(evc *x, const enode *n) {
    size_t nv = n->nv;
    af_json **args = NULL;
    if (nv) {
        args = (af_json **)calloc(nv, sizeof *args);
        if (!args) { ev_err(x, EV_CRASH, NULL); return NULL; }
    }
    /* arguments are evaluated BEFORE the FUNCS try block, so their
     * errors surface unchanged */
    for (size_t i = 0; i < nv; i++) {
        args[i] = e_eval(x, n->v[i]);
        if (!args[i]) goto fail;
    }
    const char *f = n->s;
    af_json *r = NULL;
    if (!strcmp(f, "floor") || !strcmp(f, "ceil")) {
        if (nv != 1) { ev_wrong_args(x, f); goto fail; }
        r = fn_floorceil(args[0], f[0] == 'c', x, f);
    } else if (!strcmp(f, "round")) {
        if (nv != 1) { ev_wrong_args(x, f); goto fail; }
        r = fn_round(args[0], x);
    } else if (!strcmp(f, "abs")) {
        if (nv != 1) { ev_wrong_args(x, f); goto fail; }
        r = fn_abs(args[0], x);
    } else if (!strcmp(f, "min") || !strcmp(f, "max")) {
        int want_max = (f[1] == 'a');
        if (nv == 0) { ev_wrong_args(x, f); goto fail; }   /* TypeError */
        r = nv == 1 ? fn_minmax_one(args[0], want_max, x, f)
                    : fn_minmax_args(args, nv, want_max, x, f);
    } else if (!strcmp(f, "clamp")) {
        if (nv != 3) { ev_wrong_args(x, f); goto fail; }
        r = fn_clamp(args, x);
    } else if (!strcmp(f, "odd") || !strcmp(f, "even")) {
        if (nv != 1) { ev_wrong_args(x, f); goto fail; }
        r = fn_parity(args[0], f[0] == 'o' ? 1 : 0, x, f);
    } else if (!strcmp(f, "half")) {
        if (nv != 1) { ev_wrong_args(x, f); goto fail; }
        r = fn_half(args[0], x);
    } else {
        ev_err(x, EV_CRASH, NULL);            /* unreachable (parse gate) */
    }
    for (size_t i = 0; i < nv; i++) af_json_free(args[i]);
    free(args);
    if (!r && x->st == EV_OK) ev_err(x, EV_CRASH, NULL);
    return r;
fail:
    if (args) {
        for (size_t i = 0; i < nv; i++) af_json_free(args[i]);
        free(args);
    }
    if (x->st == EV_OK) ev_err(x, EV_CRASH, NULL);
    return NULL;
}

/* ---- the evaluator ------------------------------------------------ */

static af_json *e_eval(evc *x, const enode *n) {
    if (x->st != EV_OK) return NULL;
    if (++x->depth > AF_EVAL_DEPTH) { x->depth--; ev_err(x, EV_CRASH, NULL); return NULL; }
    af_json *r = NULL;
    switch (n->k) {
    case N_CONST: r = e_const(x, n); break;
    case N_NAME: r = e_name(x, n); break;
    case N_USUB: case N_UADD: case N_NOT: case N_INVERT:
        r = e_unary(x, n); break;
    case N_ADD: case N_SUB: case N_MULT: case N_DIV: case N_FLOORDIV:
    case N_MOD: case N_POW: case N_MATMULT: case N_LSHIFT: case N_RSHIFT:
    case N_BITAND: case N_BITOR: case N_BITXOR:
        r = e_binop(x, n); break;
    case N_AND: case N_OR: r = e_boolop(x, n); break;
    case N_CMP: r = e_cmp(x, n); break;
    case N_CALL: r = e_call(x, n); break;
    default:                              /* N_OPAQUE: never evaluated */
        ev_err(x, EV_EXPR, "unsupported expression");
        break;
    }
    x->depth--;
    return r;
}

/* ---- entry points -------------------------------------------------- */

/* `syntax error in expression <repr>` / `type error in expression
 * <repr>` over the ORIGINAL (pre-prep) expression string */
static char *mk_expr_msg(const char *prefix, const af_json *expr) {
    /* repr_n takes the length: the expression can hold an embedded NUL,
     * which strlen would truncate */
    char *r = repr_n(expr->s, expr->slen);
    if (!r) return NULL;
    size_t a = strlen(prefix), b = strlen(r);
    char *m = (char *)malloc(a + b + 1);
    if (m) {
        memcpy(m, prefix, a);
        memcpy(m + a, r, b);
        m[a + b] = 0;
    }
    free(r);
    return m;
}

af_ev_status af_ev(const af_json *expr, const af_json *env,
                   af_json **out, char **msg) {
    *out = NULL;
    if (msg) *msg = NULL;
    if (!expr) return AF_EV_CRASH;
    /* the oracle returns int/float/bool literals untouched (no range
     * check: that is the document's job) */
    if (expr->type == AF_JSON_BOOL || expr->type == AF_JSON_INT ||
        expr->type == AF_JSON_FLOAT) {
        *out = af_json_clone(expr);
        return *out ? AF_EV_OK : AF_EV_CRASH;
    }
    if (expr->type != AF_JSON_STRING) return AF_EV_CRASH;    /* TypeError in _prep */
    if (memchr(expr->s, 0, expr->slen)) {
        /* CPython: "source code string cannot contain null bytes" is a
         * SyntaxError, reported like every other parse failure */
        if (msg) {
            *msg = mk_expr_msg("syntax error in expression ", expr);
            if (!*msg) return AF_EV_CRASH;
        }
        return AF_EV_ERR;
    }

    char *p = prep(expr->s, expr->slen);
    if (!p) return AF_EV_CRASH;
    tok *t = NULL;
    size_t nt = 0;
    int rc = lex(p, strlen(p), &t, &nt);
    if (rc == -2) { free(p); return AF_EV_CRASH; }

    par ps;
    memset(&ps, 0, sizeof ps);
    ps.t = t;
    ps.nt = nt;
    enode *root = rc == 0 ? p_expr(&ps) : NULL;
    /* a top-level tuple: `a, b` (trailing comma allowed). The oracle's
     * _ev never evaluates tuple items, so only the syntax matters. */
    if (root && !ps.fail && !ps.crash) {
        int is_tup = 0;
        while (is_op(at(&ps, 0), OP_COMMA)) {
            ps.i++;
            is_tup = 1;
            if (ps.i >= ps.nt) break;             /* trailing comma */
            enode *e = p_expr(&ps);
            if (!e) break;                        /* fail / crash recorded */
            node_free(e);                         /* items are never evaluated */
        }
        if (is_tup && !ps.fail && !ps.crash) {
            enode *w = node_new(N_OPAQUE);
            if (!w) { node_free(root); root = NULL; ps.crash = 1; }
            else { w->a = root; root = w; }
        }
    }
    /* the tokens borrow `p` (TK_NAME points into it); every name a node
     * keeps was copied out during the parse, so `p` can go now */
    free(p);
    if (ps.crash) {
        node_free(root);
        toks_free(t, nt);
        return AF_EV_CRASH;
    }
    if (rc != 0 || !root || ps.fail || ps.i != nt) {
        node_free(root);
        toks_free(t, nt);
        if (msg) {
            *msg = mk_expr_msg("syntax error in expression ", expr);
            if (!*msg) return AF_EV_CRASH;
        }
        return AF_EV_ERR;
    }

    evc x;
    x.st = EV_OK;
    x.msg = NULL;
    x.depth = 0;
    x.env = env;
    af_json *r = e_eval(&x, root);
    node_free(root);
    toks_free(t, nt);
    if (x.st == EV_OK) {
        if (!r) return AF_EV_CRASH;
        *out = r;
        return AF_EV_OK;
    }
    af_json_free(r);
    if (x.st == EV_CRASH) { free(x.msg); return AF_EV_CRASH; }
    char *m;
    if (x.st == EV_TYPE) {
        free(x.msg);
        m = mk_expr_msg("type error in expression ", expr);
    } else {
        m = x.msg;
        if (!m) m = xstrdup("unsupported expression");
    }
    if (!m) return AF_EV_CRASH;
    if (msg) *msg = m;
    else free(m);
    return AF_EV_ERR;
}

af_ev_status af_to_int(const af_json *v, int64_t *out, char **msg) {
    if (msg) *msg = NULL;
    if (!v) return AF_EV_CRASH;
    if (v->type == AF_JSON_BOOL) {                       /* ExprError */
        if (msg) {
            *msg = xstrdup("boolean used as a number");
            if (!*msg) return AF_EV_CRASH;
        }
        return AF_EV_ERR;
    }
    if (v->type == AF_JSON_INT) { *out = v->i; return AF_EV_OK; }
    if (v->type == AF_JSON_FLOAT) {
        double d = v->d;
        if (isnan(d) || isinf(d)) return AF_EV_CRASH;    /* ValueError / OverflowError */
        *out = f64_to_i64(floor(d));                     /* saturates: divergence */
        return AF_EV_OK;
    }
    return AF_EV_CRASH;      /* the oracle's uncaught TypeError */
}
