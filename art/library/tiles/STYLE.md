# Style sheet — the `tiles` pack

Twelve seamless 16×16 surface tiles in one tileset: grass, dirt, stone,
brick, wood, sand, water, roof, floor, road, snow, lava. The tiles a game
stands on.

## Sprite grid and sizes

* **Frame size**: 16×16 px per tile
* **Frames per state**: 1 — the tileset is ONE frame of 4×3 tiles
  (64×48), because the map importer takes the tiles from a sprite's
  *first frame*
* **Strip layout**: the tiles in scan order — grass, dirt, stone, brick /
  wood, sand, water, roof / floor, road, snow, lava
* **Anchor**: none (a tileset has no origin)

## Outline rule

* **Outline colour**: index 1 (`#101410`, the darkest in the bank)
* **Width**: 1 px
* **Where**: never on a tile — a border in a tileset repeats in EVERY
  cell of the map (the round-6 field was a striped quilt, the round-7
  cliff a row of boxes). The joints *inside* a tile (the brick mortar,
  the plank seams) are the pattern, not a border, and they tile cleanly.

## Light direction

* **Light comes from**: the top left
* **Highlight**: the lighter tone of each material's ramp
* **Shadow**: the darker tone

## Shading steps

* **Steps per material**: 2 (a base and a speckle/highlight)
* **Ramp order**: dark → light, indices 2..4 (greens), 5..7 (browns),
  8..10 (greys), 11..12 (reds), 13..14 (blues), 15 (cream)
* **Dithering**: none — at 16×16 a dither is noise

## Palette

* **Preset**: `terrain16` (bank 12) — greens, browns, greys, brick reds
  and water blues, with a cream for the snow and the sand
* **Group**: one group (the tileset), one bank
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: each tile reads as its material from the pattern
  alone (the grass has blades, the stone has joints, the water has waves)
* **Minimum feature size**: 1 px (a blade, a star) — a *tile* may carry
  1 px detail, a *character* may not
* **Negative space**: none; a tile is full
* **Never**: a border, a seam that does not wrap, a highlight that
  repeats as a visible line

## Animation

* **Frame counts**: 1 (a tileset does not animate; the game's own
  effects do)
* **Timing**: n/a
* **Baseline**: n/a
* **Facing**: n/a

## Provenance

* **Tool**: procedural (seeded)
* **Script**: `python3 art/library/tiles/gen_tiles.py --out-dir
  art/library/tiles`
* **Licence**: this repository
* **Recorded in**: `tiles.json` (the `provenance` block per asset) and the
  generated `art/ART_LICENSES.md`
