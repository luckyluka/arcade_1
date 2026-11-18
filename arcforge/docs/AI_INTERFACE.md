# AI interface

The AI drives ArcForge through a small tool surface (CLI now, MCP server in M5). Every tool returns JSON matching `schema/diagnostics.schema.json`.

## Tools
| tool | CLI today | purpose |
|---|---|---|
| list_vocabulary | `list [archetypes\|recipes\|styles]` | discover what exists |
| describe | `describe recipe\|archetype\|style NAME` | parameters, ranges, anchors, slots |
| validate | `validate ASSET` | schema + registry checks, no rendering; also accepts a style-pack document (detected by `style_pack` + `ramps` without `arcforge`) to check `outline.inner.parts` against the registry |
| render | `render ASSET --out D` | frames, spritesheet, atlas, preview PNG for visual review |
| test | `test ASSET` | validate + render + assertions |
| golden | `golden ASSET [--update]` | regression hashes |
| patch (M5) | RFC 6902 against the asset | apply a `fix` from a diagnostic |
| import_px (M2) | PNG → `.px` | produce fragments |

## Diagnostic contract
```json
{ "code": "E_UNKNOWN_RECIPE", "severity": "error", "path": "/features/2/recipe",
  "message": "NO RECIPE EXISTS named 'energy_swrd'",
  "expected": ["boots_heavy", "cape_torn", "energy_sword", "..."],
  "suggestions": ["energy_sword"],
  "fix": [ { "op": "replace", "path": "/features/2/recipe", "value": "energy_sword" } ] }
```
- `path` is an RFC 6901 JSON Pointer into the asset.
- `fix` is an RFC 6902 patch, present only when unambiguous.
- `suggestions` come from one fixed algorithm, reproduced exactly by the C
  compiler (M4): `close(name, options)` is
  `difflib.get_close_matches(str(name), list(options), n=3, cutoff=0.6)` —
  every call site uses those defaults. It scores each candidate as **seq1**
  against the query as **seq2** with the Ratcliff/Obershelp ratio (longest
  match, leftmost in seq1 then leftmost in seq2; the quick ratios are
  pre-filters only), keeps candidates with `ratio >= 0.6`, returns at most
  3, and orders ties by the lexicographically **larger** string first — so
  the result never depends on the order of `options`. The 64-character
  identifier cap (P0.2) keeps every input under difflib's 200-character
  autojunk threshold, so Python and C see the same matcher.
- The `E_UNKNOWN_RECIPE` fix threshold is separate from the suggestion
  cutoff: the replace-fix is attached when there is **exactly 1**
  suggestion, or when
  `SequenceMatcher(None, name, suggestion).ratio() > 0.85` — strict `>`,
  and the argument order is the *opposite* of `get_close_matches` (the
  query is seq1 here; the ratio is not symmetric). Other codes attach a
  fix when there is exactly one suggestion (`E_UNKNOWN_SWAP_SET` falls
  back to a remove-fix), or on any suggestion (`E_RECIPE_GEOMETRY`
  add-fix, no fix when there is none; `E_UNKNOWN_PART` replace, falling
  back to remove; the stray-geometry-key remove-fix is unconditional).
  Vectors: `tests/similarity_vectors.json`, run by
  `tests/test_similarity.py`.
- Codes are stable. Warnings never fail a run.
- `E_SCHEMA` / `E_UNKNOWN_FIELD` wording is produced by the tool, never by a
  library: one fixed sentence per failure kind — `expected <type>, got <value>`,
  `'<key>' is a required property`, `'<value>' is not a valid <identifier |
  dotted identifier | semver | category path | parameter name | diagnostic
  code>` (the name vocabulary, checked by hand — see `docs/LANGUAGE.md`),
  `expected one of [...], got ...`, `expected >= <n>, got <v>`, `expected at
  most <n> entries, got <m>` — and `oneOf` failures name the intended form (the
  branch that only lacks keys, the deepest disagreement, or the merged shapes:
  `expected array or object, got string`). Diagnostics from one schema check
  are deduplicated and ordered by (path, kind, message), so the same document
  always yields the same report; the C compiler (M4) must reproduce it
  byte-for-byte. Vectors: `tests/test_schema_messages.py`,
  `tests/test_pattern_vocab.py`.

| code | meaning |
|---|---|
| E_SCHEMA / E_UNKNOWN_FIELD | structure wrong / field not executable |
| E_UNKNOWN_ARCHETYPE / STYLE_PACK / RECIPE / ANIMATION_TEMPLATE / LAYER | name not in the registry |
| E_UNKNOWN_PARAM / E_PARAM_TYPE / E_PARAM_RANGE / E_PARAM_ENUM | parameter problems |
| E_UNKNOWN_PART | style pack `outline.inner.parts` names a part no archetype defines |
| E_RECIPE_NOT_APPLICABLE / E_SLOT_CONFLICT / E_MISSING_ANCHOR / E_CANVAS_UNSUPPORTED | composition problems |
| E_PX_NOT_FOUND / E_PX_PARSE / E_PX_LEGEND | fragment problems |
| E_RECIPE_EXPR / E_ARCHETYPE_EXPR | registry content bug (fix the registry, not the asset) |
| E_INT_RANGE | integer written in the document or in an expression literal is outside `-2147483648..2147483647` (P0.3) |
| E_PALETTE_BUDGET / E_TEST_FAILED / E_GOLDEN_MISMATCH | output checks |
| W_CLIPPED | pixels fell outside the canvas |

## Recommended agent loop
1. `list recipes` / `describe` for the archetype → pick features.
2. Write the asset. `validate`; apply `fix` patches; repeat until PASS.
3. `render`, **look at preview.png** (the AI reviews visually; structure is already guaranteed).
4. If an idea has no recipe: `custom` px_fragment, or propose a new recipe (human reviews it).
5. `test`, then `golden --update` only after intentional visual changes.
