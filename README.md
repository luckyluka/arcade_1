# ARCADE-1

A fictional 16-bit console and everything around it: the machine, its
Libretro emulator core, the guest SDK, and the tools that build and test
games on it. Everything is deterministic — same inputs, same hashes.

## What is the machine?

A RISC-V (RV32IM) console: 480×270 @ 60 fps video, 48 kHz audio, 256 KiB
ROM / 64 KiB RAM / 4 KiB MMIO. The contract — memory map, devices, frame
protocol, determinism — is [docs/SPEC.md](docs/SPEC.md); why it is designed
that way is [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). When a document
and the CLI disagree, `arcade1 info` is right.

## What is ArcForge?

A bounded visual language an AI can program, plus a deterministic compiler
that turns strict semantic JSON into pixel-art assets for ARCADE-1 —
archetypes, recipes, styles, `.px` fragments, golden-tested pixels. It is
a first-class subsystem under [arcforge/](arcforge/README.md), with its own
[docs](arcforge/docs/) and [roadmap](arcforge/docs/ROADMAP.md).

## Build, run, test

```sh
make                 # build arcade1_libretro.so (desktop x86-64 Linux)
make android         # cross-build the same core for Android (NDK)
make build/arcade1   # build the headless CLI (run/build/test/golden/…)

retroarch -L ./arcade1_libretro.so tests/roms/breakout.arc1   # play
./build/arcade1 run tests/roms/breakout.arc1 --frames 600     # headless

make check           # the whole regression (every test + the goldens)
make test-<name>     # one target
```

`make check` has a **500 s budget** and must stay warning-free — see
[docs/PROGRESS.md](docs/PROGRESS.md) for recent wall times.

## Build a game

Start at [docs/GAME_DEV_GUIDE.md](docs/GAME_DEV_GUIDE.md): write a
freestanding C guest (`#include "arcade1.h"`, `main()` + frame loop),
`arcade1 build game.c game.arc1`, then script a session and golden-lock
it. The guest API is [docs/CHEATSHEET.md](docs/CHEATSHEET.md); the CLI is
[docs/SDK.md](docs/SDK.md). Art assets go through
[art/README.md](art/README.md) (and ArcForge for the bounded vocabulary).

## Where things live

| path | what |
|---|---|
| `src/` | the machine (`arcade1/`) + the Libretro adapter (`libretro/`) |
| `programs/` | the guests (games, demos) + the guest SDK headers |
| `tools/` | the `arcade1` CLI sources, the asset compiler, art generators |
| `tests/` | the suite; the regression lives in `tests/golden/` + `tests/roms/` |
| `arcforge/` | ArcForge: spec, reference compiler, registry, its own docs |
| `art/` | the procedural art pipeline (library, rig, QA) |
| `docs/` | current documentation (below) |

## Documentation map

Canonical, in authority order — read the [AGENTS.md](AGENTS.md) rules
before changing anything:

1. [docs/SPEC.md](docs/SPEC.md) — the machine contract
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — why the machine is this way
3. [docs/SDK.md](docs/SDK.md) — the developer CLI
4. [docs/GAME_DEV_GUIDE.md](docs/GAME_DEV_GUIDE.md) — how to create games
5. [docs/CHEATSHEET.md](docs/CHEATSHEET.md) — quick guest API reference
6. `arcforge/docs/` — how assets are authored (ArcForge)
7. [docs/ROADMAP.md](docs/ROADMAP.md) — what comes next
8. [docs/PROGRESS.md](docs/PROGRESS.md) — where the project is now
9. [docs/experiments/](docs/experiments/) — what was explored and learned
10. [docs/archive/](docs/archive/) — historical material (never supersedes
    the docs above)

## What is experimental?

[docs/experiments/](docs/experiments/) holds findings: measurements,
retractions, round reports. Ideas that are not yet planned work live in
[docs/FUTURE_IDEAS.md](docs/FUTURE_IDEAS.md).
