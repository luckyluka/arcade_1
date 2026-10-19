# M29_LOG.md — "Coin Runner" on ARCADE-1

Working directory `/home/luka/m29b`. Available inputs were only `arcade1`,
`SDK.md`, `CHEATSHEET.md`, `GAME_DEV_GUIDE.md`, `include/`. No source checkout,
no `Makefile`, no `tests/` tree. Toolchain present on PATH:
`riscv64-unknown-elf-gcc`, `-objcopy`, `-nm` (`/usr/bin`).

I did not disassemble, decompile, read strings from, or modify `./arcade1`, and
I did not touch the docs. Everything below was done by *running* the tool and
reading its documented output.

## Deliverables

| File | What |
|------|------|
| `game.c` | the game (freestanding RV32IM guest) |
| `assets.h` | generated 4-bit sprites/tiles + RGB565 palette |
| `gen_assets.py` | asset generator (writes `assets.h`) |
| `game.arc1`, `game.syms` | built image and symbol dump |
| `inputs/*.txt` | scripted input files |
| `frames/*.ppm` | captured proof frames |
| `run_tests.sh` | build + run every prove-it scenario + capture |
| `tools/mksyms.py` | build the `.syms` (see DOC GAP 1) |
| `tools/ppm.py` | PPM load / diff / crop / ascii / shift |
| `tools/verify.py` | asserts the captured frames really show each claim |

Reproduce everything with `./run_tests.sh` (deterministic; same bytes every
run and every host — verified below).

## Game design decisions (and why)

* **Screen** 480x270, **world** 960x270. Camera follows: `cam_x =
  player_x+8-240`, clamped to `[0, 480]`, written with the camera register
  (`arcade1_cam`) every frame. Camera Y is always 0.
* **Floor** is a `gfx_tilemap4` of 60x17 16x16 tiles (960x272, bottom row
  clips). Cell = tile index 1/2 checkerboard, bank 0. Two floor tiles share one
  contiguous sheet so tile N lives at the documented `tiles + (N-1)*128`.
* **Player** 16x16 4-bit sprite, `PLAYER_SPEED = 3`, declared first of the
  observable globals. Default art is a right-pointing triangle; facing left
  passes `flags = 1` (flip X) to `gfx_sprite4`.
* **10 coins** 8x8, fixed at `x = 96,180,…,852`, `y = 132` (on the player's
  centre row), so one run right collects them all.
* **3 enemies** 12x12 at fixed speeds, bouncing off the world edges:
  `(x=32,y=0,v=(0,8))`, `(120,200,v=(6,0))`, `(640,240,v=(-5,0))`.
* **State codes** (docs do not define these): `0 = PLAY`, `1 = WIN`,
  `2 = GAME OVER`.
* **Respawn**: on an enemy hit the player returns to the start `(32,127)` and
  gets 60 frames of invulnerability. The brief i-frames are an implementation
  choice; a respawn onto a still-overlapping enemy otherwise drains all lives
  in consecutive frames. Not specified by the task.
* **"Fixed speed / 4 directions"**: I move each axis by `PLAYER_SPEED`
  independently, so a diagonal is `sqrt(2)`x faster. The task says "4
  directions" and "fixed speed" without saying which convention; per-axis is
  the simplest reading that still reads cleanly from the D-pad.
* **HUD** is drawn screen-space: the top bar is `arcade1_rect` (never
  scrolled), the hearts/digits/coin are `sprite4` drawn with the camera parked
  at `(0,0)`. Two frames with equal score but different camera have a
  bit-identical HUD band (checked).

---

## DOC GAPs

1. **No documented way to obtain `<game>.syms` from `arcade1 build`.**
   `--vars` requires `--syms`, and SDK.md/CHEATSHEET.md say the names come from
   an `nm` dump that `make roms-update` snapshots — but GAME_DEV_GUIDE.md says
   `arcade1 build` is *"self-contained … leaves nothing behind except the image
   you named"*, i.e. the ELF is not written, and this environment has no
   Makefile/checkout. So the documented syms path is unreachable here.
   **Resolution**: the game exposes an `ARC1_SYMS_HOOK` that (in an
   instrumented build) parks `&score/&lives/&state/&coins_left/&cam_x` in
   callee-saved registers before `present`; `arcade1 inspect` (documented:
   "print … all 32 registers") reads them back and `tools/mksyms.py` writes an
   `nm`-format `.syms`. The five observable globals are declared first, so they
   are the first five bss words and land at `0x20000000 + 4*i`. `--vars`
   confirms the values (`score=0, lives=3, state=0, coins_left=10, cam_x=0` at
   frame 0), and I cross-check with the instrumented registers.
   *This is the single biggest friction point of the SDK for a headless agent
   that has no checkout.*

2. **`arcade1 build <src> <anything>` always writes an `.arc1`; the requested
   extension is silently ignored.** `arcade1 build scratch_min.c try.elf`
   printed success and produced a `.arc1` file named `try.elf`; `nm`/`file`
   then reject it ("file format not recognized"). No warning is emitted. If a
   user believes they got an ELF (the obvious way to then run `nm`), they are
   misled.

3. (Minor) **`--capture FILE` frame semantics are not stated.** I resolved by
   experiment: `--frames N` runs frames `0..N-1` and `--capture` writes the
   framebuffer after the run (frame `N-1`). Evidence: `--frames 71` of a
   rightward run yields `cam_x = 13` (= `32+3*71+8-240`), so the capture is the
   state *after* 71 held frames.

## Places the docs were ambiguous / things that silently did nothing

* **`arcade1 build scratch_min.c` with no output name** silently wrote
  `program.arc1` (documented as `[output.arc1]` optional, but the default name
  is never stated).
* **The instrumented hook silently produced a wrong address once.** In my first
  `mksyms.py` I used one `__asm__("mv sX,%0")` per global; GCC coalesced one
  operand and a later use clobbered `s3`, so `inspect` reported
  `lives -> 0x18`. Nothing errored; the tool happily printed `lives` if I had
  written a bad syms. Fixed by a single asm block with an explicit
  `"s2"…"s6","memory"` clobber list, and by validating addresses against the
  RAM window.
* **A `gfx_tilemap4`/`sprite4` with a bad source address silently paints
  nothing** (CHEATSHEET/SDK say so). I ran every scenario with
  `--strict`; all exited 0 with empty stderr, so no draw read out of ROM/RAM.
* **Cheatsheet says "zero tile index draws nothing"** — trivially true; all my
  cells use index 1/2.
* **Enemy vs floor colours**: my first build used RGB565 red for *both* the
  enemy and the floor marker, so the "normal play" capture had ~hundreds of red
  pixels that were floor, not enemies, and no enemy was even on screen at the
  chosen frame. Not a doc bug, but an easy-to-miss false positive; caught by
  eyeballing the PNG and fixed (enemy = magenta `0xF81F`, marker = light grey).
* **`--vars all` only prints the names in the syms file.** With a hand-written
  5-line syms, `all` lists exactly those five — expected once understood, but
  the name "all" suggests "every RAM global".

---

## Attempt log (every build / run)

| # | Command | Result |
|---|---------|--------|
| 1 | `./arcade1` | usage banner; commands run/capture/test/build/inspect; exit 0 |
| 2 | `./arcade1 build scratch_min.c scratch_min.arc1` | built; exit 0 — **first build = success** |
| 3 | `./arcade1 run scratch_min.arc1 --frames 2` | 2 hash lines + summary; exit 0 — **first run = success** |
| 4 | `./arcade1 build` (no args) | usage; exit 2 |
| 5 | `./arcade1 build scratch_min.c` (no output) | wrote `program.arc1` (undocumented default name) |
| 6 | `./arcade1 build scratch_min.c try.elf` | "built try.elf"; it is an `.arc1`; `nm` rejects → DOC GAP 2 |
| 7 | `./arcade1 run scratch_min.arc1 --frames 2 --syms bad.syms --vars score` | exit 1, `'score' is not a RAM variable (type 'B' @ 0x1234)` — syms parsing works |
| 8 | `./arcade1 build instr.c instr.arc1` + `inspect --frames 1` | recovered `0x20000000/04/08/0C/10` (probe) |
| 9 | `./arcade1 capture scratch_min.arc1 --frames 1 --capture-dir cap` | `frame_0000.ppm`, P6 480x270; exit 0 |
| 10 | `./arcade1 build game.c game.arc1` | built; exit 0 — **game builds first try** |
| 11 | `./arcade1 run game.arc1 --frames 5` | 5 hash lines; exit 0 |
| 12 | `python3 tools/mksyms.py game.c game.syms` (v1) | **failed**: `lives -> 24 outside RAM` (register clobber) |
| 13 | manual instrumented build + `inspect` | confirmed `s3` clobbered (0x18); other four exact |
| 14 | `./run_tests.sh` (v1) | `$5: unbound variable` in the `trace` helper — harness bug |
| 15 | `./run_tests.sh` (v2, fixed trace) | all scenarios ran; 9/9 pixel checks PASS |
| 16 | after enemy-colour + frame fix | re-ran; (added HUD-fixed check) 11/11 PASS |

**Attempts before the first successful build: 1.**
**Attempts before the first successful run: 1.**

### Retry counts that mattered

* Building `game.c`: **1 try**.
* Getting a usable `.syms`: **3 tries** (probe → failing v1 → validated v2).
* `run_tests.sh` to run green: **2 tries** (bash `trace` arg-order bug).
* "Normal play" frame that actually shows an enemy in a distinct colour:
  **2 tries** (colour change + frame choice).
* Unambiguous floor-scroll proof: **2 tries** (32px-periodic floor made the
  shift search ambiguous until I chose a 13px camera delta and printed the
  mismatch table).

---

## Proof runs

All commands run from `/home/luka/m29b`. `run_tests.sh` runs all of these.

### (a) the score rises as coins are collected
`./arcade1 run game.arc1 --frames 400 --input inputs/collect.txt --syms game.syms --vars score,coins_left --var-every 20`

```
vars    0 score=0   coins_left=10
vars   20 score=10  coins_left=9
vars   60 score=20  coins_left=8
vars  100 score=40  coins_left=6
vars  160 score=60  coins_left=4
vars  220 score=80  coins_left=2
vars  280 score=100 coins_left=0
```
Frames `frames/collect_040.ppm` (score 10) and `frames/collect_120.ppm`
(score 40) differ only in the HUD score/coin region (checked by `verify.py`).

### (b) lives reach 0 -> GAME OVER
`./arcade1 run game.arc1 --frames 300 --input inputs/up_death.txt --syms game.syms --vars score,lives,state --var-every 25`

```
vars    0 score=0 lives=3 state=0
vars   25 score=0 lives=2 state=0
vars  150 score=0 lives=1 state=0
vars  200 score=0 lives=0 state=2   <-- GAME OVER
vars  275 score=0 lives=0 state=2
```
`frames/gameover.ppm` shows the dark-red banner + "GAME OVER".

### (c) all 10 coins -> WIN
`./arcade1 run game.arc1 --frames 400 --input inputs/collect.txt --syms game.syms --vars score,lives,state,coins_left,cam_x`

```
var score = 100
var lives = 3
var state = 1        <-- WIN
var coins_left = 0
var cam_x = 480
```
`frames/win.ppm` shows the green-border "WIN" banner.

### (d) START restarts from game over
`./arcade1 run game.arc1 --frames 470 --input inputs/restart.txt --syms game.syms --vars score,lives,state,coins_left --var-every 25`

```
vars  375 score=0 lives=0 state=2 coins_left=10
vars  400 score=0 lives=3 state=0 coins_left=10   <-- START at frame 400
vars  425 score=10 lives=3 state=0 coins_left=9
```
Frame 400 is exactly where `START` was pressed; state goes 2->0 and lives
0->3. `frames/restart_over.ppm` (frame 399, game over) and
`frames/restart_play.ppm` (frame 470, normal play, no banner — checked).

START also restarts from WIN (the task says "either end state"): with
`inputs/restart_from_win.txt` the trace shows `state=1` at frame 400 then
`state=0, score=0, coins_left=10` from frame 401.

### (e) scrolling floor + flipped player

Floor shift — two frames, different player positions/cameras:

| frame | player x | cam_x (from `--vars`) |
|-------|----------|-----------------------|
| `frames/scroll_a.ppm` (frame 5, idle) | 32 | 0 |
| `frames/scroll_b.ppm` (frame 71, right) | 245 | 13 |

`python3 tools/ppm.py shift frames/scroll_a.ppm frames/scroll_b.ppm 16`:
```
floor mismatch(dx=0)  = 1530
floor mismatch(dx=+13) = 0     <-- best in [-16,16]
=> floor scrolled by 13 px between the two frames
```
i.e. the tilemap is *not* at the same screen position, and it lands perfectly at
the camera delta. (`frames/scroll_far.ppm`, frame 220, shows the world scrolled
228 px for a full-scene example.)

Flipped player — frame 60 (moving right) vs frame 61 (moving left), same
camera (cam_x=0):
```
player-shape mask: right vs mirrored-left     = 0 / 256 differ
player-shape mask: right vs un-mirrored-left  = 96 / 256 differ
```
So the left-facing sprite is exactly the mirror of the right-facing one.

### (f) one colour frame of normal play
`frames/normal_play.ppm` (frame 200): cyan player triangle, magenta enemy,
yellow coins, hearts/score/coin-count HUD — 8 distinct colours.

### HUD fixed while the world scrolls
Frames 168 and 182 both have `score=60, coins_left=4` but `cam_x` 307 vs 349.
The 480x18 HUD band is **bit-identical**; the floor under it matches at
`dx=+10` (349-307 mod the 32px tile period) and not at `dx=0`. (`hud_a.ppm`,
`hud_b.ppm`, checked.)

### Determinism
Two identical `collect.txt` runs produce byte-identical per-frame hash lines
(`diff` clean). Also every scenario run with `--strict` exits 0 (no
out-of-window draw).

---

## Final table

### What worked first time

| Thing | Notes |
|-------|-------|
| `arcade1 build game.c game.arc1` | compiled and packed first attempt |
| `arcade1 run game.arc1` | ran first attempt |
| Local `#include "assets.h"` next to the source | resolved by `arcade1 build` |
| `arcade1 capture --capture` / `--capture-dir` | P6 PPM, correct size, first try |
| Input scripts | format worked exactly as documented |
| `--vars` / `--var-every` | names, signed 32-bit values, trace format as documented |
| Camera / tilemap4 / sprite4 / flip / palette | matched the docs; no surprises |
| Determinism | bit-identical repeats, `--strict` clean |

### What took retries, and why

| Thing | Retries | Cause |
|-------|---------|-------|
| `.syms` for `--vars` | 3 | DOC GAP 1: no syms from `arcade1 build`; then a register-clobber bug in my own hook |
| `run_tests.sh` green | 2 | my bash helper had args in the wrong order (unbound `$5`) |
| Normal-play capture showing enemies | 2 | enemy colour equalled the floor marker; chosen frame had no enemy on screen |
| Unambiguous scroll proof | 2 | 32px-periodic floor makes a shift search ambiguous; chose a 13px camera delta |

### What I would change in the docs or API

1. **Emit or document a symbol path for `arcade1 build`.** Add `--syms-out
   FILE` (or write `<output>.syms` beside the image, or honour `-o elf`, or at
   least fail/warn when the output name has a non-`.arc1` extension). Without
   this, `--vars`/`--var-every` are unusable from a checkout-free environment
   — exactly the environment the SDK advertises.
2. **Document `--capture FILE` frame semantics** (`--frames N` => frame `N-1`)
   and the default output name `program.arc1` for `arcade1 build`.
3. **Make `arcade1 build` reject/warn on a non-`.arc1` output name** instead of
   silently writing an opaque file with the requested extension.
4. **Give `--vars all` a clearer name/description** (it is "all names in the
   syms file", not "all RAM globals").
5. **State the memory-map guarantee** that zero-initialised globals begin at
   `0x20000000` in declaration order (I could rely on it only after probing);
   this would let a tool derive addresses without a hidden hook.
6. Minor API ergonomics: there is no `arcade1_set_pixel` wrapper even though
   `GFX_CMD_SET_PIXEL` exists; screen-space drawing is effectively limited to
   `clear`/`rect`, which is why the HUD uses `rect` + a `camera(0,0)` pass.

---

# Game 2 — "Starfall"

Second request: *"Create a second game. It can be anything. But maybe make it
hd res."* Game 2 is **Starfall**, a vertical-scrolling space shooter.

## "HD res" — there is no HD mode (DOC GAP 3)

The panel is fixed at **480x270**, so "HD res" cannot be taken literally:

* `CHEATSHEET.md` line 40: "Everything clips to the 480x270 screen".
* No resolution / scale / mode field exists in `include/` (checked `gfx.h` and
  the rest of the headers).
* `arcade1 run` / `capture` expose no resolution option (usage verified).

I logged it and made the reasonable call: build at the native 480x270 but make
it look as *high-detail* as the panel allows — a **96-star parallax starfield**
of 1-2 px stars in three depth layers, fine bullets, particle explosions and a
full 5x7 font HUD. Without a scaling primitive (there is none in the devices)
there is no way to render a larger virtual frame; downsampling by hand would
cost a `set_pixel` MMIO write per output pixel.

## Design

* Player ship (16x16, 4-bit) at the bottom; 4-way movement; `A` fires
  (cooldown 6 frames).
* 96-star parallax field drawn with `rect` (screen-space, so it never needs a
  camera): speeds 1/2/3 px/frame at three colours = three depths.
* 5 waves (`wave` 1..5); enemies spawn across the width, drift/bounce, and
  shoot. Clearing wave 5 sets `state=1` (WIN). 0 lives sets `state=2`
  (GAME OVER). `START` restarts from either.
* Enemy kill +10 `score`; explosions are 6 rects; 110-frame post-hit
  invulnerability (mercy clear of enemy bullets).
* Observable globals, declared first: `score`, `lives`, `state`, `wave`,
  `ship_x`.

## Deliverables (added)

| File | What |
|------|------|
| `starfall.c` | Game 2 source |
| `starfall_assets.h` | generated sprites + 6-bank palette + A-Z/0-9 font |
| `gen_starfall_assets.py` | asset generator (writes `starfall_assets.h`) |
| `starfall.arc1`, `starfall.syms` | built image and symbol dump |
| `inputs/starfall/*.txt` | scripted inputs (sweep, die, restart_over, restart_win) |
| `frames/sf_*.ppm` | captured proof frames |
| `run_starfall.sh` | build + run every scenario + capture |
| `tools/verify_starfall.py` | asserts the captured frames |

Reproduce with `./run_starfall.sh`. `tools/mksyms.py` was generalised to take
symbol names, and now serves both games.

## DOC GAP 4 — the "globals at the RAM base, in order" assumption is FALSE

This is the important finding of Game 2. My first `--syms` for Starfall used the
"canonical" guess from Game 1 (`0x20000000 + 4*i` in declaration order). It
built, ran, and `--vars` returned **garbage with no error** (`score=1941535053`,
`wave=0`, `ship_x=0`). The true addresses are **not contiguous**:

```
score  = 0x200004C8      lives = 0x20000484      state = 0x20000488
wave   = 0x20000B38      ship_x = 0x2000048C
```

`score` sits 0x44 bytes beyond `lives`, and `wave` is ~1.7 KiB away. The cause
is RISC-V small-data placement plus `.data` shifting `.bss`: Starfall *calls*
`arcade1_rng`, so `arcade1_rng_state=12345` becomes a `.data` object and moves
the base; the globals also land in different `.sbss`/`.bss` buckets. Coin
Runner only *looked* canonical because it never used the RNG and its globals
happened to fall in one small-data run.

Fix: `tools/mksyms.py` now **trusts the instrumented `arcade1 inspect` values**
when they are valid, distinct RAM addresses, and only falls back to the
canonical guess (with a loud warning) if that fails. Every run is then
confirmed against `--vars`, which is what caught the original error. This
retroactively falsifies the advice I logged as "state the memory-map guarantee"
— such a guarantee does **not** hold, which is exactly why a real symbol path is
needed (DOC GAP 1).

## API note — the SDK LCG is weak in its low bits

`arcade1_rng()` is an LCG; `arcade1_rng() % 3` (low bits) skewed the star depths
so badly that ~9 in 10 stars landed in one speed bucket (the "3" layer). Using
high bits — `(arcade1_rng() >> 9) % n` — gives the intended spread
(~32 / ~24 / ~22 per layer in the sampled frame). Worth documenting: modulo on
low bits of an LCG is a classic trap.

## Attempt log (Game 2, every build / run that mattered)

| # | Command | Result |
|---|---------|--------|
| 1 | `python3 gen_starfall_assets.py` | FAIL: packer got `'.'` (transparent); fixed by mapping `.`→0 |
| 2 | `python3 gen_starfall_assets.py` | ok, 13462 bytes |
| 3 | `./arcade1 build starfall.c starfall.arc1` | ok first try |
| 4 | `./arcade1 run ... --vars score,...` (canonical syms) | ran, but **wrong** values (DOC GAP 4) |
| 5 | RAM scan 0x20000000..0x20002000 | located `lives=0x484`, `ship_x=0x48C` |
| 6 | differential scan (idle vs sweep/fire) | located `score=0x4C8`; `wave=0xB38`; `state=0x488` |
| 7 | `--vars` with real addrs | all five correct |
| 8 | `tools/mksyms.py` fix + regenerate | syms now from `inspect` |
| 9 | `run_starfall.sh` first green run | 9/11 checks; 2 failures |
| 10 | fix verify colours (device RGB565 expansion) | 11/11 |
| 11 | RNG high-bit fix (behaviour changed: passive now WINS) | new `die.txt`; 11/11 |

Attempts to first successful build: **1**. The retries after that were all in
*observation* (syms) and *verification* (colour constants, a difficulty shift),
not in the game code or the SDK API.

## Proof runs (Game 2)

### (a) score rises while shooting
```
vars    0 score=0   lives=3 state=0 wave=1
vars  500 score=20  lives=3 state=0 wave=2
vars 1250 score=40  lives=2 state=0 wave=3
vars 2000 score=110 lives=1 state=0 wave=5
vars 2250 score=150 lives=1 state=1 wave=5
```
`sf_play.ppm` (frame 400) vs `sf_score_late.ppm` (frame 2000): the score region
of the HUD differs.

### (b) lives reach 0 -> GAME OVER
`inputs/starfall/die.txt` (climb into the enemy lane, no fire):
```
vars 1600 lives=0 state=2 wave=4
```
`sf_gameover.ppm` shows the dark-red `GAME OVER` banner, no hearts, score 000.

### (c) clear 5 waves -> WIN
`inputs/starfall/sweep.txt`: `score=150 lives=1 state=1 wave=5`.
`sf_win.ppm` shows the green `YOU WIN` banner.

### (d) START restarts
`inputs/starfall/restart_over.txt` = die, then START at frame 2000:
```
vars 1600 state=2 lives=0 wave=4     # game over
vars 2000 lives=3 state=0 wave=1     # START -> fresh game
```
`sf_restart_over.ppm` (frame 1900) has the banner; `sf_restart_play.ppm`
(frame 2200) does not. Restart-from-WIN (`restart_win.txt`) also ends
`state=0 lives=3`.

### (e) parallax starfield + fixed HUD
Two consecutive frames (`sf_anim_a` = 20, `sf_anim_b` = 21), star field only
(sampled region y=13..240, before any enemy spawns). Because the three depths
have three distinct colours, each layer must best-align at its own speed:

```
PASS dark/far  layer scrolls down 1 px per frame   (best dy=1)
PASS light/mid layer scrolls down 2 px per frame   (best dy=2)
PASS white/near layer scrolls down 3 px per frame  (best dy=3)
PASS HUD band identical across the two animated frames
PASS far star layer has many 1px stars (n=32)
```
That is the "high-detail" claim made rigorous: real per-depth motion, HUD
fixed while the field scrolls.

### (f) colour frame of normal play
`sf_normal.ppm` (frame 1500) shows the cyan ship, yellow bullets, a purple/yellow
enemy and the starfield.

### Determinism
Same script twice -> identical per-frame hashes.

## Final table (Game 2)

### What worked first time

| Thing | Notes |
|-------|-------|
| `arcade1 build starfall.c starfall.arc1` | compiled and packed first attempt |
| 4-bit sprites, palette banks | generator + device matched |
| Full 5x7 font HUD via `sprite4` | first try |
| `arcade1_rng` (after the high-bit fix) | deterministic, good spread |
| Wave / win / game-over state machine | first try, no logic bugs |

### What took retries, and why

| Thing | Retries | Cause |
|-------|---------|-------|
| Asset generator | 1 | transparent pixel (`.`) fed to the hex packer |
| `.syms` for Starfall | ~4 | DOC GAP 4: globals are not contiguous; canonical guess silently wrong |
| `verify_starfall.py` colours | 1 | device RGB565->RGB888 expansion (`0x9CF3`->`#9C9D9C`, not `#9C9C9C`) |
| Game-over proof script | 1 | the high-bit RNG fix made the passive run *win*, so a suicide script was needed |
| Star depth spread | 1 | weak low bits of the SDK LCG (`% 3`) |

### What I would change in the docs or API (Game 2 additions)

1. **Add an HD / resolution statement or option.** A one-line "the panel is
   fixed at 480x270; there is no scaling primitive" would have saved a search.
2. **Emit real symbols (DOC GAP 1) and never rely on address layout.** Game 2
   proves the "globals at the base in order" assumption is false; a silent
   wrong-answer from `--vars` is worse than an error.
3. **Document that `--vars` reads raw RAM by address**, so an incorrect syms
   file yields plausible-looking garbage rather than a diagnostic.
4. **Note the LCG's low-bit weakness** in the `arcade1_rng` docs (or provide a
   ranged helper that uses high bits).


---

# Game 3 — "Vectoroids"

## "HD / 1080p" — again impossible on the panel (extends DOC GAP 3)

The user clarified that by "HD" they meant **1080p**. Same conclusion, now
verified harder:

* The video device in `include/gfx.h` exposes exactly
  `X, Y, COLOR, CMD, ADDR, W, H, MAP_ADDR, TILE_ADDR, COLS, ROWS, CAM_X, CAM_Y,
  PAL_INDEX, PAL_DATA, PAL_BANK, FLAGS`. There is **no resolution / scale / zoom
  / video-mode register**.
* `CHEATSHEET.md` still says everything clips to the **480x270** screen; no
  other size appears anywhere in the docs.
* `arcade1 run` / `capture` have no size flag; `--capture` writes the
  framebuffer as a fixed 480x270 PPM.

So a literal 1920x1080 image cannot come out of this machine. **Agreed
compromise (user's choice):** author a native 480x270 game *and* also emit 4x
nearest-neighbour **1920x1080 PNG captures** of every proof frame, via
`tools/upscale.py`. The upscales keep the 1px wireframe crisp instead of blurring
it, which actually suits this game's look. 480x270 x4 = **exactly 1920x1080**.

## Design

**Vectoroids** — an Asteroids-style vector arena shooter. Every object is a 1-bit
wireframe sprite drawn in a flat colour, so the whole game is crisp 1px line art:

* ship triangle, pre-rasterised at **16 rotations** (16x16);
* **4 rock outline shapes x 3 sizes x 16 rotations = 192 rock sprites** (32x32,
  20x20, 12x12);
* 16-entry integer `costab`/`sintab` tables (scaled by 256) for thrust, bullet
  velocity, split angles and heading — no floating point on RV32;
* all positions/velocities are fixed point (shift 8, 1px = 256);
* rocks split big -> 2 medium -> 2 small; a hit costs a life; wrap-around arena;
* clear **3 waves** -> WIN, 0 lives -> GAME OVER, `START` restarts;
* HUD: lives as small cyan marks, `WAVE n`, 4-digit score; sparse starfield.

Observable globals (declared first, readable by name):
`score, lives, state, wave, ship_x`.

## Deliverables (added)

| File | What |
|------|------|
| `vectoroids.c` | the game |
| `vectoroids_assets.h` | generated 1-bit wireframes + sin/cos tables + font |
| `gen_vectoroids_assets.py` | asset generator |
| `vectoroids.arc1`, `vectoroids.syms` | built image and symbols |
| `inputs/vectoroids/*.txt` | scripted inputs |
| `frames/vo_*.ppm` | captured proof frames (480x270) |
| `frames/hd/vo_*.png` | the same frames at 1920x1080 |
| `run_vectoroids.sh` | build + run every prove-it scenario + capture + upscale |
| `tools/upscale.py` | 4x nearest-neighbour PPM -> PNG |
| `tools/verify_vectoroids.py` | asserts every claim from the pixels |

`./run_all.sh` now runs all three games from a clean state; all exit 0.

## DOC GAP 5 — `--vars all` silently caps at 1024 names

While trying to read a 4000-word buffer back out of guest RAM I found that
`--vars all` prints **at most 1024** entries regardless of how many symbols the
syms file defines (2000/4000/5000-name files all printed exactly 1024; a
9000-name file printed 0). Nothing documents this. To page a larger region you
must issue several runs, each with <=1024 names. (`--vars LIST` with 2048 names
*does* work — the cap is specific to the `all` keyword, which is inconsistent.)

## Note — there is no guest console / print device

The MMIO window has no UART/print/trace register. A guest cannot print anything
for debugging; the only read-back an in-guest tool has is *write to RAM and read
it with `--vars`*. This is worth a line in the docs.

## Note — balance tuning is chaotic (RNG is gameplay-dependent)

`arcade1_rng`'s stream is consumed in gameplay order, so any change to rock
counts/speeds re-routes the entire deterministic world. A one-line tweak moved
the same input script from "reaches wave 5" to "dies in wave 3". Balance was
therefore settled by testing whole runs, not by incremental reasoning.

## Attempt log (Game 3, every build / run that mattered)

1. Wrote `gen_vectoroids_assets.py`; generated `vectoroids_assets.h` (120 KB);
   rendered the 16 ship rotations and the rock outlines to a PNG and eyeballed
   them before writing any game code.
2. `arcade1 build vectoroids.c vectoroids.arc1` -> **compiled first try**.
3. `tools/mksyms.py` reported "using inspect addresses (not the canonical
   base+4*i guess)" and produced `score=0x20000CE4, lives=0x20000CE8,
   state=0x20000CEC, wave=0x20000CC4, ship_x=0x20000FAC` — non-contiguous again
   (DOC GAP 4). `--vars` then read back the expected `score=0, lives=3, state=0,
   wave=1, ship_x=240`; `--strict` was clean.
4. First capture (idle, frame 120) rendered correctly: wireframe rocks, cyan
   ship, HUD, starfield, 6 distinct colours.
5. Difficulty problem: parked/spinning ships died fast. Tried many hand scripts
   (`a left`, `a up`, `a up left`, arcs, roaming) — all GAME OVER by wave 1-2.
6. Gentler physics: `THRUST_ACC 20->26`, `MAX_SPEED 900->1200`, slower rocks,
   `INVULN 120->220`, longer-lived bullets.
7. Detour: built an **in-guest autopilot** (`vectoroids_auto.c`) and a recorder
   to *author* a script. Paged all of RAM to find the recorder array (`autopad`
   at `0x20001004`, found as the longest run of A-pressed button masks) and
   discovered the 1024-name `--vars all` cap (DOC GAP 5). Replaying the recorded
   buttons reproduced the autopilot run exactly — but the autopilot also died,
   so the detour was abandoned and removed.
8. Found that a trivially simple script — **hold `up` + `right` + `A`**
   (spin, thrust, spray) — reliably reached wave 5 (score 795 of 840). Because
   of the chaotic tuning (above), reducing wave counts moved it *backwards*, so
   I settled on `MAX_WAVE=3` with `2,2,3` big rocks; the same script then clears
   all three waves -> **WIN**.
9. Removed the autopilot code and files; rebuilt; captures, verifier, upscaler
   and `run_all.sh` all green.

## Proof runs (Game 3)

### (a) the score rises while shooting

`inputs/vectoroids/win.txt` (hold up+right+A), `--var-every 1000`:

```
0 score=0   lives=3   state=0 wave=1
1000 score=90  lives=3
2000 score=115 lives=2
4000 score=225 lives=1 state=0 wave=2
5000 score=300 lives=1 state=0 wave=3
6000 score=410 lives=1
7000 score=420 lives=1 state=1 wave=3
```

Score climbs 0 -> 420, and each cleared wave bumps `wave`. Frames
`frames/vo_play.ppm` (frame 1500) and `frames/vo_score_late.ppm` (frame 6000)
differ in the HUD score region (asserted).

### (b) lives reach 0 -> GAME OVER

`inputs/vectoroids/idle.txt` (do nothing; rocks eventually ram the parked ship):

```
6000 lives=2 ... 8000 lives=1 ... 12000 lives=0 state=2 wave=1
```

`frames/vo_gameover.ppm` (frame 13000) is asserted to contain the dark-red
banner fill (`0x3000` -> `#310000`, >1000 px).

### (c) clear 3 waves -> WIN

`run c 8000 inputs/vectoroids/win.txt`:

```
var score = 420   var lives = 1   var state = 1   var wave = 3
ran 8000 frames  final state hash=0xF47CB84736C3963D  fb hash=0x3366C9E5F8318781
```

`frames/vo_win.ppm` (frame 8000) is asserted to contain the green banner border
(`0x07E0` -> `#00FF00`).

### (d) START restarts from GAME OVER

`inputs/vectoroids/restart_over.txt` (idle into game over, then START),
`--var-every 1000`:

```
11000 lives=0 state=2 wave=1
12000 lives=0 state=2 wave=1
13000 lives=3 state=0 wave=1     <- START at frame 12500 rebooted the run
```

`frames/vo_restart_over.ppm` (frame 12400) has the banner;
`frames/vo_restart_play.ppm` (frame 13000) has neither banner (asserted).

### (e) the ship really rotates (vector rendering)

Angle 0 (pointing right, idle frame 10) vs angle 4 (pointing down, after holding
`right` for 8 frames -> 4 rotation steps):

```
vo_rot0: cyan pixels 31  bbox w=13 h=9    (wide)
vo_rot4: cyan pixels 31  bbox w=9  h=13   (tall)
```

Same ink count, transposed bounding box -> it is the same triangle rotated, not
a redraw. Both asserted.

### (f) colour frame of normal play

`frames/vo_normal.ppm` (frame 6000): exactly **6 distinct colours**
(black, white wireframes, cyan ship, yellow particles, two dim grey star
levels), 212 white outline pixels — i.e. outlines, not fills (asserted).

### Determinism

Running `win.txt` for 600 frames twice produces byte-identical per-frame hash
output (`determinism: OK`).

### 1080p captures

`tools/upscale.py 4 frames/hd frames/vo_*.ppm` writes 1920x1080 PNGs; the
verifier asserts three of them exist at exactly that size.

## Final table (Game 3)

### What worked first time

| Thing | Notes |
|-------|-------|
| `arcade1 build vectoroids.c vectoroids.arc1` | compiled first attempt |
| 1-bit wireframe sprites (ship + 192 rocks) | generator + device matched |
| integer sin/cos tables + fixed-point physics | first try |
| splitting rocks / lives / wave state machine | first try, no logic bugs |
| `--strict` clean on first run | no out-of-range sprite reads |

### What took retries, and why

| Thing | Retries | Cause |
|-------|---------|-------|
| `.syms` for Vectoroids | 1 (auto-handled) | DOC GAP 4 again: non-contiguous globals; `mksyms` used `inspect` |
| Winnable difficulty | many | genuinely hard game + chaotic RNG (see note) |
| In-guest autopilot | ~4 | bad policy; abandoned, simple script won instead |
| Recorder read-back | 2 | DOC GAP 5: `--vars all` caps at 1024 names |

### What I would change in the docs or API (Game 3 additions)

1. **State the panel resolution everywhere, and say there is no 1080p.** A single
   sentence — "output is always 480x270; the only way to get a bigger image is to
   upscale a capture yourself" — would have removed all ambiguity up front.
2. **Document the `--vars all` 1024-name cap** (DOC GAP 5), or remove it; it is
   silent and inconsistent with `--vars LIST`.
3. **Provide a guest debug channel** (a console/print MMIO register). With no way
   to print, authoring an in-guest tool means writing RAM and paging it back.
4. **Say that `arcade1_rng` is consumed in program order**, so gameplay changes
   re-route the RNG stream; this makes balance tuning non-local and surprises
   anyone expecting a fixed level layout.
