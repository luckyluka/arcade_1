/* expr_harness.c -- the M4.1c differential driver for c/expr.c.
 *
 * Usage: arcforge_expr_harness CASES.json
 *
 * CASES.json is a JSON array; each case is one of
 *
 *   ["ev",     <expr>, <env-object-or-null>]   -> af_ev
 *   ["to_int", <value>]                        -> af_to_int
 *   ["truthy", <value>]                        -> af_truthy
 *
 * and produces exactly one output line:
 *
 *   OK <Python repr() of the result>     e.g. "OK 2.0" / "OK 'knight'"
 *   ERR <ExprError message>
 *   CRASH                               (an uncaught Python exception:
 *                                         the oracle prints nothing and
 *                                         exits 1 -- see expr.h)
 *
 * tests/test_expr_parity.py computes the same three lines from
 * tools/arcforge_ref.py (ev / to_int / bool) and must match byte for
 * byte, plain and under -fsanitize=undefined,address, twice in a row,
 * in 7-bit ASCII.
 *
 * Host cc, C11, no external libraries (M4 contract): json.c + expr.c only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "json.h"

/* ---- tiny string builder ---------------------------------------------- */
typedef struct { char *b; size_t n, cap; } sb;

static void sb_init(sb *s) { s->b = NULL; s->n = s->cap = 0; }

static void sb_putn(sb *s, const char *p, size_t k) {
    if (s->n + k + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 64;
        while (nc < s->n + k + 1) nc *= 2;
        char *nb = realloc(s->b, nc);
        if (!nb) { fprintf(stderr, "expr_harness: out of memory\n"); exit(2); }
        s->b = nb; s->cap = nc;
    }
    memcpy(s->b + s->n, p, k);
    s->n += k;
    s->b[s->n] = 0;
}

static void sb_puts(sb *s, const char *p) { sb_putn(s, p, strlen(p)); }

/* Python repr() of any node the evaluator can hand back: None, True/False,
 * int, float, str (af_py_repr), list, dict. Object keys are strings in
 * every case our corpus produces, but repr them generically anyway. */
static void repr_into(sb *s, const af_json *v) {
    char buf[64];
    if (!v || v->type == AF_JSON_NULL) { sb_puts(s, "None"); return; }
    switch (v->type) {
    case AF_JSON_BOOL:
        sb_puts(s, v->b ? "True" : "False");
        return;
    case AF_JSON_INT:
        snprintf(buf, sizeof buf, "%lld", (long long)v->i);
        sb_puts(s, buf);
        return;
    case AF_JSON_FLOAT:
        af_py_repr_float(v->d, buf, sizeof buf);
        sb_puts(s, buf);
        return;
    case AF_JSON_STRING: {
        char *r = af_py_repr(v->s);
        if (!r) { fprintf(stderr, "expr_harness: out of memory\n"); exit(2); }
        sb_puts(s, r);
        free(r);
        return;
    }
    case AF_JSON_ARRAY:
        sb_puts(s, "[");
        for (size_t i = 0; i < v->n; i++) {
            if (i) sb_puts(s, ", ");
            repr_into(s, v->items[i]);
        }
        sb_puts(s, "]");
        return;
    default:
        sb_puts(s, "{");
        for (size_t j = 0; j + 1 < v->n; j += 2) {    /* n = entries */
            if (j) sb_puts(s, ", ");
            repr_into(s, v->items[j]);
            sb_puts(s, ": ");
            repr_into(s, v->items[j + 1]);
        }
        sb_puts(s, "}");
        return;
    }
}

static char *repr_all(const af_json *v) {
    sb s;
    sb_init(&s);
    repr_into(&s, v);
    return s.b;                       /* freed by the caller */
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "expr_harness: cannot open %s\n", path); return NULL; }
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return NULL; }
    for (;;) {
        if (n == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        if (!got) break;
        n += got;
    }
    fclose(f);
    buf[n] = 0;
    *len = n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s CASES.json\n", argv[0]);
        return 2;
    }
    size_t len = 0;
    char *text = slurp(argv[1], &len);
    if (!text) return 2;

    af_json *root = NULL;
    char err[256];
    if (af_json_parse(text, len, &root, err, sizeof err) ||
        !root || root->type != AF_JSON_ARRAY) {
        fprintf(stderr, "expr_harness: bad case file: %s\n", err);
        free(text);
        return 2;
    }
    free(text);

    for (size_t i = 0; i < root->n; i++) {
        const af_json *c = root->items[i];
        if (c->type != AF_JSON_ARRAY || c->n < 2 ||
            c->items[0]->type != AF_JSON_STRING) {
            fprintf(stderr, "expr_harness: bad case %zu\n", i);
            af_json_free(root);
            return 2;
        }
        const char *kind = c->items[0]->s;
        if (!strcmp(kind, "ev")) {
            if (c->n != 3) { fprintf(stderr, "expr_harness: bad ev case %zu\n", i); af_json_free(root); return 2; }
            const af_json *env = c->items[2];
            af_json *out = NULL;
            char *msg = NULL;
            af_ev_status st = af_ev(c->items[1],
                                    env && env->type == AF_JSON_OBJECT ? env : NULL,
                                    &out, &msg);
            if (st == AF_EV_OK) {
                char *r = repr_all(out);
                printf("OK %s\n", r);
                free(r);
                af_json_free(out);
            } else if (st == AF_EV_ERR) {
                printf("ERR %s\n", msg ? msg : "");
                free(msg);
            } else {
                puts("CRASH");
            }
        } else if (!strcmp(kind, "to_int")) {
            int64_t iv = 0;
            char *msg = NULL;
            af_ev_status st = af_to_int(c->items[1], &iv, &msg);
            if (st == AF_EV_OK) {
                printf("OK %lld\n", (long long)iv);
            } else if (st == AF_EV_ERR) {
                printf("ERR %s\n", msg ? msg : "");
                free(msg);
            } else {
                puts("CRASH");
            }
        } else if (!strcmp(kind, "truthy")) {
            printf("OK %s\n", af_truthy(c->items[1]) ? "True" : "False");
        } else {
            fprintf(stderr, "expr_harness: unknown kind %s\n", kind);
            af_json_free(root);
            return 2;
        }
        if (ferror(stdout)) { af_json_free(root); return 2; }
    }
    af_json_free(root);
    return 0;
}
