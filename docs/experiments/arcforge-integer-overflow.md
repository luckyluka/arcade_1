# ArcForge P0.3: integer range — what the card closes and what it does not

Date: 2026-11-18 · scope: `arcforge/` ROADMAP card **P0.3 (integer
semantics doc)** · finding type: the card's mitigation is necessary but
not sufficient (rule 7/8: recorded here in the same commit as the fix).

## What P0.3 implements

* Every JSON integer in a validated document (free-form `metadata`
  included) must fit `[-2147483648, 2147483647]` or it is `E_INT_RANGE`
  at its exact pointer, with `observed`/`expected` and no `fix`
  (clamping would silently change meaning).
* Every **integer literal** in an expression must fit the same range;
  rejection is at parse time (`ExprError` → the existing
  `E_ARCHETYPE_EXPR` / `E_RECIPE_EXPR`).
* Floor semantics are pinned in vectors: `//`, `%`, `to_int`, `half`,
  `odd`/`even`, `round` and `rot_fixed` — a truncating C `/` would render
  `rot_fixed(-7, 2, 180)` as `(448, -127)` instead of `(448, -128)`.

## Finding 1 — parse-time rejection does not bound *results*

The card (and `docs/LANGUAGE.md`) rejects values at parse time. Python's
oracle then evaluates with **exact** arithmetic; the C compiler will use
64-bit intermediates. Every operand in range does **not** imply every
intermediate in range:

* `2147483647 * 2147483647 * 2147483647` ≈ 9.92e27 — legal literals
  (three int32s), overflows `int64` at the second multiplication.
  Python returns the exact value; `int64` C wraps (well-defined for
  unsigned, *signed overflow is UB*).
* `-(-2147483648)` — both written literals are in range (the inner one is
  exactly `INT_MIN`); the result `2147483648` exceeds int32. It fits
  int64, so C does not break, but the pair shows operation results are
  never range-checked by design ("parse time" only).

**Measured today:** the whole shipped corpus (`examples/`, `registry/`,
`schema/`) contains **zero** out-of-range integers and **zero**
out-of-range integer literals inside strings — both scans are pinned in
`arcforge/tests/test_int_range.py`, so a regression is caught. Nothing in
the corpus evaluates a multi-factor product anywhere near 2^63.

**Residual risk for M4:** byte-for-byte diagnostics are safe (both tools
raise/report identically at parse time), but *rendered output* could
diverge if a future registry expression computes an out-of-int64 result.
Not fixed here: bounding intermediates would mean range-tracking every
evaluation step, which is a semantics change (`docs/LANGUAGE.md` would
need an explicit "expression results are 64-bit" clause) — that is a
separate card, not a doc fix. Until then the corpus scans above are the
guard.

## Finding 2 — floats are not range-checked (out of card scope)

`P0.3` bounds *integers*. A float literal (`1e300`) or a true-division
result (`a / b`) is legal, and `to_int` (`math.floor`) would turn
`1e300` into a ~10^300 integer in Python. In C the same value reaches a
`(int64_t)` cast of an out-of-range double — **undefined behaviour**,
worst case a garbage pixel coordinate instead of a diagnostic. No shipped
document or expression contains such a value (the float case is pinned as
"not range-checked" in the vectors, so the behaviour is deliberate and
visible). A follow-up card should decide the language rule: reject
non-finite/`> int64` floats in expressions, or clamp inside geometry with
a diagnostic.

## Finding 3 — floor audit: clean

Audited every division in `tools/arcforge_ref.py`:

* `rot_fixed`, `to_int`, `half`, `odd`/`even`, `//`, `%`, thickness
  `th // 2`, mirror axis `W // 2`, layout `(X + 16) // 32` — all floor or
  floor-equivalent (`math.floor(x + 0.5)` for `round`). ✔
* Two float divisions remain: `silhouette_area` and `cell_fill`
  assertions compare `frac` against declared bands. They are IEEE-754
  divisions of exact integers (correctly rounded on every conforming
  platform) and only feed assertion reporting/`round(frac, 4)` — no pixel
  decision depends on them, so rule 4 (integer-only *raster*) holds.

## Found and fixed while implementing — unary-minus AST fold

Python's AST writes `-2147483648` as `UnaryOp(USub, Constant(2147483648))`,
so checking the raw constant rejected the legal `INT_MIN` (and reported
the wrong value in the message). `_ev` now folds `-(literal)` before the
range check; vectors `'-2147483648'` (accept) and `'-2147483649'`
(reject, message carries `-2147483649`) pin both directions.

## Repro

```
cd arcforge && python3 tests/test_int_range.py   # 44 vectors, ALL GREEN
```
