#!/bin/sh
# ROADMAP_V4 card 7.2: the drawing library, extracted without changing a
# byte.
#
# Acceptance (pinned):
#   1. re-running the generator through tools/arc1_art/canvas.py
#      regenerates the manifest and every strip BYTE-IDENTICALLY — the
#      sha256 of each committed strip is pinned below;
#   2. two runs are identical (determinism);
#   3. the unit tests of every primitive pass (tests/test_art_canvas.py),
#      each pinned by a hand-computed pixel expectation.
#
# Host-side only: it regenerates the art into a temp dir and compares.
#
# Usage: test_art_canvas.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
case "$ROOT" in /*) ;; *) ROOT="$(pwd)/$ROOT" ;; esac

ART=$ROOT/programs/petalblade_art
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-art-canvas: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -d "$ART" ] || fail "missing $ART"
[ -f "$ART/gen_art.py" ] || fail "missing $ART/gen_art.py"
[ -f "$ROOT/tools/arc1_art/canvas.py" ] || fail "missing tools/arc1_art/canvas.py"

# --- 1) the committed strips, by sha256 ------------------------------------
# The generator is the master; this list says what it must produce.
EXPECTED="beetle.png 8f7c2bfcc92e37c90b93513bf5a4cc49977160cc63b97f9f41458ff97d48e4b7
bg_mid.png e36cfdb22be2eb92d12f7a615a81f64fdfb1f36f935cd77a8a9af4d7ee73f598
bg_near.png f15b911da4c96140ba96428a0d70c0e209ffbd012b825cbae185430e18b795ad
bg_ridge.png 690519438d32d296f749c35dba76d6392d804ac7419b9a59f0eb893bc23e6407
coin.png c0451f2c27246c61560ac985ba0ba7de7ecc551040ebb4e1a003c9654f6fc18c
fx.png c412f6dcecb8857ce2557c4d8e652ffb41ec729bfb6bdde3f7bf517680a51e3b
goal.png bdb85f7e27baee64f1ea3801713aa77f49483216ab2aa3a24a24479926e14633
hero.png 5efe8c677c42a2882a4b7b4b30eaee4ee9e2ac5d86de3d44b63eecae45d07658
moon.png 5281e44e7b04a35c151631cb174ce49c16abc8e06fc7137e99ddd7190f0f9116
oni.png 56c439e459440db61efa43aacfd4906bf7929afc0c3fb024e4031067a21a76fd
slash.png f7aedfa0d5a38c40db425a8246f47d699b4bc9bf034f805312185a78b7731f7d
tiles.png 119a7be5dd65c3af2baa171d5beca03bde8ef1a73e7c54ccbcb48e5bdde9460f"

# --- the generator, twice, into two temp dirs ------------------------------
for run in 1 2; do
    d="$OUT/run$run"
    mkdir -p "$d"
    python3 "$ART/gen_art.py" --dir "$d" --manifest petalblade_assets.json \
        --level-header "$d/level.h" > "$OUT/gen$run.log" 2>&1 || \
        fail "the generator failed on run $run: $(cat "$OUT/gen$run.log")"
done

# --- 1) every strip, byte for byte ----------------------------------------
echo "$EXPECTED" | while read -r name sum; do
    [ -n "$name" ] || continue
    got=$(sha256sum "$OUT/run1/art/$name" | cut -d' ' -f1)
    if [ "$got" != "$sum" ]; then
        echo "test-art-canvas: FAILED — $name is $got, want $sum" >&2
        exit 1
    fi
done || fail "a regenerated strip differs from the committed one"
CHECKS=$((CHECKS + 1))
echo "  ok: every regenerated strip is byte-identical to the committed one"

# the manifest and the level header too
cmp -s "$OUT/run1/petalblade_assets.json" "$ART/petalblade_assets.json" || \
    fail "the regenerated manifest differs from the committed one"
cmp -s "$OUT/run1/level.h" "$ROOT/programs/petalblade_level.h" || \
    fail "the regenerated level header differs from the committed one"
CHECKS=$((CHECKS + 1))
echo "  ok: the manifest and the level header are byte-identical"

# --- 2) determinism --------------------------------------------------------
diff -r "$OUT/run1" "$OUT/run2" > /dev/null || \
    fail "two runs of the generator differ"
CHECKS=$((CHECKS + 1))
echo "  ok: two runs of the generator are identical"

# --- 3) the unit tests -----------------------------------------------------
python3 "$ROOT/tests/test_art_canvas.py" > "$OUT/unit.log" 2>&1 || {
    cat "$OUT/unit.log" >&2
    fail "the primitive unit tests failed"
}
grep -q "test-art-canvas: ok" "$OUT/unit.log" || fail "the unit tests did not report ok"
CHECKS=$((CHECKS + 1))
echo "  ok: $(grep -o '([0-9]* checks' "$OUT/unit.log") of primitive unit tests"

echo "test-art-canvas: ok ($CHECKS checks, card 7.2)"
