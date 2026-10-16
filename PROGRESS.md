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
| 12+       | (see `docs.md` roadmap)    | NOT STARTED |

Next up: **Milestone 12 — Timer.** See `docs.md` for the exact scope.

## Architecture

```
src/
    arcade1/
        machine.h/.c   machine state + per-frame logic (frontend-independent)
        memory.h/.c    memory/bus: ROM + RAM + MMIO (device handler), 8/16/32-bit r/w
        cpu.h/.c       RV32I decoder (stateless field/immediate/classification)
        exec.h/.c      RV32I interpreter: registers + PC + single-step
    libretro/
        libretro.c     thin adapter: RetroPad <-> arcade1_* <-> frontend
programs/               guest code compiled by RISC-V GCC (M10+)
    crt0.S              startup: set sp, copy .data, zero .bss, call main, park
    link.ld             linker script: code->ROM, data/bss/stack->RAM
    counter.c           docs.md example program (counter = 123)
    mmio_demo.c         M11 guest: read IN register, write OUT register
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
make test-mmio    # M11 guest reads/writes an MMIO device register
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

## Known limitations

- 320x240 @ 60 fps, 48 kHz audio — placeholder resolution/rate, NOT final
  (per roadmap: do not freeze the display resolution yet).
- Memory map (ROM 0x0000+4K, RAM 0x1000+4K, MMIO 0x4000+4K, with a 0x2000
  gap) is a placeholder, not the final ARCADE-1 map. MMIO now routes to a
  host-registered device handler, but there are **no concrete peripherals**
  attached yet (timer is M12, the video device is M13).
- The test pattern, moving square, and beep are host-driven demo logic inside
  the machine; they will be replaced by the RV32 CPU + real devices in
  milestones 08–13.
- The RV32I interpreter (M09) and the guest program flow (M10/M11) work, but a
  compiled program is still executed by a **standalone harness**
  (`make test-program`, `make test-mmio`) — it is **not yet wired into the
  machine's frame loop**. `arcade1_run_frame` still drives the host demo logic
  (square/beep); the machine does not yet *boot* a program as firmware (that
  wiring comes in a later milestone, now that the MMIO infrastructure exists).
- MMIO is minimal by design (M11): one 4 KiB window (0x4000) routed to a single
  host-registered handler, 32-bit accesses only (sub-word lw/sw are no-ops), and
  the machine attaches no device yet. Timer/video attach via the same handler in
  M12/M13.
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
