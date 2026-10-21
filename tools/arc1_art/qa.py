"""qa — the visual checks (ROADMAP_V4 card 7.6).

`arc1-art check` reads a strip PNG (the frames side by side) and reports
what is wrong with it, as JSON, exiting 1 when a check fails. It is the
machine half of the review; the human half is `art/CRITIQUE.md`.

The checks:

* **colour count** — at most 15 colours plus the transparent key per
  frame (the machine's bank);
* **palette distinctness** — two colours that collapse under the 5/6/5
  quantisation are the SAME colour on the machine;
* **orphan pixels** — an opaque pixel with no opaque neighbour is a speck;
* **silhouette connectivity** — the shape is one connected component
  (an allowance flag for sprites that are legitimately several pieces);
* **baseline consistency** — every frame of a state stands on the same
  row;
* **bounding-box jitter** — the frames' boxes do not wander;
* **outline coverage** — the silhouette's edge is made of the outline
  colour;
* **fill-to-outline contrast** — the outline is clearly darker than the
  fill;
* **readability** — the frame's bounding box is at least 8 px tall.

Deterministic: the checks read the pixels and count them.
"""

import json

from . import canvas as _canvas

__all__ = ["QaError", "check_strip", "load_strip", "CHECKS"]


class QaError(Exception):
    """A teaching error: what is wrong and what to do about it."""


# The checks, in the order the report lists them.
CHECKS = ("colour_count", "palette_distinct", "orphans", "components",
          "baseline", "jitter", "outline", "contrast", "readable")


def load_png(path):
    """The PNG at `path` -> (w, h, rows) with rows of (r, g, b, a)."""
    import arc1_assetc

    with open(path, "rb") as f:
        data = f.read()
    try:
        w, h, pixels = arc1_assetc.decode_png(data, path)
    except arc1_assetc.ImageError as e:
        raise QaError(str(e))
    except arc1_assetc.UnsupportedImage as e:
        raise QaError(str(e))
    rows = []
    for y in range(h):
        rows.append([pixels[y * w + x] for x in range(w)])
    return w, h, rows


def split_frames(w, h, rows, frame=None):
    """The strip's frames, side by side."""
    if frame:
        fw, fh = frame
        if fh > h:
            raise QaError("the frame height %d does not fit in a %d px image"
                           % (fh, h))
        if fw * (w // fw if fw else 1) > w:
            raise QaError("the frame width %d does not fit in a %d px image"
                           % (fw, w))
    else:
        fw = fh = h            # square frames by default
    if fw <= 0 or fh <= 0:
        raise QaError("the frame size must be positive")
    frames = []
    x = 0
    while x + fw <= w:
        frame_rows = [row[x:x + fw] for row in rows[:fh]]
        frames.append(frame_rows)
        x += fw
    if not frames:
        raise QaError("the image holds no frame")
    return frames, fw, fh


def _opaque(p, key=None):
    """Is this pixel part of the sprite?

    The transparent pixel of the machine's palette is index 0 — the key
    colour — so a strip PNG paints its background in that colour. The key
    is the pipeline's convention (#000000) unless the caller says
    otherwise.
    """
    if p[3] < 128:
        return False
    if key is not None and p[0] == key[0] and p[1] == key[1] and p[2] == key[2]:
        return False
    return True


def _components(frame, fw, fh, key=None):
    """The 4-connected components of the opaque pixels, and their sizes."""
    seen = [[False] * fw for _ in range(fh)]
    groups = []
    for y in range(fh):
        for x in range(fw):
            if seen[y][x] or not _opaque(frame[y][x], key):
                continue
            group = []
            stack = [(x, y)]
            seen[y][x] = True
            while stack:
                cx, cy = stack.pop()
                group.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy),
                               (cx, cy + 1), (cx, cy - 1)):
                    if (0 <= nx < fw and 0 <= ny < fh and
                            not seen[ny][nx] and
                            _opaque(frame[ny][nx], key)):
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            groups.append(group)
    groups.sort(key=lambda g: (-len(g), g[0]))
    return groups


def _bbox(frame, fw, fh, key=None):
    minx, miny, maxx, maxy = fw, fh, -1, -1
    for y in range(fh):
        for x in range(fw):
            if _opaque(frame[y][x], key):
                minx = min(minx, x)
                maxx = max(maxx, x)
                miny = min(miny, y)
                maxy = max(maxy, y)
    if maxx < 0:
        return None
    return (minx, miny, maxx - minx + 1, maxy - miny + 1)


def _luminance(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def check_strip(path, frame=None, allow_components=1, max_colours=15,
                min_height=8, max_jitter=2, outline_colour=None,
                min_contrast=0.55, key=(0, 0, 0)):
    """Run every check over the strip at `path`.

    Returns a report dict: {file, frames, size, checks, failures, ok}.
    """
    w, h, rows = load_png(path)
    frames, fw, fh = split_frames(w, h, rows, frame)

    failures = []
    per_frame = []
    bottoms = set()
    boxes = []

    for i, f in enumerate(frames):
        opaque = [p for row in f for p in row if _opaque(p, key)]
        colours = sorted(set(opaque))
        info = {"frame": i + 1, "colours": len(colours),
                "components": 0, "orphans": 0, "bbox": None,
                "outline_coverage": 0.0, "contrast": 0.0}

        # --- the colour count --------------------------------------------
        if len(colours) > max_colours:
            failures.append("frame %d uses %d colours (the bank holds %d)"
                            % (i + 1, len(colours), max_colours))

        # --- the palette distinctness (5/6/5) -----------------------------
        seen = {}
        for c in colours:
            q = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
            if q in seen:
                failures.append(
                    "frame %d: %s and %s are the same colour on the machine "
                    "(they collapse under the 5/6/5 quantisation)"
                    % (i + 1, _hex(seen[q]), _hex(c)))
            else:
                seen[q] = c

        # --- the components and the orphans -------------------------------
        groups = _components(f, fw, fh, key)
        info["components"] = len(groups)
        if len(groups) > allow_components:
            failures.append("frame %d has %d separate pieces (the silhouette "
                            "should be one; use --allow-components to allow "
                            "it)" % (i + 1, len(groups)))
        orphans = 0
        for g in groups:
            if len(g) == 1:
                orphans += 1
        info["orphans"] = orphans
        if orphans:
            failures.append("frame %d has %d orphan pixel(s) — a single "
                            "opaque pixel with no opaque neighbour"
                            % (i + 1, orphans))

        # --- the bounding box, the baseline, the jitter --------------------
        box = _bbox(f, fw, fh, key)
        info["bbox"] = list(box) if box else None
        if box:
            boxes.append(box)
            bottoms.add(box[1] + box[3] - 1)
            if box[3] < min_height:
                failures.append("frame %d is %d px tall (at least %d are "
                                "readable)" % (i + 1, box[3], min_height))
        else:
            failures.append("frame %d is empty" % (i + 1))

        # --- the outline coverage -----------------------------------------
        edge = 0
        outlined = 0
        for y in range(fh):
            for x in range(fw):
                if not _opaque(f[y][x], key):
                    continue
                touches = False
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if (0 <= nx < fw and 0 <= ny < fh and
                            not _opaque(f[ny][nx], key)):
                        touches = True
                if not touches:
                    continue
                edge += 1
                c = f[y][x]
                if outline_colour is not None:
                    if _same(c, outline_colour):
                        outlined += 1
                else:
                    # the darkest colour in the frame is the outline
                    if c == min(colours, key=_luminance):
                        outlined += 1
        coverage = (outlined / float(edge)) if edge else 1.0
        info["outline_coverage"] = round(coverage, 3)
        if edge and coverage < 0.75:
            failures.append("frame %d: only %.0f%% of the silhouette's edge "
                            "is the outline colour (the rest is fill) — the "
                            "sprite has no readable edge"
                            % (i + 1, coverage * 100))

        # --- the fill-to-outline contrast ----------------------------------
        if outline_colour is not None:
            outline_lum = _luminance(outline_colour)
            fill_lum = 0.0
            n = 0
            for y in range(fh):
                for x in range(fw):
                    c = f[y][x]
                    if not _opaque(c, key) or _same(c, outline_colour):
                        continue
                    fill_lum += _luminance(c)
                    n += 1
            contrast = (outline_lum / (fill_lum / n)) if n and fill_lum else 1.0
            info["contrast"] = round(contrast, 3)
            if n and contrast > min_contrast:
                failures.append("frame %d: the outline is not dark enough "
                                "against the fill (the contrast ratio is "
                                "%.2f, at most %.2f)"
                                % (i + 1, contrast, min_contrast))

        per_frame.append(info)

    # --- the baseline and the jitter (over the whole strip) ----------------
    if len(bottoms) > 1:
        failures.append("the frames do not share a baseline: the lowest "
                        "opaque row is %s (the feet must stand on one row)"
                        % sorted(bottoms))
    if boxes:
        # The jitter is the movement of the character's ANCHOR (the centre
        # of the bounding box): a limb swinging changes the box's width,
        # but the character itself must not wander from frame to frame.
        centres = [b[0] + b[2] / 2.0 for b in boxes]
        jitter = max(centres) - min(centres)
        if jitter > max_jitter:
            failures.append("the frames' anchor jitters by %.1f px (at most "
                            "%d) — the character wanders between the frames"
                            % (jitter, max_jitter))

    return {
        "file": path,
        "key": list(key) if key else None,
        "frames": len(frames),
        "size": [fw, fh],
        "checks": list(CHECKS),
        "jitter": round(jitter, 2) if boxes else 0,
        "per_frame": per_frame,
        "failures": failures,
        "ok": not failures,
    }


def _hex(c):
    return "#%02X%02X%02X" % (c[0], c[1], c[2])


def _same(a, b):
    return a[0] == b[0] and a[1] == b[1] and a[2] == b[2]
