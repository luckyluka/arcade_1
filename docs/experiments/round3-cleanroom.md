# Round 3 — the clean-room fresh-agent test (Game #2)

2026-10-20, post-SDK-hardening. The reviewer's third challenge: *proof a
fresh agent can ship a game from the docs alone*. Waves 1–4's proofs were
same-agent; this is the first genuinely clean-room one.

## Setup

A background agent with **no conversation context** received a one-paragraph
console description, a game spec (Flappy-style timing game), and rules:
learn only from the repo docs (GAME_DEV_GUIDE on-ramp, SDK/CHEATSHEET as
reference), build only via `arcade1 build` from its own `round3/`
directory, test fully headlessly, prove determinism, golden-lock the
session, touch nothing existing, and write a frank friction report. The
brief contained one wrong number on purpose-by-accident (see finding 1).

## Result: PASS, on every axis

- **The game works.** `round3/flappy.c` (~180 lines freestanding C) built
  on the first try from a foreign directory: bird + gravity + edge-triggered
  A-flap, recycling pipe pairs with `arcade1_rand` gaps, +1 scoring,
  collision → GAME OVER → A/START restart, HUD via the opt-in `text.h`
  font. All observable state in file-scope globals, readable via `--vars`.
- **Determinism proven**: two `--strict` runs byte-identical (520 frames).
- **Golden-locked**: `arcade1 test --golden` matches; a tampered golden
  fails precisely at the first differing frame.
- **Record→replay round trip** bit-identical.
- **Game logic validated** against an exact host-side Python model of the
  integer maths + LCG (the model *authored* the input script as an
  autopilot): `--vars` traces agree on 520/520 frames.
- **Repo integrity**: `git status` clean outside `round3/`; zero existing
  files touched.
- **Re-verified independently** after the round: rebuild byte-identical
  (and byte-identical to the Makefile-path build — the two documented
  build paths produce the same image), determinism re-diffed, golden
  re-matched, gameplay story re-traced through `--vars`, screenshot
  inspected.

Promoted as Game #2: `programs/flappy.c` (round-3 code, unmodified but
for the provenance header), `tests/inputs/flappy.txt`,
`tests/test_flappy.sh` (determinism / story / record→replay),
`tests/roms/flappy.{arc1,syms}`, `tests/golden/flappy.txt` (520 frames),
wired into `GUEST_PROGS`, `GOLDEN_GAMES`, `GOLDEN_IMGS` and `make check`.

## Friction findings (the roadmap-V4 payload)

In the agent's own words in `round3/REPORT.md`; distilled:

1. **Briefing vs docs conflict has no trust rule.** The brief said
   320×180; the repo uniformly says 480×270 (`SPEC.md:280`,
   `CHEATSHEET.md:40-41`). The agent resolved it by trusting the repo —
   correct, but it took reading two files plus SPEC to feel sure. (The
   wrong number came from the round-3 brief, not the repo.)
2. **Golden *authoring* is make-only in the guide.** `GAME_DEV_GUIDE`
   §"Test and golden-lock it" assumes repo ownership (`make roms-update` /
   `make golden-update`); the CLI-only recipe exists but is scattered
   across three sections. One sentence — "redirect `arcade1 run` output
   to a file to create a golden" — would have removed all doubt.
3. **Golden-file grammar lives only in the CLI source**: which lines the
   compare tolerates (`tools/arcade1.c` skips non-`frame` lines) and that
   a shorter golden vs the default `--frames 600` is fine — both
   knowable only from source.
4. **The embedded SDK header set is never enumerated** (`text.h`,
   `asset.h`, `arcade1_map.h` ship inside the CLI; the agent had to read
   `tools/arcade1.c` to confirm).
5. **Edge-triggered buttons × input scripts: the nastiest silent trap.**
   Docs describe script semantics perfectly but are silent on the
   interaction with press-edge detection: a held A across a restart eats
   the next flap's edge, and the session silently diverges ~40 frames
   later. Caught only via per-frame `--vars` diffs against a host model.
   (This is the input-script worked-examples card's sequel: docs should
   say "levels in, edges are your game's job — mind holds across
   state changes".)
6. **Missing conveniences**: no one-command golden creation (`arcade1
   golden`-style), no per-frame pad dump to see what the script actually
   applied.
7. **For balance — the docs were accurate everywhere checkable**: build
   self-containment, `--vars` auto-syml, `--capture` N−1 semantics, exit
   codes, `--strict`, record→replay, precise golden-failure pointers.
   Zero in-repo doc turned out wrong.

## Verdict

The docs-as-interface bet paid for the paths that matter: a stranger
shipped a deterministic, golden-locked, well-factored game in one session
with no source archaeology beyond what the guide itself points to. The
friction list is small, specific and actionable — exactly the material
ROADMAP V4's first wave should be cut from.
