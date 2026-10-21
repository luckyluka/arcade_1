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
import os

from . import canvas as _canvas

__all__ = ["QaError", "check_strip", "check_manifest", "load_strip",
           "CHECKS"]


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
        raise QaError(
            "the image holds no frame of %dx%d — the frame size is ONE "
            "frame, so it must divide the strip's width (%d px). For a "
            "one-frame tileset the frame is the whole image."
            % (fw, fh, w))
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
                min_contrast=0.55, key=(0, 0, 0), skip_outline=False):
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
        # The outline colour is the DARKEST colour among the edge pixels
        # (the outline is on the edge by definition) — not the darkest in
        # the whole frame, which a sprite may beat with a dark fill (a
        # black hair against a dark outline). An explicit --outline-colour
        # overrides the guess.
        edge_pixels = []
        for y in range(fh):
            for x in range(fw):
                if not _opaque(f[y][x], key):
                    continue
                touches = False
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if (0 <= nx < fw and 0 <= ny < fh and
                            not _opaque(f[ny][nx], key)):
                        touches = True
                if touches:
                    edge_pixels.append(f[y][x])
        edge = len(edge_pixels)
        if edge and skip_outline:
            info["outline_check"] = "skipped (--skip-outline)"
            edge = 0
        if edge:
            if outline_colour is None:
                outline_colour = min(edge_pixels, key=_luminance)
            outlined = sum(1 for c in edge_pixels
                           if _same(c, outline_colour))
            coverage = outlined / float(edge)
            info["outline_coverage"] = round(coverage, 3)
            if coverage < 0.75:
                failures.append("frame %d: only %.0f%% of the silhouette's "
                                "edge is the outline colour (the rest is "
                                "fill) — the sprite has no readable edge"
                                % (i + 1, coverage * 100))
        else:
            # A fully opaque strip (a background layer, a tileset) has no
            # edge at all: the check does not apply, and saying so beats a
            # silent pass (ROADMAP_V4 card 7.8, the round's doc gap 3).
            info["outline_coverage"] = None
            info["outline_check"] = "does not apply — the frame is fully " \
                                    "opaque, so it has no edge"

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


# --------------------------------------------------------------------------
# The manifest's maps
# --------------------------------------------------------------------------

def check_manifest(manifest_path, key=(0, 0, 0)):
    """Check what a manifest's MAPS say, not just its strips.

    The strip checks look at sprites; these look at the world:

    * **a tile nobody can draw** — the legend does not use every tile of
      the tileset. A tile added to the art but not given a character is
      invisible in the game (the round-7 agent added a sky band without
      its legend character, and only the captured game frame showed it);
    * **a map that draws nothing** — every cell empty;
    * **a legend that misses a character** the map file uses;
    * **a collision layer of the wrong size**.

    Returns the same shape as check_strip(): {file, checks, failures, ok}.
    """
    if not os.path.isfile(manifest_path):
        raise QaError("no such manifest: %s" % manifest_path)
    base = os.path.dirname(os.path.abspath(manifest_path))
    with open(manifest_path) as f:
        doc = json.load(f)
    if not isinstance(doc, dict) or doc.get("format") != 1:
        raise QaError("%s is not a format-1 manifest" % manifest_path)

    failures = []
    warnings = []
    sprites = {s.get("name"): s for s in doc.get("sprites", [])}
    maps = doc.get("maps", [])
    checks = []

    for i, m in enumerate(maps):
        name = m.get("name", "#%d" % (i + 1))
        where = "map %s" % name
        tileset = sprites.get(m.get("tileset"))
        if tileset is None:
            failures.append("%s: the tileset %r is not a sprite of the "
                            "manifest" % (where, m.get("tileset")))
            continue
        path = os.path.join(base, m.get("file", ""))
        rows = _read_map_rows(path)
        if rows is None:
            failures.append("%s: the map file %s is missing" % (where, path))
            continue

        tw = int(m.get("tile_w", 0))
        th = int(m.get("tile_h", 0))
        legend = m.get("legend", {}) or {}

        # --- a character the map uses but the legend does not name -------
        used = set()
        for row in rows:
            used.update(row)
        for ch in sorted(used):
            if ch not in legend:
                failures.append("%s: the character %r is in the map file "
                                "but has no legend entry" % (where, ch))

        # --- a tile the legend never uses --------------------------------
        # the tileset's grid: how many tiles it holds
        sprite_w = sprite_h = 0
        sprite_path = os.path.join(base, tileset.get("file", ""))
        if os.path.isfile(sprite_path):
            sw, sh, _rows = _read_png_size(sprite_path)
            sprite_w, sprite_h = sw, sh
        if sprite_w and tw and th:
            n_tiles = (sprite_w // tw) * (sprite_h // th)
            used_tiles = set()
            for spec in legend.values():
                if spec is None:
                    continue
                idx = spec if isinstance(spec, int) else spec.get("tile")
                if isinstance(idx, int):
                    used_tiles.add(idx)
            missing = sorted(set(range(n_tiles)) - used_tiles)
            if missing:
                # A WARNING, not a failure: spare tiles are legitimate (a
                # variants set). But a tile that was just added and has no
                # character is invisible in the game — the round-7 agent
                # added a sky band that way, and only the captured frame
                # showed it.
                warnings.append(
                    "%s: the tileset %s holds %d tile(s) and the legend "
                    "never uses %s — a tile with no character cannot be "
                    "drawn (it is invisible in the game)"
                    % (where, tileset.get("name"), n_tiles,
                       ", ".join(str(t) for t in missing)))

        # --- a map that draws nothing -------------------------------------
        # A cell is empty when its legend entry is null (or missing) — NOT
        # when the tile index is 0, which is a real tile.
        def _empty(ch):
            spec = legend.get(ch)
            if spec is None:
                return True
            if isinstance(spec, dict):
                return spec.get("tile") is None
            return False

        if legend and not any(not _empty(ch) for row in rows for ch in row):
            failures.append("%s: every cell is empty — the map draws "
                            "nothing" % where)

        # --- the collision layer's size ----------------------------------
        solid = m.get("solid")
        if isinstance(solid, str):
            solid_path = os.path.join(base, solid)
            srows = _read_map_rows(solid_path)
            if srows is None:
                failures.append("%s: the collision layer %s is missing"
                                % (where, solid))
            elif len(srows) != len(rows) or any(len(r) != len(rows[0])
                                               for r in srows):
                failures.append("%s: the collision layer is %dx%d, the map is "
                                "%dx%d — they must be the same size"
                                % (where, len(srows[0]) if srows else 0,
                                   len(srows), len(rows[0]), len(rows)))
        checks.append(name)

    return {"file": manifest_path, "maps": checks,
            "checks": ["legend_covers_the_map", "every_tile_is_drawable",
                       "the_map_draws_something", "the_collision_layer_fits"],
            "warnings": warnings, "failures": failures, "ok": not failures}


def _read_map_rows(path):
    """The map file's rows (ASCII or the JSON form), or None."""
    if not os.path.isfile(path):
        return None
    if path.endswith(".json"):
        try:
            with open(path) as f:
                doc = json.load(f)
        except (OSError, ValueError):
            return None
        rows = doc.get("rows") if isinstance(doc, dict) else None
        return rows if isinstance(rows, list) else None
    try:
        with open(path) as f:
            return [line.rstrip("\r\n") for line in f if line.strip()]
    except OSError:
        return None


def _read_png_size(path):
    import struct
    with open(path, "rb") as f:
        data = f.read(24)
    return struct.unpack(">II", data[16:24]) + (None,)
