# Roadmap

Vertical slice first, breadth later. No wave starts before the previous gate is met.

## M0 Spec + executable spike  (DONE)
Strict schema, language spec, registry (1 archetype, 5 recipes, 1 style pack), `.px` format, reference compiler, diagnostics with fixes, golden + negative tests, rendered example.

## M1 Make one hero look good (25–40 h)
- Inner-outline option (per-part edge lines) so arms/torso/cape separate.
- Arm/shoulder silhouette rework; cape that reads as a cape; claw readability.
- 8–10 recipes total (add: helmet_open, shield, armor_scratched, heavy_armor_chest, antenna/visor variants).
- **Gate G1** (6 variants by JSON only, blind review).

## M2 Breadth (60–100 h)
- Second archetype (side-view humanoid or creature/enemy), recipe families `creature/*`, `effects/*`.
- Second style pack (different palette/outline rules).
- `import_px`: PNG → quantize to style pack → `.px`.
- Recipe compatibility matrix + z-order tests. **Gate G2.**

## M3 Animation depth (30–50 h)
Per-recipe animation hooks (lagging cape), frame variants (swap-in parts), hurt/death templates, easing-free but authored timing, hit-frame metadata consumed by hitboxes.

## X0 Variable-size core (20–30 h) — from `spec-next/07_MILESTONES.md`, before M4
Metrics, expression anchors, `odd/even/half`, `percent`, size classes + proportions, auto canvas, `min_cell`, size sweep lint. **Exit:** the prototype becomes the real compiler path; the sweep passes on `humanoid_front_scalable`.

## X1 Language extensions (25–40 h) — from `spec-next/07_MILESTONES.md`, before M4
`mirror`, `line`, `repeat` (+ `%`), `px` shape, `locals`, `variants`/`extends`/`palette_swap`, `px 2`, parameter tracks, swap frames, new assertions. **Exit:** every example in `spec-next/examples` compiles; negative fixtures per new `E_*` code.

## X2 Hero archetypes — from `spec-next/07_MILESTONES.md`, before Gate G1x
`humanoid_side` (side view, `height` 12–64, four classes, `min_cell` contract, the five bindable animation templates), the `platformer_24` style pack, six hero recipes (`cap_round`, `overalls`, `gloves`, `boots_round`, `moustache_block`, `belt_buckle`) and the one-file hero `examples/hero.knight.json` whose `small`/`big`/`fire` ids derive through `variants` (fire costs only a `palette_swap`). **Exit:** the sweep passes on `humanoid_side` at every size and parameter extreme; hero assertions and the three new goldens are green. **Status:** exit criteria green. Gate G1x's blind rubric was scored: big passes, small fails — see below.

## Gate G1x — the v2.1 freeze gate (before M4)
Hero small + big compile from one file with `variants`, the sweep passes across its declared range, blind rubric ≥ 3.5, and `fire` costs only a `palette_swap`. **If it fails:** stop and fall back to hand-drawn `.px` for the hero; keep ArcForge for tiles, atlas, palette, level preview and quality tooling. Only after G1x passes is v2.1 frozen and M4.1 allowed to start.

**Status (2026-11-15): FAILED on the rubric.** Mechanical criteria all green; the blind rubric passed big (height 32) and failed small (height 16) — `art/reviews/001-g1x.md` records the verdict and the sheets. The fallback is in force: the hero goes to hand-drawn `.px`, ArcForge keeps tiles/atlas/palette/level preview/quality tooling, v2.1 stays unfrozen and M4.1 stays blocked.

**Fallback delivered (2026-11-16):** `examples/hero.drawn.json` — five hand-drawn `.px` pieces per scale over the `humanoid_side` skeleton, three ids from one file (`small` base, `big` via `variants` override, `fire` via `from` + `palette_swap`), 51 frames, `make check`/`make spec-check` ALL GREEN, three new goldens. Swap-pose stride frames expose one pixel of mannequin leg per side; aligning `armor_ramp` with the fragment art makes the slivers read as leg shading. The finding and the leak-check harness are in `../../docs/experiments/hero-drawn-fallback.md`. Gate status itself is unchanged: G1x FAILED, v2.1 unfrozen, M4.1 blocked.

## M4 The C compiler (whole tool in C) (90–130 h incl. P0) — start only after Gate G3 (a game consumes assets)

Go is dropped in the same change in: ROADMAP.md M4, FEASIBILITY.md (G3 line + effort table), ARCHITECTURE.md (implementation plan), AGENTS.md rule 8, and the oracle docstring in `tools/arcforge_ref.py`.

### Premise

A standalone C CLI (`arcforge` binary) that replaces the Python reference as the shipped tool: one dependency-free binary, embeddable in the parent arcade1 toolchain. Measured per-asset latency (~0.28 s, ~80 % startup + validation) is **not** the motivation; embedding and batch scale are. The compiler is a host tool: only exported assets run on the console, so no FPGA/RISC-V claim is made for the compiler itself.

### Sequencing (read first)

1. **Do not start M4.1 before v2.1 is frozen.** The variable-size work (`spec-next`: X0 metrics/expression anchors/`percent` params, X1 `variants`/`mirror`/`repeat`) changes the language. Porting first means porting twice.
2. Order: X0 → X1 (only what level 1 needs) → X2 (hero archetypes) → Gate G1x → **P0 (below)** → M4.1 → M4.2 → M4.3.
3. G3 (a game consumes assets) stays. The owner may waive it explicitly in the card commit message; it is not waived by default.

### Contract

- `arcforge/c/` → `build/arcforge`, host `cc` (C11), **no external libraries**: bundled JSON parser (preserves key order, rejects duplicates and out-of-range ints), bundled SHA-256, integer-only raster, trig table from the generated header, bundled PNG writer (zlib "stored" blocks are acceptable; PNG bytes are not compared).
- Full CLI parity: `validate / render / test / golden / lint / list / describe`, including schema validation, `.px` parsing and full diagnostics parity (`E_*`, `path`, `observed`/`expected`, `suggestions` via the spec'd algorithm, JSON-Patch `fix`).
- Python stays the oracle with a **differential harness** in `make check`.
- No pixel behaviour changes; existing goldens untouched.

### Phases

| phase | scope | hours |
|---|---|---|
| M4.1 | JSON parser/writer, schema validation (hand-coded), registry load, validation + diagnostics parity incl. similarity | 35–50 |
| M4.2 | expression evaluator, shape build, rasterizer, outline post-pass, RGBA + SHA-256 frame hashes, `.px` | 30–40 |
| M4.3 | `render/test/golden/lint/list/describe` parity, differential harness, gate flip | 15–25 |

**Descope option** (if over budget): ship C `validate`, `render`, `golden` first; keep `lint`, `list`, `describe` on Python until a later card. The gate then flips per command, recorded in AGENTS.md.

### Acceptance (byte-identical)

1. Every `tests/invalid/*` report byte-identical to Python.
2. Frame RGBA bytes identical (PNG files not compared; goldens are frame hashes).
3. `tests/golden/*.json` byte-for-byte (knight + seraph).
4. All commands identical; `make check` ALL GREEN including the differential harness.
5. Two C runs byte-identical: no timestamps, locale, or unordered iteration; signed-integer behaviour defined by P0.3; builds clean with `-std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address` in CI.
6. Differential **fuzz**: N generated assets (random valid/invalid parameter mutations of the examples, fixed seed list in the repo) produce identical reports and frame hashes in both tools.

Only then does AGENTS.md rule 8 flip: C is the shipped tool; Python remains the cross-check until a cleanup card.

### Risks

- Hand-coded schema validation grows with the language (v2.1 adds many keys). Mitigation: one table-driven validator shared by asset/recipe/archetype/style schemas.
- Spec drift between P0 docs and Python behaviour. Mitigation: conformance tests are the spec.
- Export (`arcade1_asset`) is unchanged and stays after the C gate; it also needs the console asset format.

## P0 — Make Python portable (10–15 h, Python only, before any C)

Byte-identical parity is only achievable if the oracle's output is fully specified. Each item is a Python change plus a doc paragraph; goldens must not change.

- [x] **P0.1 — Own `E_SCHEMA` / `E_UNKNOWN_FIELD` messages.** Stop emitting the `jsonschema` library's message text. Fixed wording per failure kind (type, required, enum, pattern, min/max, additionalProperties, oneOf), stable ordering (path, then kind). *Why: the library's text cannot be reproduced in C.* Done: `schema_diags` composes every sentence from keyword + expected + instance (never `e.message`); unknown keys are computed structurally, not parsed out of prose; `oneOf` re-checks the branches to name the intended form; deduplicated, ordered (path, kind, message); vectors in `tests/test_schema_messages.py`; contract in `docs/AI_INTERFACE.md`.
- [x] **P0.2 — Replace regex patterns** in schemas with a tiny fixed pattern vocabulary (`identifier` — identifiers are at most 64 characters —, `dotted_identifier`, `semver`) implemented by hand in both tools; keep the schemas as documentation. *Why: no regex engine in a dependency-free C binary; the 64-character cap keeps every identifier below the 200-character threshold where `SequenceMatcher.autojunk` starts applying, so similarity behaves the same in both tools.* Done: all 38 `pattern` keywords replaced by `"format"` (`identifier`, `dotted_identifier`, `semver`, `category_path`, `parameter_name`, `diagnostic_code`); hand predicates + the schema walker (`_collect_formats`: properties, propertyNames, additionalProperties, prefixItems/items, `$ref`, oneOf/allOf, if/then/else, never `not`) in `tools/arcforge_ref.py`; vocabulary documented in `docs/LANGUAGE.md`; vectors in `tests/test_pattern_vocab.py`.
- [x] **P0.3 — Integer semantics doc.** Define floor division/modulo for negatives and the integer range (use 64-bit; reject values outside ±2^31 at parse time with a diagnostic). `rot_fixed` and raster arithmetic use floor division. *Why: Python `//` floors; C `/` truncates. Python ints never overflow; C ints do.* Done: `E_INT_RANGE` over a whole-document walk (every integer, `metadata` included; pointer, observed/expected, no fix — clamping is ambiguous) + parse-time expression-literal rejection (`ExprError` → `E_RECIPE_EXPR`/`E_ARCHETYPE_EXPR`, unary minus folded before the check); `docs/LANGUAGE.md` "Integer semantics (P0.3)"; vectors in `tests/test_int_range.py` (boundaries, skips, ordering, floor/`rot_fixed` pins, both integration codes, corpus scans) + negative fixture `tests/invalid/int_overflow.json`. Residual gap (exact Python vs 64-bit C on *results*; unchecked floats): `../../docs/experiments/arcforge-integer-overflow.md`.
- [x] **P0.4 — Generate the trig table** from Python into `c/gen/trig_table.h` (and a checked-in `tests/trig_table.json`); the C build never calls libm. Table = `int(round(cos|sin(deg)*1e6))`, round-half-even as Python does. *Why: libm and `round()` differences across platforms.* Done: `tools/gen_trig_table.py` (byte-deterministic, `--check` verifies without writing) emits both artifacts from the one formula; `tests/test_trig_table.py` pins header ↔ JSON ↔ oracle `COS`/`SIN` agreement for all 361 entries, staleness, the `--check` contract, reference angles, scale/guard invariants and the "no exact `.5` tie" fact; contract in `docs/ARCHITECTURE.md` (Determinism contract).
- [x] **P0.5 — JSON output spec** (`docs/OUTPUT_FORMAT.md`): insertion-ordered keys as emitted, `indent=2`, `ensure_ascii` behaviour, separators, string escaping, number formatting (floats only where Python emits them, e.g. `round(frac, 4)` → fixed decimal rule). Add a conformance test that re-serialises every report and compares. *Why: byte-identical reports need a defined writer.* Done: normative `docs/OUTPUT_FORMAT.md` (sinks + trailing-newline table, 8 writer rules, `_lit` compact fragments, determinism, full example); reference implementation `dump_json()`/`num4()` replaces every emission site (message strings now render floats as `0.2000..0.7000`, JSON number fields as `0.6904`); `load_json()` rejects `NaN`/`Infinity`/`-Infinity` at read time (same `JSONDecodeError` as malformed JSON — a report can never contain them); `tests/test_output_format.py` (74 vectors): independent writer from the document vs exact byte vectors, ref-vs-independent agreement, live corpus of every report shape round-tripped byte-for-byte + ASCII, golden/atlas/table sink round-trips, NaN rejection, source hygiene.
- [x] **P0.6 — Similarity spec.** Document the exact algorithm behind `close()` and the 0.85 fix threshold: `close(name, options)` is `difflib.get_close_matches(str(name), list(options), n=3, cutoff=0.6)` (every call site uses those defaults), which runs one reused `SequenceMatcher` with **seq1 = candidate, seq2 = word** (`set_seq2(word)`), Ratcliff/Obershelp ratio (longest match = leftmost in seq1, then leftmost in seq2; `real_quick_ratio`/`quick_ratio` are pre-filters only), keeps candidates with `ratio() >= 0.6`, and orders ties as `heapq.nlargest` on `(ratio, string)` (higher ratio first, then lexicographically **larger** string first). The 0.85 threshold is separate and lives only in the `E_UNKNOWN_RECIPE` fix: emit the replace-fix when there is exactly 1 suggestion, or `SequenceMatcher(None, name, suggestion).ratio() > 0.85` (strict `>`, argument order opposite to `get_close_matches`). Add unit vectors (`tests/similarity_vectors.json`). *Why: C must reproduce suggestions exactly.* Done: spec paragraph in `docs/AI_INTERFACE.md` (Diagnostic contract, incl. the other codes' fix rules and the autojunk precondition); `tests/similarity_vectors.json` + `tests/test_similarity.py`: pure `close()` vectors (cutoff-exact inclusion at 0.6, below-cutoff exclusion, tie order by larger string, `n=3` cap, options-order independence), pinned ratios incl. an asymmetric pair (order matters) and the exact 0.85 boundary, and four end-to-end cases driven through the real `compile_asset` on a real asset — `cape_shox` (2 suggestions, 0.8421 → no fix), `boots_heax` (2, 0.8571 → fix), `axe_simpx` (1, 0.8421 → fix), `energy_swrd` (the doc example); source hygiene (all 22 call sites use the defaults, `0.85` and the seq1 argument order single-sourced, registry names < 200 so autojunk never applies).
- [x] **P0.7 — Registry iteration order** pinned (sorted by path) and no dict/set order dependence anywhere in output. *Why: determinism contract.* Done: audit found all set→report paths already `sorted()` (or order-independent `close()`); spec paragraph in `docs/ARCHITECTURE.md` (Determinism contract); `tests/test_iteration_order.py`: load order == independent sorted-by-path walk for all three kinds, `list` report byte order pinned, every report command (`lint`, `list`, `describe`×3, `test`, `golden` read-only, `validate`, `render` + `atlas.json`, `sweep` + `table.json`) run under two different `PYTHONHASHSEED` values with byte-identical stdout and written files, a suggestions-bearing invalid fixture checked across seeds, `close(set) == close(sorted)`, and source pins on every audited `sorted()` call site.

Exit: `make check` ALL GREEN, goldens byte-identical to before P0, new conformance tests added.

## M5 AI tool interface (15–25 h)
MCP server over the CLI contract, JSON Patch application, vision review loop, prompt/skill packaging so any coding agent can drive it.

## Non-goals
General sprite editor, freehand AI drawing, runtime generation on the console, anything that cannot be tested deterministically.
