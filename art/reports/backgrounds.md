# Review — the `backgrounds` pack

What is behind the game: three parallax ridge strips, a tileable star
field, a cloud and a moon.

* **Sheet**: `art/library/backgrounds/strips/` (three 64x16 ridges, a
  32x32 star field, a 32x16 cloud, a 16x16 moon)
* **Style sheet**: `art/library/backgrounds/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**The ridges are the useful part, and they work.** Each is a seamless
silhouette built from a sum of sines, so the strip's ends match and a
layer can be scrolled forever by drawing it twice. The three are ordered
far → mid → near with a lit edge each, and the far one is the lightest:
distance washes out, and that is what sells the depth.

The honest weakness: **the sky is not in this pack.** A game still has to
draw its own gradient (a rectangle per band) and place the star field
and the moon itself. The cloud is the one piece that does not earn its
place — a single blob in a strip of sky.

## Observations (at 1x, then 4x)

1. **The ridges read as hills at 1x** ✓ — the profile alone says "far
   away", and the three layers separate by tone, not by detail ✓.
2. **The far ridge's profile is the busiest** (three sines against two
   and one), which is backwards: the far layer should be the *simplest*.
3. **The stars tile** — 32x32, and the field wraps ✓, so a sky can be
   filled with one draw per screen.
4. **The moon is a crescent** and reads as one at 1x ✓, though it is
   drawn with a hard edge (no glow) and so looks cut out.

## What changed

* The pack is **new**.
* The **ridge profiles were re-balanced** during the review: the first
  version gave the far ridge the most detail and the near ridge the
  least, which read as a flat cutout wall. Now the far ridge is the
  simplest and the nearest has the most.
* The style sheet says **how the ridges are used** (drawn twice, at the
  camera's offset and at the offset plus the strip's width) — the
  round-5 game did that and the pattern is worth writing down.

## Re-check

All six strips pass `arc1-art check` and the pack regenerates byte for
byte (`make art-library-check`).

**What I would do next**: a **sky** piece — the gradient is the one
thing every game draws by hand — and a second cloud shape, so a sky is
not one blob.
