# ARCADE-1 — Progress Log

Living record of milestone status. The roadmap itself is in `docs.md`; this
file tracks what is actually done, how to verify it, and known limitations.
The M23 architecture review is a standalone document in `ARCHITECTURE.md`, and
the v0.1 machine specification it produced (M24) is in `SPEC.md`. A
post-M24 research experiment (not a milestone, no roadmap change) — using a
Minecraft-like voxel sandbox as an architectural stress test — is written up
in `VOXEL_EXPERIMENT.md`; its SDK findings feed M25. Experiment phases P0
(graphics micro-benchmarks), P1 (deterministic terrain generator), P2
(raycast renderer) and P3 (walk/dig/place interaction) are complete:
measured costs live in `VOXEL_EXPERIMENT.md` §0.1–§0.4, runnable via
`make test-bench`, `make test-terrain`, `make test-ray` and
`make test-sandbox`.

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
| 25+       | (see `FUTURE_MILESTONES.md`) | NOT STARTED |

Next up: **Milestone 25 — SDK v0.1** (build the real developer-facing ARCADE-1 SDK). See `FUTURE_MILESTONES.md` for the exact scope.

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
make test-breakout # M15 game as firmware (since M17 via its .arc1 image)
make test-tetris   # M18 game as firmware: pinned piece stream, exact
                   # playfields, line clear + compaction, 2-pass replay
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

## Known limitations

- 320x240 @ 60 fps, 48 kHz audio — placeholder resolution/rate, NOT final
  (per roadmap: do not freeze the display resolution yet).
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
- Platformer (M22): present-yield loop; CPU load 841 inst/frame avg (6,324 max
  over 295 frames) — lighter than Breakout because terrain/sprites/camera are
  device-side. The 40x16 terrain `cells[]` is 2,560 B of RAM (the dominant
  memory cost, as with Tetris), built once at boot; the guest is 2,404 B of
  ROM. Physics is intentionally simple (integer velocities, axis-separated
  AABB, gravity 1 / jump 6 / terminal 5) and the level is flat-ground with
  floating platforms so a scripted "hold right" reaches the goal — chosen to
  keep the harness deterministic rather than to be a good game. Parallax is
  guest-side (screen-space rects at half/quarter camera rate), not a device
  layer feature. No enemies, scrolling enemies, save, or title screen.
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
