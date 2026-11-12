# spec-next's size sweep fails its own symmetry check — and the pixels are symmetric

Date: 2026-11-12. Status: **finding, unresolved** (blocked on owner decision).

## What failed

`make spec-check` (new target, runs `arcforge/spec-next/check_spec.py`) ends
`1 FAILED`:

```
FAIL  size sweep: symmetric, no clipping at 4 sizes
```

`make check` (arcforge) is ALL GREEN; goldens untouched.

## Measurement

`spec-next/prototype/size_sweep.py` (mtime 2026-11-11 **07:59**) renders the
scalable prototype at 4 sizes and exits 1 if any pixel is asymmetric:

```python
asym = sum(1 for (x, y), c in grid.items() if grid.get((2 * cx - x, y)) != c)
```

It compares the **whole grid tuple**. Since commit `110b94a` (2026-11-11
**10:01**, "M1 step 1: per-part inner outline") the tuple is
`(li, seq, part, ramp, tone)` — before it was `(li, ramp, tone)`. `seq` is the
painter's-order index of the shape that wrote the pixel; it is inherently
directional (left leg drawn at seq 5, right leg at seq 7).

Comparing only the visually meaningful fields, at all 4 sizes (h = 14/22/30/46):

| comparison | asymmetric pixels |
|---|---|
| full tuple (what the sweep does) | 44 / 98 / 178 / 416 — **FAIL** |
| ramp + tone (the colour) | **0 / 0 / 0 / 0** |
| layer only | **0 / 0 / 0 / 0** |
| presence only (occupancy) | **0 / 0 / 0 / 0** |

`clipped_px` is 0 at every size. The body is perfectly mirror-symmetric in
shape and colour; the only asymmetry is the draw-order bookkeeping field that
`110b94a` added 2 h 2 min after the sweep script was written.

## Why it matters

- The sweep's *intent* (per `spec-next/07_MILESTONES.md` X0 exit: "sweep
  passes") is visual symmetry; its *implementation* leaks an internal field.
- `spec-next/` is to be imported **unchanged**, so the fix cannot be applied
  there without the owner's approval. The tool cannot drop `seq` — the
  inner-outline pass (`outline.inner.mode: part`) needs `(li, seq)` for
  painter's order, and it is golden-locked.
- Consequence for M4 sequencing: the card gates M4.1 on X0 → X1 → **G1x**
  ("sweep passes across its declared range"). A sweep check that fails for a
  bookkeeping reason would mask a real geometry regression — it must compare
  colour, not tuples, before G1x can be trusted.

## Options (owner decision needed)

1. **Patch the prototype** (1 line): compare `(v[3], v[4])` instead of the
   full tuple. Touches `spec-next/`, i.e. breaks "imported unchanged".
2. **Re-export the drop**: fix `size_sweep.py` at the source (the arcforge
   zip) and re-supply `spec-next/`.
3. **Accept the FAIL** and record the target as known-red until then.

No pixel behaviour, goldens or tool code are affected by any option.

**Fixed 2026-11-12 (owner-approved option 1):** `size_sweep.py::analyse()` now
compares `colour(v) = (v[-2], v[-1])` (ramp, tone), counting a pixel as
asymmetric when the mirror position is missing or the colour differs; the
prototype now depends only on `(ramp, tone)`, not on the renderer's internal
grid-value layout. The clipping check is unchanged.
