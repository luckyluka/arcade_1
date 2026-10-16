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
| 09+       | (see `docs.md` roadmap)    | NOT STARTED |

Next up: **Milestone 09 — RV32I execution (CPU state + stepping).** See
`docs.md` for the exact scope.

## Architecture

```
src/
    arcade1/
        machine.h/.c   machine state + per-frame logic (frontend-independent)
        memory.h/.c    memory/bus: ROM + RAM behind read32/write32
        cpu.h/.c       RV32I decoder (stateless field/immediate/classification)
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
- No RV32 CPU execution yet. M08 added a **decoder only** (classifies a
  32-bit word into fields/immediate/format/op); registers, PC, and stepping
  arrive in M09. `cpu.c` builds into the `.so` but is not yet called by the
  machine (wired up in M09).
- Save states / cheats unsupported (stubs).

## Notes for whoever picks this up

- Follow `docs.md` Rule 1: implement only the current milestone, then stop.
- `FUTURE_IDEAS.md` is a scratch file (not part of a milestone); treat as
  optional input, not a commitment.
- Each milestone has been verified by a deterministic test harness plus a live
  RetroArch 1.18.0 load (RGB565, clean startup/shutdown). Re-run `make
  test-*` after any change.
