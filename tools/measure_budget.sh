#!/bin/sh
# tools/measure_budget.sh — measure the device's per-frame work at 480x270.
#
# ROADMAP_V4 card 4.8 (measurement only — no machine change, no cost model).
# DEVICE_BUDGET.md's numbers were measured at 320x240; this script
# re-measures every golden game at the real 480x270 screen and writes a CSV.
#
# How it measures: `arcade1 inspect --frames N` reports the LAST frame's
# counters (instructions, pixels written, command counts). The script sweeps N
# over the game's golden length with a stride and collects one sample per
# step; the avg/max below are over those samples. The stride is a knob
# (STRIDE=1 measures every frame — quadratic in the frame count).
#
# Deterministic: the machine is deterministic, so two runs write the same CSV
# byte for byte.
#
# Usage: tools/measure_budget.sh [cli] [rom-dir] [golden-dir] [out-csv]
#   defaults: build/arcade1  tests/roms  tests/golden
#             docs/experiments/budget-480x270.csv
set -eu

CLI=${1:-build/arcade1}
ROMDIR=${2:-tests/roms}
GOLD=${3:-tests/golden}
OUT=${4:-docs/experiments/budget-480x270.csv}
STRIDE=${STRIDE:-50}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac
[ -x "$CLI" ] || { echo "measure_budget: no CLI at $CLI" >&2; exit 1; }

# The golden games plus Petal Blade (Game #3). "golden_name:rom_name" —
# the golden file and the ROM have different names for three of them (the
# same mapping the Makefile's golden_rom uses).
GAMES=${GAMES:-"breakout tetris platformer scroll:scroll_demo color:color_demo \
pong:example_pong snake:example_snake example-color:example_color \
m29game asteroid_run flappy petalblade"}

SCREEN_PIXELS=129600        # 480 * 270

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

printf 'game,frames,samples,instret_avg,instret_max,pixels_avg,pixels_max,cmds_avg,cmds_max,overdraw_avg,overdraw_max\n' > "$OUT"

for entry in $GAMES; do
    g=${entry%%:*}
    r=${entry#*:}
    [ "$r" = "$entry" ] && r=$g
    rom="$ROMDIR/$r.arc1"
    golden="$GOLD/$g.txt"
    [ -f "$rom" ]    || { echo "measure_budget: missing $rom" >&2; exit 1; }
    [ -f "$golden" ] || { echo "measure_budget: missing $golden" >&2; exit 1; }

    frames=$(wc -l < "$golden" | tr -d ' ')
    samples=0

    n=1
    while [ "$n" -le "$frames" ]; do
        "$CLI" inspect "$rom" --frames "$n" 2>/dev/null > "$TMP/r.txt"
        inst=$(awk '$1=="INSTRET_FRAME"{print $2}' "$TMP/r.txt")
        px=$(awk '$1=="VIDEO_PIXELS_WRITTEN"{print $2}' "$TMP/r.txt")
        cmd=$(awk '$1=="VIDEO_CMDS"{
                 for (i = 2; i <= NF; i++) { split($i, a, "="); s += a[2] }
                 print s }' "$TMP/r.txt")
        [ -n "$inst" ] && [ -n "$px" ] && [ -n "$cmd" ] || {
            echo "measure_budget: $g frame $n: inspect reported no counters" >&2
            exit 1
        }
        printf '%s %s %s\n' "$inst" "$px" "$cmd" >> "$TMP/samples.txt"
        samples=$((samples + 1))
        n=$((n + STRIDE))
    done

    awk -v g="$g" -v frames="$frames" -v s="$samples" \
        -v screen="$SCREEN_PIXELS" '
    { i += $1; if ($1 > imax) imax = $1
      p += $2; if ($2 > pmax) pmax = $2
      c += $3; if ($3 > cmax) cmax = $3 }
    END {
        printf "%s,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f\n", \
            g, frames, s, i/s, imax, p/s, pmax, c/s, cmax, \
            (p/s)/screen, pmax/screen
    }' "$TMP/samples.txt" >> "$OUT"

    printf '  %-14s frames %5s  samples %3s  inst %s  px %s  overdraw %s\n' \
        "$g" "$frames" "$samples" \
        "$(awk '{i+=$1; if ($1>m) m=$1} END{printf "%d/%d", i/NR, m}' "$TMP/samples.txt")" \
        "$(awk '{p+=$2; if ($2>m) m=$2} END{printf "%d/%d", p/NR, m}' "$TMP/samples.txt")" \
        "$(awk -v sc=$SCREEN_PIXELS '{p+=$2} END{printf "%.3f", (p/NR)/sc}' "$TMP/samples.txt")"

    rm -f "$TMP/samples.txt"
done

echo "measure_budget: wrote $OUT (stride $STRIDE; STRIDE=1 measures every frame)"
