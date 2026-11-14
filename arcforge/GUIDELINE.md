# ArcForge — Visual Vocabulary and Deterministic Rendering Direction

## 1. Core idea

ArcForge should be treated as a **deterministic visual programming language and compiler**, not as an AI image generator.

The AI's job is to construct a semantic asset specification.

ArcForge's job is to deterministically turn that specification into pixels.

The fundamental pipeline is:

```text
AI / author
    ↓
semantic asset specification
    ↓
archetype
    ↓
recipes + typed parameters
    ↓
optional .px fragments
    ↓
AssetIR
    ↓
deterministic C rendering engine
    ↓
pixels / animation / Arcade-1 asset
```

There must be no hidden visual guessing in this pipeline.

If ArcForge cannot determine exactly what an instruction means, it must reject it rather than approximate it.

---

# 2. The main challenge is not the compiler

The difficult and strategically important part of ArcForge is building a sufficiently rich **visual vocabulary**.

The long-term objective is not simply to have thousands of unrelated recipes.

The objective is to create a **composable visual grammar**.

A relatively modest number of carefully designed primitives, archetypes, recipes, modifiers, materials, styles, poses, effects, and parameters should be capable of producing a very large number of distinct assets.

Think of ArcForge as a programming language for visual objects.

---

# 3. Do not start by creating thousands of recipes

Do NOT approach the problem as:

```text
recipe 1
recipe 2
recipe 3
...
recipe 10000
```

Instead, first design the system that makes recipes:

- composable
- parameterized
- reusable
- discoverable
- compatible with archetypes
- constrained by slots/anchors
- deterministic
- cheap to author

The goal is to make recipe #847 relatively inexpensive to add.

The vocabulary itself should become a scalable authoring system.

---

# 4. Build a layered visual vocabulary

ArcForge should eventually have several registries.

Conceptually:

```text
ArcForge
│
├── archetypes
│
├── structures / anatomy
│
├── recipes
│
├── modifiers
│
├── materials
│
├── surfaces
│
├── effects
│
├── poses
│
├── animations
│
├── palettes
│
├── styles
│
├── environments
│
├── UI components
│
└── .px fragments
```

These categories should not necessarily all become independent technical systems immediately, but the architecture should leave room for them.

---

# 5. Archetypes

An archetype provides the **structural skeleton** of an asset.

Examples:

```text
humanoid
creature
robot
vehicle
projectile
weapon
pickup
environment_object
effect
UI_element
```

A humanoid archetype might expose:

```text
head
torso
arm_l
arm_r
hand_l
hand_r
leg_l
leg_r
foot_l
foot_r
```

The archetype does not mean:

> "Draw something humanoid."

It means:

> "Instantiate this known structural model with these known anchors and slots."

Recipes then operate on this structure.

---

# 6. Anchors and slots are fundamental

Recipes should attach to explicit structural locations.

For example:

```text
helmet_closed       → head
shoulder_armor      → shoulder_l / shoulder_r
weapon              → hand_l / hand_r
boots_heavy         → foot_l / foot_r
cape                → back
```

This makes composition deterministic.

The system should know:

- where a feature is allowed to attach
- what it attaches to
- what geometry it occupies
- what other features it can coexist with
- what features conflict
- how transformations propagate

Do not rely on arbitrary coordinates for normal semantic composition.

Coordinates should be a lower-level mechanism.

---

# 7. Recipes

A recipe is an executable, deterministic visual construction.

Examples:

```text
helmet_closed
helmet_open
shoulder_armor_heavy
boots_heavy
energy_sword
shield
claw
wing
tail
engine
cannon
visor
antenna
exhaust
```

A recipe is NOT natural-language guidance.

It must have an exact implementation.

Conceptually:

```text
energy_sword
    requires:
        hand anchor

    parameters:
        length
        thickness
        color
        glow
        angle

    produces:
        deterministic geometry
```

The same recipe with the same parameters must produce the same result.

---

# 8. Parameters

Recipes should expose typed parameters rather than free-form descriptions.

Example:

```text
energy_sword

length:
    integer
    range 8..64

thickness:
    integer
    range 1..8

glow:
    enum:
        none
        weak
        medium
        strong

angle:
    integer
    range -180..180

color:
    palette reference
```

Parameters should support:

- types
- ranges
- enums
- defaults
- dependencies
- compatibility constraints

Invalid values must fail validation.

---

# 9. Modifiers

Do not create separate recipes for every visual variation.

Prefer composable modifiers.

For example:

```text
armor
+ heavy
+ damaged
+ glowing
+ asymmetrical
```

rather than:

```text
heavy_armor
damaged_heavy_armor
glowing_damaged_heavy_armor
...
```

Potential modifiers:

```text
heavy
light
damaged
broken
glowing
burning
frozen
mechanical
organic
asymmetric
rusted
ancient
clean
```

Modifiers must themselves have deterministic semantics.

---

# 10. Materials

Materials should be reusable across recipes.

Examples:

```text
metal
stone
wood
glass
energy
flesh
ice
slime
crystal
```

For example:

```text
armor
    material = metal
```

and:

```text
armor
    material = crystal
```

should reuse the same structural recipe while producing different deterministic rendering.

This avoids multiplying the recipe count unnecessarily.

---

# 11. Styles

Style should be a separate deterministic layer.

A style can define things such as:

```text
canvas/grid rules
palette
outline rules
shading rules
highlight rules
pixel density
rasterization rules
```

Example:

```text
arcade_16
arcade_32
arcade_64
monochrome
neon
industrial
```

Do not encode arbitrary natural-language artistic instructions into styles.

A style must be a defined, testable rendering configuration.

---

# 12. Effects

Effects should be first-class vocabulary where useful.

Examples:

```text
fire
smoke
spark
electricity
explosion
glow
impact
dust
debris
laser
energy_beam
```

These should be parameterized and composable.

For example:

```text
energy_beam
    length = 40
    thickness = 3
    color = cyan
    intensity = strong
```

---

# 13. Poses and animation

The same philosophy should apply to animation.

Do not make every animation a completely independent sprite.

Instead:

```text
archetype
    +
pose
    +
recipes
    +
animation modifiers
```

Potential poses:

```text
idle
walk
run
jump
fall
attack
hurt
die
crouch
aim
```

Animation generation should preserve structural identity.

Important deterministic constraints include:

```text
root stability
foot anchoring
head stability
weapon attachment
limb continuity
consistent proportions
```

---

# 14. The combinatorial goal

The vocabulary should be designed for **composition**, not merely size.

For example:

```text
20 archetypes
×
50 structural recipes
×
100 detail recipes
×
20 modifiers
×
many typed parameters
```

can already produce an enormous design space.

The goal is therefore not:

> "Build 100,000 recipes."

The goal is:

> "Build a visual grammar capable of expressing 100,000 useful designs with a manageable vocabulary."

---

# 15. `.px` remains the escape hatch

Not everything should be forced into recipes.

If something cannot be expressed by the vocabulary, use an explicit `.px` fragment.

Example:

```text
custom claw
    type = px_fragment
    file = fragments/claw.px
    anchor = hand_l
```

The important rule is:

**Unknown semantic concepts must never be silently approximated.**

The system should either:

1. use a known recipe,
2. use an explicit `.px` fragment,
3. add a new recipe to the vocabulary,
4. or reject the asset.

This is the "no fake executability" principle.

---

# 16. The deterministic drawing engine should be C

The actual rasterization/drawing core should eventually move out of the Python reference implementation and into a small C library.

Conceptually:

```text
Python
    schema validation
    JSON parsing
    registry
    recipe resolution
    diagnostics
    orchestration
        │
        ▼
libarcforge
    geometry
    drawing primitives
    rasterizer
    palette
    transforms
    layers
    .px renderer
    animation operations
        │
        ▼
pixel buffer
```

C should provide deterministic drawing primitives such as:

```c
arc_draw_pixel(...)
arc_draw_line(...)
arc_draw_rect(...)
arc_draw_polygon(...)
arc_draw_circle(...)
arc_draw_ramp(...)
arc_blit(...)
arc_transform(...)
```

Recipes use these primitives to construct their visuals.

The renderer should not depend on an AI model.

---

# 17. Important architectural property

The deterministic rendering path should work without AI:

```text
asset.json
    ↓
ArcForge compiler
    ↓
C rendering core
    ↓
asset.png
```

The exact same input, registry, style definitions, and compiler version must produce identical output.

This makes ArcForge a real compiler/toolchain rather than an image-generation wrapper.

---

# 18. AI sits above the language

The AI should eventually act as a programmer of the ArcForge language.

For example:

User:

> Create a heavy flying mechanical enemy with a central cannon, two glowing engines, damaged armor and a red warning light.

AI produces:

```text
archetype:
    flying_robot

structure:
    body
    wings
    engine_l
    engine_r
    cannon

recipes:
    heavy_armor
    central_cannon
    engine
    warning_light

materials:
    metal

modifiers:
    damaged
    glowing

palette:
    industrial_red
```

ArcForge then deterministically builds it.

The AI is selecting and composing vocabulary.

It is not drawing the pixels.

---

# 19. Vocabulary discovery must be machine-readable

The AI needs to discover what ArcForge can actually express.

The tool interface should support operations such as:

```text
list_archetypes
list_recipes
describe_recipe
list_recipes_compatible_with
list_modifiers
list_materials
list_styles
list_effects
inspect_asset
validate_asset
```

For example:

```text
describe_recipe("energy_sword")
```

should return:

```text
compatible anchors
parameters
parameter types
ranges
defaults
conflicts
dependencies
rendering semantics
```

This prevents the AI from hallucinating capabilities.

---

# 20. The compiler must remain authoritative

The AI may propose:

```text
recipe = dragon_wing
```

If the registry does not contain `dragon_wing`, the compiler must return:

```text
UNKNOWN_RECIPE
```

not:

```text
approximately rendering dragon_wing...
```

This rule is fundamental.

> If the compiler cannot prove what a semantic instruction means, it must reject it rather than approximate it.

---

# 21. Build the vocabulary in stages

Do not attempt to build the entire universe first.

Start with a small but highly composable domain.

For example:

### Phase 1 — humanoids

```text
humanoid
head
torso
arms
hands
legs
feet

helmet
armor
boots
gloves
cape
weapon
shield

idle
walk
attack
hurt
die
```

### Phase 2 — mechanical

```text
robot
drone
turret
vehicle

wheel
track
engine
cannon
antenna
exhaust
```

### Phase 3 — creatures

```text
quadruped
bird
insect
monster

wing
claw
tail
horn
fang
eye
```

### Phase 4 — effects

```text
fire
smoke
spark
explosion
laser
electricity
energy
dust
```

### Phase 5 — environment

```text
rock
tree
crate
door
platform
wall
pipe
machine
sign
```

### Phase 6 — UI

```text
panel
button
bar
icon
cursor
indicator
number
label
```

Each phase should grow the expressive power of the language rather than merely increase the number of files.

---

# 22. The key optimization

Before adding a new recipe, ask:

> Can this be expressed by combining existing recipes, modifiers, materials, and parameters?

If yes, do not add a new recipe.

If no, determine whether the missing concept is:

- a new primitive
- a new recipe
- a modifier
- a material
- a style feature
- an animation operation
- or genuinely custom geometry requiring `.px`

This keeps the vocabulary coherent.

---

# 23. Long-term target

The ideal ArcForge system should eventually feel like this:

```text
"I need a character."

→ choose humanoid

"I need a heavy sci-fi character."

→ heavy_armor
→ metal
→ mechanical

"I need a weapon."

→ energy_sword

"I need him damaged."

→ damaged

"I need a red glowing sword."

→ color=red
→ glow=strong

"I need him attacking."

→ pose=attack
```

The specification remains compact and semantic.

The compiler expands it into a large amount of deterministic geometry.

That is the core opportunity:

> **A relatively small, carefully engineered visual vocabulary can become a very large generative design space through composition.**

---

# 24. Strategic direction

ArcForge should therefore be developed as three tightly connected products:

```text
1. VISUAL LANGUAGE
   archetypes
   recipes
   parameters
   modifiers
   materials
   styles
   animation
   .px

2. DETERMINISTIC ENGINE
   C rendering core
   geometry
   rasterization
   animation
   validation
   regression testing

3. AI INTERFACE
   vocabulary discovery
   semantic asset construction
   compilation
   diagnostics
   repair
   iteration
```

The most important work now is **not generating a huge number of assets**.

It is designing a vocabulary and rendering architecture where adding vocabulary is cheap and where combinations produce useful new designs.

Once that foundation is strong, the number of available recipes, archetypes, materials, effects, modifiers, poses and styles can grow continuously.

The ultimate goal is for ArcForge to become a **programming language for deterministic visual assets**, with enough vocabulary that an AI can construct complex game graphics by composing known visual concepts rather than inventing pixels.