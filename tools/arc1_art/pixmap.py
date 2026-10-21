"""pixmap — the `.px` text pixmap format (ROADMAP_V4 card 7.3).

A pixmap is a sprite written as text: a header line, then the pixels, one
row per line, with `--` between the frames.

    # name: coin   size: 8x8   palette: warm16   bank: 3
    ..1111..
    .122221.
    12333321
    ...
    --
    (the next frame, same size)

* `.` is transparent; `0`-`9` and `a`-`f` are palette indices (index 0
  is the palette's transparent slot, so `.` is the usual way to say it).
* The header keys: `name`, `size` (WxH), `palette` (a preset from
  art/palettes/), `bank` (0..15) and the optional `anchor`
  (`feet` — the default — or `center`).
* An optional `provenance` header carries the JSON block of
  docs/art/PROVENANCE.md, so a hand-authored pixmap says who made it.

The format exists because small sprites are the one thing an agent writes
well: a grid of characters is diffable, reviewable and impossible to
corrupt silently. The pixels still travel to the machine the same way
everything does — a strip PNG, compiled by arc1-assetc.

Deterministic by construction: no clock, no randomness, sorted iteration.
"""

import json
import os

from . import canvas as _canvas

__all__ = ["PxError", "parse_px", "frames_to_indices", "write_strip",
           "write_sheet", "DIGITS"]


# --------------------------------------------------------------------------
# Errors
# --------------------------------------------------------------------------

class PxError(Exception):
    """A teaching error: where it happened and what to do about it."""

    def __init__(self, path, line, message):
        self.path = path
        self.line = line
        self.message = message
        if line:
            text = "%s:%d: %s" % (path, line, message)
        else:
            text = "%s: %s" % (path, message)
        super().__init__(text)


# --------------------------------------------------------------------------
# A tiny 3x5 digit font, for the frame numbers on a contact sheet.
# Each glyph is five rows of three bits (left to right, 1 = ink).
# --------------------------------------------------------------------------

DIGITS = {
    "0": (0b111, 0b101, 0b101, 0b101, 0b111),
    "1": (0b010, 0b110, 0b010, 0b010, 0b111),
    "2": (0b111, 0b001, 0b111, 0b100, 0b111),
    "3": (0b111, 0b001, 0b111, 0b001, 0b111),
    "4": (0b101, 0b101, 0b111, 0b001, 0b001),
    "5": (0b111, 0b100, 0b111, 0b001, 0b111),
    "6": (0b111, 0b100, 0b111, 0b101, 0b111),
    "7": (0b111, 0b001, 0b010, 0b010, 0b010),
    "8": (0b111, 0b101, 0b111, 0b101, 0b111),
    "9": (0b111, 0b101, 0b111, 0b001, 0b111),
    "-": (0b000, 0b000, 0b111, 0b000, 0b000),
    " ": (0b000, 0b000, 0b000, 0b000, 0b000),
}


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------

def _parse_header(path, line, lineno):
    """The header line: '# key: value key: value ...'.

    The value ends at the next space — except `provenance`, whose value is
    a JSON object and takes the rest of the line.
    """
    fields = {}
    rest = line[1:].strip()
    while rest:
        if ":" not in rest:
            raise PxError(path, lineno,
                          "header field %r has no ':' — write 'key: value'"
                          % rest.split(" ")[0])
        key, rest = rest.split(":", 1)
        key = key.strip().lower()
        rest = rest.strip()
        if not rest:
            raise PxError(path, lineno,
                          "header field %r has no value" % key)
        if key == "provenance":
            fields[key] = rest                 # the rest of the line
            break
        value, _, rest = rest.partition(" ")
        fields[key] = value.strip()
        rest = rest.strip()
    return fields


def parse_px(path, text=None):
    """Parse a `.px` file into
    {name, w, h, palette, bank, anchor, provenance, frames}.

    `frames` is a list of frames; each frame is a list of h rows, each row
    a list of w palette indices (0 = transparent).

    Raises PxError with the file and the line for every mistake.
    """
    if text is None:
        with open(path) as f:
            text = f.read()
    lines = text.split("\n")

    header = None
    header_line = 0
    body = []                 # (lineno, text) of the pixel lines and '--'
    for i, raw in enumerate(lines):
        lineno = i + 1
        stripped = raw.strip()
        if header is None:
            if not stripped:
                continue
            if not stripped.startswith("#"):
                raise PxError(path, lineno,
                              "the first line must be a header ('# name: ...')")
            header = _parse_header(path, stripped, lineno)
            header_line = lineno
            continue
        if not stripped:
            continue
        if stripped.startswith("#"):
            # A later header line may carry the provenance block.
            extra = _parse_header(path, stripped, lineno)
            for key, value in extra.items():
                header.setdefault(key, value)
            continue
        body.append((lineno, stripped))

    if header is None:
        raise PxError(path, 0, "the file has no header line")

    for key in ("name", "size", "palette", "bank"):
        if key not in header:
            raise PxError(path, header_line,
                          "the header has no '%s:' field" % key)

    size = header["size"].lower()
    if "x" not in size:
        raise PxError(path, header_line,
                      "size %r is not WxH" % header["size"])
    try:
        w, h = (int(part) for part in size.split("x"))
    except ValueError:
        raise PxError(path, header_line,
                      "size %r is not WxH" % header["size"])
    if w <= 0 or h <= 0:
        raise PxError(path, header_line,
                      "size %dx%d must be positive" % (w, h))

    try:
        bank = int(header["bank"])
    except ValueError:
        raise PxError(path, header_line,
                      "bank %r is not an integer" % header["bank"])
    if not 0 <= bank <= 15:
        raise PxError(path, header_line,
                      "bank %d is out of range (0..15)" % bank)

    anchor = header.get("anchor", "feet").lower()
    if anchor not in ("feet", "center"):
        raise PxError(path, header_line,
                      "anchor %r is not 'feet' or 'center'" % anchor)

    provenance = None
    if "provenance" in header:
        try:
            provenance = json.loads(header["provenance"])
        except ValueError as e:
            raise PxError(path, header_line,
                          "provenance is not valid JSON: %s" % e)

    # --- the frames -------------------------------------------------------
    frames = []
    current = []
    current_start = 0
    for lineno, stripped in body:
        if stripped == "--":
            if current:
                frames.append((current_start, current))
            current = []
            continue
        if not current:
            current_start = lineno
        row = []
        for ch in stripped:
            if ch == ".":
                row.append(0)
                continue
            if ch in "0123456789abcdefABCDEF":
                row.append(int(ch, 16))
                continue
            raise PxError(path, lineno,
                          "unknown character %r (use . or 0-9a-f)" % ch)
        if len(row) != w:
            raise PxError(path, lineno,
                          "row has %d pixels, expected %d" % (len(row), w))
        current.append(row)
    if current:
        frames.append((current_start, current))

    if not frames:
        raise PxError(path, 0, "the file has no pixels")

    for i, (start, frame) in enumerate(frames):
        if len(frame) != h:
            raise PxError(path, 0,
                          "frame %d is %dx%d, expected %dx%d"
                          % (i + 1, w, len(frame), w, h))

    return {
        "name": header["name"],
        "w": w,
        "h": h,
        "palette": header["palette"],
        "bank": bank,
        "anchor": anchor,
        "provenance": provenance,
        "frames": [f for _, f in frames],
    }


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------

def frames_to_indices(pixmap):
    """The frames as flat lists of indices (the canvas format)."""
    return [[value for row in frame for value in row]
            for frame in pixmap["frames"]]


def _palette_colours(preset):
    """The preset's colours as 8-bit RGB, index 0 = the key (transparent).

    The presets are authored in 5/6/5 and stored as the exact 8-bit
    expansion, so the PNG and the palette agree pixel for pixel and
    arc1-assetc's exact quantiser maps every one of them back.
    """
    return _canvas.palette_rgb(preset["colors"])


def write_strip(path, pixmap, preset):
    """Write the sprite's strip PNG: the frames side by side.

    `preset` is the palette preset (art/palettes/<name>.json). The pixels
    are written in the palette's own 8-bit colours, so arc1-assetc's
    quantiser maps them back to the same indices.
    """
    w, h = pixmap["w"], pixmap["h"]
    exp = _palette_colours(preset)
    frames = pixmap["frames"]
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for frame in frames:
            for x in range(w):
                v = frame[y][x]
                raw += bytes(exp[v] if v < len(exp) and exp[v] else (0, 0, 0))
    return _canvas.write_indexed_png(path, raw, w * len(frames), h)


def _draw_number(pixels, x, y, number, colour):
    """Draw a decimal number with the 3x5 digit font."""
    for digit in str(number):
        glyph = DIGITS.get(digit, DIGITS[" "])
        for dy, bits in enumerate(glyph):
            for dx in range(3):
                if bits & (1 << (2 - dx)):
                    px, py = x + dx, y + dy
                    if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
                        pixels[py][px] = colour
        x += 4


def write_sheet(path, pixmap, preset, scale=8):
    """Write the preview sheet: every frame at 1x (top) and at `scale`
    (below), with the frame numbers.

    The 1x row is what the machine shows; the scaled row is what a human
    needs to judge the art.
    """
    w, h = pixmap["w"], pixmap["h"]
    exp = _palette_colours(preset)
    frames = pixmap["frames"]
    n = len(frames)

    label_h = 6
    gap = 2
    sheet_w = n * (w * scale + gap) + gap
    sheet_h = gap + h + gap + label_h + gap + h * scale + gap

    pixels = [[0] * sheet_w for _ in range(sheet_h)]

    def put(x, y, index):
        if 0 <= y < sheet_h and 0 <= x < sheet_w:
            pixels[y][x] = index

    for i, frame in enumerate(frames):
        x0 = gap + i * (w * scale + gap)
        for y in range(h):
            for x in range(w):
                v = frame[y][x]
                if v:
                    for sy in range(scale):
                        for sx in range(scale):
                            put(x0 + x * scale + sx, gap + y * scale + sy, v)
        # the 1x row, under the numbers
        for y in range(h):
            for x in range(w):
                v = frame[y][x]
                if v:
                    put(gap + i * (w * scale + gap) + x,
                        gap + h + gap + label_h + y, v)
        _draw_number(pixels, gap + i * (w * scale + gap),
                     gap + h + gap, i + 1, len(exp) - 1)

    raw = bytearray()
    for y in range(sheet_h):
        raw.append(0)
        for x in range(sheet_w):
            v = pixels[y][x]
            col = exp[v] if v < len(exp) and exp[v] else (0, 0, 0)
            raw += bytes(col)
    return _canvas.write_indexed_png(path, raw, sheet_w, sheet_h)


def load_preset(palettes_dir, name):
    """Read a palette preset (art/palettes/<name>.json)."""
    path = os.path.join(palettes_dir, name + ".json")
    if not os.path.isfile(path):
        raise PxError(path, 0, "no such palette preset")
    with open(path) as f:
        return json.load(f)
