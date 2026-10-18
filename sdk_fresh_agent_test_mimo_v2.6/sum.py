#!/usr/bin/env python3
"""sum.py FILE.ppm — compact summary: bbox + colour-run pattern of one scanline."""
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

NAMES = {(255, 0, 0): "R", (0, 255, 0): "G", (0, 0, 255): "B",
         (255, 255, 0): "Y", (255, 0, 255): "M"}


def at(x, y):
    o = (y * w + x) * 3
    return tuple(px[o:o + 3])


bg = at(0, 0)
pts = [(x, y) for y in range(h) for x in range(w) if at(x, y) != bg]
if not pts:
    print("EMPTY")
    sys.exit(0)
xs = [p[0] for p in pts]
ys = [p[1] for p in pts]
y0 = min(ys)
runs, prev = [], None
for x in range(w):
    c = at(x, y0)
    if c != prev:
        runs.append([c, x, x])
        prev = c
    else:
        runs[-1][2] = x
pat = " ".join("%s%d-%d" % (NAMES.get(c, "?%s" % (c,)), a, b) for c, a, b in runs if c != bg)
print("x %d..%d  y %d..%d  | %s" % (min(xs), max(xs), min(ys), max(ys), pat))
