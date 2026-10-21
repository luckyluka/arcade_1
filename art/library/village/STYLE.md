# Style sheet — the `village` pack

*(This pack is the round's art, brought into the
library. The style sheet is the round's own.)*


The art set of `round6/game.c`: a farmer, a slime enemy, four pickups and
a three-tile ground, in one screen. Copy of `art/STYLE_TEMPLATE.md`,
filled in. The generator is `art/library/village/gen_village.py`; it writes every
file listed here and nothing is hand-edited.

## 1. Sprite grid and sizes

* **Frame size**: the farmer 16x24; the slime 16x16; the pickups 12x12;
  the ground tileset 48x16 (3 tiles of 16x16, **one frame**).
* **Frames per state**: the farmer walks in 2; the slime hops in 2; the
  pickups have 1; the ground has 1.
* **Strip layout**: one horizontal strip per sprite, frames left to right.
* **Anchor**: `feet` — the farmer's origin is (8, 24), the slime's (8, 16),
  the pickups' (6, 12); the ground's is (0, 0).

## 2. Outline rule

* **Outline colour**: palette index 1 in every bank (`#101018` the
  farmer, `#101808` the slime, `#1A1218` the props, `#1B1B22` the ground).
* **Width**: 1 px.
* **Where**: outside the silhouette, on every opaque edge. The index 1
  colour is the **darkest** colour of its bank, so the machine's own
  check (which infers the outline as the darkest colour in the frame)
  agrees with the art.

## 3. Light direction

* **Light comes from**: top-left.
* **Highlight**: the farmer's left shirt column (4) and the hat's crown
  (10); the slime's shine (12); the pickups' shine pixels (5, 8, 14).
* **Shadow**: the farmer's right shirt column (2) and the face's right
  side (8); the slime's bottom rows (4); the ground's darker blades (2)
  and the flagstone joints (6).
* **Never**: a second light source, a rim light on the shadow side.

## 4. Shading steps

* **Steps per material**: 2 (a light step and a shadow step); the farmer
  has 3 on the shirt (light, base, shadow).
* **Ramp order**: dark -> light.
* **Dithering**: none — the shapes are small and a dither reads as noise.
* **Rule**: one step of shading per plane.

## 5. Palette

* **Preset**: the farmer uses `hero16` verbatim, the slime uses
  `forest16` verbatim; the pickups and the ground use two **custom**
  banks (`props`, `ground`).
* **Bank**: farmer 1, slime 2, props 3, ground 4.
* **Group**: one bank per asset group — one character, one tileset, one
  background layer.
* **Deviation from the preset**: none for `hero16` and `forest16`. The
  two custom banks exist because no preset holds a red and a green
  together (the four pickups) or water and earth together (the ground);
  both follow the preset shape: index 0 is the key `#000000`, index 1 is
  the outline, 2..15 are the ramp, all 16 distinct as RGB565 and index 1
  the darkest.

## 6. Silhouette rules

* **Readable at 1x**: the farmer reads by the hat brim and the apron;
  the slime by its dome and its eyes; the coin, the apple, the carrot and
  the cabbage each have one unmistakable shape.
* **Minimum feature size**: 2 px — the farmer's eyes are 1x1 but sit in
  a 6 px face; the pickups' features are 2 px and up.
* **Negative space**: the 3 px gap between the farmer's legs keeps a
  transparent line, so the legs read as two legs; the slime's eyes are
  pale, not holes.
* **Never**: thin limbs, gaps under 2 px, a second outline colour.

## 7. Animation

* **Frame counts**: the farmer walks in 2 (160 ms a step), the slime hops
  in 2 (240 ms a step).
* **Timing**: 160 ms and 240 ms; the farmer's step is quick, the slime's
  is lazy.
* **Baseline**: the lowest opaque pixel is on row 23 in both farmer frames
  and on row 14 in both slime frames.
* **Facing**: mirrored, never re-drawn (`gfx_flip`).

## 8. Provenance

* **Tool**: procedural.
* **Script**: `python3 art/library/village/gen_village.py` (run under
  `ulimit -v 900000`), then
  `python3 tools/arc1_assetc.py --assets round6/assets.json --out-dir round6 --report`.
* **Licence**: this repository.
* **Recorded in**: `round6/assets.json` (the `provenance` block per
  asset) and, when the licences are regenerated, in
  `art/ART_LICENSES.md`.
