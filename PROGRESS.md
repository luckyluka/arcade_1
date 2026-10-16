# ARCADE-1 — Progress Log

Living record of milestone status. The roadmap itself is in `docs.md`; this
file tracks what is actually done, how to verify it, and known limitations.

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
| 17+       | (see `docs.md` roadmap)    | NOT STARTED |

Next up: **Milestone 17 — Proper ROM format.** See `docs.md` for the exact scope.

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
        video.h/.c     guest-driven CLEAR/SET_PIXEL/PRESENT -> framebuffer (M13)
        audio.h/.c     guest-driven tone regs (FREQ/CTRL), rendered per frame (M15)
    libretro/
        libretro.c     thin adapter: RetroPad <-> arcade1_* <-> frontend;
                       loads content (a guest ROM) as firmware when provided
programs/               guest code compiled by RISC-V GCC (M10+)
    crt0.S              startup: set sp, copy .data, zero .bss, call main, park
    link.ld             linker script: code->ROM, data/bss/stack->RAM
    gfx.h               guest graphics API: gfx_clear/rect/present (M14, guest-only)
    counter.c           docs.md example program (counter = 123)
    mmio_demo.c         M11 guest: read IN register, write OUT register
    video_demo.c        M13 guest: CLEAR + SET_PIXEL + PRESENT over MMIO
    gfx_demo.c          M14 guest: draws only through the gfx.h API
    breakout.c          M15 guest: the first real game (Breakout)
    build/              generated .elf/.bin (gitignored)
deps/libretro/
    libretro.h         vendored MIT-licensed libretro API header
tests/
    test_*.c           per-milestone harnesses (see below)
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
make program      # M10 build guest programs: C -> RV32 ELF -> flat .bin
make test-program # M10 run compiled C on the VM, assert counter == 123
make test-mmio    # M11 guest reads/writes an MMIO device register
make test-timer   # M12 deterministic timer: frames, MMIO regs, CPU lw, reset
make test-video   # M13 guest drives video device -> framebuffer, reset
make test-gfx     # M14 guest draws via gfx_clear/rect/present, exact pixels
make test-breakout # M15 boot Breakout as firmware, scripted game + hashes
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
- Graphics API (M14): `programs/gfx.h` provides `gfx_clear` / `gfx_rect` /
  `gfx_present` as a **guest-only** header (it dereferences fixed MMIO
  addresses — host tests must use the `arcade1_*` API instead). Deliberately
  primitive: one `sw` per pixel, device-side clipping only, and no sprites,
  tilemaps, alpha blending or hardware-accelerated rectangles yet (forbidden
  by docs.md until a later milestone).
- Breakout (M15): full-screen clear + repaint of every rectangle each frame
  (77,836 instructions/frame avg, 80,497 max over 595 frames — the measured
  CPU load of this game, M16 build); no dirty
  rectangles, no text beyond a 3x5 digit font, one level/fixed launch
  trajectory, no title screen. The frame protocol is present-yield with a
  1,000,000-instruction safety cap per frame. Game state (score/lives/state)
  is read from guest RAM by the test via `nm` symbol addresses.
- Guest programs build with `riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32
  -ffreestanding -nostdlib -msmall-data-limit=0` + `programs/link.ld` +
  `crt0.S`. No libc/SDK; `.data` is stored in ROM and copied to RAM, `.bss` is
  zeroed, stack grows down from the top of RAM. Guest image must fit the 4 KiB
  ROM (largest so far: breakout at 1718 bytes). Since M16 the ISA is RV32IM,
  so guest C may use `*` and `/` directly (spec-exact semantics, no traps —
  division by zero and `INT_MIN / -1` return the defined results) with no
  libgcc.
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
