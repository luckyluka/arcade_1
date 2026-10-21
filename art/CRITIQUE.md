# CRITIQUE — the visual review checklist

The machine half of the art review is `arc1-art check` (it counts the
colours, the orphans, the components, the baseline, the jitter, the
outline coverage and the contrast). This is the **human** half: the
checklist for an agent that can *see*.

A sprite that passes every automated check can still be ugly. These are
the things a machine cannot count.

## How to review

1. Open the **contact sheet** (`art/reports/<asset>.md` says which file).
   Look at it at **1x** (the size the machine shows) *first*: a sprite
   that only works at 8x does not work.
2. Then at **4x**, for the things 1x hides (the shading, the outline's
   consistency, the palette's harmony).
3. Write the review to `art/reports/<asset>.md` with **four** parts:
   * **Verdict** — does it work, and in what sense (a one-liner is fine);
   * **Observations** — at least three, each about something you SAW;
   * **What changed** — what you did about them (or why you did not);
   * **Re-check** — what the second look says.

If the first review finds nothing, the review was not careful enough.
Say what looked wrong.

## The checklist

**Silhouette**
- [ ] Readable at 1x: can you tell what it is without the colour?
- [ ] One shape, not a blob: the outline says where the shape ends.
- [ ] Nothing essential is thinner than 2 px (it will vanish).
- [ ] The negative space is open: no accidental holes that read as eyes
      or mouths where there are none.

**Light**
- [ ] One light direction, everywhere (the card's default: top left).
- [ ] The highlight and the shadow are on the same side in every part.
- [ ] The shadow side is darker, not just different.
- [ ] No second light source: no rim light fighting the key light.

**Colour**
- [ ] The palette is harmonious: the hues agree, the values are spaced.
- [ ] The figure reads against the background it will be used on.
- [ ] The accent (if any) is used once, on the thing that matters.

**Outline**
- [ ] The outline is the same colour everywhere.
- [ ] The outline is 1 px, and it is *inside* the silhouette (or
      everywhere outside it — but not both).
- [ ] Where the sprite meets another shape, the outline does not vanish.

**Animation** (for a state with more than one frame)
- [ ] The timing reads: the weight is in the right frames.
- [ ] The arcs are arcs: a swinging arm follows a curve, not a hinge.
- [ ] The anticipation is there (a wind-up before a strike).
- [ ] The feet stay on the ground: the lowest opaque pixel is the same
      row in every frame of the state.
- [ ] The silhouette of the *whole cycle* is readable (squint at it).

**Noise**
- [ ] Anything that reads as noise at 1x: speckles, a stray pixel, a
      shape that jitters.
- [ ] The character's weight: does it look like it would stand?

## The tool's part

Before the eyes, the machine:

```sh
arc1-art check art/build/hero.png --frame 24x32
```

It prints a JSON report and exits 1 when a check fails. The checks:
the colour count (≤ 15 plus transparent), the palette distinctness
under the 5/6/5 quantisation, the orphan pixels, the silhouette's
connectivity, the baseline, the anchor's jitter, the outline coverage,
the fill-to-outline contrast and the readability (at least 8 px tall).

A report that says `"ok": true` means the sprite is *well formed*, not
that it is *good*.
