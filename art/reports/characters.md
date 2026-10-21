# Review — the `characters` pack

The library's two rigged characters: the humanoid (the rig animator's
reference) and the samurai (Petal Blade's hero). Both come from the same
rig format, so this review is mostly about the *format* — a character as
parts, keyframes and poses rather than as drawn frames.

* **Sheets**: `art/library/characters/strips/` (16 strips, 28/24/40 px)
* **Rigs**: `art/rigs/humanoid.json`, `art/rigs/samurai.json`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**The format is the value; the samurai's pixels are not.** A rig gives
consistency, editability and a whole animation for the price of a file of
numbers — and the humanoid proves it (eight states, one preset, and a
change to the walk cycle is a change to two keyframes). The samurai, judged
only as art, is **worse than Petal Blade's hand-drawn hero**: it is a
hooded figure with a gold crest where the procedural hero had a kabuto, a
face and a rim light. Rigged art is *plausible*; drawn art is *specific*.

The honest comparison is in `docs/experiments/art-pass.md`: the rig hero
is clearly worse at 28×40 px, and better in every other way.

## Observations (at 1x, then 4x)

1. **The legs vanish into the torso.** A capsule body over capsule legs
   reads as a bell at 1x; the walk cycle's stride only shows in the
   frames where the leg clears the hem. The humanoid (a narrower body)
   survives this; the samurai (a hakama) does not.
2. **The face is a band.** Two ellipses (head, kabuto) overlap into a
   stripe of skin. At 4x the stripe is clearly a face; at 1x it is a
   mouth.
3. **The outline is on the outside**, so the sprite is 1 px larger than
   its parts and the anchor has to allow for it — the style sheet says
   so now, but the first version of the humanoid preset did not, and the
   feet sat a pixel low.
4. **The two characters share a format and not a look**: the humanoid is
   24×32 and the samurai 28×40, with different palettes and a different
   number of parts. That is the point of a *preset* — the proportions are
   the knobs.

## What changed

* Both rigs moved into the library as one pack (`characters`), so a game
  can take a rigged character without knowing which round drew it.
* The **outline pass** was renamed `grow_outline` and its contract (it
  grows the shape) was written down — the round-6 and round-7 agents both
  had to read the source to learn it.
* The style sheet now states the **2 px gap the outline pass closes**, so
  the next rig does not lose its legs.

## Re-check

All sixteen strips pass `arc1-art check` (one component, no orphans, a
shared baseline, 100 % outline coverage) and the pack regenerates byte for
byte (`make art-library-check`).

**What I would do next**: give the rig a *face* part (a jaw or a nose, so
the head has a direction) and let the legs clear the hem (a shorter
torso, or a hakama that tapers). Both are one preset's worth of numbers.
