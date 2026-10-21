# Style sheet — the `enemies` pack

Five opponents: a slime, a bat, a rat, a spider and a walking skeleton.

Four are drawn as *shapes* (a blob that squashes, a wing that beats, a
body that scurries, legs that lift); the skeleton is a **rig**
(`art/rigs/skeleton.json`), because a walking enemy is exactly what the
rig animator is for.

## Sprite grid and sizes

* **Frame size**: the slime and the bat 16×16, the rat 16×12, the spider
  12×12, the skeleton 24×32
* **Frames per state**: the slime 2 (squash, stretch), the bat 2 (wings
  up, wings down), the rat 2 (the legs swap), the spider 2 (two legs
  lift), the skeleton: idle 2, walk 4, hurt 1, dead 1
* **Strip layout**: one horizontal strip per enemy, frames left to right
* **Anchor**: the bottom centre — an enemy stands on the ground

## Outline rule

* **Outline colour**: index 1 (`#101410`, the darkest in the bank)
* **Width**: 1 px, painted as the ring *outside* the silhouette
* **Where**: every enemy, the wings of the bat included — a sprite with
  no edge does not read at 1x (the checks caught exactly that: the first
  critters had no ring and read as lumps)

## Light direction

* **Light comes from**: the top left
* **Highlight**: the slime's top (`#7BB25A`), the rat's back, the skull
* **Shadow**: the underside of each body

## Shading steps

* **Steps per material**: 2 (a body and a light)
* **Ramp order**: dark → light, indices 2..4 (the slime), 5..7 (the
  rodents), 8..10 (the spider), 11..12 (the bones)
* **Dithering**: none

## Palette

* **Preset**: `enemy16` (bank 16) — the slime's greens, the rodents'
  browns, the spider's blacks, the skeleton's bones, the eyes' red
* **Group**: one group (the enemies), one bank
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: the *shape of the motion* carries the enemy — a
  slime is a dome, a bat is a pair of wings, a rat is low and long, a
  spider is round with legs
* **Minimum feature size**: 1 px (an eye, a leg)
* **Negative space**: between the bat's wings and under the rat
* **Never**: an enemy that reads as a character (the eyes are the only
  face a blob gets)

## Facing

Every enemy is drawn **facing right**. The device mirrors a sprite for
free (`sprite4`'s flags bit0 = flip X), so one sprite is two facings —
`tests/test_art_flip.sh` proves the flip with a guest that draws an
asymmetric shape plain and mirrored. Nothing in this pack needs a second
drawing.

## Animation

* **Frame counts**: 2 for the four critters, 4 for the skeleton's walk
* **Timing**: slime 160 ms, bat 110 ms, rat 150 ms, spider 130 ms, the
  skeleton 140 ms a step
* **Baseline**: the critters stand on the ground in every frame; the
  skeleton's feet do too (the rig aligns the frames to the baseline)
* **Facing**: the sprites are mirrored, never re-drawn

## Provenance

* **Tool**: procedural (seeded) for the four critters; the rig animator
  for the skeleton
* **Script**: `python3 art/library/enemies/gen_enemies.py --out-dir
  art/library/enemies`
* **Licence**: this repository
* **Recorded in**: `enemies.json` (the `provenance` block per asset) and
  the generated `art/ART_LICENSES.md`
