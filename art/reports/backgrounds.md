# Review — the `backgrounds` pack

What is behind the game: three parallax ridge strips, a tileable star
field, a cloud, a moon, and two skies — a dusk one and a day one.

* **Sheet**: `art/library/backgrounds/strips/` (three 64x16 ridges, a
  32x32 star field, a 32x16 cloud, a 16x16 moon, and two sets of the
  sky's seventeen band tiles — the dusk ones and the day ones) and
  `art/library/backgrounds/maps/` (`sky_dusk.txt`, `sky_day.txt`)
* **Style sheet**: `art/library/backgrounds/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**The ridges are the useful part, and they work.** Each is a seamless
silhouette built from a sum of sines, so the strip's ends match and a
layer can be scrolled forever by drawing it twice. The three are ordered
far → mid → near with a lit edge each, and the far one is the lightest:
distance washes out, and that is what sells the depth.

The sky came with this pack and it is the part a game actually reaches
for: seventeen dithered band tiles and one tilemap call, instead of the
seventeen rectangles every game was writing by hand. The honest
weakness: **the sky was one hour of the day** — the dusk ramp — and a
game that wanted noon had to write its own gradient. The cloud is the
other piece that does not earn its place: a single blob in a strip of
sky.

## Observations (at 1x, then 4x)

1. **The ridges read as hills at 1x** ✓ — the profile alone says "far
   away", and the three layers separate by tone, not by detail ✓.
2. **The far ridge's profile is the busiest** (three sines against two
   and one), which is backwards: the far layer should be the *simplest*.
3. **The stars tile** — 32x32, and the field wraps ✓, so a sky can be
   filled with one draw per screen.
4. The moon is a crescent and reads as one at 1x ✓, though it is
   drawn with a hard edge (no glow) and so looks cut out.
5. **The day sky is the same seventeen bands over a different ramp** —
   the deep blue at the top, the pale blue in the middle, the warm haze
   at the horizon. Because the *shape* of the gradient is shared, the
   two skies can be swapped by changing the tileset, and the dusk and
   the day skies line up cell for cell.
6. **The day ramp needed a new preset** (`day16`, bank 8): the cool
   blues and the warm horizon are in no palette the library had. The
   dusk ramp is a night sky; it cannot be brightened into a day one
   without going through grey.
7. The two skies **cost two palette banks**, which is the price of the
   hour of the day: a game that ships one of them pays for one.

## What changed

* The pack is **new**.
* The **day sky** was added: the `day16` palette preset, the
  `sky_bands_day` tileset and the `sky_day` map — the piece this review
  had named as the next thing to build.
* The **ridge profiles were re-balanced** during the review: the first
  version gave the far ridge the most detail and the near ridge the
  least, which read as a flat cutout wall. Now the far ridge is the
  simplest and the nearest has the most.
* The style sheet says **how the ridges are used** (drawn twice, at the
  camera's offset and at the offset plus the strip's width) — the
  round-5 game did that and the pattern is worth writing down.

## Re-check

Every strip passes `arc1-art check` and the pack regenerates byte for
byte (`make art-library-check`).

**The sky came with this review** (it was the pack's missing piece): the
seventeen band tiles are dithered against their neighbours, so the
gradient has no steps, and the map stacks them — one `gfx_tilemap4` call
for a whole sky, instead of the seventeen `gfx_rect` calls every game was
writing by hand. The bands' colours are the dusk ramp the round-5 world
already used, so the sky and the ridges agree; the day sky is the same
gradient over the day ramp, and it agrees with itself.

**What I would do next**: a second cloud shape (a sky should not be one
blob) and a third sky — a dawn one, over the dusk ramp's warm end and
the day ramp's cold end. The two skies now in the pack prove the
mechanism; the third would prove it is a *library* and not a pair of
special cases.
