# Style sheet — the `icons` pack

Five small gameplay icons: a coin, a heart, a key, a potion and an arrow.
They are the interface layer of a game — what a HUD shows and what the
player picks up.

## Sprite grid and sizes

* **Frame size**: 8×8 px (every icon)
* **Frames per state**: 1 (the icons do not animate)
* **Strip layout**: one frame per strip
* **Anchor**: the bottom centre (`origin: [4, 8]`) — an icon sits on
  whatever it labels

## Outline rule

* **Outline colour**: palette index 1 (`#181018`, the darkest in the bank)
* **Width**: 1 px, drawn as the *outer* ring of the icon
* **Where**: every icon is an outline with a fill inside it

## Light direction

* **Light comes from**: the top left
* **Highlight**: index 4 (a light tone) — the coin's shine, the potion's
  glass
* **Shadow**: index 2 (a deep tone)

## Shading steps

* **Steps per material**: 2 (a light and a mid)
* **Ramp order**: dark → light, indices 2..4
* **Dithering**: none — at 8×8 a dither is noise

## Palette

* **Preset**: `props16` (bank 11) — the vermilion/tan/stone scenery
  palette, borrowed because the icons sit next to the world's props
* **Bank**: 11
* **Group**: one group (the five icons), one bank
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: yes — each icon is a distinct closed shape
* **Minimum feature size**: 2 px (the potion's cork, the key's teeth)
* **Negative space**: the heart's notch and the coin's hole are the only
  holes; everything else is solid
* **Never**: a 1 px feature (it vanishes), a colour outside the bank

## Animation

* **Frame counts**: 1 for all five
* **Timing**: n/a (static)
* **Baseline**: the bottom row of the frame is the icon's foot

## Provenance

* **Tool**: agent-authored (the `.px` text pixmaps in `pixmaps/`)
* **Script**: `python3 art/library/icons/gen_icons.py --out-dir
  art/library/icons`
* **Licence**: this repository
* **Recorded in**: `icons.json` (the `provenance` block per asset) and the
  generated `art/ART_LICENSES.md`
