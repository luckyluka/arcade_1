# Style sheet — the `faces` pack

Two faces: a samurai's and a skeleton's. They are the rig's **missing
piece** — the rig's shapes are ellipses, capsules and polygons, so it
*cannot* draw a face; its head is a blank oval. A face is drawn on top of
the head instead, and the character looks at you.

## Sprite grid and sizes

* **Frame size**: 8×8 px (every face)
* **Frames per state**: 1 (a face does not animate)
* **Strip layout**: one frame per strip
* **Anchor**: the face is placed by its **top left** corner — a face is
  put on a head by hand, not stood on a row. The manifest still gives
  the sprite an anchor (`origin: [4, 8]`, the bottom centre) so it drops
  into the sprite machinery like any other asset

## Where a face sits

The face is drawn **inside** the rig's head ellipse, so the head's own
outline ring still shows around it:

| face | the rig it goes on | the head | the face goes at |
|---|---|---|---|
| `face_samurai` | the samurai (`art/rigs/samurai.json`) | an 11×12 ellipse at (14, 9); the kabuto covers its top, so the visible skin is rows 8..15 | (10, 8) |
| `face_skeleton` | the skeleton (`art/rigs/skeleton.json`) | a 9×9 ellipse at (12, 7) | (8, 3) |

The face is 8×8 and the head is 9×9 or larger, so the face never covers
the head's ring. A face drawn *over* the ring would erase it, and the
character would lose its outline.

The rig does **not** publish its head's position — these numbers are the
style sheet's job (and the gap is worth closing: see the review).

**The humanoid takes no face.** Its head is a 9×9 ellipse whose visible
skin is only six rows tall — the hair part already covers the top of it
— so an 8×8 face would sit on the hair. A rig with a bald head, or a
smaller face, would have to be built first.

## Outline rule

* **Outline colour**: the body's own outline (index 1 of the bank's
  preset) — the face's eyes and mouth use it
* **Width**: 1 px
* **Where**: **none of its own.** A face is an overlay: it has no outline
  ring, because the head beneath it already has one. The face's
  transparent pixels let the head show through.

## Light direction

* **Light comes from**: the top left — the same as the body it sits on
* **Highlight**: the skin's light tone
* **Shadow**: the brow / the hair band

## Shading steps

* **Steps per material**: 2 (the skin and the brow)
* **Ramp order**: dark → light
* **Dithering**: none — at 8×8 a dither is noise

## Palette

* **Preset**: one per face, so the face matches the body it sits on:
  `hero16` (the samurai, bank 1) and `enemy16` (the skeleton, bank 7)
* **Bank**: the body's bank — a face and its body share one palette
* **Group**: one group per body
* **Deviation from the preset**: none

## Silhouette rules

* **Readable at 1x**: the eyes carry it — two dark marks on a skin oval
* **Minimum feature size**: 2 px (the eyes are 2×2; the skull's nose is
  2 px wide and 1 px tall, the first thing to lose)
* **Negative space**: none — the face is a solid oval of skin
* **Never**: a 1 px feature that is not the skull's nose, a face that
  reaches the head's ring

## Animation

* **Frame counts**: 1 each
* **Timing**: n/a (a face does not move; the *body* moves under it)
* **Baseline**: n/a — a face is placed by hand, not stood on a row
* **Facing**: the sprite is mirrored with the body, so a mirrored face
  still looks at you

## Provenance

* **Tool**: agent-authored (the `.px` text pixmaps in `pixmaps/`)
* **Script**: `python3 art/library/faces/gen_faces.py --out-dir
  art/library/faces`
* **Licence**: this repository
* **Recorded in**: `faces.json` (the `provenance` block per asset) and the
  generated `art/ART_LICENSES.md`
