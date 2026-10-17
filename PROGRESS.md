# ARCADE-1 — Progress Log

Living record of milestone status. The historical roadmap (M01–M24) is in
`MILESTONES_1.md` (formerly `docs.md`) and the forward roadmap (M25+) is in
`MILESTONES_2.md`; this file tracks what is actually done, how to verify it,
and known limitations. The M23 architecture review is a standalone document in
`ARCHITECTURE.md`, and the v0.1 machine specification it produced (M24) is in
`SPEC.md`. A post-M24 research experiment (not a milestone, no roadmap change) — using a
Minecraft-like voxel sandbox as an architectural stress test — is written up
in `VOXEL_EXPERIMENT.md`; its SDK findings feed M25. **All six experiment
phases are complete**: P0 (graphics micro-benchmarks), P1 (deterministic
terrain generator), P2 (raycast renderer), P3 (walk/dig/place
interaction), P4 (game systems: inventory/crafting/entities/day-night/
HUD) and P5 (persistence: snapshot save/load + input replay), with P6's
verdict — all §9 questions answered with data, ROM/RAM actuals, GO on
the raycast vs the (unmeasured) isometric fallback — written up in
`VOXEL_EXPERIMENT.md` §10. Measured costs live in §0.1–§0.6, runnable
via `make test-bench`, `make test-terrain`, `make test-ray`,
`make test-sandbox`, `make test-systems` and `make test-persist`.

## Status summary

| Milestone | Name                       | Status   |
|-----------|----------------------------|----------|
| 01        | Libretro skeleton          | COMPLETE |
| 02        | Test-pattern video         | COMPLETE |
| 03        | RetroPad input             | COMPLETE |
| 04        | Moving square              | COMPLETE |
| 05        | Audio beep                 | COMPLETE |
| 06        | ARCADE-1 machine abstraction | COMPLETE |
| 07        | Memory subsystem           | COMPLETE |
| 08        | RV32I decoder              | COMPLETE |
| 09        | RV32I execution            | COMPLETE |
| 10        | Load compiled C program    | COMPLETE |
| 11        | MMIO                       | COMPLETE |
| 12        | Timer                      | COMPLETE |
| 13        | ARCADE-1 video device      | COMPLETE |
| 14        | Simple rectangle API       | COMPLETE |
| 15        | Breakout (first game)      | COMPLETE |
| 16        | M extension (RV32IM)       | COMPLETE |
| 17        | Proper ROM format (.arc1)  | COMPLETE |
| 18        | Tetris                     | COMPLETE |
| 19        | Sprite engine              | COMPLETE |
| 20        | Tilemap engine             | COMPLETE |
| 21        | Scrolling (camera)         | COMPLETE |
| 22        | Platformer (integration)   | COMPLETE |
| 23        | Architecture Review        | COMPLETE |
| 24        | ARCADE-1 v0.1 Candidate    | COMPLETE |
| 25        | Machine v0.2a: 480×270 screen | COMPLETE |
| 26+       | (see `MILESTONES_2.md`)    | NOT STARTED |

Next up: **Milestone 26 — Machine v0.2b: Memory** (ROM 256 KiB, RAM 64 KiB, growth-friendly memory map). See `MILESTONES_2.md` for the exact scope.

## Architecture

```
src/
    arcade1/
        machine.h/.c   machine state + per-frame logic (frontend-independent);
                       from M15 owns the guest CPU and boots firmware per frame
        memory.h/.c    memory/bus: ROM + RAM + MMIO (device handler), 8/16/32-bit r/w
        cpu.h/.c       RV32IM decoder (stateless field/immediate/classification;
                       M extension MUL..REMU added at M16)
        exec.h/.c      RV32IM interpreter: registers + PC + single-step
                       (multiply/divide semantics added at M16)
        timer.h/.c     deterministic timer: 1 tick/frame, 2 MMIO regs (M12)
        video.h/.c     guest-driven CLEAR/SET_PIXEL/PRESENT (M13) plus the M19
                       FILL/SPRITE and M20 TILEMAP device-side commands (1-bpp
                       image/tile source restricted to guest ROM/RAM; TILEMAP
                       draws a COLSxROWS grid of 32-bit tile+colour cells);
                       M21 added a camera (CAM_X/CAM_Y) that SPRITE/TILEMAP
                       subtract, and TILEMAP windowing (only visible cells)
        audio.h/.c     guest-driven tone regs (FREQ/CTRL), rendered per frame (M15)
        rom.h/.c       .arc1 game image: parse/validate/write, CRC-32 (M17)
    libretro/
        libretro.c     thin adapter: RetroPad <-> arcade1_* <-> frontend;
                       loads content (an .arc1 image or legacy raw ROM) as
                       firmware when provided
programs/               guest code compiled by RISC-V GCC (M10+)
    crt0.S              startup: set sp, copy .data, zero .bss, call main, park
    link.ld             linker script: code->ROM, data/bss/stack->RAM
    gfx.h               guest graphics API: gfx_clear/rect/present (M14),
                        gfx_sprite added at M19, gfx_tilemap added at M20,
                        gfx_camera added at M21
                        (rect = one device-side FILL; tilemap = one TILEMAP;
                        camera = CAM_X/CAM_Y that SPRITE/TILEMAP subtract)
    counter.c           docs.md example program (counter = 123)
    mmio_demo.c         M11 guest: read IN register, write OUT register
    video_demo.c        M13 guest: CLEAR + SET_PIXEL + PRESENT over MMIO
    gfx_demo.c          M14 guest: draws only through the gfx.h API
    sprite_demo.c       M19 guest: blits a 1-bpp image (ROM + RAM sources)
    tilemap_demo.c      M20 guest: draws COLSxROWS tile grids via gfx_tilemap
                        (multiple tiles, per-cell colour, empty cells, ROM+RAM
                        cells, negative origin, clipping, guards)
    scroll_demo.c       M21 guest: pans a camera over a 512x352 world (32x22
                        cells of 16px tiles — larger than the screen) both
                        axes, with a world-space sprite and a fixed HUD
    platformer.c        M22 guest: the integration game — tilemap terrain,
                        animated player + coin sprites, camera follow,
                        AABB collision, jump physics, parallax layers, input
                        and audio, all in one side-scroller
    bench_gfx.c         voxel-experiment P0 research guest: 8 fixed video
                        workload phases measured by test_bench.c (not a game)
    terrain_gen.c       voxel-experiment P1 research guest: deterministic
                        (seed,x,z)->height/material generator, one row per
                        frame, checked against a host model by test_terrain.c
    raycast_demo.c      voxel-experiment P2 research guest: 80-column
                        first-person heightmap raycast (procedural heights,
                        FILL runs, camera pan), measured by test_ray.c
    sandbox.c           voxel-experiment P3 research guest: P2's raycast +
                        delta-overlay edits (walk/dig/place from the pad),
                        measured by test_sandbox.c
    systems.c           voxel-experiment P4 research guest: P3 + game systems
                        (inventory, crafting, slime entities, day/night
                        dithered tilemap overlay, HUD), measured by
                        test_systems.c
    breakout.c          M15 guest: the first real game (Breakout)
    tetris.c            M18 guest: the second game (Tetris — grid, font,
                        timer-driven gravity, fixed-seed piece stream); since
                        M20 the field[] IS the tilemap (one gfx_tilemap per
                        frame, no per-cell repaint loop)
    build/              generated .elf/.bin/.arc1 (gitignored)
tools/
    packarc1.c          host packer: flat .bin -> .arc1 game image (M17);
                        shares the header writer/CRC with the VM's rom.c
deps/libretro/
    libretro.h         vendored MIT-licensed libretro API header
tests/
    test_*.c           per-milestone harnesses (see below); test_tilemap.c (M20)
                       re-renders the whole framebuffer from an independent model
                       (as does test_scroll.c, M21, for every frame of a sweep);
                       test_platformer.c (M22) drives a scripted game and reads
                       the game globals out of RAM (like test_breakout.c)
```

Key boundary: `src/arcade1/` (the machine) has **no** libretro/RetroArch
dependency. `src/libretro/libretro.c` is the only file that touches libretro.
The machine is directly drivable via the `arcade1_*` API (see M06 test).

## Build & test

```
make              # build arcade1_libretro.so (desktop, x86-64 Linux)
make android      # cross-build the same core for RetroArch on Android TV /
                  # Google TV via the NDK: arcade1_libretro_android_arm64.so
                  # (aarch64) + arcade1_libretro_android_arm.so (armv7)
make test         # M02 test pattern (loads the .so)
make test-input   # M03 RetroPad input
make test-square  # M04 moving square
make test-beep    # M05 audio beep
make test-machine # M06 machine standalone (no libretro/.so)
make test-memory  # M07 memory bus (links ONLY memory.c)
make test-cpu     # M08 RV32IM decoder (links ONLY cpu.c)
make test-exec    # M09 RV32IM interpreter (links exec+cpu+memory, no .so;
                  #     extended at M16 with MUL..REMU semantics)
make program      # M10+ guests: C -> RV32 ELF -> flat .bin -> .arc1 image (M17)
make test-program # M10 run compiled C on the VM, assert counter == 123
make test-mmio    # M11 guest reads/writes an MMIO device register
make test-timer   # M12 deterministic timer: frames, MMIO regs, CPU lw, reset
make test-video   # M13 guest drives video device -> framebuffer, reset
make test-gfx     # M14 guest draws via gfx_clear/rect/present, exact pixels
make test-rom     # M17 .arc1 image format: CRC-32, header, every rejection
make test-sprite  # M19 sprite engine: 1-bpp blit, exact pixels + clipping
make test-tilemap # M20 tilemap engine: COLSxROWS grid, exact framebuffer vs an
                  #     independent model + determinism
make test-scroll  # M21 camera scrolling: 120-frame sweep over a map larger than
                  #     the screen, every pixel of every frame vs an independent
                  #     model, camera readbacks + determinism
make test-platformer # M22 integration game as firmware: scripted playthrough
                  #     (fall/land, walk+coin+camera, jump/land, reach goal,
                  #     restart) + audio beeps + determinism
make test-bench   # voxel-experiment P0 research micro-benchmark: measured
                  #     guest/host cost of SET_PIXEL/FILL/SPRITE/TILEMAP/CLEAR
                  #     (VOXEL_EXPERIMENT.md section 0.1) + determinism
make test-terrain # voxel-experiment P1 deterministic terrain generator:
                  #     independent host model + golden pins + 2-pass replay
                  #     (VOXEL_EXPERIMENT.md section 0.2)
make test-ray     # voxel-experiment P2 first-person raycast renderer:
                  #     height-eval + full-frame cost vs the 1M cap, render
                  #     property checks, PPM dump, determinism (section 0.3)
make test-sandbox # voxel-experiment P3 interaction (walk/dig/place over the
                  #     delta overlay): edit-model truth, edit visibility,
                  #     scan cost, determinism (section 0.4)
make test-systems # voxel-experiment P4 game systems (inventory, crafting,
                  #     slime entities, day/night dither overlay, HUD):
                  #     scripted scenario + replayed edit-model, section 0.5
make test-persist # voxel-experiment P5 persistence: input-log replay and
                  #     snapshot load-equals-replay bit-exact, save sizes
                  #     measured (section 0.6); harness-only, reuses systems
make test-breakout # M15 game as firmware (since M17 via its .arc1 image)
make test-tetris   # M18 game as firmware: pinned piece stream, exact
                   # playfields, line clear + compaction, 2-pass replay
make test-golden   # Task A: re-run the 4 game tests against the COMMITTED
                   # images in tests/roms/ and diff per-frame hash/fb lines
                   # vs tests/golden/ (first differing frame printed); needs
                   # no RISC-V compiler
make golden-update # manual: regenerate tests/golden/*.txt (deliberate act)
make roms-update   # manual: re-copy the 4 .arc1 images + nm dumps to tests/roms/
make check         # Task A: every test target + test-golden + test-frame-rules
make test-frame-rules # Task B: SPEC §8 frame boundaries, hand-assembled
                      #     guests (no RISC-V toolchain): PRESENT ends the
                      #     frame, parked guests re-end instantly, the cap is
                      #     exactly 1e6 steps
```

All builds use `-Wall -Wextra` and must stay warning-free.

Test strategy (per `docs.md` testing philosophy): milestone tests link the real
`.so` and drive it through the frontend callbacks; the machine and memory tests
link the sources directly with no libretro at all, proving frontend
independence. Pixel/audio values are verified deterministically (e.g. the
square is a unique orange `0xFD00` so tests can locate it by scanning).

## Housekeeping

A maintenance round (no roadmap change) that made three existing practices
measurable, one commit per task:

- **Stray bus access counter** — `bus.stray_accesses` counts every access the
  bus ignores or answers with a default (sub-word MMIO, out-of-range, ROM
  write, MMIO with no device); cleared by `arcade1_bus_init`/`_reset`.
  Tests assert it is 0 unless the test intentionally causes strays. To record
  it on reads, the three `arcade1_read*` entry points are no longer `const`.
- **Executed-instruction counter** — `cpu.instret` counts steps (including
  NOPs), cleared by `arcade1_cpu_reset`. A game's CPU load = instructions per
  frame (read before/after each frame).
- **State fingerprint** — `tests/state_hash.h` (test-only, header-only): an
  FNV-1a 64-bit hash over regs, PC, instret, RAM and the stray counter,
  hashed little-endian so it is host-independent. Used by `test_mmio` and
  `test_program` to assert a reset-and-replay yields an identical hash.
- **Build tidy-up** — test executables now build into gitignored `build/`
  (not `/tmp/arcade1_test_*`); target names unchanged. `test_pattern` prints a
  PPM path only when the file was really written (warnings stay non-fatal).
  A follow-up commit pinned `.DEFAULT_GOAL := all` so plain `make` still only
  builds the `.so` (the new prerequisite rule had made it run the tests).

## Regression suite (BEFORE_M25 task A)

A real golden regression, not just two-run comparisons. `tests/state_hash.h`
gained `fb_hash()` (FNV-1a over the RGB565 framebuffer); the four game tests
print `frame N hash=... fb=...` per frame. `tests/roms/` commits the exact
`.arc1` images + `nm` dumps; `tests/golden/<game>.txt` commits the per-frame
lines of a run against them. `make test-golden` re-runs and diffs (no RISC-V
compiler needed — verified with the cross tools off PATH); `make check` is
every test target + `test-golden`. The four final state hashes are unchanged
from before the work and match the reference host (see BEFORE_M25.md task A
report). Refreshing `tests/roms/` or `tests/golden/` is manual and deliberate:
a golden change is a behaviour change (docs.md §30 "Golden regression policy").

## Frame boundaries (BEFORE_M25 task B)

`SPEC.md` §8 gained "Frame boundaries": a frame ends at the first of (1) the
step that executes PRESENT, (2) the first step that leaves PC unchanged (a
parked guest then ends every later frame on its first step), or (3) 1,000,000
steps. Each case's timer tick, `STATUS` and host-visible output is pinned in a
table; `instret`/step counts are stated as diagnostic, not conformance.
`make test-frame-rules` (`tests/test_frame_rules.c`, hand-assembled guests,
no RISC-V toolchain) proves each rule against the reference.

**DECISION NEEDED (owner):** §8 said the 1M cap is "not part of the game
contract" while §10 rule 6 listed it *in* the contract. Both options are now
written out in §8 (A: truncation is contract; B: reaching the cap is a program
error, behaviour then implementation-defined) with B recommended; §10 rule 6
points at it. No `src/` change was made — the code matches the facts as
written.

## Device work budget (BEFORE_M25 task C)

Per-frame video-work counters, sized from data, to budget a future hardware
port. **`src/` change is host-only, guest-invisible, and in no state hash.**
`arcade1_video_t` gained two counter sets: `stats` (live, reset each frame by
`arcade1_video_stats_begin_frame`) and `frame_totals` (frozen by
`arcade1_video_stats_end_frame` just before `arcade1_run_frame` returns);
read via `arcade1_video_stats()` / `arcade1_video_frame_totals()`. Counted:
`cmds_*` per command kind, `commands`, `pixels_written` (post-clipping,
transparent bits excluded), `bytes_fetched` (one per pixel examined by the
reference blitter), `cells_visited` (post-windowing).

`tests/video_budget.h` accumulates an avg/max per frame over each game's run;
`tests/test_video_worst.c` (`make test-video-worst`, hand-assembled, no
RISC-V toolchain) drives five synthetic worst-case one-frame guests and
checks each counter against independent arithmetic. Both are in `make check`.
`DEVICE_BUDGET.md` holds the measured per-game and synthetic tables plus the
FPGA cycle conversion (1 px/clock, 1 byte/clock) against a 60 fps frame at
50/100 MHz, every number labelled MEASURED or ESTIMATED.

Key measured facts: the four games write 91k–130k px/frame and fetch 0–75k
bytes; CPU is negligible (≤2.1% of a frame). The worst *shipped* frame fits
one display frame with 3.8x–8.7x headroom depending on the cost model. But
two pathological synthetic frames (100 full-screen CLEARs; the max-command
unrolled loop) and the 1,000,000-step cap itself overrun a single frame even
at 100 MHz, so a hardware port needs a deterministic overrun policy.

**DECISION NEEDED (owner):** `DEVICE_BUDGET.md` §4 proposes three cost
models (serialized `inst+px+bytes`; pixels-only `px`; weighted
`inst+px/32+bytes`) with concrete budgets, headroom over the worst shipped
frame, and effect on the worst synthetic case, plus a shared deterministic
overrun-stretch policy (`N = ceil(cost/budget)` display frames). None is
implemented. The owner must pick a model, the budget number, and whether
overrun stretches or traps.

## Machine v0.2a: 480×270 screen (M25)

Changed the game resolution from 320×240 to **480×270** (16:9; exact 4× to
1080p, 8× to 4K). `ARCADE1_WIDTH`/`ARCADE1_HEIGHT` in `src/arcade1/machine.h`
are the single source of truth; the Libretro geometry (base/max size + 16:9
aspect) and every test derive from them.

* **Guests unchanged.** The three shipped games and the demos keep their
  current guest code and still draw in the top-left 320×240 region; the rest
  of the screen shows whatever their CLEAR colour leaves. The committed
  golden ROMs in `tests/roms/` are byte-identical, and the four game state
  hashes are **unchanged**: breakout `0x7816DD30A2B9D131`, tetris
  `0x782EA2CDAA0C51A7`, platformer `0x65C56C87AE736277`, scroll
  `0xB71FDB994B5D6576`.
* **Golden files regenerated** (`tests/golden/*.txt`): only the per-frame
  `fb=` (framebuffer) hashes changed — the framebuffer is bigger. The
  per-frame state `hash=` values are identical (verified on frame 0 of
  breakout: `hash=` unchanged, `fb=` changed).
* **Tests updated** (expectations recomputed from the macros, none weakened):
  the four libretro harnesses (`pattern`/`input`/`square`/`beep`) take W/H
  from the macros; `input`'s overlay mirror re-centres from the macros; `gfx`
  and `sprite` re-count pixels for the frozen demos whose on-screen rect /
  off-screen sprite no longer clip at the new size; `video_worst`'s
  arithmetic auto-scales (its full-screen TILEMAP is now 30×16 = 480 cells).
* **Assumptions / limitation introduced:** the frozen demos' right/bottom
  "overhang" and "fully off-screen" cases now fall fully on-screen at
  480×270, so FILL/SPRITE/TILEMAP right/bottom clipping is no longer
  exercised by those particular guests (negative-origin and fully-above cases
  still clip, and the negative rect still clips FILL). Guests were
  deliberately **not** re-laid-out (out of scope for M25).
* SPEC.md §8/§10: resolution pinned to 480×270; the two open decisions
  (1M-step-cap §8/§10 contradiction; per-frame work budget in
  `DEVICE_BUDGET.md`) are marked **DEFERRED** (owner postponed until the FPGA
  milestone; current reference behaviour stays). No option was picked.

Verify: `make clean && make check` (all tests + `test-golden`), `make android`.

## Known limitations

- Display is **480×270 @ 60 fps** (pinned at M25 — 16:9, exact 4× to 1080p),
  48 kHz audio. The shipped games/demos still draw in the top-left 320×240
  region (guests not re-laid-out at M25).
- Memory map (ROM 0x0000+4K, RAM 0x1000+4K, MMIO 0x4000+4K, with a 0x2000
  gap) is a placeholder, not the final ARCADE-1 map. MMIO routes to a
  host-registered handler that the machine muxes between its devices: timer
  (M12) at 0x000..0x0FF, video (M13) at 0x100..0x1FF, input (M15) at
  0x200..0x2FF, audio (M15) at 0x300..0x3FF.
- The test pattern, moving square, and beep are host-driven demo logic inside
  the machine, kept for the **content-less** core (M02–M06 behaviour, still
  covered by `make test`, `test-input`, `test-square`, `test-beep`). Since M15
  they do **not** run when firmware is attached — the guest owns the frame
  then. The audio beep itself became device-driven at M15 (AUDIO_FREQ/CTRL,
  power-on 440 Hz enabled), so it is shared by both modes.
- Guest programs execute two ways since M15: (a) as **firmware** booting in the
  machine's frame loop (`arcade1_set_firmware`, used by `make test-breakout`
  and by loading content in RetroArch), and (b) stepped by standalone harnesses
  (`make test-program`, `test-mmio`) that keep their own CPU. The machine's
  own CPU runs only while firmware is attached.
- MMIO is minimal by design (M11): one 4 KiB window (0x4000) routed to a single
  host-registered handler, 32-bit accesses only (sub-word lw/sw are no-ops).
  The machine's handler muxes the window: timer at 0x000..0x0FF, video at
  0x100..0x1FF, input at 0x200..0x2FF, audio at 0x300..0x3FF; unknown offsets
  read 0 / ignore writes.
- Timer (M12): exactly 1 tick per frame, registers `TIMER_COUNT` (read-only)
  and `TIMER_CTRL` (bit0 = enable), no interrupt/pending logic. Breakout (M15)
  is the first per-frame reader (beep duration, death pause).
- Video (M13): single framebuffer, no flipping — `PRESENT` only counts
  executions (buffering deliberately left undecided per docs.md). With
  firmware attached (M15) a PRESENT also ends the frame: the machine runs the
  guest until the guest presents.
- Graphics API (M14, extended M19/M20): `programs/gfx.h` provides `gfx_clear` /
  `gfx_rect` / `gfx_present`, `gfx_sprite` (M19) and `gfx_tilemap` (M20) as a
  **guest-only** header (it dereferences fixed MMIO addresses — host tests must
  use the `arcade1_*` API instead). Since M19 a rectangle is one device-side
  FILL and a 1-bpp image is one SPRITE (set bits = COLOR, clear bits =
  transparent, source restricted to guest ROM/RAM); since M20 a whole COLSxROWS
  grid of 32-bit tile+colour cells is one TILEMAP (tile 0 = empty, per-cell
  RGB565 tint, same 1-bpp tile format as a sprite), so the per-pixel and
  per-cell loops no longer run in the guest. Since M21 a camera (CAM_X/CAM_Y)
  pans the world-space commands (SPRITE/TILEMAP) while primitives stay
  screen-space (HUD), and TILEMAP windows to the visible cells. The platformer
  (M22) composites multiple layers with **parallax** — a background of
  screen-space rects drawn at half/quarter the camera rate, over the world-space
  terrain tilemap — but that is guest-side positioning, not a device feature.
  Still no device-side layer/priority/visibility flags, flip, alpha blending or
  rotation (forbidden by docs.md until a later milestone asks for them).
- Breakout (M15): full-screen clear + repaint of every rectangle each frame
  (now 2,039 instructions/frame avg, 2,185 max over 595 frames since M19's
  device-side FILL — down from 77,836 when rects were per-pixel `sw`s); no
  dirty rectangles, no text beyond a 3x5 digit font, one level/fixed launch
  trajectory, no title screen. The frame protocol is present-yield with a
  1,000,000-instruction safety cap per frame. Game state (score/lives/state)
  is read from guest RAM by the test via `nm` symbol addresses.
- Tetris (M18, updated M20): the same present-yield loop; CPU load is now
  8,079 instructions/frame avg (14,309 max over 300 frames) since M20's
  tilemap — down from 9,384 after M19 and from 245,797 when every cell/rect/
  glyph was a per-pixel `sw`. The field[] IS the playfield tilemap, so draw()
  is one gfx_tilemap command (no per-frame 200-cell repaint loop); the
  residual is game logic, the falling/preview piece, and the per-glyph panel
  text. The machine has no entropy device, so the piece stream is
  xorshift32 with the fixed seed `0x1234ABCD` and START rewinds it — adding
  entropy later would break reset-and-compare. A piece's 4x4 box may hang
  off-screen left; cells clamp individually at column 0 (the device clips
  draws).
- Platformer (M22): present-yield loop; CPU load 836 inst/frame avg (6,324 max
  over 308 frames) — lighter than Breakout because terrain/sprites/camera are
  device-side. The 40x16 terrain `cells[]` is 2,560 B of RAM (the dominant
  memory cost, as with Tetris), built once at boot; the guest is 2,404 B of
  ROM. Physics is intentionally simple (integer velocities, axis-separated
  AABB, gravity 1 / jump 11 / terminal 5) and the level is flat-ground with
  floating platforms so a scripted "hold right" reaches the goal — chosen to
  keep the harness deterministic rather than to be a good game. Parallax is
  guest-side (screen-space rects at half/quarter camera rate), not a device
  layer feature. No enemies, scrolling enemies, save, or title screen.
  *Gameplay repair (M22 follow-up): the floating platforms (rows 11 and 12,
  tops at y=176/192) and the coins above them were unreachable — with the old
  `JUMP 6` the player rose only 15 px (under one tile) from the ground at
  y=224, so neither the platforms nor the elevated coins could ever be
  touched, defeating M22's purpose of exercising real platformer mechanics.
  Repair: `JUMP` 6 -> 11 (rise = J(J-1)/2 = 55 px), the smallest change that
  clears the row-11 platforms with a 7 px landing margin. Platform rows,
  coins, collision, camera, sprites, audio and the frame protocol are
  unchanged; the platforms must stay at row 12 or higher (row 13 would sit in
  the walking lane and block the hold-right path to the goal). Verified on the
  real guest: all three platforms reachable by jumping from the ground, the
  elevated coins collectible by landing + walking, and hold-right still
  reaches the goal with exactly the 3 ground coins. This changes only the
  platformer guest (a game demo, not the ISA/device spec); its `.arc1`,
  per-frame golden and state hash were regenerated, the other three games'
  hashes are unchanged.*
- Guest programs build with `riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32
  -ffreestanding -nostdlib -msmall-data-limit=0` + `programs/link.ld` +
  `crt0.S`. No libc/SDK; `.data` is stored in ROM and copied to RAM, `.bss` is
  zeroed, stack grows down from the top of RAM. Guest image must fit the 4 KiB
  ROM (largest so far: tetris at 3176 bytes; its .arc1 file is that payload
  plus a 16-byte header). Since M16 the ISA is RV32IM,
  so guest C may use `*` and `/` directly (spec-exact semantics, no traps —
  division by zero and `INT_MIN / -1` return the defined results) with no
  libgcc.
- The `.arc1` image (M17) is deliberately minimal v1: 16-byte header +
  CRC-32-checked payload, nothing else — no metadata, asset sections or
  compression yet (extension room = reserved bytes + the version field).
  The CRC covers the payload only; header corruption is caught by the
  magic/version/size checks instead. The core still accepts raw flat
  `.bin` content for backward compatibility; new games ship as `.arc1`.
- The M extension (M16) is interpreter-only: all eight instructions are
  single-step interpreted with no special fast path or early-out, so a divide
  costs the same as an add per instruction.
- FENCE/ECALL/EBREAK currently execute as **NOP** (PC advances); there is no
  OS/syscall/halt handling yet. Unknown words also act as NOP. `crt0` "ends"
  a program by parking in a self-loop (`j .`), which the harness detects.
- Save states / cheats unsupported (stubs).

## Notes for whoever picks this up

- Follow `docs.md` Rule 1: implement only the current milestone, then stop.
- M10+ needs a RISC-V bare-metal toolchain: `riscv64-unknown-elf-gcc` (present
  here as GCC 13.2.0). If it's missing, guest builds fail at `make program`.
- `FUTURE_IDEAS.md` / `FUTURE_MILESTONES.md` are scratch files (not part of a
  milestone); treat as optional input, not a commitment.
- Each milestone has been verified by a deterministic test harness plus a live
  RetroArch 1.18.0 load (RGB565, clean startup/shutdown; since M15 also with
  content: `retroarch -L ./arcade1_libretro.so programs/build/breakout.bin`
  boots and runs the game). Re-run `make test-*` after any change.
