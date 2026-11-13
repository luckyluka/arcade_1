# ArcForge language (v2.0, plus the v2.1 subset marked below)

Normative spec. The reference tool (`tools/arcforge_ref.py`) implements exactly this.
Documents with `"arcforge": "2.0"` and `"arcforge": "2.1"` are both accepted; 2.0
documents cannot use the 2.1 fields (`arcforge` unknown value or unknown key → `E_UNKNOWN_FIELD`).

## Document
```json
{
  "arcforge": "2.0",
  "asset":   { "id": "hero.iron_knight", "type": "character", "version": "2.0.0", "description": "metadata only" },
  "target":  { "profile": "arcade1-draft", "canvas": [32, 32] },
  "style_pack": "arcade_16",
  "archetype": { "name": "humanoid_hero", "params": { "armor_ramp": "steel" } },
  "features": [ "boots_heavy", { "recipe": "energy_sword", "params": { "length": 14 } } ],
  "custom":   [ { "id": "claw", "type": "px_fragment", "file": "fragments/claw.px", "anchor": "hand_l", "layer": "foreground" } ],
  "animations": { "idle": { "template": "idle_breathe" } },
  "tests":    [ { "assert": "dimensions", "expected": [32, 32] } ],
  "export":   { "formats": ["png", "spritesheet"] }
}
```
Only `description` and `metadata` are free-form, and neither affects output. Any other unknown key is `E_UNKNOWN_FIELD`.

## Resolution order (what wins, what draws on top)
1. Archetype body parts (skeleton, anchors, default shapes).
2. Features in listed order. A string is shorthand for the recipe with default parameters.
3. Custom `.px` fragments in listed order.
4. Painter's algorithm: sorted by (layer index in archetype `layers`, then global sequence). Later draws over earlier within a layer.
5. Post-pass: outline (style pack), two independent passes in order:
   - `outer`: 1px outline around the silhouette; `separate_layers` (e.g. `back`): pixels of those layers touching non-listed layers become outline, so a cape separates from the body.
   - `inner`: per-part inner outline. `"inner": { "mode": "part", "parts": ["torso", ...] }` in the style pack's `outline`. A pixel belonging to a listed part becomes outline where it touches a 4-neighbour pixel of a *different* part that was drawn **before** it (painter's order: layer index, then sequence). Because the test only looks backwards, exactly one side of every boundary is outlined — always 1px, never a doubled line. `mode: "none"` (the default) disables the pass; omitting `parts` applies it to every part. Part names are cross-checked against the registry at validate/lint time (`E_UNKNOWN_PART`). Outer-outline pixels carry no part and never participate. Integer math only, no RNG; the pass is a pure function of the resolved grid.

## Coordinates
- Pixel grid, origin top-left, +x right, +y down. A coordinate is the pixel's top-left corner.
- `l`/`r` in anchor names are **screen-relative**: `hand_r` is on the viewer's right, never anatomical.
- Anchors belong to a **part**. All geometry attached to an anchor moves with that part's frame offset.
- Positive `rotate` is clockwise, integer degrees in [-180, 180], pivot = the anchor point.

## Parameters (typed, finite)
| type | fields | notes |
|---|---|---|
| `int` | min, max, default, unit? | out of range → `E_PARAM_RANGE` with clamped fix |
| `bool` | default | |
| `enum` | values, default | |
| `ramp` | default, allow? | a colour ramp name from the style pack (not the outline ramp) |
| `percent` (2.1) | of, default, min?, max?, min_px? | resolves to pixels: `px = max(min_px, (dim * pct + 50) // 100)`; `of` must name an integer archetype dimension (size parameter or metric), else supplying it is `E_UNKNOWN_SIZE_PARAM`; pct bounds default 0..100 (`E_PARAM_RANGE`) |
Unknown parameter → `E_UNKNOWN_PARAM`. Recipe parameter names must not shadow archetype names (parameters, size parameters, metrics, `$W/$H/$class`).

## Expression language (archetype shapes, anchors, metrics, recipe geometry)
Total, side-effect free, no loops, no user functions.
- Literals: numbers, `'strings'`, `true`/`false`. Variables: `$name`.
- Environment: `$W $H` (cell), archetype size parameters, `$class` (string),
  `$class_idx` (0-based), `$p_*` proportions of the resolved class, archetype
  parameters, metrics (archetype, in declaration order), recipe parameters and
  recipe `locals` (recipe scope only). Archetype `metrics` see `$W $H`, size,
  class and proportions only — not archetype parameters.
- Operators: `+ - * /` (true division; floored where an integer is required),
  `//` (floor division) and `%` (modulo) — both floor semantics for negatives
  (`-7 // 2 == -4`, `-7 % 2 == 1`), comparisons, `&& || !`.
- Functions: `floor ceil round min max abs clamp` plus 2.1 `odd` (next odd ≥ x),
  `even` (next even ≥ x), `half` (= `floor(x/2)`).
- Geometry results are **floored** to integers (half-open pixel semantics);
  every metric value is floored on assignment.
- `when` on a shape: drawn only if true.

## Shapes
`rect {x,y,w,h}`, `ellipse {x,y,w,h}` (bounding box), `poly {points}`; fields may be numbers or expressions. Relative to the shape's `anchor` in recipes, absolute in archetypes. `rotate` allowed on rect/poly (not ellipse). `fill` = `outline` | `<ramp>.<tone>` | `$param.<tone>`, tone ∈ shadow, base, light.
Rasterization: a pixel is filled if its **centre** lies inside the shape (even-odd rule), computed in 1/64-pixel integers.

## Layers, slots, conflicts
- Layers are ordered names defined by the archetype (`back, body, armor, held, foreground, fx`).
- A **slot** (e.g. `head_gear`, `weapon_main`) holds one recipe unless the recipe is `stackable`. Two recipes in one slot → `E_SLOT_CONFLICT`.

## Animation
Archetype templates give per-frame integer offsets per part (`root` moves everything). No rotation or squash in v2.0 (rigid translation only). The asset binds names `idle|walk|attack|hurt|death` to templates. Recipes follow their anchor's part automatically.

## Assertions (`tests`)
`dimensions`, `palette_size`, `feature_present`, `silhouette_area`, `bbox_margin`, `anchor_inside_canvas`, `root_stable`, `frame_count`. Evaluated on the compiled result, not by a vision model. Always on, independently of `tests`: palette budget (`E_PALETTE_BUDGET`), clipping (`E_CLIPPED`/`W_CLIPPED` per `clip_policy`), minimum feature size (`W_FEATURE_TOO_SMALL`: any compiled shape whose smaller extent is below the style pack's `min_feature_px`, which defaults to unconstrained) and occlusion (`W_OCCLUDED`: a feature not one of whose pixels survives in any frame). The size sweep adds body mirror symmetry (`E_BODY_ASYMMETRY`, see below).

## v2.1 size model (implemented in X0)
Three sizes are never conflated: **cell** (the canvas), **body size** (archetype
size parameters) and derived **metrics**.

| where | field | meaning |
|---|---|---|
| archetype | `size: {name: {min,max,default}}` | declared body-size parameters; asset values outside range → `E_SIZE_RANGE` (clamped fix), unknown keys → `E_UNKNOWN_SIZE_PARAM` |
| archetype | `classes: [{name,max}]` | size classes; the resolved class feeds `$class`/`$class_idx`; smallest `max >= size`, else the last class |
| archetype | `proportions: {class: {p_*: int}}` | the resolved row is injected as `$p_*` (small heroes get big heads) |
| archetype | `metrics: [[name, expr], ...]` | ordered derived integers (environment above); evaluation failure → `E_ARCHETYPE_EXPR` |
| archetype | `min_cell: [wExpr, hExpr]` | minimum fixed cell at this size → smaller canvas is `E_CELL_TOO_SMALL` |
| archetype | `symmetric: bool` | when true the size sweep asserts exact mirror symmetry of the body about `$cx` (or `floor($W/2)`, colour only) → `E_BODY_ASYMMETRY` |
| archetype | `anchors.*.at` | may be expressions; evaluated once per asset after metrics |
| asset | `archetype.size: {name: int}` | per-asset size selection |
| asset | `target.canvas` | `[w,h]` (fixed) or `{ "mode": "auto", "margin"?, "multiple_of"?, "align"? }`; defaults margin 1, multiple_of 1, align `pivot_bottom_center` |
| asset | `target.clip_policy` | `error` (default) or `warn`; clipped pixels → `E_CLIPPED` / `W_CLIPPED` |
| recipe | `sizes: {min,max}` | primary size-parameter range the recipe supports (primary = first `size` key of the archetype); use outside → `E_RECIPE_SIZE_RANGE` |
| recipe | `geometry` | one array shared by `applies_to`, or an object keyed by archetype name; the keys must cover `applies_to` exactly (missing/extra key → `E_RECIPE_GEOMETRY`). Only the selected key's shapes are compiled, but parameters, recipe `locals` and `hitboxes` are evaluated per archetype for every key. Failure → `E_RECIPE_EXPR` |
| recipe | `locals: [[name, expr], ...]` | ordered, floored, visible only inside the recipe; failure → `E_RECIPE_EXPR` |

A sized archetype's canvas check is `min_cell`, not the v2.0 `canvas.supported`
list (which still governs unsized archetypes). Metric ratios are written as
integer percentages (`$height*30/100`), never as source floats.

### Auto canvas (2.1)
Metrics and anchors see the **provisional cell** = the archetype's
`canvas.default`. Every frame of every animation then renders on an unbounded
plane, and the compiler takes the union bounding box (outline included), adds
`margin`, rounds each dimension up to `multiple_of`, raises it to
`min_cell(size)`, places the content per `align` (`top_left`: bbox at the
margin corner; `pivot_center` / `pivot_bottom_center`: aim the `origin` anchor
at the cell centre / bottom centre) clamped so the bbox keeps at least `margin`
on every side, and finally translates grid, anchors and hitboxes into the cell —
a pure shift, never a re-render, so an auto canvas can never clip. The
`origin`-anchor position inside the resolved cell is exported as `pivot` in
`atlas.json`; games align by `pivot`, never by cell size, so differently sized
assets mix freely.

### Size sweep (2.1)
`arcforge_ref.py sweep ARCHETYPE` (and `lint`, over every archetype that
declares `size`) executes spec 01 §9:

- **Sizes** = `size.min`, each class boundary on both sides (`max`, `max+1`),
  `size.max`, clamped and deduped. The cell at each size is the archetype's own
  `min_cell(size)` (else `canvas.default`) — the sweep therefore executes the
  min-cell formula itself; a probe at `canvas.default` may report
  `E_CELL_TOO_SMALL`, which is consumed as the formula's answer, not a failure.
- **Body** renders alone (palette budget, clipping, and — when `symmetric` —
  exact mirror symmetry about `$cx`/`floor($W/2)`, colour only; a mismatch is
  `E_BODY_ASYMMETRY`).
- **Recipes**: every recipe whose `applies_to` contains the archetype, alone on
  the body, at default parameters, its authored `examples`, `all_min`/`all_max`
  (int min/max, bool both, enum/ramp every value, percent declared bounds else
  0/100) and one variant per enum/ramp/bool value. Percent parameters whose
  `of` is not an archetype dimension are skipped there (unavailable, never
  guessed). A size outside a recipe's declared `sizes` span is skipped for that
  recipe, not failed.
- **Output**: `out/sweep/<archetype>.contact_sheet.png` (rows = sizes, columns =
  body + each recipe's default/all_min/all_max, integer scale) and
  `out/sweep/<archetype>.table.json`; the diagnostics carry paths
  `archetype:<name>/sweep/<size>/<feature>/<label>/...`. Sweep errors fail
  `lint`, so `make check` fails with them.

## What the language refuses to do
Describe appearance in prose, reference things the compiler cannot build, or silently ignore input. If the vocabulary cannot express an idea, the answer is "NO RECIPE EXISTS" plus alternatives, and the author may supply a `.px` fragment.
