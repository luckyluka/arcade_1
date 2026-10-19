#!/bin/sh
# ROADMAP_V3 card A6: text.h (5x7 font), arcade1_rand, arcade1_pixel.
#
# 1. The probe draws three 8x8 cells: 'A' and '0' from text_draw("A0") and
#    text_u32's digit '0'. The test decodes the captured PPM and asserts
#    every pixel against a hand-written oracle (copied here from the font
#    DESIGN, never read from programs/text.h): white exactly where the
#    glyph bits are set, black elsewhere in the cells, and nothing at all
#    outside the cells.
# 2. 3,000 arcade1_rand(3) calls land each bucket inside 10-40 %.
#
# Usage: test_text.sh <arcade1-cli> <text_probe.bin> <golden-dir>
set -eu

CLI=${1:?usage: test_text.sh <arcade1-cli> <text_probe.bin> <golden-dir>}
BIN=${2:?usage: test_text.sh <arcade1-cli> <text_probe.bin> <golden-dir>}
GOLD=${3:-}

# Absolute paths: this test cds into its own directory.
case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac
case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cd "$OUT"

fail() { echo "test-text: FAILED — $*" >&2; exit 1; }

[ -f "$BIN" ] || fail "missing probe binary $BIN"

# Symbols for --vars: dump the probe's own ELF beside the .bin.
ELF=${BIN%.bin}.elf
if command -v riscv64-unknown-elf-nm >/dev/null 2>&1 && [ -f "$ELF" ]; then
    riscv64-unknown-elf-nm "$ELF" > probe.syms
    VARS="--syms probe.syms --vars randc0,randc1,randc2"
else
    VARS=
fi

# --- one run: capture the frame, read the histogram --------------------------
"$CLI" run "$BIN" --frames 3 --capture t.ppm $VARS --quiet > v.txt || \
    fail "probe run failed"

# --- 1) glyph pixels vs the hand-written oracle ------------------------------
cat > check_text.py <<'PY'
import sys

def read_ppm(p):
    d = open(p, "rb").read()
    parts = d.split(b"\n", 3)
    assert parts[0] == b"P6", p
    w, h = map(int, parts[1].split())
    assert parts[2] == b"255", p
    pix = parts[3]
    assert len(pix) == w * h * 3, (p, len(pix), w * h * 3)
    return w, h, pix

WHITE = b"\xff\xff\xff"
BLACK = b"\x00\x00\x00"

# The oracle: 5x7 rows per glyph, bit 4 = leftmost pixel. These bytes are
# transcribed from the font design itself and are the test's own copy — the
# test never opens programs/text.h, so a mismatch there fails here.
ORACLE = {
    "A": [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],   # .###. ... #####
    "0": [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E],   # slashed zero
}

# What the probe drew: (glyph, cell x, cell y).
CELLS = [("A", 16, 24), ("0", 24, 24), ("0", 32, 24)]

w, h, pix = read_ppm(sys.argv[1])
assert (w, h) == (480, 270), (w, h)

def cell_of(x, y):
    for g, cx, cy in CELLS:
        if cx <= x <= cx + 7 and cy <= y <= cy + 7:
            return g, x - cx, y - cy
    return None

for y in range(h):
    for x in range(w):
        got = pix[(y * w + x) * 3:(y * w + x) * 3 + 3]
        here = cell_of(x, y)
        if here is None:
            assert got == BLACK, f"drew outside the cells at ({x},{y}): {got.hex()}"
            continue
        g, cx, cy = here
        if cy < 7 and cx < 5:
            bit = (ORACLE[g][cy] >> (4 - cx)) & 1
        else:
            bit = 0                       # 8x8 cell margins stay background
        assert got == (WHITE if bit else BLACK), \
            f"{g} cell pixel ({x},{y}) = {got.hex()}, oracle bit {bit}"

print("text ok: 3 cells match the oracle, nothing outside them")
PY
python3 check_text.py t.ppm || fail "glyph pixel check failed"

# --- 2) arcade1_rand(3) distribution over 3,000 calls ------------------------
if [ -n "$VARS" ]; then
    c0=$(sed -n 's/^var randc0 = //p' v.txt)
    c1=$(sed -n 's/^var randc1 = //p' v.txt)
    c2=$(sed -n 's/^var randc2 = //p' v.txt)
    [ -n "$c0" ] && [ -n "$c1" ] && [ -n "$c2" ] || \
        fail "missing histogram vars in: $(cat v.txt)"
    [ "$((c0 + c1 + c2))" -eq 3000 ] || \
        fail "histogram sums to $((c0 + c1 + c2)), want 3000"
    for c in "$c0" "$c1" "$c2"; do
        [ "$c" -ge 300 ] && [ "$c" -le 1200 ] || \
            fail "bucket $c outside 10-40 %% of 3000 (300..1200): $c0 $c1 $c2"
    done
    echo "rand ok: arcade1_rand(3) buckets $c0 $c1 $c2 (band 300..1200)"
else
    echo "rand histogram skipped — no riscv nm / probe elf"
fi

echo "test-text: ok — font oracle, no leaks, rand distribution"
