# Neon Tap: the aim table was mirrored (A8)

`programs/neontap.c` 2026-11-04: the 8-way aim table shipped with its east
and west swapped, so the nose and the shots pointed opposite to the pad's
horizontal component. Fixed in the same commit as the regression probe
(`test_neontap.sh` §3d), the retuned scripted session, and the deliberate
re-lock of the neontap golden triple.

## The bug

`aim = A8[(my + 1) * 3 + (mx + 1)]` (line 805), rows = up/none/down,
columns = left/none/right, then the nose is drawn at `(COS(aim), SIN(aim))`
and **bullets fly along aim** (line 818: `vx = COS(a) * 6`). The machine's
trig is 0 = east, 64 = south, 128 = west, 192 = north.

| entry | shipped | correct | |
|---|---|---|---|
| up-left  | 224 (NE) | 160 (NW) | mirrored |
| up       | 192 (N)  | 192 (N)  | ok |
| up-right | 160 (NW) | 224 (NE) | mirrored |
| left     | 0 (E)    | 128 (W)  | mirrored |
| right    | 128 (W)  | 0 (E)    | mirrored |
| down-left| 32 (SE)  | 96 (SW)  | mirrored |
| down     | 64 (S)   | 64 (S)   | ok |
| down-right| 96 (SW) | 32 (SE)  | mirrored |

Six of the nine entries are the correct angle under `a -> (128 - a) mod 256`
— an east/west reflection. The middle column (up / none / down) was already
right, which is what made the table look plausible: pure up/down aimed
correctly, and every direction *with* a horizontal component fired at its
mirror. The table was authored as if 0 meant west; `COS(0)` is east.

## Why it took a session to notice

Aim follows the last movement and the shots fly along aim, so a scripted
session is an implicit statement about the aim convention. The shipped
session's fight arcs were authored on the mirrored build, where moving
*away* from a boss horizontally fired *at* it. On the fixed build the same
arcs fire away, and the run stops dead:

| run | result at f3999 |
|---|---|
| original session, original ROM | 5 bosses, loop at f3442, `kinds_killed=2047`, `gh_on=1`, `gst_score=105480`, `sweeps=1` |
| original session, fixed ROM (geo0) | stuck in Sector 1: Sentinel out since f1130 still at 12/20 hp, `boss_kills=0`, `loop=0`, score frozen at 11960 |

So the fix invalidates the session, the session invalidates the golden: the
A8 fix, `tests/inputs/neontap.txt` and the golden triple land in one commit,
and §3d pins the convention for every future change.

## The regression probe (§3d)

`test_neontap.sh` generates a 360-frame input that holds each cardinal
direction in turn and checks **both halves** of the contract per direction:

- `aim` equals the machine angle (right 0, left 128, up 192, down 64), and
- the ship actually displaced that way (>= 2000 units along the axis).

Either half alone is insufficient: a sliding ship with a stale aim, or a
turning nose with no movement, would each pass a one-sided check.

## The retune (20+ measured iterations)

Precise open-loop aim windows never converged (iterations 1-8: every edit
shifts the downstream timing the next window was measured against).
Geometry-agnostic wobble/box/diamond controllers (9-18) killed the early
bosses but either missed the 19 px damage circle (`boss_strike` scores only
inside `R=19` around the boss centre) or drove the ship into a wall where the
homing Core pinned it (geo17: cornered at (10,10), Core 12/12 untouched).

The controller that converged (iteration 19): a **tight circle** of short
diagonal legs from f1080, so the ship orbits the arena centre in open space
and one leg of each orbit sweeps the boss point (240,90) with the gun, while
the bombs (aim-independent rings) carry the boss damage and the wave kills.
Cadence: A every 4 frames, A/B bursts at `f%128 in {2,6,10,14,18}` charge
25600, START at `f%128==48` spends it (`BOMB_BTN` is START, not A/B).

Three late conflicts had to be measured out of the way:

1. **The phantom (kind 10).** It is force-intro'd at the first frame after
   `unlock_t >= 2100`: queued f2483, spawned f2528, and the sector-4 boss
   spawn at f2566 clears the arena with it still unscored. The cadence bombs
   at f2480 / f2608 miss the 38-frame window between spawn and clear;
   `kinds_killed` stuck at 1023.
   Fix: inside block `[2432, 2560)` only, a second charge burst
   (`f%128 in {54,58,62,66,70}`) plus a bomb at `f%128==112` (f2544). The
   ring lives 34 frames and reaches R=234 in ~10, the phantom flickers
   visible 15/15 (`ph += 20` per frame), so a visible phase is always
   overlapped: killed at f2545, one frame after the bomb, no life lost.

2. **Death vs the bombs.** `gst_score` is the best run's score and the ghost
   needs `gh_on`, so credit 1 must die (state 2) and be retried (an A/B/START
   edge after `over_t > 45`; `reset_game` re-arms on every edge, there is no
   upper bound). But the bomb ring that earns the run also protects a parked
   ship: with the 128-frame cadence kept, the ring killed every arrival
   before it reached the ship (seekers cross ~130 px per 128 frames, the
   full-charge ring reaches R=234 in ~10), and credit 1 survived all 755
   wind-down frames at lives=2
   (geo21). Death needs a bombless window — and the park aim needed fixing
   too: aim=96 (SW) at the park point made the passive gun sweep the ship's
   whole left strip and kill every close spawn, pushing first hit to L+363
   and death to L+497 (geo22). Re-aiming north for the first 16 frames
   (`aim=192`, bullets exit the top wall, the gun goes neutral) brought death
   to f3608 (geo23).

3. **The sweep vs the death.** A clean sweep needs **one bomb ring to kill
   >= 8** (`R[i].n >= 8`, line 933). Credit 2 cannot provide that: `frame_t`
   resets with the credit, so `d = frame_t*256/9000` starts at 0 and a fresh
   credit spawns **1 enemy per 60 frames** — three bombs on credit 2 took
   1-4 kills each, `n_en` never exceeded 2 (geo24). The sweep must be bought
   on credit 1's dense pile (d~96: 1-3 spawns per 45 frames plus marker
   bursts; the parked wave grows to 14). But wiping the pile before it lands
   its second contact delays death: bomb at f3440 (n_en=14) swept at f3473
   yet death slipped to f3821, credit 2 at f3872, ghost frames 128 < 200
   (geo25). Moving the bomb to **f3460 — one frame after the natural first
   hit (f3459, lives 2->1)** — banks the hit, lets the pile (13) pay the
   sweep at f3493, and the survivors land the death at f3653; credit 2 starts
   at f3704 with 296 ghost frames (geo26, shipped).

## Measured (shipped session, fixed build)

| milestone | frame |
|---|---|
| title / start | f0 / f30 |
| orbit begins (SPAN_START) | f1080 |
| Sentinel / Hydra / Orbiter | f1130-1367 / f1606-1718 / f1845-2481 |
| sector-4 boss / Core | f2566-2568 / f2738-3244 |
| extra life (50000) | f2568 |
| loop (sector 1, loop=1, lives=2) | f3244 |
| wind-down re-aim / first hit | f3245 / f3459 |
| sweep bomb / sweep | f3460 / f3493 |
| credit 1 dies / credit 2 (ghost) starts | f3653 / f3704 |
| final | `sweeps=1`, `gh_on=1`, `gst_score=100050`, `kinds_killed=2047`, `boss_kills=5`, `extras=1`, ghost frames 296 |

Budget: heaviest sampled frame 293928 instructions (cap 1000000, budget
500000). `test-neontap` runs in ~2 m of the check.

## Findings

1. **A plausible table can be wrong in 6 of 9 entries.** The two correct
   entries (up/down) are exactly the ones a quick eyeball checks first. The
   probe now pins all four cardinal directions with both the angle and the
   displacement.
2. **A scripted session encodes the aim convention.** With aim coupled to
   movement, "fix the game" and "re-author the session" are one change; the
   golden triple (ROM, symbols, hashes) plus the input re-lock together, in
   the commit that says why.
3. **Open-loop aim timing does not converge** when every edit shifts what
   comes after it; the stable controller is geometric (orbit through the
   target), not temporal (fire during a window).
4. **Session-cumulative vs credit-local state** is the shape of every late
   assertion: `sweeps`/`nd`/`boss_*` survive a death, `score`/`lives`/
   `charge` do not, `gst_score` is the best run — which forces the death that
   `gh_on` then replays.
5. **The kill ring and the death want opposite things**, and so do the sweep
   (dense pile) and the death (arrivals must land). Both were resolved by
   *timing one bomb*: none during the death window, one at the frame where
   the pile has just done its other job.
6. **A fresh credit is nearly defenseless against nothing**: 1 spawn/60
   frames means post-credit-2 content must be planned from `frame_t`, not
   from the wave's appearance.

## Consequences / not done

- `programs/lumen/lumen.c:267` carries the **identical mirrored table**
  (user's file — flagged here, not touched).
- Pre-loop sweep windows were measured infeasible and abandoned: the
  moving ship spreads the wave, so the cadence bombs at f688/f816/f944 took
  1, 0 and 2 kills respectively; the only dense pre-loop windows (n_en >= 12,
  peak 14 at f975) do not coincide with a charge ready to spend.
- The shipped input is coupled to this exact build: any future A8, spawn,
  boss-timing or wind-down change re-measures the two-pass retune
  (pass A measures the loop frame L and the death frame; pass B opens at
  them — same script, same run, by determinism).
