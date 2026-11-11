# Recipes

A recipe is an engine capability: deterministic geometry with typed parameters, attached to named anchors of an archetype. The recipe library is the product; the compiler without it is technically clever but artistically empty.

## File format (`registry/recipes/<category>/<name>.recipe.json`)
| field | meaning |
|---|---|
| `recipe`, `version`, `category`, `description` | identity and discovery |
| `applies_to` | archetypes this recipe is built for |
| `slot`, `stackable?`, `conflicts_with?` | exclusivity model |
| `requires_anchors` | anchors that must exist on the archetype |
| `parameters` | typed, ranged, with defaults |
| `geometry` | shapes: anchor, layer, shape, fill, rotate?, when? (see LANGUAGE.md) |
| `hitboxes?` | anchor-relative rects with `active_in` animations |
| `examples` | parameter sets used by `lint`: defaults plus extremes. Required. |

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
