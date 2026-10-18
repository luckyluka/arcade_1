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
   by name via `--vars`; locals are invisible to them.
2. **Everything is deterministic**: no wall-clock, no unseeded random.
   The same image + the same input script always replays bit-identically,
   which is what makes the golden regression and `--record`/`--input`
   meaningful. (See `arcade1_rng` in arcade1.h for a seeded LCG and
   SPEC.md section 10 for the full determinism contract.)

The three `programs/example_*.c` games are the reference implementations
of this shape — start by reading one.

## Build

Add the program to `programs/` and to `GUEST_PROGS` in the Makefile, then
`make program` → `programs/build/<name>.arc1` (+ `.elf`/`.bin`). Needs
`riscv64-unknown-elf-gcc` on PATH. The build flags (architecture,
freestanding, linker script) are set by the Makefile — do not hand-roll
them; a guest that builds differently may behave differently.

## Run and iterate

    arcade1 run programs/build/mygame.arc1 --frames 600 --quiet
    arcade1 run programs/build/mygame.arc1 --frames 120 --capture frame.ppm
    arcade1 run programs/build/mygame.arc1 --frames 120 --input script.txt \
        --syms mygame.syms --vars score,state --var-every 60

Per-frame hash lines are diffable against `tests/golden/<game>.txt`
(`--quiet` hides them). `--capture` dumps PPM frames; `--vars` reads your
game's globals by name; `--input` plays a scripted session; `--record`
writes one. See SDK.md for the full flag reference.

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
