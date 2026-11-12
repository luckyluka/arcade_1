# ArcForge v2.1 — extended spec (PROPOSED)

> **Status: proposed.** Nothing here is implemented by `tools/arcforge_ref.py` except what `prototype/` demonstrates.
> Per `AGENTS.md` rule 1, none of these fields may appear in `schema/`, `registry/` or `examples/` until the compiler implements them.
> v2.0 assets keep compiling unchanged; v2.1 is additive.

Goal: produce all assets for a small platformer level (hero in several power states, enemies, tiles, items, scenery, effects, HUD font) **with canvas and hero size as parameters, never constants**, and improve art quality iteratively and measurably.

| doc | content |
|---|---|
| 01_VARIABLE_SIZE.md | the size model: cell, body size, metrics, size classes, parity, auto canvas, size sweep. **Read first.** |
| 02_LANGUAGE_EXTENSIONS.md | expressions, shapes (`mirror`, `line`, `repeat`, `px`), `percent` params, `extends`/`variants`, animated `.px`, parameter tracks, assertions |
| 03_ARCHETYPES.md | archetypes needed for level 1 |
| 04_TILES_TILESETS_LEVELS.md | tiles, autotiling, atlas packing, level format, level preview |
| 05_QUALITY_AND_ITERATION.md | style bible, review loops, automatic quality metrics, versioning |
| 06_PROJECT_STRUCTURE.md | folder layout and `arcforge.project.json` |
| 07_MILESTONES.md | work breakdown, acceptance criteria, effort |
| schema/ | proposed JSON Schemas (asset 2.1, archetype 2.1, project, tileset, level) |
| examples/ | proposed documents, validated against the proposed schemas |
| prototype/ | working proof of the size model (runs today) |

Run `make spec-check` from the repo root.
