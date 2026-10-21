# Review — the `icons` pack

The library's icon set: a coin, a heart, a key, a potion and an arrow,
authored as `.px` text pixmaps and compiled into strips.

* **Sheet**: `art/library/icons/strips/` (5 strips of 8×8)
* **Style sheet**: `art/library/icons/STYLE.md`
* **Reviewed**: 2026-10-21, by the implementing agent

## Verdict

**They work.** At 1x — the size the machine shows them — each of the five
reads as its object: the coin is a disc with a hole, the heart has a
notch, the key has a ring and teeth, the potion has a neck and a body,
the arrow points right. They are the best of the library's art, and they
are the *smallest* — which is the point: at 8×8 there is no room for the
mistakes the bigger sets make.

They are also the plainest: an outline with a fill inside, two tones, no
shading ramp, no dither. That restraint is deliberate (the style sheet
says so) and it is what makes them legible at 8 px.

## Observations (at 1x, then 4x)

1. **The arrow is the weakest of the five.** Its tail and its head are the
   same thickness, so at 1x it reads as a blob with a point. The head
   could be two pixels wider than the shaft.
2. **The coin's hole is a single pixel** — enough to read as a coin at
   1x, and it is the one feature that would vanish if the icon were
   scaled down further.
3. **The key's ring and its shaft are the same tone**, so the key reads
   as a "T" more than a key at 1x. The teeth (one pixel) are what make
   it a key, and they are the first thing to lose.
4. The heart and the potion are the strongest: a closed shape with a
   distinctive outline (a notch, a neck) that survives any scaling.

## What changed

* The pack was **moved into the library** (`art/px/` → `art/library/icons/`)
  and its palette unified: the five icons shared four different presets
  and banks (one per icon, because they were authored one at a time), and
  they now share one bank — `props16` at bank 11 — so a game can use
  the set without spending five banks.
* The `.px` headers were corrected to name that palette and bank, so the
  source says what the pack does.

## Re-check

All five pass `arc1-art check` (one component, no orphans, a shared
baseline, 100 % outline coverage), and the pack regenerates byte for byte
(`make art-library-check`).

**What I would do next**: widen the arrow's head by a pixel and give the
key's shaft a tone lighter than its ring, so both read at 1x. Neither is
worth a re-cut of the set alone — they are for the next pass over the
whole library.
