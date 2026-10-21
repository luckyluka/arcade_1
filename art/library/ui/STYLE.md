# Style sheet — the `ui` pack

The interface kit: a panel in three sizes, a cursor and a counter pip.

## Sprite grid and sizes

* **Frame size**: the panel pieces are 16×16 (a 3×3 tileset, ONE frame,
  48×48); the cursor is 7×9; the pip is 5×5
* **Frames per state**: 1 each
* **Strip layout**: one frame per strip
* **Anchor**: none (the panel is a map; the cursor and the pip are drawn
  where the game wants them)

## Outline rule

* **Outline colour**: index 1 (`#101018`, the darkest in the bank)
* **Width**: 1 px
* **Where**: the panel's border (a *piece* of a panel, so a border here
  is the panel's edge, not a tileset border) and the cursor's edge

## Light direction

* **Light comes from**: the top left — the panel's top edge is the lighter
  one, its bottom edge the darker
* **Highlight**: index 6 (the light edge)
* **Shadow**: index 2 (the dark fill)

## Shading steps

* **Steps per material**: 2 (a fill and an edge)
* **Ramp order**: dark → light, indices 2..6
* **Dithering**: none

## Palette

* **Preset**: `ui16` (bank 14) — greys from near-black to white, with
  red, amber, green and blue for the signal colours
* **Group**: one group, one bank
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: the panel's edge must be visible against whatever
  is behind it — hence the light top edge and the dark bottom one
* **Minimum feature size**: 1 px (the pip's rim)
* **Negative space**: the panel's fill is flat
* **Never**: a panel edge that disappears into a same-toned background

## Animation

* **Frame counts**: 1 each (a button's "pressed" state is a second map)
* **Timing**: n/a
* **Baseline**: n/a
* **Facing**: n/a

## How the panel is used

The device cannot stretch a tile, so the panel is **fixed sizes**: the
tileset holds the nine pieces (the corners, the edges, the fill) and each
map lays them out — `panel_small` (2×2), `panel_mid` (3×3), `panel_big`
(4×4). A game draws it with `gfx_tilemap4`.

## Provenance

* **Tool**: procedural
* **Script**: `python3 art/library/ui/gen_ui.py --out-dir art/library/ui`
* **Licence**: this repository
* **Recorded in**: `ui.json` and the generated `art/ART_LICENSES.md`
