# Raster ingest — what it does, and what it cannot do

`arc1-art ingest` turns a **messy** sprite sheet (the kind an image model
leaves behind) into a clean, aligned, quantized strip.

**It is a local tool.** The owner's decision (2026-10-21): image-model
output is never committed, so what this tool writes is a *local
reference*, never a shipped asset. What ships is code-generated (the
canvas, the pixmaps, the rig). The ingest output is marked
`tool: "local-ingest"` and `licence: "local reference only"` in the
manifest fragment it writes, so it cannot be mistaken for a shipped
asset.

**It is stdlib only.** No Pillow: the input is **PNG** (zlib is in the
standard library) and nothing else — no JPG. The PNG reader is the
repository's own (`arc1_assetc.decode_png`, 8-bit, non-interlaced).

## The pipeline

```sh
arc1-art ingest sheet.png --out-dir out \
    --grid 4x2 --target-h 24 --tolerance 60 --min-blob 4 \
    --anchor feet --outline 1
```

| step | what it does |
|---|---|
| 1. load, slice | `--grid CxR` or `--cell WxH` cuts the sheet into cells, in reading order |
| 2. key the background | the most common corner colour, flooded from the border with `--tolerance` (per channel); the alpha is hard (0 or 1); the opaque groups under `--min-blob` pixels are dropped |
| 3. crop, pad, align | each cell is cropped to its content, padded to a common frame size and aligned to the anchor (`feet` = the bottom centre of the content, or `center`) |
| 4. resize | to `--target-h` with an area-average (box) filter; the width follows the aspect |
| 5. quantize | median cut (a fixed split order, fixed tie-breaks) to a palette **shared by all the frames**, at most 15 colours plus transparent; `--palette preset.json` maps to a preset instead; no dithering unless `--dither bayer2\|bayer4` |
| 6. outline, orphans | an optional `--outline INDEX` pass, then the single opaque pixels with no opaque neighbour are dropped |
| 7. write | the strip PNG, an `assets.json` fragment, a before/after sheet and `report.json` (the colours, the per-frame bounding boxes, the orphan count, the blobs removed) |

Everything is deterministic: two runs of the same input write the same
bytes.

## What it fixes

* **the background** — a gradient or a flat wash is keyed away;
* **the alignment** — every frame ends up on the same baseline, so a
  sheet of frames that wobble becomes a sheet that does not;
* **the palette** — one palette for the whole sheet, at most 15 colours,
  so the machine's 5/6/5 banks can hold it;
* **the noise** — the specks and the small stray blobs go.

## What it cannot fix

**It cannot fix a drawing that is inconsistent between the frames.** The
sheet's first frame may show a sword and the fourth may show a club; the
ingest will happily align, key and quantize all four, and the animation
will still be wrong. That is a *drawing* problem, not a *file* problem,
and no amount of image processing touches it: the fix is the rig
(card 7.4), the pixmaps (card 7.3) or an artist's hand.

Two more honest limits:

* **The tolerance is per channel.** A background that drifts by more
  than `--tolerance` in one channel is only partly keyed, and the part
  that survives becomes "content" — it will be cropped in, aligned and
  quantized like the rest. Raise the tolerance until the background
  disappears, and look at the before/after sheet to see that it did.
* **A frame that is nearly empty** is a frame the sheet does not really
  contain; the ingest will align it anyway (it becomes a nearly blank
  frame), because it cannot know that the cell was meant to be empty.

## Where it is useful

* **as a style reference** — run a model's sheet through it and look at
  the clean strip, the way the human would;
* **to lift a shape** — a static prop or a piece of a background, cleaned
  up and aligned, to be *rebuilt* by hand as a `.px` pixmap or drawn by
  the rig (that is the path that leads to a shipped asset);
* **to check a generator** — point it at a strip the pipeline produced
  and see what it makes of it.

It is **not** a path into the build: nothing it writes is committed.

## The test

`tests/test_art_ingest.sh` synthesizes a messy sheet (a gradient
background, soft edges, noise) with the canvas library, runs the ingest
over it and asserts: at most 15 colours, one feet baseline across the
frames, zero orphans, the report's keys, two byte-identical runs, and the
teaching errors (a non-integer grid, a non-integer cell size, a sheet
with no opaque pixels). No third-party image is committed, and none is
needed.
