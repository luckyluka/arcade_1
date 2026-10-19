#!/bin/sh
# ROADMAP Wave 1 card 1.1: arc1-assetc skeleton + contract harness.
#
# Fixture images are BUILT IN-TEST by the embedded python mini-PNG-writer
# (struct + zlib, non-interlaced 8-bit RGB) — no binary fixtures in git.
# Every case pins the exit code (0 compiled / 1 validation / 2 usage-IO);
# later cards extend this same script with more cases.
#
# Usage: test_arc1assetc.sh <arcade1-cli> <rom-dir> <golden-dir>
#   The three standard test args are accepted for uniformity; arc1-assetc is
#   host tooling and uses none of them.
set -eu

CLI=${1:?usage: test_arc1assetc.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?usage: test_arc1assetc.sh <arcade1-cli> <rom-dir> <golden-dir>}
GOLD=${3:-}
# The three args are required (uniformity) but unused: arc1-assetc is host
# tooling, not a binary from this repo's build.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOL="$ROOT/tools/arc1_assetc.py"

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-arc1assetc: FAILED — $*" >&2; exit 1; }

[ -f "$TOOL" ] || fail "missing $TOOL"
CHECKS=0

# expect <want-rc> <desc> <cmd...>  — runs the tool, pins the exit code,
# echoes the combined output in $OUTV for the caller's substring asserts.
expect() {
    want=$1; desc=$2; shift 2
    rc=0; OUTV=$("$@" 2>&1) || rc=$?
    [ "$rc" -eq "$want" ] || \
        fail "$desc: exit $rc, want $want — $OUTV"
    CHECKS=$((CHECKS + 1))
    echo "  ok: $desc (exit $rc)"
}

assert_has() {
    echo "$OUTV" | grep -q "$1" || fail "$2: no '$1' in output — $OUTV"
    CHECKS=$((CHECKS + 1))
}

# --- embedded mini-PNG-writer (card 1.1 harness spec) -----------------------
write_png_rgb() {
    # write_png_rgb <path> <w> <h> <hex-pixels> — hex = "RRGGBB" per pixel,
    # row-major. Non-interlaced, 8-bit RGB, filter 0, fixed zlib level.
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import struct, sys, zlib

path, w, h, hexs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]

def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

raw = b""
for y in range(h):
    row = bytes.fromhex(hexs[y * w * 6:(y + 1) * w * 6])
    assert len(row) == w * 3
    raw += b"\x00" + row                      # filter type 0 per scanline

png = (b"\x89PNG\r\n\x1a\n" +
       chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(raw, 9)) +
       chunk(b"IEND", b""))
open(path, "wb").write(png)
PY
}

# --- 1) happy path: empty manifest, with and without --report ---------------
cat > "$OUT/empty.json" <<'EOF'
{ "format": 1, "palettes": {}, "sprites": [] }
EOF
expect 0 "empty assets.json compiles" \
    python3 "$TOOL" --assets "$OUT/empty.json" --out-dir "$OUT/gen"
expect 0 "--report accepted on the happy path" \
    python3 "$TOOL" --assets "$OUT/empty.json" --out-dir "$OUT/gen" --report

# --- 2) usage / IO errors -> exit 2 -----------------------------------------
expect 2 "no arguments is a usage error" python3 "$TOOL"
expect 2 "missing --assets file is an IO error" \
    python3 "$TOOL" --assets "$OUT/nope.json" --out-dir "$OUT/gen"
echo '{ not json' > "$OUT/broken.json"
expect 2 "malformed JSON is an IO error" \
    python3 "$TOOL" --assets "$OUT/broken.json" --out-dir "$OUT/gen"
assert_has "not valid JSON" "malformed JSON names the problem"

# --- 3) document validation -> exit 1 ---------------------------------------
echo '{ "format": 2, "palettes": {}, "sprites": [] }' > "$OUT/fmt.json"
expect 1 "wrong format field is a validation error" \
    python3 "$TOOL" --assets "$OUT/fmt.json" --out-dir "$OUT/gen"
assert_has 'format 1 expected (got 2)' "format error teaches the fix"

echo '{ "format": 1, "palettes": {}, "sprites": {} }' > "$OUT/spr.json"
expect 1 "sprites must be an array" \
    python3 "$TOOL" --assets "$OUT/spr.json" --out-dir "$OUT/gen"
assert_has '"sprites" must be an array' "sprites type error names the field"

# --- harness self-check: the mini-PNG-writer really writes a PNG ------------
write_png_rgb "$OUT/fixture.png" 4 2 \
    "FF000000FF000000FF80402040406080C0D0E0F00102030405"
[ "$(od -An -tx1 -N8 "$OUT/fixture.png" | tr -d ' \n')" = \
    "89504e470d0a1a0a" ] || fail "mini-PNG-writer output lacks the PNG signature"
CHECKS=$((CHECKS + 1))
echo "  ok: mini-PNG-writer emits a real PNG (signature check)"

echo "test-arc1assetc: ok ($CHECKS checks, card 1.1 skeleton)"
