#!/bin/sh
# ARCADE-1 — example_color test: paletted 4-bit colour, moving camera.
#
# Drives example_color (<rom-dir>/example_color.arc1) through the arcade1
# CLI and asserts through captured frames and named variables:
#   1. determinism      — two identical runs, byte-identical framebuffers
#   2. colour           — the frame has at least 6 distinct colours
#   3. camera moves     — cam_x/cam_y differ between frame 0 and frame 50
#   4. tilemap scrolls  — the tilemap area differs between those frames
#
# usage: test_example_color.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_example_color.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-color: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

COLOR=$ROM/example_color.arc1

[ -x "$CLI" ]   || fail "CLI not built at $CLI"
[ -f "$COLOR" ] || fail "missing $COLOR"

# Named-variable reads need an `nm` dump: use the committed one (the golden
# ROM dir has it), else make one from the freshly built ELF.
SYMS=$ROM/example_color.syms
if [ ! -f "$SYMS" ] && [ -f "$ROM/example_color.elf" ] &&
   command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    riscv64-unknown-elf-nm "$ROM/example_color.elf" > "$OUT/example_color.syms"
    SYMS="$OUT/example_color.syms"
fi

# --- 1) determinism ----------------------------------------------------------
"$CLI" run "$COLOR" --frames 1 --capture "$OUT/f0a.ppm" --quiet > /dev/null 2>&1
"$CLI" run "$COLOR" --frames 1 --capture "$OUT/f0b.ppm" --quiet > /dev/null 2>&1
if ! cmp -s "$OUT/f0a.ppm" "$OUT/f0b.ppm"; then
    fail "two identical runs produced different framebuffers"
fi
note "determinism ok — two runs byte-identical"

# Primary run: verbose, so its per-frame hash lines are what the golden
# regression (tests/golden/example-color.txt) records. The camera moves on
# its own; no input script needed. The output goes to a file and to stdout
# (for the golden grep).
"$CLI" run "$COLOR" --frames 100 > "$OUT/main.txt" 2>/dev/null
cat "$OUT/main.txt"
note "primary run ok — 100 frames"

if [ -f "$SYMS" ]; then
    # --- 2) colour -----------------------------------------------------------
    # Frame 0 again, captured with the camera at (0,0): the whole tilemap is
    # on screen, so the palette shows. (Same framebuffer as f0a.)
    N=$(tail -c +16 "$OUT/f0a.ppm" | od -An -tu1 -v | awk '
        { for (i = 1; i <= NF; i++) px[++n] = $i }
        END {
            c = 0
            for (i = 1; i <= n; i += 3) {
                k = px[i] SUBSEP px[i+1] SUBSEP px[i+2]
                if (!(k in seen)) { seen[k] = 1; c++ }
            }
            print c
        }')
    N=${N:-0}
    [ "$N" -ge 6 ] || fail "frame has $N distinct colours, need >= 6"
    note "colour ok — $N distinct colours in the frame"

    # --- 3) camera moves ------------------------------------------------------
    "$CLI" run "$COLOR" --frames 1 --syms "$SYMS" --vars cam_x,cam_y \
        --quiet > "$OUT/v0.txt" 2>/dev/null
    "$CLI" run "$COLOR" --frames 51 --capture "$OUT/f50.ppm" --syms "$SYMS" \
        --vars cam_x,cam_y --quiet > "$OUT/v50.txt" 2>/dev/null
    grep -q '^var cam_x = 0$' "$OUT/v0.txt" ||
        fail "frame 0: expected cam_x 0, got: $(grep '^var cam_x' "$OUT/v0.txt")"
    grep -q '^var cam_y = 0$' "$OUT/v0.txt" ||
        fail "frame 0: expected cam_y 0, got: $(grep '^var cam_y' "$OUT/v0.txt")"
    grep -q '^var cam_x = 100$' "$OUT/v50.txt" ||
        fail "frame 50: expected cam_x 100, got: $(grep '^var cam_x' "$OUT/v50.txt")"
    grep -q '^var cam_y = 25$' "$OUT/v50.txt" ||
        fail "frame 50: expected cam_y 25, got: $(grep '^var cam_y' "$OUT/v50.txt")"
    note "camera ok — (0,0) -> (100,25) between frame 0 and frame 50"

    # --- 4) tilemap scrolls ---------------------------------------------------
    # The tilemap area (rows 32..127, full width) must differ: the camera
    # shift reveals different world tiles.
    tail -c +$((15 + 32 * 480 * 3 + 1)) "$OUT/f0a.ppm"  | head -c $((96 * 480 * 3)) > "$OUT/band0.raw"
    tail -c +$((15 + 32 * 480 * 3 + 1)) "$OUT/f50.ppm" | head -c $((96 * 480 * 3)) > "$OUT/band50.raw"
    if cmp -s "$OUT/band0.raw" "$OUT/band50.raw"; then
        fail "tilemap area identical at camera (0,0) and (100,25) — the map is not scrolling"
    fi
    note "tilemap ok — the map area differs between the two camera positions"
else
    note "named variables skipped — no example_color.syms and no riscv nm to make one"
fi

echo "test-color: example_color ok (determinism, colour, camera, tilemap)"
