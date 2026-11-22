/* expr.h -- M4.1c: the expression pipeline (prep, parse, evaluate).
 *
 * The oracle is tools/arcforge_ref.py::_prep / ev / _ev / to_int (and
 * Python's own ast.parse for the accept set). This layer reproduces:
 *
 *   accept set  Python 3.12 eval-mode grammar, byte for byte: the full
 *               expression grammar (ternary, lambda, comprehensions,
 *               slices, f-strings, walrus in containers, yield in
 *               parens) plus the tokenizer's number/string/line rules.
 *               A construct Python rejects, this rejects; one it
 *               accepts, this accepts. See expr.c for the pinned rules.
 *   messages    `syntax error in expression {expr!r}`,
 *               `type error in expression {expr!r}`, ExprError texts
 *               (`unknown variable $k`, `unknown identifier 'k'`,
 *               `integer N is outside the valid range -2147483648..2147483647`,
 *               `division by zero` / `floor division by zero` /
 *               `modulo by zero`, `unsupported expression`,
 *               `unsupported comparison`,
 *               `wrong number or type of arguments to f()`,
 *               `boolean used as a number`), with Python repr() of the
 *               ORIGINAL (pre-prep) expression string.
 *   crash       An uncaught Python exception (TypeError from _prep on a
 *               non-string, ValueError for a NUL byte, OverflowError /
 *               ValueError from floor/ceil on a non-finite float, a
 *               TypeError from to_int on a non-number) means the oracle
 *               exits 1 with EMPTY stdout. This reports AF_EV_CRASH and
 *               the caller must do the same.
 *
 * No external libraries (M4 contract). Values are af_json nodes with
 * Python's four runtime types (int64, double, str, bool); env entries
 * are cloned on lookup. int64 arithmetic wraps on overflow -- the P0.3
 * documented divergence (Python ints are arbitrary precision) -- and a
 * float whose floor exceeds int64 saturates; neither is reachable in
 * the corpus, which the differential harness proves.
 */
#ifndef ARCFORGE_EXPR_H
#define ARCFORGE_EXPR_H

#include "json.h"

typedef enum {
    AF_EV_OK = 0,
    AF_EV_ERR,     /* ExprError: *msg is a heap string, caller frees */
    AF_EV_CRASH    /* uncaught Python exception: print nothing, exit 1 */
} af_ev_status;

/* Evaluate `expr` (an oracle expression: string, or a literal number /
 * bool the oracle would return as-is) against `env` (an object; may be
 * NULL when no variable can resolve). On AF_EV_OK *out owns a new node.
 * On AF_EV_ERR *msg owns the message. On AF_EV_CRASH nothing is owned. */
af_ev_status af_ev(const af_json *expr, const af_json *env,
                   af_json **out, char **msg);

/* Python's to_int(): bool -> ExprError("boolean used as a number"),
 * int -> itself, float -> math.floor (non-finite -> CRASH, out of int64
 * saturates -- documented divergence), anything else -> CRASH (the
 * oracle's uncaught TypeError). On AF_EV_ERR *msg owns the message. */
af_ev_status af_to_int(const af_json *v, int64_t *out, char **msg);

/* Python truthiness (BoolOp/`when`): null -> 0, bool -> b, int -> != 0,
 * float -> != 0 (NaN is truthy), str -> non-empty, array/object -> non-empty. */
int af_truthy(const af_json *v);

/* Python repr() of a str for error messages (heap, caller frees). */
char *af_py_repr(const char *s);

/* repr() of s[0..n): for text that can hold an embedded NUL (a .px line,
 * an expression), which strlen would truncate. */
char *af_py_repr_n(const char *s, size_t n);

/* Python str()/repr() of a float (shortest round-trip + the ".0" rule)
 * into buf[0..sz). */
void af_py_repr_float(double d, char *buf, size_t sz);

#endif /* ARCFORGE_EXPR_H */
