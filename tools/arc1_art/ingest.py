"""ingest — raster ingest for image-model sheets (ROADMAP_V4 card 7.5).

**LOCAL ONLY.** The owner's decision (2026-10-21): image-model output is
never committed, so this tool's output is a *local* reference, never a
shipped asset. What ships is code-generated. The tool is still worth
having: it fixes the three things a messy sheet gets wrong — the
background, the alignment and the palette — so an agent can look at a
clean strip (or lift a shape out of it) without an artist's hand.

**Stdlib only.** No Pillow (the owner's decision): the input is PNG,
which zlib covers, and nothing else — no JPG. The PNG reader is the
repository's own (`arc1_assetc.decode_png`, 8-bit non-interlaced).

The pipeline, every step deterministic (ties broken by sorted order):

1. **load** the PNG, **slice** it into cells (`--grid CxR` or
   `--cell WxH`);
2. **key the background**: the most common corner colour, flooded from
   the border with `--tolerance`; the alpha is hard (0 or 1); isolated
   opaque groups smaller than `--min-blob` are dropped;
3. **crop** each cell to its content, **pad** to a common frame size and
   **align** it to the anchor (`feet` = the bottom centre of the
   content, or `center`);
4. **resize** to the target frame height with an area-average (box)
   filter, the width by aspect;
5. **quantize** with median cut (a fixed split order and fixed
   tie-breaks) to a palette **shared by all the frames**, at most 15
   colours plus transparent — or map to a preset with `--palette`;
   dithering only with `--dither bayer2|bayer4`;
6. optionally **outline** (`--outline INDEX`) and **remove orphans**;
7. write the strip PNG, an assets.json fragment, a before/after contact
   sheet and a report.json.

It cannot fix the one thing that is actually hard: a drawing that is
inconsistent between frames. See docs/art/INGEST.md.
"""

import json
import os

from . import canvas as _canvas

__all__ = ["IngestError", "ingest", "median_cut", "quantize", "resize_area",
           "key_background", "remove_blobs", "remove_orphans"]


class IngestError(Exception):
    """A teaching error: what is wrong and what to do about it."""


# --------------------------------------------------------------------------
# 1. load and slice
# --------------------------------------------------------------------------

def load_png(path):
    """The PNG at `path` -> (w, h, pixels) with pixels a flat list of
    (r, g, b, a). Uses the repository's own reader (8-bit, non-
    interlaced)."""
    import arc1_assetc

    with open(path, "rb") as f:
        data = f.read()
    try:
        return arc1_assetc.decode_png(data, os.path.basename(path))
    except arc1_assetc.ImageError as e:
        raise IngestError(str(e))
    except arc1_assetc.UnsupportedImage as e:
        raise IngestError(str(e))


def parse_grid(text):
    """'3x2' -> (3, 2). Anything else is a teaching error."""
    if "x" not in text:
        raise IngestError("the grid %r is not CxR" % text)
    a, _, b = text.partition("x")
    try:
        cols, rows = int(a), int(b)
    except ValueError:
        raise IngestError("the grid %r is not CxR (two whole numbers)"
                           % text)
    if cols <= 0 or rows <= 0:
        raise IngestError("the grid %r must be positive" % text)
    return cols, rows


def parse_cell(text):
    """'32x32' -> (32, 32)."""
    if "x" not in text:
        raise IngestError("the cell size %r is not WxH" % text)
    a, _, b = text.partition("x")
    try:
        w, h = int(a), int(b)
    except ValueError:
        raise IngestError("the cell size %r is not WxH (two whole numbers)"
                           % text)
    if w <= 0 or h <= 0:
        raise IngestError("the cell size %r must be positive" % text)
    return w, h


def slice_cells(w, h, grid=None, cell=None):
    """The cell rectangles of the sheet, in reading order (row by row)."""
    if grid:
        cols, rows = grid
        cw, ch = w // cols, h // rows
        if cw <= 0 or ch <= 0:
            raise IngestError("the grid %dx%d does not fit in a %dx%d sheet"
                               % (cols, rows, w, h))
        return [(c * cw, r * ch, cw, ch)
                for r in range(rows) for c in range(cols)]
    if cell:
        cw, ch = cell
        if cw > w or ch > h:
            raise IngestError("the cell %dx%d does not fit in a %dx%d sheet"
                               % (cw, ch, w, h))
        out = []
        y = 0
        while y + ch <= h:
            x = 0
            while x + cw <= w:
                out.append((x, y, cw, ch))
                x += cw
            y += ch
        return out
    raise IngestError("the sheet needs --grid CxR or --cell WxH")


# --------------------------------------------------------------------------
# 2. the background key
# --------------------------------------------------------------------------

def _key_colour(pixels, w, h, rect):
    """The most common colour among the cell's four corners."""
    x0, y0, cw, ch = rect
    corners = []
    for (x, y) in ((x0, y0), (x0 + cw - 1, y0), (x0, y0 + ch - 1),
                   (x0 + cw - 1, y0 + ch - 1)):
        corners.append(pixels[y * w + x][:3])
    counts = {}
    for c in corners:
        counts[c] = counts.get(c, 0) + 1
    # the most common; ties broken by the sorted colour order
    return sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]


def key_background(pixels, w, h, rect, tolerance=0):
    """The cell's opaque mask: everything the border's flood fill does NOT
    reach is background.

    The key colour is the most common corner colour; the flood grows from
    the border while the colour stays within `tolerance` (per channel). The
    alpha is hard: 1 for the content, 0 for the background.
    """
    x0, y0, cw, ch = rect
    key = _key_colour(pixels, w, h, rect)
    mask = [0] * (cw * ch)

    def near(p):
        return (abs(p[0] - key[0]) <= tolerance and
                abs(p[1] - key[1]) <= tolerance and
                abs(p[2] - key[2]) <= tolerance)

    stack = []
    for x in range(cw):
        stack.append((x, 0))
        stack.append((x, ch - 1))
    for y in range(ch):
        stack.append((0, y))
        stack.append((cw - 1, y))
    while stack:
        x, y = stack.pop()
        if not (0 <= x < cw and 0 <= y < ch):
            continue
        i = y * cw + x
        if mask[i]:
            continue
        p = pixels[(y0 + y) * w + (x0 + x)]
        if p[3] == 0 or not near(p):
            continue
        mask[i] = 1                     # background
        stack.append((x + 1, y))
        stack.append((x - 1, y))
        stack.append((x, y + 1))
        stack.append((x, y - 1))

    out = [0 if mask[i] else 1 for i in range(cw * ch)]
    return out, key


def remove_blobs(mask, min_blob):
    """Drop the opaque groups smaller than `min_blob` pixels.

    The groups are the 4-connected components of the opaque pixels; the
    size is counted, and a group under the limit is cleared. The groups
    are visited in reading order, so the result does not depend on the
    order the flood fills happened in.
    """
    if min_blob <= 1:
        return list(mask), 0
    cw = len(mask)
    h = len(mask) // cw if cw else 0
    seen = [0] * len(mask)
    removed = 0
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        group = []
        stack = [start]
        seen[start] = 1
        while stack:
            i = stack.pop()
            group.append(i)
            x, y = i % cw, i // cw
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if not (0 <= nx < cw and 0 <= ny < h):
                    continue
                j = ny * cw + nx
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
        if len(group) < min_blob:
            removed += len(group)
            for i in group:
                mask[i] = 0
    return mask, removed


# --------------------------------------------------------------------------
# 3. crop, pad, align
# --------------------------------------------------------------------------

def crop_to_content(mask, cw, ch):
    """The content's bounding box, or None for an empty cell."""
    minx, miny, maxx, maxy = cw, ch, -1, -1
    for y in range(ch):
        for x in range(cw):
            if mask[y * cw + x]:
                minx = min(minx, x)
                maxx = max(maxx, x)
                miny = min(miny, y)
                maxy = max(maxy, y)
    if maxx < 0:
        return None
    return (minx, miny, maxx - minx + 1, maxy - miny + 1)


def align_anchor(box, anchor):
    """Where the content's anchor sits inside the content box:
    `feet` = the bottom centre, `center` = the middle."""
    x, y, w, h = box
    if anchor == "feet":
        return (x + w // 2, y + h)
    return (x + w // 2, y + h // 2)


# --------------------------------------------------------------------------
# 4. resize (area average)
# --------------------------------------------------------------------------

def resize_area(frame, w, h, tw, th):
    """A frame resized to tw x th with an area-average (box) filter.

    `frame` is a list of rows of (r, g, b, a) tuples. The alpha is
    averaged too, and a pixel with no coverage stays transparent.
    """
    out = []
    for ty in range(th):
        row = []
        y0 = ty * h / float(th)
        y1 = (ty + 1) * h / float(th)
        for tx in range(tw):
            x0 = tx * w / float(tw)
            x1 = (tx + 1) * w / float(tw)
            r = g = b = a = 0.0
            n = 0
            for y in range(int(y0), max(int(y0) + 1, int(-(-y1 // 1)))):
                if y >= h:
                    continue
                for x in range(int(x0), max(int(x0) + 1, int(-(-x1 // 1)))):
                    if x >= w:
                        continue
                    p = frame[y][x]
                    r += p[0]
                    g += p[1]
                    b += p[2]
                    a += p[3]
                    n += 1
            if n == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append((int(r / n + 0.5), int(g / n + 0.5),
                            int(b / n + 0.5), int(a / n + 0.5)))
        out.append(row)
    return out


# --------------------------------------------------------------------------
# 5. quantize
# --------------------------------------------------------------------------

def median_cut(pixels, max_colours=15):
    """Median cut: at most `max_colours` colours, a fixed split order and
    fixed tie-breaks, so the result is deterministic.

    The boxes are split on the channel with the largest spread; the
    median cut is the sorted middle. Ties (equal spreads, equal medians)
    are broken by the channel order and then by the colour order.
    """
    pts = [(p[0], p[1], p[2]) for p in pixels if p[3] > 0]
    if not pts:
        return []
    pts = sorted(set(pts))
    boxes = [pts]
    while len(boxes) < max_colours:
        # the box with the largest spread
        best = None
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for ch in range(3):
                lo = min(p[ch] for p in box)
                hi = max(p[ch] for p in box)
                spread = hi - lo
                key = (-spread, ch, i)
                if best is None or key < best[0]:
                    best = (key, i, ch)
        if best is None:
            break
        _, index, channel = best
        box = boxes[index]
        box = sorted(box, key=lambda p: (p[channel], p))
        mid = len(box) // 2
        boxes[index] = box[:mid]
        boxes.append(box[mid:])
    return [box[len(box) // 2] for box in boxes]


def quantize(pixels, palette, dither=None):
    """The frame -> the palette's indices (0 = transparent).

    With `dither` ("bayer2" or "bayer4") the error is spread with the
    ordered-dither matrix; without it, the nearest colour wins.
    """
    out = []
    for y, row in enumerate(pixels):
        out_row = []
        for x, p in enumerate(row):
            if p[3] < 128:
                out_row.append(0)
                continue
            r, g, b = p[0], p[1], p[2]
            if dither:
                t = _canvas.ordered_dither(x, y, 4 if dither == "bayer4" else 2)
                r = max(0, min(255, int(r + (t - 0.5) * 32)))
                g = max(0, min(255, int(g + (t - 0.5) * 32)))
                b = max(0, min(255, int(b + (t - 0.5) * 32)))
            best, best_d = 1, None
            for i, c in enumerate(palette):
                if i == 0:
                    continue
                d = (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2
                if best_d is None or d < best_d:
                    best, best_d = i, d
            out_row.append(best)
        out.append(out_row)
    return out


def nearest_palette(pixels, palette):
    """Map the frame to a preset palette (no dithering): the nearest
    colour of the preset, by RGB distance."""
    return quantize(pixels, palette, dither=None)


# --------------------------------------------------------------------------
# 6. the outline and the orphans
# --------------------------------------------------------------------------

def remove_orphans(indices, w, h):
    """Drop the opaque pixels with no opaque neighbour (4-connected).

    An orphan is a single pixel floating in the transparent field: it is
    usually a speck of the background that survived the key. The count is
    returned with the cleaned frame.
    """
    out = [list(row) for row in indices]
    removed = 0
    for y in range(h):
        for x in range(w):
            if not out[y][x]:
                continue
            if ((x > 0 and out[y][x - 1]) or
                    (x + 1 < w and out[y][x + 1]) or
                    (y > 0 and out[y - 1][x]) or
                    (y + 1 < h and out[y + 1][x])):
                continue
            out[y][x] = 0
            removed += 1
    return out, removed


# --------------------------------------------------------------------------
# the pipeline
# --------------------------------------------------------------------------

def ingest(path, grid=None, cell=None, target_h=None, tolerance=0,
           min_blob=1, anchor="feet", palette=None, dither=None,
           outline=None, name="sheet"):
    """Run the whole pipeline over one sheet.

    Returns a dict: {frames, size, colours, bbox_jitter, orphans,
    blobs_removed, key_colours, cells}.
    """
    w, h, pixels = load_png(path)
    cells = slice_cells(w, h, grid=grid, cell=cell)
    if not cells:
        raise IngestError("the sheet has no cells")

    # --- the raw cells, for the before/after sheet ----------------------
    before = []
    for (x0, y0, cw, ch) in cells:
        frame = []
        for y in range(ch):
            row = []
            for x in range(cw):
                p = pixels[(y0 + y) * w + (x0 + x)]
                row.append((p[0], p[1], p[2], p[3]))
            frame.append(row)
        before.append(frame)

    # --- the background and the content ---------------------------------
    masks = []
    key_colours = []
    blobs_removed = 0
    for rect in cells:
        mask, key = key_background(pixels, w, h, rect, tolerance)
        mask, removed = remove_blobs(mask, min_blob)
        blobs_removed += removed
        masks.append(mask)
        key_colours.append(key)

    boxes = [crop_to_content(m, rect[2], rect[3]) for m, rect in zip(masks, cells)]
    if all(b is None for b in boxes):
        raise IngestError("the sheet has no opaque pixels — the background "
                           "key took everything (raise --tolerance?)")

    # --- crop, pad, align ------------------------------------------------
    cropped = []
    anchors = []
    for mask, box, rect in zip(masks, boxes, cells):
        if box is None:
            cropped.append(None)
            anchors.append(None)
            continue
        x0, y0, cw, ch = rect
        bx, by, bw, bh = box
        frame = []
        for y in range(by, by + bh):
            row = []
            for x in range(bx, bx + bw):
                p = pixels[(y0 + y) * w + (x0 + x)]
                row.append((p[0], p[1], p[2], p[3] if mask[y * cw + x] else 0))
            frame.append(row)
        cropped.append(frame)
        anchors.append(align_anchor(box, anchor))

    # the common frame size: the biggest content
    fw = max((len(f[0]) for f in cropped if f), default=1)
    fh = max((len(f) for f in cropped if f), default=1)

    # --- pad to the common size, aligned ---------------------------------
    padded = []
    for frame, anc in zip(cropped, anchors):
        if frame is None:
            padded.append([[(0, 0, 0, 0)] * fw for _ in range(fh)])
            continue
        ax, ay = anc
        ox = max(0, fw // 2 - ax)
        oy = max(0, fh - ay)
        out = [[(0, 0, 0, 0)] * fw for _ in range(fh)]
        for y, row in enumerate(frame):
            ty = oy + y
            if not 0 <= ty < fh:
                continue
            for x, p in enumerate(row):
                tx = ox + x
                if 0 <= tx < fw:
                    out[ty][tx] = p
        padded.append(out)

    # --- resize ------------------------------------------------------------
    if target_h:
        tw = max(1, int(round(fw * (target_h / float(max(1, fh))))))
        th = target_h
        resized = [resize_area(f, fw, fh, tw, th) for f in padded]
    else:
        tw, th = fw, fh
        resized = padded
    fw, fh = tw, th

    # --- the palette --------------------------------------------------------
    if palette:
        colours = list(palette)
    else:
        flat = [p for f in resized for row in f for p in row]
        colours = median_cut(flat, 15)
    if not colours:
        raise IngestError("the sheet has no colours to quantize")

    # --- quantize, outline, orphans ----------------------------------------
    frames = []
    orphans = 0
    for f in resized:
        idx = quantize(f, colours, dither=dither)
        if outline:
            _outline_indices(idx, fw, fh, outline)
        idx, removed = remove_orphans(idx, fw, fh)
        orphans += removed
        frames.append(idx)

    # --- the report ---------------------------------------------------------
    jitter = []
    for idx in frames:
        minx, miny, maxx, maxy = fw, fh, -1, -1
        for y in range(fh):
            for x in range(fw):
                if idx[y][x]:
                    minx = min(minx, x)
                    maxx = max(maxx, x)
                    miny = min(miny, y)
                    maxy = max(maxy, y)
        jitter.append((minx, miny, maxx - minx + 1, maxy - miny + 1)
                      if maxx >= 0 else (0, 0, 0, 0))

    return {
        "name": name,
        "before": before,
        "frames": frames,
        "size": (fw, fh),
        "colours": colours,
        "bbox_jitter": jitter,
        "orphans": orphans,
        "blobs_removed": blobs_removed,
        "key_colours": key_colours,
        "cells": len(cells),
    }


def _outline_indices(indices, w, h, colour):
    """Paint `colour` into the transparent pixels that touch an opaque one
    (the 4-neighbour rule, the same one the canvas uses)."""
    todo = []
    for y in range(h):
        for x in range(w):
            if indices[y][x]:
                continue
            if ((x > 0 and indices[y][x - 1]) or
                    (x + 1 < w and indices[y][x + 1]) or
                    (y > 0 and indices[y - 1][x]) or
                    (y + 1 < h and indices[y + 1][x])):
                todo.append((x, y))
    for x, y in todo:
        indices[y][x] = colour
