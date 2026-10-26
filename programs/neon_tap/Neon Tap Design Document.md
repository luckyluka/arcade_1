# Neon Tap: Design Document

*Arcade-1 single-game cabinet · vector arena shooter · 1–2 players*

## 1. Pitch

Neon Tap is a vector arena shooter where **how fast you physically tap decides how strong you are**. You steer a small ship with one joystick, and three buttons do everything else. Everything on screen is lines, polygons, glow and particles drawn by code, so the game needs **no artist and no sprite sheets**.

The cabinet is a single deep game, not a multigame box. Depth comes from enemy behaviour, risk-reward scoring, bosses, co-op and verified replays.

### Design pillars

1. **The buttons matter more, not more buttons.** Three buttons forever. Depth comes from timing, speed and risk.
2. **Readable at a glance.** One shape means one behaviour. A new player can read the screen after one minute.
3. **Casual players survive, fast players win.** The slow tiers keep a beginner alive. Tapping speed multiplies the score and clears swarms.
4. **Spectators can see skill.** A fast tapper’s ship looks different (thicker, whiter) and the grid pulses with their rhythm.
5. **Trust the score.** Deterministic play gives verified replays, ghosts and daily seeds.

## 2. Controls and the effort system

| Input | Function |
| --- | --- |
| Joystick | Move. The ship fires in the direction it last moved. |
| Button A and B | Fire buttons (hold or tap). |
| Alternate A and B | Charge (see below). |
| Button C | Release the charge as a shockwave bomb. |

### Three effort tiers (fire)

| Tier | How | Fire interval | Shots |
| --- | --- | --- | --- |
| Auto | No button held | 22 frames | 1 |
| Regular | Hold A or B | 9 frames | 1 |
| Tap | Tap one button repeatedly | 9 down to 3 frames | 1, then 2 at speed over 0.35, then 3 at speed over 0.70 |

Tap speed is measured from the last few intervals between presses (about 300 ms is speed 0, about 70 ms is speed 1) and decays after 250 ms of no taps. This is the `tapcad.h` library, unchanged.

### Charge and bomb

Alternating A and B within 400 ms fills a charge meter. **While charging, the gun is off for about 350 ms after each alternation.** This is the central risk: charging in a crowd gets you killed. Button C spends the charge on a shockwave. Radius grows with charge (about 90 to 390 pixels), and it destroys everything it touches.

The meter drains slowly if you stop for half a second, so you cannot bank a full charge safely for long.

## 3. The arena

Single fixed screen, landscape. Prototype is 800×450. Arcade-1 target is 480×270 (integer-scaled), fixed 60 Hz, additive-blended lines.

The grid brightens with the player’s tap speed. This is the main readability feedback and costs nothing in art.

## 4. Enemy grammar

Each shape is one rule. Players learn the vocabulary once. New enemies cost a few lines of code, never art.

| Enemy | Shape | Behaviour | HP | Counter | Appears |
| --- | --- | --- | --- | --- | --- |
| Seeker | Magenta diamond | Steers toward you, speed ramps with time | 1 | Any gun | Sector 1 |
| Drifter | Orange square | Straight line, bounces off walls | 1 | Predict the bounce | Sector 1, after 20 s |
| Splitter | Green hexagon | Chases slowly, splits into 3 small hexagons | 2 | Bomb or burst fire | Sector 1, after 45 s |
| Pinwheel | Purple spinner | Curving chase, hard to hit | 3 | Sustained fast fire | Sector 2 |
| Turret | Cyan triangle | Stationary, fires aimed shots every 2 s | 3 | Flank and burst | Sector 2 |
| Shield | Yellow ring around a seeker | Seeker is immune until the ring is hit twice | 3 | Spread shots | Sector 3 |
| Bomber | White cross | Drops mines that explode after 2 s | 2 | Kill early, avoid trail | Sector 3 |
| Weaver | Pink chain of segments | Snake movement, kill the head or all segments | 6 | Bomb it | Sector 4 |
| Phantom | Pale diamond | Flickers out and in, can only be hit when visible | 2 | Time bursts | Sector 5 |

**Swarm events:** every 25 seconds a ring of 14 seekers warps in with a 1-second warning. This is where tap speed and bombs earn their place.

## 5. Run structure

A full credit is five sectors, roughly 12 to 14 minutes if you do not die. Each sector has a rule change to the arena and ends in a boss.

| Sector | Name | Arena | New rule | Boss |
| --- | --- | --- | --- | --- |
| 1 | Grid | Open rectangle | None. Teaches tiers. | **Sentinel**: rotating polygon with four shield plates. Break the plates, then the core. |
| 2 | Pylons | Rectangle with static pylons | Pylons block shots and movement | **Hydra**: splits into three smaller bosses at 66% and 33% health. |
| 3 | Ring | Circular arena | Enemies spawn from rotating gates | **Orbiter**: a core with a ring of satellites that fire inward. |
| 4 | Crush | Walls close in and open on a beat | You must move with the rhythm | **Weaver**: a long segmented boss that fills the arena. |
| 5 | Core | Open, grid goes dark | Phantoms and full vocabulary | **Core**: three phases, uses every enemy type. |

Target pacing per sector is 90 to 110 seconds of waves plus 45 to 60 seconds of boss. Boss kill time with fast tapping is the tuned target (see section 11).

## 6. Scoring

Scoring is the long-term skill ceiling. Beginners can survive, but experts compete for score.

- **Chain multiplier:** each kill adds 0.25 to the multiplier (cap ×10). It resets after 1.8 seconds without a kill, or when you are hit.
- **Overdrive:** holding tap speed above 0.8 for 3 seconds doubles the multiplier gain and the ship turns white. It ends the moment speed drops below 0.5.
- **Clean sweep:** a bomb that kills 8 or more enemies awards a flat bonus and refunds a quarter of the charge.
- **No-damage sector bonus:** finishing a sector without being hit.
- **Extra lives** at 50,000 and 200,000 points. Three lives at start.

The design intent is that low tiers survive and high tiers score. Fast tapping builds the chain, and the bomb is a gamble about when to spend a stored charge.

## 7. Two-player co-op

Both players share the arena, the lives pool and the score multiplier.

- **Shared lives:** one pool of lives. A downed player respawns after 4 seconds if their partner is alive.
- **Sync bomb:** if both players release within 300 ms of each other, the bomb is centred between the ships with radius ×1.5.
- **Covering:** one player can charge while the other keeps the gun going. Alternating A and B becomes a team decision.
- **Friendly fire:** off. Bombs never harm players.

## 8. Presentation without an artist

- **Rendering:** all lines, additive blending, a two-pass glow (wide faint stroke plus thin bright stroke).
- **Palette per sector:** Grid blue, Pylons violet, Ring teal, Crush red, Core white on black.
- **Juice list:** screen shake on kills and hits, particle bursts of line fragments, grid pulse tied to tap speed, flash on bombs, brief slow-down on boss death.
- **Audio (also generated):** simple synthesised blips from integer oscillators, one tone per enemy type, and a music loop built in layers. A bass layer plays at the auto tier, a drum layer at the regular tier and a lead layer at tap speed. The music literally follows your effort.

## 9. Verified replays and online features

The engine is deterministic, so the full game state follows from the seed and the input log.

- **Replay format:** seed, build hash, and a per-frame input byte stream (4 direction bits and 3 button bits). Run-length encoded it is a few kilobytes.
- **Verification:** the server replays the input log and recomputes the score. A mismatch rejects the submission.
- **Ghosts:** the best run (or a friend’s) plays back as a faint outline ship you race against.
- **Daily seed:** one fixed seed per day with its own leaderboard.
- **Checksums:** the game hashes its state at the end of each sector so desyncs are found early.

## 10. Technical plan for Arcade-1

- **Language and rules:** C99, integer only (Q8 fixed point), no libm, no malloc, seeded LCG, frame counter as the only clock. Same constraints as the Sky Descent core.
- **Libraries:** `tapcad.h` unchanged. Sine table and 3×5 pixel font come from generated `tables.inc`.
- **Renderer:** Bresenham lines with additive blend into an XRGB8888 framebuffer. Glow is a second faint pass with the line two pixels thick.
- **Entity budgets:** 128 enemies, 256 bullets, 512 line particles, 8 rings. Fixed arrays.
- **Structure:** `neon.h/.c` (core), `libretro.c` (wrapper), `neon_test.c` (headless tests), `Makefile`.
- **Input timing:** libretro polls once per frame, so tap timing has 16.7 ms resolution in the emulated build. On the real cabinet, feed FPGA timestamps to `tc_press()`.

## 11. Test plan and balance targets

Tests run headless and must pass before every milestone.

1. **Determinism:** two runs with the same inputs produce the same frame hash.
2. **Tier ordering:** scripted bots at auto, hold and tap must score in that order, and the tap bot must survive at least 1.5 times longer than the auto bot.
3. **Boss time targets:** with fast tapping, each boss dies in 45 to 60 seconds. With hold only, 90 to 120 seconds. With auto only, the boss must be survivable but slow.
4. **Full-run bot:** a scripted bot with a good strategy reaches sector 3, and one with a perfect strategy reaches the end. A random bot does not.
5. **Entity budget:** a stress test spawns the worst-case swarm plus boss and checks no array overflows.
6. **Replay round-trip:** a recorded run replays to the same score.

## 12. Milestones

| Milestone | Deliverable |
| --- | --- |
| M0 | Port the browser prototype to C: line renderer, player, tap tiers, charge and bomb. |
| M1 | Enemy grammar: all nine enemies with tests. |
| M2 | Sector 1 complete with the Sentinel boss and scoring. |
| M3 | Sectors 2 to 5 with arena rule changes and bosses. |
| M4 | Two-player co-op with the sync bomb. |
| M5 | Replay recording, verification tool and ghost playback. |
| M6 | Generated audio with the effort-driven music layers. |
| M7 | Balance pass, cabinet playtest and switch durability test. |

## 13. Risks

| Risk | Mitigation |
| --- | --- |
| Tapping fatigue and button wear | Auto and hold tiers keep casual play comfortable. Use arcade-grade switches. Test with a 15-minute session. |
| Looks flat without sound | Audio plan in section 8. Do not skip it. |
| Glow readability on a real cabinet monitor | Test early on the target display. Keep line widths at 2 pixels or more. |
| It feels like mashing | The charge decision and the scoring chain are the skill layer. Watch playtests for players who only mash. |
| Scope | Cut order: Crush sector, Phantom enemy, online features. |

## 14. Playtest plan

Before building past M2, put the prototype or the M2 build in front of 10 people for 5 minutes each. Record these three things.

1. Do they ask for another run without being prompted? (pass: at least 6 of 10)
2. Do two people start competing or ask to play together? (pass: at least 2 pairs)
3. Do people watching wander over? (pass: at least 3 of 10)

If the first fails, the mechanic is a feature and not a cabinet, and the plan should change before more is built.

## 15. Open questions

- Final arena resolution on the target monitor (480×270 or 384×216).
- Whether co-op should share lives or have separate lives.
- Whether the cabinet needs a stick that supports 8 or 4 directions. The prototype uses 8.
- Audio hardware path in the FPGA, and how much synthesis to do in software.
