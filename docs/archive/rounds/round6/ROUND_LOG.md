# ROUND 6 — the log

*Keep this as you go. The log is the deliverable: what you tried, what
happened, and — most of all — every place the documentation did not tell
you what to do.*

The brief: `BRIEF.md`.

## Timeline

| when | what |
|---|---|
| 09:31 | start of the round. Read `AGENTS.md`, `art/README.md`, `art/STYLE_TEMPLATE.md`, `art/CRITIQUE.md`, `docs/art/PROVENANCE.md`, the tool sources (`canvas.py`, `qa.py`, `arc1_assetc.py`) and the guest headers (`arcade1.h`, `gfx.h`, `asset.h`). |
| 09:44 | wrote `round6/art/gen_round6.py` (the generator: the farmer, the slime, 4 pickups, the 3-tile ground, the map, the collision layer, the manifest). |
| 09:46 | first run: `ModuleNotFoundError: No module named 'arc1_art'` — my `ROOT` guess was one level too deep (see DOC GAP 1). Fixed; the generator ran. |
| 09:47 | the generator ran under `ulimit -v 900000` and wrote 7 strips + the map + the collision layer + `assets.json`. **The first compliant asset: the farmer strip, ~16 minutes after the start of the step.** |
| 09:48 | all 7 strips passed `arc1-art check` on the first try (no failures). |
| 09:50 | `arc1-assetc --assets round6/assets.json --out-dir round6 --report` → exit 0, `ROM assets 4240 / 262144 (1.6 %)`. `--require-provenance` → exit 0. |
| 09:52 | wrote `round6/art/preview.py` (a scratch look-at-the-art tool) and LOOKED at the map at 2x: the dark border rows I had put around the tileset turned the field into a striped quilt. |
| 09:58 | redrew the tileset **seamless** (grass flows, the path is a pavement of flagstones, the pond is water with ripples) — the machine's outline check is vacuous on a fully opaque strip. Re-previewed: the field reads as a village. |
| 10:00 | LOOKED at the sprite sheet at 4x: the slime's eyes were invisible (dark on dark) and the apple's stem was lost. Gave the slime pale sclera, pupils and a smile; gave the apple a leaf. |
| 10:05 | final pass: all 7 strips pass `arc1-art check`; the generator is byte-deterministic (two runs, identical md5s); the manifest compiles. |
| 10:12 | **the game step.** Read `GAME_DEV_GUIDE.md`, `SDK.md`, `CHEATSHEET.md`, `programs/example_snake.c` and the CLI's `cmd_build` (to see the `-I<source dir>` flag that lets the guest `#include "assets.h"`). |
| 10:20 | wrote `round6/game.c` (the guest: the map, the farmer, the slime, the four harvests, the HUD, the win/lose banners). It compiled on the first try; no warnings with `-Wall -Wextra`. |
| 10:22 | first scripted run: **the game wins** — `state=1 score=4 lives=3 pickups=0` — but the CLI warned *"2940 colour writes carried bits above 15"*: two of my HUD colours were 24-bit literals (`0xFFE3A5`, `0xC6C3CE`). Fixed with `arcade1_rgb()`; `--strict` now passes. |
| 10:26 | the losing session: the farmer walks north into the pond's edge, the slime takes him three times (`lives 3→2→1→0`, `state=2`), and START restarts the game (`state=0 lives=3`). |
| 10:30 | golden-locked: `arcade1 golden … --out round6/round6.golden` (240 frame lines) and `arcade1 test … --golden` → *"240 frame lines match"*. |
| 10:33 | **looked at the frames** (`--capture`): the win banner was off-centre and unreadable over the map → a dark panel and a centred draw; re-goldened. |
| 10:40 | the sprites floated: added a shadow blob under the farmer and the slime, offset to the shadow side (the first attempt, straight under the feet, was invisible against the boots); re-goldened. |
| 10:45 | final: the build, the win session, the lose session, the golden and the determinism check (two runs, the same hashes) all pass. |

## The first compliant asset

**The farmer strip** (`round6/art/strips/farmer.png`, 2 frames of 16x24),
**≈16 minutes** after the start of the step (09:31 → 09:47). It was the
first strip written and it passed `arc1-art check` on the first try:
`ok=True frames=2 failures=[]`, outline coverage 1.0, jitter 0.0.

## Attempts

1. **Read the docs first** (`AGENTS.md` → `art/README.md` → the tool
   sources). The art README's "Writing a generator (and finding the
   library)" section gave the import shape; the two rules that catch
   everyone out (a tileset is ONE frame; a generator must not allocate
   without bound) were in it too.
2. **Wrote the generator** in one go: 4 palette banks, the farmer
   (16x24, 2 walk frames), the slime (16x16, 2 hop frames), 4 pickups
   (12x12, 1 frame each), the ground tileset (48x16 = 3 tiles of 16x16,
   **one frame**), the map (30x16 cells), the collision layer and
   `assets.json`.
3. **First run: `ModuleNotFoundError: No module named 'arc1_art'`.**
   I had applied `dirname` three times to the script's directory; the
   README's snippet applies it twice, and my script is already two levels
   below the folder. Fixed to two `dirname`s. (DOC GAP 1.)
4. **Second run under the memory cap** (`bash -c 'ulimit -v 900000;
   python3 round6/art/gen_round6.py'`) → 7 strips, 2 text files, 1
   manifest; no MemoryError (the biggest surface is 48x16 pixels; the
   whole set is a few tens of kilobytes).
5. **The checks** — all 7 strips passed `arc1-art check` immediately:
   every frame 1 component, 0 orphans, outline coverage 1.0, jitter 0.0,
   the baseline shared inside every strip, and the contrast well under
   the 0.55 limit (0.13–0.33 with `--outline-colour`).
6. **The manifest** compiled clean: `arc1-assetc --assets round6/assets.json
   --out-dir round6 --report` → 7 sprites, 2 animations, the 30x16 map with
   its 30x16 collision layer, 4 palette banks, `ROM assets 4240 / 262144`.
7. **Looked at the art** (the human half of `art/CRITIQUE.md`): wrote
   `round6/art/preview.py`, which draws the map the manifest describes
   with the sprites on it, at 1x and 2x, plus a 4x sprite sheet.
   * **Attempt 1 (rejected):** the tileset had a dark border on the top
     and the bottom of the strip (to satisfy the outline-coverage check).
     In the map that border repeats in *every* cell: the field came out as
     a striped quilt of horizontal bands, and the pond as a row of blue
     boxes. **What changed:** the tiles are now seamless — the grass
     flows, the path is a pavement of flagstones with a dark joint, the
     pond is water with ripples. The machine's outline check is vacuous
     here (a fully opaque strip has no edge pixels at all, so the
     coverage is 1.0 whatever the colours are) — that is a fact I had to
     read out of `tools/arc1_art/qa.py`, not out of the art docs (DOC
     GAP 3).
   * **Attempt 2 (rejected):** the slime's eyes were the outline colour on
     a mid-green body — they vanished, and so did the mouth. The apple's
     brown stem read as nothing. **What changed:** the slime got a pale
     sclera (`#A5DF94`) with a dark pupil and a 4-pixel smile; the apple
     got a green leaf next to its stem.
8. **Final verification:** all 7 strips pass `arc1-art check`; the
   generator run twice under the memory cap gives byte-identical output
   (`md5sum -c` on all 7 strips, the manifest and the map: all OK);
   `arc1-assetc --require-provenance` exits 0. No generated file was ever
   hand-edited — every PNG, the map, the collision layer and the manifest
   come out of `gen_round6.py`, and the header out of `arc1-assetc`.

### The game step

9. **Wrote `round6/game.c`** in one go: the map with `arcade1_tilemap4`,
   the farmer and the slime with `asset_sprite4` and the emitted
   animation tables, the harvests as sprites, the HUD with `text.h`, and
   the win/lose banners. The collision layer is read with `map_solid()`
   from `programs/asset.h`, exactly as the manifest's `"solid"` layer
   intends. It **compiled on the first try** (`arcade1: built
   round6/round6.arc1 (+ round6/round6.syms)`), and with `-Wall -Wextra`
   there is not one warning.
10. **The first scripted session already won** — but the CLI warned:
    *"2940 colour writes carried bits above 15 and were truncated"*. My
    mistake, not a doc gap: the cheat sheet says every colour argument is
    a 16-bit RGB565 word, and I had written two 24-bit literals
    (`0xFFE3A5`, `0xC6C3CE`) for the banner. Fixed with
    `arcade1_rgb(0xFF,0xE3,0xA5)`; `arcade1 run --strict` now passes and
    `arcade1 inspect` reports `VIDEO_COLOR_TRUNC 0`.
11. **The losing session** (`round6/in_lose.txt`): the farmer walks north
    until the collision layer stops him under the pond, the slime takes
    him three times (`lives 3→2→1→0`, `state=2`) and the START press at
    frame 420 restarts the game (`state=0`, `lives=3`, the farmer back at
    the start) — the restart edge works.
12. **Golden-locked with the CLI's own command**
    (`arcade1 golden … --out round6/round6.golden`, 240 frame lines) and
    proved it (`arcade1 test … --golden` → *"240 frame lines match"*).
13. **Looked at the frames** (`arcade1 run … --capture`): the first win
    banner was drawn at x=160 (so it sat left of the centre) with no
    backing, and the words ran over the grass and the pavement. **What
    changed:** the banner now has a dark panel behind it and is centred
    with a `draw_centered()` helper; the golden was re-made, because the
    art changed (a golden change is a behaviour change).
14. **The sprites floated.** At 3x (`--capture --scale 3`) the farmer and
    the slime had nothing under them. **What changed:** a two-row shadow
    blob, offset to the bottom right (the light comes from the top
    left). The first attempt — straight under the feet — was invisible
    against the farmer's dark boots, so it was moved to the shadow side
    and it now reads on the grass and on the pavement. The golden was
    re-made again.
15. **Final verification** (the transcript is in the "What I built"
    section below): the build, the win session, the lose session, the
    golden comparison, `--strict` and the determinism check (two runs,
    the same state and framebuffer hashes) all pass.

## DOC GAPs

*(every place the documentation was wrong, incomplete or missing — the
sentence that failed you, and what you had to do instead.)*

1. **The repository root: the prose and the code disagree.**
   `art/README.md` says: *"**It assumes the generator lives two directories
   below the repository root** — `<repo>/<folder>/<script>.py` — and that
   the repository's `tools/` directory is a sibling of that folder's
   parent."* and, two paragraphs later, *"keep the generator two levels
   below the root (`<repo>/roundN/art/…`)"*. Those two statements cannot
   both be true: `<repo>/roundN/art/…` is **three** levels below the root,
   and the code the same section prints —
   `ROOT = os.path.dirname(os.path.dirname(HERE))` — only resolves to the
   repo root for a script **three** levels down (`roundN/art/script.py`).
   For the two-level case the same snippet resolves to the *parent* of the
   repository. **What I did:** I used the `ARCADE1_ROOT` escape hatch the
   README offers and applied `dirname` twice, which is right for
   `round6/art/gen_round6.py`. The prose sentence ("two directories below
   the repository root") is the part that is wrong; a generator written
   exactly as the prose describes (`<repo>/round6/gen.py`) would die with
   the very `ModuleNotFoundError` the README names one paragraph later.

2. **The collision layer is not in the art documentation at all.**
   `art/README.md` §"How a game gets its art", step 3: *"Write the
   manifest (`assets.json`): palettes, sprites, animations, maps, and a
   `provenance` block per asset."* — and that is the whole of the art
   documentation about maps. The brief asks for *"a map file … and give the
   map a collision layer"*, and the key that does it (`"solid": "<file>"`
   or `"solid": {"by_tile": {...}}`, with `#` solid / `.` empty / `^`
   hazard) is documented **nowhere** in `art/`: I found it in
   `tools/arc1_assetc.py` (`compile_solid()`, whose docstring says
   *"the optional collision layer of a map (ROADMAP_V4 card 4.6)"*) and in
   `programs/asset.h` (`map_ref`, `map_solid()`, `MAP_EMPTY/MAP_SOLID/
   MAP_HAZARD`). **What I did:** wrote a second ASCII file
   `round6/art/village_solid.txt` with the same size as the map and
   declared `"solid": "art/village_solid.txt"`. An agent that only read
   the art docs would have shipped a map with no collision layer at all.

3. **The outline-coverage check is vacuous on a fully opaque strip, and
   the art docs do not say so.**
   `art/README.md` §"The visual review": *"`arc1-art check file.png
   [--frame WxH]` counts the colours, the orphan pixels, the components,
   the baseline, the anchor's jitter, the outline coverage and the
   fill-to-outline contrast"* — and `art/README.md`'s "Two rules that catch
   everyone out" mentions only the one-frame tileset and the allocation
   bound. Nothing says what "the outline coverage" means for a *background
   layer*: in `tools/arc1_art/qa.py` the edge pixels are the opaque pixels
   that touch a transparent pixel **inside the frame**, so a strip with no
   transparency has *no* edge pixels and the coverage is 1.0 by
   construction. A tileset therefore passes with or without an outline
   ring. **What I did:** I first drew a 1px dark border around the whole
   tileset "to be safe" — and that border repeats in every cell of the
   map and wrecked the look (see Attempts 7). I then made the tiles
   seamless, which is both prettier and still passes. The docs should say
   that a background layer needs no outline, and that a border in a
   tileset repeats in every cell of the map.

4. **The rule that the outline colour must be the DARKEST colour in the
   frame is only in the tool's source.**
   `art/README.md` rule 2 says: *"Index 1 of every preset is the
   **outline** colour — the dark edge that makes a sprite readable at 1x.
   Index 2..15 are the ramp."* — "dark", not "darkest". But
   `tools/arc1_art/qa.py` infers the outline colour as
   `min(colours, key=_luminance)` — the darkest colour *present in that
   frame* — and fails the coverage check if the silhouette's edge is not
   that colour. So a sprite that paints palette index 9 (`#101429`)
   *and* index 1 (`#101018`) has an edge that is not the darkest colour
   in the frame, and the check fails with a message about the outline
   coverage. `art/palettes/hero16.json` and `props16.json` are not in the
   preset list that `tests/test_art_palettes.sh` checks, and in
   `hero16` index 1 is only just the darkest (luminance 16.9 against
   21.2 for index 9). **What I did:** checked every bank I used with a
   script (index 1 must be the darkest of the bank) before drawing
   anything. The docs should say "the darkest", not "dark".

5. **Nothing says a manifest may define its own palette.**
   `art/README.md` §"The presets": *"`art/palettes/*.json` — six 16-entry
   banks, ready to paste into your `assets.json` manifest (add the `bank`
   number there)"*, and `art/STYLE_TEMPLATE.md` §5 asks for
   *"**Preset**: `<neutral16 | warm16 | cool16 | dusk16 | forest16 |
   ui16>`"* and *"**Deviation from the preset**: `<none, or exactly which
   index and why>`"*. A deviation is described as *changing an index of a
   preset*, not as *writing a new bank* — but four pickups need a red
   **and** a green **and** a gold, and no preset holds a red and a green
   together, and no preset holds water **and** earth. **What I did:**
   wrote two custom 16-entry banks (`props`, `ground`), each with the
   key at index 0, the outline at index 1 and the ramp above, and noted
   the deviation. `hero16` and `forest16` are used verbatim for the
   farmer and the slime.

6. **(minor) The manifest's path convention is only in the compiler.**
   The art docs never say that the `"file"` of a sprite and the `"file"`
   of a map are **relative to the `assets.json` file**, not to the
   repository root; the only statement of it is the compiler's error
   message (*"path to the strip (PNG or P6 PPM), relative to
   assets.json"*) and the same line in its docstring. **What I did:**
   wrote `"art/strips/farmer.png"` and `"art/village.txt"` relative to
   `round6/assets.json`. A generator that wrote absolute paths would
   compile, but the manifest would not be portable.

7. **A file-scope global that the guest never *reads* is deleted by the
   optimiser, and `--vars` then says the name does not exist.**
   `GAME_DEV_GUIDE.md` rule 1: *"Observable state is a file-scope global
   (score, lives, state, length, camera position, ...). The tools read
   those out of guest RAM by name via `--vars`"*, and `SDK.md`: *"Each
   variable is read as one 32-bit word — the guest's `int`, printed
   signed"*. Neither says the variable has to survive `-Os`. I measured
   it (a scratch guest, three cases):

   | the guest does this to a `static int` | the symbol |
   |---|---|
   | writes it, never reads it | **gone** (the store is dead) |
   | reads it, never writes it | **gone** (folded to a constant) |
   | writes it and reads it | kept |

   and the CLI's answer is not a hint but a runtime error:
   `arcade1: no symbol named 'byte_state'` (exit 1). **What I did:**
   made sure every global the tools read is *read* by the game as well
   as written (the HUD prints `score`, `lives` and `pickups`; the game
   reads `state`, `frame`, `stun`, `facing`, the positions and
   `taken[]`), and checked the symbol dump: all twelve survive. The docs
   should say that the variable must be read (or `volatile`), and that a
   missing name can also mean "the optimiser removed it".

## Had to read the source

*(the things I could only work out by reading a tool's source — the file,
the function, and what it was doing)*

* `tools/arc1_art/qa.py` — `check_strip()`: what the checks really are.
  The edge-pixel definition (an opaque pixel touching a transparent pixel
  **inside** the frame — so a fully opaque strip has no edge pixels and
  the coverage is 1.0), the outline colour inference (the darkest colour
  present in the frame, not palette index 1), the "readability" rule (the
  bounding box is at least 8 px tall) and the jitter rule (the centre of
  the bounding box, not the feet).
* `tools/arc1_art/canvas.py` — `to_strip()` writes an 8-bit **RGB** PNG
  and paints a transparent pixel as `(0, 0, 0)`, which is why every
  palette's index 0 has to be `#000000`: both the compiler and the check
  treat black as the key. `outline(colour)` paints a 1px ring *outside*
  the silhouette, into the transparent pixels only.
* `tools/arc1_assetc.py` — the manifest schema in practice: the map
  `legend` and the `"solid"` layer (`compile_solid()`), the "a tileset is
  ONE frame" rule (the error text in `compile_maps()` names it as *doc
  gap 2*), the `origin`/`collision` metadata and its bounds checks
  (`_meta_geometry()`), and the one-bank-per-palette rule
  (`check_banks()`).
* `programs/asset.h` — `sprite4_ref` (the `origin` anchor and the
  `collision` box the header carries) and `map_ref`/`map_solid()` (the
  collision layer's three values, and that the outside of the map is
  solid).
* `tools/arcade1.c` — `cmd_build()`: the build adds `-I<the source's
  own directory>`, which is what lets the guest `#include "assets.h"`
  with the generated header sitting beside it (the comment there names it
  *"doc gap 4"*). The build also needs `riscv64-unknown-elf-gcc` and
  `nm` on PATH and writes the `.syms` beside the image.

## What I built

### The game — "The Farmer of the Village"

`round6/game.c`, one screen, no camera, no scrolling: the 30x16 village
map (480x256 px) is the whole world, and the 14 rows under it are the
HUD.

* **Controls** — the d-pad walks the farmer (3 px a frame); **START**
  restarts after a win or a loss. The farmer is stopped by the map's
  collision layer: he cannot walk into the pond, and he cannot walk off
  the world (the map's outside is solid).
* **The slime** — starts in the north-west and hunts the farmer, one
  pixel a frame on each axis (it hops every other frame), turning every
  20 frames. It is slower than the farmer, so a farmer who keeps moving
  outruns it; a farmer who stands still does not.
* **Win** — collect all four harvests (the coin, the apple, the carrot
  and the cabbage): `state = 1`.
* **Lose** — the slime touches the farmer: one life is lost, the farmer
  is knocked out for 45 frames and the slime is pushed back; at zero
  lives `state = 2`.
* **Observable state** (file-scope globals, read with `--vars`):
  `state`, `score`, `lives`, `pickups`, `farmer_x`, `farmer_y`,
  `slime_x`, `slime_y`, `stun`, `facing`, `frame` and `taken[]` — all
  twelve survive the optimiser (see DOC GAP 7).
* **The art** — the seven strips of the previous step, compiled into
  `round6/assets.h` by `arc1-assetc` and drawn with `asset_sprite4` and
  the emitted animation tables (`farmer_walk`, `slime_hop`); the map is
  drawn with `arcade1_tilemap4` from the generated `village_cells` /
  `village_tiles`; the four palette banks are uploaded once with
  `gfx_pal_upload`.

### The acceptance transcript

```
$ build/arcade1 build round6/game.c round6/round6.arc1
arcade1: built round6/round6.arc1 (+ round6/round6.syms)

$ build/arcade1 run round6/round6.arc1 --input round6/in.txt --frames 240 \
      --syms round6/round6.syms --vars state,score,lives,pickups,farmer_x,farmer_y,slime_x,slime_y --quiet
var state = 1
var score = 4
var lives = 3
var pickups = 0
var farmer_x = 217
var farmer_y = 161
var slime_x = 78
var slime_y = 115
ran 240 frames  final state hash=0xCDDD76D178D9DA25  fb hash=0x48179D65FC344CAF

$ build/arcade1 golden round6/round6.arc1 --input round6/in.txt --frames 240 --out round6/round6.golden
arcade1: golden: wrote 240 frame lines to round6/round6.golden

$ build/arcade1 test round6/round6.arc1 --input round6/in.txt --frames 240 --golden round6/round6.golden
arcade1: test: 240 frame lines match round6/round6.golden

$ build/arcade1 run round6/round6.arc1 --input round6/in_lose.txt --frames 460 \
      --syms round6/round6.syms --vars state,score,lives --quiet --var-every 100
vars    0 state=0 score=0 lives=3
vars  100 state=0 score=0 lives=3
vars  200 state=0 score=0 lives=2
vars  300 state=2 score=0 lives=0
vars  400 state=2 score=0 lives=0
var state = 0        # the START press at frame 420 restarted the game
var score = 0
var lives = 3

$ build/arcade1 inspect round6/round6.arc1 --input round6/in.txt --frames 60
VIDEO_CMDS CLEAR=1 FILL=4 SPRITE=31 TILEMAP=0 SPRITE4=6 TILEMAP4=1 PRESENT=1
VIDEO_OOB_DRAWS 0
VIDEO_COLOR_TRUNC 0
ROM_USED 7948 / 262144
```

### Where the art is weakest

* The **pond** is a flat blue patch with a hard edge against the grass —
  a seamless tile cannot have a shore, and the map has only three tiles.
* The **slime** is 16x16 and its silhouette is a dome: no bump, no drip,
  no asymmetry. It reads as "a green blob" a second after the eyes.
* The **grass texture** is an even scatter of dark blades, so the eye
  finds rows in it when the camera moves.
* In the game, the farmer and the slime **float**: nothing grounds them
  on the field (a 1px shadow would cost one more draw a frame).
  **Fixed:** they now have a two-row shadow, offset to the bottom right
  (the light comes from the top left).

## Checklist

- [x] the game builds and runs
- [x] the game is playable (a scripted session reaches the end state)
- [x] it is golden-locked with `arcade1 golden`
- [x] the golden passes with `arcade1 test --golden`
- [x] every asset passes `arc1-art check`
- [x] no generated file was hand-edited
- [x] this log is complete
