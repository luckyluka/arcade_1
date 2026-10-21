# Review — the `village` pack

*(The round's own review, kept as the pack's. The art
is unchanged; the pack is the same art, tidied.)*


The visual review of the round 6 art set, in the form `art/CRITIQUE.md`
asks for. The files it looks at:

* `round6/art/strips/*.png` — the 7 strips (the machine's half:
  `arc1-art check`).
* `round6/art/preview_map.png` (2x), `round6/art/preview_map_1x.png` (1x)
  and `round6/art/preview_sprites.png` (4x) — what the eye looks at.
  They are written by `round6/art/preview.py`, a scratch tool, not a
  shipped asset.

## Verdict

It works: the field reads as a village at 1x (grass, a paved square, a
road, a pond) and the farmer reads as a farmer in two frames. The art's
weak point is the ground's texture, not its layout.

## Observations (at 1x first, then 4x)

1. **At 1x** the first version of the ground was a **striped quilt**:
   the tileset carried a dark border on its top and bottom, and a border
   in a tileset repeats in *every* cell of the map, so the whole field
   was banded with 2 px dark lines every 16 px. It looked like a
   barcode, not a village.
2. **At 1x** the pond was a row of separate blue boxes with dark
   borders, for the same reason.
3. **At 4x** the slime's eyes were the outline colour on a mid-green
   body: they were *there* (the machine's checks passed: one component,
   no orphans, 100% outline coverage) and **invisible**. The mouth was
   worse — a 2 px dark line on a dark body, i.e. nothing.
4. **At 4x** the apple was a red blob with a stem too dark to see; the
   carrot and the cabbage read fine, and the coin reads as a coin.
5. **At 4x** the farmer's legs merged into one dark mass: the 2 px gap
   between them was closed by the outline pass, so the two legs became
   one block with a 1 px dark line in it.
6. **At 1x** the grass texture reads as a *pattern* — the dark blades
   are evenly spread and the eye finds rows in them — while the path's
   flagstone joints read exactly right.

## What changed

* The tileset is now **seamless**: the grass flows into the grass, the
  path is a pavement of flagstones with a dark joint on its own tile,
  and the pond is water with ripples. (The machine's outline-coverage
  check is vacuous on a fully opaque strip, so nothing is lost; see the
  DOC GAP 3 in `round6/ROUND_LOG.md`.)
* The slime got a **pale sclera** (`#A5DF94`) with a dark pupil and a
  four-pixel smile, and a shine in the top left. The eyes now read at
  1x.
* The apple got a **green leaf** next to its stem.
* The farmer's legs were moved 1 px apart (a 3 px gap, so the outline
  leaves a transparent line between them) and the boots were widened
  outwards, so the stance reads as two legs.

## Re-check

Every strip passes `arc1-art check` (7/7, no failures, outline coverage
1.0, jitter 0.0, one component per frame, the baseline shared inside
every strip) and the contrast is 0.13–0.33 against the 0.55 limit.

At 1x the field is a green meadow with a paved square, a road and a
pond; the farmer, the slime and the four pickups are all legible
against it. At 4x the farmer and the pickups are good; the **slime is
the weakest sprite** — 16x16, its silhouette is a dome with no bump, no
drip and no asymmetry, so it could be any green blob. The **grass
texture** is the weakest ground: evenly scattered blades that make the
eye find rows. Both are noted in the log; neither breaks the game.

---

# Review — the `village` pack

*(The round's own review, kept as the pack's. The art
is unchanged; the pack is the same art, tidied.)*


The second half of the round put the art on the machine, and the machine
is the harshest reviewer there is: `round6/shot.png` (the win) and
`round6/shot_mid.png` (the farmer at work) are captures of the running
game, not previews.

## Verdict

The art survives the machine. At 480x270 the field reads as a place
rather than as tiles, the farmer reads as a person, and the HUD is
legible. What the screen exposes is the **pond** (a blue rectangle with
a hard edge) and the **floating sprites** (nothing grounds the farmer
or the slime on the field).

## Observations

1. **On the machine the grass reads better than in the preview.** The
   same 16x16 tile that looked like "scattered blades" in the preview
   reads as a field of grass at 1x on a 480x270 screen — the eye stops
   seeing the pattern. The preview's verdict was too harsh.
2. **The pond is the one thing that reads as a rectangle.** It is a
   6x2 block of flat blue with a hard vertical edge against the grass
   and no shore at all. It is the only part of the screen that looks
   *placed* rather than *painted*.
3. **The farmer and the slime float.** Neither has a shadow, so on the
   flat field they read as stickers: the slime in particular looks like
   a green dot on the grass. The slime's 16x16 dome gives the eye
   nothing to hold on to.
4. **The win banner was unreadable and off-centre** — the first capture
   showed "YOU WIN" starting at x=160 (so it sat a third of the way
   across the screen) with no backing, and the words ran over both the
   grass and the pavement.

## What changed

* The banner got a **dark panel** behind it and is now **centred**
  (`draw_centered()` computes the x from the string's length). The
  golden was re-made, because the picture changed.
* The harvest sprites were **left where they are** — they read well
  against the pavement.
* The floating sprites got a **shadow**: a two-row blob under the feet,
  offset to the bottom right (the light comes from the top left). The
  first attempt — a shadow straight under the feet — was invisible
  against the farmer's dark boots, so it was moved to the shadow side
  and it now reads on both the grass and the pavement.

## Re-check

The second capture shows the banner centred on a dark panel, legible
from the grass and from the pavement; the HUD (`SCORE 4  LIVES 3  LEFT
0  START RESTARTS`) sits on the black strip under the map and is easy to
read; the farmer stands on the pavement with the slime still prowling
in the north-west, and the two harvests he has not taken yet are visible
on the square. Both walkers now sit on the field instead of over it.

**Still weak, and now measurable on the screen:** the pond (observation
2) — a hard-edged blue rectangle. Fixing it properly needs either a
fourth tile (a shore) or a non-seamless water tile, and a non-seamless
tile repeats its border in every cell of the map (see the DOC GAP 3 in
`round6/ROUND_LOG.md`). With three tiles I could not do it honestly, so
the pond stays the weakest thing on the screen. The grass texture and
the slime's dome silhouette are the next two.
