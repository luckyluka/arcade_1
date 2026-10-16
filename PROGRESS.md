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
| 10+       | (see `docs.md` roadmap)    | NOT STARTED |

Next up: **Milestone 10 — Load compiled C program.** See `docs.md` for the
exact scope (RISC-V GCC → RV32 executable → ARCADE-1 VM).

## Architecture

```
src/
    arcade1/
        machine.h/.c   machine state + per-frame logic (frontend-independent)
        memory.h/.c    memory/bus: ROM + RAM, 8/16/32-bit read/write
        cpu.h/.c       RV32I decoder (stateless field/immediate/classification)
        exec.h/.c      RV32I interpreter: registers + PC + single-step
    libretro/
        libretro.c     thin adapter: RetroPad <-> arcade1_* <-> frontend
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
make              # build arcade1_libretro.so
make test         # M02 test pattern (loads the .so)
make test-input   # M03 RetroPad input
make test-square  # M04 moving square
make test-beep    # M05 audio beep
make test-machine # M06 machine standalone (no libretro/.so)
make test-memory  # M07 memory bus (links ONLY memory.c)
make test-cpu     # M08 RV32I decoder (links ONLY cpu.c)
make test-exec    # M09 RV32I interpreter (links exec+cpu+memory, no .so)
```

All builds use `-Wall -Wextra` and must stay warning-free.

Test strategy (per `docs.md` testing philosophy): milestone tests link the real
`.so` and drive it through the frontend callbacks; the machine and memory tests
link the sources directly with no libretro at all, proving frontend
independence. Pixel/audio values are verified deterministically (e.g. the
square is a unique orange `0xFD00` so tests can locate it by scanning).

## Known limitations

- 320x240 @ 60 fps, 48 kHz audio — placeholder resolution/rate, NOT final
  (per roadmap: do not freeze the display resolution yet).
- Memory map (ROM 0x0000+4K, RAM 0x1000+4K) is a placeholder, not the final
  ARCADE-1 map. No MMIO/peripherals yet.
- The test pattern, moving square, and beep are host-driven demo logic inside
  the machine; they will be replaced by the RV32 CPU + real devices in
  milestones 08–13.
- The RV32I **interpreter** (M09) exists and is instruction-tested, but is
  **not yet wired into the machine's frame loop** — there is no program to run
  until M10 loads one. `exec.c` builds into the `.so` but `arcade1_run_frame`
  still drives the host-side demo logic (square/beep) directly.
- FENCE/ECALL/EBREAK currently execute as **NOP** (PC advances); there is no
  OS/syscall/halt handling yet. Unknown words also act as NOP.
- Save states / cheats unsupported (stubs).

## Notes for whoever picks this up

- Follow `docs.md` Rule 1: implement only the current milestone, then stop.
- `FUTURE_IDEAS.md` is a scratch file (not part of a milestone); treat as
  optional input, not a commitment.
- Each milestone has been verified by a deterministic test harness plus a live
  RetroArch 1.18.0 load (RGB565, clean startup/shutdown). Re-run `make
  test-*` after any change.
