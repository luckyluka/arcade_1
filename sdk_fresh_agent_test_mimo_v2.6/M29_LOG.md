# M29 log — building "Breakout-lite" with the ARCADE-1 SDK

Task: build `game.c` (paddle, ball, 3 rows x 10 bricks, 3 lives, score,
game-over, START to restart) using at least one SPRITE4 and one TILEMAP4 with
a palette I define; prove it works headlessly with scripted input, named
variable reads and frame captures.

Docs read first (only files used): `SDK.md`, `CHEATSHEET.md`,
`GAME_DEV_GUIDE.md`, `include/{arcade1.h,gfx.h,arcade1_map.h,crt0.S,link.ld}`,
plus the tool `./arcade1` itself (usage strings / disassembly of the tool I was
given, when the docs did not say what I needed).

---

## Environment / first contact

| # | Command | Result |
|---|---------|--------|
| 1 | `ls` | Only `arcade1`, `SDK.md`, `CHEATSHEET.md`, `GAME_DEV_GUIDE.md`, `include/`. No `programs/`, no `build/`, **no Makefile**. |
| 2 | `./arcade1` | usage: `run capture test build inspect` (exit 0) |
| 3 | `./arcade1 build` | `usage: arcade1 build <program.c> [output.arc1]` (exit 2) |
| 4 | `./arcade1 capture` / `test` / `inspect` | print **run's** usage (exit 2) |

**DOC GAP 1** — `GAME_DEV_GUIDE.md` says the way to build a guest is
"add it to `programs/` + `GUEST_PROGS` in the Makefile, then `make program`".
There is no Makefile in the working set at all, and the task's build entry
point is `./arcade1 build game.c game.arc1`. The guide's build section is
unusable as delivered.

**DOC GAP 2** — `SDK.md` "Remaining M28 deliverables" says the full
`arcade1 build/test/capture` command set is *not built yet*, but the binary
does have `build`, `capture`, `test`, `inspect`. Doc is stale in the opposite
direction from GAP 1.

**DOC WRONG 1** — `arcade1 capture` and `arcade1 test` are advertised as their
own commands ("dump frames to a directory", "run a game (smoke; optional
golden compare)") but in the binary they are pure aliases of `run` (main
dispatches all three to `cmd_run` with no extra flags). Only `inspect` adds
anything (it appends the **undocumented** `--inspect` flag, which prints
`inspect: frame=N pc=0x... instret=N`). `--inspect` appears in no doc file.

## Discovering what `arcade1 build` actually needs

Attempt 1 — `./arcade1 build probe1.c probe1.arc1` with a trivial
`int main(void){for(;;){}}`:

```
cc1: fatal error: programs/crt0.S: No such file or directory
```
(exit 1)

The `build` subcommand shells out (from the tool's own strings):

```
mkdir -p programs/build && riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32
  -Os -ffreestanding -nostdlib -nostartfiles -T programs/link.ld
  -o <name>.elf programs/crt0.S <src>.c
  && riscv64-unknown-elf-objcopy -O binary <name>.elf <name>.bin
  && build/packarc1 <name>.bin <out>.arc1
```

So the CLI hard-codes three paths that do **not** exist in the working set:

* `programs/crt0.S` (only `include/crt0.S` shipped)
* `programs/link.ld` (only `include/link.ld` shipped)
* `build/packarc1` (a packer helper, not shipped at all)

**DOC GAP 3** — nothing in SDK.md / CHEATSHEET.md / GAME_DEV_GUIDE.md mentions
`packarc1` or the `.arc1` image header layout. SDK.md only says the image is
"validated: magic, version, size, CRC-32" without a byte layout, so nobody can
recreate the packer from the docs.

Fix (my choice, docs untouched): copy the shipped crt0/linker script into the
paths the CLI expects, and implement the missing `build/packarc1`.

| # | Command | Result |
|---|---------|--------|
| 5 | `cp include/crt0.S programs/crt0.S; cp include/link.ld programs/link.ld` then `./arcade1 build probe1.c probe1.arc1` | `sh: 1: build/packarc1: not found` (exit 1) |

To write the packer I derived the header layout from the tool binary itself
(`arcade1_image_parse` / `arcade1_image_header` in `./arcade1`), since the docs
do not state it:

```
offset 0x00  char[4] magic "ARC1"
offset 0x04  u32     format version = 1
offset 0x08  u32     payload size in bytes (1..262144)
offset 0x0C  u32     CRC-32 of payload (standard zlib CRC-32)
offset 0x10  payload (the flat .bin)
```
file size must be exactly `0x10 + payload_size`. Error strings in the tool
(`image too short for a header`, `bad magic (not an ARC1 image)`,
`unsupported format version`, `payload size invalid or mismatched`,
`payload checksum mismatch`) match this layout and the returned error codes 1..5.

**DOC GAP 4** — `SDK.md` says the CLI takes `-I`-free gcc flags? No: the build
line has **no `-I` include path**, so `#include "arcade1.h"` (the form every
doc example uses) does not compile with `arcade1 build`. My choice: write
`#include "include/arcade1.h"` in `game.c` (quoted include resolves relative to
the source file; `arcade1.h` then pulls `gfx.h`/`arcade1_map.h` from the same
directory). Every API example in CHEATSHEET.md would fail as written under
`arcade1 build` — that is a doc/API mismatch, not something I could fix in the
docs.

---

## Phase 2 — the first guest that actually runs (attempts 6–11)

Attempt 6 — `./arcade1 build game.c game.arc1` → **exit 0**, 3132-byte image,
`./arcade1 run game.arc1 --frames 60` → the VM ran but nothing on screen moved
and the frame counter advanced while the game loop spun ~255 times per frame.

**DOC WRONG 2** — nothing in `SDK.md` / `CHEATSHEET.md` / `GAME_DEV_GUIDE.md`
says what ends a frame. Reading the tool (`arcade1_run_frame` +
`arcade1_video_mmio_write`, CMD 3 = PRESENT): **one frame is "guest execution
until the next write to the CMD register with value PRESENT"**. A guest that
never calls `arcade1_present()` burns its whole per-frame instruction budget in
frame 0 and the machine looks frozen. `probe5` (a counter + one `present()` per
loop) confirmed exactly one loop iteration per frame once `present()` is
called. My choice: `arcade1_present()` at the end of `draw()`, every frame.

Attempt 7 — rebuilt with `present()`: 1 iteration/frame, ball and paddle moved,
HUD updated. `riscv64-unknown-elf-nm programs/build/game.elf > game.syms` and
`--syms game.syms --vars score,lives,state --var-every 100` printed the named
globals by name (`vars 100 score=10 lives=3 state=0`).

Attempt 8 — a hand-written "hold RIGHT" input script: the paddle parked in the
corner and the ball never touched a brick. Wrote `gen_script.py`: it runs the
game once with `--var-every 1` (globals only), simulates the paddle in
Python from the traced `ball_x/ball_y/paddle_x`, and emits a button script
(`chase` = stand under the ball, `dodge` = run away).

Attempts 9–11 — four `chase` script revisions (`scripts/chase1..5.txt`,
`follow1..4.txt`). Two real game bugs fell out of watching them:

| # | Observation | Fix |
|---|-------------|-----|
| 9 | ball that hit the paddle *stuck to it* and travelled down with it | paddle bounce used `speed = -ball_vy`; the ball was already moving down, so it must be `+ball_vy` |
| 10 | ball could escape through the empty side corridors beside the brick wall | bricks are wall-to-wall (10 x 48 px = the full 480 px), no side gap |
| 11 | ball served already touching the paddle, so a parked paddle caught it instantly | `serve_ball()` launches from above the paddle heading away from it |

## Phase 3 — scripted proof (attempts 12–16)

| # | Command | Result |
|---|---------|--------|
| 12 | `run game.arc1 --frames 1800 --input scripts/score.txt --syms game.syms --vars score,lives,state --var-every 100` | **(a) score climbs 0→130** (`0:0/3/0  100:10/3/0  300:20/3/0  600:50/3/0  1000:100/3/0  1400:130/1/0`) |
| 13 | same with `scripts/dodge.txt` | **(b) lives 3→0 and `state=1`** by frame 600 (`300:20/2/0  400:20/1/0  600:30/0/1`) |
| 14 | same with `scripts/restart.txt` (= dodge + `650 655 start` + `660 1000 none`) | **(c) START restarts**: `state=1` at 575–625, then `650: score=0 lives=3 state=0`, and play resumes (`750: score=10/3/0`) |
| 15 | `run game.arc1 --frames 30 --capture frame.ppm` + pixel scan | colour frame captured, **but only ~6 of 10 brick columns were on screen** |
| 16 | re-read `include/gfx.h` cell format (`[15:0]` tile index, `[19:16]` bank) | suspected the emulator; started probing TILEMAP4 in isolation |

## Phase 4 — the TILEMAP4 brick-field bug (attempts 17–34)

### 4.1 Probing TILEMAP4 in isolation

`probe6..probe10.c` + `sweep.sh` (one `arcade1 build`+`run` per geometry,
frame captured to a PPM, then measured with `sum.py`/`col.py`). Each probe
pokes the same registers `gfx_tilemap4()` writes, so the header cannot hide a
register mismatch. Measured with 16x8 tiles at (0,20), one shared tile, cell
bank cycling per column:

| # | COLS x ROWS | Result |
|---|-------------|--------|
| 17 | 10 x 3, tile 48x12 | full field, 10 cols x 3 rows, 48x12 cells — the geometry my game uses |
| 18 | 8 x 1/2/3/4 | all rows, all columns correct |
| 19 | 2..5 x 1..4 | **nothing drawn at all** (not one pixel) |
| 20 | 6..12 x 1..4 | all correct |
| 21 | 5 x 5, 4 x 6, 3 x 6, 2 x 7, 1 x 9 | correct (so it is not a "COLS >= 6" rule) |
| 22 | 5 x 4, 4 x 5, 3 x 5, 2 x 6, 1 x 8 | nothing |
| 23 | position sweep (x 0/100/200/400, y 0/100/200) | position-independent |
| 24 | tile size sweep 2x8 … 48x12, and the 1-bit `arcade1_tilemap()` at 5 x 2 | tile W/H are honoured; the 1-bit path has no such floor |

Minimum COLS that draws, per ROWS (measured, R = 1..9):

```
ROWS   1  2  3  4  5  6  7  8  9
minCOLS 6  6  6  6  5  3  2  2  1
```

**DOC/API WRONG 3** — `gfx.h` documents no minimum geometry, but TILEMAP4
silently draws **nothing** for small maps (e.g. 4x3, 5x1, 1x8). The predicate
is not derivable from the header; `COLS >= 6` with any `ROWS >= 1` is always
safe, and that is what the game uses (10 x 3).

### 4.2 Ground truth from the tool binary

`objdump -d ./arcade1` + a jump-table decoder (`jtab.py`) — the two tables at
0x6fc4 (register writes) and 0x70d8 (commands) decode cleanly:

| mmio | field | dev offset |
|------|-------|-----------|
| 0x100 / 0x104 / 0x108 / 0x10C | X / Y / COLOR / CMD (dispatch) | 0x18 / 0x1c / 0x20 / 0x24 |
| 0x114 / 0x118 / 0x11C | ADDR / W / H | 0x28 / 0x2c / 0x30 |
| 0x120 / 0x124 / 0x128 / 0x12C | MAP_ADDR / TILE_ADDR / COLS / ROWS | 0x34 / 0x38 / 0x3c / 0x40 |
| 0x130 / 0x134 | CAM_X / CAM_Y | 0x44 / … |
| 0x138 / 0x13C / 0x140 / 0x144 | PAL_INDEX / PAL_DATA / PAL_BANK / FLAGS | 0x248 / 0x48+i*2 / 0x249 / 0x24a |

`include/gfx.h` matches this map exactly — **the header is right**. CMD 8
(0x4b90) is TILEMAP4 and it too matches the documented semantics: it requires
W, H, COLS and ROWS all non-zero, computes the tile stride as `(W/2)*H`,
resolves tile N as `tiles + (N-1)*stride`, skips tile index 0, and honours each
cell's bank/flip bits.

The one hard rule the disassembly does show (`video_blit_4bpp`, 0x477b): a
pixel whose source byte address is outside `0x00000..0x3ffff` and outside the
ROM window `0x20000000..0x2000ffff` is **silently skipped** — no error, no
diagnostic, no draw.

### 4.3 The bug was mine, and the fix

`fill_bricks()` gave every column a *different tile index* (1..10) while only
**one** tile of pixel data existed. Every cell with index >= 2 therefore
resolved past the end of the tile blob into zeroed `.bss`, whose nibbles are 0
= transparent — i.e. invisible. That is exactly the "6 of 10 columns" picture.

Attempt 33 — fix (docs untouched, `game.c` only):

```c
#define BRICK_TILE_INDEX 1u      /* one shared tile; the bank picks the colour */
cells[r*BRICK_COLS+c] = BRICK_TILE_INDEX | ((unsigned)(BANK_BRICK0 + r) << 16);
```

| # | Command | Result |
|---|---------|--------|
| 34 | rebuild + `--capture frames/frame_0030.ppm` + scan | brick rows y 28..39 / 40..51 / 52..63, **x 0..479, 10/10 columns in all three rows**, 48 px seam pattern visible per brick, ball 8x8 at (274,166), paddle 56x10 at (212,246), HUD text at y 6..15 |

## Re-verification after the fix (attempts 35–39)

| # | Command | Result |
|---|---------|--------|
| 35 | `run game.arc1 --frames 1800 --input scripts/score.txt … --var-every 100` | **(a)** score 0→130, lives 3→1 over the run |
| 36 | same with `scripts/dodge.txt` | **(b)** `600: score=30 lives=0 state=1` |
| 37 | same with `scripts/restart.txt` | **(c)** `600: 30/0/1` → `650: 0/3/0` → `750: 10/3/0` |
| 38 | `run … --capture frame.ppm` (frame 40) + pixel scan | **(d)** full brick field + paddle + ball in colour |
| 39 | `run … --frames 101 --capture-every 100` + per-cell scan | live tilemap: at frame 100 (score=10) row 2 column 7 is empty while the other 29 bricks are still drawn |

## Remaining DOC GAPs / API notes

| id | what | impact | my choice |
|----|------|--------|-----------|
| GAP 1 | `GAME_DEV_GUIDE.md` build instructions (Makefile + `programs/`) | build entry point unusable | used `arcade1 build` |
| GAP 2 | `SDK.md` says build/test/capture "not built yet" | stale | used them anyway |
| GAP 3 | no doc for `packarc1` or the `.arc1` header layout | cannot build at all | wrote `build/packarc1`, layout derived from the binary |
| GAP 4 | build passes no `-I` | every doc example fails to compile | `#include "include/arcade1.h"` |
| WRONG 1 | `capture`/`test` are aliases of `run`; `--inspect` undocumented | misleading | used `run` |
| WRONG 2 | "what ends a frame" is undocumented | guest looks frozen | `arcade1_present()` every frame |
| WRONG 3 | TILEMAP4 silently draws nothing for small maps / out-of-range tile indices | invisible graphics, no error | COLS >= 6; one tile, index 1 |
| GAP 5 | no doc of the video register map (I had to decode it) | could not tell header from machine | map confirmed to match `gfx.h` |
| GAP 6 | no doc of the RAM/ROM windows the blitter accepts | silent pixel drops | out-of-range source ⇒ pixel skipped |

## Attempt count

**39 logged build/run attempts** (1–5 toolchain discovery, 6–11 getting the
guest to run, 12–16 the scripted proof, 17–34 the TILEMAP4 investigation and
fix, 35–39 re-verification), plus ~140 unlogged probe builds/runs in the
COLS x ROWS sweeps (`sweep.sh` — each one a full `arcade1 build` +
`arcade1 run` + PPM measurement).

## Final summary

| deliverable | state |
|-------------|-------|
| `game.c` | builds clean (`arcade1 build`, exit 0, 3212 bytes), uses SPRITE4 (paddle, ball) + TILEMAP4 (30 bricks) + a palette I define (6 banks x 16 RGB565) |
| globals | `score`, `lives`, `state` (plus `paddle_x`, `ball_*`, `bricks_left`, `frame_no`) are file-scope and readable by name via `--syms` + `--vars` |
| proof (a) score rises | `scripts/score.txt` → 0 → 130 |
| proof (b) lives→0, game over | `scripts/dodge.txt` → `state=1` at frame 600 |
| proof (c) START restarts | `scripts/restart.txt` → `1` → `0/3/0` → play resumes |
| proof (d) colour frame | `frame.ppm` — 30 bricks, paddle, ball, HUD |
| `M29_LOG.md` | this file |
| `build/packarc1`, `gen_script.py`, `sweep.sh`+`sum.py`+`col.py`+`jtab.py`, `probe*.c` | the scaffolding that made the above provable |

| what worked first time | what took retries | why |
|------------------------|-------------------|-----|
| `arcade1 run/capture --input/--syms/--vars/--capture` | the build (`programs/`, `packarc1`, `-I`) | the CLI hard-codes paths the working set does not ship |
| RV32IM mul/div, fixed-point, palette upload, SPRITE4, rects, 1-bit tilemap, camera registers | frame semantics | "a frame ends at PRESENT" is undocumented |
| variable reads by name | paddle bounce, ball escape, serve | my physics bugs, found by scripted runs |
|  | brick-field rendering | **my** cell/tile-index bug, plus an undocumented TILEMAP4 floor that made it look like an SDK bug |

**Recommended doc/API changes:** document that a frame ends at the PRESENT
command; ship or document `packarc1` and the `.arc1` header; add `-Iinclude` to
the build command (or document `#include "include/arcade1.h"`); document the
TILEMAP4 minimum geometry and the out-of-range-tile = silent no-op rule; either
make `capture`/`test` distinct commands or drop them from the usage string;
document `--inspect`.
