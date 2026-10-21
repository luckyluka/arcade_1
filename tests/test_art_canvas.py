#!/usr/bin/env python3
"""tests/test_art_canvas.py — unit tests for the drawing library.

Every primitive is pinned by a HAND-COMPUTED pixel expectation (not by a
reference implementation): a 5x5 ellipse, a 3x3 outline, a filled
polygon, a flood fill, a rotation, a recolour, the dither thresholds and
the seeded RNG. If a primitive changes, one of these fails and says what
changed.

Run: python3 tests/test_art_canvas.py
Exit: 0 ok, 1 a check failed.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "tools"))

from arc1_art.canvas import (Canvas, Rng, bayer, ordered_dither,   # noqa: E402
                             expand565)

FAILURES = []
CHECKS = [0]


def check(name, got, want):
    CHECKS[0] += 1
    if got != want:
        FAILURES.append("%s:\n  got  %r\n  want %r" % (name, got, want))


def rows(canvas):
    """The canvas as a list of rows of indices (for the expectations)."""
    return [list(r) for r in canvas.p]


# --- px / get / set -------------------------------------------------------
def test_px():
    c = Canvas(4, 4)
    c.px(1, 2, 5)
    check("px sets a pixel", c.get(1, 2), 5)
    c.px(0, 0, 0)                       # transparent never writes
    check("px ignores colour 0", c.get(0, 0), 0)
    c.px(9, 9, 7)                      # outside is clipped
    check("px clips outside", c.get(9, 9), 0)
    c.set(3, 3, 0)                     # set DOES write transparent
    check("set writes transparent", c.p[3][3], 0)
    c.set(0, 0, 4)
    check("set writes", c.get(0, 0), 4)


# --- rect / box / hline / vline -------------------------------------------
def test_rect():
    c = Canvas(6, 4)
    c.rect(1, 1, 3, 2, 2)
    check("rect fills the box",
          rows(c),
          [[0, 0, 0, 0, 0, 0],
           [0, 2, 2, 2, 0, 0],
           [0, 2, 2, 2, 0, 0],
           [0, 0, 0, 0, 0, 0]])
    c2 = Canvas(6, 4)
    c2.rect(-1, 0, 2, 1, 3)            # clipped at the left edge
    check("rect clips outside",
          rows(c2),
          [[3, 0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0, 0]])


def test_lines():
    c = Canvas(5, 5)
    c.hline(2, 1, 3, 6)
    c.vline(4, 0, 1, 6)
    check("hline/vline",
          rows(c),
          [[0, 0, 0, 0, 6],
           [0, 0, 0, 0, 6],
           [0, 6, 6, 6, 0],
           [0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0]])


def test_line():
    c = Canvas(5, 5)
    c.line(0, 0, 4, 0, 3)
    check("horizontal line",
          rows(c),
          [[3, 3, 3, 3, 3],
           [0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0],
           [0, 0, 0, 0, 0]])
    c2 = Canvas(5, 5)
    c2.line(0, 0, 4, 4, 3)             # a diagonal, rounded
    check("diagonal line",
          rows(c2),
          [[3, 0, 0, 0, 0],
           [0, 3, 0, 0, 0],
           [0, 0, 3, 0, 0],
           [0, 0, 0, 3, 0],
           [0, 0, 0, 0, 3]])


# --- disc / ellipse -------------------------------------------------------
def test_disc():
    c = Canvas(5, 5)
    c.disc(2, 2, 1, 4)
    # the disc test is dx^2 + dy^2 <= r*r + r, so r=1 fills the whole 3x3
    # dx^2 + dy^2 <= r*r + r = 2, so the corners (1+1) count too: a 3x3
    # block centred at (2,2)
    check("disc r=1 fills the 3x3 block",
          rows(c),
          [[0, 0, 0, 0, 0],
           [0, 4, 4, 4, 0],
           [0, 4, 4, 4, 0],
           [0, 4, 4, 4, 0],
           [0, 0, 0, 0, 0]])


def test_ellipse():
    # A 5x5 ellipse, rx = ry = 2, centred at (2, 2): the pixels with
    # dx^2 + dy^2 <= 4 — the 3x3 block plus the four axis tips.
    c = Canvas(5, 5)
    c.ellipse(2, 2, 2, 2, 1)
    check("5x5 ellipse",
          rows(c),
          [[0, 0, 1, 0, 0],
           [0, 1, 1, 1, 0],
           [1, 1, 1, 1, 1],
           [0, 1, 1, 1, 0],
           [0, 0, 1, 0, 0]])
    # a flat ellipse (ry = 0) is a horizontal line
    c2 = Canvas(5, 3)
    c2.ellipse(2, 1, 2, 0, 1)
    check("flat ellipse is a line",
          rows(c2),
          [[0, 0, 0, 0, 0],
           [1, 1, 1, 1, 1],
           [0, 0, 0, 0, 0]])


# --- polygon --------------------------------------------------------------
def test_polygon():
    c = Canvas(6, 6)
    c.polygon([(1, 1), (4, 1), (4, 4), (1, 4)], 2)
    check("polygon fills the square (both edges)",
          rows(c),
          [[0, 0, 0, 0, 0, 0],
           [0, 2, 2, 2, 2, 0],
           [0, 2, 2, 2, 2, 0],
           [0, 2, 2, 2, 2, 0],
           [0, 2, 2, 2, 2, 0],
           [0, 0, 0, 0, 0, 0]])
    # a triangle: the diagonal edge is half-open at the top
    c2 = Canvas(5, 5)
    c2.polygon([(0, 0), (4, 0), (0, 4)], 3)
    check("triangle",
          rows(c2),
          [[3, 3, 3, 3, 3],
           [3, 3, 3, 3, 0],
           [3, 3, 3, 0, 0],
           [3, 3, 0, 0, 0],
           [3, 0, 0, 0, 0]])


# --- flood ----------------------------------------------------------------
def test_flood():
    c = Canvas(5, 5)
    c.rect(0, 0, 5, 5, 1)              # a wall
    for y in range(1, 4):              # a hole (set writes transparent;
        for x in range(1, 4):          # rect/px never paint colour 0)
            c.set(x, y, 0)
    c.flood(2, 2, 7)                   # fill the hole only
    check("flood fills the region",
          rows(c),
          [[1, 1, 1, 1, 1],
           [1, 7, 7, 7, 1],
           [1, 7, 7, 7, 1],
           [1, 7, 7, 7, 1],
           [1, 1, 1, 1, 1]])


# --- outline --------------------------------------------------------------
def test_outline():
    # A 3x3 outline: one opaque pixel in the middle, outlined with the
    # four side neighbours (4-neighbourhood) and all eight (8-neighbourhood).
    c = Canvas(3, 3)
    c.px(1, 1, 1)
    c.outline(2)
    check("3x3 outline (4 neighbours)",
          rows(c),
          [[0, 2, 0],
           [2, 1, 2],
           [0, 2, 0]])
    c2 = Canvas(3, 3)
    c2.px(1, 1, 1)
    c2.outline(2, neighbours=8)
    check("3x3 outline (8 neighbours)",
          rows(c2),
          [[2, 2, 2],
           [2, 1, 2],
           [2, 2, 2]])


# --- ramp / dither --------------------------------------------------------
def test_ramp():
    c = Canvas(4, 4)
    c.ramp_shade(0, 3, 0, 3, [1, 2], dither=False)
    # the 0.5 threshold means the lower half takes stops[0] and the
    # upper half stops[1] (t = y/3 * 1)
    check("ramp without dither",
          rows(c),
          [[1, 1, 1, 1],
           [1, 1, 1, 1],
           [2, 2, 2, 2],
           [2, 2, 2, 2]])


def test_dither_thresholds():
    check("bayer(0,0)", bayer(0, 0), 0.5 / 16)
    check("bayer(3,3)", bayer(3, 3), 5.5 / 16)
    check("ordered_dither 2x2", ordered_dither(0, 0, 2), 0.5 / 4)
    check("ordered_dither 2x2 (1,1)", ordered_dither(1, 1, 2), 1.5 / 4)


# --- transforms -----------------------------------------------------------
def test_mirror():
    c = Canvas(3, 2)
    c.px(0, 0, 1)
    c.px(2, 1, 2)
    c.mirror_h()
    check("mirror_h",
          rows(c),
          [[0, 0, 1],
           [2, 0, 0]])


def test_rotate90():
    c = Canvas(3, 2)
    c.px(0, 0, 1)
    c.px(2, 1, 2)
    r = c.rotate90()
    # clockwise: the source (0,0) goes to (h-1-0, 0) = (1,0), and (2,1)
    # goes to (h-1-1, 2) = (0,2)
    check("rotate90 is clockwise",
          rows(r),
          [[0, 1],
           [0, 0],
           [2, 0]])


def test_recolor():
    c = Canvas(2, 1)
    c.px(0, 0, 1)
    c.px(1, 0, 5)
    c.recolor({1: 9})
    check("recolor remaps",
          rows(c),
          [[9, 5]])


def test_stamp():
    src = Canvas(2, 2)
    src.rect(0, 0, 2, 2, 3)
    dst = Canvas(4, 4)
    dst.stamp(src, 1, 1)
    check("stamp draws the source",
          rows(dst),
          [[0, 0, 0, 0],
           [0, 3, 3, 0],
           [0, 3, 3, 0],
           [0, 0, 0, 0]])


def test_copy():
    c = Canvas(2, 2)
    c.px(0, 0, 1)
    d = c.copy()
    d.px(0, 0, 2)
    check("copy is independent", c.get(0, 0), 1)


# --- colour expansion -----------------------------------------------------
def test_expand():
    check("expand 5/6/5 white", expand565((31, 63, 31)), (255, 255, 255))
    check("expand 5/6/5 black", expand565((0, 0, 0)), (0, 0, 0))
    check("expand transparent", expand565(None), None)


# --- the RNG --------------------------------------------------------------
def test_rng():
    a = Rng(12345)
    b = Rng(12345)
    seq = [a.next() for _ in range(4)]
    check("Rng is deterministic", [b.next() for _ in range(4)], seq)
    check("Rng values are 32-bit", all(0 <= v < 2 ** 32 for v in seq), True)
    check("Rng advances", len(set(seq)), 4)
    c = Rng(7)
    check("Rng.rand is in range", all(0 <= c.rand(6) < 6 for _ in range(50)),
          True)
    check("Rng.rand(1) is 0", Rng(7).rand(1), 0)


def main():
    for fn in (test_px, test_rect, test_lines, test_line, test_disc,
               test_ellipse, test_polygon, test_flood, test_outline,
               test_ramp, test_dither_thresholds, test_mirror, test_rotate90,
               test_recolor, test_stamp, test_copy, test_expand, test_rng):
        fn()
    if FAILURES:
        sys.stderr.write("test-art-canvas: FAILED — %d check(s)\n"
                         % len(FAILURES))
        for f in FAILURES:
            sys.stderr.write("  " + f + "\n")
        return 1
    print("test-art-canvas: ok (%d checks, card 7.2)" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
