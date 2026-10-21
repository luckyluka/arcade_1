# Style sheet — the `characters` pack

Two **rigged** characters: the humanoid (the rig animator's reference) and
the samurai (Petal Blade's hero). A rig is the library's character format:
parts, keyframes and poses — numbers in one file — so a game can change a
walk cycle without touching the art, and every frame of every state is the
same character by construction.

## Sprite grid and sizes

* **Frame size**: 24×32 px for the humanoid, 28×40 px for the samurai
* **Frames per state**: idle 4, walk 8, run 8, jump 1, fall 1, attack 4,
  hurt 1, dead 1
* **Strip layout**: one horizontal strip per state, frames left to right
* **Anchor**: the feet — the bottom centre of the frame (`origin: [w/2, h]`)

## Outline rule

* **Outline colour**: palette index 1 (the darkest in the bank)
* **Width**: 1 px, painted as the ring *outside* the silhouette
  (`grow_outline` — the sprite comes out 1 px larger in every direction)
* **Where**: every opaque shape

## Light direction

* **Light comes from**: the top left
* **Highlight**: the lighter tones of the ramp
* **Shadow**: the `shade` tone each part carries — the shape is drawn in
  the shade colour and again one pixel up and left in the main colour, so
  the shadow side is what is left over

## Shading steps

* **Steps per material**: 2 (a main tone and one shade)
* **Ramp order**: dark → light, indices 2..9
* **Dithering**: none

## Palette

* **Preset**: the humanoid uses `dusk16` (bank 0) and the samurai
  `hero16` (bank 1) — the round-5 hero palette, verbatim
* **Group**: one bank per character
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: the silhouette carries the pose; the face does not
* **Minimum feature size**: 2 px
* **Negative space**: the gap between the legs (the outline pass closes a
  2 px gap — leave 3 px)
* **Never**: a limb thinner than 3 px, a head larger than a quarter of
  the body

## Animation

* **Frame counts**: idle 4, walk 8, run 8, jump 1, fall 1, attack 4,
  hurt 1, dead 1
* **Timing**: idle 200 ms, walk 120 ms, run 80 ms, jump 150 ms, fall 150 ms,
  attack 100 ms, hurt 200 ms, dead 300 ms
* **Baseline**: the lowest opaque pixel is the same row in every frame of a
  state (the frames are aligned to the baseline when they are rendered)
* **Facing**: the sprite is mirrored, never re-drawn

## Provenance

* **Tool**: procedural (the rig animator)
* **Script**: `python3 art/library/characters/gen_characters.py --out-dir
  art/library/characters` — the rigs themselves are `art/rigs/humanoid.json`
  and `art/rigs/samurai.json`
* **Licence**: this repository
* **Recorded in**: `characters.json` (the `provenance` block per asset) and
  the generated `art/ART_LICENSES.md`
