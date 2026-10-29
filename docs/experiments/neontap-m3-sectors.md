# Neon Tap: watching a whole five-sector credit

The binding constraint is the design's own extra-life economy, not a longer
waves clock.

Card: Neon Tap M3 (Sectors 2–5 + the Hydra, Orbiter, Weaver and Core).
Date: 2026-10-29.

## Premise

`programs/neon_tap/Neon Tap Design Document.md` §5 paces a credit as five
sectors, each "90–110 s of waves plus a 45–60 s boss". M2 already found
that clock ~18× too long for a golden session
([neontap-m2-sector-1.md](neontap-m2-sector-1.md)) and shipped Sector 1
compressed to `SEC_FRAMES = 1100` waves frames plus a Sentinel that dies
under fire in ~4 s. M3 must add Sectors 2–5, their arena rules and their
four bosses, and — per the card and design §11.4 — the regression should
*watch* the whole thing, since "a scripted bot with a good strategy reaches
sector 3, and one with a perfect strategy reaches the end".

## Change

**Pacing.** Per-sector waves caps `SECW = {1100, 400, 320, 260, 200}` and a
**kill quota** `SECQ = {0, 4, 3, 3, 2}` — the waves end on the *quota or the
clock, whichever comes first*, so a player who is winning reaches the boss
sooner. Sector 1 keeps the pure clock (`SECQ[0] = 0`) so the M1/M2 story is
untouched. The first draft (`{1100,460,400,340,280}` / `{0,5,5,4,4}`) left
the bot dying in Sector 2's waves before the Hydra ever spawned; the quotas
had to be small because the golden bot kills only ~29 enemies per 2000
frames.

**Bosses.** One dispatcher keyed on `bo_k`, sharing one part pool `BP[9]`:
the Sentinel keeps the M2 grammar byte-for-byte (guarded `bo_k == 1`); the
Hydra is one 9-hit body that **splits into three** at ≤6 and ≤3 hits left
(the split preserves hits left, so the boss gets smaller and more numerous
without gaining health); the Orbiter a 6-hit core with four 2-hit satellites
firing inward; the Weaver a 6-hit head pulling eight 1-hit segments; the
Core a 12-hit fortress in **three phases** that each summon the vocabulary
and that start with a **90-frame shield soak** (the phase banner's beat —
without it the bot melts phases 2 and 3 in ~4 frames each and nobody, human
or scripted, ever sees them).

**Arenas.** `arena` 2 pylons (block shots and the ship, counted in
`pyl_hits`), 3 ring (radius 112, waves arrive through rotating gates,
counted in `gates`), 4 crush (walls inset to 90 px on a 120-frame beat,
`wall_in`), 5 dark (the grid never lights). Sectors 1–2 keep their plain
screen clamp, so the M1/M2 movement and bullet behaviour is exact.

**The bomb versus bosses** (design §5's weaver row says "Bomb it"): the
shockwave now sweeps **every boss part it crosses, once each**, and the
Weaver boss keeps the chain grammar — kill the head *or* the whole chain.
A boss also no longer warps in on top of a wave-phase ring still sweeping
the room (the arena clear now clears the rings too; without this the Hydra
and Weaver bosses spawned into a live ring and died in 4 frames).

**Grace window.** `boss_kill` grants `inv = 120`. The design's juice is "a
slow-down when a boss dies"; this machine has one frame clock and no way to
stretch it, so the beat ships as a two-second grace while the next sector
assembles.

## Findings

**1. The bot's credit, not the waves clock, is the binding constraint.**
The scripted bot (`tests/inputs/neontap.txt`) loses its third life at frame
860 and its *last* life at ~2490 — a ~2450-frame credit. A five-sector loop
needs the run to survive to ~3450. Nothing about the waves clock fixes that;
what carries the run is the design's **own economy**: the 50 000 extra-life
mark lands at frame 2490 (score 71 260 after the Weaver), refunding the
fourth life exactly when the credit would have ended. Two mechanisms from
the document, not two new ones.

**2. Bombers had no runway under sector pacing — the Core gave them one.**
A bomber drops its first mine 60 frames after hatching. Under M2's single
long Sector 1 one survived; under sector pacing the arena clears every few
hundred frames and every wave-phase bomber died first (measured: 0 mines in
4000 frames). Lowering the timer to 10 frames produced exactly 2 mines, so
bombers were alive 10–30 frames and no longer. Rather than weaken the
`mines_dropped` assertion, the Core — which the design says "uses every
enemy type" — now summons a bomber with each phase, and the fight itself
(760 frames, with the soak beats) is long enough: **4 mines at the shipped
1 s timing**, no gameplay change needed.

**3. A boss that is always behind you is unhittable by this bot.** The
Weaver boss shipped at 700 Q8 (2.7 px/frame) with ±3° steering — faster
than the waves' own weaver (640, ±2°) — and the circling bot, which can
only fire along its last heading, landed **zero hits in 80 frames** and was
rammed to death. At 560/±2 it dies properly. Design §11.4's "good strategy"
bot cannot chase-and-aim; a boss glued to its tail is a wall, not a fight.

**4. `loop` must survive death.** "Credits completed this session" was being
cleared by `reset_game`, so the HUD's L counter and the observable lied the
moment the player died. Now session-cumulative, like the vocabulary gates.

## Measurement (golden session, 4000 frames)

| frame | event |
|---|---|
| 30 | play starts |
| 520, 860 | lives 3→2→1 (the M2 story, unchanged) |
| 1130–1360 | **Sentinel** (arena 1): 20 hits, dies in 230 frames |
| 1360 | sector 2, pylons (`pyl_hits` → 39 over the session) |
| 1580–1640 | **Hydra** (arena 2): hp 9 → 5 with `boss_n` 3 (split) → 3 with `boss_n` 3 (split) → dies |
| 1640 | sector 3, ring (`gates` → 7) |
| 1900–2290 | **Orbiter** (arena 3): 4 satellites, dies in 390 frames |
| 2290 | sector 4, crush (`wall_in` reaches its full 90 px) |
| 2420–2490 | **Weaver** (arena 4): 8 segments; the bomb sweeps the chain |
| 2490 | sector 5, dark; score 71 260 → **`extras` 1** (the 50 000 mark) |
| 2690–3450 | **Core** (arena 5): phases 1→2→3 (`boss_ph`), soak beats, `mines_dropped` 4 |
| 3450 | sector 1, **`loop` 1** — the credit completed |
| 3660 | credit 2 ends (105 480); credit 3 starts 3710 |

Final pins: `kinds_seen = kinds_killed = 2047` (every kind appears *and* is
killed across the two credits — the weaver chain that outlived M2's arena
clear is now hunted down), `sweeps = 1`, `nd = 3`, `boss_kills = 5`,
`mines_dropped = 4`, `eh_fired = 47`. Heaviest sampled frame 309 737
instructions (the Hydra mid-split), against the 500 000 budget.

## Consequences / not done

- **Boss clocks stay compressed** as in M2: our bosses die in 40–760 frames
  where §11.3 wants 2700–3600. The mechanism (waves stop, boss fights, boss
  pays) ships; the clock does not.
- A well-placed bomb can now end the Hydra outright after its second split
  (each body has ≤1 hit left and the sweep strikes every part once). That is
  "Bomb it" applied consistently; it is a legal, if abrupt, strategy.
- The bot's *second* credit never re-enters Sector 2 inside the session
  (game over at 3660), so the run-loop is observed once, not twice.
- Design §11.5's entity-budget stress test is still covered only by the ten
  budget samples in `tests/test_neontap.sh`, which now span the Hydra split,
  the crush walls and the Core's phase-3 vocabulary.
