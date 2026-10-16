# ARCADE-1 Development Roadmap

## 0. Mission

Build **ARCADE-1**, a small deterministic game-oriented virtual machine and eventual hardware architecture.

The initial implementation target is a **Libretro core** so that ARCADE-1 can run through RetroArch on Linux and later Android/Google TV.

The long-term architecture is:

```text
                    ARCADE-1
                       │
              ┌────────┴────────┐
              │                 │
           VM/core          Game SDK
              │                 │
              └────────┬────────┘
                       │
                    game
                       │
                       ▼
                Libretro core
                       │
                 RetroArch
                       │
             ┌─────────┼─────────┐
             ▼         ▼         ▼
           Linux     Android   Google TV
```

Later, the same ARCADE-1 machine specification should be implementable in FPGA hardware.

The project is **not** an Android application and is **not** a RetroArch-specific machine. Libretro is only the first host interface.

---

# 1. Core architectural principle

Keep these layers separate:

```text
┌──────────────────────────────────────┐
│             ARCADE-1 GAME            │
├──────────────────────────────────────┤
│          ARCADE-1 SDK / ABI          │
├──────────────────────────────────────┤
│           ARCADE-1 MACHINE           │
│                                      │
│  RV32IM CPU                          │
│  memory                              │
│  MMIO                                │
│  timer                               │
│  input                               │
│  video                               │
│  audio                               │
├──────────────────────────────────────┤
│        host adapter / frontend       │
│                                      │
│        Libretro initially            │
└──────────────────────────────────────┘
```

The ARCADE-1 machine must not depend on RetroArch.

The Libretro layer must be a thin adapter.

The eventual FPGA implementation must implement the same machine semantics.

---

# 2. Important design rules

## Rule 1 — Incremental development

Implement only the current roadmap step.

Do not implement future features because they appear obvious.

For every step:

1. inspect the existing repository;
2. make the smallest necessary change;
3. build;
4. run available tests;
5. manually verify the result where possible;
6. document what changed;
7. stop.

Do not silently proceed to the next step.

---

## Rule 2 — Keep the project buildable

Every completed step must leave the repository in a usable state.

Do not knowingly leave:

- broken builds;
- unused experimental code;
- half-implemented APIs;
- commented-out alternative implementations;
- speculative abstractions.

If a future abstraction is not needed yet, don't add it.

---

## Rule 3 — Prefer simple C

The reference implementation should initially be written in **C**.

Use:

- C11 or the project's existing C standard;
- small modules;
- explicit data structures;
- deterministic behavior;
- no unnecessary dependencies.

Avoid:

- C++ unless there is a compelling reason;
- large frameworks;
- scripting languages inside the VM;
- dynamic plugin systems;
- unnecessary abstraction layers.

The eventual machine should be straightforward to reproduce in RTL.

---

## Rule 4 — Reference implementation first

The software VM is the reference implementation.

Later:

```text
ARCADE-1 specification
        │
        ├── software VM
        │
        └── FPGA implementation
```

The VM should therefore favor:

- clarity;
- determinism;
- explicit behavior;
- easy debugging;
- reproducible execution.

Do not optimize prematurely.

---

# 3. What ARCADE-1 is

ARCADE-1 is intended to be a **small game computer**, not a general-purpose computer.

Primary characteristics:

- RV32IM CPU;
- bare-metal execution;
- deterministic frame-oriented execution;
- controller-oriented input;
- 2D graphics;
- audio;
- ROM-based games;
- no operating system;
- no filesystem requirement;
- no networking;
- no 3D requirement.

The eventual display target is **HD television use**, but do NOT hard-code the final display resolution into the machine architecture at this stage.

The graphics architecture must remain resolution-independent.

1080p60 is a likely reference output, but this is deliberately not frozen yet.

---

# 4. Roadmap

Implement the following milestones in exactly this order.

```text
01  Libretro skeleton
02  Test-pattern video
03  RetroPad input
04  Moving square
05  Audio beep
06  ARCADE-1 machine abstraction
07  Memory subsystem
08  RV32I decoder
09  RV32I execution
10  Load compiled C program
11  MMIO
12  Timer
13  ARCADE-1 video device
14  Simple rectangle API
15  Breakout
16  M extension
17  Proper ROM format
18  Tetris
19  Sprite engine
20  Tilemap engine
21  Scrolling
22  Platformer
```

Do not skip milestones.

---

# 5. Milestone 01 — Libretro skeleton

Goal:

Create the smallest valid Libretro core.

It should:

- compile as a Libretro core;
- load in RetroArch;
- initialize correctly;
- implement the required Libretro lifecycle functions;
- accept a content file;
- shut down cleanly.

At this stage there is:

- no RV32 CPU;
- no ARCADE-1 machine;
- no graphics engine;
- no audio engine.

The core can simply run an empty frame.

Deliverable:

```text
arcade1_libretro.so
```

and enough metadata/configuration to load it in RetroArch.

---

# 6. Milestone 02 — Test-pattern video

Make the core produce video.

Initially use a simple software framebuffer.

Do NOT design the final ARCADE-1 video architecture yet.

Generate a deterministic test pattern.

For example:

```text
solid colors
gradients
moving test bars
```

The purpose is only to prove:

```text
Libretro
   ↓
video callback
   ↓
RetroArch
   ↓
display
```

The chosen test resolution is temporary.

Do not interpret it as the final ARCADE-1 resolution.

---

# 7. Milestone 03 — RetroPad input

Read Libretro's standard controller abstraction.

Implement at least:

```text
UP
DOWN
LEFT
RIGHT
A
B
START
SELECT
```

Do not access Linux/Android/USB/Bluetooth devices directly.

The Libretro frontend owns physical controller handling.

The ARCADE-1 machine should eventually expose its own abstract input device.

For now, just prove that Libretro input works.

---

# 8. Milestone 04 — Moving square

Draw a square and move it using:

```text
LEFT
RIGHT
UP
DOWN
```

This is the first complete interactive vertical slice.

It does not yet run on RV32.

The square may be controlled by host-side test code.

The purpose is to validate:

```text
input → state → rendering
```

---

# 9. Milestone 05 — Audio beep

Generate a simple deterministic beep.

Use Libretro's audio callback.

Prefer batch audio.

The exact ARCADE-1 audio hardware is NOT defined yet.

The goal is only:

```text
ARCADE-1 core
      ↓
audio samples
      ↓
Libretro
      ↓
RetroArch
      ↓
speakers
```

---

# 10. Milestone 06 — ARCADE-1 machine abstraction

Now separate the actual machine from Libretro.

Create something conceptually similar to:

```text
src/
    arcade1/
        machine.c
        machine.h

    cpu/
    memory/
    video/
    audio/

    libretro/
        libretro.c
```

The important boundary is:

```text
Libretro
   │
   ▼
ARCADE-1 machine
```

The machine must be usable without RetroArch.

For example:

```c
arcade1_machine_t machine;

arcade1_init(&machine);
arcade1_reset(&machine);
arcade1_run_frame(&machine);
```

The exact API is up to the implementation, but keep it small.

---

# 11. Milestone 07 — Memory subsystem

Implement a clean memory/bus abstraction.

Initially support:

```text
ROM
RAM
```

Do not invent a large memory map yet.

Memory accesses should go through one clear interface.

For example:

```c
uint32_t arcade1_read32(...);
void arcade1_write32(...);
```

The exact implementation is flexible.

The important property is:

```text
CPU
 │
 ▼
machine memory/bus
 │
 ├── ROM
 ├── RAM
 └── future MMIO
```

Do not add peripherals yet.

---

# 12. Milestone 08 — RV32I decoder

Implement an RV32I instruction decoder.

Start from the RISC-V specification.

Support only the required RV32I base instructions.

The decoder should be independently testable.

Create unit tests for:

- opcode extraction;
- register fields;
- immediate decoding;
- instruction formats;
- branch offsets;
- load/store offsets.

Do not implement M yet.

---

# 13. Milestone 09 — RV32I execution

Implement execution of the RV32I instruction set.

CPU state should at minimum contain:

```text
32 × 32-bit registers
PC
```

Respect the RISC-V rule:

```text
x0 == 0
```

Implement:

- arithmetic;
- logical operations;
- shifts;
- branches;
- jumps;
- loads;
- stores;
- immediate instructions.

Create instruction-level tests.

Do not add caches, pipelines, speculative execution, interrupts, or other CPU features.

This is an interpreter, not a hardware simulator.

---

# 14. Milestone 10 — Load compiled C program

Get a real C program running.

The desired flow is:

```text
C source
   ↓
RISC-V GCC
   ↓
RV32 executable
   ↓
ARCADE-1 VM
   ↓
program executes
```

Initially use the simplest possible program.

For example:

```c
volatile int counter;

int main(void)
{
    counter = 123;
    return 0;
}
```

The agent should establish the required compiler/linker/startup assumptions.

Do not invent a sophisticated SDK yet.

The goal is simply:

**compiled C executes correctly on the RV32I VM.**

---

# 15. Milestone 11 — MMIO

Introduce memory-mapped I/O.

The bus becomes:

```text
CPU
 │
 ▼
BUS
 ├── ROM
 ├── RAM
 └── MMIO
```

Initially create only the minimal MMIO infrastructure.

Do not yet create every ARCADE-1 peripheral.

The important thing is that a guest program can write/read a device register.

---

# 16. Milestone 12 — Timer

Implement a deterministic timer.

The timer must be suitable for games.

The eventual architecture should have a clear relationship between:

```text
CPU execution
frame timing
timer
```

Avoid depending on host wall-clock time for the machine's internal semantics.

The VM may run as fast as possible while emulation timing is controlled by the host/frontend.

---

# 17. Milestone 13 — ARCADE-1 video device

Move video control into the guest machine.

The guest program should now be able to communicate with the video device through the ARCADE-1 machine interface/MMIO.

Do NOT finalize the graphics architecture yet.

The initial device may be extremely simple.

For example:

```text
CLEAR
SET_PIXEL
PRESENT
```

or another equally minimal mechanism.

The purpose is to establish:

```text
RV32 program
     ↓
MMIO
     ↓
ARCADE-1 video device
     ↓
software renderer
     ↓
Libretro
```

---

# 18. Milestone 14 — Simple rectangle API

Provide a simple guest-facing graphics API.

At minimum:

```text
clear
rectangle
present
```

The implementation may initially be primitive.

The important thing is that a program running inside RV32 can draw graphics.

Do not add sprites, tilemaps, textures, shaders, rotation, alpha blending, or other features yet.

---

# 19. Milestone 15 — Breakout

Build Breakout as the first real ARCADE-1 game.

It should run as a **guest program**, not as host-side C.

It should demonstrate:

- input;
- timing;
- game loop;
- rectangles;
- collision;
- score;
- lives;
- game state;
- basic audio.

The desired chain is:

```text
breakout.c
    ↓
RISC-V compiler
    ↓
ARCADE-1 ROM
    ↓
RV32I VM
    ↓
ARCADE-1 video/audio/input devices
    ↓
Libretro
    ↓
RetroArch
```

This is a major milestone.

---

# 20. Milestone 16 — M extension

Add the RISC-V M extension:

```text
MUL
MULH
MULHSU
MULHU
DIV
DIVU
REM
REMU
```

Add tests.

Only after RV32I is already proven.

---

# 21. Milestone 17 — Proper ROM format

Now define a real ARCADE-1 game image format.

It should be minimal.

Possible contents:

```text
header
code/data image
metadata
optional asset sections
```

Do not build a filesystem.

Do not build a package manager.

Do not over-design the format.

A game should eventually be:

```text
game.arc1
```

and RetroArch should pass that content to the core.

---

# 22. Milestone 18 — Tetris

Build Tetris as a guest program.

Purpose:

Stress the current API with:

- grid-based graphics;
- repeated objects;
- text;
- deterministic timing;
- game state.

Do not add sprite hardware merely because Tetris would be easier with sprites.

Instead use the existing API and observe what is missing.

---

# 23. Milestone 19 — Sprite engine

Only now design sprites.

Implement the smallest useful sprite abstraction based on actual game requirements.

Potential properties:

```text
x
y
tile/image
palette/color
flip X
flip Y
priority
visibility
```

Do not assume these are final.

The software VM is the reference implementation.

The eventual FPGA implementation must be able to reproduce the same semantics.

---

# 24. Milestone 20 — Tilemap engine

Implement tilemaps.

Potential concepts:

```text
tilemap
tiles
tile attributes
scroll position
layer
```

Again, derive the design from actual games rather than designing a general-purpose GPU.

---

# 25. Milestone 21 — Scrolling

Add camera/scrolling support.

At minimum investigate:

```text
horizontal scrolling
vertical scrolling
```

Test against a large tilemap.

Do not assume the final output resolution.

The graphics API should use logical coordinates and remain independent of physical display resolution.

---

# 26. Milestone 22 — Platformer

Build a small platformer.

This should exercise:

- tilemaps;
- sprites;
- scrolling;
- collision;
- animation;
- multiple layers;
- input;
- audio.

After this milestone, stop and review the architecture.

Do NOT immediately continue adding features.

Instead produce an architecture review answering:

1. What does ARCADE-1 actually need?
2. Which features are unnecessary?
3. What are the real graphics bandwidth requirements?
4. What should the FPGA renderer look like?
5. What should the final memory map look like?
6. What should the final game binary format look like?
7. What should remain software?
8. What should become hardware?
9. What should be explicitly excluded?

Only then define ARCADE-1 v0.1.

---

# 27. Graphics philosophy

ARCADE-1 is intended primarily for **2D games**.

The eventual renderer should be optimized around things such as:

```text
sprites
tilemaps
layers
scrolling
priority
bitmap fonts
simple primitives
```

Do not turn ARCADE-1 into a miniature PC GPU.

The likely eventual architecture is something like:

```text
                ARCADE-1 2D ENGINE

       ┌──────────┬──────────┬──────────┐
       │          │          │          │
    tilemaps   sprites   primitives   text
       │          │          │          │
       └──────────┴──────────┴──────────┘
                       │
                   compositor
                       │
                  video output
```

A framebuffer may be used by the software reference implementation.

It is NOT yet decided whether the FPGA implementation will use a framebuffer.

The eventual hardware may instead use a scanline/line-buffer architecture.

---

# 28. Resolution philosophy

Do NOT define ARCADE-1 as a 720p machine.

Do NOT define ARCADE-1 as a 1080p machine yet.

Display output and logical game coordinates are separate concepts.

The eventual target is modern HD television use.

A likely reference output is 1920×1080 @ 60 Hz, but this remains a design decision for a later stage.

Games may eventually use logical resolutions such as:

```text
320×180
640×360
960×540
1280×720
1920×1080
```

or another coordinate system.

Do not make this decision prematurely.

---

# 29. Libretro philosophy

Libretro is a **host adapter**, not part of the ARCADE-1 architecture.

Correct:

```text
ARCADE-1 machine
       │
       ▼
Libretro adapter
       │
       ▼
RetroArch
```

Incorrect:

```text
ARCADE-1 machine
       │
       └── depends directly on RetroArch
```

The core should eventually be usable by another frontend or standalone test harness without changing the ARCADE-1 machine.

---

# 30. Testing philosophy

Tests should exist at multiple levels.

### CPU

Instruction-level tests.

### Memory

Read/write/boundary tests.

### Devices

MMIO tests.

### Machine

Reset/frame execution tests.

### Graphics

Deterministic rendering tests.

### Games

Golden-frame or behavior tests where practical.

The same ARCADE-1 game should eventually be executable by:

```text
Linux Libretro
Android Libretro
FPGA
```

with equivalent machine behavior.

---

# 31. Debugging

Build useful debugging facilities early.

At minimum eventually support:

```text
register dump
PC
instruction trace
memory inspection
MMIO tracing
frame counter
```

A debug mode such as:

```text
arcade1 --trace game.arc1
```

would be useful, but do not implement a CLI merely for the sake of having one.

Prioritize facilities that actually help development.

---

# 32. What NOT to build

Until explicitly required, do not add:

- operating system;
- multitasking;
- virtual memory;
- filesystem;
- networking;
- 3D graphics;
- OpenGL/Vulkan dependency;
- scripting language;
- JIT compiler;
- GPU shaders;
- dynamic module system;
- cloud services;
- online multiplayer;
- achievements;
- launcher UI;
- Android-specific machine code;
- FPGA-specific behavior in the reference VM.

Keep the machine small.

---

# 33. Definition of success

The early success condition is NOT:

> "We have designed a complete console."

It is:

```text
C game
   ↓
RV32 compiler
   ↓
ARCADE-1 ROM
   ↓
ARCADE-1 VM
   ↓
Libretro core
   ↓
RetroArch
   ↓
screen + controller + audio
```

running an actual game.

The later success condition is:

```text
                  same game.arc1
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
       software ARCADE-1          FPGA
             VM
              │                     │
          RetroArch                HDMI
              │                     │
          Linux/TV                 TV
```

The same machine specification should describe both.

---

# 34. Agent behavior

The coding agent should behave as an engineer working incrementally.

For every milestone:

### Before coding

- inspect the repository;
- identify existing code;
- identify the current build/test commands;
- understand the current architecture;
- state briefly what it intends to change.

### During coding

- make minimal changes;
- preserve working functionality;
- don't rewrite unrelated code;
- don't add speculative features.

### After coding

Run the strongest available validation:

```text
build
unit tests
integration tests
manual execution
```

If a required toolchain is missing, report it explicitly.

Never claim something was tested if it wasn't.

### At the end of each milestone

Report:

```text
STATUS: COMPLETE / BLOCKED

Implemented:
...

Tests:
...

Known limitations:
...

Files changed:
...
```

Then STOP.

Do not automatically continue to the next milestone.

---

# 35. Current starting point

Start at:

**Milestone 01 — Libretro skeleton.**

Do not implement RV32.

Do not implement graphics.

Do not implement ARCADE-1.

First establish the smallest working Libretro core and prove that it can be built and loaded by RetroArch.

Once that works, proceed to Milestone 02.