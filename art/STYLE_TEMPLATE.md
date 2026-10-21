# Art style sheet — <asset set name>

Copy this file, fill it in, and keep it beside the assets it describes
(`art/<asset-set>/STYLE.md`). A style sheet is the *contract* between the
art and the machine: it says what the sprites look like, how they move,
and which palette they use. The fields below are required — the lint
(`tests/test_art_palettes.sh`) checks that they are all present.

## 1. Sprite grid and sizes

* **Frame size**: <W>x<H px (all frames of a set share it)
* **Frames per state**: idle <n>, walk <n>, run <n>, jump <n>, fall <n>,
  attack <n>, hurt <n>, dead <n>
* **Strip layout**: one horizontal strip per state, frames left to right
* **Anchor**: <feet | centre> — the point that stays put when the sprite
  moves (the classic is bottom-centre = "feet")

## 2. Outline rule

* **Outline colour**: palette index <n> (index 1 in every preset)
* **Width**: <1> px
* **Where**: <every opaque edge, except where the shape is cut by the
  frame>

## 3. Light direction

* **Light comes from**: <top-left | top-right | top>
* **Highlight**: palette indices <n, m>
* **Shadow**: palette indices <n, m>
* **Never**: a second light source, a rim light on the shadow side

## 4. Shading steps

* **Steps per material**: <n>
* **Ramp order**: dark -> light, indices <n..m>
* **Dithering**: <none | bayer2 | bayer4> — and where it is used
* **Rule**: one step of shading per plane; two at the terminator only

## 5. Palette

* **Preset**: <neutral16 | warm16 | cool16 | dusk16 | forest16 | ui16>
* **Bank**: <n>
* **Group**: <one asset group per bank>
* **Deviation from the preset**: <none, or exactly which index and why>
  — a deviation may also be a **new bank**: write one in the manifest's
  "palettes" when the art needs colours no preset holds (index 0 the
  transparent key, index 1 the outline — the darkest colour in the bank,
  2..15 the ramp, every colour distinct as RGB565)

## 6. Silhouette rules

* **Readable at 1x**: <how — the shape that carries the read>
* **Minimum feature size**: <n> px
* **Negative space**: <what must stay open, and by how much>
* **Never**: <the things that break the read — thin limbs, <2>px gaps>

## 7. Animation

* **Frame counts**: idle <n>, walk <n>, run <n>, jump <n>, fall <n>,
  attack <n>, hurt <n>, dead <n>
* **Timing**: <ms per frame>
* **Baseline**: the lowest opaque pixel is on the same row in every frame
  of a state
* **Facing**: mirrored, never re-drawn

## 8. Provenance

* **Tool**: <procedural | agent-authored | local-ingest>
* **Script**: <the command that regenerates these assets>
* **Licence**: <the licence, or "this repository's">
* **Recorded in**: `assets.json` (the `provenance` block per asset) and
  the generated `art/ART_LICENSES.md`
