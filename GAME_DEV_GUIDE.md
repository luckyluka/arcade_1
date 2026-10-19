# ARCADE-1 Game Development Guide

How to build, run, test and debug a game on ARCADE-1. This is the workflow
document — it deliberately contains no API or CLI reference:

- **CHEATSHEET.md** — the guest API (commands, formats, palette, rules).
- **SDK.md** — the developer CLI (`arcade1 run` and its flags).
- **SPEC.md** — the machine contract (memory map, devices, determinism).
- **PROGRESS.md** — what is actually done and how to verify it.

## The shape of a game

A guest program is freestanding C for RV32IM: `#include "arcade1.h"`, a
`main` with an infinite loop, no libc. One loop iteration is one 60 fps
frame: read the pad, update state, draw, `arcade1_present()`.

Two rules make games testable:

1. **Observable state is a file-scope global** (score, lives, state,
   length, camera position, ...). The tools read those out of guest RAM
   by name via `--vars`; locals are invisible to them. The address comes
   from `game.syms`, never from declaration order — don't assume
   `score` sits at any particular offset.
2. **Everything is deterministic**: no wall-clock, no unseeded random.
   The same image + the same input script always replays bit-identically,
   which is what makes the golden regression and `--record`/`--input`
   meaningful. (See `arcade1_rng` in arcade1.h for a seeded LCG and
   SPEC.md section 10 for the full determinism contract. It is consumed
   strictly in program order — re-balancing when a branch draws random
   numbers re-routes the stream for every later caller — and its low bits
   are weak; for gameplay randomness use `arcade1_rand(n)`, which reads
   the high bits.)

The three `programs/example_*.c` games are the reference implementations
of this shape — start by reading one.

## Build

One command, from any directory, with nothing but the CLI and your source:

    arcade1 build game.c game.arc1

`arcade1 build` is self-contained: the startup code, the linker script and
the SDK headers travel inside the CLI, and the image is packed in process.
It needs `riscv64-unknown-elf-gcc` (and `objcopy`) on PATH — plus `nm` to
write the symbol file (without it the build still succeeds and prints a
warning) — and writes exactly two files: the image and its `<image>.syms`.
The output name must end in `.arc1`; omit it and the source stem is used
(`game.c` → `game.arc1`, in the current directory). The final line lists
what was written: `arcade1: built game.arc1 (+ game.syms)`. It uses the
same flags as the Makefile path below. The
image format is the 16-byte `.arc1` header documented in CHEATSHEET.md.
Because the `.syms` sits beside the image, `--vars` needs no `--syms`:
`arcade1 run game.arc1 --vars score` just works after a build.

Inside this checkout the same guests are built by the Makefile: add the
program to `programs/` and to `GUEST_PROGS`, then `make program` →
`programs/build/<name>.arc1` (+ `.elf`/`.bin`). Either way the build
flags (architecture, freestanding, linker script) are fixed by the build
system — do not hand-roll them; a guest that builds differently may
behave differently.

## Run and iterate

    arcade1 run game.arc1 --frames 600 --quiet
    arcade1 run game.arc1 --frames 120 --capture frame.ppm
    arcade1 run game.arc1 --frames 120 --input script.txt \
        --syms game.syms --vars score,state --var-every 60
    arcade1 test game.arc1 --golden tests/golden/mygame.txt   # hash check

Per-frame hash lines are diffable against `tests/golden/<game>.txt`
(`--quiet` hides them) — `arcade1 test --golden` does that comparison for
you. `--capture` dumps a frame (`.png` or `.ppm` by extension; it holds
the frame after `--frames N`, i.e. `N-1`'s screen); `arcade1 capture`
dumps every frame into `captures/` (`--format png`, `--scale N`);
`--vars` reads your game's globals by name; `--input` plays a scripted
session; `--record` writes one. `arcade1 inspect` runs the game, then
dumps the final PC, all 32 registers and the instruction count.
`--strict` fails the run if any draw read a source address outside
ROM/RAM (see SDK.md for the full flag reference).

## Test and golden-lock it

Give the game a shell test in `tests/test_<name>.sh` in the style of
`tests/test_run.sh`: scripted input, assertions through `--vars` and
captured frames, determinism via two identical runs. Then:

1. Add the game to `GOLDEN_GAMES` (and `GOLDEN_IMGS`) in the Makefile and
   its test to `CHECK_TARGETS`.
2. `make roms-update` — snapshots the built `.arc1` + `nm` dump into
   `tests/roms/`.
3. `make golden-update` — runs the test and records its frame hashes into
   `tests/golden/<name>.txt`.

From then on `make check` re-runs the game against the committed image
and diffs every frame hash; a mismatch means the game's behaviour
changed (a golden change is a behaviour change — commit it deliberately
and say why in the commit message).

## Debugging

Reproduce first: `--record` a session, replay it with `--input`. Then
bisect with `--var-every` traces and `--capture-dir` frame dumps. If a
frame looks wrong, compare its PPM against the golden's; if the state
drifts, compare `--vars` traces. The machine itself is never the first
suspect — check the game's own globals before suspecting the VM.

## Status of the examples

`example_pong`, `example_snake` and `example_color` are first-class
guests: built by `make program`, covered by `make check`, golden-locked.
`experimental/` holds works in progress that are NOT built by default
and NOT covered by tests — move a program there while it is rough, and
promote it (into `GUEST_PROGS` + a test) only when it is meant to last.
