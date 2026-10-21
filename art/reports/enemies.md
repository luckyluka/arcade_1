# Review — the `enemies` pack

Five opponents: a slime, a bat, a rat, a spider and a walking skeleton.

* **Sheet**: `art/library/enemies/strips/` (four critter strips and the
  skeleton's four states)
* **Rig**: `art/rigs/skeleton.json`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**The skeleton is the one that matters, and it works.** A walking enemy is
exactly the case the rig animator was built for: four states (idle, walk,
hurt, dead) out of a file of numbers, and the walk cycle reads at 1x — the
legs and the arms swing in opposition, the body stays upright, and the
feet stay on the ground in every frame. Nothing else in the library is
*this* cheap to animate.

The four critters are the opposite case, and that is the finding: **a blob
is a shape, and a shape is cheap; a character is a rig, and a rig is
numbers.** The slime and the bat are two frames each and they read; the
rat and the spider are the weakest, because a small creature needs more
than a silhouette to be *a* creature.

## Observations (at 1x, then 4x)

1. **The skeleton has no face.** The skull is a white ellipse, and the
   rig cannot draw a face — its shapes are ellipses, capsules and polygons,
   so eye sockets are out of reach. At 1x it reads as a skull; at 4x it
   reads as a blank egg. The fix is a `.px` head composited on top, which
   the format does not do yet.
2. **The bat's wings read as a solid triangle** at 1x — the two frames
   differ only in the wing's height, and at 1x the difference is a row
   or two. The ring now draws the wing's edge, which helps, but the two
   frames are still nearly the same picture.
3. **The rat is the weakest of the four**: a low brown lump with a head
   that is one pixel wider than the body, and the tail is a single pixel.
   It reads as "a thing on the ground" rather than as a rat.
4. **The slime is the best of the critters** — the squash/stretch pair
   sells it, and the two black eyes on the light top are enough to make
   it a *creature* rather than a green blob.

## What changed

* The pack is **new**, and the skeleton arrived as a rig preset
  (`art/rigs/skeleton.json`) rather than as drawn frames — the point of
  the pack is that an enemy is *posed*, not *painted*.
* **Every critter got the 1 px dark ring** (`grow_outline`). The checks
  caught that the first versions had no edge at all — they read as coloured
  lumps, and the outline coverage was 56 % and falling.
* **The rat and the spider grew to 8 px of body** (the readability floor),
  and the spider's legs now start *on* the body: they used to float a pixel
  away, which made the spider two separate pieces.
* The **eyes** (a red pixel each) were in from the start, and they are
  the difference between a lump and a creature.

## Re-check

Every strip passes `arc1-art check` for its role (a sprite), the
skeleton's frames share a baseline, and the pack regenerates byte for byte
(`make art-library-check`).

**What I would do next**: a `.px` head for the skeleton — the rig
positions it, the pixmap draws it — and a **flip**. The bat's two frames
are the same shape with the wings higher, and a mirrored second bat would
double the set for free.
