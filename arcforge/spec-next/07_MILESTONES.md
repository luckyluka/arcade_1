# 07 — Milestones for the level-1 slice

> PROPOSED. Estimates are focused hours, ±50 %.

## Level-1 asset inventory (original designs)
| group | contents | frames |
|---|---|---|
| hero | small, big, fire variants: idle, walk(6), run(4), skid, jump up/down, crouch, hurt, death, win, grow transition | 60–90 |
| enemies | walker (walk 2, squash 1), shellback (walk 2, hide 1, slide 4) | 12 |
| tiles | ground top/fill, brick, question block (4), used block, hard block, pipe top/body ×2 sides | 16–20 |
| items | coin (4), power-up (2), flower (4), fireball (4) | 14–16 |
| scenery | cloud ×2, hill ×2, bush ×2, flagpole + flag (2), small castle | 10 |
| fx | brick debris (4), stomp puff (3), block bump, popups | 8–12 |
| UI | 5×7 font (~45 glyphs), 3 icons | ~50 |
Total ≈ 40–50 assets, 170–230 frames.

## Work packages
| id | package | hours | exit criteria |
|---|---|---|---|
| X0 | variable-size core: metrics, expression anchors, `odd/even/half`, `percent`, size classes + proportions, auto canvas, `min_cell`, size sweep lint | 20–30 | prototype becomes the real compiler path; sweep passes on `humanoid_front_scalable` |
| X1 | language extensions: `mirror`, `line`, `repeat` (+ `%`), `px` shape, `locals`, `variants`/`extends`/`palette_swap`, `px 2`, parameter tracks, swap frames, new assertions | 25–40 | every example in `spec-next/examples` compiles; negative fixtures per new `E_*` code |
| X2 | archetypes: `humanoid_side`, `walker`, `shellback`, `tile`, `item`, `effect`, `scenery`, `font_bitmap`, `icon` | 40–70 | each renders at min/mid/max size, sweep passes, one reference asset each |
| X3 | atlas packer, level format, level-lint, level-preview, contact-sheet, diff-render | 20–35 | level 1 composites from real compiled assets; atlas deterministic |
| X4 | quality metrics, `impact`, `report`, review templates | 15–25 | metrics report on the whole project; thresholds in project file |
| X5 | content + iteration for level 1 (recipes, tuning, reviews) | 60–120 | blind review passes; level preview accepted |
Total ≈ 180–320 h. Content (X5) is the part a human artist would also have to do.

## Order
X0 → X1 (only what X2 needs) → X2 for the hero first → **Gate G1x** → remaining archetypes → X3 → X4 → X5 continuous.
**Gate G1x:** hero small + big compile from one file with `variants`, sweep passes across its declared range, blind rubric ≥ 3.5, `fire` costs only a `palette_swap`. If it fails, stop and fall back to hand-drawn `.px` for the hero; keep ArcForge for tiles, atlas, palette, level preview and quality tooling.

## Risks
1. Taste (high): recipes look generic. Mitigation: authored size classes, `px` shape for hand-tuned detail, style bible, rubric.
2. Scope (medium): 9 archetypes. Mitigation: build only the ones level 1 uses; tiles/icons/font may stay mostly `.px`.
3. Parity/size bugs (medium): covered by the size sweep from day one.
4. Tooling creep (medium): contact sheet and level preview first; the rest only if used.

## Deferred
Blob-47 autotiling, rotation/squash animation, audio, runtime generation, ARCADE-1 export (blocked on the asset format), multiple levels.
