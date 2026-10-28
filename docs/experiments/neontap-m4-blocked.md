# Neon Tap: two-player co-op and the daily seed are blocked by the machine contract

Card: Neon Tap M2 (scope triage for M4 and the online features).
Date: 2026-10-28.

## Premise

`programs/neon_tap/Neon Tap Design Document.md` §7 asks for **two-player
co-op** (shared lives, a sync bomb within 300 ms, covering fire) and §9
for a **daily seed** — "one fixed seed per day with its own leaderboard".
The design's pillars lean on both: "Casual players survive, fast players
win" plus "Trust the score. Deterministic play gives verified replays,
ghosts and daily seeds". The milestone table puts co-op at M4.

## Measurement

Neither feature has a wire to reach it on this machine. `arcade1 info`
and `SPEC.md` agree, and the CLI is the authority:

1. **One pad, one player.** `SPEC.md` §7 documents exactly one input
   register, `INPUT_PAD` at `0x200`, read-only, 8 bits (up, down, left,
   right, A, B, start, select). There is no second pad register, no
   player index, no way for a guest to ask "whose button is this". Two
   players would need two `arcade1_input_t` streams into one guest, and
   the machine exposes one. Everything §7 wants — a second ship, a
   second tap-cadence meter, the 300 ms sync window between two humans
   — needs input the machine cannot carry.

2. **No wall clock, and that is the point.** `SPEC.md` §4 gives
   `TIMER_COUNT` (`0x00`): a tick count that "advances exactly once per
   completed `arcade1_run_frame()`", read-only, power-on 0. It counts
   *frames of this run*, not time of day: it resets with the machine,
   runs at whatever rate the host drives it, and is identical for every
   replay of the same inputs. There is no RTC, no guest-visible storage
   (RAM is volatile — `src/arcade1/memory.c`), and no network. A "seed
   for today" would have to come from outside the deterministic contract
   — which is exactly what determinism forbids the machine to do. Every
   golden hash in `tests/golden/` is a promise that the same inputs give
   the same frames; a daily clock read would break that promise for
   every game that used it.

## Consequence

M4 (co-op) and the daily-seed/leaderboard half of §9 are **not
buildable as specified** on ARCADE-1 and are not scheduled. What the
design wanted from them has honest halves that do fit:

- **Verified replays and ghosts** (the other half of §9) are pure
  determinism and need no new hardware: `arcade1 run --record` already
  captures a session as an input script, and `tests/test_neontap.sh`
  replays one bit-identically. A ghost is that same script played back
  alongside the live player, drawn dimmer. This is M5 and it is
  reachable — the machine contract is *why* it works.
- **A "daily seed" becomes a host-side affair**: a tool outside the
  machine picks the day's `rng_state` and writes it into the input
  script (or a `--seed` the host passes at load). The guest still sees
  only determinism. If a real need shows up, that is the shape to
  promote into a card — see `FUTURE_IDEAS.md`.

Co-op's *design intent* — covering fire, a shared risk pool — has no
single-player substitute worth building; it is simply out of scope for
this cabinet, and the design document stays as the record of what was
asked for.

## Result

No code changed for this finding. M2 ships Sector 1, the Sentinel and
the §6 scoring rules; M3 (sectors 2–5), M5 (replays/ghosts) and M6
(music) remain, and are all within the machine contract.
