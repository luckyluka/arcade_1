# ArcForge language (v2.0)

Normative spec. The reference tool (`tools/arcforge_ref.py`) implements exactly this.

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
Unknown parameter → `E_UNKNOWN_PARAM`. Recipe parameter names must not shadow archetype parameter names.

## Expression language (recipe geometry)
Total, side-effect free, no loops, no user functions.
- Literals: numbers, `'strings'`, `true`/`false`. Variables: `$name` (recipe params + archetype params).
- Operators: `+ - * /` (true division), comparisons, `&& || !`.
- Functions: `floor ceil round min max abs clamp`.
- Geometry results are **floored** to integers (half-open pixel semantics).
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
`dimensions`, `palette_size`, `feature_present`, `silhouette_area`, `bbox_margin`, `anchor_inside_canvas`, `root_stable`, `frame_count`. Evaluated on the compiled result, not by a vision model. Palette budget and clipping are checked implicitly (`E_PALETTE_BUDGET`, `W_CLIPPED`).

## What the language refuses to do
Describe appearance in prose, reference things the compiler cannot build, or silently ignore input. If the vocabulary cannot express an idea, the answer is "NO RECIPE EXISTS" plus alternatives, and the author may supply a `.px` fragment.
