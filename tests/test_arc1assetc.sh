#!/bin/sh
# ROADMAP Wave 1 cards 1.1-1.5 + Wave 2 refs/metadata + Wave 3 animations
# + Wave 4 maps (incl. the minsize-retraction fixtures):
# arc1-assetc harness.
#
# Fixture images are BUILT IN-TEST by the embedded python mini-PNG-writer
# (struct + zlib, non-interlaced 8-bit RGB) — no binary fixtures in git.
# Every case pins the exit code (0 compiled / 1 validation / 2 usage-IO);
# later cards extend this same script with more cases. (The Wave-2 DEMO
# guest keeps its art committed beside it: programs/experimental/.)
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
    echo "$OUTV" | grep -Fq "$1" || fail "$2: no '$1' in output — $OUTV"
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
python3 -B - "$ROOT" <<'PY' || fail "module units"
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

# --- 7) image loader + quantization (card 1.3) ------------------------------
write_png_rgba() {
    # write_png_rgba <path> <w> <h> <hex> — "RRGGBBAA" per pixel, row-major.
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import struct, sys, zlib
path, w, h, hexs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
raw = b""
for y in range(h):
    row = bytes.fromhex(hexs[y * w * 8:(y + 1) * w * 8])
    assert len(row) == w * 4
    raw += b"\x00" + row
open(path, "wb").write(b"\x89PNG\r\n\x1a\n" +
    chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
    chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
PY
}

write_ppm_p6() {
    # write_ppm_p6 <path> <w> <h> <hex> — "RRGGBB" per pixel.
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import sys
path, w, h, hexs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
body = bytes.fromhex(hexs)
assert len(body) == w * h * 3
open(path, "wb").write(b"P6\n%d %d\n255\n" % (w, h) + body)
PY
}

manifest() {  # manifest <path> <sprite-json> [palette-json]
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
path, sprite, pal = sys.argv[1], json.loads(sys.argv[2]), json.loads(sys.argv[3] or "{}")
json.dump({"format": 1, "palettes": pal, "sprites": [sprite]},
          open(path, "w"))
PY
}

# acceptance: a 3-px-wide strip -> exit 1, message says "even"
write_png_rgb "$OUT/odd3.png" 3 1 "FF0000FF0000FF0000"
manifest "$OUT/odd3.json" \
    '{"name":"player","file":"odd3.png","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FF0000"]}}'
expect 1 "3-px-wide strip is a validation error" \
    python3 "$TOOL" --assets "$OUT/odd3.json" --out-dir "$OUT/gen"
assert_has "even" "odd-width error names the fix"

# even IMAGE but odd FRAME (6 / 2 frames = 3)
hexrep() { python3 -c "print('$1' * $2, end='')"; }
write_png_rgb "$OUT/oddframe.png" 6 1 "$(hexrep FF0000 6)"
manifest "$OUT/oddframe.json" \
    '{"name":"player","file":"oddframe.png","palette":"world","frames":2}' \
    '{"world":{"bank":0,"colors":["#000000","#FF0000"]}}'
expect 1 "odd FRAME width is a validation error" \
    python3 "$TOOL" --assets "$OUT/oddframe.json" --out-dir "$OUT/gen"
assert_has "frame width 3 is odd" "frame-parity error names the frame width"

# width not divisible by frames
write_png_rgb "$OUT/div.png" 7 1 "$(hexrep 00FF00 7)"
manifest "$OUT/div.json" \
    '{"name":"player","file":"div.png","palette":"world","frames":2}' \
    '{"world":{"bank":0,"colors":["#000000","#FF0000"]}}'
expect 1 "non-divisible width is a validation error" \
    python3 "$TOOL" --assets "$OUT/div.json" --out-dir "$OUT/gen"
assert_has "not divisible by frames 2" "divisibility error names both numbers"

# acceptance: unlisted color -> pinned exact teaching error
write_png_rgb "$OUT/notin.png" 2 1 "000000010203"
manifest "$OUT/notin.json" \
    '{"name":"player","file":"notin.png","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "color outside the palette is a validation error" \
    python3 "$TOOL" --assets "$OUT/notin.json" --out-dir "$OUT/gen"
assert_has "pixel (1,0) #010203 not in palette 'world' (nearest #FFFFFF, d=192029) — fix the art or extend the palette" \
    "exact-match error is the card's exact text (independent d)"

# acceptance: happy path -> header emitted; size computed independently
python3 -c "print('FFFFFF' * 32, end='')" > "$OUT/allwhite.hex"
write_png_rgb "$OUT/ok.png" 8 4 "$(cat "$OUT/allwhite.hex")"
manifest "$OUT/game.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 0 "happy path compiles to a header" \
    python3 "$TOOL" --assets "$OUT/game.json" --out-dir "$OUT/gen"
want=$(((8 / 2) * 4))                 # (image_w/2)*h = stride*frames = 16
grep -Fq "player_sheet[$want]" "$OUT/gen/game.h" || \
    fail "happy header lacks player_sheet[$want] — got: $(cat "$OUT/gen/game.h")"
CHECKS=$((CHECKS + 1))
echo "  ok: happy path header has player_sheet[$want] (computed independently)"

# alpha 0 -> nibble 0 (transparent); RGBA PNG
write_png_rgba "$OUT/alpha.png" 2 1 "FFFFFF00FFFFFFFF"
manifest "$OUT/alpha.json" \
    '{"name":"player","file":"alpha.png","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 0 "RGBA with alpha 0 compiles" \
    python3 "$TOOL" --assets "$OUT/alpha.json" --out-dir "$OUT/gen"
grep -Fq "player_sheet[1]" "$OUT/gen/alpha.h" || fail "alpha header size"
grep -Fq "0x01," "$OUT/gen/alpha.h" || \
    fail "alpha-0 pixel must pack as nibble 0 — $(cat "$OUT/gen/alpha.h")"
CHECKS=$((CHECKS + 1))
echo "  ok: alpha 0 packs as nibble 0 (0x01 = transparent + index 1)"

# "palette": "auto" end to end: first-appearance indices 1,2,3
write_png_rgb "$OUT/auto.png" 4 1 "112233112233445566778899"
manifest "$OUT/auto.json" \
    '{"name":"player","file":"auto.png","palette":"auto","bank":5,"frames":1}' '{}'
expect 0 '"palette": "auto" compiles from the pixels' \
    python3 "$TOOL" --assets "$OUT/auto.json" --out-dir "$OUT/gen"
grep -Fq "0x11, 0x23," "$OUT/gen/auto.h" || \
    fail "auto palette must assign A=1,B=2,C=3 in first-appearance order — $(cat "$OUT/gen/auto.h")"
CHECKS=$((CHECKS + 1))
echo "  ok: auto palette assigns indices in first-appearance order"

# quantize "nearest": off-palette pixel maps to the closest colour, no error
write_png_rgb "$OUT/near.png" 2 1 "0A0A0AFFFFFF"
manifest "$OUT/near.json" \
    '{"name":"player","file":"near.png","palette":"world","frames":1,"quantize":"nearest"}' \
    '{"world":{"bank":0,"colors":["#FF0000","#000000","#FFFFFF"]}}'
expect 0 'quantize "nearest" maps off-palette pixels silently' \
    python3 "$TOOL" --assets "$OUT/near.json" --out-dir "$OUT/gen"
grep -Fq "0x12," "$OUT/gen/near.h" || \
    fail "nearest must map #0A0A0A to index 1 and #FFFFFF to 2 — $(cat "$OUT/gen/near.h")"
CHECKS=$((CHECKS + 1))
echo "  ok: quantize nearest maps to closest colours (0x12)"

# quantize rejects unknown modes (schema)
manifest "$OUT/badq.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"quantize":"bicubic"}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 'quantize "bicubic" is a validation error' \
    python3 "$TOOL" --assets "$OUT/badq.json" --out-dir "$OUT/gen"
assert_has '"quantize" must be "exact" or "nearest"' "quantize mode error"

# auto with 16 distinct colours -> V3's teaching error (CLI, end to end)
python3 -c "print(''.join('%02X0000' % (i * 8) for i in range(16)), end='')" \
    > "$OUT/sixteen.hex"
write_png_rgb "$OUT/sixteen.png" 16 1 "$(cat "$OUT/sixteen.hex")"
manifest "$OUT/sixteen.json" \
    '{"name":"player","file":"sixteen.png","palette":"auto","bank":1,"frames":1}' '{}'
expect 1 "16 distinct colours exceed an auto palette" \
    python3 "$TOOL" --assets "$OUT/sixteen.json" --out-dir "$OUT/gen"
assert_has "sprite player: 16 colours (max 15 + transparent) — reduce colours" \
    "auto overflow reports the true colour count"

# unsupported PNG capability -> validation error naming the capability
python3 - "$OUT/depth16.png" <<'PY'
import struct, sys, zlib
def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" +
    chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 16, 2, 0, 0, 0)) +
    chunk(b"IEND", b""))
PY
manifest "$OUT/depth16.json" \
    '{"name":"player","file":"depth16.png","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "16-bit PNG is an unsupported capability (validation)" \
    python3 "$TOOL" --assets "$OUT/depth16.json" --out-dir "$OUT/gen"
assert_has "need non-interlaced 8-bit" "capability error teaches the export"

# missing sprite image -> IO (exit 2)
manifest "$OUT/missing.json" \
    '{"name":"player","file":"nope.png","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 2 "missing sprite image is an IO error" \
    python3 "$TOOL" --assets "$OUT/missing.json" --out-dir "$OUT/gen"
assert_has "cannot read" "missing image names the file"

# P6 PPM input
write_ppm_p6 "$OUT/ppm.ppm" 2 1 "000000FFFFFF"
manifest "$OUT/ppm.json" \
    '{"name":"player","file":"ppm.ppm","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 0 "P6 PPM input compiles" \
    python3 "$TOOL" --assets "$OUT/ppm.json" --out-dir "$OUT/gen"
grep -Fq "player_sheet[1]" "$OUT/gen/ppm.h" || fail "ppm header size"
grep -Fq "0x01," "$OUT/gen/ppm.h" || fail "ppm nibble mapping"
CHECKS=$((CHECKS + 2))
echo "  ok: P6 PPM loads (size + nibbles)"

# bank collisions are loud (a silent overwrite at upload time otherwise)
echo '{"format":1,"palettes":{"a":{"bank":3,"colors":["#000000"]},"b":{"bank":3,"colors":["#000000"]}},"sprites":[]}' \
    > "$OUT/clash.json"
expect 1 "two palettes on one bank is a validation error" \
    python3 "$TOOL" --assets "$OUT/clash.json" --out-dir "$OUT/gen"
assert_has 'bank 3 is already used by "a"' "bank clash names both palettes"

# --- 8) emitter contract + report (card 1.4) --------------------------------
expect 0 "--report prints greppable KEY lines" \
    python3 "$TOOL" --assets "$OUT/game.json" --out-dir "$OUT/gen" --report
assert_has "SPRITE player 4x4 x2 = 16 B" \
    "SPRITE line matches the independent (w/2)*h*frames count"
assert_has "PALETTE world bank 0 = 32 B" "PALETTE line names the bank"
assert_has "ROM assets 48 / 262144 (0.0 %)" "ROM line totals sheets + palettes"

H="$OUT/gen/game.h"
for wantdef in "#define PLAYER_W 4" \
               "#define PLAYER_H 4" \
               "#define PLAYER_FRAMES 2" \
               "#define PLAYER_STRIDE 8        /* (w/2)*h bytes per frame */" \
               "world_palette[16]" \
               "; palette world (bank 0)"; do
    grep -Fq "$wantdef" "$H" || \
        fail "header contract missing: $wantdef — $(cat "$H")"
    CHECKS=$((CHECKS + 1))
done
echo "  ok: header contract (4 defines, palette array, banner palette clause)"

# nearest sprites report QUANT with independent maxerr (10-0)^2*3 = 300
expect 0 "nearest sprites report QUANT" \
    python3 "$TOOL" --assets "$OUT/near.json" --out-dir "$OUT/gen" --report
assert_has "QUANT player maxerr=300 pixels=2" "QUANT line (independent d, all opaque pixels)"

# budget: 4096x130 solid strip = 266240 B + 32 B palette > 262144
python3 - "$OUT/big.png" <<'PY'
import struct, sys, zlib
def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
w, h = 4096, 130
raw = b"".join([b"\x00" + b"\xff\xff\xff" * w] * h)
open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" +
    chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
    chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
PY
manifest "$OUT/big.json" \
    '{"name":"huge","file":"big.png","palette":"world","frames":1}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "over-budget assets fail before writing" \
    python3 "$TOOL" --assets "$OUT/big.json" --out-dir "$OUT/gen"
assert_has "over budget" "budget error teaches the fix"

# emission order is sorted by name, not manifest order
cat > "$OUT/two.json" <<EOF
{ "format": 1,
  "palettes": {"world": {"bank": 0, "colors": ["#000000", "#FFFFFF"]}},
  "sprites": [
    {"name": "zeta", "file": "ok.png", "palette": "world", "frames": 2},
    {"name": "alpha", "file": "ok.png", "palette": "world", "frames": 2} ] }
EOF
expect 0 "two sprites compile" \
    python3 "$TOOL" --assets "$OUT/two.json" --out-dir "$OUT/gen"
line2=$(sed -n '2p' "$OUT/gen/two.h")
case "$line2" in
    *"alpha:"*) CHECKS=$((CHECKS + 1))
        echo "  ok: banner lists sprites sorted (alpha before zeta)" ;;
    *) fail "banner not sorted: $line2" ;;
esac

# --- 9) determinism proof (card 1.5) ----------------------------------------
# Two independent processes over one fixture -> byte-identical header AND
# report; then delete + regenerate -> identical again (the committed
# output is the build truth and the tool is the master).
python3 "$TOOL" --assets "$OUT/two.json" --out-dir "$OUT/det1" --report \
    > "$OUT/det1.report" 2> "$OUT/det1.err" || \
    fail "determinism run 1: $(cat "$OUT/det1.err")"
python3 "$TOOL" --assets "$OUT/two.json" --out-dir "$OUT/det2" --report \
    > "$OUT/det2.report" 2> "$OUT/det2.err" || \
    fail "determinism run 2: $(cat "$OUT/det2.err")"
cmp "$OUT/det1/two.h" "$OUT/det2/two.h" || \
    fail "two runs differ byte-for-byte in the header"
cmp "$OUT/det1.report" "$OUT/det2.report" || \
    fail "two runs differ byte-for-byte in the report"
CHECKS=$((CHECKS + 2))
echo "  ok: two runs byte-identical (header + report)"

rm -rf "$OUT/det1"
python3 "$TOOL" --assets "$OUT/two.json" --out-dir "$OUT/det1" --report \
    > "$OUT/det1.report" 2> "$OUT/det1.err" || \
    fail "regeneration: $(cat "$OUT/det1.err")"
cmp "$OUT/det1/two.h" "$OUT/det2/two.h" || \
    fail "regenerated header differs from the original"
cmp "$OUT/det1.report" "$OUT/det2.report" || \
    fail "regenerated report differs from the original"
CHECKS=$((CHECKS + 2))
echo "  ok: delete + regenerate byte-identical (header + report)"

# --- 10) metadata + per-frame refs (Wave 2) ---------------------------------
# game.json's sprite is a 4x4 2-frame strip; a variant adds origin [4, 4]
# (the bottom-right EDGE — allowed, [w/2, h] is the feet-anchor idiom) and
# collision [0, 1, 3, 3] (fits: 0+3 <= 4, 1+3 <= 4).
python3 - "$OUT/game.json" "$OUT/meta.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
doc["sprites"][0]["origin"] = [4, 4]
doc["sprites"][0]["collision"] = [0, 1, 3, 3]
json.dump(doc, open(sys.argv[2], "w"))
PY
expect 0 "origin + collision compile into frame refs" \
    python3 "$TOOL" --assets "$OUT/meta.json" --out-dir "$OUT/gen"
H="$OUT/gen/meta.h"
for wantdef in \
    "static const sprite4_ref player_frames[PLAYER_FRAMES] = {" \
    "    { player_sheet + 0 * PLAYER_STRIDE, PLAYER_W, PLAYER_H, 0, 4, 4, 0, 1, 3, 3 }," \
    "    { player_sheet + 1 * PLAYER_STRIDE, PLAYER_W, PLAYER_H, 0, 4, 4, 0, 1, 3, 3 }," \
    "origin (4, 4), collision (0, 1 3x3)" \
    "the NAME_frames refs need programs/asset.h"; do
    grep -Fq "$wantdef" "$H" || fail "refs header missing: $wantdef — $(cat "$H")"
    CHECKS=$((CHECKS + 1))
done
echo "  ok: refs header (array, both frames, banner metadata, include-order note)"

# sprites without the optional fields still compile, refs default to
# origin (0, 0) and no collision box.
expect 0 "sprites without metadata still compile" \
    python3 "$TOOL" --assets "$OUT/game.json" --out-dir "$OUT/gen"
grep -Fq "    { player_sheet + 0 * PLAYER_STRIDE, PLAYER_W, PLAYER_H, 0, 0, 0, 0, 0, 0, 0 }," \
    "$OUT/gen/game.h" || fail "default ref must be all zeros — $(cat "$OUT/gen/game.h")"
grep -Fq "origin (0, 0), collision (0, 0 0x0)" "$OUT/gen/game.h" || \
    fail "default banner metadata missing — $(cat "$OUT/gen/game.h")"
CHECKS=$((CHECKS + 2))
echo "  ok: absent metadata defaults to origin (0, 0), no collision box"

# shape errors teach the exact field format
manifest "$OUT/badorigin.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"origin":[4]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "one-element origin is a validation error" \
    python3 "$TOOL" --assets "$OUT/badorigin.json" --out-dir "$OUT/gen"
assert_has '"origin" must be [x, y] — two integers' "origin shape error teaches the format"

manifest "$OUT/badcoll.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"collision":[0,0,4]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "three-element collision is a validation error" \
    python3 "$TOOL" --assets "$OUT/badcoll.json" --out-dir "$OUT/gen"
assert_has '"collision" must be [x, y, w, h] — four integers' "collision shape error teaches the format"

manifest "$OUT/strorigin.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"origin":[4,"2"]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "non-integer origin member is a validation error" \
    python3 "$TOOL" --assets "$OUT/strorigin.json" --out-dir "$OUT/gen"
assert_has '"origin" must be [x, y] — two integers' "non-integer origin member"

# geometry errors name both the metadata and the frame
manifest "$OUT/ooborigin.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"origin":[5,0]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "origin outside the frame is a validation error" \
    python3 "$TOOL" --assets "$OUT/ooborigin.json" --out-dir "$OUT/gen"
assert_has "origin [5, 0] outside frame 4x4 — the anchor stays inside" "origin geometry error"

manifest "$OUT/oobcoll.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"collision":[2,2,3,3]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "collision outside the frame is a validation error" \
    python3 "$TOOL" --assets "$OUT/oobcoll.json" --out-dir "$OUT/gen"
assert_has "collision [2, 2, 3, 3] does not fit frame 4x4" "collision geometry error"

manifest "$OUT/halfcoll.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"collision":[1,1,0,3]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 1 "a half-zero collision box is a validation error" \
    python3 "$TOOL" --assets "$OUT/halfcoll.json" --out-dir "$OUT/gen"
assert_has 'box sits inside the sprite' "half-zero box error teaches the fix"

manifest "$OUT/nobox.json" \
    '{"name":"player","file":"ok.png","palette":"world","frames":2,"collision":[0,0,0,0]}' \
    '{"world":{"bank":0,"colors":["#000000","#FFFFFF"]}}'
expect 0 'collision [0, 0, 0, 0] compiles (exactly like omitting it)' \
    python3 "$TOOL" --assets "$OUT/nobox.json" --out-dir "$OUT/gen"

# the emitted refs must COMPILE against the real programs/asset.h (the
# struct order in the emitter and in the SDK header cannot drift apart
# without failing make check right here)
cat > "$OUT/refs_check.c" <<'EOF'
#include "asset.h"
#include "gen/meta.h"
int ref_probe(void) { return player_frames[1].w + player_frames[0].ox; }
EOF
CC=${CC:-cc}
if "$CC" -fsyntax-only -Wall -Wextra -Wno-unused -Wno-pointer-to-int-cast \
        -I"$ROOT/programs" -I"$OUT" "$OUT/refs_check.c" 2> "$OUT/refs_err.txt"; then
    CHECKS=$((CHECKS + 1))
    echo "  ok: emitted refs compile against the real programs/asset.h"
else
    cat "$OUT/refs_err.txt" >&2
    fail "emitted refs do not compile"
fi

# determinism holds with metadata in play (two processes, byte-identical)
python3 "$TOOL" --assets "$OUT/meta.json" --out-dir "$OUT/det3" 2>/dev/null || \
    fail "metadata determinism run 1"
python3 "$TOOL" --assets "$OUT/meta.json" --out-dir "$OUT/det4" 2>/dev/null || \
    fail "metadata determinism run 2"
cmp "$OUT/det3/meta.h" "$OUT/det4/meta.h" || \
    fail "metadata headers differ between two runs"
CHECKS=$((CHECKS + 2))
echo "  ok: metadata headers byte-identical across two runs"

# --- 11) animations + anim_tick (Wave 3) ------------------------------------
# animmanifest <path> <animations-json>: game.json's 4x4 2-frame player plus
# an "animations" array.
animmanifest() {
    python3 - "$OUT/game.json" "$1" "$2" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
doc["animations"] = json.loads(sys.argv[3])
json.dump(doc, open(sys.argv[2], "w"))
PY
}

echo '{"format":1,"palettes":{},"sprites":[],"animations":{}}' > "$OUT/animshape.json"
expect 1 '"animations" must be an array' \
    python3 "$TOOL" --assets "$OUT/animshape.json" --out-dir "$OUT/gen"
assert_has '"animations" must be an array' "animations container error"

animmanifest "$OUT/animobj.json" '[42]'
expect 1 "a non-object animation is a validation error" \
    python3 "$TOOL" --assets "$OUT/animobj.json" --out-dir "$OUT/gen"
assert_has "each animation must be an object" "animation object error"

animmanifest "$OUT/animname.json" '[{"name":"run!","sprite":"player","frames":[0],"ms":80}]'
expect 1 "a non-identifier animation name is a validation error" \
    python3 "$TOOL" --assets "$OUT/animname.json" --out-dir "$OUT/gen"
assert_has 'missing or invalid "name"' "animation name error"

animmanifest "$OUT/animdup.json" \
    '[{"name":"run","sprite":"player","frames":[0],"ms":80},{"name":"run","sprite":"player","frames":[1],"ms":80}]'
expect 1 "a duplicate animation name is a validation error" \
    python3 "$TOOL" --assets "$OUT/animdup.json" --out-dir "$OUT/gen"
assert_has "duplicate animation name" "duplicate animation error"

animmanifest "$OUT/animclash.json" '[{"name":"player","sprite":"player","frames":[0],"ms":80}]'
expect 1 "an animation named like its sprite is a validation error" \
    python3 "$TOOL" --assets "$OUT/animclash.json" --out-dir "$OUT/gen"
assert_has 'clashes with sprite "player"' "sprite/animation clash error"

animmanifest "$OUT/animnospr.json" '[{"name":"run","sprite":"nope","frames":[0],"ms":80}]'
expect 1 "an undefined sprite reference is a validation error" \
    python3 "$TOOL" --assets "$OUT/animnospr.json" --out-dir "$OUT/gen"
assert_has 'sprite "nope" not defined' "undefined sprite error"

animmanifest "$OUT/animframes.json" '[{"name":"run","sprite":"player","frames":[],"ms":80}]'
expect 1 "an empty frames list is a validation error" \
    python3 "$TOOL" --assets "$OUT/animframes.json" --out-dir "$OUT/gen"
assert_has '"frames" must be a non-empty array of integers' "empty frames error"

animmanifest "$OUT/animrange.json" '[{"name":"run","sprite":"player","frames":[0,1,2],"ms":80}]'
expect 1 "an out-of-range frame is a validation error" \
    python3 "$TOOL" --assets "$OUT/animrange.json" --out-dir "$OUT/gen"
assert_has "frame 2 out of range — sprite player has 2 frames (0..1)" "frame range error"

animmanifest "$OUT/animms.json" '[{"name":"run","sprite":"player","frames":[0],"ms":0}]'
expect 1 "ms 0 is a validation error" \
    python3 "$TOOL" --assets "$OUT/animms.json" --out-dir "$OUT/gen"
assert_has '"ms" must be an integer 1..60000' "ms range error"

# happy path: the table preserves PLAY order (frames [1, 0]), and the
# report counts the 4 B-per-step pointers in the ROM total (16 B sheet +
# 32 B palette + 2 steps x 4 B = 56)
animmanifest "$OUT/run.json" '[{"name":"run","sprite":"player","frames":[1,0],"ms":80}]'
expect 0 "a valid animation compiles into a frame table" \
    python3 "$TOOL" --assets "$OUT/run.json" --out-dir "$OUT/gen" --report
H="$OUT/gen/run.h"
for wantdef in "#define RUN_MS 80" \
               "#define RUN_STEPS 2" \
               "static const sprite4_ref *const run_frames[RUN_STEPS] = {" \
               "anim run of player: 2 steps, 80 ms"; do
    grep -Fq "$wantdef" "$H" || fail "anim header missing: $wantdef — $(cat "$H")"
    CHECKS=$((CHECKS + 1))
done
FIRST=$(awk '/run_frames\[RUN_STEPS\]/{f=1;next} f&&/&player_frames/{print;exit}' "$H")
case "$FIRST" in
    *"[1],"*) CHECKS=$((CHECKS + 1))
        echo "  ok: anim table in play order (RUN_MS/STEPS, &frames[1] first, banner)" ;;
    *) fail "anim table must start at &player_frames[1] — got: $FIRST" ;;
esac
assert_has "ANIM run of player: 2 steps, 80 ms = 8 B" "ANIM report line"
assert_has "ROM assets 56 / 262144 (0.0 %)" "ROM total counts the anim table"

# the emitted table must COMPILE against the real programs/asset.h — the
# ANIM() macro, anim_tick() and the emitter's types cannot drift apart
cat > "$OUT/anim_check.c" <<'EOF'
#include "asset.h"
#include "gen/run.h"
static const sprite4_ref *probe(void)
{
    static anim_state run = ANIM(run_frames, RUN_MS, ANIM_LOOP);
    return anim_tick(&run, 0u);
}
int anim_ref_probe(void) { return (int)(probe() - player_frames); }
EOF
CC=${CC:-cc}
if "$CC" -fsyntax-only -Wall -Wextra -Wno-unused -Wno-pointer-to-int-cast \
        -I"$ROOT/programs" -I"$OUT" "$OUT/anim_check.c" 2> "$OUT/anim_err.txt"; then
    CHECKS=$((CHECKS + 1))
    echo "  ok: emitted anim table compiles against the real asset.h"
else
    cat "$OUT/anim_err.txt" >&2
    fail "emitted anim table does not compile"
fi

# anim_tick itself: a runnable host probe over all three modes plus the
# timing edges (exact 3-frame steps at 50 ms, catch-up, wrap, stall clamp)
cat > "$OUT/anim_probe.c" <<'EOF'
#include <stdio.h>
#include "asset.h"

static const sprite4_ref refs[3] = {
    { 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 },   /* w = 1 identifies step 0 */
    { 0, 2, 2, 0, 0, 0, 0, 0, 0, 0 },   /* w = 2 identifies step 1 */
    { 0, 3, 3, 0, 0, 0, 0, 0, 0, 0 },   /* w = 3 identifies step 2 */
};
static const sprite4_ref *const loop3[3] = { &refs[0], &refs[1], &refs[2] };
static const sprite4_ref *const pong3[3] = { &refs[0], &refs[1], &refs[2] };
static const sprite4_ref *const once3[3] = { &refs[0], &refs[1], &refs[2] };

static anim_state a_loop = ANIM(loop3, 50, ANIM_LOOP);
static anim_state a_pong = ANIM(pong3, 50, ANIM_PINGPONG);
static anim_state a_once = ANIM(once3, 50, ANIM_ONCE);
static anim_state a_stall = ANIM(pong3, 60000, ANIM_PINGPONG);
static anim_state a_zero = ANIM(loop3, 0, ANIM_LOOP);   /* hardening 2 */

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int step_of(const sprite4_ref *fr) { return fr->w - 1; }

int main(void)
{
    /* every step reads for exactly 3 frames (50 ms = 3 frames), the
     * first tick carries dt=0 (reset anchors at t=0), so step 1 first
     * shows at t=3 */
    static const int want_loop[10] = {0,0,0,1,1,1,2,2,2,0};
    static const int want_pong[14]  = {0,0,0,1,1,1,2,2,2,1,1,1,0,0};
    static const int want_once[10]  = {0,0,0,1,1,1,2,2,2,2};
    unsigned t;

    /* LOOP: 50 ms = 3000 units = exactly 3 frames per step, no drift */
    anim_reset(&a_loop, 0);
    for (t = 0; t < 10; t++)
        CHECK(step_of(anim_tick(&a_loop, t)) == want_loop[t]);

    /* PINGPONG: forward to the end, back, no repeats at the turns */
    anim_reset(&a_pong, 0);
    for (t = 0; t < 14; t++)
        CHECK(step_of(anim_tick(&a_pong, t)) == want_pong[t]);

    /* ONCE: reaches the last step and holds (done) */
    anim_reset(&a_once, 0);
    for (t = 0; t < 10; t++)
        CHECK(step_of(anim_tick(&a_once, t)) == want_once[t]);
    CHECK(a_once.done);

    /* catch-up: one call after 7 frames advances TWO steps at once and
     * carries the remainder (7000 - 2*3000 = 1000) */
    anim_reset(&a_loop, 0);
    CHECK(step_of(anim_tick(&a_loop, 7)) == 2);
    CHECK(a_loop.sub == 1000u);

    /* wrap-around: unsigned arithmetic keeps the delta exact (dt = 3) */
    anim_reset(&a_pong, 0xFFFFFFFEu);
    CHECK(step_of(anim_tick(&a_pong, 1u)) == 1);

    /* stall clamp: 100 000 frames read as at most 600 (10 s), so the
     * 60 000 ms animation must NOT jump a step */
    anim_reset(&a_stall, 0);
    CHECK(step_of(anim_tick(&a_stall, 100000u)) == 0);

    /* a 0 ms step length holds the current step and drops the carry
     * (before the SDK hardening this case spun forever) */
    anim_reset(&a_zero, 0);
    CHECK(step_of(anim_tick(&a_zero, 30)) == 0);
    CHECK(a_zero.sub == 0u);
    CHECK(step_of(anim_tick(&a_zero, 60)) == 0);

    if (fails) { printf("anim probe: %d FAILS\n", fails); return 1; }
    printf("anim probe: ok\n");
    return 0;
}
EOF
if "$CC" -Wall -Wextra -Wno-pointer-to-int-cast -I"$ROOT/programs" \
        -o "$OUT/anim_probe" "$OUT/anim_probe.c" 2> "$OUT/anim_cc_err.txt" &&
   "$OUT/anim_probe" > "$OUT/anim_out.txt" 2>&1; then
    CHECKS=$((CHECKS + 1))
    echo "  ok: anim_tick unit probe (loop/ping-pong/once, catch-up, wrap, clamp)"
else
    cat "$OUT/anim_cc_err.txt" "$OUT/anim_out.txt" >&2
    fail "anim_tick unit probe failed"
fi

# determinism holds with animations in play (two processes, byte-identical)
python3 "$TOOL" --assets "$OUT/run.json" --out-dir "$OUT/det5" 2>/dev/null || \
    fail "animation determinism run 1"
python3 "$TOOL" --assets "$OUT/run.json" --out-dir "$OUT/det6" 2>/dev/null || \
    fail "animation determinism run 2"
cmp "$OUT/det5/run.h" "$OUT/det6/run.h" || \
    fail "animation headers differ between two runs"
CHECKS=$((CHECKS + 2))
echo "  ok: animation headers byte-identical across two runs"

# --- 12) maps + the minsize retraction (Wave 4) ------------------------------
# The committed mapdemo manifest is the happy fixture: one 64x16 tileset
# sprite (4 scan tiles; tile 3 byte-identical to tile 0), three maps — the
# retraction's 4x3 and 5x1 draw fixtures plus the 10x3 M29-shape field.
MAPMAN="$ROOT/programs/experimental/mapdemo_tiles.json"
# the mutated manifests below live in $OUT and resolve the tileset PNG
# relative to themselves — copy it alongside (a missing image is a
# distinct exit 2, not the validation path these fixtures exercise)
cp "$ROOT/programs/experimental/mapdemo_tiles.png" "$OUT/"

# mapman <path> <maps-json>: the mapdemo manifest with a new "maps" array.
mapman() {
    python3 - "$MAPMAN" "$1" "$2" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
doc["maps"] = json.loads(sys.argv[3])
json.dump(doc, open(sys.argv[2], "w"))
PY
}

echo '{"format":1,"palettes":{},"sprites":[],"maps":{}}' > "$OUT/mapshape.json"
expect 1 '"maps" must be an array' \
    python3 "$TOOL" --assets "$OUT/mapshape.json" --out-dir "$OUT/gen"
assert_has '"maps" must be an array' "maps container error"

# a clean one-cell map the duplicate fixture can hold in its first slot
printf 'x\n' > "$OUT/lvl.txt"
BASEMAP='{"name":"lvl","file":"lvl.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{"x":0}}'
mapman "$OUT/mapobj.json" '[42]'
expect 1 "a non-object map is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapobj.json" --out-dir "$OUT/gen"
assert_has "each map must be an object" "map object error"

mapman "$OUT/mapname.json" '[{"name":"lvl!","file":"lvl.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{}}]'
expect 1 "a non-identifier map name is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapname.json" --out-dir "$OUT/gen"
assert_has 'missing or invalid "name"' "map name error"

mapman "$OUT/mapdup.json" "[$BASEMAP,$BASEMAP]"
expect 1 "a duplicate map name is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapdup.json" --out-dir "$OUT/gen"
assert_has "duplicate map name" "duplicate map error"

mapman "$OUT/mapclash.json" '[{"name":"tiles","file":"lvl.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{}}]'
expect 1 "a map named like its tileset sprite is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapclash.json" --out-dir "$OUT/gen"
assert_has 'clashes with sprite "tiles"' "map/sprite clash error"

mapman "$OUT/mapnospr.json" '[{"name":"lvl","file":"lvl.txt","tileset":"nope","tile_w":16,"tile_h":16,"legend":{}}]'
expect 1 "an undefined tileset is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapnospr.json" --out-dir "$OUT/gen"
assert_has 'tileset "nope" not defined' "undefined tileset error"

mapman "$OUT/mapodd.json" '[{"name":"lvl","file":"lvl.txt","tileset":"tiles","tile_w":15,"tile_h":16,"legend":{}}]'
expect 1 "an odd tile_w is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapodd.json" --out-dir "$OUT/gen"
assert_has "tile_w 15 must be a positive even integer — the 4-bit blit rejects odd widths (src/arcade1/video.c)" "odd tile_w error"

mapman "$OUT/mapdiv.json" '[{"name":"lvl","file":"lvl.txt","tileset":"tiles","tile_w":10,"tile_h":16,"legend":{}}]'
expect 1 "a tile_w that does not divide the tileset is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapdiv.json" --out-dir "$OUT/gen"
assert_has 'tile_w 10 does not tile sprite "tiles" (64 px wide)' "tile_w divisor error"

mapman "$OUT/mapdivh.json" '[{"name":"lvl","file":"lvl.txt","tileset":"tiles","tile_w":16,"tile_h":6,"legend":{}}]'
expect 1 "a tile_h that does not divide the tileset is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapdivh.json" --out-dir "$OUT/gen"
assert_has 'tile_h 6 does not tile sprite "tiles" (16 px tall)' "tile_h divisor error"

mapman "$OUT/mapnofile.json" '[{"name":"lvl","file":"nope.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{}}]'
expect 1 "a missing map file is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapnofile.json" --out-dir "$OUT/gen"
assert_has "nope.txt not found — paths are relative to assets.json" "missing map file error"

: > "$OUT/empty.txt"
mapman "$OUT/mapempty.json" '[{"name":"lvl","file":"empty.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{}}]'
expect 1 "an empty map is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapempty.json" --out-dir "$OUT/gen"
assert_has "map empty.txt is empty — a map needs at least one row of at least one column" "empty map error"

printf 'ab\nabc\n' > "$OUT/rag.txt"
mapman "$OUT/maprag.json" '[{"name":"lvl","file":"rag.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{"a":0,"b":0,"c":0}}]'
expect 1 "ragged rows are a validation error" \
    python3 "$TOOL" --assets "$OUT/maprag.json" --out-dir "$OUT/gen"
assert_has "row 2 of rag.txt is 3 columns wide, row 1 is 2 — all rows must match" "ragged rows error"

printf 'x.\n' > "$OUT/char.txt"
mapman "$OUT/mapchar.json" '[{"name":"lvl","file":"char.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{".":null}}]'
expect 1 "a character without a legend entry is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapchar.json" --out-dir "$OUT/gen"
assert_has "character \"x\" at row 1, column 1 has no legend entry" "legend completeness error"

mapman "$OUT/mapoob.json" '[{"name":"lvl","file":"char.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{"x":9,".":null}}]'
expect 1 "an out-of-range legend tile is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapoob.json" --out-dir "$OUT/gen"
assert_has "legend \"x\" tile 9 out of range — sprite tiles holds 4 tiles (4 x 1 grid)" "legend range error"

mapman "$OUT/mapkey.json" '[{"name":"lvl","file":"char.txt","tileset":"tiles","tile_w":16,"tile_h":16,"legend":{"ab":0,".":null}}]'
expect 1 "a multi-character legend key is a validation error" \
    python3 "$TOOL" --assets "$OUT/mapkey.json" --out-dir "$OUT/gen"
assert_has 'legend key "ab" must be exactly one character' "legend key error"

# happy path: the committed mapdemo fixture (the retraction's 4x3 + 5x1
# draw fixtures and the 10x3 field; ROM total 512 sheet + 32 palette +
# 304 + 404 + 504 map bytes = 1756)
expect 0 "the mapdemo manifest compiles" \
    python3 "$TOOL" --assets "$MAPMAN" --out-dir "$OUT/mapdemo" --report
H="$OUT/mapdemo/mapdemo_tiles.h"
for wantdef in "#define SMALL43_COLS 4" "#define SMALL43_ROWS 3" \
               "#define SMALL43_TILES 2" "#define SMALL51_COLS 5" \
               "#define SMALL51_ROWS 1" "#define SMALL51_TILES 3" \
               "#define FIELD_COLS 10" "#define FIELD_ROWS 3" \
               "#define FIELD_TILES 3"; do
    grep -Fq "$wantdef" "$H" || fail "map header missing: $wantdef"
    CHECKS=$((CHECKS + 1))
done
# dedup proof: 4 scan tiles, tile 3 byte-identical to tile 0 -> 3 blobs
grep -Fq "map field of tiles: 10x3 cells, tile 16x16, 3 unique tiles, bank 5" \
    "$H" || fail "field banner must report 3 unique tiles — $(grep 'map field' "$H")"
CHECKS=$((CHECKS + 2))
echo "  ok: map defines + banners (4x3/5x1 fixtures, field dedup 4->3)"

assert_has "MAP field of tiles: 10x3 cells, 3 tiles = 504 B" "field report line"
assert_has "MAP small43 of tiles: 4x3 cells, 2 tiles = 304 B" "small43 report line"
assert_has "MAP small51 of tiles: 5x1 cells, 3 tiles = 404 B" "small51 report line"
assert_has "ROM assets 1756 / 262144 (0.7 %)" "ROM total counts the maps"

# F3 oracle rule: recompute every map's dedup + cell encoding INDEPENDENTLY
# in python (split the emitted sheet bytes yourself, dedup first-occurrence,
# pack [15:0]|[19:16]|[20]|[21]) and byte-compare with the header arrays.
# A JSON map file ({\"rows\": [...]}) rides along as a fourth map.
printf '{"rows": ["x.", ".x"]}' > "$OUT/jmap.json"
mapman "$OUT/mapjson.json" "[{\"name\":\"jmap\",\"file\":\"jmap.json\",\"tileset\":\"tiles\",\"tile_w\":16,\"tile_h\":16,\"legend\":{\"x\":{\"tile\":1,\"flipY\":true},\".\":null}}]"
expect 0 "a JSON map file compiles" \
    python3 "$TOOL" --assets "$OUT/mapjson.json" --out-dir "$OUT/mapjson"
python3 - "$H" "$MAPMAN" "$ROOT/programs/experimental" \
          "$OUT/mapjson/mapjson.h" "$OUT/mapjson.json" "$OUT" <<'PY'
import json, os, re, sys

sprites = None
fails = []
checked = 0
for trip in range(0, len(sys.argv) - 1, 3):
    header = open(sys.argv[1 + trip]).read()
    manifest = json.load(open(sys.argv[2 + trip]))
    basedir = sys.argv[3 + trip]

    def arr(name, cast):
        m = re.search(re.escape(name) + r"\[[^\]]*\] = \{(.*?)\};", header, re.S)
        if not m:
            sys.exit("header lacks array %s" % name)
        return [cast(t) for t in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]

    sheet = arr("tiles_sheet", lambda h: int(h, 16))
    W = int(re.search(r"#define TILES_W (\d+)", header).group(1))
    HT = int(re.search(r"#define TILES_H (\d+)", header).group(1))
    sprites = {s["name"]: s for s in manifest["sprites"]}

    for mdoc in manifest["maps"]:
        name = mdoc["name"]
        tw, th = mdoc["tile_w"], mdoc["tile_h"]
        stride = W // 2

        def tile(i):                   # independent split, 2 px/byte
            tx, ty = i % (W // tw), i // (W // tw)
            out = []
            for y in range(ty * th, (ty + 1) * th):
                row = sheet[y * stride:(y + 1) * stride]
                out += row[tx * tw // 2:(tx + 1) * tw // 2]
            return bytes(out)

        leg = {}
        for ch, spec in mdoc.get("legend", {}).items():
            if spec is None:
                leg[ch] = (0, 0, 0)
            elif isinstance(spec, int):
                leg[ch] = (spec + 1, 0, 0)
            else:
                leg[ch] = (spec["tile"] + 1,
                           1 if spec.get("flipX") else 0,
                           1 if spec.get("flipY") else 0)
        path = os.path.join(basedir, mdoc["file"])
        if mdoc["file"].endswith(".json"):
            rows = json.load(open(path))["rows"]
        else:
            rows = [l.rstrip("\r\n") for l in open(path)]
        bank = manifest["palettes"][sprites[mdoc["tileset"]]["palette"]]["bank"]
        seen, tiles, cells = {}, [], []
        for r in rows:
            for ch in r:
                idx, fx, fy = leg[ch]
                if idx == 0:
                    cells.append(0)
                    continue
                b = tile(idx - 1)
                if b not in seen:      # first-occurrence dedup
                    seen[b] = len(tiles) + 1
                    tiles.append(b)
                cells.append(seen[b] | bank << 16 | fx << 20 | fy << 21)
        if arr(name + "_cells", lambda h: int(h, 16)) != cells:
            fails.append("%s: cells differ from the oracle" % name)
        if arr(name + "_tiles", lambda h: int(h, 16)) != \
                [v for b in tiles for v in b]:
            fails.append("%s: tile blob differs from the oracle" % name)
        checked += 1
if fails:
    sys.exit("oracle mismatch: " + "; ".join(fails))
print("oracle: %d maps match the independent encoding" % checked)
PY
CHECKS=$((CHECKS + 1))
echo "  ok: cells + tiles match the independent oracle (F3), incl. flips + dedup + JSON rows"

# the emitted arrays must COMPILE against the real SDK — arcade1_tilemap4's
# argument types cannot drift from the emitter's arrays
cat > "$OUT/map_check.c" <<'EOF'
#include "asset.h"
#include "mapdemo/mapdemo_tiles.h"
static void probe(void)
{
    arcade1_tilemap4(0, 0, field_cells, field_tiles,
                     FIELD_COLS, FIELD_ROWS, FIELD_TILE_W, FIELD_TILE_H);
}
EOF
CC=${CC:-cc}
if "$CC" -fsyntax-only -Wall -Wextra -Wno-unused -Wno-pointer-to-int-cast \
        -I"$ROOT/programs" -I"$OUT" "$OUT/map_check.c" 2> "$OUT/map_err.txt"; then
    CHECKS=$((CHECKS + 1))
    echo "  ok: emitted cells/tiles compile against the real arcade1_tilemap4"
else
    cat "$OUT/map_err.txt" >&2
    fail "emitted map arrays do not compile"
fi

# determinism holds with maps in play (two processes, byte-identical)
python3 "$TOOL" --assets "$MAPMAN" --out-dir "$OUT/mapdet1" 2>/dev/null || \
    fail "map determinism run 1"
python3 "$TOOL" --assets "$MAPMAN" --out-dir "$OUT/mapdet2" 2>/dev/null || \
    fail "map determinism run 2"
cmp "$OUT/mapdet1/mapdemo_tiles.h" "$OUT/mapdet2/mapdemo_tiles.h" || \
    fail "map headers differ between two runs"
CHECKS=$((CHECKS + 2))
echo "  ok: map headers byte-identical across two runs"

# --- harness self-check: the mini-PNG-writer really writes a PNG ------------
write_png_rgb "$OUT/fixture.png" 4 2 \
    "FF000000FF000000FF80402040406080C0D0E0F00102030405"
[ "$(od -An -tx1 -N8 "$OUT/fixture.png" | tr -d ' \n')" = \
    "89504e470d0a1a0a" ] || fail "mini-PNG-writer output lacks the PNG signature"
CHECKS=$((CHECKS + 1))
echo "  ok: mini-PNG-writer emits a real PNG (signature check)"

echo "test-arc1assetc: ok ($CHECKS checks, cards 1.1-1.5 + Waves 2-4 assetc)"
