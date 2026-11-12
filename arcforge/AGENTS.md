# Instructions for AI coding agents

Read README.md and docs/LANGUAGE.md first. Then:

1. **Never invent semantics.** If a field or recipe is not implemented in the compiler, it must not exist in a schema, a recipe, or an example. Fake executable fields are bugs.
2. **Run `make check` before and after every change.** It must end with `ALL GREEN`.
3. **Intentional pixel changes** (recipe geometry, archetype shapes, style colours) change golden hashes. Run `make render`, LOOK at `out/hero.iron_knight/preview.png`, and only then `make golden`. Never update goldens to silence a failure you do not understand.
4. **Determinism is a hard requirement.** Integer/fixed-point only in the rasterizer. No floats in raster decisions, no random, no dict-order dependence, no timestamps. Trig only via the integer-degree table.
5. **New recipe checklist** (docs/RECIPES.md): file under registry/recipes/<category>/, at least 2 `examples` (defaults + extremes), `make lint` clean (no `W_CLIPPED`), param ranges chosen so every combination stays inside the canvas, golden + at least one assertion in an example asset.
6. **Errors must be useful to an AI:** every new validation failure needs a stable `E_*` code, a JSON-pointer `path`, `observed`/`expected` where meaningful, `suggestions` where a close match exists, and a JSON Patch `fix` when the fix is unambiguous. Add a negative fixture in tests/invalid/ with its expected code in expectations.json.
7. **Do not touch** `schema/*.json` semantics without updating docs/LANGUAGE.md and the reference tool in the same change.
8. The Python tool is the oracle. The C compiler (ROADMAP M4) must reproduce `tests/golden/*.json` and every diagnostics report byte-for-byte before it replaces the Python tool.
9. `spec-next/` is **PROPOSED** (v2.1 extended spec). Nothing in it may appear in `schema/`, `registry/` or `examples/` until a roadmap milestone implements it; check it with `make spec-check`.
