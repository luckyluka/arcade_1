# ArcForge

A **bounded visual language** that an AI can program, plus a deterministic compiler that turns it into pixel-art assets for ARCADE-1.

ArcForge is not an AI that draws sprites. The AI writes strict semantic JSON; ArcForge either knows how to build it (archetype, recipe, parameters) or says so and asks for an explicit `.px` fragment.

```
AI ── semantic JSON ──▶ validate ──▶ compile ──▶ rasterize ──▶ tests ──▶ export
                          │
        unknown vocabulary? ──▶ structured diagnostic + suggestions + JSON Patch fix
```

## Status
M0 done: spec + executable reference implementation (Python) rendering a 32×32 hero with idle/walk/attack frames, deterministic, golden-tested. See `docs/FEASIBILITY.md` for the honest assessment and `docs/ROADMAP.md` for what is next.

`spec-next/` holds the **PROPOSED** v2.1 extended spec (variable size, language extensions, level-1 slice). X0 and X1 implemented the subsets marked in `spec-next/01_VARIABLE_SIZE.md` and `02_LANGUAGE_EXTENSIONS.md`; X2 added the `humanoid_side` archetype, the `platformer_24` style pack, six humanoid recipes and the one-file hero `examples/hero.knight.json` (`small`/`big`/`fire` via `variants`), marked in `spec-next/03_ARCHETYPES.md`. The rest is not implemented. Run `make spec-check` to check its internal consistency.

**Shaded look (implemented, outside the X roadmap):** 5-tone ramps, shaded fills (`"fill": "$ramp.*"`), the `capsule` shape, per-frame joint `rotate`, the `shaded_15` style pack (15-colour bank: outline + steel + team + skin) and the 96×96 `knight_front` archetype with `examples/knight.blue.json`. Reference: `docs/LANGUAGE.md`. Not done: weapon recipes for `knight_front`, the other heroes, and a check of the 15-colour banks against the ARCADE-1 asset compiler.

## Quickstart
```bash
pip install jsonschema
make check            # lint registry, run assertions, golden hashes, negative fixtures
make render           # out/hero.iron_knight/preview.png (+ frames, spritesheet, atlas.json)
python3 tools/arcforge_ref.py list recipes
python3 tools/arcforge_ref.py describe recipe energy_sword
python3 tools/arcforge_ref.py validate tests/invalid/unknown_recipe.json   # see a diagnostic
```

## The five rules
1. **No fake executability.** Every field either changes the output or is declared metadata. Unknown fields are errors (`E_UNKNOWN_FIELD`), never ignored.
2. **Free text is metadata.** `description` and `metadata` never control rendering. `"visual": "make him look battle-worn"` is rejected; use features/params.
3. **Hierarchy:** archetype (skeleton) → recipes with typed parameters (exact construction) → `.px` fragments (escape hatch for what the vocabulary cannot express).
4. **Deterministic.** Same input + same registry version = byte-identical pixels. Integer/fixed-point math only. No RNG.
5. **The engine explains itself.** Unknown names return suggestions and a machine-applicable fix. The AI discovers the vocabulary by asking (`list`, `describe`).

## Layout
```
docs/        FEASIBILITY, ARCHITECTURE, LANGUAGE, RECIPES, PX_FORMAT, AI_INTERFACE, TESTING, ROADMAP, DECISIONS
schema/      asset (strict, AI-facing), recipe, archetype, style_pack, diagnostics
registry/    archetypes/, recipes/<category>/, styles/   <- the content; this is the product
examples/    hero.iron_knight.json + fragments/claw.px
tests/       run_tests.py, invalid/ (negative fixtures), golden/ (pixel hashes)
tools/       arcforge_ref.py  (reference implementation = executable spec)
AGENTS.md    rules for AI coding agents working in this repo
```

## Reading order
FEASIBILITY → LANGUAGE → RECIPES → ARCHITECTURE → AI_INTERFACE → ROADMAP.
