# Review — the `tiles` pack

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
