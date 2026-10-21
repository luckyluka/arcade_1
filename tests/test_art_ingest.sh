#!/bin/sh
# ROADMAP_V4 card 7.5: raster ingest (LOCAL ONLY, stdlib only).
#
# The messy sheet is SYNTHESIZED in the test with card 7.2's library — a
# gradient background, soft edges and noise, the way an image model leaves
# things. No third-party image is committed, and none is needed.
#
# Acceptance (pinned):
#   1. after the ingest: at most 15 colours plus transparent;
#   2. the feet baseline is identical in all the frames;
#   3. the orphan count is 0;
#   4. two runs are byte-identical;
#   5. report.json carries its keys;
#   6. the teaching errors: a non-integer grid, and a sheet with too few
#      opaque pixels.
#
# Host-side only.
#
# Usage: test_art_ingest.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

ART=$ROOT/tools/arc1-art
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-ingest: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -x "$ART" ] || fail "no arc1-art at $ART"

# --- the messy sheet, synthesized with the canvas library -------------------
python3 - "$ROOT" "$OUT" <<'PY' || exit 1
import os, sys
sys.path.insert(0, os.path.join(sys.argv[1], "tools"))
from arc1_art import canvas
from arc1_art.canvas import Rng

out = sys.argv[2]
COLS, ROWS, SIZE = 4, 2, 32
W, H = COLS * SIZE, ROWS * SIZE
rng = Rng(20261021)

raw = bytearray()
for y in range(H):
    raw.append(0)                           # the PNG filter byte
    cy = y // SIZE
    ly = y - cy * SIZE
    for cx in range(COLS):
        top = (40 + cx * 7, 60 + cy * 5, 90)
        bot = (70 + cx * 7, 100 + cy * 5, 140)
        shape = (230, 90 + cx * 20, 70)
        t = ly / float(SIZE - 1)
        bg = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        for x in range(SIZE):
            dx = x - SIZE / 2.0 + 0.5
            dy = ly - SIZE / 2.0 + 0.5
            d = (dx * dx + dy * dy) / float((SIZE / 2 - 4) ** 2)
            if d <= 1.0:
                soft = max(0.0, min(1.0, (1.0 - d) * 4))  # a soft edge
                p = tuple(int(bg[i] + (shape[i] - bg[i]) * soft)
                          for i in range(3))
            else:
                p = bg
            raw += bytes(p)

# the noise: a few specks of a random colour, and a stray blob
for _ in range(40):
    x = rng.rand(W)
    y = rng.rand(H)
    raw[(y * (W * 3 + 1)) + 1 + x * 3:(y * (W * 3 + 1)) + 4 + x * 3] = \
        bytes((rng.rand(256), rng.rand(256), rng.rand(256)))

canvas.write_indexed_png(os.path.join(out, "messy.png"), raw, W, H)
print("sheet: %dx%d, %d cells of %dx%d" % (W, H, COLS * ROWS, SIZE, SIZE))
PY

# --- 1) to 5) the ingest itself -------------------------------------------
rc=0
"$ART" ingest "$OUT/messy.png" --out-dir "$OUT/a" --grid 4x2 --target-h 24 \
    --tolerance 30 --min-blob 4 --anchor feet --outline 1 > "$OUT/log.txt" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "the ingest failed: $(cat "$OUT/log.txt")"

[ -f "$OUT/a/sheet.png" ] || fail "no strip was written"
[ -f "$OUT/a/assets.json" ] || fail "no manifest fragment was written"
[ -f "$OUT/a/sheet_ingest.png" ] || fail "no before/after sheet was written"
[ -f "$OUT/a/report.json" ] || fail "no report was written"

python3 - "$OUT/a" <<'PY' || exit 1
import json, os, sys
out = sys.argv[1]
report = json.load(open(os.path.join(out, "report.json")))
fails = []
for key in ("colours", "bbox_jitter", "orphans", "blobs_removed", "size",
            "colour_count", "key_colours"):
    if key not in report:
        fails.append("report.json has no %r" % key)
if report.get("colour_count", 99) > 15:
    fails.append("the ingest used %s colours (max 15)"
                 % report.get("colour_count"))
if report.get("orphans", 1) != 0:
    fails.append("the ingest left %s orphan pixels"
                 % report.get("orphans"))

# the feet baseline: the same lowest opaque row in every frame
boxes = report.get("bbox_jitter", [])
bottoms = set(b[1] + b[3] for b in boxes)
if len(bottoms) != 1:
    fails.append("the frames do not share a feet baseline: %s" % sorted(bottoms))

# the strip itself: at most 15 colours plus transparent
import zlib, struct
data = open(os.path.join(out, "sheet.png"), "rb").read()
pos, idat = 8, b""
while pos < len(data):
    ln = struct.unpack(">I", data[pos:pos + 4])[0]
    tag = data[pos + 4:pos + 8]
    if tag == b"IDAT":
        idat += data[pos + 8:pos + 8 + ln]
    if tag == b"IHDR":
        w, h, depth, ctype = struct.unpack(">IIBB", data[pos + 8:pos + 18])
    pos += 12 + ln
raw = zlib.decompress(idat)
stride = w * 3 + 1
colours = set()
for y in range(h):
    row = raw[y * stride:(y + 1) * stride]
    for x in range(w):
        colours.add(bytes(row[1 + x * 3:4 + x * 3]))
if len(colours) > 15:
    fails.append("the strip has %d colours (max 15)" % len(colours))

for f in fails:
    sys.stderr.write("test-art-ingest: FAILED — %s\n" % f)
if fails:
    sys.exit(1)
PY
CHECKS=$((CHECKS + 1))
echo "  ok: <=15 colours, one feet baseline, 0 orphans, the report's keys"

# --- 4) two runs are byte-identical ---------------------------------------
"$ART" ingest "$OUT/messy.png" --out-dir "$OUT/b" --grid 4x2 --target-h 24 \
    --tolerance 30 --min-blob 4 --anchor feet --outline 1 > /dev/null 2>&1
for f in sheet.png assets.json sheet_ingest.png report.json; do
    cmp -s "$OUT/a/$f" "$OUT/b/$f" || fail "two runs differ: $f"
done
CHECKS=$((CHECKS + 1))
echo "  ok: two ingests are byte-identical"

# --- 6) the teaching errors ----------------------------------------------
rc=0
"$ART" ingest "$OUT/messy.png" --out-dir "$OUT/c" --grid 4x2x2 > "$OUT/e1.txt" 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "a non-integer grid should exit 1, got $rc"
grep -q "is not CxR" "$OUT/e1.txt" || fail "the grid message is wrong: $(cat "$OUT/e1.txt")"
rc=0
"$ART" ingest "$OUT/messy.png" --out-dir "$OUT/c" --cell 32 > "$OUT/e2.txt" 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "a non-integer cell size should exit 1, got $rc"
grep -q "is not WxH" "$OUT/e2.txt" || fail "the cell message is wrong: $(cat "$OUT/e2.txt")"
# a sheet with nothing opaque: a flat background and a huge tolerance
python3 - "$ROOT" "$OUT" <<'PY'
import os, sys
sys.path.insert(0, os.path.join(sys.argv[1], "tools"))
from arc1_art import canvas
raw = bytearray()
for y in range(32):
    raw.append(0)
    for x in range(32):
        raw += bytes((10, 20, 30))
canvas.write_indexed_png(os.path.join(sys.argv[2], "flat.png"), raw, 32, 32)
PY
rc=0
"$ART" ingest "$OUT/flat.png" --out-dir "$OUT/c" --grid 1x1 \
    --tolerance 255 > "$OUT/e3.txt" 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "an empty sheet should exit 1, got $rc"
grep -q "no opaque pixels" "$OUT/e3.txt" || \
    fail "the empty-sheet message is wrong: $(cat "$OUT/e3.txt")"
CHECKS=$((CHECKS + 1))
echo "  ok: the teaching errors (grid, cell size, empty sheet)"

echo "test-art-ingest: ok ($CHECKS checks, card 7.5)"
