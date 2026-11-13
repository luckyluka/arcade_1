# Recipes

A recipe is an engine capability: deterministic geometry with typed parameters, attached to named anchors of an archetype. The recipe library is the product; the compiler without it is technically clever but artistically empty.

## File format (`registry/recipes/<category>/<name>.recipe.json`)
| field | meaning |
|---|---|
| `recipe`, `version`, `category`, `description` | identity and discovery |
| `applies_to` | archetypes this recipe is built for |
| `slot`, `stackable?`, `conflicts_with?` | exclusivity model |
| `requires_anchors` | anchors that must exist on the archetype |
| `parameters` | typed, ranged, with defaults (types: `int`, `bool`, `enum`, `ramp`, and 2.1 `percent`) |
| `geometry` | shapes: anchor, layer, shape, fill, rotate?, when? (see LANGUAGE.md). Either one array shared by every `applies_to` member, or an object keyed by archetype name (2.1) |
| `hitboxes?` | anchor-relative rects with `active_in` animations |
| `sizes?` | 2.1: `{min,max}` on the archetype's primary size parameter; using the recipe outside → `E_RECIPE_SIZE_RANGE` |
| `locals?` | 2.1: ordered `[name, expression]` pairs, floored, visible only inside this recipe |
| `examples` | parameter sets used by `lint`: defaults plus extremes. Required. |

### 2.1 notes
- A `percent` parameter (`{type:"percent", of:"height", default:30, min_px:3, min?, max?}`)
  binds **pixels**: `px = max(min_px, (dim*pct + 50)//100)`. `of` names an integer
  archetype dimension (size parameter or metric); on an archetype that lacks it the
  parameter is unavailable (supplying it → `E_UNKNOWN_SIZE_PARAM`), which lets one
  recipe serve both fixed-art and size-parametric archetypes.
- Percent bounds (`min`/`max`, default 0..100) exist so the authoring rule below
  stays satisfiable: the sweep renders every recipe at its declared extremes, so a
  recipe that only fits at "reasonable" percentages must declare that range.
- Recipe `sizes` is checked against the archetype's **primary** size parameter
  (the first key of its `size` object).
- **Keyed geometry**: when `geometry` is an object, every `applies_to` member
  needs a key and every key must be in `applies_to` (`E_RECIPE_GEOMETRY`). The
  selected key's shapes are the whole geometry for that archetype. Recipe-level
  `locals` are shared by all keys and evaluate in the *current* archetype's
  environment, so a local naming a dimension only one archetype has fails
  everywhere (`E_RECIPE_EXPR`): keep locals common to every key and inline
  key-specific expressions into the shapes. Parameters may be consumed by only
  one key (`energy_sword`'s `length`/`thickness`/`angle` drive the fixed-art key,
  the percent `reach`/`blade_w` the scalable one) — they stay declared and bound
  either way, and `hitboxes` are shared across keys for the same reason.

## Authoring checklist
1. Pick the slot and layer. Decide which part's motion it should follow (via the anchor).
2. Choose parameter ranges so **every** combination stays inside the canvas. `make lint` renders each example and reports `W_CLIPPED`.
3. Use ramp tones for light/shadow instead of literal colours so style packs can restyle it.
4. Include at least one example at each extreme (min/max) and one with all booleans flipped.
5. Render, look at it at 10×, tune. Only then update goldens.
6. Add the recipe to an example asset and an assertion (`feature_present`).
7. Keep names descriptive and finite: `helmet_closed`, not `cool_helmet`. If two recipes differ only in a parameter, they are one recipe.

## Naming and discovery
Categories mirror archetype families (`humanoid/head`, `humanoid/weapon`, `creature/legs`, `effects/fire`). The AI discovers recipes with `list recipes` and `describe recipe <name>`.

## Anti-patterns
- A recipe whose geometry ignores its parameters (fake executability).
- Parameters named after adjectives ("badass", "worn"). Parameters are measurable (`length`, `thickness`, `angle`) or enumerated (`glow`).
- Hard-coded canvas coordinates outside the anchor frame.
