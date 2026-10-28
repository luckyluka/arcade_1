# Neon Tap: the design document's per-sector pacing cannot fit a golden session

Card: Neon Tap M2 (Sector 1 + the Sentinel + scoring). Date: 2026-10-28.

## Premise

`programs/neon_tap/Neon Tap Design Document.md` §5 paces a credit as five
sectors of **90–110 s of waves + a 45–60 s boss** each — "12 to 14 minutes
if you do not die". M2 must ship Sector 1 (Grid) with the Sentinel and the
§6 scoring rules, and it must be *observable* in the regression: the
golden session (`tests/inputs/neontap.txt`) is 2400 frames = **40 s of
wall clock**, of which game 1 runs frames 30–2094 (~34 s).

The literal pacing is therefore unreachable by a factor of ~18: sector 1's
waves alone (90–110 s) consume the whole session, and the Sentinel would
never appear. This is the same class of finding as the M1 vocabulary
gates ([neontap-vocabulary-gates.md](neontap-vocabulary-gates.md)): the
design's *mechanism* ships, the design's *clock* is compressed.

## Change

Sector 1's waves phase is `SEC_FRAMES` = **1100 play frames** (~18.3 s),
then the Sentinel warps in. Each later sector shaves 120 frames off the
waves (floor 700), so a five-sector loop tightens as it goes. The
Sentinel: a rotating polygon with **four 3-hit shield plates** and an
**8-hit core** (20 hits total) — hits land on the nearest intact plate
until all four are gone, then on the core; a bomb cracks it once per
expansion. While it is out, the arena clears (everything still alive
bursts, unscored), waves and the swarm stop, and it drifts slowly while
firing two aimed shots every 2 s. Killing it pays the chain-multiplied
boss score, awards the **no-damage sector bonus** if the player was never
hit this sector, and advances the run (`sector` 1→5, then `loop`++).

Scoring (§6) ships as: **overdrive** (180 frames above 0.8 tap speed →
double chain gain + white ship, ends the moment speed drops below 0.5),
**clean sweep** (a bomb ring that scores ≥ 8 kills pays a flat 2000 and
refunds a quarter of the spent charge), the no-damage bonus (flat 5000),
and **extra lives** at 50 000 / 200 000 points.

## Measurement (golden session, 2400 frames)

- play starts at frame 30; the Sentinel spawns at frame **1130** (exactly
  1100 play frames of waves) and dies at frame **1358** — a **228-frame
  (3.8 s)** fight under the bot's sustained fire; `sector → 2` at 1358;
- the fight reads as designed: `boss_hp` steps 20 → 19 → … with
  `boss_plates` 4 → 3 (1158) → 2 (1170) → 1 (1179) → 0 (1187), i.e. the
  plates break in about the first quarter and the exposed core takes the
  rest;
- **overdrive** latches at frame 215 (the bot taps every 4 frames, speed
  256) and again at 2327 after the restart;
- the first bomb's ring scores its kills by frame **1093** and is a
  **clean sweep** (`sweeps → 1`);
- game 1 ends at 2094 with `score = 19910` — so **`extras` stays 0**: the
  design's 50 000 extra-life mark is ~2.5× past anything this session
  reaches;
- **`nd` stays 0**: the bot is hit at frames 518 and 860, so the
  no-damage bonus never fires in this session. The mechanism ships and is
  covered by the sector-advance path, but the bonus itself is not
  observable under this bot;
- the Sentinel's arena-clear changed which enemies die with credit:
  `kinds_killed` moves 1663 → **1279** (bits 0–7, 10). The bomber now
  dies (a bomb sweeps it before the boss arrives), and the weaver chain
  no longer does — a chain alive when the Sentinel warps in is destroyed
  unscored.

### Why the boss is 20 hits, not 500

§11.3 asks for a boss that takes **45–60 s with fast tapping**. Measured
on this session: the 20 hits land between frames 1149 and 1358 — a
concentrated burst of ~18 hits in 60 frames (~3 frames per hit) while the
bot's stream is on the core, then two more as the aim wanders. At that
burst rate a 45–60 s boss needs **several hundred hits**, which reads as
a punching bag at any human tap rate and would swallow the session whole.
The shipped value keeps the *grammar* of the fight (plates break first,
then the core, then the run advances) visible in 3.8 s of sustained fire;
the design's time target belongs to a full-length credit with human,
non-sustained tapping, which the 40 s golden session cannot display.
Tier-ordered boss timing (§11.3) is left to the balance pass (M7) once
more than one tier of bot exists.

## Result

`make test-neontap` passes with the M2 story assertions (overdrive
latched, a clean sweep paid, the Sentinel seen at 4 plates, plates
breaking, the run reaching Sector 2); the heaviest sampled frame is
282 252 instructions of the 1 000 000 cap, boss frames included. The
neontap golden triple is re-locked in this commit — the only golden that
moves — because the game's behaviour deliberately changed.
