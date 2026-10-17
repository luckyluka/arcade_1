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

## MMIO decisions (pinned at M11)

The MMIO contract is expensive to rework later, so it is fixed here now.
Specific device registers are appended to the table as each device is
implemented (M12 timer, M13 video, ...).

**Region & access**

- Window `0x4000..0x4FFF` (4 KiB); `0x2000..0x3FFF` is an unused gap, keeping
  MMIO a distinct region rather than a continuation of RAM.
- Registers are 32-bit, accessed with `lw`/`sw` only. 8/16-bit accesses to the
  window are no-ops (read 0, write ignored).
- One host handler services the whole window and dispatches on the byte offset.
  With no device attached, reads return 0 and writes are ignored.

**When a write takes effect**

- Synchronously, at the instruction that performs the `sw`, in program order,
  within that CPU step. No buffering and no frame-end deferral. A `lw` observes
  device state at the instruction that performs it. This is what keeps
  execution deterministic and identical across hosts.

**Register table** — the canonical list of machine device registers. It was
empty at M11 (infrastructure only); each device appends its rows here when it
lands. The timer's rows were pinned at M12, the video device's at M13, the
input and audio devices' at M15, the sprite engine's additions at M19, and
the tilemap engine's additions at M20:

| Offset | Register    | Access | Function | When it takes effect |
|--------|-------------|--------|----------|----------------------|
| 0x00   | TIMER_COUNT | R      | Tick count, +1 per completed frame while enabled. Read-only: writes are ignored by the device. | Advances once per `arcade1_run_frame()` while CTRL bit0 is set; a `lw` observes the value at the instruction that performs it |
| 0x04   | TIMER_CTRL  | R/W    | bit0 = enable (1) / pause (0); all other bits are ignored on write and read as 0 | The register latches at the `sw` instruction; the new state governs every frame that completes after that write |
| 0x100  | VIDEO_X     | R/W    | Draw X coordinate for SET_PIXEL / FILL / SPRITE, kept exactly as written; the draw itself clips to the screen (X is treated as **signed** by FILL/SPRITE). For TILEMAP, X is the map's origin column on screen (also **signed** — a negative origin clips the left columns) | Latches at the `sw`; read by the next draw command |
| 0x104  | VIDEO_Y     | R/W    | Draw Y coordinate for SET_PIXEL / FILL / SPRITE, kept exactly as written; the draw itself clips to the screen (Y is treated as **signed** by FILL/SPRITE). For TILEMAP, Y is the map's origin row on screen (also **signed**) | Latches at the `sw`; read by the next draw command |
| 0x108  | VIDEO_COLOR | R/W    | RGB565 colour for CLEAR/SET_PIXEL/FILL/SPRITE, masked to 16 bits. Not used by TILEMAP (colour is per-cell) | Latches at the `sw` |
| 0x10C  | VIDEO_CMD   | W      | 1 = CLEAR (fill screen with COLOR), 2 = SET_PIXEL (plot COLOR at X,Y), 3 = PRESENT (count a presented frame), 4 = FILL (fill the W×H rectangle at X,Y with COLOR), 5 = SPRITE (blit the 1-bpp image at ADDR, W×H, at X,Y), 6 = TILEMAP (draw the COLS×ROWS grid of 32-bit cells at MAP_ADDR, using tiles at TILE_ADDR); other values ignored | Executes synchronously at the `sw`: the framebuffer / present counter changes at that instruction |
| 0x110  | VIDEO_STATUS | R     | Number of PRESENT commands executed | Increments at the `sw` of each PRESENT |
| 0x114  | VIDEO_ADDR  | R/W    | Guest address of a SPRITE's 1-bpp image data (must lie in ROM `0x0000..0x0FFF` or RAM `0x1000..0x1FFF`; anything else reads as all-transparent) | Latches at the `sw`; read by the next SPRITE command |
| 0x118  | VIDEO_W     | R/W    | Width in pixels for FILL/SPRITE, kept exactly as written. For TILEMAP, W is one tile's width in pixels (the cell pitch) | Latches at the `sw`; read by the next FILL/SPRITE/TILEMAP |
| 0x11C  | VIDEO_H     | R/W    | Height in pixels for FILL/SPRITE, kept exactly as written. For TILEMAP, H is one tile's height in pixels (the cell pitch) | Latches at the `sw`; read by the next FILL/SPRITE/TILEMAP |
| 0x120  | VIDEO_MAP_ADDR | R/W | Guest address of a TILEMAP's cell array: COLS×ROWS little-endian 32-bit words, each `tile_index | (rgb565 << 16)` (index 0 = empty). Must lie in ROM or RAM; anything else reads as all-empty (no side effects) | Latches at the `sw`; read by the next TILEMAP command |
| 0x124  | VIDEO_TILE_ADDR | R/W | Guest address of a TILEMAP's tile-set image data: the same 1-bpp format as a SPRITE. Tile N (>= 1) lives at TILE_ADDR + (N-1)·(H·ceil(W/8)). Must lie in ROM or RAM | Latches at the `sw`; read by the next TILEMAP command |
| 0x128  | VIDEO_COLS  | R/W    | TILEMAP grid width in cells (0 = draws nothing) | Latches at the `sw`; read by the next TILEMAP command |
| 0x12C  | VIDEO_ROWS  | R/W    | TILEMAP grid height in cells (0 = draws nothing) | Latches at the `sw`; read by the next TILEMAP command |
| 0x130  | VIDEO_CAM_X | R/W    | Camera X offset in **signed** pixels. The two world-space commands — SPRITE and TILEMAP — draw at (X − CAM_X, Y − CAM_Y); the screen-space primitives (CLEAR / SET_PIXEL / FILL) are never scrolled. Power-on 0 = no offset | Latches at the `sw`; read by the next SPRITE/TILEMAP command |
| 0x134  | VIDEO_CAM_Y | R/W    | Camera Y offset in **signed** pixels (see CAM_X) | Latches at the `sw`; read by the next SPRITE/TILEMAP command |
| 0x200  | INPUT_PAD   | R      | RetroPad for the current frame: bit0 up, bit1 down, bit2 left, bit3 right, bit4 A, bit5 B, bit6 start, bit7 select; other bits read as 0 | Reflects the `arcade1_input_t` the host set for this frame (`arcade1_set_input`, called before the frame starts); read-only, writes ignored |
| 0x300  | AUDIO_FREQ  | R/W    | Tone frequency in Hz (power-on 440 — the historical demo beep). 0 = constant level while enabled | Latches at the `sw`; shapes **this** frame's samples, because the machine renders audio after the guest has run |
| 0x304  | AUDIO_CTRL  | R/W    | bit0 = tone enable (power-on: set); other bits ignored on write and read as 0 | Disabled = silence for this frame; takes effect the same way as FREQ |

(The machine registers a **single** handler for the whole window — the M11
contract — and muxes it internally: `0x000..0x0FF` is the timer, `0x100..0x1FF`
the video device, `0x200..0x2FF` the input device (read-only), `0x300..0x3FF`
the audio device; unknown offsets read 0 / ignore writes. Every access thus
reaches a device, so `stray_accesses` stays 0. M11's harness
`tests/test_mmio.c` demonstrates read/write dispatch with a scratch register
pair that lives in the test.)

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

## Timer decisions (pinned at M12)

- **Tick source: one tick per frame.** `TIMER_COUNT` advances exactly once
  per completed `arcade1_run_frame()` while enabled. Nothing reads a host
  wall clock, so the count is a pure function of the frames executed and is
  identical on every host; the host/frontend only decides how fast frames are
  produced.
- **Registers:** `TIMER_COUNT` (R, read-only) at offset 0x00 and `TIMER_CTRL`
  (R/W, bit0 = enable) at offset 0x04, appended to the register table in
  §15. Unknown offsets read 0 and ignore writes.
- **Relationship:** the CPU counts instructions (`instret`), the machine
  counts frames, and the timer hands frame time to the guest over MMIO. If
  the timer is never paused, `TIMER_COUNT == frame_counter`.
- **Scope:** the timer has no interrupt/pending logic yet, and no guest
  firmware reads it until the CPU is wired into the frame loop (later
  milestone) — `tests/test_timer.c` drives frames and reads the registers
  from the host and from a hand-assembled `lw`.

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

## Video decisions (pinned at M13)

- **Window split:** the machine keeps the M11 contract of ONE handler for the
  window and muxes it internally — `0x000..0x0FF` is the timer, `0x100..` is
  the video device. Unknown offsets read 0 / ignore writes (still serviced by
  the handler, so never stray).
- **Registers:** `VIDEO_X`, `VIDEO_Y`, `VIDEO_COLOR` (R/W), `VIDEO_CMD` (W:
  1 = CLEAR, 2 = SET_PIXEL, 3 = PRESENT) and `VIDEO_STATUS` (R: PRESENTs
  executed), appended to the register table in §15.
- **Single buffer, no flip:** the device draws directly into the machine's
  scanout framebuffer — the RGB565 buffer Libretro reads. `PRESENT` only
  counts executions; buffering/flip policy stays deliberately undecided
  (docs.md: do NOT finalize the graphics architecture yet).
- **Clipping:** SET_PIXEL ignores plots outside 320x240 (coordinates still
  read back as written), COLOR is masked to 16 bits, CLEAR fills the whole
  screen.
- **Reset:** `arcade1_reset()` restores the power-on video state — black
  screen, zeroed registers and present counter (the framebuffer pointer and
  the window's MMIO attachment survive, like RAM/ROM semantics).
- **Scope:** the host demo rendering inside `arcade1_run_frame()` still paints
  the test pattern each frame for now, so guest pixels land *between* frames;
  full guest takeover of the screen arrives when the CPU is wired into the
  frame loop (later milestone).

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

## Graphics API decisions (pinned at M14)

- **Guest-side layer, no new hardware:** the API is `programs/gfx.h`
  (header-only, `static inline`), compiled into guest programs. It maps onto
  the M13 video device's registers — so **no register-table rows are added at
  M14**: `clear` -> `VIDEO_CMD = 1`, `rectangle` -> a loop of `VIDEO_CMD = 2`
  plots with `VIDEO_COLOR` set once per call, `present` -> `VIDEO_CMD = 3`.
- **Signatures:** `gfx_clear(color)`, `gfx_rect(x, y, w, h, color)`,
  `gfx_present()` — signed screen coordinates with top-left origin, RGB565
  colours. `w/h <= 0` draws nothing.
- **Primitive by design:** a rectangle costs one `sw` per pixel (no hardware
  rectangle, no acceleration). Anything outside the screen — including
  negative x/y — is clipped per pixel by the device, so a partly off-screen
  rectangle is safe to draw. *This per-pixel loop was retired at M19, when
  `gfx_rect` became a single device-side FILL command (see "Sprite
  decisions, pinned at M19"): same signature, same clipped pixels, one
  command instead of w*h of them. The M14 state hash changed accordingly
  (`0x3D91642CBF636CF1` -> `0x96E1742EE36B68B2`) because the guest executes
  a different instruction stream for the identical picture.*
- **Explicitly excluded** (per the milestone text): sprites, tilemaps,
  textures, shaders, rotation, alpha blending — none of it exists at M14.
- **Scope:** at M14, guest drawing still landed *between* frames while the
  host demo repainted every `arcade1_run_frame()` (same as M13). M15 changed
  that for firmware runs: with firmware attached the demo does not repaint, so
  the guest's presented frame *is* the frame (see M15 below). The M15 game
  (Breakout) uses this API as its drawing primitive.

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

## Firmware and frame protocol (pinned at M15)

- **The machine owns the CPU.** `arcade1_machine_t` now contains an
  `arcade1_cpu_t`; `arcade1_set_firmware(m, true)` boots guest code from the
  reset vector (0x00000000) on the next `arcade1_run_frame`. `arcade1_reset`
  reboots the guest too: ROM contents and the firmware flag persist, everything
  else (RAM, CPU, devices, timer, framebuffer) returns to power-on state, so a
  reset is a deterministic cold boot.
- **One host frame = one guest loop iteration.** `arcade1_run_frame` runs the
  guest until it executes a PRESENT write (via `gfx_present`), until the PC
  stops changing (crt0's `j .` park), or until a 1,000,000-instruction safety
  cap. Game speed is therefore locked to the host's 60 fps, deterministically;
  a guest that never presents is bounded by the cap, never by wall-clock.
  **Frame boundaries** — the exact end-of-frame rules (which of the three
  ends a frame first, where the next frame resumes, and what the timer,
  `STATUS` and the host see in each case) are specified in `SPEC.md` §8
  "Frame boundaries" and pinned by `make test-frame-rules`. That note is the
  normative description; this bullet is the M15 summary of it.
- **Demo mode is unchanged.** While no firmware is attached, `run_frame`
  behaves exactly as M02–M06 defined it (test pattern, moving square, pad
  overlay, input-driven state); with firmware attached, the host demo does not
  render — the guest owns the frame.
- **Input:** `INPUT_PAD` (0x200, read-only) reflects the host's
  `arcade1_set_input` for the current frame, so the guest always sees a stable
  pad for the whole frame. Rows in the register table above.
- **Audio:** the device powers on at 440 Hz enabled — the historical M05 demo
  beep — so the audio pipeline is exercised even with no guest code. Breakout
  silences it at boot and beeps on events (brick 880 Hz, paddle 660 Hz, death
  220 Hz, game over 110 Hz, restart 440 Hz), each beep lasting 6 timer ticks.
  The waveform is rendered by the machine *after* the guest runs, so a register
  write at frame N already shapes frame N's samples.
- **Libretro:** `retro_load_game` boots content when `game->data` is non-NULL
  (the file is the guest ROM, must fit the 4 KiB ROM); with no content the core
  keeps its no-content demo mode. This also surfaced that the core had never
  exported `retro_api_version` — RetroArch tolerates that in menu mode but
  fatals loading content, so the M15 fix adds the required symbol.
- **Guest constraints:** at M15, Breakout was built `-march=rv32i` without
  libgcc, so it contained no variable multiplication or division (score digits
  are produced by subtraction) and ran entirely on the M09
  interpreter. (Lifted at M16: the guest toolchain moved to `-march=rv32im`,
  so the current build does use `mul` — see the M16 decisions below.) The full
  chain works: `breakout.c` -> RISC-V GCC -> ROM -> RV32IM VM ->
  video/audio/input devices -> Libretro -> RetroArch, and the game is also
  headless-runnable with
  a printed hash per frame (`make test-breakout`, per the Games testing rule).
- **Measured CPU load:** ~77.8k instructions per frame on average (80.5k max,
  full-redraw strategy: clear + repaint all rectangles every frame).

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

## M extension decisions (pinned at M16)

- **Decode:** the 8 operations live on the existing OP major opcode
  (`0x33`) with `funct7 = 0000001`; one `funct3` value each (MUL..REMU).
  The decoder classifies them (`ARCADE1_OP_MUL` .. `ARCADE1_OP_REMU`) —
  no new formats, no new fields, no decoder state. `funct7 = 0x01` on
  OP-IMM stays UNKNOWN: M exists only on OP.
- **Execute, spec-exact, no traps:** results come from 64-bit
  intermediates (no host overflow). Division truncates toward zero (C99
  `/`, `%`, which is what the spec requires). The two spec-defined edge
  cases return defined values instead of faulting: division by zero
  yields all-ones for `DIV`/`DIVU` and the dividend for `REM`/`REMU`;
  the signed overflow case (`INT_MIN / -1`) yields `INT_MIN` for `DIV`
  and `0` for `REM`. Nothing traps, so a guest cannot fault the VM —
  determinism is preserved.
- **x0 and counters unchanged:** writes to `x0` are discarded exactly
  like every other instruction, and each M instruction counts once in
  `instret` — covered by tests.
- **Guest toolchain now `-march=rv32im`:** the point of the extension
  is that games may use `*` and `/` directly. Guests still build
  freestanding with no libc and no libgcc (rv32im needs neither).
  Breakout now contains real `mul` instructions (3, down from a
  shift/add chain — the binary went 1714 -> 1718 bytes); the other four
  demo guests recompiled byte-identical, so the M10–M14 printed hashes
  are unchanged. Breakout's per-frame and final hashes changed once with
  the recompile and are stable from M16 on.
- **Testing:** decoder classification of all 8 ops (plus the negative
  cases) in `tests/test_cpu.c`; execution semantics including the
  div-by-zero and signed-overflow edges in `tests/test_exec.c`; full
  regression of every earlier harness to prove RV32I behaviour is
  untouched.

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

## ARCADE-1 image format decisions (pinned at M17)

- **The format, v1** — a 16-byte header followed by the payload (the
  same flat RV32IM image the VM has always loaded into its 4 KiB ROM):

  ```text
  offset  size  field
  0       4     magic   "ARC1"
  4       1     version (currently 1)
  5       3     reserved, must be 0
  8       4     payload_size   (little-endian u32, 1..4096)
  12      4     payload_crc32  (little-endian u32, IEEE CRC-32)
  16      N     payload        (N == payload_size)
  ```

  Every field earns its place: magic = unambiguous detection, version =
  a home for future incompatible layouts, size = exact framing (a
  truncated or padded file is rejected, never half-loaded), CRC = the
  transfer-corruption check (the trip to a Google TV made that concrete).
- **Deliberately excluded, per the milestone text**: metadata text,
  asset sections, compression, signatures, multi-file anything. The
  reserved bytes and the version byte are the only extension room.
- **One implementation, two consumers**: the parser, CRC-32 and header
  writer live in `src/arcade1/rom.{h,c}` (pure, no machine/libretro
  dependency); the host packer `tools/packarc1.c` links that same code
  to *write* images, so writer and reader can never disagree.
- **Loading**: the libretro adapter accepts an `.arc1` image (detected
  by magic; any parse failure refuses to boot) or — kept for backward
  compatibility with M15 content — a raw flat image, which is treated
  exactly as before. Raw support is legacy compatibility, not a format
  feature; new games ship as `.arc1`.
- **Rejection, not traps**: a bad image makes `retro_load_game` return
  false with a distinct reason (`arcade1_image_strerror`); the machine
  never sees invalid bytes. Valid images are transparent: the payload
  is the identical byte stream, so all state hashes are unchanged.
- **Testing**: `make test-rom` (CRC test vector, header round-trip,
  payload -> ROM load, and every rejection: short, magic, version,
  size, checksum, plus a full 4 KiB payload); `make test-breakout`
  now boots `breakout.arc1` and must reproduce the M16 state hash
  `0x052E8B24448A4ADD` exactly — format transparency asserted as a
  determinism test; `make test-program` still loads a raw `.bin`, so
  the compatibility path stays covered too. `make program` now emits
  both `game.bin` and `game.arc1` per guest. *(The M19 sprite engine
  later re-pinned this hash to `0x7816DD30A2B9D131` — see "Sprite
  decisions, pinned at M19"; the `.arc1`-vs-`.bin` transparency
  property itself is unchanged.)*

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

## Tetris decisions (pinned at M18)

- **Playfield**: 10x20 grid, cell pitch 10 px (a 9 px block + 1 px gap
  instead of grid lines), origin (10,10); a 2 px frame around it; the
  right-hand panel (x=130) carries SCORE/LINES/LEVEL labels + digits and
  a NEXT preview box. Everything is `gfx_rect`/`gfx_present` — no new
  device, no sprite hardware, per the milestone text.
- **Repeated objects**: one shape table, 7 tetrominoes, each stored as
  4 cells of a 4x4 box (28 bytes). Rotation is math — `(r,c) -> (c,3-r)`
  applied `rot` times — never a stored per-rotation table; O is
  rotation-stable and skips it. The same table drives drawing, collision
  and the NEXT preview.
- **Text**: the 3x5 scale-2 font from Breakout (one byte per row, bit2 =
  left) extended to 14 letters (A C E G I L M N O P R S T V) for the
  labels and the GAME OVER / PRESS START screen; digits unchanged.
- **Deterministic timing**: gravity is `TIMER_COUNT`-driven (30 ticks at
  level 0, -2 per level, floor 4); horizontal input = press edge then
  every 3rd tick; soft drop = every 2nd tick. No host time anywhere.
- **No entropy, on purpose**: the machine has no random device, so the
  piece stream is xorshift32 with the fixed seed `0x1234ABCD` — first
  pieces L O L Z T J ...; two identical runs are the same game, and
  START rewinds the seed so a restart replays from the opening piece.
  Adding an entropy device later would break reset-and-compare, so this
  stays a property, not a workaround.
- **Game state**: score = {0,100,300,500,800}[simultaneous clears] x
  (level+1), level = lines/10 capped at 15; two states (PLAY, OVER);
  game over = spawn collision; full rows clear with the classic
  shift-down (consecutive rows re-checked in place).
- **Audio events** (6 ticks each): lock 440, line clear 880, game over
  110, restart 220; the guest silences the power-on tone in its boot
  frame, so boot is silent until the first event.
- **What is missing (the observation this milestone exists for)**:
  every frame repaints all 200 cells + panel + piece unconditionally —
  245,797 instructions/frame average (299,269 max), 3.2x Breakout's
  77,836 — because nothing in the API says *what changed*. A dirty
  region/cell update, a text primitive (each glyph is 15 rect calls)
  and a sprite/cell blit are the concrete candidates for M19; the audio
  envelope (guest must time beep expiry against the timer) and double
  buffering are noted but stay out of scope.
- **Testing**: `make test-tetris` runs a 300-frame scripted game twice
  (reset-and-compare: identical per-frame hashes + byte-identical
  framebuffer). The script checks the pinned PRNG stream, byte-exact
  playfields after each of 4 locks, rotation incl. rejected rotation,
  wall clamps (box origin may hang off-screen; CELLS clamp at column 0),
  a line clear with marker-row compaction verified over all 200 bytes,
  game over, restart-to-boot-state, timer == frames, presents == frames,
  stray accesses == 0. Final state hash `0x982F9F026B07E9C2` — re-pinned
  to `0x0D742BEFFEBA2C3E` at M19, when the same picture began costing
  9,384 instructions/frame instead of 245,797 (the pixel assertions are
  unchanged; only the executed instruction stream differs).
- **Fit**: 3,066-byte payload of the 4 KiB ROM budget (1,030 bytes
  spare); the shipped `tetris.arc1` is 3,082 bytes with the header.

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

## Sprite decisions (pinned at M19)

The property list above is the starting point; what the two shipped games
actually need is narrower. Breakout's ball/paddle/bricks and Tetris' blocks
and panel are all *solid rectangles*, and the one thing neither `CLEAR` nor
`SET_PIXEL` could do cheaply was a **block of pixels in one command** — the
per-pixel loop lived in the guest, which is why M18 measured 245,797
instructions/frame for Tetris.

So the smallest abstraction that pays for itself is two device-side commands
in the video device (the same parameter registers, two new command values,
three new registers — the register table above is updated in place):

- **`FILL`** — the W×H rectangle at (X,Y) filled with COLOR. This is the
  degenerate sprite: a flat-colour object with no image.
- **`SPRITE`** — the 1-bpp image at (ADDR, W×H) blitted at (X,Y): set bits
  are drawn in COLOR, **clear bits are left untouched (transparent)**.

Implemented properties: **x, y, image (optional), colour**. Deliberately
*not* implemented, because no shipped game needs them yet (docs.md: build
only what the games need — revisit when a real game asks):

- **flip X / flip Y** — useful for a future platformer; a blit engine gains
  them cheaply later.
- **priority** — draw order is the compositor; painter's order wins, which
  is what both games already rely on.
- **visibility flag** — an unneeded sprite is simply not drawn (no command).

**Image format** (also what the FPGA must reproduce): rows of pixels,
row-major, each row padded to a whole byte; in each byte the **MSB is the
leftmost** pixel. Bit `(7 - (col & 7))` of byte
`addr + row * ((w + 7) / 8) + col / 8`. `gfx_sprite()` in the guest API
takes a plain `const void *` to such an array.

**Sources and safety**: the device fetches image bytes only from guest ROM
(`0x0000..0x0FFF`) and RAM (`0x1000..0x1FFF`); anything else — in particular
the MMIO window itself — reads as zero bits, so a sprite can never have side
effects on another device. These are device-side fetches, not CPU accesses,
so `stray_accesses` stays untouched.

**Clipping**: X/Y are *signed* coordinates and W/H are sizes; both commands
draw the intersection of `[x, x+w) × [y, y+h)` with the screen, computed in
64-bit so no coordinate can wrap around an edge. This reproduces exactly
what the old per-pixel loop drew for every value the guest API can produce
(negative origins show their right/bottom part; right/bottom overhangs are
cut), so the framebuffer bytes are unchanged by this milestone — only the
per-pixel loop moved from the guest into the device. A fully off-screen or
zero-size block draws nothing. `SET_PIXEL` (M13) keeps its original
per-pixel semantics untouched.

**Guest API**: `gfx_rect()` keeps its signature and its pixels but is now
**one FILL command** instead of a loop of `SET_PIXEL`s. The new
`gfx_sprite(x, y, img, w, h, color)` issues a SPRITE. `gfx_clear` /
`gfx_present` are unchanged.

**Measured effect** (the payoff the M18 observation asked for): both games
now repaint with one command per object instead of one `sw` per pixel.
Breakout: 77,836 -> **2,039** instructions/frame average (595 frames).
Tetris: 245,797 -> **9,384** instructions/frame average, 16,678 max (300
frames) — 26x faster, and now only ~2x Breakout instead of 3.2x. The
remaining per-frame cost is game logic and the per-cell/per-glyph command
issue, which is what M20's tilemap engine is for. State hashes of the two
games changed accordingly (the CPU executes a different instruction stream
for the same picture); their framebuffer pixel assertions are unchanged and
still byte-exact.

**Testing**: `make test-sprite` boots `sprite_demo.c` on the machine's own
CPU and verifies every affected pixel: exact counts (red 32, green 16,
white 8, yellow 32, blue 76,712), MSB-first bit order, transparent clear
bits, a RAM copy rendering identically to the ROM original, right/bottom
clipping, negative origins, fully off-screen / zero-size / illegal-source
draws leaving nothing behind, register readbacks (X/Y/W/H/COLOR/ADDR), the
timer and `stray_accesses` undisturbed, and reset-and-compare determinism
(identical framebuffer + state hash `0xC970CDBE677933C9`). `make test-gfx`
(M14) still passes with the new FILL-based `gfx_rect`, with its register
readback updated to the rectangle's parameters.

**Fit**: `sprite_demo` is a 548-byte payload; `gfx.h` grew by one function
and three register defines; `video.{c,h}` gained two commands, three
registers and the ROM/RAM borrows the machine already had. No new device,
no new MMIO window, no buffering change.

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

## Tilemap decisions (pinned at M20)

Both shipped games draw a grid where each cell is a *tile reference plus a
per-cell colour*: Tetris a 10×20 field with 7 piece colours, Breakout a 5×10
brick field with per-row colours. That is "tiles + tile attributes," and the
one attribute both actually need is **colour** — without it neither game could
use a tilemap at all. So M20 implements exactly that and nothing more; scroll
position (M21) and layers are deliberately out of scope.

**Cell** = one 32-bit little-endian word: bits [15:0] = tile index
(**0 = empty**, skipped so the background shows through), bits [31:16] = an
RGB565 tint applied to that tile's set bits. One word per cell is
FPGA-friendly (a single aligned fetch) and lets a map initialised to zeros be
blank. Per-cell colour is the one "tile attribute" both games need.

**Tiles** are the *same* 1-bpp images the SPRITE command (M19) uses: row-major,
rows padded to whole bytes, MSB = leftmost. Tile N (>= 1) lives at
`TILE_ADDR + (N-1) · tile_bytes`, `tile_bytes = H · ceil(W/8)`. Index 0 is
reserved as "empty" (never fetched). Reusing the sprite format means one pixel
pipeline, not two — critical for FPGA reproducibility.

**Registers** (CMD 6 = TILEMAP): reuse X/Y (0x100/0x104) as the map's signed
origin on screen and W/H (0x118/0x11C) as one tile's size; add MAP_ADDR
(0x120, the cell array), TILE_ADDR (0x124, the tile-set image), COLS (0x128),
ROWS (0x12C). COLOR is unused (colour is per-cell). Cell (col, row) lands at
`(X + col·W, Y + row·H)`; each tile clips to the screen exactly like a sprite
(negative origin shows its right/bottom part, right/bottom overhangs are cut),
computed in 64-bit so no coordinate wraps. A zero W/H or COLS/ROWS draws
nothing. Like SPRITE, cell and tile fetches read only from ROM/RAM; anything
else (including the MMIO window) reads as empty/transparent, so a map can
never disturb a device and `stray_accesses` stays 0.

**Guest API**: `gfx_tilemap(x, y, cells, tiles, cols, rows, tile_w, tile_h)`
issues one TILEMAP. `gfx_clear` / `gfx_rect` / `gfx_sprite` / `gfx_present`
are unchanged.

**Adopted in Tetris** — the field *became* the tilemap: `field[]` is now the
200-cell array itself (single source of truth for collision AND rendering).
Occupied cells store `1u | (pcol[piece] << 16)` (tile 1 = a 9×9 block inside a
10×10 cell, reproducing the old per-cell `gfx_rect` exactly, gap included); the
per-cell colour carries the piece identity, so one tile renders all seven
pieces. `draw()` is now one `gfx_tilemap` call — no per-frame repaint loop —
and lock/clear only touch the cells that change. Breakout was **not** converted:
its bricks are a per-row bitmask (not a cell grid) and it is already at ~2k
inst/frame; the payoff there is marginal, so per "smallest change" it stays on
FILL. The tilemap's real remaining consumer is M22's platformer.

**Measured effect** (Tetris): 9,384 -> **8,079** instructions/frame average,
16,678 -> **14,309** max (300 frames). The field-repaint loop is gone; the
residual is game logic, the falling/preview piece, and the per-glyph panel
text. State hash re-pinned; framebuffer pixel assertions are byte-identical
(the 9×9 block is unchanged).

**Testing**: `make test-tilemap` boots `tilemap_demo.c` on the machine's own
CPU and re-renders **every pixel** from an independent model written from the
documented semantics (not by calling the device): 4 distinct tiles at 8×8,
per-cell colour, empty cells, right/bottom overhang, negative origin, a RAM
cell source rendering identically to the ROM original, an illegal (MMIO-window)
cell source and zero-size guards drawing nothing, register readbacks
(X/Y/W/H/MAP/TILE/COLS/ROWS), timer and `stray_accesses` undisturbed, and
reset-and-compare determinism. `make test-tetris` re-verified the full game
script against the new 32-bit field representation.

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

## Scrolling decisions (pinned at M21)

**What the camera is.** A 2D view offset. The two *world-space* commands —
SPRITE (M19) and TILEMAP (M20) — draw at `(X − CAM_X, Y − CAM_Y)`; the
*screen-space* primitives (CLEAR / SET_PIXEL / FILL) are never scrolled, so a
guest keeps its HUD at fixed screen coordinates and its world at stable world
coordinates, moving only the camera each frame. Power-on camera is (0,0),
which reproduces M19/M20 exactly (every pre-existing state hash is unchanged).

**Registers** (added at 0x130/0x134): CAM_X and CAM_Y, signed pixel offsets,
latching at the `sw` and read by the next SPRITE/TILEMAP command. Guest API:
`gfx_camera(cam_x, cam_y)`.

**Honest scope.** Functional scrolling already existed before M21: X/Y are
signed, and a map or sprite could simply be drawn at `(world − camera)` by the
guest with per-frame clipping. The camera register is therefore *ergonomic and
consistency* (one place to pan the whole world, sprites and maps stay in sync)
rather than new raw capability. Its one substantive addition is below.

**Device windowing (the bandwidth answer).** TILEMAP now visits only the
columns/rows that are even partially on screen, computed from the camera in
64-bit; a cell fully outside the viewport is skipped before any cell/tile
fetch. Skipping such a cell cannot change a pixel — the per-tile clip would
have drawn nothing for it — so the framebuffer is identical to visiting every
cell, but a map far larger than the screen costs only its visible cells in
fetches. This is what makes a big scrolling map affordable and is the direct
input to M23's "what are the real graphics bandwidth requirements?".

**Logical coordinates / resolution independence.** `programs/gfx.h` never
hardcodes the screen size: the camera and all coordinates are in logical
pixels, and the device (not the guest) clips to the physical 320×240. A guest
written against gfx.h is therefore independent of the physical display
resolution, satisfying the milestone's requirement.

**A real memory-map limit surfaced.** With the current 4 KiB ROM + 4 KiB RAM,
a cell grid large enough to exceed the screen in *both* axes does not fit at
small tile sizes: at 8 px tiles, a map bigger than 320×240 needs > 40×30 =
1,200 cells = 4,800 bytes — over the budget. The demo therefore uses **16 px**
tiles and a 32×22 map (512×352 world): the 704 cells = 2,816 bytes fit in RAM
and scroll both axes (192 px horizontal, 112 px vertical). This is a concrete
data point for M23's "what should the final memory map look like?" — bigger
RAM (or a procedural/windowed map) is needed for full-screen-plus maps at fine
tile sizes.

**Testing**: `make test-scroll` boots `scroll_demo.c` on the machine's own CPU
as firmware and, for **120 frames**, re-renders **every pixel of every frame**
from an independent model and memcmps it against the real framebuffer. The
model reads the demo's 704 map cells straight out of guest RAM, uses its own
hand-mirrored tiles and sprite, computes the expected camera independently from
the frame index (and asserts the CAM_X/CAM_Y register readback matches, so a
wrong camera register is caught), and visits every cell then clips — proving
the device's windowing is pixel-identical. It also asserts the camera-dependent
invariants (world sprite pans, HUD stays fixed), register readbacks, one
present and one timer tick per frame, `stray_accesses == 0`, and
reset-and-compare determinism (identical 120-frame hash sequence +
byte-identical final framebuffer). Steady-state guest CPU load is ~49
instructions/frame (one TILEMAP command does all the pixel work on the device),
with 17,140 on the boot frame (map construction).

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

## Platformer decisions (pinned at M22)

The integration milestone: one game that exercises every capability built so
far, and the last code milestone before the architecture review (M23). It is
`programs/platformer.c`, a side-scrolling platformer run as guest firmware.

**How each required feature is exercised.**

| Feature | How the platformer uses it |
|---------|----------------------------|
| tilemaps | the terrain is a single 40×16 `gfx_tilemap` (M20); the cell array is the **single source of truth** for both rendering *and* collision (a cell's low-16 index drives solidity), exactly the Tetris `field` pattern |
| sprites  | the player (animated) and the coins are `gfx_sprite` (M19) |
| scrolling | the camera (M21) follows the player, clamped to the 640×256 world |
| collision | axis-separated AABB vs the solid terrain cells (walk into walls, land on ground/platforms) |
| animation | a 3-frame player (2 walk + 1 airborne) chosen by a timer + airborne flag |
| layers | sky (`gfx_clear`) → parallax background (screen-space rects at half/quarter camera rate) → terrain tilemap (world-space, camera) → sprites → HUD |
| input | left/right walk, A to jump (INPUT_PAD) |
| audio | jump (660 Hz), coin (880 Hz), goal (440 Hz) beeps (AUDIO_FREQ/CTRL), each held for a few timer ticks |

**Memory budget (the recurring constraint).** World = 40×16 tiles of 16 px =
640×256 px (camera pans 320 px horizontally, 16 px vertically). The terrain is
one `cells[16][40]` grid (640 words = **2,560 B of RAM**) built once at boot;
tile art + level logic live in ROM. The whole guest is **2,404 B of ROM** and
leaves ~1.4 KB of stack — comfortably inside the 4 KiB + 4 KiB budget, and the
third data point (after Tetris and scroll_demo) that the 32-bit-per-cell
tilemap format is the dominant memory cost of any non-trivial game. That
observation is M23's answer-seed for "what should the final memory map /
tile format look like?".

**Design choices worth pinning.** The ground is flat across the whole level so
the main path is walkable end-to-end (a scripted "hold right" reaches the
goal), while the *vertical* interest lives in floating platforms you jump onto
— this keeps the physics testable without fragile frame-perfect input. The
player spawns ~24 px above the ground so the boot frames fall and land under
gravity. Coins on the ground path are collected by walking; coins above
platforms need a jump. Multiple layers are composited with **parallax**: the
background scrolls at half (hills) and quarter (clouds) the camera rate,
drawn as screen-space primitives (which the camera never moves) positioned
manually from `cam_x`, while the terrain and sprites use the world-space
camera — a direct demonstration of the M21 "primitives are screen-space,
sprites/maps are world-space" split.

**Testing**: `make test-platformer` boots the game as firmware and drives a
scripted RetroPad through the whole story, reading the game globals (`px`,
`py`, `onground`, `score`, `state`, `cam_x`, `cam_y`) out of guest RAM: spawn
falls and lands on the exact surface, layers all render (sky/hills/terrain/
player/coin each found in the framebuffer), walking advances `px` by 2/frame
and collects a ground coin, jumping leaves the ground (`vy<0`) and lands back
on the surface, reaching the goal sets `state` to WIN with the goal beep, the
camera follows and clamps at both world edges, and A restarts. Plus audio
square-wave checks, one present/timer tick per frame, `stray_accesses == 0`,
and reset-and-compare determinism (identical 295-frame hash sequence +
byte-identical final framebuffer). Guest CPU load: **841 inst/frame average,
6,324 max** (295 frames) — a *lighter* per-frame load than Breakout (2,039
avg) despite doing far more, because the tilemap/sprite/camera work is
offloaded to the device; the guest only runs the physics loop and issues a
handful of MMIO commands per frame, so a full layered game sits comfortably
inside a 60 fps frame budget.

The roadmap's M22 instruction — "After this milestone, stop and review the
architecture... Only then define ARCADE-1 v0.1" — is discharged in the next two
milestones: the **architecture review** (M23) lives in `ARCHITECTURE.md`, and the
**v0.1 machine specification** it produces (M24) lives in `SPEC.md`. A separate,
non-milestone research note — a voxel-sandbox **architectural stress test**
(`VOXEL_EXPERIMENT.md`) — asks how far this machine could go toward a
Minecraft-like game without becoming a 3D GPU; it changes no roadmap and no
machine behaviour.

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

### Determinism

Determinism is a test, not an assumption: establish a state, reset, replay the
same inputs, and assert the identical result (frame, waveform, or
register-write stream). The beep, machine, and pattern harnesses already do
this reset-and-compare check — keep doing it for every new test.

### Stray accesses

The bus counts every access it ignores or answers with a default (sub-word
MMIO, out-of-range, ROM write, MMIO with no device) in `stray_accesses`.
Tests should assert `stray_accesses == 0` unless the test intentionally causes
strays.

### Games

Golden-frame or behavior tests where practical.

Each game must be headless-runnable (no frontend) and print a hash per frame,
so runs can be diffed byte-for-byte across hosts and after a reset. A game's
CPU load is instructions executed per frame; read the counter before and after
each frame. Record each game's CPU load as a number once it runs, so
regressions are visible.

### Golden regression policy

The pinned per-frame hashes are not just printed — they are stored and
compared. `tests/roms/` holds the committed game images (`.arc1`) plus their
`nm` symbol dumps (`.syms`); `tests/golden/<game>.txt` holds every
per-frame `hash=`/`fb=` line from a run against those images.
`make test-golden` re-runs the four game tests against `tests/roms/` and
diffs, printing the first differing frame; it needs no RISC-V compiler.
`make check` runs every test target plus `test-golden`.

A golden change **is** a behaviour change: refreshing `tests/roms/` or
`tests/golden/` (via the manual `make roms-update` / `make golden-update`)
must happen in the same commit as the change that caused it, and the commit
message must say why the behaviour changed and which spec section it moves.

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