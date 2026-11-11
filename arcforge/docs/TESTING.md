# Testing

| layer | what | how |
|---|---|---|
| registry lint | every archetype/recipe/style matches its schema; every recipe example compiles, stays palette-legal, and does not clip | `make lint` |
| negative fixtures | each failure mode yields exactly its expected error code(s) | `tests/invalid/*.json` + `expectations.json` |
| asset assertions | structural facts about the compiled asset | `tests` block in the asset |
| golden hashes | per-frame SHA-256 of RGBA | `tests/golden/*.json`, `make golden` |
| human/AI visual review | does it look good? | `preview.png`; not automatable |

## Why no vision-model tests
The compiler placed every shape, so "the sword exists" is an IR fact, not a perception problem. Visual quality is reviewed, not asserted. Gate G1 (FEASIBILITY.md) is a blind human review.

## Assertion vocabulary
`dimensions {expected}`, `palette_size {max}`, `feature_present {recipe}`, `silhouette_area {min,max}` (fraction of canvas), `bbox_margin {min}` (opaque pixels incl. outline, all frames), `anchor_inside_canvas {anchor}`, `root_stable {max}`, `frame_count {max}`.

## Updating goldens
Only after you rendered and looked. A golden diff is a visual change; treat it like a screenshot review.
