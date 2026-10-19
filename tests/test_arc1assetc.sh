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

# --- 4) palette module (card 1.2) -------------------------------------------
pal17() {  # pal17 <path> — a palette with 17 distinct colors
    python3 - "$1" <<'PY'
import json, sys
json.dump({"format": 1,
           "palettes": {"world": {"bank": 3,
                                  "colors": ["#%06X" % i for i in range(17)]}},
           "sprites": []}, open(sys.argv[1], "w"))
PY
}
pal17 "$OUT/pal17.json"
expect 1 "17-color palette is a validation error" \
    python3 "$TOOL" --assets "$OUT/pal17.json" --out-dir "$OUT/gen"
assert_has 'palette world: 17 colors (max 16) — merge colors or add a second bank' \
    "17-color error teaches the fix"

echo '{"format":1,"palettes":{"world":{"bank":16,"colors":["#000000"]}},"sprites":[]}' \
    > "$OUT/bank16.json"
expect 1 "bank 16 is out of range" \
    python3 "$TOOL" --assets "$OUT/bank16.json" --out-dir "$OUT/gen"
assert_has 'palette world: bank 16 out of range 0..15' "bank error text"

echo '{"format":1,"palettes":{"world":{"bank":0,"colors":["#000000","#111111","#222222","#000000"]}},"sprites":[]}' \
    > "$OUT/dup.json"
expect 1 "duplicate palette color is a validation error" \
    python3 "$TOOL" --assets "$OUT/dup.json" --out-dir "$OUT/gen"
assert_has 'duplicate color #000000 at indices 0 and 3' "duplicate names both indices"

echo '{"format":1,"palettes":{"world":{"bank":0,"colors":["#FFF"]}},"sprites":[]}' \
    > "$OUT/badhex.json"
expect 1 "malformed hex color is a validation error" \
    python3 "$TOOL" --assets "$OUT/badhex.json" --out-dir "$OUT/gen"
assert_has 'bad color "#FFF" — use #RRGGBB' "hex error teaches the format"

# --- 5) sprite palette side: auto/bank pairing + references (V3 4.2) --------
echo '{"format":1,"palettes":{},"sprites":[{"name":"player","file":"x.png","palette":"auto"}]}' \
    > "$OUT/autonobank.json"
expect 1 '"palette": "auto" without "bank" is a validation error' \
    python3 "$TOOL" --assets "$OUT/autonobank.json" --out-dir "$OUT/gen"
assert_has '"palette": "auto" requires "bank"' "auto requires bank"

echo '{"format":1,"palettes":{"world":{"bank":0,"colors":["#000000"]}},"sprites":[{"name":"player","file":"x.png","palette":"world","frames":1,"bank":2}]}' \
    > "$OUT/bankonnamed.json"
expect 1 '"bank" on a named palette is a validation error' \
    python3 "$TOOL" --assets "$OUT/bankonnamed.json" --out-dir "$OUT/gen"
assert_has '"bank" is only valid with "palette": "auto"' "bank-on-named error"

echo '{"format":1,"palettes":{},"sprites":[{"name":"player","file":"x.png","palette":"world","frames":1}]}' \
    > "$OUT/noref.json"
expect 1 "undefined palette reference is a validation error" \
    python3 "$TOOL" --assets "$OUT/noref.json" --out-dir "$OUT/gen"
assert_has 'palette "world" not defined' "undefined reference teaches the fix"

echo '{"format":1,"palettes":{"world":{"bank":3,"colors":["#000000","#F8D8A8"]}},"sprites":[]}' \
    > "$OUT/palok.json"
expect 0 "a valid named palette compiles" \
    python3 "$TOOL" --assets "$OUT/palok.json" --out-dir "$OUT/gen"

# --- 6) module units: rgb565 + auto palette (ROADMAP_V3 4.2) ----------------
python3 - "$ROOT" <<'PY' || fail "module units"
import sys
sys.path.insert(1, sys.argv[1] + "/tools")
import arc1_assetc as m

# Device upload semantics: #F8D8A8 -> 0xFED5.
assert m.rgb565(0xF8, 0xD8, 0xA8) == 0xFED5, hex(m.rgb565(0xF8, 0xD8, 0xA8))
assert m.parse_hex_color("#fed5ab") == (0xFE, 0xD5, 0xAB)
assert m.parse_hex_color("#FED5") is None and m.parse_hex_color("FED5AB") is None

# Auto palette: <=15 distinct -> 16 slots, key at 0, first appearance order.
pal = m.collect_auto("player", [0x1234, None, 0x5678, 0x1234, 0x9ABC])
assert pal["entries"] == [0, 0x1234, 0x5678, 0x9ABC] + [0] * 12, pal

# >15 distinct -> ROADMAP_V3's exact teaching error (19 colours counted).
try:
    m.collect_auto("player", [i + 1 for i in range(19)])
except m.AutoPaletteError as e:
    assert str(e) == ('sprite player: 19 colours (max 15 + transparent) — '
                      'reduce colours, use "quantize": "nearest" with an '
                      'explicit palette, or split the sheet'), str(e)
else:
    raise SystemExit("19 distinct colours did not raise AutoPaletteError")
PY
CHECKS=$((CHECKS + 4))
echo "  ok: rgb565 + auto-palette module units (4 asserts)"

# --- harness self-check: the mini-PNG-writer really writes a PNG ------------
write_png_rgb "$OUT/fixture.png" 4 2 \
    "FF000000FF000000FF80402040406080C0D0E0F00102030405"
[ "$(od -An -tx1 -N8 "$OUT/fixture.png" | tr -d ' \n')" = \
    "89504e470d0a1a0a" ] || fail "mini-PNG-writer output lacks the PNG signature"
CHECKS=$((CHECKS + 1))
echo "  ok: mini-PNG-writer emits a real PNG (signature check)"

echo "test-arc1assetc: ok ($CHECKS checks, cards 1.1-1.2)"
