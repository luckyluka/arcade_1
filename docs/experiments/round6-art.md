# Round 6 — the clean-room art round

ROADMAP_V4 card 7.8: the acceptance test of the art pipeline. A **fresh
agent with no context** gets `AGENTS.md`, the art documents, the tools and
one brief, and has to produce a playable one-screen game with its art,
using only the repository's tools. What it hits on the way is the
measurement: the pipeline's friction, classified.

**Status: prepared, instrumented, not yet run clean-room.** The
infrastructure for fresh-agent sessions was unstable (four attempts, each
killed by a server restart — and one of them was taking the machine down
with it, see "The crash" below), so this file records the *method*, the
*rehearsal* and the *metrics harness*, and leaves the round itself to be
run by the operator with a fresh agent.

## What is in place

| | |
|---|---|
| `round6/BRIEF.md` | the brief: the game, what the agent may read, what it must produce, the rules, the log protocol |
| `round6/ROUND_LOG.md` | the log template the agent fills in as it goes |
| `tools/measure_round6.py` | the metrics harness: the objective checks are measured, the subjective ones are read from the log |

The folder holds **only** the brief and the log: an earlier attempt's art
would contaminate the run (a fresh agent must meet the art pipeline cold,
not find it half-built).

## The metrics

The card's success metrics, and how each one is measured:

| metric | how |
|---|---|
| attempts to the first compliant asset | the log (the agent's own count) |
| the number of DOC GAPs | the harness counts the numbered items in the log's DOC GAP section |
| whether `art/CRITIQUE.md` was used | the harness greps the log |
| whether a generated file was hand-edited | **objective**: the harness copies the round, re-runs every generator and the asset compiler there, and compares the trees file by file |
| `arc1-art check` clean on all the assets | **objective**: the harness runs the check on every sprite in the manifest |
| the game golden-locked via card 4.2 | **objective**: the harness runs `arcade1 test --golden` |

```sh
python3 tools/measure_round6.py round6      # the round
```

It prints a JSON report and exits 1 when the round is not complete.

## The rehearsal (not the round)

To prove the brief is executable — and to shake out the documentation's
rough edges — the same brief was carried out by the agent that maintains
the tools, with full context. **Its metrics are not comparable to a fresh
agent's** (that agent knows the tools), but its *findings* are real: it
hit four places where the documentation did not say what to do.

| | |
|---|---|
| the game | a one-screen fantasy village: a farmer, a slime, four pickups (a carrot, a mushroom, an egg, a pot of honey), a three-row ground of grass, path and stone |
| the art | `gen_round6.py` — procedural, deterministic, seeded; nine strips; one manifest; the collision layer from the map's `solid` table |
| the golden | 400 frames, written by `arcade1 golden` and verified with `arcade1 test --golden` |
| the harness | `assets_clean: true`, `golden.ok: true`, `no_hand_edited_generated_file: true`, `gaps: 4` |

### The four DOC GAPs (rehearsal)

1. **The art generator's path assumption is undocumented.** The generator
   resolves the repo's `tools/` as "two directories up from the script",
   so it only works when the art lives at `<repo>/<dir>/art/`. Moved
   anywhere else it dies with `ModuleNotFoundError: No module named
   'arc1_art'`. — *class: docs.*
2. **"A tileset is ONE frame" is not in the error message.** The importer
   said `legend "p" tile 1 out of range — sprite tiles holds 1 tiles
   (1 x 1 grid)`; the real mistake was writing three tiles as three
   *frames*. The rule is in `GAME_DEV_GUIDE.md`, but the error never
   points at it. — *class: tool (the message).*
3. **The SDK defines no `SCREEN_W` / `SCREEN_H`.** Every game defines
   them itself; nothing says so, and the compiler's suggestion was
   `TREE_H`, a sprite constant. — *class: docs (or the SDK).*
4. **`arcade1 build` does not carry the game's own include path.** The
   generated header sits next to the manifest, and the CLI compiles in a
   temp directory — so `#include "assets.h"` resolves only because the
   source and the manifest happen to share a folder. — *class: tool.*

### A finding the machine made, not the agent

The game wrote a 24-bit colour literal (`0x1A1030`) and the CLI's colour
truncation diagnostic (card 4.4) caught it: *"400 colour writes carried
bits above 15 and were truncated to RGB565 (use `arcade1_rgb()`)"*. The
diagnostic works, and it works on code it has never seen.

### A finding about the checks

`arc1-art check` passed all nine strips — and the generator was still
able to exhaust the machine's RAM. The QA tool counts pixels; nothing
counts *memory*. A generator that builds a sheet the size of a screen
would pass every check.

## The crash

Worth recording, because it cost the round four attempts and the machine
several restarts (and the editor with it).

The interrupted agent's generator filled its contact sheet's canvas with
the *integer* `0x404040` instead of an RGB tuple. The write loop then did
`bytes(col)` on that integer — which in Python means "a bytes object of
this **length**", 4 211 264 bytes for every background pixel. The sheet
was a few hundred kilobytes; the buffer was gigabytes. The machine
swapped, the server died, the session died with it.

The fix was one character. The lesson is not about the agent: it is that
**a tool that can allocate without bound is a tool that can take the
machine down**, and the art pipeline has no guard against it. The
harness now runs the generators inside a copy and compares the trees, but
nothing caps their memory.

Two cheap guards, if the owner wants them:

* the canvas library could refuse an allocation larger than a sane
  maximum (a 1920×1080 sheet is 6 MB; the runaway one asked for gigabytes);
* `arc1-art` could run its generators with a memory cap (the rehearsal
  used `ulimit -v` and the runaway died with a `MemoryError` instead of
  taking the machine with it).

## What the round still needs

The operator runs it: a fresh agent, the brief, the tools, and
`round6/ROUND_LOG.md` to fill in. Then:

```sh
python3 tools/measure_round6.py round6
```

and the classification of every friction item — **docs / tool / pipeline /
game** — goes into this file, replacing the rehearsal's.

**The round's own result, if the agent produced something unattractive but
compliant, will be reported as such**: compliance is the test, taste is a
separate section.
