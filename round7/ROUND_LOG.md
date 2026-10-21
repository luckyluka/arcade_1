# ROUND 7 — the log

*Keep this as you go. The log is the deliverable: what you tried, what
happened, and — most of all, every place the documentation did not tell you
what to do.*

The brief: `BRIEF.md`.

**The log covers two steps: the art (first half) and the game (second
half). The game builds, plays, is golden-locked, and every asset passes the
tool's visual checks.**

## Timeline

| when | what |
|---|---|
| 10:53 | read `AGENTS.md`, `BRIEF.md`, `art/README.md`, `art/CRITIQUE.md`, `art/STYLE_TEMPLATE.md`, `docs/art/PROVENANCE.md`, and the tools' own source (`canvas.py`, `qa.py`, `arc1_assetc.py`, `preview.py`) |
| 10:58 | designed the art set: 9 sprites + 5 map layers over 7 palette banks; the world is 30x64 tiles of 16 px (480x1024) — taller than the 480x270 screen |
| 11:02 | wrote `round7/art/gen_round7.py` (one file, the master); first run under `ulimit -v 900000` — 14 files, no MemoryError |
| 11:03 | `python3 tools/arc1_assetc.py --assets round7/assets.json --out-dir round7 --report` — no errors, ROM 15.4 % |
| 11:04 | **all nine strips passed `arc1-art check` on the first run** — the first compliant asset |
| 11:05 | the contact sheet (`arc1-art preview`) — the eyes found what the machine cannot: salt-and-pepper rock, a sea crest that reads as icebergs, a scarf that reads as a beard, a raised arm that reads as a floating stick |
| 11:08 | revision 1: rock drawn as angular facets, one long swell across three crest tiles, the head lifted and the scarf narrowed, the lamp lit with a flame instead of a yellow pane, the arm's shoulder moved inside the coat |
| 11:12 | rendered the whole 480x1024 world with a scratch renderer of my own (no tool does this) — a white ladder, a striped sky, a pyramid-shaped cliff |
| 11:16 | revision 2: the ladder became dark iron in a recess, the sky became flat bands, the cliff edge broke up |
| 11:17 | final run of the art step: every strip passes the checks, the manifest compiles, the contact sheet is written |
| 11:18 | wrote `round7/art/STYLE.md` and the first half of this log |
| 11:22 | **the game step**: read `GAME_DEV_GUIDE.md`, `CHEATSHEET.md`, `SDK.md`, `programs/arcade1.h`, `programs/text.h`, `programs/example_snake.c`; designed the game (ladder, three lamps, the rising sea) and moved the ladder onto the tower's left face in the art generator, so the lamps can hang beside the keeper |
| 11:24 | wrote `round7/game.c` (one file: the state machine, the collision reads, the five map layers, the HUD) and `round7/in.txt` (the scripted session: a loss, a restart, a win) |
| 11:25 | `build/arcade1 build round7/game.c round7/round7.arc1` — built first time; the first run drowned the keeper on frame 1 (the drown test was inverted) |
| 11:30 | the lamps would not light (the charge counter was cleared every frame); fixed, and the script's frame arithmetic was corrected twice (the keeper has to stop *in* the lamp's band) |
| 11:33 | the session wins: state 1, score 800, lamps 3/3, lives 2 — the first attempt was lost to the sea, so the same script proves a loss and a win |
| 11:35 | **the captured frames** (`--capture`) showed what no check could: the stars were gold squares (the sky's legend had drifted from the tile count), the cliff read as a staircase, the sky was a saturated slab, the ladder was a white thread |
| 11:40 | revision 3 of the art: the sky's bands re-cut (the warm ones thin, at the horizon), the rock drawn as angular blocks, the ladder bright iron in a dark recess; the camera now rises to show the tower's top at the win; the HUD's message got a backing plate |
| 11:45 | re-compiled the art, re-ran the nine checks (all green), rebuilt the game, re-ran the session (state 1, score 800) |
| 11:47 | **golden-locked**: `arcade1 golden … --out round7/round7.golden` (420 frame lines) and `arcade1 test … --golden round7/round7.golden` → *"420 frame lines match"*, exit 0; `--strict` passes too |
| 11:50 | wrote `round7/art/reports/round7.md` (the CRITIQUE review) and this log |

## The first compliant asset

**11:04 — 4 minutes after the generator's first run (11:02:59), ~11 minutes
after the step began.** `keeper_idle.png` (24x32, 2 frames) was the first
strip to pass `arc1-art check`; it passed on the very first run, together
with all eight other strips, and went into the manifest at 11:03.

The time was spent *reading*, not failing: the design (what the game needs)
and the tool's exact rules (the outline, the contrast, the exact
quantisation) were settled before the first pixel was painted.

## Attempts

### The art step

1. **One generator, one run.** Wrote `round7/art/gen_round7.py` with the
   whole set in it (sky, tower, rock, sea tilesets; keeper idle/climb/carry;
   lamp off/on; the map layers; the manifest). Ran it under the memory cap
   as instructed — it wrote 14 files, no MemoryError. The compile and the
   nine checks both passed immediately.
2. **The contact sheet said the checks were not enough.** The rock was a
   field of single-pixel specks (noise at 1x), the sea crest read as a
   mountain range, the keeper's scarf sat under his chin like a beard, and
   the raised arm in the climb frames floated free of the body. Fixed in the
   generator (never by hand-editing a PNG): rock as whole angular facets,
   one continuous swell over three crest tiles, the head and hat lifted a
   pixel and the scarf narrowed, the flame drawn as a taper, the arm's
   shoulder joint moved inside the coat.
3. **The world could not be seen.** `arc1-art preview` draws the manifest's
   *sprites* only — the maps are invisible to every tool in the repository.
   I wrote a scratch renderer (in `/tmp`, not a deliverable) that composes
   the four map layers into the 480x1024 world. It showed three more
   faults: the ladder was two pale lines (a bright thread, not a ladder),
   the sky's dithered ramp was a rainbow of stripes, and the cliff was a
   regular pyramid.
4. **Sky by dithering, then not.** The first sky was an ordered dither
   between the palette's levels; at 1x the horizon read as blue-and-orange
   static. Restricting the dither to the indigo family alone made it read
   as horizontal stripes instead (the 4x4 matrix's period shows at 1x).
   Flat bands won: deep night at the top of the world, indigo, violet,
   crimson, coral, amber at the horizon. Deliberate, quiet, and it reads.

### The game step

5. **The first build worked; the first run drowned the keeper at once.**
   The build needed nothing but `#include "assets.h"` beside the source
   (the CLI adds the source's own directory to the include path ✓). The
   run then ended on frame 1 with `state = 2`: my drown test was
   **inverted** — the water's line has to reach *above* his feet
   (`sea_y <= keeper_y`), not below them. One sign flip.
6. **The lamps would not light.** The keeper reached the top but
   `lamps_lit` stayed 0: my charge counter was zeroed at the top of the
   lamp loop and then incremented, so it never passed 1. The charge has to
   *carry over the frames* A is held — the flame takes a moment to catch.
   Restructured it around a `charge_target` (the lamp being lit) and a
   charge that survives while A stays down.
7. **The script's arithmetic was off by one segment.** The keeper must
   stop *inside* a lamp's band (±24 px of the lamp's row) to light it, and
   he moves 4 px a frame, so the script has to release UP one frame later
   than I first wrote it. I recomputed every segment from the keeper's
   speed and re-wrote `round7/in.txt`.
8. **The captured frames found what no check could.** `--capture` at frames
   60, 210 and 400 showed: the "stars" were **gold squares** — I had
   added an eleventh sky band without adding its legend character, so the
   legend's indices had drifted off the tileset's tiles (assetc compiled it
   happily, every strip passed `arc1-art check`, and the contact sheet
   looked fine — the drift only shows in the *map*); the cliff read as a
   **staircase**; the sky was a **saturated slab**; the ladder was a
   **white thread**. All four were fixed in the generator and the art was
   regenerated, re-checked and recompiled.
9. **The win shot did not show the tower's top.** The camera followed the
   keeper with a fixed offset, so at the top of the ladder the beacon (in
   the lantern room) was above the screen. The camera now rises as the
   keeper nears the top, and the HUD's message got a backing plate so it
   reads over the bricks.
10. **Final state.** The session loses attempt one to the sea, restarts on
    START, lights the three lamps and reaches the beacon: `state = 1`,
    `score = 800`, `lamps_lit = 3`, `lives = 2`. Golden-locked with the
    CLI's own `golden` command and re-checked with `test --golden`.

## DOC GAPs

*(every place the documentation was wrong, incomplete or missing — quote the
sentence that failed you, and say what you had to do instead.)*

1. **`arc1-art check --frame WxH` cannot tell you what a legal frame size
   is.** `art/CRITIQUE.md` only ever shows it for a character:
   *"arc1-art check art/build/hero.png --frame 24x32"*. For a tileset — one
   frame holding a grid of tiles — the frame size is the **whole image**,
   and when I got it wrong the tool said only:

   > `arc1-art: the image holds no frame`

   with nothing else. I had to read `tools/arc1_art/qa.py`'s
   `split_frames()` to learn the rule: *the frame width must divide the
   strip's width* (and for a one-frame tileset that means `--frame` is the
   image's own size, e.g. `224x16` for the 14-tile sky). Nothing in
   `art/README.md` says this, and a tileset's `--frame` is exactly the case
   a reader meets first.

2. **The outline checks are not skippable, and the docs do not say that a
   tileset with transparent cells cannot pass them.**
   `art/README.md` rule 6 says:

   > "A background layer (a tileset) needs no outline ring ... (`arc1-art
   > check` says "the outline check does not apply" for a fully opaque
   > strip: there is no edge to outline.)"

   The parenthesis is true but does not say the other half: **as soon as one
   pixel of the strip is transparent, the strip is graded like a character**
   — the outline check (>= 75 % of the edge pixels must be the outline
   colour) and the fill-to-outline contrast (<= 0.55) both apply, and there
   is **no flag to switch them off** (`arc1-art check` has `--key`,
   `--outline-colour`, `--allow-components`, `--max-colours`,
   `--min-height`, `--max-jitter` — nothing for the outline). I planned a
   ladder standing in front of the sky and a wave crest with sky above it;
   both would have failed. I read `qa.py` and worked around it: every
   tileset is fully opaque — the ladder is drawn *on* the tower's wall, and
   the sea's surface is the top edge of its tile. The docs should say this
   outright, because "make the tileset seamless" and "make the tileset
   opaque" are different rules and only the second one is checkable.

3. **Nothing tells a generator that the PNG's pixels must be *exactly* the
   bank's colours.** `art/README.md`'s manifest section says only:

   > "A hand-written bank follows the same rules as a preset: index 0 is
   > the transparent key, index 1 is the outline (the darkest colour in the
   > bank), 2..15 are the ramp, and every colour is distinct as RGB565"

   The default quantisation of the compiler is `"exact"`, and a pixel that
   is not one of the bank's colours is a **hard error**, not a warning
   (`tools/arc1_assetc.py`, `compile_sprites`: *"pixel (%d,%d) #%02X%02X%02X
   not in palette '%s' ... — fix the art or extend the palette"*). The
   docs never mention `quantize` at all, so a generator that paints with
   free-form colours fails at compile time with an error that says nothing
   about the palette bank. I paint with palette *indices* and let
   `canvas.to_strip()` write the bank's own hex colours, which is exact by
   construction.

4. **There is no way to look at the world.** `art/README.md` closes the
   pipeline with:

   > "5. Look at it: `arc1-art check` (7.6) plus a human-style review
   > written to `art/reports/<asset>.md`."

   and `art/CRITIQUE.md` says:

   > "Open the **contact sheet** (`art/reports/<asset>.md` says which
   > file). Look at it at **1x** (the size the machine shows) *first*"

   But `arc1-art preview` composes the manifest's **sprites** only
   (`tools/arc1_art/preview.py` iterates `doc["sprites"]`); the maps are
   never drawn, and no command renders a tilemap. So the three things that
   make or break a level — where the tower stands, where the cliff breaks,
   where the sea is — cannot be seen with the shipped tools. I wrote a
   scratch renderer to compose the map layers into the 480x1024 world; it
   found three faults that every strip-level check was blind to. (This is
   the same gap the preview tool's own docstring admits: *"the pipeline had
   no way to make one, so the round-6 agent wrote its own scratch tool"* —
   the round-6 agent needed it for sprites; for **maps** nobody has shipped
   one.)

   **Refined in the game step:** the *game* is the renderer. `arcade1 run
   … --capture shot.png` shows the composed world exactly as the machine
   draws it, and that is where the art's real faults showed up (the gold
   "stars", the staircase cliff, the slab sky). The gap stands for the art
   alone — with a game in hand, the world is reviewable; without one, it is
   not.

5. **The presets table is stale.** `art/README.md` says:

   > "`art/palettes/*.json` — six 16-entry banks, ready to paste into an
   > `assets.json` manifest"

   and lists six presets. The directory holds **eight**: `cool16`,
   `dusk16`, `forest16`, `hero16`, `neutral16`, `props16`, `ui16`,
   `warm16`. I used `props16` for the cliff (its stone greys and greens are
   exactly right) and it is not in the table. A reader following the
   document would have painted the cliff out of `neutral16` and lost the
   moss.

6. **The map's legend rules are only in the compiler's error messages.**
   `art/README.md`'s "collision layer" section shows
   `"legend": { "g": 0, "p": 1, "s": 2 }` but never says that a legend key
   must be **exactly one character** (`arc1-assetc`: *"legend key %s must be
   exactly one character"*) — which matters the moment a layer needs more
   than a handful of tile types (the sky needs fourteen). I allocated
   fourteen single characters up front (`abcdefghijkSCM`) instead of
   discovering the rule by breaking it.

7. **The machine half of the review never looks at a map.** `CRITIQUE.md`
   presents the two halves as if they covered the same thing:

   > "The machine half of the art review is `arc1-art check` (it counts the
   > colours, the orphans, the components, the baseline, the jitter, the
   > outline coverage and the contrast). This is the **human** half: the
   > checklist for an agent that can *see*."

   In fact it looks at **strips**: one PNG, sliced into frames. A map — the
   thing that decides what the world *is* — is never checked by anything,
   and a legend that has drifted off its tileset is silent: I added an
   eleventh sky band without adding its legend character, `arc1-assetc`
   compiled the manifest without a murmur, all nine strips passed
   `arc1-art check`, and the contact sheet looked fine — because the
   contact sheet shows the *tileset*, not the *map*. Only the captured game
   frame showed the truth (the stars had become gold squares). A check that
   renders a map's first row, or a lint that compares the legend's
   character count with the tileset's tile count, would have caught it in a
   second.

8. **The one end-to-end example of the asset pipeline is in the directory
   the brief forbids.** `GAME_DEV_GUIDE.md`:

   > "A working end-to-end example: `programs/experimental/asset_demo.c`,
   > built by `make experimental` and pinned pixel-for-pixel by `make
   > test-assetdemo`."

   and, a few lines earlier, the reference implementations it points at:

   > "The three `programs/example_*.c` games are the reference
   > implementations of this shape — start by reading one."

   The three examples use **no assets at all** (snake draws rectangles), and
   the round-7 brief says: *"Do not read ... `programs/experimental/*`"*.
   So a clean-room agent gets a working example of the asset API that it
   is not allowed to open, and three examples that never touch it. I had to
   derive the whole pattern from `programs/asset.h`'s comments and
   `CHEATSHEET.md`: the manifest's `"animations"` → `anim_tick()` →
   `asset_sprite4()` chain, the `map_ref`/`map_solid()` reads, and the
   `gfx_pal_upload()` of seven banks at startup. A second worked example
   outside `programs/experimental/` (or the brief allowing that one file)
   would have saved an hour.

9. **The docs never say how a game is supposed to compose several map
   layers.** `art/README.md` documents a map, a tileset and a collision
   layer, and shows exactly one map in the manifest. The game's world is
   four or five layers (the sky, the cliff, the tower, the sea's surface,
   the deep water), each its own map and its own palette bank, drawn in
   order, all in world space so one camera moves them together — and none
   of that is written down anywhere. I found out that a *rising* sea
   needs a second map (the deep water under the surface band) because the
   tilemap draws a fixed rectangle, and that the cells carry their own
   palette bank so every layer's palette has to be uploaded, by reading
   `gfx.h` and `arc1_assetc.py`. The manifest section could say in one
   sentence that a game may stack maps.

## Had to read the source

*(the things I could only work out by reading a tool's source — the file,
the function, and what it was doing)*

* **`tools/arc1_art/canvas.py` — `Canvas.outline(c, only=0, neighbours=4)`.**
  It paints the outline **outside** the silhouette (into the transparent
  pixels that touch an opaque one), not inside it. That is the opposite of
  what the name suggests, and it is what makes the machine's outline check
  pass: the outermost pixels are the outline colour, so the silhouette's
  *edge* is the outline.
* **`tools/arc1_art/qa.py` — `check_strip()`.** The exact rules: the outline
  colour is inferred as the **darkest edge pixel** (not the darkest in the
  frame) and then **reused for every frame of the strip**; the contrast is
  `outline_luminance / mean_fill_luminance` and must be **<= 0.55**; a
  frame with no transparent pixel skips both; the "jitter" is the spread of
  the bounding boxes' **centres** (not their edges); the baseline is the
  lowest opaque row. I designed the keeper against these numbers (the
  outline is index 1, the coat stays light enough that the mean fill
  luminance is far above the outline's).
* **`tools/arc1_assetc.py` — `compile_sprites()`, `compile_maps()`,
  `compile_solid()`, `emit_header()`.** The exact quantisation, the even
  frame width ("the 4-bit blit needs an even width, odd draws nothing"),
  the tileset-is-one-frame rule, the legend's one-character keys, the
  cells' encoding (`[15:0]` tile, `[19:16]` bank, `[20]`/`[21]` flips) and
  the emitted names (`<map>_map` for the `map_ref`, `<map>_cells`,
  `<map>_tiles`, `<palette>_palette`).
* **`tools/arc1_art/preview.py`.** The contact sheet draws the manifest's
  sprites only — no maps — which is how I found gap 4.
* **`src/arcade1/video.c` — `video_tilemap4()`.** The tilemap is *windowed*:
  the device walks only the cells the camera leaves on screen ("those are
  never visited, never fetched"), so five full-world tilemaps per frame
  cost nothing (the whole game draws in ~1 600 instructions per frame).
* **`tools/arcade1.c` — `cmd_build()`.** The build is self-contained: the
  SDK headers travel inside the CLI and it adds `-I<the source's own
  directory>`, which is why `#include "assets.h"` resolves when the header
  sits beside the game.

## What I built

**The game**: `round7/game.c` — a lighthouse keeper on a cliff at dusk.

* **The world**: 30x64 tiles of 16 px (480x1024), nearly four screens tall.
  Five map layers, all in world space so one camera moves them together:
  the sky (the dusk bands, the stars, the clouds, the moons), the cliff,
  the tower, the sea's surface band and the deep water under it. The tower
  and the cliff carry collision layers, compiled by `arc1-assetc` and read
  with `map_solid()`.
* **How it plays**: the keeper stands on the iron ladder on the tower's
  face. **UP/DOWN** climbs (4 px a frame; the collision layer stops him at
  the cliff's top and at the gallery). **A** held for 48 frames at a lamp's
  height lights that lamp (the keeper takes his lantern out while it
  catches). **START** begins another attempt. The sea rises 2 px a frame
  the whole time — twice as fast as the keeper's climb, so every second
  spent lighting a lamp is a second the water gains on him. The camera
  follows the keeper and keeps the water in view while it is near; at the
  top it rises to show the lantern room.
* **Win**: all three lamps lit *and* the keeper at the top of the ladder →
  the beacon burns, `state = 1`, `score = 800` (100 a lamp, 500 for the
  beacon).
* **Lose**: the water's line reaches the keeper's feet → `state = 2`, one
  life spent. Three attempts; START after the last one begins a fresh
  round.
* **The scripted session** (`round7/in.txt`, 420 frames) proves both: the
  keeper does nothing and the sea takes him at frame 96 (`state = 2`,
  `lives` 3 → 2); START at frame 100 begins a new attempt; the keeper then
  climbs, lights the three lamps and reaches the beacon at frame 384
  (`state = 1`, `score = 800`, `lamps_lit = 3`, `lives = 2`).
* **The golden**: `round7/round7.golden`, 420 frame lines, written by the
  CLI's own `golden` command and re-checked with `test --golden`
  (*"420 frame lines match"*, exit 0). `--strict` passes as well: no draw
  reads outside ROM/RAM and no colour word carried bits above 15.

**Where the art is weakest**, honestly:

* the **sky is flat bands**, so it posterises — deliberate, but a smooth
  ramp would be nicer and the palette does not have enough close hues to
  dither one;
* the **cliff tiles repeat every 16 px**, and the angular blocks make the
  repeat visible as a pattern across a wide cliff;
* the **keeper's face** is six pale pixels with two dark ones for the eyes,
  and at 1x the eyes sit wide apart — the read comes from the hat and the
  coat, not the face;
* the **ladder** is legible but thin: at 1x it is two bright lines with
  faint rungs.

## Acceptance (the art step)

```
$ bash -c 'ulimit -v 900000; python3 round7/art/gen_round7.py'
gen_round7: 15 file(s) written

$ python3 tools/arc1_assetc.py --assets round7/assets.json --out-dir round7 --report
SPRITE keeper_carry 24x32 x2 = 768 B
SPRITE keeper_climb 24x32 x4 = 1536 B
SPRITE keeper_idle 24x32 x2 = 768 B
SPRITE lamp_off 16x16 x1 = 128 B
SPRITE lamp_on 16x16 x2 = 256 B
SPRITE rock 64x32 x1 = 1024 B
SPRITE sea 48x32 x1 = 768 B
SPRITE sky 224x16 x1 = 1792 B
SPRITE tower 96x32 x1 = 1536 B
MAP rock_map of rock: 30x64 cells, 3 tiles = 9984 B, 1920 B collision layer
MAP sea_deep_map of sea: 30x56 cells, 1 tiles = 6848 B
MAP sea_map of sea: 30x8 cells, 6 tiles = 1728 B
MAP sky_map of sky: 30x64 cells, 14 tiles = 9472 B
MAP tower_map of tower: 30x64 cells, 7 tiles = 10496 B, 1920 B collision layer
ROM assets 47368 / 262144 (18.1 %)

$ tools/arc1-art check round7/art/strips/<name>.png --frame <WxH>
check OK  sky (224x16)       check OK  keeper_idle (24x32)
check OK  tower (96x32)      check OK  keeper_climb (24x32)
check OK  rock (64x32)       check OK  keeper_carry (24x32)
check OK  sea (48x32)        check OK  lamp_off (16x16)
                             check OK  lamp_on (16x16)

$ tools/arc1-art preview round7/assets.json --out round7/art/sheet.png --scale 4
preview: round7/art/sheet.png
```

## Acceptance (the game step)

```
$ build/arcade1 build round7/game.c round7/round7.arc1
arcade1: built round7/round7.arc1 (+ round7/round7.syms)

$ build/arcade1 run round7/round7.arc1 --input round7/in.txt --frames 420 \
      --syms round7/round7.syms --vars state,score,lives,lamps_lit,keeper_y,sea_y
var state = 1
var score = 800
var lives = 2
var lamps_lit = 3
var keeper_y = 176
var sea_y = 360

$ build/arcade1 golden round7/round7.arc1 --input round7/in.txt --frames 420 \
      --out round7/round7.golden
arcade1 golden: wrote 420 frame lines to round7/round7.golden

$ build/arcade1 test round7/round7.arc1 --input round7/in.txt --frames 420 \
      --golden round7/round7.golden
arcade1 test: 420 frame lines match round7/round7.golden
(exit 0)

$ build/arcade1 run round7/round7.arc1 --input round7/in.txt --frames 420 --strict
ran 420 frames  final state hash=0x76932EF5BA73BB7D  fb hash=0xAD882B8066325E0B
(exit 0)

$ build/arcade1 inspect round7/round7.arc1 --input round7/in.txt --frames 420
INSTRET_FRAME 1617
VIDEO_CMDS CLEAR=1 FILL=2 SPRITE=44 TILEMAP=0 SPRITE4=5 TILEMAP4=5 PRESENT=1
```

Determinism: the art generator was run twice and every output file hashed
the same (`md5sum` diff empty) — the same inputs give the same bytes; the
golden test passes twice in a row.

## Checklist

- [x] the game builds and runs
- [x] the game is playable (a scripted session reaches the end state)
- [x] it is golden-locked with the CLI's `golden` command
- [x] the golden passes with `arcade1 test --golden`
- [x] every asset passes the tool's visual checks
- [x] no generated file was hand-edited
- [x] this log is complete
