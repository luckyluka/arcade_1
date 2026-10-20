# AGENTS.md — how to work in this repository

ARCADE-1 is a fictional 16-bit console: a RISC-V (RV32IM) machine, its
emulator, the guest SDK, and the tools that build and test games on it.
This file is the map: what to read, what to trust, and the rules.

## Reading order

1. **[GAME_DEV_GUIDE.md](GAME_DEV_GUIDE.md)** — the workflow: build, run, test,
   golden-lock. Start here.
2. **[CHEATSHEET.md](CHEATSHEET.md)** — the guest API: commands, formats,
   coordinate spaces, the traps.
3. **[SDK.md](SDK.md)** — the `arcade1` CLI: run, build, test, golden,
   inspect, info; the input-script and golden formats.
4. **[SPEC.md](SPEC.md)** — the machine contract: memory map, devices,
   determinism, the frame protocol.
5. **[PROGRESS.md](PROGRESS.md)** — what is done; **[ROADMAP_V4.md](ROADMAP_V4.md)**
   — the work that is next (§0 binds).

## The trust rule

> **If a brief, a tutorial, a README or a model disagrees with
> `arcade1 info`, `arcade1 info` is right.**

`arcade1 info` prints the machine's constants from the same macros the VM is
compiled with — screen size, memory map, instruction cap, tick rate, SDK
headers. Documents drift; the CLI does not.

## Where things live

| path | what |
|---|---|
| `src/arcade1/`, `src/libretro/` | the machine and its Libretro adapter |
| `programs/` | the guests (games, demos) + the guest SDK headers |
| `tools/` | the `arcade1` CLI, the asset compiler, the art generators |
| `tests/` | the suite, the goldens (`tests/golden/`), the ROMs (`tests/roms/`) |
| `docs/experiments/` | findings: what a round measured or retracted |

**Goldens and ROMs are the regression.** `tests/golden/<game>.txt` holds a
scripted session's per-frame hashes, `tests/roms/<game>.arc1` the image it
came from. They change only when a game's behaviour changes — deliberately, in
the commit that says why.

## The rules

1. **One card = one commit.** The message says what and why.
2. **Run the acceptance commands and read the output.** "Looks right" is not
   done; paste the key output lines into the report.
3. **Before every commit:** `make clean && make check` exits 0 with **zero
   compiler warnings**.
4. **Existing goldens and ROMs are untouchable.** `git diff --stat --
   tests/golden tests/roms` is empty unless the card says "adds new files".
   Never run `make roms-update`/`make golden-update` in a tooling card; a
   new game adds only its own files.
5. **Dependencies:** host `cc` and `python3` **stdlib only** for anything
   `make check` builds or tests — except the optional offline tool in
   ROADMAP_V4 card 7.5 (Pillow; skipped with exit 77 when absent).
6. **No machine behaviour changes.** `src/` may gain host-side diagnostics
   only (ROADMAP_V4 cards 4.4, 8.1), with zero effect on any hash. If a
   card needs more, stop and report.
7. **A card's premise may be wrong.** If a measurement contradicts it, stop,
   write `docs/experiments/<topic>.md`, report. (The V3 min-geometry
   retraction and the V4 stale-header finding are the precedents.)
8. **Findings are deliverables.** Surprises go to `docs/experiments/` in the
   same commit as the fix.
9. **Stop after each wave** and report: cards done, acceptance output,
   surprises, not-done items.  10. **Scope guard.** New ideas go to
   `FUTURE_IDEAS.md`, not the card.
11. **Determinism.** Every generator, converter and encoder is
    byte-deterministic: no wall-clock, no unseeded randomness, no
    dict-order dependence, sorted iteration.

## Verifying

`make check` — the whole regression (every test + the goldens); `make
test-<name>` — one target. `make check` must stay under **120 s** (last
measurement in PROGRESS.md); if a new test would push it over, report first.

A game's observable state must be a **file-scope global** (the tools read it
by name) and everything **deterministic**.
