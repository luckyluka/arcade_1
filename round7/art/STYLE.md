# Art style sheet — round 7: the lighthouse keeper

The art of `round7/` — a lighthouse keeper on a cliff at dusk. Everything is
painted by `round7/art/gen_round7.py` (stdlib only, deterministic); the
strips and the manifest it writes are the deliverables.

## 1. Sprite grid and sizes

* **Frame size**: the keeper 24x32 px; the lamp 16x16 px; the tilesets are
  grids of 16x16 tiles (sky 13x1, tower 6x2, rock 4x2, sea 3x2)
* **Frames per state**: idle 2, climb 4, carry 2, lamp lit 2, lamp dark 1
* **Strip layout**: one horizontal strip per sprite, frames left to right
* **Anchor**: feet — the keeper's origin is `[12, 31]`, the lamp's `[8, 8]`

## 2. Outline rule

* **Outline colour**: index 1 of each bank (the darkest colour in it)
* **Width**: 1 px
* **Where**: outside the silhouette, everywhere (`Canvas.outline()`), so the
  silhouette's edge *is* the outline — the machine's own check reads it
* The tilesets are fully opaque: a background layer has no edge to outline
  (`arc1-art check` says "the outline check does not apply").

## 3. Light direction

* **Light comes from**: top-left
* **Highlight**: the keeper's coat index 8 (`#C6A263`), the rock's facets 14
  (`#ADA2B5`), the sky's moon 9
* **Shadow**: the coat's right side index 4 (`#6B4521`), the rock's facets 13
  (`#211821`)
* **Never**: a second light source; the lamp's flame is the only glow, and it
  is on the lamp

## 4. Shading steps

* **Steps per material**: 3 (dark, mid, light) on the keeper's coat and the
  rock; the sky is flat bands
* **Ramp order**: dark -> light
* **Dithering**: none in the shipped art. The sky was tried as an ordered
  dither and read as stripes at 1x, so it is flat bands instead
* **Rule**: one step of shading per plane

## 5. Palette

| bank | preset | group |
|---|---|---|
| 0 | `dusk16` | the sky |
| 1 | `neutral16` | the tower |
| 2 | `props16` | the cliff |
| 3 | `cool16` | the sea |
| 4 | `warm16` | the keeper |
| 5 | `dusk16` | the lamps |
| 6 | `ui16` | the HUD (no sprite yet) |

* **Deviation from the preset**: none — every bank is a preset verbatim, so
  every colour is distinct as RGB565 by construction
* The lamps share the sky's colours in their own bank: the flame is the only
  warm accent in the game

## 6. Silhouette rules

* **Readable at 1x**: the hat and the long coat carry the keeper; the ladder
  is dark iron against pale stone; the cliff is angular facets
* **Minimum feature size**: 2 px
* **Negative space**: the gap between the arms and the body stays open; the
  face keeps two dark pixels for the eyes and nothing else
* **Never**: salt-and-pepper texture (the rock is drawn as whole shapes),
  thin white lines (the ladder is dark), a lit window where a flame belongs

## 7. Animation

* **Frame counts**: idle 2, climb 4, carry 2, lamp 2
* **Timing**: idle 520 ms, climb 130 ms, carry 320 ms, lamp 140 ms
* **Baseline**: the keeper's boots are on row 30 in every frame of every
  state; the head may bob by one pixel (the idle's breath)
* **Facing**: the keeper is drawn from the front; no mirroring is needed

## 8. Provenance

* **Tool**: procedural
* **Script**: `python3 round7/art/gen_round7.py`
* **Licence**: this repository's
* **Recorded in**: `round7/assets.json` (a `provenance` block per asset)
