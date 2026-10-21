# Style sheet — the `backgrounds` pack

What is behind the game: three parallax ridge strips, a tileable star
field, a cloud and a moon.

## Sprite grid and sizes

* **Frame size**: the ridges are 64×16 (horizontal strips — a silhouette
  that repeats), the stars 32×32 (tileable), the cloud 32×16, the moon
  16×16
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

* **Preset**: `dusk16` (bank 15) — the night sky's violets and indigos,
  with the warm band for the horizon
* **Group**: one group (the backgrounds), one bank
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
