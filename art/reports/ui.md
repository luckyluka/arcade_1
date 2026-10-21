# Review — the `ui` pack

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
