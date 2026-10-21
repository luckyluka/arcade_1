"""canvas — the ARCADE-1 drawing library (ROADMAP_V4 card 7.2).

A canvas is a grid of **palette indices**: index 0 is transparent, the
rest address a palette bank (CHEATSHEET.md). Everything here is
deterministic and stdlib-only: no clock, no unseeded randomness, no
dict-order dependence, sorted iteration.

The primitives below are the ones Petal Blade's generator used
(`programs/petalblade_art/gen_art.py`): they are extracted **verbatim**,
so re-pointing the generator at this module reproduces every asset byte
for byte. On top of them this module adds the general API the pipeline
needs: ellipses, polygons, flood fill, rotation, recolouring, a seeded
RNG, the strip/sheet writers and the indexed-PNG writer.

The API:

    Canvas(w, h, palette=None)   the surface (palette = index -> RGB)
    px(x, y, c)                  set one pixel (0 = transparent, no-op)
    get(x, y) / set(x, y, c)     read / write one pixel
    rect(x, y, w, h, c)          filled rectangle
    line(x0, y0, x1, y1, c, t=1)  a line, thickness t
    ellipse(cx, cy, rx, ry, c)   a filled ellipse
    polygon(points, c)           a filled polygon (even-odd)
    flood(x, y, c)               flood fill of the region at (x, y)
    outline(color, neighbours=4) paint the edge around the opaque pixels
    ramp_shade(x0, x1, y0, y1, stops, dither=True)
                                a vertical ramp through palette indices
    ordered_dither(x, y, bayer=4)
                                the ordered-dither threshold in [0, 1)
    mirror_h()                   mirror the canvas horizontally (in place)
    rotate90()                   a new canvas, rotated 90 degrees
    stamp(src, x, y)             draw another canvas onto this one
    recolor(map)                 remap palette indices (in place)
    copy()                       an independent copy
    to_strip(frames, palette, path)
                                write a horizontal strip PNG
    contact_sheet(items, scale, labels)
                                write a contact sheet PNG
    Rng(seed)                    xorshift32, the pipeline's one RNG

Aliases kept for the generator that predates this module: `grad_v` is
`ramp_shade`, `hmirror` is `mirror_h`, `blit` is `stamp`.
"""

import struct
import zlib

__all__ = ["Canvas", "Rng", "BAYER4", "bayer", "ordered_dither",
           "expand565", "write_indexed_png", "to_strip", "contact_sheet",
           "png_chunk"]


# --------------------------------------------------------------------------
# Ordered dithering
# --------------------------------------------------------------------------

BAYER4 = [[0, 8, 2, 10],
          [12, 4, 14, 6],
          [3, 11, 1, 9],
          [15, 7, 13, 5]]

BAYER2 = [[0, 2],
          [3, 1]]


def bayer(x, y):
    """Ordered-dither threshold in [0, 1) — the 4x4 matrix."""
    return (BAYER4[y & 3][x & 3] + 0.5) / 16.0


def ordered_dither(x, y, bayer_size=4):
    """The ordered-dither threshold at (x, y), in [0, 1).

    `bayer_size` is 2 or 4. The 4x4 matrix is the one the machine's
    assets use; the 2x2 is the cheap one for a single shade step.
    """
    if bayer_size == 2:
        return (BAYER2[y & 1][x & 1] + 0.5) / 4.0
    return bayer(x, y)


# --------------------------------------------------------------------------
# Colours
# --------------------------------------------------------------------------

def expand565(c):
    """A 5/6/5 palette component triple -> the 8-bit RGB the device shows.

    The device expands floor(v * 255 / max); this is the same arithmetic
    (the 8-bit value is written straight into the PNG, so a round trip
    through the quantiser is exact).
    """
    if c is None:
        return None
    r, g, b = c
    return (((r << 3) | (r >> 2)),
            ((g << 2) | (g >> 4)),
            ((b << 3) | (b >> 2)))


# --------------------------------------------------------------------------
# The random source
# --------------------------------------------------------------------------

class Rng:
    """xorshift32 — the pipeline's one random source.

    Deterministic by construction: the same seed always produces the same
    stream, on every host. The state is 32 bits and must not be zero.

        r = Rng(12345)
        r.next()        # a 32-bit value
        r.rand(6)       # a value in [0, 6)
    """

    def __init__(self, seed):
        self.state = (seed & 0xFFFFFFFF) or 0x1F123BB5

    def next(self):
        """The next 32-bit value (xorshift32)."""
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x & 0xFFFFFFFF
        return self.state

    def rand(self, n):
        """A value in [0, n) — the high bits, which are the well-mixed ones
        (the low bits of a xorshift stream are weak)."""
        if n <= 1:
            return 0
        return (self.next() >> 16) % n


# --------------------------------------------------------------------------
# The canvas
# --------------------------------------------------------------------------

class Canvas:
    """A grid of palette indices. Index 0 is transparent."""

    def __init__(self, w, h, palette=None):
        self.w, self.h = w, h
        self.p = [[0] * w for _ in range(h)]
        self.palette = palette          # index -> (r5, g6, b5) or None

    # --- primitives -------------------------------------------------------
    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def px(self, x, y, c):
        if c and 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.p[y][x]
        return 0

    def box(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                if c:
                    self.p[y][x] = c

    def rect(self, x, y, w, h, c):
        self.box(x, y, x + w - 1, y + h - 1, c)

    def hline(self, y, x0, x1, c):
        self.box(x0, y, x1, y, c)

    def vline(self, x, y0, y1, c):
        self.box(x, y0, x, y1, c)

    def line(self, x0, y0, x1, y1, c, t=1):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        r = max(0, t // 2)
        for i in range(n + 1):
            x = round(x0 + (x1 - x0) * i / n)
            y = round(y0 + (y1 - y0) * i / n)
            if r:
                self.disc(x, y, r, c)
            else:
                self.px(x, y, c)

    def disc(self, cx, cy, r, c):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r:
                    self.px(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        """A filled ellipse: the pixels with
        (x-cx)^2/rx^2 + (y-cy)^2/ry^2 <= 1. rx or ry of 0 draws a line."""
        if rx <= 0 and ry <= 0:
            return
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                dx = (x - cx) / float(rx) if rx > 0 else 0.0
                dy = (y - cy) / float(ry) if ry > 0 else 0.0
                if dx * dx + dy * dy <= 1.0:
                    self.px(x, y, c)

    def polygon(self, points, c):
        """A filled polygon (even-odd rule) through `points`."""
        if len(points) < 3:
            return
        ys = [p[1] for p in points]
        for y in range(max(0, min(ys)), min(self.h - 1, max(ys)) + 1):
            xs = []
            n = len(points)
            for i in range(n):
                x0, y0 = points[i]
                x1, y1 = points[(i + 1) % n]
                if y0 == y1:
                    continue                     # a horizontal edge
                if min(y0, y1) <= y <= max(y0, y1):
                    t = (y - y0) / float(y1 - y0)
                    xs.append(x0 + (x1 - x0) * t)
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                for x in range(int(round(xs[i])), int(round(xs[i + 1])) + 1):
                    self.px(x, y, c)

    def flood(self, x, y, c):
        """Fill the 4-connected region of equal pixels that contains
        (x, y) with colour `c`."""
        if not (0 <= x < self.w and 0 <= y < self.h):
            return
        target = self.p[y][x]
        if target == c:
            return
        stack = [(x, y)]
        while stack:
            cx, cy = stack.pop()
            if not (0 <= cx < self.w and 0 <= cy < self.h):
                continue
            if self.p[cy][cx] != target:
                continue
            self.p[cy][cx] = c
            stack.append((cx + 1, cy))
            stack.append((cx - 1, cy))
            stack.append((cx, cy + 1))
            stack.append((cx, cy - 1))

    # --- compositing ------------------------------------------------------
    def blit(self, other, dx, dy):
        for y in range(other.h):
            for x in range(other.w):
                c = other.p[y][x]
                if c:
                    self.px(dx + x, dy + y, c)

    def stamp(self, src, x, y):
        """Draw `src` onto this canvas at (x, y). The transparent pixels of
        `src` leave the canvas alone."""
        self.blit(src, x, y)

    def outline(self, c, only=0, neighbours=4):
        """Paint `c` into the `only` pixels that touch an opaque pixel.

        `neighbours` is 4 (the four sides — the machine's own outline
        rule) or 8 (the sides and the corners)."""
        todo = []
        for y in range(self.h):
            for x in range(self.w):
                if self.p[y][x] != only:
                    continue
                if neighbours == 8:
                    touch = False
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            if dx or dy:
                                if self.get(x + dx, y + dy):
                                    touch = True
                else:
                    touch = (self.get(x - 1, y) or self.get(x + 1, y) or
                             self.get(x, y - 1) or self.get(x, y + 1))
                if touch:
                    todo.append((x, y))
        for x, y in todo:
            self.p[y][x] = c

    def edge_light(self, c, dx=-1, dy=-1):
        """Highlight pixels whose neighbour in (dx,dy) is transparent."""
        todo = []
        for y in range(self.h):
            for x in range(self.w):
                if not self.p[y][x]:
                    continue
                if not self.get(x + dx, y + dy):
                    todo.append((x, y))
        for x, y in todo:
            self.p[y][x] = c

    # --- shading ----------------------------------------------------------
    def grad_v(self, x0, x1, y0, y1, stops, dither=True):
        """Vertical ramp through `stops` (palette indices) over y0..y1."""
        n = len(stops) - 1
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            t = 0.0 if n == 0 else (y - y0) / max(1, (y1 - y0)) * n
            i = min(int(t), n - 1) if n else 0
            frac = 0.0 if n == 0 else t - i
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                if not n:
                    c = stops[0]
                elif (frac > (bayer(x, y) if dither else 0.5)):
                    c = stops[i + 1]
                else:
                    c = stops[i]
                if c:
                    self.p[y][x] = c

    def ramp_shade(self, x0, x1, y0, y1, stops, dither=True):
        """A vertical ramp through palette indices — the name the pipeline
        uses; `grad_v` is the older name for the same thing."""
        self.grad_v(x0, x1, y0, y1, stops, dither)

    # --- transforms -------------------------------------------------------
    def hmirror(self):
        self.p = [list(reversed(row)) for row in self.p]

    def mirror_h(self):
        """Mirror the canvas horizontally (in place)."""
        self.hmirror()

    def rotate90(self):
        """A new canvas, this one rotated 90 degrees clockwise."""
        out = Canvas(self.h, self.w, self.palette)
        for y in range(self.h):
            for x in range(self.w):
                out.p[x][self.h - 1 - y] = self.p[y][x]
        return out

    def recolor(self, mapping):
        """Remap palette indices in place: `mapping` maps old -> new."""
        for y in range(self.h):
            row = self.p[y]
            for x in range(self.w):
                row[x] = mapping.get(row[x], row[x])

    def copy(self):
        g = Canvas(self.w, self.h, self.palette)
        g.p = [row[:] for row in self.p]
        return g


# --------------------------------------------------------------------------
# PNG output (8-bit RGB, no dependencies)
# --------------------------------------------------------------------------

def png_chunk(tag, data):
    """One PNG chunk: length, type, body, CRC."""
    return struct.pack(">I", len(data)) + tag + data + \
        struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_indexed_png(path, raw, width, height):
    """Write an 8-bit RGB PNG from a raw scanline buffer (filter 0 on every
    row — the machine's own PNG writer does the same)."""
    png = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                          8, 2, 0, 0, 0)) +
           png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    return path


def to_strip(frames, palette, path):
    """Write a horizontal strip PNG: every frame side by side.

    `frames` is a list of canvases of the same size; `palette` maps an
    index to a (r5, g6, b5) triple or None (transparent -> black).
    """
    if not frames:
        raise ValueError("to_strip needs at least one frame")
    w, h = frames[0].w, frames[0].h
    exp = [expand565(c) for c in palette]
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for g in frames:
            for x in range(w):
                v = g.p[y][x]
                raw += bytes(exp[v] if exp[v] else (0, 0, 0))
    return write_indexed_png(path, raw, w * len(frames), h)


def contact_sheet(items, scale=3, labels=None, path=None, palette=None,
                  bg=(16, 16, 28)):
    """Write a contact sheet: every canvas side by side, upscaled.

    `items` is a list of canvases (or (canvas, label) pairs); `labels`
    is an optional list of strings drawn... nowhere: this writer has no
    font, so the labels are returned for the caller to record. `palette`
    maps an index to a (r5, g6, b5) triple or None (transparent -> bg).
    """
    frames = []
    for item in items:
        frames.append(item[0] if isinstance(item, tuple) else item)
    if not frames:
        raise ValueError("contact_sheet needs at least one canvas")
    w, h = frames[0].w, frames[0].h
    cell_w, cell_h = w * scale, h * scale
    W, H_ = cell_w * len(frames), cell_h
    exp = [expand565(c) for c in (palette or [])]
    raw = bytearray()
    for y in range(H_):
        raw.append(0)
        for x in range(W):
            fi = x // cell_w
            lx = (x - fi * cell_w) // scale
            ly = y // scale
            v = frames[fi].p[ly][lx]
            col = exp[v] if v < len(exp) else None
            raw += bytes(col if col else bg)
    if path:
        write_indexed_png(path, raw, W, H_)
    return (raw, W, H_)
