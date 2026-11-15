# REPORT — Flappy on ARCADE-1 (round3)

A complete Flappy-style timing game for the ARCADE-1 console, built only from
the repository's documentation, headlessly tested, determinism-proven and
golden-locked. Everything lives in `round3/`; no file outside it was touched.

## Deliverables

| File | What it is |
|---|---|
| `flappy.c` | the game, one source file (~180 lines freestanding C) |
| `flappy.arc1` (+ `flappy.syms`) | the built image and its symbol sidecar |
| `input.txt` | scripted session: 520 frames, three flights, crashes, restarts |
| `flappy.golden.txt` | golden per-frame state/framebuffer hashes for that session |
| `flappy_model.py` | exact host-side Python model of the game's integer maths, used to *author* `input.txt` (an autopilot plays it) |
| `verify.sh` | build + determinism + golden check in one script |
| `run_a.txt`, `run_b.txt` | the two byte-identical determinism runs (evidence) |
| `emu_vars.txt`, `model_trace.txt` | per-frame `--vars` trace of the emulator vs the model — 520/520 frames match on `bird_y`, `bird_vy`, `score`, `state` |
| `mid_flight.png`, `dead.png` | captured frames (flight in progress / game-over screen) |
| `session_norm.txt` | the `--record`-normalized canonical form of the session |

## a) Build, run, verify — from a clean shell

```sh
cd /home/luka/Documents/arcade1/round3
ARC=/home/luka/Documents/arcade1/build/arcade1

# build (self-contained: crt0, linker script and SDK headers live in the CLI;
# needs riscv64-unknown-elf-gcc/objcopy/nm on PATH — they are)
$ARC build flappy.c flappy.arc1
# -> arcade1: built flappy.arc1 (+ flappy.syms)

# play the scripted session headlessly (hash line per frame)
$ARC run flappy.arc1 --frames 520 --input input.txt

# watch the game's own globals while it plays
$ARC run flappy.arc1 --frames 520 --input input.txt \
    --vars score,state,bird_y,bird_vy --var-every 40 --quiet

# capture a frame (frame N-1's screen, as documented)
$ARC run flappy.arc1 --frames 150 --input input.txt --capture mid.png

# golden regression (the documented flow: run -> golden, test -> compare)
$ARC test flappy.arc1 --golden flappy.golden.txt --input input.txt --frames 520
# -> arcade1 test: 520 frame lines match flappy.golden.txt

# determinism proof: two identical runs, byte-for-byte
$ARC run flappy.arc1 --frames 520 --input input.txt --strict > a.txt
$ARC run flappy.arc1 --frames 520 --input input.txt --strict > b.txt
diff a.txt b.txt && echo identical

# or everything at once
./verify.sh
```

To regenerate the golden deliberately (only after a behaviour change):

```sh
$ARC run flappy.arc1 --frames 520 --input input.txt | grep '^frame' > flappy.golden.txt
```

## b) Controls and what a session looks like

* **A** — flap (edge-triggered: a press, not a hold). Impulse −2 px/frame,
  gravity +0.25 px/frame², terminal velocity 3.5 px/frame (quarter-pixel fixed
  point internally).
* **A or START** on the game-over screen — restart from the beginning with the
  score reset to 0.
* The bird sits at x = 96 on a 480×270 screen (the console's actual
  framebuffer — see friction item 1). Pipes are 26 px wide, 64 px gap, scroll
  2 px/frame, spaced 150 px. Passing one's right edge beyond the bird scores
  +1. Pipe, ground (y ≥ 250) or ceiling (y ≤ 0) contact ends the run and shows
  `GAME OVER` / `PRESS A` with the score on top.

`input.txt` plays a ~8.7 s session (520 frames @ 60 fps) of real gameplay:

* **Frames 0–279 — flight 1.** Taps every ~14 frames; scores **3**
  (frames 95, 170, 245); the player stops flapping at 250 and the bird hits
  the **ground** at 279. `state` → 1, `GAME OVER` shows.
* **Frame 280 — restart tap.** `score` resets to 0, world regenerates.
* **Frames 281–433 — flight 2.** Scores **1** (frame 376); crashes into a
  **pipe** at 433 while chasing a low gap.
* **Frame 435 — restart tap.**
* **Frames 436–480 — flight 3.** No flaps: the bird falls and hits the ground
  at 480 (a clean free-fall crash), then the game-over screen holds to the
  last frame.

Scoring/crashing/restarting all appear in the golden, and `--vars` traces
confirm score goes 0→3, reset→0→1, reset→0 across the two restarts.

## c) The frank part — friction with the documentation

Everything below was hit by a newcomer doing exactly this task. Where docs and
reality disagreed, I trusted reality and say so.

1. **Screen size: my task briefing said 320×180; the repo uniformly says
   480×270.** `SPEC.md:280` ("Display: 480×270 … 60 Hz") and
   `CHEATSHEET.md:40-41` ("The screen is always 480x270 — there is no scaling
   or video-mode register"). The repo is internally consistent and matches the
   emulator; I built for 480×270. (The 320×180 claim came from outside the
   repo, so this is a briefing error, not a docs error — but a newcomer given
   the wrong number has no way to know which source to trust without reading
   two files plus SPEC.)
2. **The guide's golden-locking flow is `make`-only and assumes you own the
   repo.** `GAME_DEV_GUIDE.md:242-258` ("Test and golden-lock it") tells you
   to add the game to `GOLDEN_GAMES`/`GOLDEN_IMGS` in the Makefile, then run
   `make roms-update` / `make golden-update`. There is **no documented
   CLI-only recipe for authoring a golden**, even though the CLI can do the
   whole thing. I had to assemble it from three separate places:
   `GAME_DEV_GUIDE.md:222` (`arcade1 test game.arc1 --golden …`),
   `SDK.md:257-262` (`--golden` compares "the layout the tests/golden files
   use") and `SDK.md:79-92` (the per-frame hash line format). One sentence in
   SDK.md — "redirect `arcade1 run` output to a file to create a golden" —
   would have removed all doubt.
3. **What counts as a valid golden file is only visible in the CLI source.**
   `tools/arcade1.c:1198-1206`: the compare skips every line that doesn't
   start with `frame `, so comment headers are allowed and the run's trailing
   `ran N frames …` summary line is harmless. The committed repo goldens
   (`tests/golden/*.txt`) contain *only* frame lines, so I grepped `^frame`
   to match convention — but nothing in the docs says the summary line must
   (or need not) be excluded.
4. **Golden shorter than the run's default frame count: fine, but only the
   source says so.** `arcade1 test` defaults to `--frames 600`; against my
   520-line golden it still passes because the compare stops at the golden's
   first pass (`tools/arcade1.c:1193-1206`, "run was shorter than the
   golden's first pass" handles the other direction at
   `tools/arcade1.c:1256-1258`). `SDK.md:257-262` doesn't mention this
   tolerance; I only passed `--frames 520` defensively, then confirmed by
   experiment that the bare command works too.
5. **"The SDK headers travel inside the CLI" doesn't say *which* headers.**
   `GAME_DEV_GUIDE.md:185-187` and `SDK.md:268-272` claim self-containment
   but never enumerate the embedded files. I wanted the opt-in font
   (`CHEATSHEET.md:106-107` documents `#include "text.h"`) and had to open
   `tools/arcade1.c:1517-1526` to confirm `text.h` (and `asset.h`,
   `arcade1_map.h`) are in the embedded set before trusting the build. A list
   in SDK.md would settle it.
6. **Edge-triggered buttons × input scripts: a silent authoring trap.**
   `SDK.md:134-161` documents script semantics perfectly (levels, taps,
   overlap-replaces), but for a flap game you edge-detect presses
   (`pad_prev`), and then a *held* A never flaps twice. My first scripted
   session held A across the restart frame, so the restart ate the edge and
   the next intended flap silently didn't happen — the bird fell out of the
   sky ~40 frames later. `--var-every 1` traces against my host-side model
   (`flappy_model.py`) caught it. Docs are not wrong here, just silent on the
   interaction between button edges and script runs; this is the single
   nastiest way a session can silently diverge from its plan.
7. **There is no one-command "give me a golden" or per-frame pad dump.**
   Expected conveniences that don't exist: an `arcade1 golden`-style command
   (input script + frames → golden file), and any tool that dumps the applied
   per-frame pad alongside hashes (I reconstructed intent from
   `--record` + `--vars` traces instead — workable, more steps).
8. **Positives, for balance — the docs were accurate everywhere I could check
   them.** `arcade1 build` worked first try from a foreign directory and wrote
   exactly the two documented files (`GAME_DEV_GUIDE.md:187-196`);
   `--vars` auto-loaded the sidecar `.syms` with no flags
   (`GAME_DEV_GUIDE.md:195-196`); `--capture` held frame N−1 as documented
   (`SDK.md:28-29`); exit codes matched `SDK.md:56-64`; `--strict` ran clean;
   the record→replay round trip reproduced the golden bit-for-bit
   (`SDK.md:233-238`); and a tampered golden fails with a precise pointer to
   the first differing frame line. The one wrong number I was given
   (320×180) came from my briefing, not from any repo document.

### Verification evidence

* **Determinism:** two independent `--strict` runs of
  `flappy.arc1 --frames 520 --input input.txt` produced byte-identical
  output (`run_a.txt` vs `run_b.txt`, 520 hash lines each, `diff` clean).
* **Golden lock:** `arcade1 test … --golden flappy.golden.txt` →
  `520 frame lines match`; a deliberately tampered golden exits 1 and names
  the differing frame line.
* **Replay round trip:** `--record` normalizes the session
  (`session_norm.txt`); replaying it reproduces the same 520 golden hashes.
* **Game logic:** the Python model of the game's exact integer maths
  (including the seeded LCG) agrees with the emulator on `bird_y`, `bird_vy`,
  `score` and `state` for all 520 frames (`emu_vars.txt` vs
  `model_trace.txt`).
* **Build reproducibility:** rebuilding from source yields a byte-identical
  `.arc1`, so `verify.sh` can rebuild and golden-check in one step.
