# Decisions

| # | decision | reason |
|---|---|---|
| 1 | Bounded declarative language, not prompts | AI-friendly must not mean hallucination-friendly |
| 2 | Free text is metadata only | No field may pretend to be executable |
| 3 | Archetype → recipes(params) → `.px` | Finite vocabulary, huge design space, honest escape hatch |
| 4 | Unknown names return suggestions + JSON Patch fixes | The engine teaches the AI its own vocabulary |
| 5 | Integer/fixed-point rasterization | Bit-exact across the Python and C implementations, golden-testable |
| 6 | Screen-relative l/r | Avoids anatomical left/right bugs |
| 7 | Rigid translation animation first | Rotation of pixel art is hard to make look good; add variants later |
| 8 | Python reference before the C compiler (M4) | Spec still moving; goldens make the port verifiable |
| 9 | Palette roles (ramp.tone), no raw RGB | Style packs can restyle everything |
| 10 | Structural tests instead of vision tests | The compiler knows what it placed |
| 11 | Image models only as optional `.px` producers | Keeps the core deterministic |
| 12 | Go port replaced by a standalone C compiler (M4) | Embedding in the arcade1 toolchain, dependency-free binary |
| 13 | Keyed recipe geometry: one array = shared, object keyed by archetype name | One recipe file serves fixed-art and size-parametric bodies; a key missing from `applies_to` or vice versa is `E_RECIPE_GEOMETRY`, never a silent drop |
| 14 | §H quality assertions are structural and per asset (7 of 9 in X1) | Same principle as #10: the compiler knows what it placed. `contrast_min`/`silhouette_distinct` need project data (backgrounds, peer assets) and wait for the X4 project file |
| 15 | `min_feature` counts every compiled shape | `affects: ["detail"]` (02 §D) is documentation-only — no runtime marker exists — so the conservative reading fails loudly instead of silently skipping art |
| 16 | The X1 showcase is `extends` + one variant, not a new base example | Exercises §E (extends, variants, palette_swap) on real files while every shipped example and golden stays byte-identical; the variant re-points the document at an inventory archetype |

## v1 spec → v2 fate
| v1 element | fate |
|---|---|
| `generation.prompt`, `seed`, `reference_strength`, image-model backend | removed from core; optional `.px` importer later |
| `visual.*`, `style.avoid`, `anatomy.*` free-text/adjective fields | removed (`E_UNKNOWN_FIELD`); replaced by archetype/recipes/params |
| vision-backed semantic tests | replaced by structural assertions on the compiled asset |
| reference_similarity ≥ 0.9 | only for imported/`.px` art or own golden regression |
| `additionalProperties: true` everywhere | `false` everywhere except `metadata` |
| 100×100 sprite assumption | 32×32 slice; canvas per archetype |
