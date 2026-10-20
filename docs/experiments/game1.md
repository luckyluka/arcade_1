# Game #1 — ASTEROID RUN through the full flow (Wave 5, ROADMAP_V3 §4.4)

Status: shipped as `programs/experimental/asteroid_run.c` (finished from the
user's rectangle-only example), tested by `make test-asteroid_run` (part of
`make check`), locked as golden game 10 (`tests/golden/asteroid_run.txt`,
reproduced from `tests/golden/conditions.txt`).

The card asks for the TENTH-GAME METRIC: how long does it take to get from an
empty directory to a playable `.arc1`, how many hand-written byte tables did
the game need, and what does `make check` cost. Measured facts, not
adjectives.

## The tenth-game metric

| Fact | Value |
|---|---|
| Empty dir → playable `.arc1` | **0.177 s wall** (`arcade1 build asteroid_run.c`, CLI copied in beside two files: the source + its assetc header) |
| Files the empty dir needed | `arcade1` (the CLI), `asteroid_run.c`, `asteroid_run_sheet.h` |
| Hand-written byte tables in the game | **0** (no `0x..,` arrays in the source: all art is the generator's PNGs through `tools/arc1_assetc.py`; the HUD font ships in `text.h`) |
| Guest SDK the CLI embeds | `crt0.S link.ld arcade1.h gfx.h arcade1_map.h asset.h text.h` — asset.h + text.h joined in this wave so the empty-dir build of a full-flow game resolves `#include "asset.h"` / `"text.h"` (the parked owner decision, forced by the metric) |
| `make check` wall time | **72.51 s** (exit 0, zero `warning:` lines, incl. test-asteroid_run + all 10 goldens) |
| Game payload | 11 112 B payload → 11 128 B `.arc1` image + assets 6520 / 262144 ROM (2.5 %) |

The CLI resolves `#include "asteroid_run_sheet.h"` because quote-includes
search the including file's directory first — the generated header travels
with the game source, exactly like any user-authored file.

## What the finish added (every piece through the shipped pipeline)

| Piece | Pipeline | Where |
|---|---|---|
| Ship, asteroids, comet | Wave 1 assetc, 4-bit sprites (bank 4) | `tools/gen_game1_art.py` (placeholder art, per the owner's decision) → `asteroid_run_sheet.json` → `asteroid_run_sheet.h` |
| 3 player states + asteroid spin | Wave 3 `anim_tick` tables | `idle`/`boost`/`fire`, `spin`; state changes re-anchor with `anim_reset` |
| Scrolling level | Wave 4 map import | `asteroid_run_level.txt` (30×40, incl. the nebula's flipX twin) → `arcade1_tilemap4` under `arcade1_cam(0, scroll)` |
| HUD + terminal overlay | `text.h` | SCORE/LIVES/DIST; the cam drops to (0,0) before glyphs (text.h's world-space contract) |
| Two enemy types | gameplay | asteroids (+100, slow, spin) vs comets (+300, fast, AIMED: spawn lead + drift onto the ship's column) |
| Win / lose / restart | named globals | `score lives state scroll pstate` — state 0 playing, 1 game over, 2 level clear; START restarts from either terminal state |

## Game-design finding: aimed comets (the honest fix for camping)

First cut: asteroids fell anywhere, comets fell anywhere faster. Measured:
a parked ship WON every time (7 parked positions × 920 frames, all reached
`scroll=370` with lives intact — expected hits per run ≈ 0.5 at 16 px of
ship against a 480 px field). Camping a lane was the dominant strategy and
the scripted LOSE half of the narrative was unreachable.

The fix is a genre standard, not a rig: comets now compute a spawn lead —
`x = player_x ± (frames-to-ship-row) · 1px/frame` — and drift onto that
column while falling at 4–5 px/frame. A ship holding its column meets them;
the sweep (4 px/frame) outruns them. Measured after the change: the
hands-off run loses all three lives at frame ~110 of the run (deterministic,
identical after every restart), while the committed sweep script clears the
level with one life to spare. Sitting still is now a strategy that loses —
that is the game working, and it is what makes the WIN/LOSE narrative
provable from input alone.

## The scripted narrative (tests/inputs/asteroid_run.txt, 760 frames)

| Frames | Act | Proven by |
|---|---|---|
| 0–9 | idle: `pstate=0` | vars trace |
| 10–89 | fire in place: `pstate=2`, score starts rising | vars trace |
| 90–369 | sweep R/L/R firing: `pstate=1`, lives 3→1, score → 3000 | vars trace |
| 370 | `state=2` LEVEL CLEAR at `scroll=370` | vars trace + win-overlay capture (panel + cyan edges + text) |
| 380 | START → fresh match (`score=0 lives=3 state=0 scroll=0`) | vars trace |
| 390–489 | hands off → aimed comets land → `state=1` at 490 | vars trace |
| 530 | START → third match | vars trace |
| 640 | the SAME passive death (`lives=0 state=1 scroll=102`) | vars trace — restart leaves no residue |

## Findings from building it

1. **`ANIM()` is an initializer, not decoration.** A file-scope
   `static anim_state x;` is zeroed, and `anim_tick`'s catch-up loop with
   `ms = 0` never terminates: the guest hung inside frame 0 (pc parked in
   `anim_tick`, framebuffer never drawn). Every `anim_state` must carry
   `ANIM(...)` at its declaration; the 12 per-asteroid spin states are all
   listed because a struct-copy assignment makes the freestanding build
   emit `memcpy`, which no libc provides. A `ms <= 0` guard in `anim_tick`
   would be a good future hardening, but asset.h is byte-frozen for
   existing guests this wave.
2. **`cmd_build`'s temp-file array was sized `8` by hand** — adding two
   SDK files tripped `-Warray-bounds`. Now `sizeof sdk_names / sizeof [0]
   + 3`, so a new SDK file cannot outrun it again.
3. **A hold in an input script only covers its own frames** — a later
   entry replaces the state for its window, but frames after that window
   fall back to the earlier entry. The first draft leaked `a right` into
   the restart, silently seeding a different asteroid stream; the final
   script makes every act's window explicit.
4. **Golden-game 10 is real** — `test-golden` reports
   `asteroid_run ok (760 frames)`, and `test-golden-conditions` replays it
   from the manifest row (rom + committed input + frames) on the CLI.

## Reproduce

```
make game1-sheet            # regenerate the placeholder art + level + header
make experimental           # build the game (or: arcade1 build in any dir)
make test-asteroid_run      # the narrative + draw proofs (in make check)
make test-golden            # all 10 goldens, incl. asteroid_run's 760 frames
```

Watch a run: `build/arcade1 run programs/build/asteroid_run.arc1 --frames
760 --input tests/inputs/asteroid_run.txt --syms programs/build/asteroid_run.syms
--vars score,lives,state,scroll,pstate --var-every 10`
