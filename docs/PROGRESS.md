# ARCADE-1 — Progress

What is done and how to verify it. The full historical log — every
milestone in detail, every measurement, every `make check` wall-time row —
is [archive/PROGRESS_LOG.md](archive/PROGRESS_LOG.md). This file is the
condensed current view.

## Status summary

| phase | what | status |
|---|---|---|
| M01–M14 | the machine: Libretro skeleton, video, input, audio, memory, RV32IM, MMIO, timer, devices, gfx API | COMPLETE |
| M15–M22 | Breakout, RV32M, `.arc1` format, Tetris, sprite/tilemap/scroll engines, Platformer | COMPLETE |
| M23–M24 | architecture review → **SPEC.md v0.1** (the machine contract) | COMPLETE |
| M25–M27 | machine v0.2: 480×270 screen, memory map (256 KiB ROM / 64 KiB RAM), colour (palette + SPRITE4/TILEMAP4) | COMPLETE |
| M28 | AI SDK v0.1 — headless `arcade1 run`, scripted input, per-frame hashes, `build/test/golden/inspect/info`, record/replay, named variables | COMPLETE |
| M29 | the fresh-agent SDK round ("Breakout-lite") and its doc-gap findings — `docs/m29/` | landed (record) |
| Wave 7a/7/8 | fixes + hygiene, the AI art pipeline, audio observability (cards 4.1–4.9, 7.1–7.8, 8.1–8.3) | COMPLETE — see [ROADMAP.md](ROADMAP.md) |
| ArcForge | M0 (spec + reference implementation), X0 (variable-size core), X1 (language extensions) | COMPLETE — see [../arcforge/docs/ROADMAP.md](../arcforge/docs/ROADMAP.md) |
| Wave 9 | freeze-readiness dossier | NOT STARTED |
| Wave 10 | ARCADE-1 on FPGA (RTL + board) | NOT STARTED |

## Shipped games (the regression)

**17 committed ROMs** (`tests/roms/*.arc1`) and **18 golden session files**
(`tests/golden/*.txt`): Breakout, Tetris, Platformer, Asteroid Run,
Flappy, Petal Blade (+ the rig variant), TEMPEST (`shmup.c`), Brickstorm,
Sky Descent, Neon Tap, m29game, plus the demo/example sessions (pong,
snake, colour, scroll, conditions).

A golden is the game's observable behaviour: per-frame hashes of a
scripted session. They change only when a game's behaviour changes —
deliberately, in the commit that says why.

## Verification

```
make              # build arcade1_libretro.so (desktop, x86-64 Linux)
make android      # cross-build the same core for Android (NDK)
make check        # every test target + the golden regression
make test-<name>  # one target
```

All builds use `-Wall -Wextra` and must stay warning-free.

### `make check` wall time

**Budget: 500 s** (owner's decision 2026-10-26; raised from 300 s, before
that 120 s). If a new test would push the total over, **report it before
adding the test**. Recent measurements (full table in the archive log):

| date | what | `make clean && make check` |
|---|---|---|
| 2026-11-14 | the repository cleanup (146 file moves + path fixes; no code, no test changes) | 456.61 s — **inside the 500 s budget**, rc=0, zero warnings |
| 2026-11-13 | X1 commit 1 (arcforge schemas/oracle/fixtures; no root test, no root code) | 599.87 s — over the line on the loaded evening desktop (load avg 2.8), same starved-box pattern; rc=0, zero warnings |
| 2026-11-09 | Wave 10 roadmap card (docs only) | 479 s — inside the budget |
| 2026-11-07 | Audio-v2 decision brief (docs only) | 509 s — loaded desktop (load avg 2.2–2.5) |
| 2026-11-06 | WAV dump (card 8.1; new test ~4 s) | 443 s — 57 s of headroom |

The suite itself is stable; the over-500 s rows are machine load, not
suite growth (the A/B rows in the archive log carry the evidence: the same
HEAD measured 631 s loaded against a 443 s idle baseline).

## Known limitations

Display is 480×270 @ 60 fps (guests still draw in the top-left 320×240
region), 48 kHz audio; the M extension is interpreter-only; FENCE/ECALL/
EBREAK execute as NOP; no save states. The full list, with the reasoning,
is in [archive/PROGRESS_LOG.md](archive/PROGRESS_LOG.md) §Known
limitations.

## Notes for whoever picks this up

The working rules live in [../AGENTS.md](../AGENTS.md); the forward plan
is [ROADMAP.md](ROADMAP.md); uncommitted ideas are in
[FUTURE_IDEAS.md](FUTURE_IDEAS.md). Every milestone was verified by a
deterministic test harness plus a live RetroArch load — re-run
`make test-*` after any change.
