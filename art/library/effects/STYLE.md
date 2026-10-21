# Style sheet — the `effects` pack

Five particle strips: dust, smoke, a spark, a splash and a flame. The
things a game throws around when something happens.

## Sprite grid and sizes

* **Frame size**: 16×16 px
* **Frames per state**: dust 3, smoke 3, spark 3, splash 3, fire 4
* **Strip layout**: one horizontal strip per effect, frames left to right
* **Anchor**: the bottom centre (`origin: [8, 16]`) — a particle sits on
  the ground it came off

## Outline rule

* **Outline colour**: index 1 (`#181421`, the darkest in the bank)
* **Width**: none — an effect is soft, and an outline would make it read
  as a solid object
* **Where**: nowhere

## Light direction

* **Light comes from**: the top left
* **Highlight**: the core of the flame and the flash (creams and white)
* **Shadow**: the outer edge of the dust (browns)

## Shading steps

* **Steps per material**: 2 (a core and a body)
* **Ramp order**: dark → light, indices 2..5 (earths and ambers), 9..10
  (the whites), 11..14 (the blues and the reds)
* **Dithering**: none

## Palette

* **Preset**: `fx16` (bank 13) — the round-5 effects palette: earths and
  ambers for the dust, the creams for the smoke and the flash, the
  oranges for the flame, the blues for the splash
* **Group**: one group (the effects), one bank
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: a particle is a shape, not a picture — it must
  read as "something is happening here" at a glance
* **Minimum feature size**: 1 px (a droplet, a spark arm)
* **Negative space**: most of a particle is empty
* **Never**: a shape that reads as a character or an item

## Animation

* **Frame counts**: 3 (4 for the flame)
* **Timing**: dust 120 ms, smoke 140 ms, spark 70 ms, splash 110 ms,
  flame 90 ms
* **Baseline**: the particles do not share a baseline (they move) — they
  share a *size*
* **Facing**: n/a

## Provenance

* **Tool**: procedural (seeded)
* **Script**: `python3 art/library/effects/gen_effects.py --out-dir
  art/library/effects`
* **Licence**: this repository
* **Recorded in**: `effects.json` and the generated `art/ART_LICENSES.md`
