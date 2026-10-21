#!/bin/sh
# ROADMAP_V4 (after the clean-room rounds): the asset library.
#
# A library is only worth having if it holds up. This test proves:
#   1. every pack regenerates BYTE-IDENTICALLY (the art is code, and the
#      committed strips are the build truth);
#   2. every asset passes `arc1-art check`;
#   3. every pack has a style sheet and a review;
#   4. the index lists every pack and every asset;
#   5. the packs merge into a manifest a game can compile.
#
# Host-side only.
#
# Usage: test_art_library.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

ART=$ROOT/tools/arc1-art
LIB=$ROOT/art/library

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-library: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -x "$ART" ] || fail "no arc1-art at $ART"
[ -d "$LIB" ] || fail "no library at $LIB"

PACKS=$(ls "$LIB" 2>/dev/null | grep -v '\.md$' || true)
[ -n "$PACKS" ] || fail "the library holds no packs"

# --- 1) every pack regenerates byte-identically ----------------------------
rc=0
"$ART" library --check > "$OUT/check.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "a pack does not regenerate byte for byte: $(cat "$OUT/check.log")"
CHECKS=$((CHECKS + 1))
echo "  ok: every pack regenerates byte for byte"

# --- 2) every asset passes the checks --------------------------------------
for p in $PACKS; do
    [ -d "$LIB/$p" ] || continue
    frag="$LIB/$p/$p.json"
    [ -f "$frag" ] || fail "the pack $p has no manifest fragment"
    python3 - "$frag" "$LIB/$p" <<'PY' || exit 1
import json, os, subprocess, sys
frag, pack_dir = sys.argv[1], sys.argv[2]
doc = json.load(open(frag))
root = os.path.dirname(os.path.dirname(os.path.dirname(pack_dir)))
art = os.path.join(root, "tools", "arc1-art")
# the role: a sprite is one closed shape with an outline; a tileset, a
# parallax layer and a particle are not, and the checks grade them
# differently (the check's --role)
roles = doc.get("roles", {})
bad = 0
for sprite in doc.get("sprites", []):
    path = os.path.join(pack_dir, sprite["file"])
    if not os.path.isfile(path):
        sys.stderr.write("test-art-library: FAILED — %s: the strip %s is "
                         "missing\n" % (sprite["name"], path))
        bad += 1
        continue
    frames = max(1, int(sprite.get("frames", 1)))
    import struct
    with open(path, "rb") as f:
        w, h = struct.unpack(">II", f.read(24)[16:24])
    r = subprocess.run([art, "check", path, "--frame",
                        "%dx%d" % (w // frames, h),
                        "--role", roles.get(sprite["name"], "sprite")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write("test-art-library: FAILED — %s: %s\n"
                         % (sprite["name"], r.stderr.strip()))
        bad += 1
sys.exit(1 if bad else 0)
PY
    CHECKS=$((CHECKS + 1))
done
echo "  ok: every asset passes the checks"

# --- 3) every pack has a style sheet and a review --------------------------
for p in $PACKS; do
    [ -d "$LIB/$p" ] || continue
    [ -f "$LIB/$p/STYLE.md" ] || fail "the pack $p has no style sheet"
    [ -f "$ROOT/art/reports/$p.md" ] || fail "the pack $p has no review"
    [ -f "$LIB/$p/gen_$p.py" ] || fail "the pack $p has no generator"
done
CHECKS=$((CHECKS + 1))
echo "  ok: every pack has a generator, a style sheet and a review"

# --- 4) the index lists everything -----------------------------------------
[ -f "$LIB/INDEX.md" ] || fail "no index"
for p in $PACKS; do
    [ -d "$LIB/$p" ] || continue
    grep -q "\`$p\`" "$LIB/INDEX.md" || fail "the index does not list $p"
done
python3 - "$LIB" "$ROOT" <<'PY' || exit 1
import json, os, sys
lib, root = sys.argv[1], sys.argv[2]
index = open(os.path.join(lib, "INDEX.md")).read()
missing = []
for pack in sorted(os.listdir(lib)):
    frag = os.path.join(lib, pack, "%s.json" % pack)
    if not os.path.isfile(frag):
        continue
    for sprite in json.load(open(frag)).get("sprites", []):
        if "`%s`" % sprite["name"] not in index:
            missing.append("%s/%s" % (pack, sprite["name"]))
if missing:
    sys.stderr.write("test-art-library: FAILED — the index does not list: %s\n"
                     % ", ".join(missing))
    sys.exit(1)
PY
CHECKS=$((CHECKS + 1))
echo "  ok: the index lists every pack and every asset"

# --- 5) the packs merge into a manifest a game can compile ------------------
rc=0
"$ART" merge icons characters village --out "$OUT/merged.json" > /dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "the merge failed"
[ -s "$OUT/merged.json" ] || fail "the merged manifest is empty"
python3 - "$OUT/merged.json" <<'PY' || exit 1
import json, sys
doc = json.load(open(sys.argv[1]))
banks = [p["bank"] for p in doc["palettes"].values()]
assert len(banks) == len(set(banks)), "two palettes share a bank"
names = [s["name"] for s in doc["sprites"]]
assert len(names) == len(set(names)), "two sprites share a name"
assert doc["sprites"], "the merge produced no sprites"
PY
CHECKS=$((CHECKS + 1))
echo "  ok: the packs merge into one manifest (no bank or name collisions)"

echo "test-art-library: ok ($CHECKS checks)"
