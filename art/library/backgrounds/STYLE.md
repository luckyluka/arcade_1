# Style sheet — the `backgrounds` pack

What is behind the game: three parallax ridge strips, a tileable star
field, a cloud, a moon, and **two skies** — a dusk one and a day one.

## Sprite grid and sizes

* **Frame size**: the ridges are 64×16 (horizontal strips — a silhouette
  that repeats), the stars 32×32 (tileable), the cloud 32×16, the moon
  16×16, the sky's band tiles 16×16
* **Frames per state**: 1 each
* **Strip layout**: one frame per strip
* **Anchor**: none (a background layer is drawn at the camera's offset)

## Outline rule

* **Outline colour**: index 1 (`#080C18`, the darkest in the bank)
* **Width**: 1 px
* **Where**: the ridge's lit edge (one pixel along the silhouette) — not
  around the whole strip, which would draw a box in the sky

## Light direction

* **Light comes from**: the top left — the ridges are lit from the left
* **Highlight**: the edge tone of each ridge (the far ridge's is the
  lightest of the three: distance washes out)
* **Shadow**: the ridge's body

## Shading steps

* **Steps per material**: 2 (a body and a lit edge)
* **Ramp order**: dark → light, indices 2..7 (the night's violets)
* **Dithering**: none

## Palette

* **Preset**: `dusk16` (bank 3) for the ridges, the stars, the cloud and
  the moon — the night sky's violets and indigos, with the warm band for
  the horizon; `day16` (bank 8) for the day sky — the deep blue at the
  top, the pale blue in the middle, the warm haze at the horizon
* **Group**: one group per palette (the dusk background, the day sky)
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: a ridge reads as a ridge from its profile alone
* **Minimum feature size**: 1 px (a star)
* **Negative space**: the sky above a ridge is empty
* **Never**: a ridge that does not wrap — the strip repeats, so the left
  and right ends must match

## Animation

* **Frame counts**: 1 each (the parallax comes from the camera, not from
  frames)
* **Timing**: n/a
* **Baseline**: the ridges sit on the bottom row of their strip
* **Facing**: n/a

## The sky

`sky_bands` is seventeen 16x16 **band tiles**, each dithered against its
neighbour, and `sky_dusk` is the map that stacks them (30x17 cells — the
screen's width and height in tiles). The dither is the point: seventeen
solid bands would show seventeen steps, and seventeen dithered bands read
as one gradient.

**The day sky** is the same seventeen bands over the day ramp:
`sky_bands_day` (the tileset) and `sky_day` (the map), on the `day16`
palette at bank 8. The dusk and the day skies are the same *shape* —
deep at the top, warm at the horizon — over two different ramps, so a
game can change the hour of the day without touching its tilemap code:

| band (top → horizon) | the dusk sky | the day sky |
|---|---|---|
| 1..2 | the night's indigo | the deep blue |
| 3..8 | the violets | the blues |
| 9..14 | the pales | the pale blues |
| 15..17 | the warm band | the warm haze |

Both maps are 30×17 cells, so a game can draw either with one call and
switch between them by changing the tileset.

**How the sky is drawn.** The device cannot stretch a sprite, so a
gradient is a *tilemap*, not a scaled picture:

```c
arcade1_cam(0, 0);            /* the sky is screen-space */
arcade1_tilemap4(0, 0, sky_dusk_cells, sky_dusk_tiles,
                 SKY_DUSK_COLS, SKY_DUSK_ROWS, 16, 16);
/* ... then the world, with the camera where the game wants it */
```

One draw call for the whole sky. (The camera is a register, so it can be
set for the sky and set again for the world.)

## How the ridges are used

A parallax layer is drawn twice: at the camera's offset and at the
camera's offset plus the strip's width, so the layer scrolls forever. The
three ridges are ordered far → mid → near, and the game picks the
scroll rate for each (the round-5 game used 1/4, 1/2 and 3/4).

## Provenance

* **Tool**: procedural (seeded)
* **Script**: `python3 art/library/backgrounds/gen_backgrounds.py
  --out-dir art/library/backgrounds`
* **Licence**: this repository
* **Recorded in**: `backgrounds.json` and the generated
  `art/ART_LICENSES.md`
