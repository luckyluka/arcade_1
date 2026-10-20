# ARCADE-1 Game Development Guide

How to build, run, test and debug a game on ARCADE-1. This is the workflow
document — it deliberately contains no API or CLI reference:

- **CHEATSHEET.md** — the guest API (commands, formats, palette, rules).
- **SDK.md** — the developer CLI (`arcade1 run` and its flags).
- **SPEC.md** — the machine contract (memory map, devices, determinism).
- **PROGRESS.md** — what is actually done and how to verify it.

## The trust rule

> **If a brief, a tutorial, a README or a model disagrees with
> `arcade1 info`, `arcade1 info` is right.**

`arcade1 info` prints the machine's constants from the *same macros the VM
is compiled with* — screen size, framebuffer format, memory map, instruction
cap, tick rate, and the SDK headers the CLI carries:

```sh
arcade1 info          # human-readable
arcade1 info --json   # the same facts, machine-readable
```

Screen resolution, ROM/RAM sizes and the instruction cap have changed
during development (a brief once said 320x180, the repo said 480x270).
Documents drift; the CLI does not. If a number you were given disagrees
with `arcade1 info`, the CLI is the current truth and the document is
stale — say so, do not work around it.

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

## Assets via arc1-assetc

Sprite art reaches the ROM only as compiled data, so art is compiled
ahead of the build by `tools/arc1_assetc.py` (python3, stdlib only). The
committed generated header is the build truth and the tool is the master:
regenerate it, never hand-edit.

    python3 tools/arc1_assetc.py --assets art/assets.json --out-dir programs --report

Exit 0 = compiled, 1 = validation error (one teaching line per asset on
stderr), 2 = usage/IO. One `assets.json` per game declares the palettes
and the sprites; images are horizontal strips (image width = frames ×
frame width, even frame width), transparent pixels are alpha 0 or the
declared key colour (`colors[0]`), at most 15 colours plus transparent
per bank:

```json
{
  "format": 1,
  "palettes": {
    "world": { "bank": 3, "colors": ["#000000", "#F8D8A8", "#503000"] }
  },
  "sprites": [
    { "name": "player", "file": "art/player.png", "palette": "world",
      "frames": 6, "quantize": "nearest", "origin": [16, 40] },
    { "name": "spark",  "file": "art/spark.png",  "palette": "auto",
      "bank": 7, "frames": 4, "collision": [2, 2, 12, 12] }
  ]
}
```

`"palette": "auto"` collects the sheet's distinct colours into `bank`
for you; `"quantize": "nearest"` maps off-palette pixels to the closest
palette entry instead of failing (the default `"exact"` fails loudly
with the pixel, the nearest colour and the distance). Two optional
gameplay fields travel with each sprite: `"origin": [x, y]` is the
anchor INSIDE the frame (default `[0, 0]` = top-left; the edge is
allowed, so `[w/2, h]` is the classic feet anchor) and
`"collision": [x, y, w, h]` is a box inside the frame (omit it for
none). Both are validated against the frame's geometry, so a misplaced
anchor fails the build, not the boss fight. `--report` prints
what each asset cost and how far the quantizer strayed — real output of
the manifest above:

    SPRITE player 32x40 x6 = 3840 B
    QUANT player maxerr=515 pixels=5760
    SPRITE spark 16x16 x4 = 512 B
    PALETTE spark bank 7 = 32 B
    PALETTE world bank 3 = 32 B
    ROM assets 4416 / 262144 (1.7 %)

The guest includes the emitted header (`#include "assets.h"`): the
per-sprite defines (`PLAYER_W`, `PLAYER_H`, `PLAYER_FRAMES`,
`PLAYER_STRIDE`), the sheet bytes (`player_sheet`) and the palette
(`world_palette`, uploaded with `gfx_pal_upload` + `gfx_pal_bank`). The
tool re-derives byte-identical output from the same inputs (asserted in
`tests/test_arc1assetc.sh`), so after any art change regenerate the
header — nothing else re-checks it for you.

Each sprite also emits a per-frame ref array,
`static const sprite4_ref player_frames[PLAYER_FRAMES]`, whose type and
draw wrapper live in `programs/asset.h` — include that header FIRST,
the generated one names its type. A ref carries the frame's pixels,
size, palette bank and the origin/collision metadata, so game code
addresses art by frame and draws it with one call that repeats nothing:

    #include "asset.h"        // sprite4_ref + asset_sprite4
    #include "assets.h"       // the generated header

    asset_sprite4(&player_frames[3], x, y, flags);   // flags bit0 = flip X

Placement by the ref's own anchor is then just
`asset_sprite4(&player_frames[f], x - player_frames[f].ox,
                                  y - player_frames[f].oy, flip)` —
no stride arithmetic anywhere (names resolve at build time; nothing on
the target looks art up by string). A working end-to-end example:
`programs/experimental/asset_demo.c`, built by `make experimental` and
pinned pixel-for-pixel by `make test-assetdemo`.

`"animations"` (top-level, referencing sprites by name) turns a list of
frames into a played resource:

```json
"animations": [
  { "name": "run", "sprite": "player", "frames": [0,1,2,3,4,5], "ms": 80 }
]
```

Each emits `#define RUN_MS`, `#define RUN_STEPS` and a frame table
`static const sprite4_ref *const run_frames[RUN_STEPS]` — refs into the
sprite's frames, in play order. The game PLAYS a table with
`anim_tick()` from `programs/asset.h`: an `anim_state` names the table,
the step length and the mode — `ANIM_LOOP` wraps, `ANIM_PINGPONG` runs
forward and back without repeating the turnarounds, `ANIM_ONCE` stops on
the last step and sets `done`. Time comes from the frame timer
(`arcade1_timer_count()`: +1 per machine frame, never host time), so
playback is a pure function of frames and identical across hosts. Read
the timer once per frame and tick:

    static anim_state run = ANIM(run_frames, RUN_MS, ANIM_LOOP);
    ...
    unsigned t = arcade1_timer_count();
    asset_sprite4(anim_tick(&run, t), x, y, flip);

`anim_reset(&run, t)` restarts from the first step, anchored at the
current reading — call it on state changes so the new animation doesn't
inherit the old carry. A worked example: `programs/experimental/
walkdemo.c` plays the emitted idle/walk tables from its assetc-compiled
sheet (`make test-walkdemo` pins the behaviour).

`"maps"` imports a level grid over a tileset sprite (the sprite is ONE
frame spanning the whole tileset image). The map file is an ASCII grid
of legend characters — or `{"rows": [...]}` JSON:

    "maps": [
      { "name": "level1", "file": "level1.txt", "tileset": "tiles",
        "tile_w": 16, "tile_h": 16,
        "legend": { ".": null, "#": 0, "q": { "tile": 0, "flipX": true } } }
    ]

`tile_w × tile_h` splits the tileset image into tiles in scan order;
the legend maps each character to a tile — `null` is an empty cell, an
integer is a tile, and the object form adds `flipX`/`flipY` (the device
mirrors within the tile, so one tile serves both directions).
Identical tiles **dedup** at build time: the M29 game's whole 10×3
brick field was one tile in ROM, and the importer derives that
automatically. Each map emits `NAME_COLS`, `NAME_ROWS`,
`NAME_TILE_W/H`, `NAME_TILES`, a packed `NAME_tiles` blob and a
`NAME_cells` array in gfx.h's encoding (`[15:0]` tile index — 0 =
empty, `[19:16]` palette bank, `[20]` flipX, `[21]` flipY):

    arcade1_tilemap4(x, y, level1_cells, level1_tiles,
                     LEVEL1_COLS, LEVEL1_ROWS,
                     LEVEL1_TILE_W, LEVEL1_TILE_H);

Validators are the machine's real rules: even `tile_w` (the 4-bit blit
rejects odd widths, `src/arcade1/video.c`), a tileset that splits into
whole tiles, a complete legend, a non-empty map. There is **no minimum
map size** — that "rule" was a pre-barrier compiler artifact, retracted
in `docs/experiments/tilemap-minsize-retraction.md`; 4×3 and 5×1 maps
draw, pinned by `make test-mapdemo` (inside `make check`).

A map may also declare a **collision layer** — so a game does not have to
write a second, parallel collision table next to its map (assetc dedups
tiles, so a cell index is not a tile type; every game used to pay for that
with its own generator). Two forms:

    "solid": "level1_solid.txt"          # a second ASCII layer, same size
    "solid": { "by_tile": { "#": 1 } }   # a per-tile-type table

The layer file uses `#` = solid, `.` = empty, `^` = hazard; the table form
keys on the map's own legend characters (the tile types) and takes 0/1/2
(or "empty"/"solid"/"hazard"). Either way assetc emits `NAME_solid[]` —
one byte per cell — with `NAME_SOLID_COLS/ROWS` and a `map_ref`, and
`asset.h` reads it:

    #include "asset.h"          /* map_ref + map_solid */
    #include "assets.h"

    int s = map_solid(&level1_map, tx, ty);   /* 0 empty, 1 solid, 2 hazard */

Outside the map is solid: a player must not walk off the world.

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

There is no libc. The guest runtime (crt0) provides exactly the three
functions the compiler itself emits — `memcpy`, `memset`, `memmove` — so
struct assignment, array initialisers and `= {0}` locals link out of the
box (this used to be the #1 build trap: `undefined reference to memcpy`).
Nothing else libc-ish exists: no `printf`, no `malloc`, no string
functions. Use the arcade1.h API for I/O and fixed arrays for storage.
One more boundary: the machine is RV32IM and the SDK links no libgcc
helpers, so 64-bit arithmetic (`uint64_t` maths) fails at link — keep to
32-bit types.

## Run and iterate

    arcade1 run game.arc1 --frames 600 --quiet
    arcade1 run game.arc1 --frames 120 --capture frame.ppm
    arcade1 run game.arc1 --frames 120 --input script.txt \
        --syms game.syms --vars score,state --var-every 60
    arcade1 run game.arc1 --live --record session.txt   # play it by hand
    arcade1 test game.arc1 --golden tests/golden/mygame.txt   # hash check

Per-frame hash lines are diffable against `tests/golden/<game>.txt`
(`--quiet` hides them) — `arcade1 test --golden` does that comparison for
you. `--capture` dumps a frame (`.png` or `.ppm` by extension; it holds
the frame after `--frames N`, i.e. `N-1`'s screen); `arcade1 capture`
dumps every frame into `captures/` (`--format png`, `--scale N`);
`--vars` reads your game's globals by name; `--input` plays a scripted
session; `--record` writes one; `--live` opens a play window (SDL3) so you
can play your own game by hand — R ends the session and writes the keys you
pressed as a normal input script, so a hand-played session replays exactly
like a scripted one (without SDL3 or a display it exits 3; everything
headless is unaffected). `arcade1 inspect` runs the game, then reports
the last frame as greppable `KEY value` lines (video counters, payload
budget, your `--vars`; `--json` for one object, `--regs` for the
register dump, `--commands`/`--at X,Y` for command records and pixel
provenance).
`--strict` fails the run if any draw read a source address outside
ROM/RAM (see SDK.md for the full flag reference).

### Edge-triggered buttons and input scripts

**A press is an edge, not a hold.** Games edge-detect their buttons (press
= "down now, up last frame"), so a script line `6 40 a` gives the guest
**one** press — at frame 6 — and then "still down". It is not 35 presses.

The trap: if that one edge lands while the game is in its game-over state,
the edge is spent on the **restart** and the presses you meant to give it
never happen. The session then diverges from your plan silently (in
Flappy, the bird fell out of the sky ~40 frames later).

**The fix: release the button for one frame** — a release is what makes
the next press a new edge.

Both scripts are in the repo and the test runs exactly them, so this
example cannot drift:

* `tests/inputs/edge_trap.txt` — the trap: `6 40 a` (A held).
* `tests/inputs/edge_fixed.txt` — the fix: release A at frame 7, press
  again at frame 8, release between every press.

```
$ arcade1 run probe_edge.bin --input tests/inputs/edge_trap.txt  --frames 40 --vars state,presses,flaps,restarts
var state = 1        # dead again: the edge was spent on the restart
var presses = 1      # one edge — the hold was ONE press
var flaps = 0        # no flap ever happened
var restarts = 1

$ arcade1 run probe_edge.bin --input tests/inputs/edge_fixed.txt --frames 40 --vars state,presses,flaps,restarts
var state = 0        # still alive
var presses = 5      # five real edges
var flaps = 4
var restarts = 1
```

When a session diverges from your plan, look at the pad the guest actually
saw:

```sh
arcade1 run game.arc1 --input session.txt --frames 200 --pad-trace pad.txt
```

`pad.txt` holds one line per frame — `frame N pad 0xXX` — which shows
exactly which frames carried a press (A = 0x10). The full worked example
is in [SDK.md](SDK.md#edge-triggered-buttons-and-input-scripts).

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

To author a golden by hand (outside the Makefile), the CLI does it too:

```sh
arcade1 golden game.arc1 --input session.txt --frames 600 --out game.golden
arcade1 test  game.arc1 --input session.txt --frames 600 --golden game.golden
```

`arcade1 golden` writes the per-frame hash lines and nothing else — the
exact format the committed goldens use. The grammar (what counts as a
frame line, what a shorter golden or a shorter run means) is in
[SDK.md](SDK.md#golden-files).

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

`flappy` is Game #2: built clean-room from these very docs in the
round-3 fresh-agent exercise (no existing game source was read — see
docs/experiments/round3-cleanroom.md), and promoted into the same
first-class set: `make program`, `make check`, golden-locked.
