#!/bin/sh
# ROADMAP_V3 card A3: PNG captures and --scale.
#
# 1. --capture picks the format by extension; the decoded PNG carries
#    exactly the same pixels as the PPM of the same frame (scale 1)
# 2. --scale 4 gives exactly 1920x1080 with nearest-neighbour replication
# 3. two runs produce byte-identical PNGs
# 4. --capture-dir: --format png writes frame_NNNN.png, the default stays
#    .ppm, and --frames N captures the framebuffer after frame N-1
# 5. --scale/--format validation is a teaching exit 2
#
# The PNG decoder is python3 zlib+struct and validates every chunk CRC
# against zlib.crc32 — a wrong hand-rolled CRC-32 fails here.
#
# Usage: test_capture_png.sh <arcade1-cli> <rom-dir> <golden-dir>
set -eu

CLI=${1:?usage: test_capture_png.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?usage: test_capture_png.sh <arcade1-cli> <rom-dir> <golden-dir>}
GOLD=${3:-}

# Absolute paths: this test cds into its own directory (Makefile args are
# repo-relative, e.g. build/arcade1 and tests/roms).
case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac
case "$ROM" in /*) ;; *) ROM="$(pwd)/$ROM" ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cd "$OUT"

fail() { echo "test-capture-png: FAILED — $*" >&2; exit 1; }

BREAK="$ROM/breakout.arc1"
[ -f "$BREAK" ] || fail "missing $BREAK"

# Decode a PNG to a PPM, checking signature, IHDR, every chunk CRC and
# the filter bytes along the way. Usage: png2ppm.py in.png out.ppm
cat > png2ppm.py <<'PY'
import sys, zlib, struct

data = open(sys.argv[1], "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
pos, idat, w, h = 8, b"", None, None
while pos < len(data):
    ln = struct.unpack(">I", data[pos:pos + 4])[0]
    typ = data[pos + 4:pos + 8]
    body = data[pos + 8:pos + 8 + ln]
    crc = struct.unpack(">I", data[pos + 8 + ln:pos + 12 + ln])[0]
    assert crc == zlib.crc32(typ + body) & 0xFFFFFFFF, f"bad {typ} crc"
    if typ == b"IHDR":
        w, h, depth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", body)
        assert (depth, ctype, comp, filt, inter) == (8, 2, 0, 0, 0), "IHDR"
    elif typ == b"IDAT":
        idat += body
    elif typ == b"IEND":
        break
    pos += 12 + ln
assert w and h, "no IHDR"
raw = zlib.decompress(idat)
stride = w * 3 + 1
assert len(raw) == h * stride, (len(raw), h * stride)
pix = bytearray()
for y in range(h):
    row = raw[y * stride:(y + 1) * stride]
    assert row[0] == 0, f"filter {row[0]} at row {y}"
    pix += row[1:]
open(sys.argv[2], "wb").write(b"P6\n%d %d\n255\n" % (w, h) + pix)
PY

# --- 1) same frame, both formats, pixel-identical at scale 1 ----------------
"$CLI" run "$BREAK" --frames 30 --capture f.ppm --quiet > /dev/null ||
    fail "ppm capture run"
"$CLI" run "$BREAK" --frames 30 --capture f.png --quiet > /dev/null ||
    fail "png capture run"
[ -s f.png ] || fail "no f.png produced"
python3 png2ppm.py f.png f_dec.ppm || fail "png decode/validate failed"
cmp -s f.ppm f_dec.ppm || fail "png pixels differ from ppm pixels at scale 1"

# --- 2) scale 4: exactly 1920x1080, nearest-neighbour replication -----------
"$CLI" run "$BREAK" --frames 30 --capture f4.ppm --scale 4 --quiet > /dev/null ||
    fail "scale-4 ppm run"
"$CLI" run "$BREAK" --frames 30 --capture f4.png --scale 4 --quiet > /dev/null ||
    fail "scale-4 png run"
python3 png2ppm.py f4.png f4_dec.ppm || fail "scale-4 decode failed"
cmp -s f4.ppm f4_dec.ppm || fail "scale-4 png differs from scale-4 ppm"
cat > scalecheck.py <<'PY'
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

w1, h1, base = read_ppm(sys.argv[1])
w4, h4, big = read_ppm(sys.argv[2])
assert (w4, h4) == (1920, 1080), (w4, h4)
assert (w4, h4) == (w1 * 4, h1 * 4), ((w4, h4), (w1, h1))
for y in range(h4):
    y1 = y // 4
    r1 = base[y1 * w1 *3:(y1 + 1) * w1 * 3]
    exp = b"".join(r1[i:i + 3] * 4 for i in range(0, len(r1), 3))
    if big[y * w4 * 3:(y + 1) * w4 * 3] != exp:
        raise SystemExit(f"row {y} is not a 4x replication")
print("scale 4 ok: 1920x1080, every pixel replicated")
PY
python3 scalecheck.py f.ppm f4.ppm || fail "replication check failed"

# --- 3) two runs, byte-identical --------------------------------------------
"$CLI" run "$BREAK" --frames 30 --capture f2.png --quiet > /dev/null ||
    fail "second png run"
cmp -s f.png f2.png || fail "two runs produced different PNG bytes"

# --- 4) --capture-dir: --format, default, frame N-1 semantics ---------------
mkdir d1 d2
"$CLI" run "$BREAK" --frames 5 --capture-every 4 --capture-dir d1 \
    --format png --quiet > /dev/null || fail "--format png dir run"
"$CLI" run "$BREAK" --frames 5 --capture-every 4 --capture-dir d2 \
    --quiet > /dev/null || fail "default dir run"
[ -f d1/frame_0004.png ] || fail "no d1/frame_0004.png"
[ -f d2/frame_0004.ppm ] || fail "default dir extension should stay .ppm"
# --frames 5 leaves the framebuffer after frame 4: the final --capture must
# be that same screen, in both formats.
"$CLI" run "$BREAK" --frames 5 --capture g.ppm --quiet > /dev/null ||
    fail "final capture run"
cmp -s g.ppm d2/frame_0004.ppm || fail "--capture after --frames 5 != frame 4"
python3 png2ppm.py d1/frame_0004.png d1_dec.ppm || fail "dir png decode"
cmp -s g.ppm d1_dec.ppm || fail "dir png != same frame as ppm"

# --- 5) validation ----------------------------------------------------------
rc=0; "$CLI" run "$BREAK" --frames 2 --capture x.png --scale 9 \
      --quiet 2> e1.txt || rc=$?
[ "$rc" -eq 2 ] || fail "--scale 9 should exit 2, got $rc"
grep -q -- "--scale must be 1..8" e1.txt || fail "missing scale message"
rc=0; "$CLI" run "$BREAK" --frames 2 --capture-every 2 --capture-dir d2 \
      --format gif --quiet 2> e2.txt || rc=$?
[ "$rc" -eq 2 ] || fail "--format gif should exit 2, got $rc"
grep -q -- "--format must be png or ppm" e2.txt || fail "missing format message"

echo "test-capture-png: ok — png == ppm pixels, 1920x1080 replication, repeat identical"
