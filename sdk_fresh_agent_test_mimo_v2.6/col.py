#!/usr/bin/env python3
"""col.py FILE.ppm [y] [n] [w] — colour at the centre of each w-px column."""
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
y = int(sys.argv[2]) if len(sys.argv) > 2 else 26
n = int(sys.argv[3]) if len(sys.argv) > 3 else 10
cw = int(sys.argv[4]) if len(sys.argv) > 4 else 48
out = []
for c in range(n):
    x = c * cw + cw // 2
    if x >= w:
        break
    o = (y * w + x) * 3
    out.append("%d:%s" % (c, tuple(px[o:o + 3])))
print(" ".join(out))
