# ARCADE-1 — Architecture Review (Milestone 23)

This is the mandatory stop-and-review the roadmap calls for after the three
shipped games (Breakout, Tetris, Platformer). It answers the nine questions in
`docs.md` §26 using **measured numbers from the reference implementation**, not
intuition. Every figure below comes from the running machine and its test
harnesses; where something is a recommendation rather than a measurement it is
marked as such.

The C VM remains the reference implementation throughout. Nothing here is
implemented — M23 only *reviews*; M24 turns the conclusions into the v0.1
specification, and M27 revisits the architecture again only where SDK/game
evidence justifies it.

---

## Data the review is grounded in

Measured on the reference VM (host `make test-*` harnesses), as of M22:

**Per-game CPU load** (guest instructions executed per 60 fps frame):

| Game       | avg inst/frame | max inst/frame | inst/sec (avg, ×60) |
|------------|---------------:|---------------:|--------------------:|
| Breakout   |          2,039 |          2,185 |             122,340 |
| Platformer |            841 |          6,324 |              50,460 |
| Tetris     |          8,079 |         14,309 |             484,740 |
| scroll_demo|            191 |         17,140 |              11,460 |

The worst observed frame across all games is Tetris's line-clear at 14,309
instructions. Even that is under 1 MIPS — a very small soft core (or a slow
host) clears it with enormous headroom. **The CPU is not the bottleneck.**

**The historical cost of NOT offloading** (same games, before the device-side
commands existed):

| Game     | per-pixel `sw` era | with device command | ratio |
|----------|-------------------:|--------------------:|------:|
| Breakout |             77,836 |               2,039 |   38× |
| Tetris   |            245,797 |               8,079 |   30× |

This is the single strongest piece of evidence for the whole architecture:
moving FILL / SPRITE / TILEMAP from a guest per-pixel loop into the device cut
CPU load by ~30–40×. The device-offload model is correct and must be preserved.

**Memory footprint** (largest consumers, within the 4 KiB ROM + 4 KiB RAM
placeholder map):

| Game       | ROM (payload B) | RAM (.bss B) | biggest RAM item |
|------------|----------------:|-------------:|------------------|
| Tetris     |           3,176 |          864 | `field[200]` = 800 B |
| Platformer |           2,404 |        2,680 | `cells[40×16]` = 2,560 B |
| Breakout   |           1,842 |           56 | — |
| scroll_demo|             844 |        2,816 | `cells[32×22]` = 2,816 B |

**The 32-bit-per-cell tilemap format is the dominant RAM cost of every
non-trivial game** — a fact that drives questions 3 and 5 below.

**Instruction mix:** every game emits M-extension instructions, but sparingly
(Breakout 3, Platformer 8, scroll_demo 6, Tetris 18 `mul`/`div`/`rem`). RV32IM
is justified — it removes the need for libgcc soft-mul/div routines — but M is
lightly used, a point worth remembering when weighing FPGA silicon (Q8).

---

## 1. What does ARCADE-1 actually need?

Stripped to what the three games and four demos actually exercised, the machine
is:

- **A deterministic RV32IM core** with a flat little-endian address space and no
  OS, no libc, no interrupts, no entropy. Reset-and-compare is a *test*, so any
  source of non-determinism (interrupts, timers that drift, RNG, wall-clock) is
  architecturally excluded, not merely unused.
- **A memory map** with ROM (program image), RAM (data + stack), and a single
  MMIO window. Four KiB each is *adequate* for the shipped games but cramped
  (Q5).
- **A synchronous command device** for video — the thing that made the 30–40×
  CPU win. Its command set is small: CLEAR, SET_PIXEL, FILL, SPRITE, TILEMAP,
  PRESENT, plus a camera (CAM_X/CAM_Y) that offsets the world-space commands.
- **A square-wave audio tone** (FREQ/CTRL) — one register pair, event-driven.
- **An 8-button input pad** (D-pad, A, B, start, select).
- **A frame timer** (COUNT/CTRL), one tick per frame.
- **A frame protocol**: guest `main()` loops `update/draw` ending in PRESENT;
  the machine runs one guest iteration per host frame and yields.

That is the whole machine. Everything the games needed beyond this — sprites,
tilemaps, per-cell colour, windowing, camera — is *graphics*, and it all lives
behind one synchronous command interface. There is no filesystem, no dynamic
allocation, no threading, no networking, no 3D.

## 2. Which features are unnecessary?

Features that exist in the reference VM but that **no game or demo uses**, and
which should therefore not survive into a leaner FPGA build or a v0.1 freeze
without a justification:

- **Sub-word (8/16-bit) MMIO.** The bus supports `lb`/`lh` everywhere, but every
  device register is 32-bit `lw`/`sw`. Sub-word MMIO is dead weight on the
  device interface and can be dropped from the MMIO path (RAM/ROM keep it — the
  CPU needs byte access for C).
- **`SET_PIXEL`** as a *game-facing* primitive. The demos use it; no game does.
  The device should keep a per-pixel path for its own use, but it is not a
  headline feature and does not need to be fast.
- **`stray_accesses`** is a host-side diagnostic only; it is not part of the
  machine contract and should not appear in a spec-frozen FPGA build.
- **The M02–M06 host-driven demo logic** (test pattern, moving square). Kept only
  for the content-less core's smoke tests; it does not run when firmware is
  attached and is not part of the real machine.
- **The `0x2000..0x3FFF` gap** in the placeholder map is deliberate slack, not a
  feature; the final map should not inherit it blindly (Q5).

Equally important is what is **absent and should stay absent**: alpha blending,
rotation, scaling, affine transforms, priority layers, floating point,
interrupts, and an entropy source. None were needed by Breakout, Tetris or the
Platformer; each would add determinism risk and FPGA cost for no demonstrated
game. The roadmap already forbids them until a later milestone asks (M30/M31).

## 3. What are the real graphics bandwidth requirements?

The milestone's key question. Two independent bandwidths matter, and they are
very different sizes.

**(a) Framebuffer write bandwidth (the scan-out + redraw cost).**
Screen is 320×240 = 76,800 pixels, RGB565 = 2 bytes. A worst-case frame that
touches every pixel is therefore **76,800 pixel-writes/frame ≈ 4.6 Mpixel/s**,
or **~9.2 MB/s** of framebuffer writes at 60 fps. Scan-out *reads* the same
framebuffer once per frame (another ~9.2 MB/s). This is the floor cost of
*any* framebuffer design and is trivially met by a single- or dual-ported
block-RAM framebuffer on essentially any FPGA. It does **not** grow with map
size, because of windowing (below).

**(b) Command/fetch bandwidth (the "real" question).**
This is what a naive design would get wrong. With the M21 windowing, a TILEMAP
visits **only the columns/rows even partially on screen** — a map far larger
than the screen costs the *same* as a screen-sized one:

- A full screen of 16×16 tiles = **20×15 = 300 visible cells**.
- Cell data: 300 × 4 B = **1,200 B/frame**.
- Tile bitmaps: 300 × 32 B (16×16 at 1 bpp) = **9,600 B/frame**.
- Worst case ≈ **10.8 KB of fetch per frame ≈ 650 KB/s** — and that is if *every*
  cell is a distinct non-empty tile, which no shipped game does (empty cells
  fetch nothing; a tileset is shared across the whole map, so the tile-bitmap
  working set is small and cacheable).

So the true per-frame graphics fetch is **~1–11 KB**, not "map size". A 40×16
map (Platformer) and a 20×15 screen fetch the *same* 300 visible cells. The
camera and windowing make map size essentially free in bandwidth; only the
*visible* tile count is paid for.

**Sprites** are negligible by comparison: the Platformer's player (12×14) is 28
bytes of fetch and ≤168 pixel-writes per frame; a screen full of such sprites
would still be under 10 KB/frame.

**Conclusion:** the device must sustain a *blit* of at most one screen of
1-bpp tiles per frame (≤76,800 tinted pixel-writes, ≤11 KB fetch). That is a
modest, well-bounded requirement — a small state-machine blitter, not a GPU.
The CPU→device *command* traffic is tinier still: a handful of 32-bit register
writes per frame (the Platformer issues ~6 commands/frame).

## 4. What should the FPGA renderer look like?

The evidence points to a **synchronous command-processor blitter**, not a
pipelined GPU:

- **One shared 1-bpp blit pipeline.** SPRITE and each TILEMAP cell already share
  `video_blit_1bpp` in the reference; the FPGA should have a single blit engine
  that both commands drive. It fetches 1-bpp source bytes, tests the bit, and
  on a set bit writes the command's RGB565 tint to the framebuffer (clear bits
  are transparent — no write).
- **Synchronous, in-order, one command at a time.** Commands complete at the
  `sw` that triggers them. There is no queue, no out-of-order completion, no
  vsync-relative scheduling. The CPU issues a command and reads it back
  immediately; this is what makes the machine deterministic and testable. The
  FPGA renderer should therefore be a **FSM**: idle → decode CMD → run the
  blit/fill/clear loop → done. PRESENT simply increments a frame counter and
  signals scan-out.
- **Windowing in hardware.** The visible column/row window (Q3) is a couple of
  64-bit subtract/compare counters per axis computed once per TILEMAP; the FSM
  iterates only that window. This is what bounds the fetch cost.
- **A camera offset** applied to SPRITE/TILEMAP origins (world-space), with
  CLEAR/FILL/SET_PIXEL left screen-space. Two subtractors.
- **Scan-out decoupled from rendering.** The display reads the framebuffer on
  its own schedule; the command engine writes it. Because a frame's commands
  finish long before the next scan-out (the worst frame is one screen of
  blit), a single framebuffer with a simple arbitration is enough — no double
  buffering is *required* by the current games (PRESENT only counts; buffering
  is deliberately undecided).

Explicitly **not** in the renderer: shaders, a geometry stage, texture caching
hierarchy, blending units, rotation/scaling, or a command FIFO. The reference
VM's video device is already this shape (an FSM + one blit loop + a windowing
counter); the FPGA should mirror it so the C VM stays a faithful model.

## 5. What should the final memory map look like?

The placeholder map (ROM 4 K, RAM 4 K, gap, MMIO 4 K) *works* — every shipped
game fits — but the footprint data (above) shows the pressure point clearly:

- **The 32-bit-per-cell tilemap is the dominant RAM cost.** Platformer spends
  2,560 B and scroll_demo 2,816 B of their 4 KiB RAM on map cells alone, leaving
  ~1.3 KB of stack. A larger map, or a second layer, would not fit. This is the
  real constraint on game scope, not CPU speed and not bandwidth.

The review's recommendations for the final map (to be pinned in v0.1, M24):

- **Keep the 32-bit cell format** for v0.1. It is FPGA-friendly (a single
  aligned fetch), trivially supports per-cell colour, and is what every game
  already emits. Replacing it (index plane + attribute plane, run-length, etc.)
  is a *possible* future optimisation but is not justified by evidence yet —
  and M27 says change architecture only where evidence justifies it.
- **Give RAM room to grow**, because cells are the cost driver. The recommendation
  is to size RAM so a comfortable map (say 64×32 = 2,048 cells = 8 KB) plus
  stack fits — i.e. **RAM ≥ 16 KiB**, not 4 KiB. This is a recommendation, sized
  from the observed 2.5–2.8 KB for maps that are still smaller than a
  comfortable game level.
- **Keep ROM, RAM and MMIO as three distinct regions** with ROM at the reset
  vector (0x0000), and drop the inherited 0x2000 gap as a *meaningful* boundary
  (it was placeholder slack). MMIO should remain a single contiguous window with
  the existing device-offset split (timer <0x100, video <0x200, input <0x300,
  audio <0x400), which the register table in `docs.md` §15 already documents.

The exact sizes are a v0.1 decision (M24); the *finding* here is that **RAM, not
ROM and not bandwidth, is the binding constraint**, and it is bound by the
tilemap cell format.

## 6. What should the final game binary format look like?

`.arc1` v1 is already correct in spirit and should be **kept, not redesigned**:
a 16-byte header (magic `ARC1`, version, payload_size, payload CRC-32) wrapping
the flat RV32IM image. It is validated, shared host/guest (writer and reader
cannot disagree), and has explicit extension room (reserved bytes + version).

The review confirms:

- **Do not add metadata, asset sections, or compression now.** The asset
  pipeline is M29; inventing a container layout before real assets exist would
  be designing ahead of evidence. The reserved bytes and version field are the
  designated extension points.
- **Keep the CRC over the payload only**, with header corruption caught by the
  magic/version/size checks — this is already the behaviour and is sufficient.
- **Keep backward compatibility with raw `.bin`** for the demos; the format's
  job at v0.1 is to be a thin, verifiable envelope, nothing more.

## 7. What should remain software?

The reference VM, and everything host-side, stays software permanently — the FPGA
is an *implementation of the same machine*, never a replacement for the model:

- **The C VM** (`src/arcade1/`) — the reference, the thing conformance is
  measured against (M34).
- **The libretro adapter** (`src/libretro/`) — a thin, replaceable frontend.
- **The SDK, compiler toolchain, asset pipeline, and test harnesses** (M25+).
- **The games themselves** — RV32IM programs; they are data to the machine and
  run identically on VM and FPGA.

The whole *game logic* is software by construction: the machine provides no
game-specific hardware, only the synchronous devices above.

## 8. What should become hardware?

The FPGA build should implement the **machine contract**, and the priority order
falls straight out of the bandwidth and CPU data:

1. **The video command engine** (Q4) — the one place where hardware buys a
   measured 30–40× CPU win. This is the primary and first target: an FSM blitter
   + windowing + camera over a block-RAM framebuffer.
2. **The square-wave audio tone generator** — trivial (a phase accumulator +
   compare), and it removes per-sample CPU work.
3. **The frame timer and input-pad registers** — a counter and a register latch.
4. **The RV32IM core** — a small soft core. It is *not* the bottleneck (≤1 MIPS
   observed), so a modest core with ample headroom is fine. **M is lightly used**
   (3–18 instructions/game); whether M-extension multiply/divide earns its
   silicon in a resource-constrained build is an open question worth revisiting
   at M33 — dropping M and linking soft routines is a legitimate trade if BRAM/
   LUT budget forces it, at the cost of the ~30× CPU regression the device
   offload already bought being partly undone in software.

Nothing else needs to be hardware. There is no case from the shipped games for
hardware sprite-scaling, blending, layers, or a GPU.

## 9. What should be explicitly excluded?

To keep ARCADE-1 small, deterministic and buildable, the following are
**explicitly out of scope** for v0.1, with the reason each:

| Excluded | Why |
|----------|-----|
| Interrupts, async events | Break reset-and-compare determinism; no game needed them |
| Entropy / RNG device | Non-deterministic; Tetris uses a fixed-seed PRNG for this reason |
| Floating point | No game used it; adds cost and determinism surface |
| Alpha blend, rotate, scale, affine | Forbidden by the roadmap until M30/M31; no shipped game uses them |
| Priority/multiple hardware layers | Parallax was achieved guest-side with screen-space rects; no hardware layer needed |
| 3D, geometry, shaders | Out of scope by definition (2D only) |
| Filesystem, dynamic allocation, threading, OS | No game used them; would destroy determinism and simplicity |
| Sub-word MMIO | Dead weight; all registers are 32-bit |
| Double buffering | Not required by current games (PRESENT only counts); revisit only if tearing is demonstrated |

Anything not in this table and not in Q1's list is **not automatically in scope
either** — M27's rule stands: change the architecture only where SDK/game
evidence justifies it, and re-test against the existing games.

---

## Summary

ARCADE-1 is a **small deterministic RV32IM machine whose only real hardware
complexity is a synchronous 2D command blitter**. The CPU is never the
bottleneck (≤1 MIPS observed); the device-offload of FILL/SPRITE/TILEMAP is the
architecture's central win (30–40×) and must be preserved; true graphics
bandwidth is a modest, window-bounded ~1–11 KB of fetch per frame, not map size;
and the binding constraint on game scope is **RAM, driven by the 32-bit tilemap
cell format** — not CPU, not bandwidth, not ROM. The recommendations flowing
from this (keep the 32-bit cell format; grow RAM; keep `.arc1`; keep the
command-engine FPGA shape; exclude determinism-breaking features) are turned
into the v0.1 specification in **M24** (`SPEC.md`).
