# ArcForge v2.1 — extended spec (PROPOSED)

> **Status: proposed, partially implemented.** X0 implemented the size model subset marked
> at the top of `01_VARIABLE_SIZE.md` and `02_LANGUAGE_EXTENSIONS.md`; those fields now
> live in `schema/`, `registry/` and `examples/` of the main tool (an implemented section
> stops being purely proposed). Everything not marked there is still unimplemented, and
> per `AGENTS.md` rule 1 it may not appear in `schema/`, `registry/` or `examples/`.
> v2.0 assets keep compiling unchanged; v2.1 is additive.

Goal: produce all assets for a small platformer level (hero in several power states, enemies, tiles, items, scenery, effects, HUD font) **with canvas and hero size as parameters, never constants**, and improve art quality iteratively and measurably.

| doc | content |
|---|---|
| 01_VARIABLE_SIZE.md | the size model: cell, body size, metrics, size classes, parity, auto canvas, size sweep. **Read first.** Implemented by X0 except where its status line says otherwise |
| 02_LANGUAGE_EXTENSIONS.md | expressions, shapes (`mirror`, `line`, `repeat`, `px`), `percent` params, `extends`/`variants`, animated `.px`, parameter tracks, assertions — `percent`/expressions/anchors/diagnostics live (see its status line), the rest proposed |
| 03_ARCHETYPES.md | archetypes needed for level 1 |
| 04_TILES_TILESETS_LEVELS.md | tiles, autotiling, atlas packing, level format, level preview |
| 05_QUALITY_AND_ITERATION.md | style bible, review loops, automatic quality metrics, versioning |
| 06_PROJECT_STRUCTURE.md | folder layout and `arcforge.project.json` |
| 07_MILESTONES.md | work breakdown, acceptance criteria, effort |
| schema/ | proposed JSON Schemas (asset 2.1, archetype 2.1, project, tileset, level) |
| examples/ | proposed documents, validated against the proposed schemas |
| prototype/ | historical proof of the size model; the live sweep is `tools/arcforge_ref.py sweep` and `make spec-check` runs both |

Run `make spec-check` from the repo root.
