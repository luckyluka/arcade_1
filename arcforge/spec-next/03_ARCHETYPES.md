# 03 — Archetypes for a platformer level

> PROPOSED. Sizes are ranges, not constants. Every archetype declares `size`, `classes`, `metrics`, `anchors`, `layers`, `animations`, `min_cell`, and whether it is `symmetric`.

| archetype | used for | size params (range) | classes | key anchors |
|---|---|---|---|---|
| `humanoid_side` | hero (all power states), humanoid enemies | `height` 12–64 | tiny ≤16, small ≤28, medium ≤44, large | `origin, head, hand_f, hand_b, foot_f, foot_b, back, hat` |
| `walker` | simple ground enemy | `width` 10–32 | tiny, small, medium | `origin, eye_l, eye_r, foot_l, foot_r, top` |
| `shellback` | turtle-like enemy with hide/slide states | `width` 10–32 | tiny, small, medium | `origin, head, shell_top, foot_l, foot_r` |
| `tile` | ground, brick, blocks, pipes | `g` ∈ {8,16,32}, `cols`, `rows` (cell = g·cols × g·rows) | by `g` | `origin` (top-left), `top_edge` |
| `item` | coin, power-up, flower, fireball | `size` 8–32 | tiny, small | `origin, center` |
| `effect` | debris, puffs, popups, bump | `size` 8–64 | by size | `origin, center` |
| `scenery` | cloud, hill, bush, flagpole, castle | `width` 16–256, `height` 16–192 | by area | `origin` (bottom-left) |
| `font_bitmap` | HUD text | `glyph_w` 3–8, `glyph_h` 5–12 | — | per-glyph baseline |
| `icon` | lives, coin counter | `size` 6–16 | — | `origin` |

## humanoid_side (the demanding one)
- Parts: `head, torso, arm_f, arm_b, leg_f, leg_b` (front/back relative to facing right). Side view: asymmetric (`symmetric: false`); body mirrors only at runtime via `flip_x`.
- Templates: `idle_breathe(2) walk_cycle_6 run_cycle_4 skid(1) jump_up(1) jump_down(1) crouch(1) hurt(2) death(3) win(2) attack_slash(3)` — rigid offsets plus `swap` frames for leg poses.
- Power states are `variants` of one asset (small/big/fire = size + feature overrides + palette swap), never separate files with copied geometry.
- Recipes (initial): `cap_round`, `hair_short`, `overalls`, `gloves`, `boots_round`, `helmet_closed`, `cape_short`, `shield_round`, `energy_sword`, `moustache_block`, `eyes_dot`. Characters must be original designs, not recreations of existing franchises.

## walker / shellback
Parametric blob body (ellipse + `repeat` for feet), eyes from `eyes_*` recipes, `squashed` template via parameter tracks (height percent 100→40→0 in 3 frames). Shellback: `walk(2)`, `hide(1)` (head/feet recipes switched off with `swap`), `slide(4)` (shell pattern scrolls with a `phase` int param).

## tile (see 04)
Seamless by contract (`outline_override: "none"`, `tileable: {x,y}`), recipes `surface_grass`, `brick_courses`, `block_question` (4-frame glint by parameter track), `block_hard`, `pipe_part{top,body}` with `side` and `edges` bitmask params for autotiling.

## item / effect
`coin_spin` via `px 2` or parameter track on ellipse width (6,4,2,4); `debris_burst` uses a fixed scatter table `scatter(i)` indexed by particle number (no RNG); `puff_stomp` 3 expanding ellipses; `popup_text` composes glyphs from a `font_bitmap`.

## scenery
Large canvases (variable width/height are the point). Recipes built from `ellipse` + `repeat` + `mirror`: `cloud_puffs(count, width)`, `hill_round(width,height,eyes?)`, `bush_cluster(count)`, `flag_pole(height)`, `castle_small(towers)`. Cell = tight bounding box (auto mode).

## font_bitmap
Authored as one `.px` grid per font (`charset` string in order). Compiles to glyph rects + per-glyph advance. HUD text is rendered by the game from the atlas, not baked per string.

## Archetype document skeleton (excerpt)
```json
{ "archetype": "humanoid_side", "version": "1.0.0", "view": "side", "symmetric": false,
  "size": { "height": { "min": 12, "max": 64, "default": 32, "unit": "px" } },
  "classes": [ { "name": "tiny", "max": 16 }, { "name": "small", "max": 28 }, { "name": "medium", "max": 44 }, { "name": "large", "max": 64 } ],
  "proportions": { "tiny": { "p_head": 36 }, "small": { "p_head": 32 }, "medium": { "p_head": 28 }, "large": { "p_head": 24 } },
  "metrics": [ ["u", "max(1, floor($height/16))"], ["cx", "floor($W/2)"], ["foot_y", "$H-2"] ],
  "min_cell": [ "$height + 4", "$height + 2" ],
  "layers": ["back","body","armor","held","foreground","fx"],
  "parts": { "...": "shape lists using expressions" },
  "anchors": { "origin": { "part": "root", "at": ["$cx", "$foot_y"] } },
  "animations": { "walk_cycle_6": { "fps": 10, "loop": true, "frames": [ { "offsets": {}, "swap": {} } ] } }
}
```
