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
| 26        | Machine v0.2b: Memory (256 KiB ROM / 64 KiB RAM) | COMPLETE |
| 27        | Machine v0.2c: Color Graphics (palette + SPRITE4/TILEMAP4) | COMPLETE |
| 28        | AI SDK v0.1 (headless runner `arcade1 run`) | COMPLETE |
| 29+       | (see `MILESTONES_2.md`)    | NOT STARTED |

Next up: **Milestone 28 — AI SDK v0.1**. The foundation (headless `arcade1 run` with
scripted input, per-frame hashes and PPM frame dumps) is landed; the rest of M28
is record/replay capture, the full `arcade1 build/test/capture` command set,
named game variables, a cheat sheet and two example games — see `SDK.md`.

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

### `make check` wall time

`make check` (every test target + the golden regression) has a **budget of
120 s**. Measured on this machine (2026-10-20, ROADMAP_V4 card 4.9):

| date | commit | `make clean && make check` |
|---|---|---|
| 2026-10-21 | BRICKSTORM (Game #5) | 1 m 57 s (117 s) |
| 2026-10-21 | TEMPEST (Game #4) | 1 m 52 s (112 s) |
| 2026-10-21 | the library's day sky and faces | 1 m 48 s (108 s) |
| 2026-10-20 | card 4.9 | **1 m 28 s** (88 s) |
| 2026-10-20 | `22fdb0d` (card 4.8) | 1 m 22 s (82 s) |
| 2026-10-20 | `711a2e4` (before Wave 7a) | 1 m 42 s (102 s) |

The suite got *faster* in Wave 7a: the compressed PNG writer (card 4.5)
stops the test suite from writing 6 MB of stored deflate blocks per capture.
If a new test would push the total over 120 s, **report it before adding
the test** (ROADMAP_V4 card 4.9) — the budget is a promise to whoever
runs the suite, not a formality.

Test strategy (per `docs.md` testing philosophy): milestone tests link the real
`.so` and drive it through the frontend callbacks; the machine and memory tests
link the sources directly with no libretro at all, proving frontend
independence. Pixel/audio values are verified deterministically (e.g. the
square is a unique orange `0xFD00` so tests can locate it by scanning).

## TEMPEST (Game #4) — the vertical shoot-'em-up

A classic arcade shmup, modernised: the player flies a cyan interceptor
continuously upward through a scrolling battlefield while formations of
fighters, diving drones, attack helicopters, heavy bombers and a cruiser
come down, and anti-aircraft turrets, tanks and missile launchers work
the ground below. Every stage ends in a multi-phase boss with a weak
point in its core.

* **The game**: `programs/shmup.c` (freestanding C for RV32IM, no libc).
* **The art**: `programs/shmup_art/gen_art.py` paints every sprite
  procedurally (18 strips, 8 palette banks) and writes the manifest;
  `programs/shmup_assets.h` is the compiled header and the build truth
  (`make shmup-art` regenerates both).
* **The controls**: the D-pad flies, **A** fires, **B** is the FOCUS (a
  slower, tighter shot and the hitbox on screen — the classic
  bullet-hell bargain), **START** starts and restarts.
* **The shape**: a 4x4 hitbox (the classic tiny target), three lives,
  the screen clears when the ship dies (the classic mercy), a weapon
  level of 1..4 from the power-ups, a shield pickup, a kill combo, an
  extra life at 50,000, and a boss that gets angrier as it dies (three
  phases: aimed shots, fans plus drones, then a fan and a spiral).
* **The sound**: the audio device's one tone generator, driven straight
  from the guest's MMIO (the first shipped game to make a sound — the
  cheat sheet now documents the guest side).
* **The test**: `tests/test_shmup.sh` with the scripted session
  `tests/inputs/shmup.txt` — the title, the waves, the boss (all three
  phases) and the start of stage 2 — plus a 2,600-frame golden
  (`tests/golden/shmup.txt`) and a committed ROM.
* **Cost**: 8,665 instructions and 166,296 pixels a frame (1.28 screens)
  — under 1 % of the instruction cap and leaner than Petal Blade's 2.59
  screens.

## BRICKSTORM (Game #5) — the breakout remake

The milestone-15 Breakout, done again from nothing: the machine's native
480×270 instead of the old 320×240, sprite art instead of flat
rectangles, and a modern arcade's worth of feel on the classic bones.
**The original is untouched** — `programs/breakout.c`, its golden and its
ROM are exactly as they were.

* **The game**: `programs/brickstorm.c` (freestanding C for RV32IM).
* **Twelve brick layouts and a boss** (thirteen stages): FIRST LIGHT,
  PYRAMID, CHECKER, FORTRESS, DIAMOND, HIVE, COLUMNS, STAIRCASE,
  GAUNTLET, CROSS, LATTICE, CROWN and THE CORE. The title screen's
  **level picker** (left/right) lets a player jump to any stage.
* **The feel**: the paddle returns the ball at an angle that depends on
  where you hit it (thirteen steps from -60° to +60°), a **lost ball
  costs a life and not the bricks you have already broken** (the classic
  rule — the first cut reloaded the field and that was wrong), and the
  screen clears when you die.
* **The physics, revised after play-testing.** The first cut sampled the
  paddle collision once a frame, so the launch angle was a third of a
  step off the one the player aimed at; the ball's speed crept up on
  every hit, so the rally turned to noise; and nothing stopped a bounce
  from flattening the ball, so it could settle into a loop between the
  wall and the paddle. Now: the contact is **swept** (the exact point
  where the ball crossed the paddle's plane decides the angle), the
  speed is a property of the **stage** (it grows a little per level and
  never creeps mid-rally), no bounce may leave the ball shallower than
  **60° from the vertical** (it is put back on that line at the same
  speed), and the paddle's own motion adds a little **english** — but
  only within the direction the catch point chose: a ball caught on the
  left always leaves to the left, and a ball caught dead centre leaves
  straight up. The catch point is the joystick; the sweep is a garnish.
  The ball's velocity is observable (`ball_vx`, `ball_vy`) so the
  invariants can be checked: over a whole session the ball is never
  flatter than 60 degrees.
* **The contact recorder.** The game publishes every paddle contact (the
  frame, where the ball landed, which side of the centre that was, and
  the velocity it left with), and `tools/record_hits.py` turns the
  CLI's per-frame `--vars` trace into a contact log — one line per
  contact, with the exact frame — and checks the catch-point rule over
  the whole log. It is a regression, and it found two bugs:
  * the paddle's launch step lost its **SIGN**, so the paddle always
    kicked the ball to the right, whatever side it was caught on;
  * the flat-angle limit was **inverted** — it forbade the ball from
    travelling flatter than 49° above the horizontal, while the
    shallowest *legal* angle is 30°, so it re-aimed every single
    launch and the catch point decided nothing but the side. That was
    the "an invisible glove moves the ball" feeling: the limit was
    quietly overwriting the player's aim on every contact. The log
    shows it — every contact used to leave with the same |vx|; now a
    catch 22 px left of the centre leaves at −32 and one 19 px right
    at +25, i.e. the catch point decides the angle again.
* The power-up drops are **rarer** (one in fourteen broken bricks, not
  one in five).
* **Four brick kinds**: normal, tough (two hits — it cracks first),
  steel (the ball bounces off it) and bomb (it takes its neighbours with
  it, three links deep).
* **Six power-ups**: EXPAND, MULTI (three balls), SLOW, FAST, LASER (the
  paddle shoots) and CATCH (the ball sticks until you release it).
* **The boss**: a 200×72 dreadnought whose underside is the weak strip
  and whose three cores are worth double. It sweeps smoothly (a square
  wave could never be lined up with) and it shoots.
* **The art**: `programs/brickstorm_art/gen_art.py` paints 20 sprites in
  11 palette banks — the paddle in three widths, the ball, the five brick
  rows, the tough, steel and bomb bricks, the six power-up capsules, the
  boss and its core. The compiled header
  `programs/brickstorm_assets.h` is the build truth (`make
  brickstorm-art` regenerates both).
* **The sound**: the audio device's tone, pitched by where the ball hit
  the paddle and by which row of brick it broke.
* **The test**: `tests/test_brickstorm.sh` with the scripted session
  `tests/inputs/brickstorm.txt` (the title, level 1 played to the game
  over, the level picker, the boss and the win) — plus a 2,600-frame
  golden and a committed ROM. The stage-to-stage progression is *not*
  scripted: with the new physics a blind paddle cannot clear a stage
  (the ball's path is too precise for a fixed sweep, and a real player
  breaks the loop by moving), so the stage-CLEAR path is covered by the
  boss's death instead — the last stage ends like any other.
* **Cost**: 7,155 instructions and 167,541 pixels a frame (1.29 screens)
  — under 1 % of the instruction cap.

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

## Machine v0.2b: Memory (M26)

Enlarged ROM to **256 KiB** and RAM to **64 KiB** and moved to a
growth-friendly map with each region on a 512 MiB boundary:

| Region | Base         | Size    |
|--------|--------------|---------|
| ROM    | `0x00000000` | 256 KiB |
| RAM    | `0x20000000` | 64 KiB  |
| MMIO   | `0x40000000` | 4 KiB   |

The reset vector stays at `0x00000000`; the stack top derives to `0x20010000`.
The map is pinned in `SPEC.md` §2; the guest mirror is `programs/arcade1_map.h`
(included by `gfx.h` and every direct-MMIO program). The v0.1 placeholder map
(ROM 4 KiB at 0x0000, RAM 4 KiB at 0x1000, MMIO 4 KiB at 0x4000) is superseded.

* **Bus rules unchanged** (no faults, out-of-range reads 0, writes only inside
  RAM, ROM writes ignored, MMIO 32-bit only, stray counter). The `.arc1`
  payload limit now tracks `ARCADE1_ROM_SIZE` (256 KiB); image version 1 and
  the CRC rule are unchanged.
* **Guests switched to the shared map header** (`arcade1_map.h`); `gfx.h` and
  the direct-MMIO programs use `ARCADE1_MMIO_BASE` instead of hard-coding
  `0x4000`. `link.ld` moves RAM to `0x20000000` (stack top derives to
  `0x20010000`); `crt0.S` needs no change (it loads `_stack_top`).
* **Guest behaviour preserved.** The four game framebuffers are byte-identical
  (golden `fb=` unchanged); only the per-frame state `hash=` changed, because
  the state hash covers RAM, which grew and moved. Final state hashes: breakout
  `0x91B248CDCEC093C1`, tetris `0xF1936F22ACFBB807`, platformer
  `0xE0EE4A90F16E5E67`, scroll `0x99242B25C526BE86`. The pinned state hashes in
  `test_ray`/`test_sandbox`/`test_systems`/`test_persist` were updated for the
  same reason (their frame digests are unchanged).
* **`sprite_demo`/`tilemap_demo`'s "illegal source"** is the MMIO window (not
  image data); that address moved with the map, so they still draw nothing —
  output byte-identical.
* **Tests updated** (expectations recomputed from the macros, none weakened):
  fixed-address tests (exec RAM base, frame-rules/timer/worst LUI, tetris/
  breakout MMIO base, video-worst tile/sprite RAM addresses) moved to the new
  map; the worst-case TILEMAP now fits all 480 tiles in 64 KiB RAM (was 128 of
  480), its assertions auto-derive from `ARCADE1_RAM_SIZE`; `test_memory` gains
  a case proving the old `0x1000`/`0x4000` bases are now inside ROM (a write to
  either is a stray).
* `tests/roms/` regenerated (guest images + `.syms` at the new base). Golden
  `fb=` values are byte-identical for all four games; only `hash=` changed.

Verify: `make clean && make check` (all tests + `test-golden`), `make android`.

## Machine v0.2c: Color Graphics (M27)

Added paletted 4-bit graphics on top of the unchanged 1-bit commands:

* **Palette**: 256 RGB565 entries (16 banks × 16 colours) in the video device,
  addressed via `PAL_INDEX` (0x138) / `PAL_DATA` (0x13C; a write stores
  `value & 0xFFFF` and auto-increments, wrapping 255→0; a read does not
  increment). Power-on: entry 0 = black, entries 1..15 = a fixed EGA-like
  default, 16..255 = black (exact values pinned in MILESTONES_1.md "Color
  decisions, pinned at M27" and mirrored by the tests).
* **SPRITE4** (command 7): draws a 4-bit image (two pixels per byte, HIGH nibble
  first, even width; pixel 0 transparent, `v` → `PALETTE[bank×16 + v]`),
  world-space + clipped like `SPRITE`. Bank from `PAL_BANK` (0x140), flips from
  `FLAGS` (0x144, bit0 X / bit1 Y).
* **TILEMAP4** (command 8): like `TILEMAP` (same windowing/camera/clip) but each
  32-bit cell supplies its own bank `[19:16]` and flips `[20]`/`[21]` (index in
  `[15:0]`, 0 = empty); tile *N* at `tile_addr + (N−1) × (W/2 × H)`.
* **Invalid sizes ignored**: zero or odd `W` draws nothing; out-of-ROM/RAM source
  draws nothing with no bus access. `bytes_fetched` for 4-bit commands counts
  one byte per two pixels examined; new host-only `cmds_sprite4`/`cmds_tilemap4`
  counters.
* **Existing commands/behaviour/goldens unchanged** — the four game goldens and
  their ROMs are byte-identical (sha256-verified); the palette is unused by
  1-bit commands.
* **New tests**: `test-video-palette`, `test-video-sprite4`, `test-video-tilemap4`
  (each re-verifies the whole framebuffer against an independent model), and
  `test-color` (color_demo: scrolling TILEMAP4 world + SPRITE4 discs + 1-bit HUD,
  property checks + determinism + pinned hashes + PPM dump). **color_demo is the
  5th golden game** (`tests/roms/color_demo.arc1`, `tests/golden/color.txt`).
* `programs/gfx.h` gained `gfx_pal_upload`/`gfx_pal_bank`/`gfx_flip`/`gfx_sprite4`/
  `gfx_tilemap4`.

Verify: `make clean && make check` (all tests + 5-game `test-golden`), `make android`.

## Example games, cheat sheet, docs (M28, steps 4–10)

Closing the M28 SDK gaps (`MILESTONES_2.md` M28; the three docs keep
distinct roles: CHEATSHEET.md = guest API, SDK.md = CLI, GAME_DEV_GUIDE.md
= workflow). One commit per step; `make clean && make check` after each;
the five older goldens (breakout/tetris/platformer/scroll/color) are
byte-identical throughout (md5-verified before and after).

* **Step 4 — `programs/arcade1.h` drawing helpers**: thin wrappers
  `arcade1_sprite/sprite4/tilemap/tilemap4` over the gfx.h commands, with
  the units stated once (world-space coordinates, ROM/RAM image
  addresses, 32-bit map cells, 4-bit bytes = two pixels, leftmost in the
  high nibble). No new machine behaviour.
* **Step 5 — `programs/example_color.c` fixed**: the old demo poked raw
  registers, pointed TILE_ADDR at 0 and left an unexplained NOTE. Now a
  40x20 grid of `unsigned int` cells ([15:0] tile, [19:16] bank, [20]
  flipX, [21] flipY), four real 16x16 4-bit tiles with the tile-size
  registers set by the helper, a 16-colour palette + a 4-entry bank 1, a
  camera that moves every frame, and an asymmetric arrow sprite drawn
  flipped. Frame 0 has 8 distinct colours.
* **Step 6 — observable state as file-scope globals**: example_pong
  (bx,by,dx,dy,py,oy,score,opp_score,state), example_snake
  (sx,sy,length,dx,dy,fx,fy,score,state), example_color (cam_x,cam_y) —
  so `--vars`/`--syms` can read them. The pong opponent AI now tracks at
  2 px/frame (ball vertical is 3) so a rally can be won.
* **Step 7 — tests + goldens for the three examples**: they join the
  golden regression (GUEST_PROGS/GOLDEN_IMGS/GOLDEN_GAMES) with shell
  tests in the style of test_run.sh asserting through named variables and
  captured frames — pong: sweep rally scores 4-5, first to 5 sets game
  over, START resets (trace-checked); snake: growth (length 3→4, score
  0→1) then wall game over, START resets; color: ≥6 distinct colours,
  camera (0,0)→(100,25) by frame 50, tilemap band differs. New goldens
  pong.txt/snake.txt/example-color.txt; the five older ones unchanged.
* **Step 8 — CHEATSHEET.md**: the whole guest API in 120 lines (all
  commands, palette, camera, flags, coordinate spaces, clipping/ignore
  rules, input bits, memory map, frame protocol, the observable-state
  rule, CLI usage). `tests/test_cheatsheet.sh` extracts every fenced C
  block and compiles it with the real guest toolchain under -Wall
  -Wextra, so documented API drift fails make check.
* **Step 9 — GAME_DEV_GUIDE.md**: the pasted copy of gfx.h (200 lines of
  drift-prone duplication) is removed; the doc keeps exactly the workflow
  role and points at the headers. Pinned: the three docs' roles, the two
  testability rules, the build/run/test/golden-lock workflow, and that
  experimental/ holds untested works in progress.
* **Step 10 — asteroid_run.c**: converted to arcade1.h and moved to
  programs/experimental/ — NOT built by default, NOT covered by make check
  (a `make experimental` target builds it on demand; verified it compiles
  and runs). The test+golden promotion is deferred.

Verify: `make clean && make check` (all tests + 8-game `test-golden`),
`make android`.

## Assumptions

* The pong opponent AI was slowed (3 → 2 px/frame) so the scripted rally
  can produce player scores; the task did not pin the AI's difficulty.
* The pong test's restart assertion keys on the deterministic game-over
  frame (~675) and a START held from frame 725; both were verified
  empirically and are stable because the machine is deterministic.
* The color test compares a full-width band (rows 32..127) as "the
  tilemap area"; the camera shift (100,25) is not a tile multiple, so the
  band is guaranteed to differ.
* "At least 6 distinct palette colours" is checked on the rendered frame
  (8 found), not just the uploaded palette entries.
* asteroid_run takes the experimental route (no test/golden); promoting
  it later means adding it to GUEST_PROGS + a test + golden-update.
* The golden game entry for example_color is spelled `example-color`
  (hyphen) because make target names use hyphens; the golden file is
  tests/golden/example-color.txt.

## Record/replay capture (M28, step 3)

Third slice of the developer SDK (`MILESTONES_2.md` M28; design principles
in `AI_NATIVE_DEVELOPMENT.md`, contract in `SDK.md`). The write side of the
replay primitive: an input script is already the complete, reproducible
record of a session's controls — `--record` produces one from a run.

* **`--record FILE`** (`tools/arcade1.c`): run-length encodes the applied
  per-frame button state (what `--input` resolved to, or all-idle without
  it) into the input script format — consecutive frames in the same state
  merge into one entry, idle stretches record as `none`, overlapping script
  entries (later lines win) normalize into canonical runs. Recording works
  with or without `--input`; an unwritable record file is a runtime error
  (exit 1), failed fast before the run starts.
* **Round-trip by construction**: a recording fed back through `--input`
  replays the session bit-identically (same per-frame hashes) — the property
  `make test-run` asserts.
* **Test coverage** — `make test-run` (`tests/test_run.sh`) gained check 7:
  normalization of an overlapping script, an all-`none` idle recording,
  byte-identical repeat recordings, the record→replay hash round-trip, and
  the unwritable-file exit.
* **Docs** — `SDK.md` gained a "Record/replay" section (encoding, canonical
  form, round-trip contract); the feature moved out of "Remaining M28
  deliverables".

Verify: `make clean && make check` (all tests + 5-game `test-golden`), `make android`.

## Named game variables (M28, step 2)

Second slice of the developer SDK (`MILESTONES_2.md` M28; design principles
in `AI_NATIVE_DEVELOPMENT.md`, contract in `SDK.md`). Per-frame hashes answer
"what does the machine believe?"; named variables answer "what does the
**game** believe?" — a game's own globals read out of guest RAM by name.

* **`--syms/--vars/--var-every`** (`tools/arcade1.c`): load an `nm` symbol
  dump (the same `<game>.syms` the golden regression uses), select RAM
  data/bss globals by name (comma list, or `all`), and print each as one
  signed 32-bit word — exactly how the game tests read score/lives/state.
  Final values print as `var <name> = <value>`; `--var-every K` adds a
  `vars <frame> <name>=<value> ...` trace line every K frames. Unknown names
  and non-RAM symbols (code/linker marks) are runtime errors (exit 1);
  `--vars` without `--syms` is a usage error (exit 2).
* **Test coverage** — `make test-run` (`tests/test_run.sh`) gained check 6:
  named reads (`breakout` `score = 0` / `lives = 3` after 30 idle frames),
  the `--var-every` trace, and both error exits. Works against both ROM
  layouts: committed `tests/roms/*.syms`, or a dump generated from the
  freshly built ELF via `riscv64-unknown-elf-nm` (skipped with a note if
  neither is available).
* **Docs** — `SDK.md` gained a "Named game variables" section (semantics,
  error contract, test coverage); the feature moved out of "Remaining M28
  deliverables".

Verify: `make clean && make check` (all tests + 5-game `test-golden`), `make android`.

## AI SDK v0.1 — foundation (M28, step 1)

First slice of the developer-facing SDK (`MILESTONES_2.md` M28; design principles
in `AI_NATIVE_DEVELOPMENT.md`, contract in `SDK.md`). A single headless tool over
`src/arcade1/` only — no Libretro/RetroArch/GPU/host-input — so it runs
identically in a terminal, CI, or an AI agent's sandbox.

* **`arcade1 run`** (`tools/arcade1.c`, built as `build/arcade1`): loads a game
  image (`.arc1` validated, or raw `.bin`), runs a fixed number of frames with
  optional scripted input, prints one `frame N hash=… fb=…` line per frame, and
  dumps framebuffers as PPM (P6). Flags: `--frames --input --capture
  --capture-dir --capture-every --quiet`; exit codes 0/1/2.
* **Hashes are the golden fingerprints** — the tool includes `tests/state_hash.h`
  (via `-Itests`), so its `hash`/`fb` values are bit-identical to the golden
  regression's and directly diffable against `tests/golden/<game>.txt`.
* **Input script format** — plain-text run-length lines
  `<first> <last> <buttons…>` (later lines win, uncovered frames = no buttons,
  `#` comments). This is the replay primitive; `--record` (log a live session to
  this format) is a later step.
* **New test `make test-run`** (`tests/test_run.sh`, in `make check`): asserts
  determinism (two identical runs byte-identical), **golden faithfulness** (its
  120 color_demo frame hashes match `tests/golden/color.txt`), PPM capture size
  (388815 bytes = 15-byte header + 480×270×3), scripted input changes breakout's
  state hash reproducibly, and the 0/1/2 exit codes.
* **Not built yet** (rest of M28): record/replay `--record`, the full
  `arcade1 build/test/capture` command set, named game variables, a cheat sheet
  and two example games — see `SDK.md` "Remaining".

Verify: `make clean && make check` (all tests + 5-game `test-golden`), `make android`.

## Known limitations

- Display is **480×270 @ 60 fps** (pinned at M25 — 16:9, exact 4× to 1080p),
  48 kHz audio. The shipped games/demos still draw in the top-left 320×240
  region (guests not re-laid-out at M25).
- Memory map (v0.2 since M26: ROM 256 KiB at 0x00000000, RAM 64 KiB at
  0x20000000, MMIO 4 KiB at 0x40000000, with gaps between) is growth-friendly,
  pinned in `SPEC.md` §2 and `programs/arcade1_map.h`. MMIO routes to a
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
- MMIO is minimal by design (M11): one 4 KiB window (0x40000000) routed to a single
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
  zeroed, stack grows down from the top of RAM. Guest image must fit the 256 KiB
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
