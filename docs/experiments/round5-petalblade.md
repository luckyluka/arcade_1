# Round 5 — PETAL BLADE (samurai platformer)

2026-10-20. The fifth round's brief: *a Mario-like whose hero is a samurai,
pushing the machine's visuals, at 1080p, in `round5/`* — reusing round 4's
code and taking the round-5 reference art as the style source.

Result: `round5/` holds a complete, playable, deterministic game
(`petalblade.arc1`), generated entirely from `gen_art.py`, that reaches the
torii gate under a scripted run (`state = 1`, score 70, lives 3) and captures
at exactly 1920×1080.

## Setup

- **Machine:** ARCADE-1, RV32IM, freestanding C, no libc. Native framebuffer
  480×270; `--scale 4` → 1920×1080. ROM budget 256 KiB (used 20 %).
- **Round 4 code reused:** `gen_art.py` is adapted from round 4's generator
  (same palette/G-canvas/PNG approach); `game.c` follows round 4's `smoke.c`
  shape (`asset.h` + generated header, pad → update → draw → present).
- **Round 5 art used as style reference:** the four hand-drawn samurai
  walk-cycle sheets (50×50 grids) are the reference for the hero's
  proportions, kabuto/crescent, indigo-crimson-gold armour and the dusk
  palette. They cannot be dropped in directly (scanned paper, >15 colours,
  not strip-form), so the sprites are generated procedurally to fit the
  machine's 4-bit / 16-colours-per-bank format.

## Findings

1. **RGB565 literals are a silent trap (guest authoring).** Every
   colour-taking helper — `arcade1_clear`, `arcade1_rect`, `arcade1_pixel`,
   `text_draw`, `gfx_sprite`'s `color` — takes a **16-bit RGB565** word. The
   device masks it to 16 bits, so a 24-bit `#RRGGBB` literal (e.g. `0xE8C0A0`)
   is silently truncated to `0xC0A0` and renders the wrong colour (a cloud
   came out magenta; HUD text came out pink). The fix is to convert:
   `(r>>3)<<11 | (g>>2)<<5 | (b>>3)`. CHEATSHEET says "RGB565" but never
   warns against 24-bit literals — the trap bit this round twice and is worth
   an explicit line.
2. **assetc map defines are prefixed by the map name.** A map named `level1`
   emits `LEVEL1_COLS`, `LEVEL1_ROWS`, `LEVEL1_TILE_W`, `LEVEL1_TILE_H`,
   `LEVEL1_TILES`, `LEVEL1_tiles`, `LEVEL1_cells` — *not* `LEVEL_TILE_W`.
   Easy to trip on if you also keep a hand-written level header with
   `LEVEL_COLS`/`LEVEL_ROWS`.
3. **Tilemap cells are for rendering, not collision.** assetc **dedups**
   tiles, so a cell's `[15:0]` index is a *dedup index*, not a tile type —
   you cannot tell solidity from it. A parallel collision grid is required;
   round 5 emits one (`level.h`, `level_solid[]`) from the same generator that
   writes the map. (Dedup is a ROM win — the whole level is 7 unique tiles.)
4. **Parallax on this machine** = draw seamless W-wide strips at
   `world_x = cam_x + screen_offset`, with layer scroll `L = cam_x * factor`
   and `screen_offset = -(L mod W)`, repeating until the screen is covered.
   Three strips (ridge ¼, mid ½, treeline ¾) plus a screen-fixed moon give a
   convincing depth stack without any hardware layers.
5. **Jump/pit tuning.** Jump distance = `MOVE × airtime`; a pit of width `P`
   with a player box `W` needs `MOVE × airtime > P + W`. Worse: a
   *continuous* jump (hold A) lands every `MOVE × airtime` px, so landing
   positions can coincide with pit edges — a level that is clearable with
   discrete jumps can be a trap with a held button. Round 5 ships a discrete
   jump script for that reason.
6. **assetc rejects palettes whose colours collapse to the same RGB565.**
   `#101028` and `#15122C` both map to `0x1025`, so a gradient authored in
   8-bit hex can fail validation. Fix by stepping the 5/6/5 components so
   every entry is distinct (the checker is a 30-line pre-flight).
7. **Hero collision-box offset.** A sprite's `collision` metadata is a box
   *inside the frame*; if `px,py` is the collision-box top-left, draw at
   `(px - cx, py - cy)` (round 5: box `[8,12,12,28]` in a 28×40 frame → draw
   at `px-8, py-12`). Mixing frame-origin and box-origin placement is a
   common source of invisible offsets.
8. **`--scale 4` is the 1080p path.** The framebuffer is 480×270; the capture
   header confirms 1920×1080. No guest change needed — it is a CLI/viewer
   concern.
9. **Determinism held.** Particles are driven by `arcade1_timer_count()` and
   `arcade1_rand()` (seeded LCG, consumed in program order). Two identical
   runs produce identical state and framebuffer hashes.
10. **ROM headroom is large.** All 12 sprite sheets + map + 10 palettes =
    52 520 B (20 %). There is room for more animation frames or a second
    level before any budget pressure.

## Promotion (ROADMAP_V4 card 4.7) — 2026-10-20

Petal Blade was promoted to **Game #3**: `programs/petalblade.c`, the art
pipeline in `programs/petalblade_art/`, the generated headers
`programs/petalblade_assets.h` and `programs/petalblade_level.h` as the build
truth, a script (`tests/inputs/petalblade.txt`), a test inside `make check`
(`tests/test_petalblade.sh`), a committed ROM + symbols
(`tests/roms/petalblade.*`) and a golden (`tests/golden/petalblade.txt`,
1202 frames). The script is the round's `in_run.txt` session preceded by a
death and a restart, so the story (die → START → win at the torii with 70
points) is what the test pins.

### Finding 11 — the committed round-5 header was STALE

Regenerating the header from the committed art did **not** reproduce the
committed one:

```
old header (round5/assets.h):  map level1 of tiles: 130x17 cells, tile 16x16, 7 unique tiles
regenerated:                    map level1 of tiles: 130x17 cells, tile 16x16, 6 unique tiles
```

The evidence that the old header was stale, not the tool wrong:

* the quantized sprite sheet (`tiles_sheet`) is **byte-identical** in both;
* the old header's tile 0 (`4444…ffff…f2ff2f2f…2222…cc2c…`) is **not any
  tile of the current sheet** — it is a tile of an earlier version of
  `art/tiles.png`;
* no committed version of `tools/arc1_assetc.py` (checked `026b0d6`,
  `bd1d28a`, `4fb5ae6` and HEAD) produces 7 tiles for the committed art;
* `gen_art.py` is deterministic: re-running it reproduces the committed
  PNGs and the committed manifest **byte for byte**.

So the art was regenerated after the header was compiled, and the header
was never rebuilt — exactly what the repo's rule ("regenerate the header,
never hand-edit it") exists to prevent, and the reason the promoted game's
terrain had a tile that no longer existed in its own art.

**Resolution:** the promoted header is regenerated from the committed art
(6 unique tiles). Gameplay is unchanged — the collision grid comes from
`petalblade_level.h` (unchanged), the entities and the scoring are fixed
coordinates, and the scripted session still ends at the torii with 70
points. Only the terrain's pixels differ from the round-5 build.

**Lesson for the art pipeline (Wave 7):** a generated file that nobody
regenerates is a file nobody can trust. The promotion now has a rule and a
Makefile target for it (`make petalblade-art`), and the art pipeline's
first job in Wave 7 (card 7.2) is to make regeneration a single command
that proves the committed headers are byte-identical.

### Finding 12 — the repo carried four 6.2 MB captures

`round4/smoke.png`, `round5/final300.png`, `round5/final660.png` and
`round5/out.png` were 1080p PNGs written by the *uncompressed* PNG writer
(6 222 418 bytes each, 24.9 MB of the 37 MB working tree). With the
compressed writer (ROADMAP_V4 card 4.5) the same frame is 62 280 bytes, so
they are not worth keeping; they are removed, and `.gitignore` now keeps
`captures/` and the round captures out.

## Workflow notes

- `gen_art.py` is the single source: it emits the strips, `assets.json`,
  `level1.txt` and `level.h`, all deterministically. Regenerate after any art
  change; `assets.h` is the compiled truth (never hand-edited).
- `arcade1 build game.c petalblade.arc1` works from a foreign directory —
  the SDK (crt0, linker script, headers) is embedded in the CLI, so only a
  RISC-V toolchain is needed on PATH.
- `--vars` reads file-scope globals; all observable state (`state`, `score`,
  `lives`, `px`, `py`, `cam_x`) is file-scope, so the tools see it.

## Verdict

The round-5 brief is met: a samurai platformer that is playable, deterministic
and visually pushed (gradient sky, moon, clouds, three-layer parallax, detailed
tiles, particles, rim-lit characters) at 1080p. The findings above are the
actionable payload — finding 1 in particular is a one-line doc fix that would
have saved two round-trips this round.
