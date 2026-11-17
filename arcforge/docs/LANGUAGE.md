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

### Name vocabulary (P0.2)

Names are never matched with regular expressions — the C compiler (M4) has no
regex engine — so the schemas declare a name form with `"format"` and both
tools check it by hand. A failed check is `E_SCHEMA` at the value's pointer
(`"<value>" is not a valid <form>`); a value of the wrong type is reported by
the type check, not the format check.

| format | rule | used by |
|---|---|---|
| `identifier` | 1–64 characters of `a-z 0-9 _` | recipe/archetype/style/slot names, object keys, ramp names, fragment vars |
| `dotted_identifier` | one or more `identifier`s joined by `.` | `asset.id` |
| `semver` | exactly three groups of 1+ digits `0-9` joined by `.` (e.g. `2.0.0`) | `asset.version`, recipe `version` |
| `category_path` | one or more `identifier`s joined by `/` | recipe `category` |
| `parameter_name` | `p_` + `identifier`, 64 characters total | class proportion keys |
| `diagnostic_code` | `E_` or `W_` + 1+ of `A-Z _` | diagnostic `code` |

Digits are `0-9` only (no Unicode digits), and the 64-character cap keeps
every identifier below the threshold where `difflib`'s similarity heuristic
starts treating long sequences specially — so the Python tool and the C
compiler suggest the same near-misses.

### Derived assets (2.1, X1)
| field | meaning |
|---|---|
| `extends: "<file>"` | derive this document from another asset file: RFC 7386 JSON Merge Patch (objects merge recursively, arrays/null replace whole), path relative to *this* file, target must stay inside the repo root, chain at most 3 links, no file twice. Violations are `E_EXTENDS_PATH` (missing/escaping/non-asset/invalid target), `E_EXTENDS_DEPTH`, `E_EXTENDS_CYCLE`, all reported at `/extends` of the file the command was given |
| `variants: [{id, from?, override?, palette_swap?}]` | several assets from one file: each entry is an id plus an RFC-7386 `override` over the base document, or over the **earlier** entry named by `from` (base id or earlier variant id; anything else → `E_UNKNOWN_VARIANT`). Ids are unique within the file (`E_DUPLICATE_ID`) — project-wide uniqueness waits for the X4 project file |
| `palette_swap: {<ramp>: <ramp>}` | ramp-name map applied **when fills resolve** (on base and per variant); both ends must exist in the style pack (`E_UNKNOWN_RAMP` at `/palette_swap/<k>`). Assertions (`palette_size`) see the swapped palette |

- A document that has `extends` is a **patch**: its required fields come from the target, so the raw schema check runs only on documents without one; types and unknown keys of a patch are checked on the merged document (`E_UNKNOWN_FIELD` lands where the merged document holds the key). Nested `override` objects merge the same way, so `{"archetype": {"params": {"armor_ramp": "gold"}}}` restates only what changes.
- `test`, `golden` and `render` compile the base **and every variant**: golden files are named per id (`tests/golden/<id>.json`), renders go to `out/<id>/`, and the report carries `stats.ids`. A variant's diagnostics are reported under its file pointer `/variants/<i>` plus the pointer inside the merged document.
- `variants`/`extends` replace, never merge with, the other side's arrays — RFC 7386, no special cases.

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

### Shape vocabulary (2.1, X1)
| shape / field | meaning |
|---|---|
| `line {x1,y1,x2,y2,thickness}` | integer Bresenham between two points (same coordinates as `rect`), square brush `thickness²` centred on every step; `thickness` must be a positive odd integer, `line` cannot be rotated — violations are `E_SHAPE_PARAM` |
| `repeat {var,count,step_x?,step_y?,max,shapes[]}` | bounded loop; `$var` and `$i` are bound per iteration, fixed order `0..count-1`; `step_x`/`step_y` default 0 and shift that iteration's drawables; `count` must evaluate to `0..max`, `max` ≤ 256, nesting ≤ 2 levels — violations are `E_REPEAT_BOUND` |
| `px {file,anchor?,flip?,frame?,when?}` | draw one frame of a `.px` fragment (resolved against the asset's directory, like `custom`); `anchor` defaults to the shape's own anchor in recipes, the cell origin in archetypes; `flip: "x"` / `"y"` mirrors the fragment's pixel offsets about its attach pixel; `frame` (default 0) selects a px-2 block — out of range is `E_PX_FRAMES`; `custom` takes the same `frame`. File/legend failures reuse `E_PX_NOT_FOUND` / `E_PX_PARSE` / `E_PX_LEGEND` |
| `mirror: "x"` | **also** draw the exact integer reflection about the mirror axis: the archetype's `mirror_axis` metric if declared (not an integer metric/parameter → `E_ARCHETYPE_EXPR`), else `$cx` when the archetype declares it, else `floor($W/2)`. Cannot be combined with `rotate` (`E_SHAPE_PARAM`) |

- Recipe `repeat` children inherit `anchor`, `layer` and `fill` from the repeat itself when they omit them; a child that still has no fill is `E_SHAPE_PARAM`. Archetype `repeat` children are full shapes: they declare `layer` and `fill` themselves.
- `mirror` may sit on `repeat`: iterations are built and stepped first, then every result is reflected. The reflection is a second drawable drawn after the original (same fill), so a shape straddling the axis thickens by overlap.
- `line` compiles to a pixel set; the rasterizer, extents, and `min_feature_px` checks treat it like any `.px` geometry.

## Layers, slots, conflicts
- Layers are ordered names defined by the archetype (`back, body, armor, held, foreground, fx`).
- A **slot** (e.g. `head_gear`, `weapon_main`) holds one recipe unless the recipe is `stackable`. Two recipes in one slot → `E_SLOT_CONFLICT`.

## Animation
Archetype templates give per-frame integer offsets per part (`root` moves everything). v2.0 frames move parts rigidly — no rotation or scale transform anywhere. The asset binds names `idle|walk|attack|hurt|death` to templates. Recipes follow their anchor's part automatically.

### Frames: tracks, swap, hold (2.1, X1)
| field | meaning |
|---|---|
| `params: {<param>: value}` | **parameter track** (02 §F): override parameters for that frame — archetype size parameters (silently **clamped** to range) and archetype/recipe parameters of kinds `int`, `bool`, `enum`, `percent` (`ramp` is not trackable). A key the archetype cannot override, or a non-integer size track, is `E_TRACK_PARAM` at `/archetype/animations/<t>/frames/<i>/params/<k>`; archetype/recipe track values are otherwise validated exactly like supplied values (`E_PARAM_TYPE`/`E_PARAM_RANGE`/`E_PARAM_ENUM`, and `E_UNKNOWN_SIZE_PARAM` when a percent's `of` is unavailable — a track counts as supplying it). Percent tracks resolve with the same `of` rules against the frame's size. Recipe tracks apply only to recipes **this asset uses** |
| `swap: {<part>: <set>}` | **variant frame** (02 §F): for this frame the part draws `parts.<part>.variants.<set>` instead of `parts.<part>.shapes` (anchors and offsets unchanged). Unknown part → `E_UNKNOWN_PART`, unknown set → `E_UNKNOWN_SWAP_SET`, both at `/archetype/animations/<t>/frames/<i>/swap/<part>` |
| `hold: n` | per-frame duration in ticks at the template's `fps`; integer ≥ 1, default 1. `atlas.json` exports `durations: [hold per frame]` while `frames` keeps counting rendered frames (02 §F timing). Structure is schema-enforced with the archetype, like `fps` |

- Track and swap validation runs for the templates **the asset binds** — a template nothing names is never compiled (documented scope limit). Each distinct `(params, swap)` combination compiles to its own geometry at validate time, so failures report there, before any render.
- A frame state re-derives size, class/proportions, parameters and metrics at the track's values and rebuilds archetype **and** feature shapes in that env (the mirror axis too). **Anchors, hitboxes and part offsets stay at their base values** — the atlas exports them once — and a recipe's `sizes` span is checked once at the asset's chosen size, not per frame.
- `atlas.json` carries `facing: "right"` (02 §F): art is authored facing right; a game flips left at runtime, no duplicated art.
- A fixed canvas clips a frame state like any frame (`E_CLIPPED`/`W_CLIPPED` per `clip_policy`); an auto canvas unions every state's pixels into its bbox.

## Assertions (`tests`)
`dimensions`, `palette_size`, `feature_present`, `silhouette_area`, `bbox_margin`, `anchor_inside_canvas`, `root_stable`, `frame_count`. Evaluated on the compiled result, not by a vision model. Always on, independently of `tests`: palette budget (`E_PALETTE_BUDGET`), clipping (`E_CLIPPED`/`W_CLIPPED` per `clip_policy`), minimum feature size (`W_FEATURE_TOO_SMALL`: any compiled shape whose smaller extent is below the style pack's `min_feature_px`, which defaults to unconstrained) and occlusion (`W_OCCLUDED`: a feature not one of whose pixels survives in any frame). The size sweep adds body mirror symmetry (`E_BODY_ASYMMETRY`, see below).

### Quality assertions (2.1, X1 — spec-next 02 §H)
| assert | args | meaning |
|---|---|---|
| `baseline_stable` | `anchor`, `max` | y spread of the anchor over every rendered frame (base + all animations) ≤ `max` — feet don't float. Unknown anchor → FAIL listing the known anchors |
| `pivot_stable` | `max` | `origin` anchor's Chebyshev displacement from its base-frame position ≤ `max` |
| `min_feature` | `min` | smallest filled bbox dimension over every compiled shape — base geometry **and** every frame state — ≥ `min`. The `affects: ["detail"]` marker (02 §D) is documentation-only, so no shape can be excluded yet: **every shape counts** (the conservative reading; recorded as a gap) |
| `cell_fill` | `min`, `max` | per-frame opaque-pixel ratio of the cell: the observed band over all frames must sit inside the declared one |
| `seamless` | `axes` (`["x"]` / `["y"]` / `["x","y"]`) | every rendered frame: each border cell named by `axes` is present in the grid **and** is not outline-ramp — tileable borders carry no transparent and no outline pixels |
| `jitter_max` | `max` | largest Chebyshev step between floored centre-of-mass means of consecutive frames, per animation, ≤ `max` |
| `orphan_pixels` | `max` | per-frame count of pixels with no 4-neighbour ≤ `max` |

A failing assertion is `E_TEST_FAILED` at `/tests/<i>` with observed/expected. Out of X1 (they need the project file): `contrast_min` (backgrounds) and `silhouette_distinct` (peer assets) — 02 §H names both; they arrive with X4, so the schema does not accept them yet.

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

## Shaded volumes, capsules and joint rotation (HD look, 5-tone ramps)

- **Ramps** may have 3 to 5 tones, named `deep`, `shadow`, `base`, `light`, `high` (`base` is required).
- **Shaded fill**: `"fill": "$team_ramp.*"` on an `ellipse` or `capsule` shades the shape as a volume from the
  style pack's light (`shading.light`, an integer vector of length ~1024; `shading.dither` = `none` or `bayer2`).
  The pixel's tone is picked from the ramp's tones in order; the whole computation is integer-only (normals scaled by
  1024, `math.isqrt`, floor division) so a C port can match it byte for byte. Rects and polygons stay flat.
- **`capsule`**: `{"shape":"capsule","x1","y1","x2","y2","w","layer","fill"}` is a thick segment between two cap centres.
  Round caps by default; `"caps":"flat"` ends exactly at the cap centres (use it for torsos and belts).
- **Joint rotation**: an animation frame may carry `"rotate": {"<part>": [degrees, pivot_x, pivot_y]}`: whole degrees
  -180..180, positive = clockwise, pivot in cell pixels. It is applied before `offsets`. Polygons and capsules rotate
  exactly; an ellipse or `.px` fragment only moves its centre (it keeps its orientation).
- Budget: a SPRITE4 bank is 15 colours + transparent, so a style like `shaded_15` allows one outline colour, one
  5-tone steel ramp, one 5-tone team ramp and a 4-tone skin ramp per asset (`palette_size max 15`).
