# ARCADE-1 — Machine Specification, v0.1 Candidate (Milestone 24)

This is the first complete machine specification, written from the M23
architecture review (`ARCHITECTURE.md`) and pinned against the reference
implementation (`src/arcade1/`). Where the review *recommended* a change (e.g.
grow RAM), this spec records the **v0.1 value**; where behaviour already exists
and is tested, this spec records the **exact current behaviour**, which the C VM
implements today.

The C VM (`src/arcade1/`) is the **reference implementation**. Conformance
(M34) means matching this document; the FPGA (M33) implements the same contract.

**Scope of v0.1.** A small deterministic machine for 2D games: an RV32IM core, a
flat memory map, one MMIO window muxed across four devices (timer, video, input,
audio), a `.arc1` program image, and a present-yield frame protocol. Nothing
else. Features explicitly excluded by the review (interrupts, entropy, FP,
blend/rotate/scale, double buffering) are **out of scope for v0.1** and are not
described here.

---

## 1. CPU

| Property | v0.1 value |
|----------|-----------|
| ISA | **RV32IM** — RV32I base + the M extension (mul/mulh/mulhsu/mulhu, div/divu, rem/remu) |
| Endianness | little-endian |
| Registers | 32 × 32-bit GPRs (`x0`..`x31`) + a program counter |
| `x0` | hardwired to zero: reads yield 0, writes are discarded |
| `pc` | reset to the **ROM base (0x0000)** on reset — the entry point |
| Execution model | plain single-instruction interpreter; **no** caches, pipelines, speculation, or interrupts |
| `instret` | counts retired steps (including NOP-behaved steps); diagnostic only, cleared on reset |
| Faults | **none** — the machine never traps (see §3 for how bad accesses behave) |

**M-extension semantics** are the spec-exact RISC-V results, with no traps:
division by zero and `INT_MIN / -1` return their defined results (no exception).
All eight M instructions are interpreted with no special fast path (a divide
costs the same per-instruction as an add in the reference).

**System instructions.** `FENCE`, `ECALL`, `EBREAK` execute as **NOP** (PC
advances by 4). Unknown/unsupported encodings also behave as NOP. There is no
OS, syscall, or halt handling; a program "ends" by parking in a self-loop.

## 2. Memory map

Flat, byte-addressed, little-endian. Three regions:

```
ROM    0x00000000..0x0003FFFF   256 KiB   program image (read-only at runtime)
gap    0x00040000..0x1FFFFFFF             unused (out of range)
RAM    0x20000000..0x2000FFFF    64 KiB   .data/.bss + stack (grows down from
                                          0x20010000)
gap    0x20010000..0x3FFFFFFF             unused (out of range)
MMIO   0x40000000..0x40000FFF      4 KiB   device registers (routed to the
                                          handler)
```

**v0.2 (M26) adopts the growth-friendly map above** — ROM 256 KiB, RAM 64 KiB,
each region on a 512 MiB boundary so sizes can grow later without moving
anything. The v0.1 placeholder (ROM 4 KiB at 0x0000, RAM 4 KiB at 0x1000,
MMIO 4 KiB at 0x4000) is **superseded**; the M23 review had already
recommended growing RAM (≥16 KiB) because the 32-bit tilemap cell format is
the dominant RAM cost of any non-trivial game. The map above is normative
for v0.2.

**Access rules** (the single clear interface the CPU goes through for every
access):

- **Reads** (`lb`/`lh`/`lw` and unsigned forms): out-of-range addresses and
  accesses that straddle a region boundary return **0**. ROM reads return the
  loaded image byte.
- **Writes** (`sb`/`sh`/`sw`): accepted **only when the whole access lands in
  RAM**. ROM writes and out-of-range writes are **ignored**.
- **MMIO**: only **32-bit** accesses (`lw`/`sw`) dispatch to the device handler.
  Sub-word (8/16-bit) MMIO accesses are **no-ops** — reads return 0, writes are
  ignored. Device registers are always accessed as 32-bit.
- **No access ever faults.** The bus is deterministic and side-effect-free
  beyond the cases above.

The gaps (0x00040000..0x1FFFFFFF and 0x20010000..0x3FFFFFFF) exist to keep
MMIO a distinct region rather than a continuation of RAM; they carry no
semantics.

## 3. MMIO window and device map

One 4 KiB window at **0x40000000**, serviced by a **single muxed handler** that
dispatches on the byte offset within the window:

| Offset range | Device |
|--------------|--------|
| `0x000..0x0FF` | Timer |
| `0x100..0x1FF` | Video |
| `0x200..0x2FF` | Input |
| `0x300..0x3FF` | Audio |
| anything else  | reads 0, writes ignored |

The canonical, register-by-register table lives in **`docs.md` §15** and is
normative; §4–§7 below summarize each device's contract for v0.1.

## 4. Timer

Exposes frame time to the guest. Deterministic — one tick per completed frame,
no wall-clock anywhere.

| Offset | Register | Access | Behaviour |
|--------|----------|--------|-----------|
| `0x00` | `TIMER_COUNT` | R | tick count, +1 per completed frame while enabled |
| `0x04` | `TIMER_CTRL`  | R/W | bit0 = enable; other bits ignored on write, read as 0 |

- `TIMER_COUNT` is **read-only** (writes ignored). Advances exactly once per
  completed `arcade1_run_frame()` while CTRL bit0 is set; a `lw` observes the
  value at the instruction that performs it.
- `TIMER_CTRL` latches at the `sw`; the new state governs every frame that
  completes after the write.
- **Power-on:** count = 0, enabled.

## 5. Video

A single RGB565 framebuffer the device draws **directly into**; the guest drives
it through parameter registers + one command register. Commands execute
**synchronously at the `sw`** that triggers them.

Registers (offsets from window base):

| Offset | Register | Access | Behaviour |
|--------|----------|--------|-----------|
| `0x100` | `X` | R/W | draw X; **signed** for FILL/SPRITE/TILEMAP-origin; clipped to screen |
| `0x104` | `Y` | R/W | draw Y; same signed/clipping rules |
| `0x108` | `COLOR` | R/W | RGB565, masked to 16 bits |
| `0x10C` | `CMD` | W | command (below); other values ignored |
| `0x110` | `STATUS` | R | count of PRESENT commands executed |
| `0x114` | `ADDR` | R/W | 1-bpp image address (SPRITE source) |
| `0x118` | `W` | R/W | width in pixels (FILL/SPRITE/TILEMAP tile width) |
| `0x11C` | `H` | R/W | height in pixels (FILL/SPRITE/TILEMAP tile height) |
| `0x120` | `MAP_ADDR` | R/W | TILEMAP cell-array address (ROM or RAM) |
| `0x124` | `TILE_ADDR` | R/W | TILEMAP tile-set image address |
| `0x128` | `COLS` | R/W | TILEMAP grid width in cells (0 = draws nothing) |
| `0x12C` | `ROWS` | R/W | TILEMAP grid height in cells (0 = draws nothing) |
| `0x130` | `CAM_X` | R/W | camera X, **signed** pixels; power-on 0 |
| `0x134` | `CAM_Y` | R/W | camera Y, **signed** pixels; power-on 0 |
| `0x138` | `PAL_INDEX` | R/W | palette entry index 0..255 (see palette) |
| `0x13C` | `PAL_DATA` | R/W | RGB565 entry at `PAL_INDEX`; a **write** stores it and auto-increments `PAL_INDEX` (wrapping 255→0); a **read** returns the entry without incrementing |
| `0x140` | `PAL_BANK` | R/W | palette bank 0..15 (masked to 4 bits) used by `SPRITE4`; power-on 0 |
| `0x144` | `FLAGS` | R/W | bit0 = flip X, bit1 = flip Y (masked to 2 bits), used by `SPRITE4`; power-on 0 |

**Commands** (`CMD`):

| Value | Command | Behaviour |
|-------|---------|-----------|
| 1 | `CLEAR` | fill the whole screen with `COLOR` |
| 2 | `SET_PIXEL` | plot `COLOR` at (`X`,`Y`), clipped |
| 3 | `PRESENT` | acknowledge the frame; increments `STATUS` |
| 4 | `FILL` | fill the `W`×`H` rectangle at (`X`,`Y`) with `COLOR`, clipped |
| 5 | `SPRITE` | blit the 1-bpp image at `ADDR`, size `W`×`H`, at (`X`,`Y`) |
| 6 | `TILEMAP` | draw the `COLS`×`ROWS` grid of 32-bit cells at `MAP_ADDR`, using the tile set at `TILE_ADDR` |
| 7 | `SPRITE4` | blit the **4-bit** image at `ADDR`, size `W`×`H`, at (`X`,`Y`), coloured from a palette bank with optional flips |
| 8 | `TILEMAP4` | draw the `COLS`×`ROWS` grid of **4-bit** tiles at `MAP_ADDR`/`TILE_ADDR`, each cell supplying its own palette bank and flips |

**World-space vs screen-space (camera).** `SPRITE` and `TILEMAP` are
**world-space**: they draw at (`X − CAM_X`, `Y − CAM_Y`). `CLEAR`,
`SET_PIXEL` and `FILL` are **screen-space** and are never scrolled (they are the
HUD). `CAM_X`/`CAM_Y` power on at (0,0) = no offset.

**1-bpp image format** (shared by SPRITE and each TILEMAP tile): row-major,
rows padded to whole bytes, **MSB = leftmost**. Set bits are drawn in the
command's colour; clear bits are **transparent** (the framebuffer is untouched).
The image source must lie in ROM or RAM; anything else draws nothing.

**TILEMAP cell format.** Each cell is one 32-bit little-endian word: bits
`[15:0]` = tile index (**0 = empty**, skipped so background shows through),
bits `[31:16]` = an RGB565 tint applied to that tile's set bits. Tile *N* (≥ 1)
lives at `tile_addr + (N−1) × tile_bytes`, `tile_bytes = H × ceil(W/8)`. Cell
(col, row) lands at (`X + col×W`, `Y + row×H`) after the camera; each tile clips
to the screen.

**Windowing.** `TILEMAP` visits **only the columns/rows even partially on
screen**, computed from the camera; a cell fully outside the viewport is skipped
before any fetch. Skipping it cannot change a pixel (the per-tile clip would
draw nothing), so the framebuffer is identical to visiting every cell, but a map
larger than the screen costs only its visible cells in bandwidth.

### Palette (M27)

The device holds a **256-entry RGB565 palette** used only by the 4-bit commands
(`SPRITE4`, `TILEMAP4`); the 1-bit commands ignore it. Entries are addressed
through `PAL_INDEX`/`PAL_DATA`: a guest writes the start index once, then streams
values — each `PAL_DATA` write stores `value & 0xFFFF` at the current index and
auto-increments it (wrapping 255→0), so a whole block uploads with one index
write plus N data writes. A `PAL_DATA` **read** returns the entry at the current
index and does **not** increment.

The palette is organised as **16 banks of 16 colours**: a 4-bit pixel value `v`
(1..15) selects `PALETTE[bank×16 + v]`. Pixel value **0 is transparent**.

**Power-on palette:** entry 0 = black; entries 1..15 = a fixed 16-colour default
(an EGA-like set, exact RGB565 values pinned in docs.md "Color decisions,
pinned at M27"); entries 16..255 = black. Any reset restores this exactly.

### 4-bit image format (M27)

Shared by `SPRITE4` and each `TILEMAP4` tile: **two pixels per byte**, the
**leftmost pixel in the HIGH nibble**, row-major, and the **width must be even**
(so a row is exactly `W/2` bytes, no padding). Pixel value 0 is transparent;
value `v` (1..15) draws `PALETTE[bank×16 + v]`. The image source must lie in ROM
or RAM; anything else (in particular the MMIO window) reads as 0 pixels, so it
draws nothing and has no side effects.

### SPRITE4 (M27)

Draws the 4-bit image at `ADDR`, size `W`×`H`, at (`X`,`Y`). It is **world-space**
like `SPRITE` (drawn at `X − CAM_X`, `Y − CAM_Y`), clipped to the screen. The
palette **bank** comes from `PAL_BANK` and the **flips** from `FLAGS`
(bit0 = mirror horizontally, bit1 = mirror vertically); a flipped sprite that
hangs off an edge still shows the correct image pixels.

### TILEMAP4 (M27)

Like `TILEMAP` (same windowing, camera and per-tile clipping) but each tile is a
4-bit image of size `W`×`H`; tile *N* (≥ 1) lives at `tile_addr + (N−1) ×
(W/2 × H)` bytes. Each 32-bit little-endian cell supplies its **own** palette
bank and flips, and `PAL_BANK`/`FLAGS` are **ignored**:

| Bits | Meaning |
|------|---------|
| `[15:0]` | tile index (**0 = empty**, skipped) |
| `[19:16]` | palette bank (0..15) |
| `[20]` | flip X |
| `[21]` | flip Y |
| `[31:22]` | reserved, ignored |

### Invalid sizes / ignore rules (M27)

For the 4-bit commands, a zero `W` or `H` draws nothing, and an **odd `W`** draws
nothing (the format is undefined for it). A source address outside ROM/RAM draws
nothing and performs no bus access (no stray count, no device side effects). All
registers are still written/read normally regardless of what is drawn.

**Power-on:** all parameters zeroed (`PAL_INDEX`, `PAL_BANK`, `FLAGS` = 0),
`STATUS` = 0, nothing presented, palette at its default.

**Display conformance.** The presented frame is the **framebuffer contents at
the end of the frame** (§8 "Frame boundaries"). `PRESENT` does exactly two
things: it increments `STATUS`, and it ends the current frame. How an
implementation buffers, double-buffers, or scans out that framebuffer is **not
specified**, as long as this is what the viewer sees. No text here should be
read as mandating a particular buffering scheme.

## 6. Audio

A single square-wave tone channel, register-driven; the machine renders the
waveform from the device state each frame.

| Offset | Register | Access | Behaviour |
|--------|----------|--------|-----------|
| `0x300` | `AUDIO_FREQ` | R/W | tone frequency in Hz (0 = constant level while enabled) |
| `0x304` | `AUDIO_CTRL` | R/W | bit0 = tone enable; other bits ignored, read as 0 |

- Waveform: **square wave at fixed amplitude ±8000**. No PCM, no mixing, no
  channels in v0.1.
- The machine renders audio **after** the guest has run each frame, so a
  register write at frame *N* already shapes frame *N*'s samples.
- **Power-on:** `AUDIO_FREQ` = **440 Hz**, `AUDIO_CTRL` = enabled (the
  historical demo beep, shared by the content-less core).

## 7. Input

A read-only 8-button pad register reflecting the host-supplied controller state
for the current frame. The machine never touches host input devices directly.

| Offset | Register | Access | Bits |
|--------|----------|--------|------|
| `0x200` | `INPUT_PAD` | R | bit0 up, bit1 down, bit2 left, bit3 right, bit4 A, bit5 B, bit6 start, bit7 select; other bits read as 0 |

- Read-only; writes are ignored.
- The value reflects the `arcade1_input_t` the host set for this frame via
  `arcade1_set_input()` (called before the frame starts).

## 8. Timing, frame protocol and determinism

- **Display:** 480×270 (16:9; scales exactly 4× to 1080p and 8× to 4K), **60
  fps**. (Pinned at M25 — see `ARCADE1_WIDTH`/`ARCADE1_HEIGHT`, from which
  every other machine size derives. The three shipped games and the demos keep
  their current guest code and still draw in the top-left 320×240 region; the
  rest of the screen shows whatever their CLEAR colour leaves. Re-laying the
  guests out is not part of M25.)
- **Audio:** **48,000 Hz** stereo; **800 stereo frames** (1,600 samples) per
  60 fps video frame.
- **Frame protocol (present-yield).** A guest `main()` is an **infinite loop**
  whose body ends in a `PRESENT`. `arcade1_run_frame()` resumes the CPU from its
  last `pc` and runs guest instructions until the next `PRESENT` (or the cap,
  below). One guest iteration = one host frame.
- **Instruction cap.** `arcade1_run_frame()` steps at most **1,000,000**
  instructions per frame; this is a runaway-guest safety cap, not part of the
  game contract. A guest that parks in a self-loop is detected and the frame
  ends.
  **DEFERRED: owner postponed until the FPGA milestone; current reference
  behaviour stays.** §8 calls this cap "not part of the game contract", but
  §10 rule 6 lists it *in* the v0.1 contract. Both readings are written out
  here; do not pick silently. (The reference implementation currently truncates
  at exactly 1,000,000 steps and the frame-rule tests pin that behaviour; the
  A-vs-B choice is NOT made here.)
  - **Option A — truncation at exactly 1,000,000 steps is contract.** Hardware
    (M33) and every other implementation must replicate the truncation point,
    which constrains any future optimisation (no reordering/fusing past the
    cap) and makes a diagnostic a conformance requirement.
  - **Option B — reaching the cap is a program error (RECOMMENDED).** A
    conforming guest always ends its frame with `PRESENT` (or parks); hitting
    the cap means the guest is broken. Behaviour afterwards is then
    implementation-defined and conformance tests never reach it, so hardware
    is free. No shipped test depends on the truncation point (the worst
    measured shipped frame is ~14.3k instructions, ~1.4% of the cap).
  See also §10 rule 6. (A earlier note recommended B; the owner has since
  deferred the choice, so treat neither option as decided.)
- **Timer.** Exactly **1 tick per completed frame** (§4).
- **Determinism (a hard requirement).** The machine is a pure function of
  (ROM, reset state, per-frame input sequence). There are **no** interrupts, no
  wall-clock, no entropy, and no floating point. Reset-and-compare is a
  first-class test: the same ROM + the same input script must produce an
  identical per-frame hash sequence and an identical final framebuffer across
  runs and hosts. Any future change that breaks this is architecturally wrong.

### Frame boundaries

`arcade1_run_frame()` steps the guest one instruction at a time and ends the
frame at the **first** of these three conditions:

1. **`PRESENT` executed.** The `sw` that writes `CMD = PRESENT` completes — the
   `STATUS` increment is visible immediately after that instruction — the frame
   ends, and **no instruction after it runs this frame**.
2. **`pc` unchanged.** A step leaves `pc` identical to what it was before that
   step (a self-loop such as `j .`). The instruction that caused it *did* run.
   The condition is re-tested from a fresh baseline every frame, so a guest
   that parks this way ends **every** later frame on its first step too.
3. **Step cap.** 1,000,000 steps have executed in this frame ("Instruction
   cap", above).

**Where the next frame starts.** `arcade1_run_frame()` never resets the PC; it
resumes from wherever the CPU stopped. Precisely: after a `PRESENT`-ended frame
the store has already advanced `pc`, so the next frame starts at the
instruction **after** the `PRESENT`; after a `pc`-unchanged end it starts at
that same parking instruction (hence the immediate re-end); after the step cap
it starts at the instruction after the last one executed.

**What each case leaves behind:**

| End of frame      | `STATUS` (PRESENT count) | Timer tick | Host output                          |
|-------------------|--------------------------|------------|--------------------------------------|
| `PRESENT`         | +1                       | +1         | framebuffer at end of frame = at `PRESENT` (nothing ran after it) |
| `pc` unchanged    | unchanged                | +1         | framebuffer as last drawn            |
| Step cap          | unchanged                | +1         | framebuffer as last drawn            |

The timer ticks **exactly once per `arcade1_run_frame()` call** whatever ended
the frame (§4), so it counts frames, not presents. `STATUS` counts `PRESENT`s
only. The host sees the framebuffer contents **when `arcade1_run_frame()`
returns**.

`instret` and the step count are **diagnostic, not part of conformance**: a
conforming implementation must reproduce the frame's visible output and
observable register behaviour, not the exact instruction total that produced
it.

## 9. Program model and image format (`.arc1`)

A program is a **flat RV32IM image** loaded into ROM and entered at the reset
vector (0x0000). It runs bare-metal: no libc, no OS, no SDK at runtime. `.data`
is stored in the image and copied to RAM at startup, `.bss` is zeroed, and the
stack grows down from the top of RAM (0x20010000). `crt0` provides `_start`, which
calls `main()`.

The **`.arc1` v1** container wraps the image (defined in `src/arcade1/rom.h`,
pinned in `docs.md` §21):

```
offset  size  field
0       4     magic   "ARC1"
4       1     version (1)
5       1     reserved, must be 0
6       2     reserved, must be 0
8       4     payload_size   (little-endian u32, 1..4096)
12      4     payload_crc32  (little-endian u32, IEEE CRC-32)
16      N     payload        (the flat RV32IM ROM image)
```

- The **CRC-32 covers the payload only**; header corruption is caught by the
  magic/version/size checks. CRC-32 is IEEE (poly `0xEDB88320`, reflected);
  known vector `crc32("123456789") == 0xCBF43926`.
- Validation rejects: short buffer, bad magic, unknown version, declared size 0
  or > 4096 or disagreeing with the actual file length, and CRC mismatch.
- **Deliberately excluded:** filesystem, metadata, asset sections, compression,
  signatures. The reserved bytes and version field are the only extension room;
  an incompatible layout bumps the version.
- The core also **accepts raw flat `.bin`** content (payload only) for backward
  compatibility; new games ship as `.arc1`.

## 10. Compatibility rules (v0.1 contract)

These are the rules that make v0.1 a *contract* rather than an implementation
detail; conformance (M34) is measured against them:

1. **The C VM is the reference.** Any other implementation (FPGA, another
   emulator) conforms iff it produces, for every ROM + input script, the same
   per-frame framebuffer, the same audio samples, and the same observable
   device-register behaviour as `src/arcade1/`.
2. **Determinism is mandatory.** Same ROM + same input ⇒ identical output,
   byte for byte, every run and every host. No interrupt, entropy, wall-clock,
   or FP may be introduced.
3. **The memory map is fixed** (§2): ROM at 0x00000000, RAM at 0x20000000, MMIO
   at 0x40000000, at the sizes given.
4. **The register map is fixed** (§3–§7, `docs.md` §15). Offsets, access
   rights, power-on values, and synchronous-at-the-`sw` command semantics are
   part of the contract.
5. **No faulting.** Bad reads return 0, bad writes are ignored; the machine
   never traps.
6. **The frame protocol is present-yield** (§8). Frame boundaries follow the
   three rules in §8 "Frame boundaries"; the 1,000,000-instruction cap is a
   safety net, not a contract requirement — **DEFERRED: owner postponed until
   the FPGA milestone; current reference behaviour stays.** §8 used to call it
   "not part of the game contract" while earlier versions of this rule listed
   it as one; §8 sets out both options. The reference implementation truncates
   at exactly 1,000,000 steps (pinned by the frame-rule tests). Once the owner
   picks, this rule and §8 must be updated together.
7. **`.arc1` v1 is the image format** (§9); raw `.bin` remains accepted.
8. **Excluded features stay excluded** for v0.1: interrupts, entropy/RNG,
   floating point, alpha blend, rotation, scaling, affine transforms,
   hardware priority layers, double buffering, filesystem/dynamic allocation/
   threading. Adding any of these requires an architecture revision (M27) and
   re-testing against the existing games.

Any change to rules 2–6 is a **breaking change** to the v0.1 contract and
requires a version bump and a documented migration; the three shipped games
(Breakout, Tetris, Platformer) plus the Scroll demo, run against the committed
golden images in `tests/roms/` and diffed frame-by-frame against
`tests/golden/` (`make test-golden`), are the regression suite that must
continue to pass. A change to those golden files is a behaviour change and
must be justified in the commit message with the spec section it moves
("Golden regression policy", docs.md §30).
