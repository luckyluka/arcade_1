# Neon Tap: the design document's per-run enemy gates are unreachable in a golden session

Card: Neon Tap M1 (enemy grammar). Date: 2026-10-26.

## Premise

`programs/neon_tap/Neon Tap Design Document.md` §4 times the enemy
vocabulary per *run*: new kinds appear roughly 20 s and 45 s into a game,
with the last kinds reserved for the sector structure (M2/M3). M1 has no
sectors, so a naive implementation would gate kinds on `frame_t` (the
current game's timer) at the literal 20 s / 45 s marks (frames 1200 /
2700).

## Measurement

The golden session (`tests/inputs/neontap.txt`, scripted bot) plays game 1
from frame 30 to game over at `frame_t` ≈ 1570 (~26 s of play) — the bot
loses its third life around wall-clock frame 1676 and the session then
plays further short games. Consequences of the literal per-run gates,
observed with a 2400-frame probe run (`arcade1 run --vars ...`):

- the 45 s gate (`frame_t` 2700) is **never reached in game 1**: the
  splitters' and pinwheels' slot in the spawn table is dead weight for the
  first game;
- after a death `frame_t` resets to 0, so every game starts the ladder
  over — a player who dies early never meets the late kinds at all;
- the design's real intent ("the vocabulary widens as the session goes on")
  is a *session* property, not a per-game one.

## Change

Vocabulary unlock is driven by `unlock_t`, a counter of cumulative play
frames for the *session* (it does **not** reset on death — meeting an enemy
once keeps it unlocked for the run). The gate ladder is a compression of
the design's sector pacing into one session:

```
GATE = { 0, 600, 900, 0, 1200, 1600, 1700, 1800, 1950, 0, 2100 }   /* seek drift split mini pin turret shield bomber weaver wseg phantom */
```

`mini` and `wseg` have no gate (weight 0): they are spawned by splitters
and weavers only. A **force-intro** rule guarantees that the first enemy of
a newly unlocked kind is queued as soon as its gate opens and it has not
been seen this session (`kinds_seen` bit clear, nothing of that kind
already queued) — so even a player who dies constantly meets the whole
vocabulary within one session.

Two further adjustments came out of measurement, both documented here so
the golden numbers are traceable:

1. **Ladder softening.** The first ladder draft
   (`{0,600,900,0,1200,1350,1500,1650,1800,0,1950}`) opened the mid kinds
   so early that the bot was down to 1 life before the load-bearing
   charge+bomb window at frames 1000–1070 — the session lost its story.
   The shipped ladder pushes the mid/late gates ~200–350 frames later so
   game 1 keeps its shipped difficulty curve.
2. **Second charge+bomb cycle.** Even with all kinds spawning, the
   shield/turret/bomber/weaver/phantom *kills* plateaued: the vocabulary
   arrives after the only bomb of the session. The scripted session now
   charges again at frames 2200–2249 and bombs at 2260, sweeping the late
   kinds (measured: `kinds_killed` grows from 63 to 1663 across the
   extended session).

## Result (golden session, 2400 frames)

- `kinds_seen = 2047` — all 11 enum bits (the design's 9 kinds + mini +
  weaver segment), every one force-introduced after its gate;
- `kinds_killed = 1663` (bits 0–6, 9, 10) — everything the bot can catch
  dies; bombers out-bounce it, and a half-killed weaver chain is cleared
  by the next death rather than dying to its last segment (so the head's
  bit never sets in this session);
- `unlock_t` continues across the game-1 death (1570 → 2273 at frame
  2400), confirming the session-cumulative design.

The per-run gates of the design document will be replaced by the real
sector pacing once M2 (sectors) lands; the session-cumulative mechanism is
expected to survive as the cross-sector unlock.
