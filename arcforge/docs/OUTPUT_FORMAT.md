# Output format (P0.5)

Normative. Every JSON document `tools/arcforge_ref.py` emits — stdout
reports, golden files, `atlas.json`, the sweep table — is written by the
reference implementation `dump_json()`. The C compiler (M4) must
reproduce every document **byte-for-byte**. The conformance vectors in
`tests/test_output_format.py` re-serialise real reports with an
independent writer written from *this* document and compare against the
tool's actual bytes, so spec, reference implementation and compiler all
meet in one place.

## Sinks

| sink | trailing newline |
|---|---|
| stdout reports (`validate`, `test`, `render`, `golden`, `lint`, `sweep`, `list`, `describe`) | exactly one (added by `print`) |
| `tests/golden/<id>.json` (written by `golden --update`) | exactly one, written into the file |
| `out/<id>/atlas.json` (written by `render`) | **none** |
| `out/sweep/<name>.table.json` (written by `sweep`) | exactly one, written into the file |

Not covered here: internal hash keys (frame-state dedup keys are
`json.dumps(..., sort_keys=True, separators=(",", ":"))` and never leave
the process) and the authoring scripts (`gen_assets.py`, `gen_ramps.py`,
`lib_add.py`), which are not the compiler.

## Writer rules

1. **Encoding.** Output is 7-bit ASCII. Every non-ASCII character is
   escaped (rule 4); no byte ≥ 0x80 is ever written.
2. **Key order.** Insertion order as emitted, by the code that built the
   object. Keys are never sorted.
3. **Layout.** Two spaces of indent per nesting level; `": "` between key
   and value; `,` followed by newline + indent between members and
   items; empty objects `{}` and empty arrays `[]` occupy a single line;
   no trailing comma; the root value starts at column 0.
4. **Strings** (Python's `ensure_ascii=True` escaping): `"` → `\"`,
   `\` → `\\`, backspace / tab / newline / formfeed / CR → `\b \t \n
   \f \r`, every other C0 control → `\u00xx`, every other non-ASCII
   character → `\uxxxx` — hex digits **lowercase**, astral characters as
   UTF-16 surrogate pairs (`😀` → `\ud83d\ude00`). `/` is not escaped.
   Object keys are escaped with the same rules.
5. **Integers, booleans, null.** Integers are decimal with an optional
   leading `-`, no leading zeros, no exponent, no `+`, no float suffix
   (arbitrary precision — Python ints never truncate). Booleans are
   `true` / `false` and null is `null`; they are recognised before the
   integer rule (`true` is not `1`).
6. **Floats: exactly four decimal digits, always.** Fixed-point
   notation (never an exponent), correctly rounded on the exact binary
   value with round-half-even — i.e. C's `printf("%.4f")`. Examples:
   `0.0` → `0.0000`, `0.5` → `0.5000`, `0.6904` → `0.6904`, `1.0` →
   `1.0000`, `-0.0` → `-0.0000`, `-1.23456` → `-1.2346`.
   The rule applies **everywhere a float reaches a report**: JSON number
   fields (assertion `observed` values, `E_TEST_FAILED` payloads) *and*
   numbers interpolated into message strings (`expected` bands such as
   `0.2000..0.7000`). The tool's `num4()` is the reference for text,
   `dump_json()` for numbers. Report floats are finite by construction
   (`round(x, 4)` of a bounded ratio, or document parameters); the
   non-finite tokens `NaN`, `Infinity`, `-Infinity` are **not JSON** and
   `load_json()` rejects them at read time with the same
   `JSONDecodeError` as malformed JSON — a report can therefore never
   contain them, and the C parser rejects them too. If one ever appears
   in a report, that is a bug, not a format decision.
7. **Message-embedded JSON fragments** (`_lit`, e.g. the
   `["int", "bool"]` inside `expected one of [...]`): the same rules
   with compact separators — `, ` between items, `": "` between key and
   value, no newlines (`dump_json(v, indent=None)`).
8. **Determinism.** The same object always produces the same bytes:
   no timestamps, no locale- or platform-dependent formatting, no
   dict-order dependence (objects are built with fixed insertion order).

## Example

A complete stdout report exactly as `validate` prints it (the last line
is followed by the single trailing newline from the sink rule):

```json
{
  "status": "FAIL",
  "asset": "hero.int_overflow",
  "stage": "validate",
  "diagnostics": [
    {
      "code": "E_INT_RANGE",
      "severity": "error",
      "path": "/metadata/spawn_count",
      "message": "integer 4294967296 is outside the valid range -2147483648..2147483647",
      "observed": 4294967296,
      "expected": "-2147483648..2147483647"
    }
  ]
}
```

## Conformance

`tests/test_output_format.py` (run by `make check`):

* an independent `writer()` derived from the rules above is checked
  against exact byte vectors — layout, empty containers, every escaping
  category, all integer/float rules, compact fragments;
* every real report the tool can emit — `validate` on every example and
  every negative fixture, `lint`, `list`, `describe` for each kind,
  `test`, `golden` — is captured, parsed, re-serialised with `writer()`
  and compared byte-for-byte (and must be pure ASCII);
* every checked-in golden file round-trips through `writer()`;
* the float-in-message rule is observed in a live `test` report
  (`"expected": "0.2000..0.7000"`);
* hygiene: every remaining `json.dumps` call in the tool is internal
  (`sort_keys=True`) or the writer's own string escaper, and the sink
  newline rules are pinned in the source.
