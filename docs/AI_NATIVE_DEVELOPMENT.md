# ARCADE-1 AI-Native Development

## Purpose

ARCADE-1 should be designed not only as a capable game machine, but as a machine that is exceptionally easy for both humans and AI systems to develop for.

The goal is not to optimize ARCADE-1 specifically for AI at the expense of human developers.

Instead:

> **Make the machine deterministic, observable, inspectable, testable and easy to program. These properties benefit both AI and humans.**

AI-assisted game development is expected to be an important use case for ARCADE-1.

This document describes a future development direction.

It does **not** modify the fixed M01–M22 roadmap.

---

# 1. Core Principle

ARCADE-1 should not merely show the developer what happened.

It should make it possible to determine **why it happened**.

A screenshot tells us:

> What does the game look like?

Machine state tells us:

> What is the machine doing?

Structured game/debug state tells us:

> What does the game believe is happening?

ARCADE-1 should eventually make all three available.

```text
                 GAME
                   │
          ┌────────┼────────┐
          │        │        │
          ▼        ▼        ▼
       Pixels    Machine   Game State
                  State
          │        │        │
          └────────┼────────┘
                   ▼
              AI / HUMAN
```

---

# 2. Deterministic Execution

Determinism is foundational.

Given:

```text
ROM
+
initial machine state
+
input sequence
```

the result must be reproducible.

This enables:

- deterministic tests
- replay
- regression testing
- bug reproduction
- frame-by-frame debugging
- AI iteration
- automated game testing
- screenshot comparison
- state comparison

An AI should be able to reproduce exactly the frame where a failure occurred.

For example:

```text
arcade1 run game.arc1 --replay test.replay --frame 384
```

should produce the same machine and visual state every time.

---

# 3. Machine Inspection

The development environment should eventually expose machine-level information.

Possible command:

```text
arcade1 inspect
```

Potential information:

```text
CPU
  PC
  registers
  instructions/frame
  execution budget

MEMORY
  ROM usage
  RAM usage
  stack usage
  MMIO activity

VIDEO
  commands
  sprites
  tilemaps
  pixels written
  framebuffer state

AUDIO
  commands
  channels/voices
  samples

INPUT
  current input
```

This is primarily a development/debugging facility.

It does not imply that all of this information must exist in the production hardware.

---

# 4. Render Inspection

A particularly important capability is understanding what was actually rendered.

A screenshot alone is insufficient for reliable AI debugging.

The development runtime should eventually be able to answer questions such as:

- Which sprites were submitted?
- Where were they drawn?
- Which were visible?
- Which tilemap cells were visible?
- What camera position was active?
- What draw order was used?
- Which graphics commands affected a region?
- Which objects were outside the screen?
- How much graphics work was performed?

Example:

```text
FRAME 384

CAMERA
  x=128
  y=64

SPRITES
  player
    position=143,182
    size=16x24
    visible=yes
    asset=player_run_03

  enemy_0
    position=211,184
    visible=yes
    asset=slime_02

TILEMAP
  layer=world
  visible_cells=384

HUD
  score=120
  lives=3

VIDEO
  sprites=17
  tile_cells=384
  pixel_operations=42183
```

This structured information should complement the framebuffer rather than replace it.

---

# 5. Semantic Game Inspection

Machine inspection and rendering inspection are not enough.

Games should eventually have a development mechanism for exposing meaningful game state.

For example:

```text
PLAYER
  position=(143,182)
  velocity=(1,-2)
  health=84
  state=RUNNING

ENEMIES
  count=14

WORLD
  current_level=2
  camera=(128,64)

GAME
  score=120
  lives=3
  elapsed_frames=384
```

This information should be supplied by the game/SDK rather than hard-coded into the machine.

A possible future SDK concept:

```c
arcade1_debug_value("score", score);
arcade1_debug_entity("player", &player);
```

The exact API must be designed later.

Do not prematurely freeze such an API.

---

# 6. Pixels + Semantics

The most important development model is:

```text
                FRAME
                  │
       ┌──────────┴──────────┐
       ▼                     ▼
   FRAMEBUFFER          STRUCTURED DATA
       │                     │
       │               machine state
       │               render state
       │               game state
       │
       └──────────┬──────────┘
                  ▼
             AI / HUMAN
```

AI systems should not be forced to infer everything from pixels.

Conversely, structured state should not replace visual inspection.

Both are necessary.

---

# 7. AI Development Loop

The intended long-term workflow is:

```text
AI writes code
      ↓
arcade1 build
      ↓
game.arc1
      ↓
deterministic execution
      ↓
automated input/replay
      ↓
capture frames
      ↓
inspect machine state
      ↓
inspect rendered objects
      ↓
inspect game state
      ↓
AI evaluates result
      ↓
AI modifies code
      ↓
repeat
```

This should make autonomous game development practical without requiring the AI to interact with a desktop GUI like a human.

---

# 8. Failure Diagnosis

The development environment should make failures machine-readable.

Instead of:

```text
game looks wrong
```

an AI should eventually be able to obtain:

```text
FRAME 384

EXPECTED:
  player visible
  player x=143
  player y=182

ACTUAL:
  player submitted
  player x=143
  player y=182
  visible=false

RENDER ANALYSIS:
  player covered by tilemap layer 0
```

The system should help distinguish between:

- game logic errors
- collision errors
- rendering errors
- asset errors
- memory exhaustion
- CPU budget exhaustion
- graphics budget exhaustion
- invalid API usage
- deterministic regression

---

# 9. Resource Observability

Performance should eventually be measurable in terms meaningful to the machine.

CPU instructions alone are insufficient.

Potential measurements include:

```text
CPU instructions
RAM usage
ROM usage
stack usage
video operations
pixels processed
sprites submitted
tiles processed
audio work
frame budget
```

This is especially important because ARCADE-1 contains hardware-style devices.

A game should not appear cheap simply because expensive work is performed by a video device.

The development tools should expose meaningful resource consumption.

---

# 10. Automated Testing

ARCADE-1 should eventually support tests such as:

```text
given input sequence X
run 600 frames
expect state hash Y
expect framebuffer hash Z
expect score=120
expect player position=(143,182)
```

Potential future command:

```text
arcade1 test
```

Tests should be deterministic and reproducible.

Golden outputs should be considered carefully.

A framebuffer hash can detect visual regressions that a CPU-only state hash cannot.

---

# 11. Replay

Input recording should eventually become a first-class development artifact.

Example:

```text
game.arc1
test.replay
```

A replay contains:

- initial conditions
- deterministic seed where applicable
- per-frame input
- version information

Then:

```text
arcade1 run game.arc1 --replay test.replay
```

can reproduce a session exactly.

This is useful for:

- debugging
- AI development
- regression tests
- competitions
- demonstrations
- bug reports

---

# 12. AI-Friendly Tooling

Future tools may include:

```text
arcade1 build
arcade1 run
arcade1 test
arcade1 inspect
arcade1 capture
arcade1 replay
arcade1 debug
arcade1 profile
```

The commands should have:

- deterministic output
- machine-readable formats
- predictable exit codes
- concise human-readable output
- optional JSON output
- no unnecessary interactive UI

For example:

```text
arcade1 inspect --json
```

could provide structured information suitable for an AI coding agent.

---

# 13. Headless Operation

ARCADE-1 should eventually be usable without a graphical frontend.

A headless mode could:

- run a ROM
- inject inputs
- execute a fixed number of frames
- capture framebuffer output
- capture state
- produce hashes
- report resource usage
- exit with a deterministic result

This is important for CI and AI agents.

The Libretro frontend should remain useful for humans, but it should not be required for automated development.

---

# 14. Visual Feedback

AI development should support visual evaluation.

Potential future capabilities:

```text
arcade1 capture --frame 384
```

or:

```text
arcade1 run game.arc1 --frames 600 --capture ./frames/
```

The AI can then inspect:

- screenshots
- animation
- UI
- sprite positions
- visual glitches
- camera behavior
- rendering regressions

The structured inspection data should accompany these images whenever possible.

---

# 15. Do Not Build This Prematurely

This document is intentionally future-facing.

Do not:

- modify M01–M22
- add speculative debugging MMIO
- add unnecessary runtime overhead
- add AI-specific hardware
- add semantic game knowledge to the machine
- freeze an inspection API prematurely
- make production FPGA hardware depend on debug facilities

The correct time to implement these capabilities is after the machine and SDK requirements are better understood.

The C reference VM is the natural first environment for experimentation.

---

# 16. Relationship to the SDK

The SDK should eventually make observability easy without forcing developers to build custom tooling.

However:

> **The SDK should not become an enormous framework merely to support AI.**

The ideal model is that normal game development remains simple.

AI-oriented inspection is an additional development layer.

For example:

```text
GAME
 │
 ├── normal game code
 │
 └── optional development instrumentation
             │
             ▼
       ARCADE-1 runtime
             │
       ┌─────┴─────┐
       ▼           ▼
   framebuffer   inspection
```

Production builds may remove or disable optional instrumentation.

---

# 17. Architecture Feedback

The AI-development requirements may reveal legitimate machine architecture improvements.

Examples:

- insufficient graphics bandwidth
- inefficient memory access
- inadequate resource limits
- missing rendering primitive
- poor data representation
- awkward synchronization
- insufficient state visibility
- difficult asset representation

However:

> **Do not add machine features merely because they make an AI implementation easier.**

First determine whether the problem belongs in:

1. game code
2. SDK
3. development tooling
4. reference VM
5. machine architecture

Only the final category should change the machine.

---

# 18. Guiding Principle

The long-term goal is:

> **ARCADE-1 should be easy to build for, easy to observe, easy to test, and difficult to misunderstand.**

The machine should be small.

The development environment should be powerful.

And the same properties that make ARCADE-1 attractive to AI should make it attractive to humans.

---

# 19. Ultimate Vision

A future AI should be able to receive a game specification such as:

> Create a platformer with three levels, a player character, five enemy types, collectibles, a title screen, music and a boss.

Then operate approximately:

```text
write source
    ↓
build
    ↓
run
    ↓
inspect
    ↓
capture
    ↓
test
    ↓
observe failure
    ↓
modify
    ↓
repeat
```

without needing to understand:

- Libretro
- RetroArch
- FPGA implementation
- host operating systems
- GPU APIs
- emulator internals

The AI should primarily understand:

> **ARCADE-1 SDK + game source + assets + deterministic development tools.**

That is the desired end state.

---

## Core Principle

> **Do not optimize the machine around hypothetical AI requirements.**
>
> **Build a good game machine, make it deterministic and observable, and make the development environment expose enough structured information that both AI and humans can reason about what happened.**