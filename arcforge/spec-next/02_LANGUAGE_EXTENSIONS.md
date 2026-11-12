# 02 — Language extensions

> PROPOSED (v2.1). Document version `"arcforge": "2.1"`; `"2.0"` documents compile unchanged.

## A. Expressions
Additions to the v2.0 language: variables `$W $H $class $class_idx`, size parameters, metrics, recipe `locals`, repeat variables; functions `odd even half sign`; string `in` is **not** added (keep the language small). Still total, no loops, no user functions. Locals: recipe `"locals": [["boot_h","max(2, floor($leg_h/3)+1)"]]`, ordered, integer-floored, visible only inside the recipe.

## B. Anchors as expressions
Archetype anchors: `"hand_r": { "part": "arm_r", "at": ["$cx+$half_t+1+half($arm_w)", "$torso_top+$arm_h-1"] }`. Evaluated once per asset after metrics.

## C. Shapes
| shape / flag | meaning | determinism |
|---|---|---|
| `"mirror": "x"` | also draw the reflection about `$cx` (archetype may rename the axis metric via `mirror_axis`) | exact integer reflection |
| `line {x1,y1,x2,y2,thickness}` | integer Bresenham with square brush; thickness must be odd | integer only |
| `repeat {var,count,step_x,step_y,max,shapes[]}` | bounded loop; `$i` available; `count` must evaluate to ≤ `max`; `max` ≤ 256; nesting ≤ 2 | fixed iteration order; `E_REPEAT_BOUND` |
| `px {file, anchor?, flip?, when?}` | draw a fixed `.px` fragment inside a recipe (hand-tuned detail); size-specific files selected with `when` | as `.px` |
| `rotate` | unchanged (rect/poly only) | integer-degree table |

## D. Parameter types
`int`, `bool`, `enum`, `ramp` (unchanged) and `percent` (01 §7). A parameter may declare `"affects": ["silhouette"|"detail"|"color"]` (documentation for AI and lint only).

## E. Derived assets
**`variants`** — several assets from one file; each entry is an id plus an RFC 7386 JSON Merge Patch over the base document (arrays are replaced whole):
```json
"variants": [
  { "id": "hero.knight.big",  "override": { "archetype": { "size": { "height": 32 } }, "features": ["helmet_closed", "shoulder_armor_heavy", "boots_heavy"] } },
  { "id": "hero.knight.fire", "from": "hero.knight.big", "palette_swap": { "red": "gold" } }
]
```
**`extends`** — `"extends": "characters/hero.knight.small.json"` plus top-level overrides (same merge rule); depth ≤ 3; cycle → `E_EXTENDS_CYCLE`.
**`palette_swap`** — maps ramp names; applied when fills resolve; both ramps must exist (`E_UNKNOWN_RAMP`). Power-up colour variants cost zero art.
Compiled ids must be unique project-wide (`E_DUPLICATE_ID`).

## F. Animation
- **Parameter tracks**: an animation template frame may override parameters: `{ "offsets": {...}, "params": { "coin_w": 6 } }`. Enables spin/squash/pulse without rotation. Overridable: archetype size parameters (clamped to range) and archetype/recipe parameters of kinds `int`, `bool`, `enum`, `percent`.
- **Variant frames** (part swaps): `{ "swap": { "head": "head_hurt" } }` where the archetype defines alternative part shape sets.
- **Timing**: per-frame `hold` (frames at template fps, default 1). Hit frames and `phases` as in v2.0.
- **Required templates for a playable hero**: `idle, walk, run, skid, jump_up, jump_down, crouch, attack?, hurt, death, win`.
- `facing` is **runtime metadata**: art is authored facing right; the atlas carries `facing: "right"`; left = flip_x in the game. No duplicated art.

## G. `.px` v2
```
px 2
name coin
size 8 10
frames 4
fps 10
anchor attach 4 9
legend
  . transparent
  y gold.base
  l gold.light
frame 0
  ...rows...
frame 1
  ...rows...
```
All frames share size and legend; `frames N` must match; v1 files stay valid. Errors: `E_PX_FRAMES`.

## H. New assertions
| assert | args | meaning |
|---|---|---|
| `baseline_stable` | anchor, max | anchor y identical (± max) across frames (feet don't float) |
| `pivot_stable` | max | origin inside cell stable across frames |
| `min_feature` | min | smallest filled bbox dimension of non-`detail` shapes ≥ min |
| `cell_fill` | min, max | opaque pixel ratio of the cell |
| `seamless` | axes | tile has no outline/transparent pixels on its tileable borders |
| `contrast_min` | against[], min | luminance contrast of silhouette vs listed background colours |
| `silhouette_distinct` | from[], max_iou | IoU with other assets' base silhouettes below threshold |
| `jitter_max` | max | centre-of-mass step between consecutive frames |
| `orphan_pixels` | max | isolated single pixels (4-neighbourhood) count |

## I. Layers
Default order grows to `back, body, armor, held, foreground, fx`; scenery and tiles use `bg, mid, fg`. Layer sets are per archetype.

## J. Diagnostics (new codes)
`E_CELL_TOO_SMALL, E_SIZE_RANGE, E_UNKNOWN_SIZE_PARAM, E_CLIPPED, E_RECIPE_SIZE_RANGE, E_REPEAT_BOUND, E_EXTENDS_CYCLE, E_DUPLICATE_ID, E_PX_FRAMES, E_TRACK_PARAM, W_FEATURE_TOO_SMALL, W_OCCLUDED`. Every error keeps the v2.0 contract: JSON-pointer `path`, `observed/expected`, `suggestions`, JSON Patch `fix` when unambiguous.
