"""preview — the contact sheet of a manifest (ROADMAP_V4 card 7.8).

The review checklist (`art/CRITIQUE.md`) asks a human to look at a
contact sheet at 1x and 4x — and until now the pipeline had no way to
make one, so the round-6 agent wrote its own scratch tool. This is the
shipped version: `arc1-art preview <assets.json> --out sheet.png`.

The sheet shows every sprite of the manifest: its frames side by side, at
1x (what the machine shows) and scaled up (what a human needs to judge
the art), with the sprite's name under it.

Deterministic: the same manifest always writes the same sheet.
"""

import json
import os

from . import canvas as _canvas

__all__ = ["PreviewError", "preview_manifest"]


class PreviewError(Exception):
    """A teaching error: what is wrong and what to do about it."""


def _read_png(path):
    """The PNG at `path` -> (w, h, rows of (r, g, b, a))."""
    import arc1_assetc

    with open(path, "rb") as f:
        data = f.read()
    try:
        w, h, pixels = arc1_assetc.decode_png(data, os.path.basename(path))
    except arc1_assetc.ImageError as e:
        raise PreviewError(str(e))
    except arc1_assetc.UnsupportedImage as e:
        raise PreviewError(str(e))
    rows = []
    for y in range(h):
        rows.append([pixels[y * w + x] for x in range(w)])
    return w, h, rows


def _digits(pixels, x, y, number, colour, w, h):
    """A 3x5 number, in the pixels' own colours."""
    from .pixmap import DIGITS
    for digit in str(number):
        glyph = DIGITS.get(digit, DIGITS[" "])
        for dy, bits in enumerate(glyph):
            for dx in range(3):
                if bits & (1 << (2 - dx)):
                    px, py = x + dx, y + dy
                    if 0 <= py < h and 0 <= px < w:
                        pixels[py][px] = colour
        x += 4


def preview_manifest(manifest_path, out_path, scale=4):
    """Write the contact sheet of every sprite in the manifest.

    The manifest's file paths are relative to the manifest's directory.
    """
    if not os.path.isfile(manifest_path):
        raise PreviewError("no such manifest: %s" % manifest_path)
    base = os.path.dirname(os.path.abspath(manifest_path))
    with open(manifest_path) as f:
        doc = json.load(f)
    if not isinstance(doc, dict) or doc.get("format") != 1:
        raise PreviewError("%s is not a format-1 manifest" % manifest_path)

    palettes = doc.get("palettes", {})
    entries = []
    for sprite in doc.get("sprites", []):
        name = sprite.get("name", "?")
        path = os.path.join(base, sprite.get("file", ""))
        if not os.path.isfile(path):
            raise PreviewError("the strip of %s is missing: %s" % (name, path))
        w, h, rows = _read_png(path)
        frames = max(1, int(sprite.get("frames", 1)))
        fw = w // frames
        if fw * frames != w:
            raise PreviewError("%s: the strip is %d px wide, not a whole "
                               "number of %d px frames" % (name, w, fw))
        pal = palettes.get(sprite.get("palette"), {})
        colours = pal.get("colors", [])
        entries.append({"name": name, "fw": fw, "fh": h, "rows": rows,
                        "colours": colours, "frames": frames})

    if not entries:
        raise PreviewError("the manifest has no sprites to preview")

    # the layout: one row per sprite — the frames at 1x, then at `scale`
    gap = 2
    label_h = 6
    row_h = max(e["fh"] for e in entries) + gap + label_h + \
        max(e["fh"] for e in entries) * scale + gap
    width = gap + max(sum(e["fw"] for e in entries) for e in entries) + \
        gap * max(e["frames"] for e in entries)
    height = gap + len(entries) * row_h

    pixels = [[(24, 24, 32)] * width for _ in range(height)]

    def put(x, y, colour):
        if 0 <= y < height and 0 <= x < width:
            pixels[y][x] = colour

    y = gap
    for number, entry in enumerate(entries, 1):
        colours = entry["colours"]
        rgb = []
        for hexcolor in colours:
            v = int(hexcolor.lstrip("#"), 16)
            rgb.append(((v >> 16) & 255, (v >> 8) & 255, v & 255))
        x = gap
        for f in range(entry["frames"]):
            for fy in range(entry["fh"]):
                for fx in range(entry["fw"]):
                    p = entry["rows"][fy][f * entry["fw"] + fx]
                    if p[3] < 128:
                        continue
                    idx = _nearest(p[:3], rgb)
                    if idx is None:
                        continue
                    colour = rgb[idx]
                    # the 1x row
                    put(x + fx, y, colour)
                    # the scaled row
                    for sy in range(scale):
                        for sx in range(scale):
                            put(x + fx * scale + sx,
                                y + entry["fh"] + gap + label_h + fy * scale + sy,
                                colour)
            # the frame number under the 1x row
            _digits(pixels, x - entry["fw"] - gap + 1, y + entry["fh"] + gap,
                    f + 1, (200, 200, 210), width, height)
            x += entry["fw"] + gap
        # the sprite's number (the legend is printed by the caller)
        _digits(pixels, 0, y + entry["fh"] + gap, number, (240, 200, 120),
                width, height)
        y += row_h

    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for colour in row:
            raw += bytes(colour)
    _canvas.write_indexed_png(out_path, raw, width, height)
    return out_path, [e["name"] for e in entries]


def _nearest(p, rgb):
    """The palette index of the colour nearest to p."""
    best, best_d = None, None
    for i, colour in enumerate(rgb):
        d = ((colour[0] - p[0]) ** 2 + (colour[1] - p[1]) ** 2 +
             (colour[2] - p[2]) ** 2)
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best
