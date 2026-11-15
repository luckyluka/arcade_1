#!/usr/bin/env python3
"""HD_EXPERIMENT.md card H1 — byte-for-byte nearest-neighbour comparison.

usage: nn_compare.py BASE.ppm SCALED.ppm K

BASE.ppm is a capture from the k = 1 machine, SCALED.ppm the same frame from
the k = K machine. The scaled capture must be exactly the nearest-neighbour
upscale of the base: output pixel (x, y) == base pixel (x // K, y // K), with
no filtering, no interpolation and no off-by-one at any edge.

Exit 0 when they are identical, 1 with a reason on stderr otherwise.
Deterministic: no clock, no randomness, stdlib only.
"""

import sys


def fail(msg):
    sys.stderr.write("nn_compare: %s\n" % msg)
    return 1


def read_ppm(path):
    """Parse a binary P6 PPM. Returns (width, height, raster_bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError("%s: not a P6 PPM (magic %r)" % (path, data[:2]))

    i = 2
    fields = []
    while len(fields) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if i < len(data) and data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        if j == i:
            raise ValueError("%s: truncated PPM header" % path)
        fields.append(int(data[i:j]))
        i = j
    i += 1  # exactly one whitespace byte separates maxval from the raster

    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("%s: maxval %d, expected 255" % (path, maxval))
    raster = data[i:]
    if len(raster) != w * h * 3:
        raise ValueError("%s: raster is %d bytes, expected %d"
                         % (path, len(raster), w * h * 3))
    return w, h, raster


def nn_upscale(w, h, raster, k):
    """Nearest-neighbour upscale by integer factor k. Byte-identical output
    to what arcade1's --scale and the k x k plot block both produce."""
    out = bytearray()
    span = 3 * k
    for y in range(h):
        src = raster[y * w * 3:(y + 1) * w * 3]
        row = bytearray(w * span)
        for x in range(w):
            row[x * span:(x + 1) * span] = src[x * 3:x * 3 + 3] * k
        out += row * k
    return bytes(out)


def first_diff(expected, actual):
    """Index of the first differing byte, or -1."""
    n = min(len(expected), len(actual))
    for i in range(n):
        if expected[i] != actual[i]:
            return i
    return -1 if len(expected) == len(actual) else n


def main(argv):
    if len(argv) != 4:
        return fail("usage: nn_compare.py BASE.ppm SCALED.ppm K")
    base_path, scaled_path, k = argv[1], argv[2], int(argv[3])
    if k < 1:
        return fail("K must be >= 1")

    try:
        bw, bh, braster = read_ppm(base_path)
        sw, sh, sraster = read_ppm(scaled_path)
    except (OSError, ValueError) as e:
        return fail(str(e))

    if sw != bw * k or sh != bh * k:
        return fail("%s is %dx%d, expected the k=%d upscale of the %dx%d "
                    "base, i.e. %dx%d"
                    % (scaled_path, sw, sh, k, bw, bh, bw * k, bh * k))

    expected = nn_upscale(bw, bh, braster, k) if k > 1 else braster
    if sraster == expected:
        return 0

    at = first_diff(expected, sraster)
    return fail("%s differs from the nearest-neighbour upscale of %s "
                "at byte %d of %d"
                % (scaled_path, base_path, at, len(expected)))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
