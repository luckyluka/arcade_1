# ARCADE-1 Asset Ecosystem — Analysis and Design Task

We need to think seriously about the asset ecosystem for ARCADE-1.

Do **not implement anything yet**.

The goal is to investigate and propose how ARCADE-1 can support a broad modern game-development workflow while keeping the actual machine small, deterministic, understandable, and suitable for FPGA implementation.

## Core principle

ARCADE-1 should have:

> **A small runtime asset model, but a broad and powerful development-time asset pipeline.**

The machine should NOT need to understand every asset format used by modern game development.

For example, ARCADE-1 should not necessarily need native support for:

- PNG
- JPEG
- SVG
- Aseprite
- PSD
- Blender files
- TTF/OTF
- arbitrary audio formats
- etc.

Instead, the development environment should be able to convert source assets into efficient ARCADE-1-native resources.

Conceptually:

```text
SOURCE ASSETS
    │
    ├── PNG
    ├── Aseprite
    ├── SVG
    ├── Blender
    ├── procedural assets
    ├── AI-generated assets
    └── other common formats
            │
            ▼
    ARCADE-1 ASSET PIPELINE
            │
            ├── conversion
            ├── packing
            ├── optimization
            ├── palette/format conversion
            ├── atlas generation
            ├── animation extraction
            ├── metadata generation
            ├── compression where justified
            └── validation
            │
            ▼
    ARCADE-1 NATIVE RESOURCES
            │
            ▼
         .arc1
            │
            ▼
       ARCADE-1 VM / FPGA
```

## Important architectural distinction

Keep these three layers conceptually separate:

### 1. Source assets

Anything convenient for humans and AI.

Examples:

```text
player.aseprite
world.png
font.ttf
background.svg
model.blend
music.wav
```

### 2. Native ARCADE-1 resources

Compact representations designed around the actual machine.

Potential resource categories include:

```text
SPRITE
SPRITESHEET / ATLAS
TILESET
TILEMAP
PALETTE
ANIMATION
FONT
AUDIO
BACKGROUND
PARTICLE DATA
UI ELEMENTS
```

Do not assume all of these are required. Determine which are justified.

### 3. Game-facing SDK

The game should ideally interact with semantic resources rather than raw source formats or awkward binary structures.

For example, conceptually:

```c
arcade1_sprite_draw(player_idle, x, y);
arcade1_animation_draw(player_walk, x, y, frame);
arcade1_tilemap_draw(world);
arcade1_text_draw(font_ui, "SCORE", x, y);
```

The exact API is NOT being specified here. Discover it from real game requirements.

---

# Why this matters

The current ARCADE-1 graphics system is intentionally primitive:

- 1-bpp image data
- RGB565 colours
- sprites
- tilemaps
- rectangles
- simple audio

This was appropriate for early milestones.

However, the existing platformer already provides evidence that the current visual representation may become restrictive.

Breakout and Tetris work very well at low resolution and with simple graphics.

A platformer and especially a sandbox/voxel-like game expose the limitations much more quickly.

Therefore:

> Do not assume the current 1-bpp/tile representation is the final ARCADE-1 asset model.

At the same time:

> Do not respond by turning ARCADE-1 into a general-purpose modern GPU or game engine.

We want to discover the smallest useful machine model.

---

# Asset types to investigate

Analyze what a practical ARCADE-1 game ecosystem would need for at least:

## 2D arcade game

Example:

- Breakout
- Tetris

Requirements may be very small.

## Platformer

Consider:

- sprite sheets
- multiple animation states
- directional animation
- tilesets
- tilemaps
- parallax backgrounds
- particles
- UI
- fonts
- effects
- collision masks

## Rich 2D game

Consider:

- larger sprites
- animation
- layers
- transparency
- palettes
- particles
- backgrounds
- UI
- text
- effects

## Sandbox / Minecraft-like experiment

Consider:

- many tiles
- block types
- terrain
- chunks
- procedural assets
- entities
- inventory icons
- particles
- possibly lighting/fake lighting
- large worlds
- efficient repeated assets

## 2.5D / fake-3D experiment

Consider:

- scaling sprites
- perspective
- sprite orientation
- layered backgrounds
- depth ordering
- texture-like data
- procedural geometry
- potentially simple 3D-ish resources

The objective is NOT to make all of these possible immediately.

The objective is to determine which asset primitives are fundamental and which can be implemented by the development pipeline.

---

# Sprite sheets / atlases

Investigate how ARCADE-1 should handle sprite sheets.

A source asset might contain:

```text
player.png

idle_0 idle_1 idle_2
walk_0 walk_1 walk_2
jump
fall
attack_0 attack_1 attack_2 attack_3
```

The asset compiler could transform this into an ARCADE-1-native atlas plus metadata:

```text
player atlas
    region 0 → idle_0
    region 1 → idle_1
    region 2 → idle_2
    ...
```

The game should ideally reference semantic assets rather than manually calculating pixel offsets.

Investigate:

- atlas packing
- sprite metadata
- animation metadata
- pivot/origin points
- trimming transparent borders
- collision rectangles/masks
- directional variants
- frame timing
- naming conventions
- deterministic compilation

Do not assume that all of these need runtime support. Some may be compiled away.

---

# Asset compilation

Investigate what should happen during:

```text
arcade1 build
```

For example:

```text
assets/player.aseprite
        ↓
    compiler
        ↓
sprites + animations + metadata
        ↓
    native resources
        ↓
      game.arc1
```

Consider whether the asset compiler should perform:

- format conversion
- palette reduction
- colour quantization
- atlas packing
- duplicate elimination
- cropping
- alignment
- animation extraction
- font rasterization
- audio conversion
- compression
- collision-mask generation
- metadata generation
- resource indexing
- validation
- size reporting

The compiler should do complexity-heavy work so that the runtime machine does not have to.

---

# AI-native considerations

ARCADE-1 is specifically intended to be a machine that AI agents can develop games for.

This makes the asset pipeline particularly important.

An AI should ideally be able to operate something like:

```text
arcade1 build
arcade1 assets inspect
arcade1 assets validate
arcade1 run game.arc1
arcade1 capture
arcade1 inspect
```

The AI should be able to discover:

```text
assets:
    player
        animations: idle, walk, jump
        frames: 7
        dimensions: 24x32

    world
        tiles: 48
        tile_size: 16x16

    font_ui
        glyphs: 96

resource usage:
    ROM: 184 KiB
    RAM: 31 KiB
```

Consider what structured information would be useful to an AI.

The asset system should make it easy to answer questions such as:

- What assets exist?
- What animations exist?
- What frames are available?
- What dimensions do they have?
- How much ROM do they consume?
- What resources are unused?
- Which assets failed compilation?
- Which assets exceed machine limits?
- What format conversion occurred?
- Why does an asset look different from its source?
- Which game object references an asset?

Prefer machine-readable output such as JSON where useful.

---

# Determinism

Asset compilation should ideally be deterministic.

Given:

```text
same source assets
same source metadata
same compiler version/configuration
```

the resulting native resources should be identical.

Investigate how to make this reliable.

Avoid unnecessary timestamps, host-dependent ordering, filesystem-order dependence, random atlas placement, etc.

The final `.arc1` should remain a reproducible game artifact.

---

# Do not confuse development capability with runtime capability

This distinction is extremely important.

The development ecosystem may understand:

```text
Aseprite
PNG
SVG
Blender
TTF
WAV
etc.
```

while the machine might only need to understand:

```text
native sprite data
native tile data
native animation data
native font data
native audio data
```

The FPGA should not have to implement PNG decoding just because a developer uses PNG.

The compiler should perform that work.

Similarly, a sophisticated editor or asset compiler is acceptable even if the final machine remains extremely small.

---

# Hardware implications

For every proposed native resource type, distinguish:

1. What can be compiled into static data?
2. What requires runtime interpretation?
3. What requires dedicated virtual hardware?
4. What can remain an SDK/software operation?
5. What would become expensive on FPGA?
6. What improves game development enough to justify machine complexity?

Do not add hardware merely because an asset format exists.

For example:

```text
PNG
```

is a development format, not a reason to add PNG decoding hardware.

Likewise:

```text
sprite animation
```

does not automatically require dedicated animation hardware.

The question should always be:

> What is the smallest useful runtime primitive?

---

# Resolution and visual density

The current machine uses 320×240.

Early games such as Breakout and Tetris look good at this resolution.

The platformer and sandbox experiments expose visual limitations.

Do NOT change the resolution as part of this task.

Instead, consider how asset design interacts with possible future resolutions such as:

```text
320×240
640×360
1280×720 output
```

In particular investigate the difference between:

- logical/game resolution
- native machine framebuffer
- physical display resolution
- integer scaling

The asset pipeline should not unnecessarily couple source asset resolution to physical display resolution.

---

# Compression

Investigate compression carefully.

Questions:

- Which resources benefit from compression?
- Should compression be transparent to the game?
- Should the FPGA decompress at runtime or should resources be expanded at load time?
- Is ROM space currently a real bottleneck?
- Does compression make deterministic inspection/debugging harder?
- Would simple formats be preferable to sophisticated compression?

Do not introduce compression merely because it is possible.

---

# Fonts

Fonts are particularly important for AI-generated games.

Investigate whether the pipeline should accept:

```text
TTF / OTF
```

and compile selected glyphs into a native ARCADE-1 representation.

Consider:

- bitmap fonts
- glyph atlases
- multiple sizes
- ASCII vs Unicode
- kerning
- text measurement
- localization

Do not assume full Unicode support is necessary.

Find the smallest useful solution.

---

# Audio

Apply the same philosophy to audio.

Source:

```text
WAV
OGG
etc.
```

Potential native representation:

```text
sample
music
tone
sound effect
```

Investigate what is appropriate for the machine.

Again:

> broad development input, small runtime representation.

---

# Resource model

Propose a coherent model for resources inside `.arc1`.

Consider:

```text
resource ID
resource type
offset
size
metadata
data
```

But do not blindly implement this.

Compare possible approaches.

Questions:

- Should resources be addressable by integer IDs?
- Should names survive compilation?
- Should names be debug-only?
- Should resources be relocatable?
- Should the runtime see a resource table?
- Should the SDK hide resource IDs?
- How should resources be validated?
- How should unused resources be removed?
- How should versioning work?

---

# Development workflow

Describe a proposed end-to-end workflow:

```text
create/edit assets
        ↓
arcade1 build
        ↓
asset validation
        ↓
native resource compilation
        ↓
game compilation
        ↓
.arc1
        ↓
run in reference VM
        ↓
inspect / capture / replay / test
        ↓
FPGA
```

The goal is for an AI agent to be able to perform almost all normal game-development work without needing to understand the internal implementation of the VM or FPGA.

---

# Critical constraint

Do NOT turn this into an abstract "complete game engine" design.

ARCADE-1 should remain:

- small
- deterministic
- inspectable
- testable
- FPGA-friendly
- easy for humans to understand
- easy for AI agents to understand

The asset ecosystem may be sophisticated on the development side while the runtime remains deliberately simple.

---

# Most important question

The final analysis should answer:

> **What is the smallest ARCADE-1 native asset/resource model that would allow an AI and human developer to create visually rich 2D and 2.5D games without constantly fighting the machine?**

Do not optimize for hypothetical features.

Use the actual games already built — Breakout, Tetris, Platformer, and the sandbox/voxel experiments — as evidence.

Identify:

1. What the current system already does well.
2. Where it is becoming restrictive.
3. Which asset capabilities are genuinely needed.
4. Which capabilities belong entirely in the compiler/toolchain.
5. Which capabilities belong in the SDK.
6. Which capabilities justify changes to the virtual machine.
7. Which capabilities should explicitly NOT be added.

## Deliverable

Produce a design analysis document.

Include:

- proposed asset architecture
- source → compiler → native resource flow
- candidate native resource types
- sprite/spritesheet/animation strategy
- tileset/tilemap strategy
- font strategy
- audio strategy
- metadata strategy
- `.arc1` integration
- AI/tooling implications
- determinism/reproducibility considerations
- FPGA implications
- memory/ROM implications
- unresolved questions
- recommended experiments for SDK Validation (M26)

Do not modify the machine or implement the asset pipeline yet.

This is an architecture/design investigation intended to inform the SDK and subsequent architecture revision.