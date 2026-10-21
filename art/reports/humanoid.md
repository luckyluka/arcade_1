# The humanoid rig — the visual review

ROADMAP_V4 card 7.4 asks the implementing agent to review the rig's
contact sheet **with the card 7.6 checklist at 1x and 4x**, and to record
the verdict here — plus what looked wrong and what was changed. Card 7.6's
tool does not exist yet, so this review uses the checklist the card
describes: silhouette readable at 1x, one light direction, palette
harmony, outline consistency, animation timing and arcs, and anything that
reads as noise.

* **Asset**: `art/rigs/humanoid.json` (24×32, dusk-Japan palette)
* **Sheet**: `programs/rig/humanoid_sheet.png` (one row per state, every
  frame at 1x and at 4x)
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**It works, and it is crude.** The rig produces a *consistent*
character: the same silhouette in every frame, the feet on one row, a
walk cycle that reads as a walk, an attack that reads as an attack, and a
body that lies down when it dies. Everything the card asks for
mechanically is there.

What it is not: good art. Beside Petal Blade's hand-authored hero this
reads as a **hooded figure**, not a samurai. The procedural hero has a
kabuto with a crescent maedate, a rim light, a drawn face and eleven
carefully posed frames; the rig has a dark cap, a band of face and a robe.
At 4x the shape is legible and the animation is believable; at 1x it is a
blob with a face.

**The honest comparison** (this is the question card 7.7 will ask again
with Petal Blade): *the rig hero is clearly worse than the procedural
one.* The rig's value is not beauty — it is that the frames are
**consistent by construction** and that anyone (an agent) can change a
pose and get a whole animation back.

## Observations (round 1 — what looked wrong)

1. **The limbs were swallowed by the torso.** The first preset had a
   10 px wide torso and the arms and legs hung at ±3 and ±4 px from the
   centre line, so every limb was *inside* the torso's silhouette. The
   character was a peanut. The legs only appeared in the walk cycle, and
   only because the swing carried them out.
2. **The sheet was wrong**: every state was drawn in one row, and the 1x
   rows of the states were drawn on top of each other. There was no way
   to compare the states.
3. **The hair was a helmet.** A 12×8 ellipse at the head's centre
   covered the whole head; only a band of face showed, and the head read
   as a dark blob with a chin.
4. **The sword was invisible** in every state but the attack: 3 px wide
   and 11 px long, drawn behind the arm, in a colour that disappeared
   into the outline.

## What changed

1. The body was slimmed: the torso 10 → 8 px, the arms moved out to
   ±5, the legs to ±3, and the head 11 → 9 px. The limbs now clear the
   torso in every pose.
2. The sheet was rewritten: **one row per state**, the scaled frames on
   top, the frame numbers between, and the 1x row underneath.
3. The hair became a 10×5 cap sitting on the head's top (`pivot` -3),
   so the face reads.
4. The sword grew to 4×13 and was drawn in front of the arm.
5. A **two-tone shading pass** was added to the rig (a part may carry a
   `shade` tone: the shape is drawn in the shade colour and again, one
   pixel up and left, in the main colour — the light comes from the top
   left). The torso and the head use it; what is left of the shade is the
   shadow side. It is the cheapest way to stop a flat shape reading as a
   blob, and it is one of the three mitigations the card allows.

## Re-check

The second sheet (after the changes above):

* **Silhouette at 1x**: still a small blob with a face, but now the
  legs separate from the robe and the walk cycle's stride is visible even
  in the 1x row. Passable, not good.
* **One light direction**: yes — the shade tone is on the lower right of
  every shape, and nothing contradicts it.
* **Palette harmony**: yes — the figure uses three tones of the dusk
  ramp (the tunic, its shadow, the trousers) plus the skin, the hair and
  the gold of the sword. It is the same palette family as Petal Blade's
  world, which is the point of the preset.
* **Outline consistency**: yes — the outline pass runs last on every
  frame, so the dark edge is the same 1 px everywhere.
* **Animation timing and arcs**: the walk and run cycles read correctly
  (the legs swing, the arms counter-swing, the body bobs by 1 px); the
  attack raises the sword and the follow-through settles. The walk's
  opaque-pixel count varies by 2.4 % across the cycle — far inside the
  card's 25 % bound, which is the machine-level evidence that the
  animation is consistent.
* **What still reads as noise**: the *dead* pose is the weakest frame —
  the body lying at -74° puts the head where the legs were, and at 1x it
  reads as a lump. The face band also disappears when the character
  turns (the head is an ellipse, so it has no direction).

**What I would do next, in order**: give the head a facing direction (a
nose or a jaw asymmetry — the rig would need a "front" colour band), give
the arms hands (a 2 px lighter tip), and make the dead pose a *crouch*
rather than a rotation, which the frame can hold better than a lie.

## The numbers

| state | frames | opaque px (min..max) | mean | deviation | colours/frame |
|---|---:|---:|---:|---:|---:|
| idle   | 4 | 452..459 | 453.8 | 1.2 % | 8 |
| walk   | 8 | 436..455 | 446.6 | 2.4 % | 8 |
| run    | 8 | 432..454 | 445.4 | 3.0 % | 8 |
| jump   | 1 | 384 | 384.0 | — | 8 |
| fall   | 1 | 386 | 386.0 | — | 8 |
| attack | 4 | 444..481 | 458.0 | 5.0 % | 7-8 |
| hurt   | 1 | 438 | 438.0 | — | 8 |
| dead   | 1 | 398 | 398.0 | — | 8 |
