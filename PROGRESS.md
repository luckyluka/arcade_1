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
| 11+       | (see `docs.md` roadmap)    | NOT STARTED |

Next up: **Milestone 11 — MMIO.** See `docs.md` for the exact scope.

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
programs/               guest code compiled by RISC-V GCC (M10+)
    crt0.S              startup: set sp, copy .data, zero .bss, call main, park
    link.ld             linker script: code->ROM, data/bss/stack->RAM
    counter.c           docs.md example program (counter = 123)
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
make              # build arcade1_libretro.so
make test         # M02 test pattern (loads the .so)
make test-input   # M03 RetroPad input
make test-square  # M04 moving square
make test-beep    # M05 audio beep
make test-machine # M06 machine standalone (no libretro/.so)
make test-memory  # M07 memory bus (links ONLY memory.c)
make test-cpu     # M08 RV32I decoder (links ONLY cpu.c)
make test-exec    # M09 RV32I interpreter (links exec+cpu+memory, no .so)
make program      # M10 build guest programs: C -> RV32 ELF -> flat .bin
make test-program # M10 run compiled C on the VM, assert counter == 123
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
- The RV32I interpreter (M09) and the guest program flow (M10) work, but a
  compiled program is still executed by a **standalone harness**
  (`make test-program`) — it is **not yet wired into the machine's frame
  loop**. `arcade1_run_frame` still drives the host demo logic (square/beep);
  making the machine *boot* a program as firmware arrives with MMIO (M11+).
- Guest programs build with `riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32
  -ffreestanding -nostdlib -msmall-data-limit=0` + `programs/link.ld` +
  `crt0.S`. No libc/SDK; `.data` is stored in ROM and copied to RAM, `.bss` is
  zeroed, stack grows down from the top of RAM. Guest image must fit the 4 KiB
  ROM (currently ~112 bytes).
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
  RetroArch 1.18.0 load (RGB565, clean startup/shutdown). Re-run `make
  test-*` after any change.
