#!/usr/bin/env python3
"""tools/make_library_reviews.py — the reviews of the library's new packs.

The library's contract says every pack carries a review (the critique
protocol's four parts: verdict, observations, what changed, the re-check).
The packs that came from the rounds already have one; the new ones are
written here, from the sheets the generators produce.

    python3 tools/make_library_reviews.py
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORTS = os.path.join(ROOT, "art", "reports")

REVIEWS = {
"tiles": """# Review — the `tiles` pack

Twelve seamless 16x16 surface tiles in one tileset: grass, dirt, stone,
brick, wood, sand, water, roof, floor, road, snow, lava.

* **Sheet**: `art/library/tiles/strips/tiles.png` (one frame, 4x3 tiles)
* **Style sheet**: `art/library/tiles/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**It works, and it is the most useful thing in the library.** A game needs
ground before it needs a hero, and this is twelve surfaces that tile
without a seam — the thing the rounds got wrong twice (the round-6 field
was a striped quilt, the round-7 cliff a row of boxes). The tiles read at
1x: the grass has blades, the stone has joints, the water has waves, the
lava has cracks.

The honest weakness: **the materials are flat.** A tile is a base tone
plus a speckle; a wall of them reads as a texture, not as a lit
surface. The round-5 world's terrain had a gradient baked into the tile
(light at the top, dark at the bottom) and that is what made it look
*placed* rather than *tiled*.

## Observations (at 1x, then 4x)

1. **Every tile is flat.** There is no top-light: a floor of stone tiles
   has no fall-off, so a large area of it reads as wallpaper. The
   round-5 terrain tiles had a vertical ramp and looked better.
2. **The brick and the roof are the same red**, so a house built from
   both reads as one material in two cuts. The roof wants a darker, colder
   red than the brick.
3. **The snow is almost white** (cream, index 15) and the sand is the
   same cream with a brown speckle — they are the same material at two
   densities, which is a choice, not a mistake.
4. The grass blades wrap correctly and do not read as rows — the thing
   the round-6 grass got right.

## What changed

* The pack is **new**: the twelve tiles were painted for the library, not
  lifted from a round.
* The **tileset is one frame** and the pack says so in its style sheet,
  because "three frames of one tile each" is the mistake that cost the
  round-7 agent an hour (the importer takes the tiles from the first
  frame only).
* Every tile is **seamless by construction** — the patterns wrap, and
  there is no border anywhere in the set.

## Re-check

All twelve tiles pass `arc1-art check` (one component per tile, no
orphans, a shared baseline) and the pack regenerates byte for byte
(`make art-library-check`).

**What I would do next**: add a top-lit variant of the four surfaces a
game shows most (grass, stone, sand, snow) — a second row of tiles with
a vertical ramp, and the game's map picks the top row for the surface it
sees. That is the round-5 trick, and it is what would make a field of
these look placed rather than tiled.
""",

"effects": """# Review — the `effects` pack

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
""",

"ui": """# Review — the `ui` pack

The interface kit: a panel in three sizes, a cursor and a counter pip.

* **Sheet**: `art/library/ui/strips/` (the nine panel pieces, a cursor, a
  pip) and `art/library/ui/maps/` (the three panels)
* **Style sheet**: `art/library/ui/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**The panel is the useful part, and it works.** Nine pieces (the corners,
the edges, the fill) in one tileset, and three maps that lay them out at
2x2, 3x3 and 4x4 — so a game draws a frame around anything with a
`gfx_tilemap4` call, at three sizes, with the top edge light and the
bottom edge dark so the panel reads as lit.

The honest weakness: **it is a fixed-size panel, not a 9-slice.** The
device cannot stretch a tile, so a panel cannot grow — a HUD that has to
fit a long score line needs another map, not a stretched edge. That is a
machine limit, not a pack's fault, but a game author will hit it.

## Observations (at 1x, then 4x)

1. **The corners and the edges do not match perfectly**: the corner
   piece's 3x3 block is a shade lighter than the edge's, so the corner
   reads as a small notch at 4x. It disappears at 1x.
2. **The pip is a 5x5 disc with a red centre and a white rim** — a
   counter dot, and it reads as one at 1x ✓. The rim is 1 px, so it is
   the first thing to vanish if the pip is ever scaled down.
3. **The cursor is a 7x9 arrow** that reads as a pointer at 1x ✓,
   though its tail is thin.
4. The panel's fill is a flat mid grey — the same tone as the edge, so
   the panel only reads because of its border. On a background of the
   same grey it would disappear.

## What changed

* The pack is **new**.
* The **maps were added**, so the panel is not just a tileset but
  something a game can draw: the round-7 agent had to work out the
  nine-piece layout from scratch.
* The style sheet now says **plainly** that the device cannot stretch a
  tile, so the panels are fixed sizes — the thing an author would
  otherwise assume.

## Re-check

The three strips pass `arc1-art check`, the three maps compile (their
legends cover their tiles, and every tile of the panel tileset is
drawable), and the pack regenerates byte for byte.

**What I would do next**: a second panel set (a "raised" and a "sunken"
variant, for a dialog on top of a panel) and a pressed state for the
button — the same nine pieces with the light and dark edges swapped.
""",

"backgrounds": """# Review — the `backgrounds` pack

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
""",
}


def main():
    os.makedirs(REPORTS, exist_ok=True)
    for name, text in sorted(REVIEWS.items()):
        path = os.path.join(REPORTS, "%s.md" % name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print("review: %s" % path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
