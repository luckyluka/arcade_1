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

## M4 Port + export (40–60 h) — start only after Gate G3 (a game consumes assets)
Go implementation matching goldens bit-for-bit; `arcade1_asset` exporter once the ARCADE-1 sprite/tile format is fixed.

## M5 AI tool interface (15–25 h)
MCP server over the CLI contract, JSON Patch application, vision review loop, prompt/skill packaging so any coding agent can drive it.

## Non-goals
General sprite editor, freehand AI drawing, runtime generation on the console, anything that cannot be tested deterministically.
