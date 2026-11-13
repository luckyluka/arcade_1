# 01 — Variable size

> PROPOSED (v2.1). Evidence: `prototype/size_sweep.py` renders one archetype at four hero heights.

> **Status (X0, implemented in `tools/arcforge_ref.py`)**: §1–§5, §7 and §9 are live —
> exercised by `registry/archetypes/humanoid_front_scalable.archetype.json`, its recipes
> and `make check`'s sweep. §6 is partial (`symmetric` and the odd-width convention are
> real; the `mirror` shape flag is not), §3's `size.unit` is not accepted by the schema,
> §8/§10 are findings and prose. §9's "recipe stays on its slot region" check is not
> implemented. The document stays PROPOSED until every section is.

## 1. Three sizes, never conflated
| name | meaning | where declared |
|---|---|---|
| **cell** | pixel rectangle of one sprite frame (the canvas) | asset `target.canvas` |
| **body size** | the character's own scale: one or more *size parameters* (e.g. `height`) | asset `archetype.size`, ranges in the archetype |
| **unit `u`** | derived stroke/detail unit for thickness, gaps, steps | archetype `metrics` |

A 16 px hero may live in a 16×16, 24×24 or auto-fitted cell. The cell never changes how the body is built; it only has to contain it.

```json
"target":    { "profile": "arcade1-draft", "canvas": { "mode": "auto", "margin": 1, "multiple_of": 1, "align": "pivot_bottom_center" }, "clip_policy": "error" },
"archetype": { "name": "humanoid_side", "size": { "height": 30 }, "params": { "armor_ramp": "red" } }
```

## 2. Canvas modes
- **Fixed**: `"canvas": [w, h]`. Must satisfy the archetype's `min_cell(size)`; else `E_CELL_TOO_SMALL` with the minimum in `expected` and a JSON Patch fix.
- **Auto**: `{ "mode": "auto", "margin": m, "multiple_of": k, "align": "pivot_bottom_center" }`. The compiler renders every frame of every animation on an unbounded plane, takes the union bounding box (outline included), adds `margin`, rounds up to a multiple of `k`, and fixes one cell for the whole asset so animations share a frame size. The origin anchor position inside the cell is exported as `pivot`.
- `clip_policy`: `error` (default; `W_CLIPPED` becomes `E_CLIPPED`) or `warn`. Auto mode can never clip.
- The game aligns sprites by `pivot` from the atlas, never by cell size, so assets of different sizes mix freely.

## 3. Archetype size contract
```json
"size":    { "height": { "min": 12, "max": 64, "default": 32, "unit": "px" } },
"classes": [ { "name": "tiny", "max": 16 }, { "name": "small", "max": 28 }, { "name": "medium", "max": 44 }, { "name": "large", "max": 64 } ],
"min_cell": [ "$torso_w + 2*$arm_w + 2*$sword_reach + 2", "$height + 2" ]
```
Out-of-range size → `E_SIZE_RANGE` (with clamp fix). Unknown size parameter → `E_UNKNOWN_SIZE_PARAM`.

## 4. Metrics: derived integers
`metrics` is an ordered list of `[name, expression]`. Environment: `$W $H` (resolved cell), size parameters, `$class` (string), `$class_idx` (0-based), earlier metrics. Every metric is floored to an integer. Anchors (`at`) and shape fields may be expressions.
```json
"metrics": [
  ["u",       "max(1, floor($height/16))"],
  ["head_h",  "max(4, round($height*$p_head/100))"],
  ["torso_w", "odd(round($height*55/100))"],
  ["cx",      "floor($W/2)"],
  ["foot_y",  "$H-2"]
]
```
Rules: no floats in the source of truth: write ratios as integer percentages (`*30/100`). Division is IEEE true division then floored; results at exact `.5` are representable so `round` is deterministic. Functions added in 2.1: `odd(x)` (next odd ≥ x), `even(x)`, `half(x)` = `floor(x/2)`.

## 5. Proportions per size class
Pure scaling gives mannequins (see §8). Archetypes therefore carry percentage tables per class:
```json
"proportions": {
  "tiny":   { "p_head": 36, "p_leg": 26, "p_torso_w": 60 },
  "small":  { "p_head": 32, "p_leg": 28, "p_torso_w": 56 },
  "medium": { "p_head": 28, "p_leg": 30, "p_torso_w": 52 },
  "large":  { "p_head": 24, "p_leg": 32, "p_torso_w": 50 }
}
```
The row for the resolved class is injected as `$p_*`. Small heroes get big heads (readable face), large ones get realistic ratios. Tables are authored art decisions, tuned at class representatives.

## 6. Parity and symmetry
- Bodies are built with **odd widths** around a single centre column `cx = floor(W/2)`. `odd()` enforces it.
- Shape flag `"mirror": "x"` draws the shape and its reflection about `$cx` (see 02). Gaps and limbs use odd gaps.
- With an even cell width the body sits half a pixel off the cell centre. This is accepted and documented: the **pivot** (`origin` anchor at `cx`) is the alignment reference.
- Archetypes declare `"symmetric": true|false`; the size sweep asserts exact mirror symmetry of the body when true.

## 7. Percent parameters and size-aware recipes
Recipe lengths follow the hero: `{ "type": "percent", "of": "height", "default": 30, "min_px": 3 }`. The asset gives an integer percent; the compiler resolves `px = max(min_px, (of*pct + 50) // 100)` (integer round-half-up) and binds `$length` as pixels. Absolute `int` parameters still exist for things that must not scale (outline, 1 px highlights).
Recipes add detail only where it fits: `"when": "$class_idx >= 2"`.

## 8. What the prototype showed (`prototype/out/size_sweep.png`)
| height → cell | body asymmetry | clipped px | notes |
|---|---|---|---|
| 14 → 16×16 | 0 | 0 | readable, helmet visor collapses to a 1-px line |
| 22 → 24×24 | 0 | 0 | good |
| 30 → 32×32 | 0 | 0 | good, flat areas start to dominate |
| 46 → 48×48 | 0 | 0 | visor becomes a huge bar, big plain surfaces: needs authored detail |

Mechanics hold. Quality does not scale for free: features that are fixed-width (1 px outline, 1 px highlight) look thin on big sprites, and proportional features look chunky. Hence classes, per-class proportions and size-conditional detail.

## 9. Size sweep (mandatory lint)
`arcforge lint` renders every archetype at `size.min`, each class boundary (both sides) and `size.max`, and every recipe on top at each of those sizes with default and extreme parameters. Checks: no clipping, body symmetry, palette budget, minimum feature size (`W_FEATURE_TOO_SMALL`), recipe not fully occluded (`W_OCCLUDED`), recipe stays on its slot region. Output is a contact sheet plus a JSON table. A recipe that fails at a size must declare `"sizes": {"min": .., "max": ..}`; using it outside that range is `E_RECIPE_SIZE_RANGE`.

## 10. Honest limits
- Parametric scaling is good **within a size class** (roughly ±25 %). Across classes, expect authored overrides.
- Budget: each new size class costs tuning on every recipe used at that class. Platformer plan: **two** hero sizes (small ≈ 16, big ≈ 32), both authored, plus a free parametric range between for experiments. A boss uses a third class.
- Automatic scaling of finished art (nearest-neighbour upscale) is out of scope; it would break the 1:1 pixel rule.
