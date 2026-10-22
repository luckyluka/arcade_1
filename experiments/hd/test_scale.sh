#!/bin/sh
# HD_EXPERIMENT.md card H1 — acceptance for the scaled-rendering harness.
#
# usage: experiments/hd/test_scale.sh <default-cli> <hd-k1> <hd-k2> <hd-k4> <rom-dir>
#        (make hd-experiment passes build/arcade1, build/arcade1-hd1,
#         build/arcade1-hd2, build/arcade1-hd4 and tests/roms)
#
# Against the COMMITTED ROMs and input scripts it proves:
#
#   A. k = 1 of the experiment build is identical to the default binary —
#      same frame hash lines, and captures equal byte for byte. The
#      experiment plumbing changes nothing when k = 1.
#   B. k = 2 and k = 4 reproduce the frozen regression line for line over
#      frames 0..400 of each game, i.e. the logical picture (CPU/RAM state
#      hash and the framebuffer hash) is untouched by the scale.
#   C. at frames 60, 200 and 400 the k = 2 and k = 4 framebuffers are EXACTLY
#      the nearest-neighbour upscale of the k = 1 framebuffer, byte for byte.
#   D. pixels_written at k is exactly k^2 times k = 1 — for the frame total
#      and for every recorded command, i.e. for every command type seen.
#
# Deterministic: no timing, no clock, no randomness. Writes only under
# build/hd-experiment/ (gitignored). Not part of `make check` (§0 rule 3).
set -u

CLI=${1:?usage: test_scale.sh <default-cli> <hd-k1> <hd-k2> <hd-k4> <rom-dir>}
HD1=${2:?}
HD2=${3:?}
HD4=${4:?}
ROMDIR=${5:?}

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
INPUTS="$ROOT/tests/inputs"
GOLDEN="$ROOT/tests/golden"
NN="$ROOT/experiments/hd/nn_compare.py"
PX="$ROOT/experiments/hd/px_check.py"
OUT="$ROOT/build/hd-experiment"

GAMES="petalblade asteroid_run flappy"
LAST=401                     # frames 0..400, so 60/200/400 are all inside
SAMPLE_FRAMES="60 200 400"   # the card's sampling points
CMD_FRAMES="61 401"          # frames inspect must report (last frame seen)

fail=0
ok()  { printf '  ok: %s\n' "$*"; }
bad() { printf '  FAIL: %s\n' "$*"; fail=1; }

bin_of() {
    case "$1" in
        cli) printf '%s\n' "$CLI" ;;
        k1)  printf '%s\n' "$HD1" ;;
        k2)  printf '%s\n' "$HD2" ;;
        k4)  printf '%s\n' "$HD4" ;;
        *)   printf 'unknown tag %s\n' "$1" >&2; exit 2 ;;
    esac
}

for exe in "$CLI" "$HD1" "$HD2" "$HD4"; do
    if [ ! -x "$exe" ]; then
        echo "test_scale: not executable: $exe" >&2
        exit 2
    fi
done

rm -rf "$OUT"
mkdir -p "$OUT" || exit 2

echo "test_scale: H1 acceptance, k = 1 / 2 / 4 over $GAMES"
echo "            (geometry per machine: 480x270, 960x540, 1920x1080)"
echo

for g in $GAMES; do
    R="$OUT/$g"
    GAME="$ROMDIR/$g.arc1"
    SCRIPT="$INPUTS/$g.txt"
    mkdir -p "$R"
    echo "$g:"

    if [ ! -f "$GAME" ] || [ ! -f "$SCRIPT" ]; then
        bad "missing $GAME or $SCRIPT"
        echo
        continue
    fi

    # --- A + B: every frame line over frames 0..$(LAST-1) ----------------
    for tag in cli k1 k2 k4; do
        bin=$(bin_of "$tag")
        if "$bin" run "$GAME" --frames "$LAST" --input "$SCRIPT" \
               > "$R/$tag.out" 2> "$R/$tag.err"; then
            grep '^frame ' "$R/$tag.out" > "$R/$tag.fr"
        else
            bad "$tag: run exited $? (see $R/$tag.err)"
            : > "$R/$tag.fr"
        fi
    done

    head -n "$LAST" "$GOLDEN/$g.txt" > "$R/golden.head"
    if [ ! -s "$R/golden.head" ]; then
        bad "golden $GOLDEN/$g.txt has fewer than $LAST frame lines"
    elif diff -q "$R/cli.fr" "$R/golden.head" > /dev/null 2>&1; then
        ok "default binary matches the first $LAST frame lines of tests/golden/$g.txt"
    else
        bad "default binary differs from tests/golden/$g.txt (first mismatch below)"
        diff "$R/cli.fr" "$R/golden.head" 2>&1 | head -2 | sed 's/^/       /'
    fi

    if diff -q "$R/cli.fr" "$R/k1.fr" > /dev/null 2>&1; then
        ok "k=1 experiment binary: frame lines identical to the default binary"
    else
        bad "k=1 experiment binary differs from the default binary"
    fi

    for tag in k2 k4; do
        if diff -q "$R/$tag.fr" "$R/golden.head" > /dev/null 2>&1; then
            ok "$tag: all $LAST frame lines match the golden (logical picture identical)"
        else
            bad "$tag: frame lines differ from the golden"
            diff "$R/$tag.fr" "$R/golden.head" 2>&1 | head -2 | sed 's/^/       /'
        fi
    done

    # --- C: nearest-neighbour upscale of the framebuffer, byte for byte --
    for f in $SAMPLE_FRAMES; do
        N=$((f + 1))                    # frames 0..f inclusive
        for tag in cli k1 k2 k4; do
            bin=$(bin_of "$tag")
            if ! "$bin" run "$GAME" --frames "$N" --input "$SCRIPT" \
                   --capture "$R/${tag}_$f.ppm" \
                   > /dev/null 2> "$R/${tag}_$f.err"; then
                bad "$tag f=$f: capture run exited $? (see $R/${tag}_$f.err)"
            fi
        done

        if cmp -s "$R/cli_$f.ppm" "$R/k1_$f.ppm" 2>/dev/null; then
            ok "f=$f: k=1 capture byte-identical to the default binary's"
        else
            bad "f=$f: k=1 capture differs from the default binary's"
        fi

        if python3 "$NN" "$R/k1_$f.ppm" "$R/k2_$f.ppm" 2; then
            ok "f=$f: k=2 framebuffer is the exact nearest-neighbour upscale of k=1"
        else
            bad "f=$f: k=2 framebuffer is not the nearest-neighbour upscale of k=1"
        fi
        if python3 "$NN" "$R/k1_$f.ppm" "$R/k4_$f.ppm" 4; then
            ok "f=$f: k=4 framebuffer is the exact nearest-neighbour upscale of k=1"
        else
            bad "f=$f: k=4 framebuffer is not the nearest-neighbour upscale of k=1"
        fi
    done

    # --- D: pixels_written is exactly k^2, per command and per frame -----
    for f in $CMD_FRAMES; do
        for tag in k1 k2 k4; do
            bin=$(bin_of "$tag")
            if ! "$bin" inspect "$GAME" --frames "$f" --input "$SCRIPT" \
                   --commands > "$R/${tag}_ins_$f.txt" \
                   2> "$R/${tag}_ins_$f.err"; then
                bad "$tag f=$f: inspect exited $? (see $R/${tag}_ins_$f.err)"
            fi
        done
        if python3 "$PX" "$R/k1_ins_$f.txt" "$R/k2_ins_$f.txt" 2; then
            :
        else
            bad "f=$f: k=2 pixels_written is not exactly 4x k=1"
        fi
        if python3 "$PX" "$R/k1_ins_$f.txt" "$R/k4_ins_$f.txt" 4; then
            :
        else
            bad "f=$f: k=4 pixels_written is not exactly 16x k=1"
        fi
    done

    # Keep only what was asserted about; the PPMs are large.
    rm -f "$R"/*.ppm
    echo
done

# --- D, continued: every command type ---------------------------------------
# The three acceptance games never issue TILEMAP (the 1-bpp tilemap), so run
# one more committed image that does — identically at every k, with no input
# script (platformer's golden comes from tests/test_platformer.c, not from the
# CLI, so it is driven here purely as command coverage) — and then assert that
# EVERY pixel-drawing command type was actually seen in one of these runs.
# "k^2 for every command type" is only proved for types that occurred.
echo "command coverage (adds TILEMAP, which the three games never issue):"
COV="platformer"
for f in $CMD_FRAMES; do
    for tag in k1 k2 k4; do
        bin=$(bin_of "$tag")
        if ! "$bin" inspect "$ROMDIR/$COV.arc1" --frames "$f" --commands \
               > "$OUT/${COV}_${tag}_ins_$f.txt" \
               2> "$OUT/${COV}_${tag}_ins_$f.err"; then
            bad "$tag $COV f=$f: inspect exited $? (see $OUT/${COV}_${tag}_ins_$f.err)"
        fi
    done
    if python3 "$PX" "$OUT/${COV}_k1_ins_$f.txt" "$OUT/${COV}_k2_ins_$f.txt" 2; then
        :
    else
        bad "$COV f=$f: k=2 pixels_written is not exactly 4x k=1"
    fi
    if python3 "$PX" "$OUT/${COV}_k1_ins_$f.txt" "$OUT/${COV}_k4_ins_$f.txt" 4; then
        :
    else
        bad "$COV f=$f: k=4 pixels_written is not exactly 16x k=1"
    fi
done

types=$(grep -h '^CMD ' "$OUT"/*_k1_ins_*.txt "$OUT"/*/k1_ins_*.txt 2>/dev/null \
        | awk '{print $3}' | sort -u | tr '\n' ' ')
printf '            command types exercised at k=1:%s\n' " $types"
for t in CLEAR FILL SET_PIXEL SPRITE SPRITE4 TILEMAP TILEMAP4 PRESENT; do
    case " $types " in
        *" $t "*) ;;
        *) bad "command type $t never occurred, so its k^2 rule is unproven" ;;
    esac
done
ok "all eight command types occurred, and each obeyed k^2 exactly"
echo

if [ "$fail" -ne 0 ]; then
    echo "test_scale: FAILED — see the FAIL lines above"
    exit 1
fi
echo "test_scale: H1 acceptance passed (A/B/C/D: default, k=1, k=2, k=4)"
