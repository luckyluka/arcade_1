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

# --- harness self-check: the mini-PNG-writer really writes a PNG ------------
write_png_rgb "$OUT/fixture.png" 4 2 \
    "FF000000FF000000FF80402040406080C0D0E0F00102030405"
[ "$(od -An -tx1 -N8 "$OUT/fixture.png" | tr -d ' \n')" = \
    "89504e470d0a1a0a" ] || fail "mini-PNG-writer output lacks the PNG signature"
CHECKS=$((CHECKS + 1))
echo "  ok: mini-PNG-writer emits a real PNG (signature check)"

echo "test-arc1assetc: ok ($CHECKS checks, cards 1.1-1.4)"
