#!/bin/sh
# experiments/hd/verify_h2.sh — HD_EXPERIMENT.md card H2's acceptance, made
# mechanical so it can be re-run instead of believed.
#
#   A. DETERMINISM.  Re-run measure.sh into a temporary CSV and compare it to
#      docs/experiments/hd-measurements.csv with the three host-timing columns
#      (bench_ms_median, _p95, _max) dropped. Those three are wall-clock
#      measurements on this box; every other column is a property of the
#      deterministic machine and must match byte for byte.
#
#   B. SPOT CHECK.  Three (game, k) rows read back against `arcade1 inspect`
#      invoked directly at the same frame count. The row's px_last, inst_last
#      and rom_bytes must equal inspect's VIDEO_PIXELS_WRITTEN,
#      INSTRET_FRAME and ROM_USED — i.e. the CSV really is the CLI's own
#      numbers, not a second implementation.
#
#   C. GENERATED, NOT TYPED.  Regenerate hd-fpga-estimates.md from the CSV
#      and require it byte-identical to the committed file. The FPGA and
#      asset tables therefore cannot have been hand-edited.
#
# usage: experiments/hd/verify_h2.sh
#   needs build/arcade1-hd{1,2,4} (make hd-measure builds them)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CSV="$ROOT/docs/experiments/hd-measurements.csv"
MD="$ROOT/docs/experiments/hd-fpga-estimates.md"
INPUTS="$ROOT/tests/inputs"
ROMDIR="$ROOT/tests/roms"

for b in build/arcade1-hd1 build/arcade1-hd2 build/arcade1-hd4; do
    [ -x "$ROOT/$b" ] || { echo "verify_h2: missing $b (run 'make hd-measure')"
                           >&2; exit 1; }
done
[ -f "$CSV" ] || { echo "verify_h2: no $CSV (run 'make hd-measure')" >&2; exit 1; }

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT

# CSV columns: 1 game 2 k 3 frames 4 resolution 5 fb_bytes 6 screen_pixels
#              7 px_avg 8 px_max 9 px_last 10 overdraw_avg 11 overdraw_max
#              12 bench_ms_median 13 bench_ms_p95 14 bench_ms_max
#              15 bench_cpu 16 rom_bytes 17 inst_avg 18 inst_max 19 inst_last
# Everything except 12, 13 and 14 must be reproducible.
drop_timing() {
    awk -F, '{
        out = ""
        for (i = 1; i <= NF; i++)
            if (i < 12 || i > 14)
                out = out (out == "" ? "" : ",") $i
        print out
    }' "$1"
}

echo "verify_h2: A — determinism (re-running measure.sh changes timing only)"
FRAMES=600 sh "$ROOT/experiments/hd/measure.sh" "$ROMDIR" "$TMP/again.csv" \
    >/dev/null
drop_timing "$CSV"  > "$TMP/was.txt"
drop_timing "$TMP/again.csv" > "$TMP/is.txt"
if ! cmp -s "$TMP/was.txt" "$TMP/is.txt"; then
    echo "verify_h2: A FAILED — a non-timing column changed:" >&2
    diff -u "$TMP/was.txt" "$TMP/is.txt" >&2 || true
    exit 1
fi
echo "  ok: all $(( $(wc -l < "$TMP/was.txt") - 1 )) rows identical apart from" \
     "bench_ms_median / _p95 / _max"

echo "verify_h2: B — three CSV rows read back against 'arcade1 inspect'"
check_row() {
    game=$1 k=$2
    case $k in 1) bin=$ROOT/build/arcade1-hd1 ;;
               2) bin=$ROOT/build/arcade1-hd2 ;;
               4) bin=$ROOT/build/arcade1-hd4 ;;
               *) echo "verify_h2: bad k=$k" >&2; exit 1 ;; esac

    "$bin" inspect "$ROMDIR/$game.arc1" --frames 600 \
        --input "$INPUTS/$game.txt" > "$TMP/ins.txt" 2>/dev/null

    i_px=$(awk '$1 == "VIDEO_PIXELS_WRITTEN" { print $2 }' "$TMP/ins.txt")
    i_inst=$(awk '$1 == "INSTRET_FRAME"      { print $2 }' "$TMP/ins.txt")
    i_rom=$(awk '$1 == "ROM_USED"           { print $2 }' "$TMP/ins.txt")
    i_frm=$(awk '$1 == "FRAME"              { print $2 }' "$TMP/ins.txt")

    row=$(awk -F, -v g="$game" -v k="$k" '$1 == g && $2 == k' "$CSV")
    [ -n "$row" ] || { echo "verify_h2: no CSV row for $game k=$k" >&2; exit 1; }
    c_px=$(printf '%s\n'   "$row" | cut -d, -f9)
    c_inst=$(printf '%s\n' "$row" | cut -d, -f19)
    c_rom=$(printf '%s\n'  "$row" | cut -d, -f16)
    c_frm=$(printf '%s\n'  "$row" | cut -d, -f3)

    for pair in "px:$i_px:$c_px" "inst:$i_inst:$c_inst" "rom:$i_rom:$c_rom" \
                "frames:$i_frm:$c_frm"; do
        f=${pair%%:*}; rest=${pair#*:}
        iv=${rest%%:*}; cv=${rest#*:}
        [ "$iv" = "$cv" ] || {
            echo "verify_h2: B FAILED — $game k=$k $f:" >&2
            echo "  inspect $iv != csv $cv" >&2
            exit 1
        }
    done
    echo "  ok: $game k=$k  px=$c_px inst=$c_inst rom=$c_rom frames=$c_frm"
}
check_row petalblade 1
check_row shmup 4
check_row brickstorm 2

echo "verify_h2: C — the FPGA/asset tables are generated, not typed"
python3 "$ROOT/experiments/hd/fpga_estimates.py" "$CSV" "$TMP/regen.md" \
    >/dev/null
if ! cmp -s "$MD" "$TMP/regen.md"; then
    echo "verify_h2: C FAILED — $MD differs from what fpga_estimates.py" \
         "generates:" >&2
    diff -u "$MD" "$TMP/regen.md" >&2 || true
    exit 1
fi
echo "  ok: $(basename "$MD") == fpga_estimates.py(CSV), byte for byte"

echo "verify_h2: A, B and C all pass"
