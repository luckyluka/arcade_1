# Art style sheet — BRICKSTORM (Game #5)

The breakout remake's art: the paddle, the ball, the bricks, the power-up
capsules, the boss and the terrain.

Everything here is **procedural**: `programs/brickstorm_art/gen_art.py`
paints every pixel, and the strips it writes are the build truth. The
generator is the master; `programs/brickstorm_assets.h` is the compiled
header.

## 1. Sprite grid and sizes

* **Frame size**: each sprite has its own size — the ball 8×8, the paddle
  40/56/72×10 (small, medium, large), the bricks 44×14 (40 px of brick
  and a 4 px gap), the power-ups 14×14, the boss 200×72, the boss's core
  20×20
* **Frames per state**: 1 for everything but the ball (2: the glow
  pulses), the tough brick (2: whole, then cracked) and the boss (2: the
  damaged variant)
* **Strip layout**: one horizontal strip per sprite, frames left to right
* **Anchor**: the sprite's centre, except the bricks (the tile's top left
  — the gap is part of the tile)

## 2. Outline rule

* **Outline colour**: palette index 1 in every bank (the darkest colour
  in it)
* **Width**: 1 px, painted as the ring **outside** the silhouette
  (`outline()` — the shape grows by one pixel)
* **Where**: every opaque shape. The brick's gap is transparent, so the
  field reads as bricks and not as a slab

## 3. Light direction

* **Light comes from**: the top left
* **Highlight**: the lighter tone of each bank's ramp (the brick's lit
  top edge, the paddle's shine)
* **Shadow**: the darkest tone of the ramp, along the bottom edge

## 4. Shading steps

* **Steps per material**: 2 (a body and a lit edge)
* **Ramp order**: dark → light, indices 2..5
* **Dithering**: none — the shapes are small and the ramps are short

## 5. Palette

Eleven banks, one per role:

| bank | palette | what it carries |
|---|---|---|
| 0..4 | `row0`..`row4` | the brick rows: red, orange, yellow, green, blue |
| 5 | `hero` | the paddle, the ball and the steel brick |
| 6..8 | `pow_a`..`pow_c` | the power-up capsules, two sprites per bank |
| 9 | `boss` | the boss and its cores |
| 10 | `fx` | the bomb brick and the sparks |

* **Group**: one group per bank
* **Deviation from the preset**: none — the banks are the game's own
  ramps, written out in the generator. Every colour in a bank is
  distinct under the 5/6/5 quantisation (assetc refuses the ones that
  are not ✓)

## 6. Silhouette rules

* **Readable at 1x**: the brick's bevelled edge carries the row's colour;
  the paddle is a lit bar; the boss is a wide hull with three cores
* **Minimum feature size**: 2 px
* **Negative space**: the 4 px gap between the bricks
* **Never**: a brick that touches its neighbour (the field must read as
  bricks), a core smaller than 20 px (the boss's weak point has to be
  hittable — a fight, not a lottery)

## 7. Animation

* **Frame counts**: the ball 2, the tough brick 2, the boss 2, the boss's
  core 2, everything else 1
* **Timing**: the ball's glow every 6 frames, the core every 8
* **Baseline**: the frames of a state stand on the same row — the ball's
  glow and the core's pulse change in colour, never in size

## Provenance

* **Tool**: procedural
* **Script**: `python3 programs/brickstorm_art/gen_art.py --dir
  programs/brickstorm_art --manifest brickstorm_assets.json`
* **Licence**: this repository
* **Recorded in**: `brickstorm_assets.json` (the `provenance` block per
  asset) and the generated `art/ART_LICENSES.md`
