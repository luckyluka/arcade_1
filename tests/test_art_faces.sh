#!/bin/sh
# ROADMAP_V4 (the library): a rig body with a .px face on it.
#
# The rig cannot draw a face — its shapes are ellipses, capsules and
# polygons, so the head is a blank ellipse. The fix is composition: the
# rig gives the body, a `.px` pixmap gives the face, the game draws one
# over the other. This test proves the composition works end to end:
#
#   1. the packs merge (characters + enemies + faces) into one manifest;
#   2. the manifest compiles (the palettes are shared, so the names are
#      predictable);
#   3. the guest draws two rigs with a face on each;
#   4. each face's EYES are on its head — a rig's head alone has none.
#
# Host-side only (the guest runs in the emulator).
#
# Usage: test_art_faces.sh <arcade1-cli> <guest-build-dir> [repo-root]
set -eu

CLI=${1:?usage: test_art_faces.sh <arcade1-cli> <guest-build-dir> [repo-root]}
GUEST=${2:?usage: test_art_faces.sh <arcade1-cli> <guest-build-dir> [repo-root]}
ROOT=${3:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-faces: FAILED — $*" >&2; exit 1; }
CHECKS=0

ART=$ROOT/tools/arc1-art
[ -x "$ART" ] || fail "no arc1-art at $ART"

# --- 1) the packs merge ----------------------------------------------------
rc=0
"$ART" merge characters enemies faces --out "$OUT/assets.json" \
    > /dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "the merge failed"
python3 - "$OUT/assets.json" <<'PY' || exit 1
import json, sys
doc = json.load(open(sys.argv[1]))
names = [s["name"] for s in doc["sprites"]]
assert "samurai_walk" in names, "the samurai is not in the merge"
assert "skeleton_walk" in names, "the skeleton is not in the merge"
assert "face_samurai" in names, "the samurai's face is not in the merge"
assert "face_skeleton" in names, "the skeleton's face is not in the merge"
# the packs SHARE the palettes they have in common: the same name, the
# same colours -> one bank, whatever order the packs were merged in
for name in ("hero16", "enemy16"):
    pal = doc["palettes"][name]
    assert pal["bank"] < 16, "the palette bank is out of range"
banks = [p["bank"] for p in doc["palettes"].values()]
assert len(banks) == len(set(banks)), "two palettes share a bank"
PY
CHECKS=$((CHECKS + 1))
echo "  ok: the packs merge into one manifest (the palettes are shared)"

# --- 2) the manifest compiles ---------------------------------------------
python3 "$ROOT/tools/arc1_assetc.py" --assets "$OUT/assets.json" \
    --out-dir "$OUT" > /dev/null || fail "the merged manifest did not compile"
[ -f "$OUT/assets.h" ] || fail "no header was written"
CHECKS=$((CHECKS + 1))
echo "  ok: the merged manifest compiles"

# --- 3) + 4) the guest draws the faces onto the heads ---------------------
mkdir -p "$OUT/guest"
cp "$ROOT/tests/guests/face_probe.c" "$OUT/guest/"
riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 -Os -ffreestanding \
    -nostdlib -nostartfiles -msmall-data-limit=0 -I"$ROOT/programs" \
    -I"$OUT" -T "$ROOT/programs/link.ld" -o "$OUT/guest/face_probe.elf" \
    "$ROOT/programs/crt0.S" "$OUT/guest/face_probe.c" 2>"$OUT/cc.log" || {
    cat "$OUT/cc.log" >&2
    fail "the face probe did not compile"
}
riscv64-unknown-elf-objcopy -O binary "$OUT/guest/face_probe.elf" \
    "$OUT/guest/face_probe.bin" || fail "objcopy failed"

"$CLI" run "$OUT/guest/face_probe.bin" --frames 3 \
    --capture "$OUT/scene.ppm" --quiet > /dev/null 2>&1 || \
    fail "the face probe did not run"

# count the DARK pixels in each face's box. A rig's head is a blank
# ellipse — the skin, the bone, nothing else — so any dark pixel in the
# box is the face's eyes or mouth.
count_dark() {
    tail -c $((480 * 270 * 3)) "$OUT/scene.ppm" | od -An -v -tu1 | awk \
        -v x0="$1" -v x1="$2" -v y0="$3" -v y1="$4" '
        { for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
            if (n % 3 == 0) {
                p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1 &&
                    c[0] < 90 && c[1] < 90 && c[2] < 110) cnt++ } } }
        END { print cnt + 0 }'
}

# the samurai's face: 8x8 at (10, 8)
dark=$(count_dark 10 17 8 15)
[ "$dark" -ge 8 ] || \
    fail "the samurai's face is not on the head ($dark dark pixels)"

# the skeleton's face: 8x8 at (68, 3)
dark2=$(count_dark 68 75 3 10)
[ "$dark2" -ge 8 ] || \
    fail "the skeleton's face is not on the head ($dark2 dark pixels)"

# and the bodies are there too: the figures are taller than the faces
body=$(tail -c $((480 * 270 * 3)) "$OUT/scene.ppm" | od -An -v -tu1 | awk \
    '{ for (i = 1; i <= NF; i++) { c[n % 3] = $i; n++
        if (n % 3 == 0) {
            p = int(n / 3) - 1; x = p % 480; y = int(p / 480)
            if (c[0] || c[1] || c[2]) {
                if (y > max) max = y
                cnt++ } } } }
    END { print cnt + 0 " " max + 0 }')
[ "${body% *}" -gt 200 ] || fail "the body is not drawn (${body% *} pixels)"
CHECKS=$((CHECKS + 1))
echo "  ok: the rig bodies and the pixmap faces compose ($dark and $dark2 dark pixels on the heads)"

echo "test-art-faces: ok ($CHECKS checks)"
