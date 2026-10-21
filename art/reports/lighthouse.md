# Review — the `lighthouse` pack

*(The round's own review, kept as the pack's. The art
is unchanged; the pack is the same art, tidied.)*


*Reviewed with `art/CRITIQUE.md` — yes, its checklist is the instrument here
(silhouette, light, colour, outline, animation, noise), and the contact
sheet `round7/art/sheet.png` plus the captured game frames
`round7/shot_60.png` (the start), `round7/shot_210.png` (climbing),
`round7/shot_lose.png` (the sea wins) and `round7/shot_400.png` (the
beacon) are what I looked at.*

## Verdict

It works: at 1x the tower reads as a tower, the cliff as rock, the sea as
water, and the keeper as a person on a ladder — and the four map layers
compose into one scene (a lighthouse on a cliff at dusk), not four pictures.
The weakest thing in the set is the sky, which is flat bands rather than a
ramp, and the cliff's 16 px tile repeat.

## Observations

1. **The ladder did not read at 1x.** In the first game capture the ladder
   was two pale lines on a pale wall — a white thread, not a ladder. The
   strip passed every check (colour count, orphans, components, baseline,
   outline coverage, contrast) because the checks count pixels and cannot
   see that two parallel lines are not a ladder.
2. **The cliff read as a staircase.** The rock tiles were drawn as
   horizontal strata, so a wall of them made strong horizontal bands and
   the cliff's stepped silhouette became a flight of stairs. The angular
   blocks fixed the *bands*, and the cliff's silhouette then read as a
   broken edge.
3. **The sky was a saturated slab.** The warm bands were 4 rows each and
   the visible part of the world was mostly one crimson band — a red
   rectangle, not a dusk. Thinning the warm bands to 2 rows each and
   pushing them down to the horizon turned the same palette into a
   gradient with a glow at the bottom.
4. **The keeper's face is two dark pixels on a pale oval**, and the hat
   reads as hair rather than a hat. The read comes from the hat's brim, the
   scarf (the only accent) and the long coat — the face itself carries
   nothing.
5. **The lit lamp read as a lit window**, because its glass was a solid
   gold rectangle. A taper with a bright core reads as a flame.

## What changed

* **The ladder**: a dark recess in the wall with bright iron rails and
  rungs every 8 px (the rails light, the gaps dark) — it now reads as a
  ladder from across the screen.
* **The rock**: horizontal strata became angular blocks with diagonal
  edges, so a tiled cliff no longer reads as steps; the cracks stayed.
* **The sky**: the bands were re-cut — the indigo family gets most of the
  world, the warm bands are 2 rows each and sit at the horizon, and the
  gold band is where the sea starts.
* **The lamp**: the flame is a taper (a tip, a body, a core) instead of a
  filled pane; the bracket was made lighter so the lamp reads as a
  bracket and not a floating box.
* **The keeper**: the head was lifted a pixel, the scarf narrowed to six
  pixels and made three rows deep (so the idle's breath cannot break it
  off the coat), the eyes moved with the head.
* **The sea**: the crest's swell is one long wave over three tiles, so it
  reads as water and not as icebergs.

## Re-check

At 1x in the captured frames: the tower's ladder is unmistakable, the
cliff is a broken mass of blocks with moss on its crown, the sky grades
from night at the top of the world to amber at the water, and the sea's
crest is a wave. The keeper reads as a small figure with a lantern on a
ladder — the face is still the weakest part, and the cliff's tile repeat is
still visible if you look for it, but neither hurts the read at the size
the machine shows.
