# BRICKSTORM: what the test session does not cover

Date: 2026-10-22. A review of Game #5 flagged "stage progression and
power-ups are not covered by the tests" as a hole to close. Measuring it
first turned up a more useful answer than the premise: **part of the
hole is not there, and the part that is cannot be closed cheaply.**

This card is report-only — no test was added, deliberately, for the
reasons at the bottom.

## The premise, checked

`tests/test_brickstorm.sh` says so itself (lines 13–18):

> The stage-to-stage progression (clearing a stage of bricks and moving
> on to the next) is NOT covered here: with the new physics a scripted
> paddle cannot clear a stage — the ball's path is too precise for a
> blind sweep, and a real player breaks the loop by moving. The stage
> CLEAR path itself is covered, by the boss's death (the last stage ends
> like any other).

That claim is worth checking rather than trusting, because the rest of
the card followed from it. It holds.

### Every state is reached

Sampling `state` on all 2800 frames of the scripted session
(`--vars state --var-every 1`):

| state | frames |
|---|---:|
| `ST_TITLE` | 64 |
| `ST_SERVE` | 20 |
| `ST_PLAY` | 693 |
| `ST_DEAD` | 240 |
| **`ST_CLEAR`** | **120** |
| `ST_OVER` | 1196 |
| `ST_WIN` | 467 |

All seven, and `ST_CLEAR` for a solid 120 frames. **The clear path is
covered**, exactly as the header says — reached through the boss's
death.

### The stage-to-stage handoff is not

`level` over the same session takes two values and no more:

```
1894 frames at level = 1
 906 frames at level = 13
```

The level picker jumps straight to the boss, so `load_level(n + 1)` —
the stage N → stage N+1 handoff, the thing "stage progression" actually
names — **never runs**. Stages 2 through 12 are never drawn at all. That
is the real hole, and it is narrower than it sounds: the CLEAR
machinery is exercised, the *advance* is not.

### Power-ups: one branch of six

Watching the globals each power-up mutates, rather than `power` (see the
trap below), over the same 2800 frames:

| power-up | its observable | measured | exercised |
|---|---|---|---|
| `P_MULTI` | `balls` | 1 → 3 at frame 157 | **yes** |
| `P_EXPAND` | `paddle_w` | 56 on every frame (never grows) | no |
| `P_SLOW` / `P_FAST` | `speed_timer` | 0 on every frame | no |
| `P_LASER` | `laser_timer` | 0 on every frame | no |
| `P_CATCH` | `power` | 0 on every frame | no |

`P_MULTI` is the only one that ever fires, and it fires once, at frame
157. **Five of the six `apply_power()` branches are dead** in the
scripted session — `EXPAND` never widens the paddle, `SLOW`/`FAST`
never touch the speed, `LASER` and `CATCH` are never picked up, so the
laser firing path and the catch-and-release path are both untested.

## The trap for whoever closes this

`power` does **not** mean "a power-up is in force". The enum is

```c
enum { P_EXPAND, P_MULTI, P_SLOW, P_FAST, P_LASER, P_CATCH, P_KINDS };
int power = 0;            /* the power-up in force, or 0 */
```

but `apply_power()` assigns `power` in **only two** branches —
`P_LASER` and `P_CATCH`. The other four mutate their own state and
leave `power` at 0. So sampling `power` and concluding "no power-up was
ever picked up" is **wrong**: `P_MULTI` demonstrably fired at frame 157
while `power` read 0 the entire session.

Worth knowing separately: because `P_EXPAND == 0` is numerically
identical to "none", assigning `power = P_EXPAND` would be a silent
no-op. Nothing does today, so nothing is broken — but the first person
to give `EXPAND` a timer will lose an afternoon to it.

## Why nothing was added

Two reasons, in order of force.

**1. `make check` is already over budget.** AGENTS.md sets the budget at
120 s. Measured at 222 s with an idle-ish machine, and 411 s at load
4.86 on 8 cores — a pre-existing breach, with PROGRESS.md's own history
running 88 → 112 → 117 → 130 s. Any new test grows it. The rule is to
report first, so this is the report.

**2. The uncovered branches cannot be reached from outside.** Closing
the power-up hole needs a session in which a drop is actually caught,
which needs a paddle placed under a drop whose fall is not knowable in
advance from the input script; closing the level handoff needs a stage
cleared, which the header already established a blind sweep cannot do.
Both need either game-side test hooks (publishing more state, or a
deterministic seed for drop placement) or a real player, and both are
game design decisions rather than test-writing ones.

## What closing it would take

* **Cheap, partial:** a new input script that catches one drop, pinning
  one more branch. Still needs the drop's fall to be predictable, which
  today it is not.
* **Honest:** a test hook — e.g. a `P_MULTI`-style deterministic drop
  schedule — so a script can position the paddle deliberately. This
  changes the game, and so the golden.
* **Free:** nothing about the current session needs protecting beyond
  what `tests/test_brickstorm.sh` already asserts.

Until one of the first two is wanted, the gap is recorded here rather
than papered over with an assertion that only pins the current
behaviour and reads like coverage.
