# Round 6 — the clean-room art round

ROADMAP_V4 card 7.8: the acceptance test of the art pipeline. A **fresh
agent with no context** got `AGENTS.md`, the art documents, the tools and
one brief, and had to produce a playable one-screen game with its art,
using only the repository's tools. What it hit on the way is the
measurement.

**The round ran, and it passed.** The game builds, plays, is golden-locked
with the CLI's own `golden` command, every asset passes `arc1-art check`,
no generated file was hand-edited, and the agent used `art/CRITIQUE.md`
and wrote the review the checklist asks for. It also found **seven places
where the documentation did not tell it what to do** — which is the point
of the exercise.

## The result

| metric | how it is measured | the number |
|---|---|---|
| the game | built, run, golden-locked with `arcade1 golden` (240 frames), the golden verified with `arcade1 test --golden` | **pass** |
| the assets | `arc1-art check` on every sprite in the manifest | **7 / 7 clean** |
| nothing hand-edited | the harness re-runs every generator and the compiler in a copy and compares the trees | **pass** |
| the DOC GAPs | counted in the log's DOC GAP section | **7** |
| `art/CRITIQUE.md` used | the harness greps the log | **yes** |
| the first compliant asset | the log's own timeline | **the farmer strip, ~16 minutes** after the step began (all 7 strips passed `arc1-art check` on the first try) |
| the time | the log's timeline | the art step 09:31–10:05, the game step 10:12–10:45 |

```sh
python3 tools/measure_round.py round6     # the JSON report
```

The harness's verdict for the round: `"ok": true`.

## What the agent built

**"The Farmer of the Village"** — one screen, no camera: a 30×16 village
map (grass, a paved square, a road, a pond) with a farmer, a hunting
slime and four harvests to collect. The d-pad walks the farmer; START
restarts. Collect all four and the day is saved (`state = 1`); the slime
costs a life (three and the village is lost, `state = 2`). The collision
comes from the map's compiled layer — the pond really is solid.

The art is procedural and deterministic (`round6/art/gen_round6.py`):
seven strips (the farmer, the slime, a coin, an apple, a carrot, a
cabbage, a three-tile ground), a map, a collision layer, a manifest with a
provenance block on every asset, and a style sheet
(`round6/art/STYLE.md`).

The review (`round6/art/reports/round6.md`) follows `art/CRITIQUE.md`:
verdict, observations, changes, re-check. It caught two things the
machine's checks could not — the tileset's dark border repeating in every
map cell (the field was a striped quilt) and the slime's eyes being
invisible at 4x (dark on dark, and *passing* every check). Both were
fixed and re-goldened.

**Compliance is the test, taste is a separate section**: the game is
compliant and playable, and the art is plain — a green field, a blue
pond, a small farmer. It does not look like Petal Blade, and nobody
pretended it does.

## The seven DOC GAPs, classified

| # | the gap | class |
|---|---|---|
| 1 | **the repository root: the prose and the code disagree.** `art/README.md` said the generator "lives two directories below the repository root" and, two paragraphs later, "keep the generator two levels below the root" — the two cannot both be true, and the snippet it prints only resolves for a script **three** levels down. The agent hit `ModuleNotFoundError` immediately | docs |
| 2 | **the collision layer is not in the art documentation at all.** The brief asks for one; the key that does it (`"solid"`) is documented only in `tools/arc1_assetc.py` and `programs/asset.h`. An agent that read only the art docs would have shipped a map with no collision | docs |
| 3 | **the outline-coverage check is vacuous on a fully opaque strip** (no edge pixels → coverage 1.0), and the docs never say that a *background* layer needs no outline ring — nor that a border in a tileset repeats in every map cell. The agent first drew a 1 px border "to be safe" and the field became a barcode | tool + docs |
| 4 | **"Index 1 is the outline colour — *dark*"** in the docs, but the checker infers the outline as the **darkest colour present in that frame**; in `hero16` index 1 is only just the darkest (luminance 16.9 against 21.2 for index 9), so the two rules can disagree | docs (+ tool) |
| 5 | **nothing says a manifest may define its own palette.** The style template frames a deviation as *changing an index of a preset*, but the four pickups need red + green + gold and no preset holds red and green together. The agent wrote two custom banks | docs |
| 6 | **(minor) the manifest's paths are relative to `assets.json`** — stated only in the compiler's error text | docs |
| 7 | **a file-scope global the guest never *reads* is deleted by `-Os`**, and `--vars` then says the name does not exist (a bare runtime error, no hint). The agent measured it: written-never-read → gone; read-never-written → folded to a constant; read *and* written → kept | docs (+ tool) |

**The tally: five documentation gaps, one tool gap, one that is both.**
Five of the seven are in documents the agent is *expected* to read, and
two of those (1 and 4) are in text written for this very wave — which is
the uncomfortable part: the fresh agent found contradictions in the
instructions it was given, minutes after they were written.

**Not a gap, but worth recording:** the agent's two 24-bit colour
literals (`0xFFE3A5`, `0xC6C3CE`) were caught by the CLI's colour
truncation diagnostic (card 4.4) — *"2940 colour writes carried bits
above 15"* — on code the tool had never seen. The cheat sheet had said
RGB565 all along; the diagnostic made the mistake *visible*.

## A pipeline gap the round found by itself

The agent wrote **its own** look-at-the-art tool (`round6/art/preview.py`):
a contact sheet of the map at 1x and 2x and of the sprites at 4x. The
pipeline has the drawing library's `contact_sheet` and the rig's sheet,
but **nothing that turns a manifest into a preview** — and the review
checklist in `art/CRITIQUE.md` asks a human to look at a contact sheet
that the pipeline does not produce. Two of the agent's four art fixes
came from that scratch tool.

*Class: pipeline.* The fix is small: an `arc1-art preview <manifest>`
that writes the sheet the checklist expects.

## What is in the repository

| | |
|---|---|
| `round6/BRIEF.md` | the brief the agent got |
| `round6/ROUND_LOG.md` | the log it kept — the timeline, the attempts, the seven gaps, what it had to read in the tool's source, and the checklist |
| `round6/art/` | the generator, the strips, the map, the collision layer, the style sheet, the review |
| `round6/game.c` | the guest |
| `round6/assets.json`, `round6/assets.h` | the manifest and the compiled header |
| `round6/round6.arc1`, `round6/round6.syms`, `round6/round6.golden` | the image, the symbols, the golden (240 frames) |
| `round6/in.txt`, `round6/in_lose.txt` | the winning and the losing sessions |
| `round6/shot*.png` | the frames it looked at |
| `tools/measure_round.py` | the metrics harness |

The round is **not** promoted into `make check` — the brief said to keep
it inside `round6/`, and promoting it would change the game list. The
agent's own note says the same: *"Promote the game to a repo test … and,
if the round is judged a success, a `GUEST_PROGS` entry — deliberately
not done here."*

## All seven are closed (and pinned)

The gaps above are not a wish list: every one of them is fixed and
pinned by `tests/test_doc_gaps.sh`, so the next agent cannot hit them
again.

| # | the fix |
|---|---|
| 1 | `art/README.md` now says where the repository root is, in the words the code uses ("a folder whose parent's parent is the repository root"), and offers `ARCADE1_ROOT` as the way out |
| 2 | `art/README.md` has a **"The collision layer"** section: both forms (`"solid": "<file>"` and `"solid": {"by_tile": {...}}`), the characters, and how to read it with `map_solid()` |
| 3 | the QA tool now says **"the outline check does not apply — the frame is fully opaque, so it has no edge"** instead of passing silently, and the art rules say that a border in a tileset **repeats in every cell of the map** |
| 4 | the outline is now inferred as the **darkest colour among the frame's EDGE pixels** (the outline is on the edge by definition) — not the darkest in the whole frame, which a dark fill could beat. The docs say "the **darkest** colour in the bank", and the palette test enforces it (index 1 must be clearly the darkest) |
| 5 | `art/README.md` has a **"The manifest, in full"** section: a manifest **may define its own palette** (with the same rules as a preset), and the style template says a deviation may be a new bank |
| 6 | the same section states that **every path is relative to the `assets.json` file** |
| 7 | `GAME_DEV_GUIDE.md` and `SDK.md` say the game must **READ** the variable (or declare it `volatile`), and the CLI's *"no symbol named X"* now says the symbol may have been **optimised away**, and why |

**The pipeline gap is closed too:** `arc1-art preview <assets.json> --out
sheet.png` writes the contact sheet the review checklist asks for — the
round-6 agent had to write its own. It shows every sprite's frames at 1x
and scaled, numbered, with the legend on stdout.

**What the fixes did not change:** the round's metrics. They are recorded
above and were **not tuned afterwards** — the numbers are what the round
measured, gap for gap.