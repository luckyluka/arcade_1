# AI interface

The AI drives ArcForge through a small tool surface (CLI now, MCP server in M5). Every tool returns JSON matching `schema/diagnostics.schema.json`.

## Tools
| tool | CLI today | purpose |
|---|---|---|
| list_vocabulary | `list [archetypes\|recipes\|styles]` | discover what exists |
| describe | `describe recipe\|archetype\|style NAME` | parameters, ranges, anchors, slots |
| validate | `validate ASSET` | schema + registry checks, no rendering |
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
- Codes are stable. Warnings never fail a run.

| code | meaning |
|---|---|
| E_SCHEMA / E_UNKNOWN_FIELD | structure wrong / field not executable |
| E_UNKNOWN_ARCHETYPE / STYLE_PACK / RECIPE / ANIMATION_TEMPLATE / LAYER | name not in the registry |
| E_UNKNOWN_PARAM / E_PARAM_TYPE / E_PARAM_RANGE / E_PARAM_ENUM | parameter problems |
| E_RECIPE_NOT_APPLICABLE / E_SLOT_CONFLICT / E_MISSING_ANCHOR / E_CANVAS_UNSUPPORTED | composition problems |
| E_PX_NOT_FOUND / E_PX_PARSE / E_PX_LEGEND | fragment problems |
| E_RECIPE_EXPR / E_ARCHETYPE_EXPR | registry content bug (fix the registry, not the asset) |
| E_PALETTE_BUDGET / E_TEST_FAILED / E_GOLDEN_MISMATCH | output checks |
| W_CLIPPED | pixels fell outside the canvas |

## Recommended agent loop
1. `list recipes` / `describe` for the archetype → pick features.
2. Write the asset. `validate`; apply `fix` patches; repeat until PASS.
3. `render`, **look at preview.png** (the AI reviews visually; structure is already guaranteed).
4. If an idea has no recipe: `custom` px_fragment, or propose a new recipe (human reviews it).
5. `test`, then `golden --update` only after intentional visual changes.
