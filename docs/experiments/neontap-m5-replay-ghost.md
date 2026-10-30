# Neon Tap: the replay is a guest buffer and a checksum

The ghost is the round-trip made visible.

Card: Neon Tap M5 (replay recording, verification, ghost playback).
Date: 2026-10-30.

## Premise

`programs/neon_tap/Neon Tap Design Document.md` §9 asks for verified replays
and ghosts: a replay is "seed, build hash, and a per-frame input byte stream
(4 direction bits and 3 button bits)", verification is "the server replays
the input log and recomputes the score", and ghosts are "the best run plays
back as a faint outline ship you race against". §11.6 makes the round-trip a
test: "a recorded run replays to the same score".

There is no server and no scoreboard on this machine — the CLI is the only
host, and it can already record a session to an input script and replay it
bit-identically (the M0 regression). What M5 had to add is the *guest's own*
side of the contract: the stream in the format the design names, a
verification hook that does not need the host, and a ghost that is genuinely
a replay rather than a decoration.

## Change

**The stream.** Every frame of a credit packs into one byte — the design's
own layout: bits 0..3 = up/down/left/right, 4 = A, 5 = B, 6 = the bomb
(START) — and lands in `REC[4096]` (68 s of credit). `rec_n` is the length,
`rec_crc = rec_crc * 33 + b` a rolling checksum (unsigned: it wraps by
definition). The design says "run-length encoded it is a few kilobytes";
RLE was left out — the raw stream is 4 KiB and RAM had room (below).

**The ghost.** `ship_step()` is the ship's motion, extracted from `step_play`
and shared: a pure function of the pad history and the arena clock. On a new
credit the best run so far (`score > gst_score`, at least 240 frames) copies
to `GST[4096]` and replays through `ship_step` as an outline ship — no
shots, no hits, no score. `gst_score` carries the score it ended with,
`gh_on` says it is flying.

**The arena clock is shared, not recorded.** The ghost flies the recorded
*inputs* through the *live* arena (pylons, ring, crush walls) — the room it
races in is your room, not a recording of yours.

## Findings

**1. The golden already verifies the stream; `rec_crc` just makes it
readable.** `tests/state_hash.h` hashes *all RAM bytes* plus the registers,
so `tests/golden/neontap.txt` has been pinning `REC[]` and `GST[]`
byte-for-byte since the buffers existed — the design's "replay the input log
and recompute" is the machine's determinism contract, which M5 did not need
to build, only to expose. That is why the M5 diff re-locks all 4000 golden
lines while the *picture* (`fb=`) is unchanged for the first 3704 frames:
`hash=` covers the registers and the recording buffer, `fb=` covers what you
see. The two M5 observables are the design's verification hook pointed
inward.

**2. A replayed run recomputes the same score — and the same checksum.** The
record->replay test now asserts it the way §11.6 words it: the CLI replays
the recorded script and the footer values of `rec_n`, `rec_crc` and `score`
must be identical to the run that produced them. They are.

**3. The ghost is a true round-trip, measurable from outside.** Because
`ship_step` is pure, the ghost's k-th play frame must land on the player's
k-th play frame from the recorded credit. It does — 296 frames, zero
mismatches — and the test asserts exactly that (the trace must be sampled
every frame: at `--var-every 10` the two sequences are not aligned by play
index). Note the CLI appends a `var NAME = VALUE` footer and a `ran ...`
line after the trace; an awk that reads every line picks up 7 phantom ghost
frames from it and reports a divergence at the session's last play frame.

**4. The ghost costs nothing measurable.** Its step runs only while it flies
(credit 2's last 296 frames here). The session's heaviest sampled frame is
unchanged from M3 — the Hydra split at 310 998 instructions, against the
500 000 budget; the ghost-window sample (3900) is 279 824. RAM: BSS
11 784 → 20 020 bytes for two 4 KiB buffers, leaving 44 KiB of stack
headroom.

**5. The recording outlives the credit it records, by design.** `rec_n`
resets on `reset_game`, so each credit is its own stream — the design's
"replay" is a *run*, and a run ends when the credit does. The ghost is armed
from the *previous* credit's stream, which is why the second credit is where
`gh_on` first appears (frame 3704, the moment the new credit starts).

## Measurement (golden session, 4000 frames)

| frame | event |
|---|---|
| 30 | play starts; `rec_n` counts up from 0 |
| 3660 | credit 1 ends: `rec_n` 3625, `rec_crc` 209 387 501, score 105 480 |
| 3704 | new credit: `rec_n` 0, `gst_score` 105 480, **`gh_on` 1** |
| 3704–3999 | the ghost flies the recorded 296 frames, frame-exact |
| 3999 | `rec_n` 295 (credit 2 still recording), `rec_crc` 254 151 995 |

Golden picture (`fb=`) is bit-identical to M3's for frames 0–3704 — at 3704
the ghost still sits exactly under the ship it was recorded from — and
differs on the 295 frames where the two have separated (3705–3999); `hash=`
differs on all 4000 (it covers the registers and the new buffers). Heaviest
sampled frame 310 998.

## Consequences / not done

- **No host-side verification tool.** The design's "verification tool" is
  the CLI's `--record`/`--input` pair plus the `rec_crc`/`score` footer
  comparison the test makes; a standalone `arcade1 verify` subcommand would
  be new host tooling, not needed by anything yet.
- **No RLE.** The design notes the stream is "a few kilobytes" run-length
  encoded; the raw 4 KiB buffer fits and RLE would buy nothing the tools
  can observe.
- **No seed / build hash in the stream.** The machine has no wall clock and
  the ROM is the build; a daily-seed scheme stays host-side (FUTURE_IDEAS
  §9).
- **The ghost races your room, not its own.** A ghost recorded in the pylon
  arena is pushed by the *live* pylons. Correct for a race, wrong as an
  archive; the design says "you race against", so this is the shipped
  behaviour.
- **The second credit is short in the golden** (296 frames before the
  session ends), so the ghost's flight is observed but not stress-tested
  across a full sector loop.
