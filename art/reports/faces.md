# Review — the `faces` pack

Two faces for the rigged characters: a samurai's and a skeleton's. The
rig cannot draw a face — its shapes are ellipses, capsules and polygons,
so the head is a blank oval — and this pack is the answer: the rig gives
the body, a `.px` pixmap gives the face, the game draws one over the
other.

* **Sheet**: `art/library/faces/strips/` (two strips of 8×8)
* **Source**: `art/library/faces/pixmaps/` (two `.px` text pixmaps)
* **Style sheet**: `art/library/faces/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**It works, and it is the smallest thing in the library.** The samurai
stands there with a kabuto, a gold maedate and — for the first time — a
face: two dark eyes and a mouth on the skin under the helmet. The
skeleton gets the same treatment in bone white, with the sockets where
the eyes should be. Eight pixels of `.px` text each, and the difference
between "a shape that walks" and "a character".

The honest weakness: **the rig does not tell you where the face goes.**
The head's position is a number in the style sheet, typed by hand, and a
game that changes the rig (a bigger kabuto, a new preset) has to
re-measure it. That is a gap in the *rig*, not in the faces — the rig
should publish its head's centre the way it publishes its frames.

## Observations (at 1x, then 4x)

1. **The eyes carry the whole face.** At 1x — the size the machine shows
   them — the two 2×2 dark marks are all that says "face": the mouth is
   four pixels in a row and it reads as a mouth only because the eyes
   have set the expectation. A face without eyes would be a blank oval,
   which is exactly what the rig draws.
2. **The samurai's face is a rectangle of skin.** The head under it is
   an ellipse, and the face is a 8×8 box that fills the visible part of
   it, so the head's curve survives only in the four corners the face
   leaves transparent. It reads, but a round face would read better.
3. **The skeleton's nose is the weakest mark in the pack**: 2 px wide
   and 1 px tall, in the middle of a white oval. It is the first thing
   that would disappear at a smaller scale, and it is the only feature
   that makes the skull a skull rather than a headless body.
4. The **palette sharing is what makes the pack cheap**: a face and its
   body use one bank between them, so a face costs no palette bank at
   all and 32 bytes of ROM (64 pixels at 4 bits each).

## What changed

* The pack is **new** — the rig's missing piece, in two faces.
* The **first draft had three faces** (a samurai, a skull and a
  villager). The villager was cut: the humanoid's head is a 9×9 ellipse
  whose visible skin is six rows — the hair part already covers the top
  — so an 8×8 face would have sat on the hair. Two faces that go on
  real rigs are worth more than three that do not.
* The **faces are overlays, not sprites**: the pack's manifest gives
  them the role `overlay`, so `arc1-art check` does not grade them as
  standalone sprites (a face has no outline ring of its own — the head's
  ring shows around it — and the sprite rules would fail it for that).
* The **palettes are shared on merge**: a pack whose palette is the same
  name with the same colours now shares the bank instead of taking a
  second one, so the merged header's names do not depend on the order
  the packs were merged in.

## Re-check

The composition is proved end to end by `tests/test_art_faces.sh`: the
packs merge, the merged manifest compiles, a guest draws two rigs with a
face on each, and the test counts the dark pixels on each head (13 on
the samurai's, 12 on the skeleton's) — a rig's head alone has none.

```
  ok: the packs merge into one manifest (the palettes are shared)
  ok: the merged manifest compiles
  ok: the rig bodies and the pixmap faces compose (13 and 12 dark pixels on the heads)
```

**What I would do next**: publish the head's centre from the rig (a
`head` entry in the rig preset, emitted as a constant beside the
frames), so the face's position is derived rather than measured; and
then give the humanoid a bald variant, which would take a third face.
