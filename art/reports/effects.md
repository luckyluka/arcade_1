# Review — the `effects` pack

Five particle strips: dust, smoke, a spark, a splash and a flame.

* **Sheet**: `art/library/effects/strips/` (5 strips of 16x16)
* **Style sheet**: `art/library/effects/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**They work.** Every effect is drawn as a *shape* rather than as a drawn
animation — a puff is a puff at every size, the flame is a flame at every
height — so the set is consistent by construction and cheap to extend.
The dust, the smoke and the splash read at 1x; the spark is a four-pointed
flash that shrinks; the flame has a bright core and a breathing height.

The honest weakness: **the effects are dotty.** A particle made of
single pixels is a particle made of single pixels; the round-5 game's dust
had a shape (a scuff mark) and read better.

## Observations (at 1x, then 4x)

1. **The dust is a scatter of brown pixels** and the smoke is the same
   scatter in a paler tone. At 1x they are the same effect in two
   colours; a puff with a body (a 2x2 core) would separate them.
2. **The splash is the weakest**: five droplets at random x positions
   per frame, so the frames do not read as one motion — the eye expects
   the droplets to travel along arcs.
3. **The flame is the best of the five** — a bright core, an amber body,
   a height that breathes over the four frames, and it reads as fire at
   1x.
4. The spark is a plus sign that shrinks; at 1x it is a star ✓, and its
   first frame's extra centre pixel is what makes it pop.

## What changed

* The pack is **new**: the five effects were painted for the library.
* The **flame was rebuilt** during the review: the first version was a
  three-row band at the bottom of the frame (it drew *down* from the
  top), and the second — a rising flame with a bright core and a
  breathing height — is the one in the pack.
* The **anchors are set** (bottom centre), so a game can place a
  particle on the ground it came off.

## Re-check

All five strips pass `arc1-art check` and the pack regenerates byte for
byte (`make art-library-check`).

**What I would do next**: give the dust and the smoke a body (a 2x2 or
3x3 core with a speckle around it) and give the splash arcs (each
droplet on a parabola) — then the set would read as *motion* rather than
as noise.
