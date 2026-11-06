#!/bin/sh
# ROADMAP_V4 card 8.1: the WAV dump (`arcade1 run --audio FILE`).
#
# `--audio FILE` writes the audio device's samples: 48 kHz, 16-bit, stereo,
# 800 samples per frame, the existing mixing model. Acceptance (pinned):
#   1. a guest holding a 440 Hz tone yields a WAV whose zero-crossing count
#      over one second is 880 +-2 (2 crossings per cycle);
#   2. two identical runs produce byte-identical files;
#   3. the WAV length is exactly frames x 800 samples (checked at 60 and
#      30 frames, header sizes included);
#   4. stdout with --audio equals stdout without it — the dump moves no
#      hash — and the header is the canonical PCM/48000/2ch/16-bit one.
#
# Host-side only: the test writes to a temp dir, it never touches the
# committed goldens or ROMs.
#
# Usage: test_audio_wav.sh <arcade1-cli> <guest-build-dir>
set -eu

CLI=${1:?usage: test_audio_wav.sh <arcade1-cli> <guest-build-dir>}
GUEST=${2:?usage: test_audio_wav.sh <arcade1-cli> <guest-build-dir>}

case "$CLI" in /*) ;; *) CLI="$(pwd)/$CLI" ;; esac

HERE=$(dirname "$0")
PROBE=$GUEST/probe_wav.bin

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-audio-wav: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -f "$PROBE" ] || fail "missing $PROBE (build it: make programs/build/probe_wav.bin)"

# --- the runs --------------------------------------------------------------
# Two identical --audio runs (must be byte-identical), one plain run whose
# stdout must be the --audio run's stdout, and a 30-frame run to pin the
# length rule at a second frame count.
"$CLI" run "$PROBE" --frames 60 --audio "$OUT/a.wav" > "$OUT/a.out" \
    || fail "run 1 (--audio, 60 frames) exited non-zero"
"$CLI" run "$PROBE" --frames 60 --audio "$OUT/b.wav" > "$OUT/b.out" \
    || fail "run 2 (--audio, 60 frames) exited non-zero"
"$CLI" run "$PROBE" --frames 60 > "$OUT/plain.out" \
    || fail "run 3 (no --audio, 60 frames) exited non-zero"
"$CLI" run "$PROBE" --frames 30 --audio "$OUT/half.wav" --quiet \
    > "$OUT/half.out" || fail "run 4 (--audio, 30 frames) exited non-zero"

# --- 2) two runs are byte-identical ----------------------------------------
cmp -s "$OUT/a.wav" "$OUT/b.wav" || fail "two --audio runs are not byte-identical"
CHECKS=$((CHECKS + 1))
echo "  ok: two --audio runs are byte-identical ($(wc -c < "$OUT/a.wav") bytes)"

# --- 4a) stdout with --audio == stdout without it --------------------------
cmp -s "$OUT/a.out" "$OUT/plain.out" || {
    diff "$OUT/a.out" "$OUT/plain.out" | head -6 >&2
    fail "--audio changed stdout (the hashes moved)"
}
CHECKS=$((CHECKS + 1))
echo "  ok: stdout with --audio equals stdout without it (hashes unchanged)"

# --- 1) + 3) + 4b) header, length, and the 440 Hz zero-crossing count ------
python3 - "$OUT/a.wav" 60 "$OUT/half.wav" 30 <<'PY'
import struct
import sys

a_path, a_frames, b_path, b_frames = (sys.argv[1], int(sys.argv[2]),
                                      sys.argv[3], int(sys.argv[4]))
failures = []


def fail(msg):
    failures.append(msg)


def check(path, frames):
    raw = open(path, "rb").read()
    # The canonical 44-byte header, every field as the writer promises.
    (tag, riff_size, wave, fmt, fmt_size, fmt_tag, chans,
     rate, byte_rate, align, bits, data_tag, data_size) = struct.unpack(
         "<4sI4s4sIHHIIHH4sI", raw[:44])
    if tag != b"RIFF" or wave != b"WAVE" or fmt != b"fmt " or data_tag != b"data":
        fail("%s: not a canonical RIFF/WAVE file" % path)
    if (fmt_size, fmt_tag, chans, rate, byte_rate, align, bits) != (
            16, 1, 2, 48000, 192000, 4, 16):
        fail("%s: fmt chunk is %r, want PCM/48000/2ch/16-bit" %
             (path, (fmt_size, fmt_tag, chans, rate, byte_rate, align, bits)))
    want_bytes = frames * 800 * 4          # 800 stereo 16-bit samples
    if riff_size != 36 + want_bytes or data_size != want_bytes:
        fail("%s: sizes are riff=%u data=%u, want %u/%u" %
             (path, riff_size, data_size, 36 + want_bytes, want_bytes))
    if len(raw) != 44 + want_bytes:
        fail("%s: file is %u bytes, want %u" % (path, len(raw), 44 + want_bytes))

    samples = struct.unpack("<%dh" % (frames * 800 * 2), raw[44:])
    left, right = samples[0::2], samples[1::2]
    if left != right:
        fail("%s: channels differ (the mixing model renders L == R)" % path)

    # Zero crossings of the square wave: 2 per cycle, 440 Hz -> 880 per
    # second, +-2 for the phase-accumulator's integer step.
    flips = sum(1 for i in range(1, len(left))
                if (left[i - 1] < 0) != (left[i] < 0))
    want_flips = frames * 800 * 440 // 48000 * 2
    if abs(flips - want_flips) > 2:
        fail("%s: %d zero crossings over %d samples, want %d +-2"
             % (path, flips, len(left), want_flips))
    return flips, len(left)


for path, frames in ((a_path, a_frames), (b_path, b_frames)):
    flips, samples = check(path, frames)
    print("  ok: %s — %d samples (%d frames), %d zero crossings"
          % (path.split("/")[-1], samples, frames, flips))

if failures:
    for msg in failures:
        print("test-audio-wav: FAILED — %s" % msg, file=sys.stderr)
    sys.exit(1)
PY
CHECKS=$((CHECKS + 1))
echo "  ok: header is PCM/48000/2ch/16-bit, lengths are frames x 800 samples"
CHECKS=$((CHECKS + 1))
echo "  ok: 440 Hz tone = 880 zero crossings per second (60 frames), 440 (30)"

echo "test-audio-wav: ok ($CHECKS checks, card 8.1)"
