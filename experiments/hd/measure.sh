#!/bin/sh
# experiments/hd/measure.sh — HD_EXPERIMENT.md card H2, step 1.
#
# Runs every game that has a committed input script at k = 1, 2 and 4, FRAMES
# frames each, and writes one CSV row per (game, k).
#
# Columns:
#   game            the guest
#   k               ARCADE1_HD_SCALE: 1 -> 480x270, 2 -> 960x540, 4 -> 1920x1080
#   resolution      the PHYSICAL framebuffer, WxH
#   fb_bytes        2 * W * H, the RGB565 framebuffer's size in RAM
#   screen_pixels   W * H, the physical screen; the denominator of the overdraw
#   px_avg          physical pixels written per frame, AVERAGE over FRAMES
#   px_max          physical pixels written per frame, MAXIMUM over FRAMES
#   px_last         frame FRAMES-1 only — the number `arcade1 inspect` reports
#   overdraw_avg    px_avg   / screen_pixels
#   overdraw_max    px_max   / screen_pixels
#   bench_ms_*      host ms per frame, over arcade1_run_frame only (MEDIAN /
#                   P95 / MAX); bench_cpu names the host those came from
#   rom_bytes       image payload bytes in use, from `arcade1 inspect`
#   inst_avg/max/last  guest instructions retired per frame
#
# EXACT, not sampled: `run --bench` accumulates one entry per frame, so the
# avg/max cover all FRAMES frames. Therefore re-running this script changes
# ONLY the bench_ms_* columns (and nothing else — the machine is
# deterministic); experiments/hd/verify_h2.sh check A proves that.
#
# k = 1 uses build/arcade1-hd1 — the same sources with the scale pinned to 1
# — so all three rows are one code path, and H1 proved that binary identical
# to the default build.
#
# usage: experiments/hd/measure.sh [rom-dir] [out-csv]
#   defaults: tests/roms  docs/experiments/hd-measurements.csv
#   env:      FRAMES (default 600), GAMES
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ROMDIR=${1:-$ROOT/tests/roms}
OUT=${2:-$ROOT/docs/experiments/hd-measurements.csv}
INPUTS="$ROOT/tests/inputs"
FRAMES=${FRAMES:-600}
GAMES=${GAMES:-"petalblade asteroid_run flappy shmup brickstorm"}

BIN1="$ROOT/build/arcade1-hd1"
BIN2="$ROOT/build/arcade1-hd2"
BIN4="$ROOT/build/arcade1-hd4"

# Logical geometry. The guest's screen never changes with k; the physical one
# is k times it in each direction.
LOGICAL_W=480
LOGICAL_H=270

for b in "$BIN1" "$BIN2" "$BIN4"; do
    [ -x "$b" ] || { echo "measure: missing $b (run 'make hd-measure')" >&2
                     exit 1; }
done

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT

bin_for() {
    case "$1" in
        1) printf '%s\n' "$BIN1" ;;
        2) printf '%s\n' "$BIN2" ;;
        4) printf '%s\n' "$BIN4" ;;
        *) echo "measure: no binary for k=$1" >&2; exit 2 ;;
    esac
}

# Value of "KEY rest of line" (rest may contain spaces).
keyval() { sed -n "s/^$2 //p" "$1" | head -n 1; }

printf '%s\n' \
  'game,k,frames,resolution,fb_bytes,screen_pixels,px_avg,px_max,px_last,overdraw_avg,overdraw_max,bench_ms_median,bench_ms_p95,bench_ms_max,bench_cpu,rom_bytes,inst_avg,inst_max,inst_last' \
  > "$OUT"

for g in $GAMES; do
    rom="$ROMDIR/$g.arc1"
    script="$INPUTS/$g.txt"
    [ -f "$rom" ]    || { echo "measure: missing $rom" >&2;    exit 1; }
    [ -f "$script" ] || { echo "measure: missing $script" >&2; exit 1; }

    # ROM bytes in use: a property of the image, not of k. One inspect run.
    "$BIN1" inspect "$rom" --frames 1 > "$TMP/ins.txt" 2>/dev/null
    rom_bytes=$(awk '$1 == "ROM_USED" { print $2 }' "$TMP/ins.txt")
    [ -n "$rom_bytes" ] || { echo "measure: no ROM_USED for $g" >&2; exit 1; }

    for k in 1 2 4; do
        bin=$(bin_for "$k")
        "$bin" run "$rom" --frames "$FRAMES" --input "$script" --bench \
            > "$TMP/run.txt" 2>/dev/null

        bmed=$(keyval  "$TMP/run.txt" BENCH_MS_MEDIAN)
        bp95=$(keyval  "$TMP/run.txt" BENCH_MS_P95)
        bmax=$(keyval  "$TMP/run.txt" BENCH_MS_MAX)
        bcpu=$(keyval  "$TMP/run.txt" BENCH_CPU_MODEL)
        pxa=$(keyval   "$TMP/run.txt" WORK_PX_AVG)
        pxm=$(keyval   "$TMP/run.txt" WORK_PX_MAX)
        pxl=$(keyval   "$TMP/run.txt" WORK_PX_LAST)
        ia=$(keyval    "$TMP/run.txt" WORK_INST_AVG)
        im=$(keyval    "$TMP/run.txt" WORK_INST_MAX)
        il=$(keyval    "$TMP/run.txt" WORK_INST_LAST)

        for v in "$bmed" "$bp95" "$bmax" "$bcpu" "$pxa" "$pxm" "$pxl" \
                 "$ia" "$im" "$il"; do
            [ -n "$v" ] || { echo "measure: $g k=$k: missing a --bench key" >&2
                             sed 's/^/    /' "$TMP/run.txt"; exit 1; }
        done

        awk -v game="$g" -v k="$k" -v frames="$FRAMES" \
            -v w="$((LOGICAL_W * k))" -v h="$((LOGICAL_H * k))" \
            -v pxa="$pxa" -v pxm="$pxm" -v pxl="$pxl" \
            -v bmed="$bmed" -v bp95="$bp95" -v bmax="$bmax" -v bcpu="$bcpu" \
            -v romb="$rom_bytes" \
            -v ia="$ia" -v im="$im" -v il="$il" \
            'BEGIN {
                 sp = w * h
                 printf "%s,%d,%d,%dx%d,%d,%d,%.1f,%s,%s,%.4f,%.4f,%s,%s,%s,%s,%s,%.1f,%s,%s\n",
                        game, k, frames, w, h, 2 * sp, sp,
                        pxa, pxm, pxl,
                        pxa / sp, pxm / sp,
                        bmed, bp95, bmax, bcpu, romb,
                        ia, im, il
             }' >> "$OUT"
    done
    echo "measure: $g done (k = 1, 2, 4; $FRAMES frames each)"
done

echo "measure: wrote $OUT ($(wc -l < "$OUT" | tr -d ' ') lines, header included)"
