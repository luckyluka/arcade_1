#!/usr/bin/env python3
"""scan.py FILE.ppm [y] — report what a captured ARCADE-1 frame contains.

Prints the colour runs on one scanline and the bounding box of everything
that is not the background, so a probe's drawn geometry can be read off.
"""
import sys

d = open(sys.argv[1], "rb").read()
toks, i = [], 0
while len(toks) < 4:
    while d[i:i + 1].isspace():
        i += 1
    if d[i:i + 1] == b"#":
        i = d.index(b"\n", i) + 1
        continue
    j = i
    while not d[j:j + 1].isspace():
        j += 1
    toks.append(d[i:j])
    i = j
w, h = int(toks[1]), int(toks[2])
px = d[i + 1:]


def at(x, y):
    o = (y * w + x) * 3
    return tuple(px[o:o + 3])


bg = at(0, 0)
pts = [(x, y) for y in range(h) for x in range(w) if at(x, y) != bg]
if not pts:
    print("all background", bg)
    sys.exit(0)
xs = [p[0] for p in pts]
ys = [p[1] for p in pts]
print("bg", bg, "content x %d..%d (%d px)  y %d..%d (%d px)"
      % (min(xs), max(xs), max(xs) - min(xs) + 1, min(ys), max(ys), max(ys) - min(ys) + 1))
y = int(sys.argv[2]) if len(sys.argv) > 2 else min(ys)
runs, prev = [], None
for x in range(w):
    c = at(x, y)
    if c != prev:
        runs.append([c, x, x])
        prev = c
    else:
        runs[-1][2] = x
print("scanline y=%d:" % y)
for c, a, b in runs:
    if c != bg:
        print("   x %3d..%3d  %s  (%d px)" % (a, b, c, b - a + 1))
