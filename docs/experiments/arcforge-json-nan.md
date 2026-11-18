# Python's JSON reader accepts NaN/Infinity; no conforming parser will

*Finding from arcforge P0.5 (JSON output spec). 2026-11-17.*

## What happened

While writing `arcforge/docs/OUTPUT_FORMAT.md` (the normative byte-level
spec for every report the tool emits, so the M4 C compiler can reproduce
them), the question "can a report ever contain a non-finite float?"
surfaced. Measured, in the arcforge reference tool
(`arcforge/tools/arcforge_ref.py`):

- **Reader side.** `json.loads()` — i.e. Python's standard library —
  accepts the bare tokens `NaN`, `Infinity` and `-Infinity` by default.
  They are *not* JSON (RFC 8259 has no such literal), and no conforming
  parser accepts them; a bundled C parser (M4) certainly will not.
  So a file containing `{"x": NaN}` loaded cleanly and the value flowed
  into geometry and assertion maths as a float.
- **Writer side.** The tool itself never produced such a value (report
  floats are `round(ratio, 4)` of bounded quantities, or document
  parameters), but nothing *prevented* it: `json.dumps()` happily writes
  `NaN`, and the old emission path would have put it in a report byte for
  byte — a report no conforming consumer could parse.
- **Reach.** A quick scan of every checked-in document (examples,
  registry, schema, negative fixtures) found zero non-finite constants, so
  nothing in the corpus was affected. The gap was structural, not latent.

## What changed (P0.5)

1. `load_json()` (and `load_asset()`, which delegates to it) now pass
   `parse_constant=<reject>` to `json.loads`, so `NaN`/`Infinity`/
   `-Infinity` raise `json.JSONDecodeError` at read time — **the same
   exception class as malformed JSON**, joining the pre-existing failure
   mode instead of inventing a new one. The reader's contract is now
   exactly "RFC 8259 or an error".
2. `docs/OUTPUT_FORMAT.md` rule 6 states the invariant: reports contain
   only finite numbers; the tokens are not JSON and are rejected at read
   time; if a non-finite value ever reaches `dump_json()`, that is a bug,
   not a format decision (the writer raises `TypeError` on anything it
   cannot serialise under the rules).

`tests/test_output_format.py` pins both sides: rejection vectors for all
three tokens through `load_json` and `load_asset` (plus a malformed-JSON
control showing the class matches), and the writer's finite-only output
over the full live corpus.

## Not done (deliberately)

- No change to `json.dumps` call sites' *values*: arithmetic that could
  overflow to inf/None in Python (e.g. `2147483647**3` is exact in Python
  but overflows int64 in C) is tracked separately in
  `arcforge-integer-overflow.md`; P0.5 only closes the JSON-representation
  hole.
- The authoring scripts (`gen_assets.py`, `gen_ramps.py`, `lib_add.py`)
  are out of scope: they are not the compiler and their output is not a
  report.
