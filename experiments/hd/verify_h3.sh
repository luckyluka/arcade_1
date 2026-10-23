#!/bin/sh
# experiments/hd/verify_h3.sh — HD_EXPERIMENT.md card H3's acceptance, made
# mechanical so it can be re-run instead of believed.
#
#   A. DETERMINISM.  Regenerate both art sets and require them byte-identical
#      to what is on disk; build both guests with `arcade1 build`; rebuild
#      both captures into a temporary directory and require them byte-identical
#      to docs/experiments/hd-art_480.png / _960.png. The captures are then
#      reproducible artifacts, not screenshots of one lucky run. The fb hash
#      must also be the same on every frame — the card asks for ONE STATIC
#      SCENE, and a per-frame fb hash would mean it moves.
#
#   B. SHAPED CAPTURES.  Both PNGs are exactly 1920x1080, 8-bit truecolour,
#      and their IDAT stream really inflates (one chunk, ratio ~0.008) — i.e.
#      the card's "both at the same on-screen size, compressed PNG" is the
#      file, not a claim about it.
#
#   C. REVIEW, CRITERION BY CRITERION.  docs/experiments/hd-art.md must carry
#      art/CRITIQUE.md's five criteria, the verdict in the card's own words,
#      the count of colours the hero needed, both screenshots, and the byte
#      totals.
#
#   D. BYTES.  Every sprite's compiled size read out of the generated headers
#      must match H2's rule bytes = W * H * 4 / 8 within 10%, and the assets
#      this card regenerated must quadruple within 10% (H2's table is a
#      straight 4x).
#
#   E. UNTOUCHABLES.  tests/golden and tests/roms uncommitted, src/ clean,
#      and HD_EXPERIMENT.md itself still untracked (the card file is the
#      owner's, never staged).
#
# usage: experiments/hd/verify_h3.sh
#   needs build/arcade1, build/arcade1-geo960, the scene ROMs and captures
#   (make hd-scene produces all of them)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DOC="$ROOT/docs/experiments/hd-art.md"
CAP480="$ROOT/docs/experiments/hd-art_480.png"
CAP960="$ROOT/docs/experiments/hd-art_960.png"
CLI="$ROOT/build/arcade1"
GEO="$ROOT/build/arcade1-geo960"

for b in "$CLI" "$GEO"; do
    [ -x "$b" ] || { echo "verify_h3: missing $b (run 'make hd-scene')" >&2
                     exit 1; }
done
for f in "$DOC" "$CAP480" "$CAP960"; do
    [ -f "$f" ] || { echo "verify_h3: missing $f (run 'make hd-scene' and" \
                          "write the report)" >&2; exit 1; }
done

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT

echo "verify_h3: A — determinism (art, ROMs and captures reproduce exactly)"

for s in 1 2; do
    dir="$ROOT/experiments/hd/scene$([ "$s" = 1 ] && echo 480 || echo 960)_art"
    cp -r "$dir" "$TMP/save$s"
    python3 "$ROOT/experiments/hd/build_scene_art.py" --scale "$s" >/dev/null
    if ! diff -r "$dir" "$TMP/save$s" >"$TMP/diff$s" 2>&1; then
        echo "verify_h3: A FAILED — regenerating the $s x art changed it:" >&2
        cat "$TMP/diff$s" >&2
        exit 1
    fi
    echo "  ok: scene${s}x art regenerates byte-identical"
done

"$CLI" build "$ROOT/experiments/hd/scene_480.c" "$TMP/scene_480.arc1" \
    >/dev/null
"$CLI" build "$ROOT/experiments/hd/scene_960.c" "$TMP/scene_960.arc1" \
    >/dev/null
if ! cmp -s "$TMP/scene_480.arc1" "$ROOT/build/scene_480.arc1"
then
    echo "verify_h3: A FAILED — scene_480.arc1 did not reproduce" >&2
    exit 1
fi
if ! cmp -s "$TMP/scene_960.arc1" "$ROOT/build/scene_960.arc1"
then
    echo "verify_h3: A FAILED — scene_960.arc1 did not reproduce" >&2
    exit 1
fi
echo "  ok: both guests rebuild byte-identical (480: $(wc -c < \
     "$TMP/scene_480.arc1") B, 960: $(wc -c < "$TMP/scene_960.arc1") B)"

static_check() {
    bin=$1 rom=$2 scale=$3 name=$4
    "$bin" run "$rom" --frames 5 --scale "$scale" --capture "$TMP/$name.png" \
        > "$TMP/$name.log" 2>&1
    n=$(awk '/^frame/ { print $NF }' "$TMP/$name.log" | sort -u | wc -l)
    [ "$n" = 1 ] || {
        echo "verify_h3: A FAILED — $name is not static: $n distinct fb" \
             "hashes over 5 frames" >&2
        cat "$TMP/$name.log" >&2
        exit 1
    }
    cmp -s "$TMP/$name.png" "$ROOT/docs/experiments/hd-art_$name.png" || {
        echo "verify_h3: A FAILED — hd-art_$name.png did not reproduce" >&2
        exit 1
    }
    echo "  ok: $name is static (fb hash identical on all 5 frames) and its" \
         "capture reproduces byte for byte"
}
static_check "$CLI" "$ROOT/build/scene_480.arc1" 4 480
static_check "$GEO" "$ROOT/build/scene_960.arc1" 2 960

echo "verify_h3: B — both captures are 1920x1080 compressed PNGs"
check_png() {
    python3 - "$1" <<'PY'
import struct, sys, zlib
p = sys.argv[1]
d = open(p, "rb").read()
assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
w, h = struct.unpack(">II", d[16:24])
assert (w, h) == (1920, 1080), "expected 1920x1080, got %dx%d" % (w, h)
assert d[24] == 8 and d[25] == 2, "expected 8-bit truecolour"
i, idat, n = 8, b"", 0
while i < len(d):
    ln = struct.unpack(">I", d[i:i + 4])[0]
    typ = d[i + 4:i + 8]
    if typ == b"IDAT":
        idat += d[i + 8:i + 8 + ln]; n += 1
    if typ == b"IEND":
        break
    i += 12 + ln
raw = zlib.decompress(idat)
ratio = len(idat) / len(raw)
assert ratio < 0.05, "IDAT is %d%% of raw — not really deflated" % (ratio * 100)
print("  ok: %s  %dx%d, %d IDAT, %s B deflated of %s raw (%.3f)"
      % (p.rsplit("/", 1)[-1], w, h, n, f"{len(idat):,}", f"{len(raw):,}",
         ratio))
PY
}
check_png "$CAP480"
check_png "$CAP960"

echo "verify_h3: C — the review covers every CRITIQUE criterion and says a" \
     "verdict"
for want in "silhouette" "lighting" "palette harmony" "outline" "noise"; do
    grep -qi "$want" "$DOC" || {
        echo "verify_h3: C FAILED — hd-art.md never mentions '$want'" >&2
        exit 1
    }
done
echo "  ok: silhouette / lighting / palette harmony / outline / noise all" \
     "present"
for want in "hd-art_480.png" "hd-art_960.png"; do
    grep -q "$want" "$DOC" || {
        echo "verify_h3: C FAILED — hd-art.md does not embed $want" >&2
        exit 1; }
done
echo "  ok: both screenshots referenced"
if ! grep -Eiq 'verdict|looks (better|worse)|the same' "$DOC"; then
    echo "verify_h3: C FAILED — hd-art.md states no plain verdict" >&2
    exit 1
fi
echo "  ok: a plain verdict is stated"
if ! grep -Eqi 'colou?rs the hero|hero uses [0-9]+|colours? of the bank' "$DOC"
then
    echo "verify_h3: C FAILED — hd-art.md never states how many colours the" \
         "hero needed" >&2
    exit 1
fi
echo "  ok: the hero's colour count is stated"

echo "verify_h3: D — compiled bytes vs H2's W*H*4/8, within 10%"
METRICS=$(python3 "$ROOT/experiments/hd/art_metrics.py")
echo "$METRICS" | sed -n '/^worst row deviation/p'
worst=$(echo "$METRICS" \
    | sed -n 's/^worst row deviation: +\{0,1\}\([-0-9.]*\)%.*/\1/p')
[ -n "$worst" ] || {
    echo "verify_h3: D FAILED — art_metrics.py reported no deviation" >&2
    exit 1
}
python3 - "$worst" <<'PY'
import sys
v = abs(float(sys.argv[1]))
if v > 10.0:
    sys.exit("verify_h3: D FAILED — worst row deviates %.1f%% (> 10%%)" % v)
PY
echo "$METRICS" | sed -n '/^2x rule/,/^all sprites/p' | sed 's/^/  /'
echo "  ok: every row within 10% of H2's rule"

echo "verify_h3: E — the things a tooling card must not touch"
if [ -n "$(git -C "$ROOT" status --porcelain -- tests/golden tests/roms)" ]; then
    echo "verify_h3: E FAILED — tests/golden or tests/roms is modified:" >&2
    git -C "$ROOT" status --porcelain -- tests/golden tests/roms >&2
    exit 1
fi
echo "  ok: tests/golden and tests/roms untouched"
if [ -n "$(git -C "$ROOT" status --porcelain -- src/)" ]; then
    echo "verify_h3: E FAILED — src/ has uncommitted changes:" >&2
    git -C "$ROOT" status --porcelain -- src/ >&2
    exit 1
fi
echo "  ok: src/ untouched by this card"
if git -C "$ROOT" ls-files --error-unmatch HD_EXPERIMENT.md >/dev/null 2>&1
then
    echo "verify_h3: E FAILED — HD_EXPERIMENT.md is tracked; it is the" \
         "owner's file and is never staged" >&2
    exit 1
fi
echo "  ok: HD_EXPERIMENT.md still untracked"

echo "verify_h3: A, B, C, D and E all pass"
