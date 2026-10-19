# Findings — the AI creation flow for a side-scroller (audited design trace)

| | |
|---|---|
| Date | 2026-10-19 |
| HEAD | `e80cb39` (docs: AssetEcosystem.md) |
| Kind | Design trace + baseline evidence. **No new runtime/tool code was built for this step** — repo changes are this file plus three factual corrections to `AssetEcosystem.md` (§7). |
| Predecessor | `docs/experiments/walkdemo.md` — full record of the baseline experiment |
| Audience | Reviewer AI / human reviewer. Every factual claim is tagged and reproducible. |

**Status tags used below**

- **VERIFIED** — checked in this repo at HEAD; reproduction given (§6).
- **PROPOSED** — specified in `AssetEcosystem.md`; not implemented.
- **GAP** — known missing; blocks part of the flow.
- **ILLUSTRATIVE** — example numbers, computed but not measured on a running game.

## 1. Question

If `AssetEcosystem.md` were implemented, what would the AI-assisted
creation flow look like for a game like `walkdemo`, but "proper" — a real
side-scroller: scrolling tilemap world, running/jumping player,
parallax, enemies, HUD, score, effects?

## 2. Baseline — what the flow actually looks like *today*

`walkdemo` (`2aca55e`) is a complete small instance of the current AI
creation flow, built in one session. Its measured anatomy:

| step | what actually happened | evidence |
|---|---|---|
| art | hand-authored `tools/gen_walk_sheet.py` — **225 lines**, stdlib only, byte-deterministic (regeneration = zero diff) | `wc -l`; `walkdemo.md` |
| build truth | generated header `walkdemo_sheet.h` **committed**; generator is master | `walkdemo.md` |
| guest | 94-line C guest (sheet, speed/anim constants, ping-pong anim, flip-X facing) | `wc -l programs/experimental/walkdemo.c` |
| verify | scripted inputs + `--vars`/`--syms` + fb-hash stability + pupil-pixel scan of PPM captures | `tests/test_walkdemo.sh` (132 lines) |
| bugs caught | 4 tool bugs, incl. **two silent colour corruptions** | `walkdemo.md` |
| missing | no live input; "feel" reviewed by a human looking at PPMs | — |

Verified assertions the test makes (`tests/test_walkdemo.sh`):

- idle: 12 frames → exactly **1 fb hash** (:78)
- walk right: 40 frames `BTN_RIGHT` → **x = 100 exactly** (from x=20 at SPEED 2) (:91)
- flip-X: pupil at char-relative column **23** facing right, mirrored to
  **16 = 39−23** facing left (:113, :129); out-and-back returns **x=20, facing=1** (:123-124)
- sheet = 40/2 × 56 × 7 = **7 840 B ≈ 3.0 %** of the 256 KiB ROM

**Cost of today's flow:** asset tooling is bespoke per game;
verification is real but re-hand-assembled each time; iteration on
*feel* is impossible (the CLI is headless).

## 3. The flow as `AssetEcosystem.md` would implement it

Session sketch. Per-command tags: ✓ exists now, ✗ PROPOSED, ⚠ GAP.

```
$ arcade1 new sidescroller ✗                 scaffold: code/ art/ maps/ tests/
$ arc1-assetc build art/ maps/ palettes/ \
      -o res/ --report ✗                     validate (even widths, palette range,
                                              min map geometry) → quantize → pack →
                                              dedup → ROM/RAM report; byte-deterministic,
                                              artifacts checked in (§2.1)
$ make game ✓
$ arcade1 run game.arc1 --input tests/in/smoke.a1 \
      --strict --inspect ✓                   machine-aware guardrails (§16):
                                              out-of-window draws fail; budget reported
$ arcade1 test game.arc1 --input tests/in/run.a1 \
      --golden tests/golden/01_side.txt ✓    behavioural proof — conditions MUST be
                                              reproduced (see F9)
$ arcade1 run game.arc1 --live --record r.a1 ⚠ you play it; R records your session
                                              (record/replay half ✓ exists, front-end GAP)
```

Two roles stay distinct (§2.3/§18): **AI edits structured artifacts and
measures**; **the human supplies taste**, and taste's artifact is an
input script — which the machine can replay forever.

## 4. Findings

**F1 — Determinism is what makes AI verification *trustworthy*.**
Per-frame FNV-1a state/framebuffer hashes (`tools/arcade1.c:16`) compared
byte-exactly against committed goldens (`make test-golden`: grep `frame `
lines + `diff`, `Makefile` test-golden recipe). A green run is a proof,
not an opinion — the property that makes AI-side iteration safe at all.
All 9 golden games match at HEAD. **[VERIFIED]**

**F2 — The replay primitive already exists headlessly; Phase 0's gap is
narrower than "we need recording".** `--record` writes applied per-frame
buttons *in the exact format `--input` reads* (`tools/arcade1.c:30,45-49`),
and the round trip is tested bit-for-bit: normalized runs, idle→`none`,
two recordings of one session byte-identical, and replaying a recording
reproduces the original per-frame hashes (`tests/test_run.sh:146-169`).
What does **not** exist: any interactive input — no `--live`, keyboard,
or SDL anywhere in `tools/arcade1.c` (895 lines; grep returns nothing);
subcommands are run/capture/test/build/inspect only (:976-984).
So the taste loop = *live front-end* (GAP) + existing record/replay.
**[VERIFIED / GAP]**

**F3 — Independent-oracle rule: shared-bug review passes wrong output.**
walkdemo's python preview palette and the C palette code agreed with
each other while *both* wrong (16-bit values truncated through
`unsigned char`; out-of-range 5/6/5 components silently `& 31`-masked).
Both survived eyeball review; both died only when checked against
independently computed expectations (`walkdemo.md`). For an AI reviewer
this is the general rule: **cross-checks must not share the code path
under test.** **[VERIFIED]**

**F4 — Machine rules are enumerable, so validation beats review for AI
output.** Each rule a future `arc1-assetc` (§11) must encode, with its
source:

- odd 4-bit width draws nothing — `src/arcade1/video.c:303-304`
  (shared blit; tilemap4 too, `:460`)
- TILEMAP4 small-map draw rule — **RETRACTED**: the once-measured size table
  was read as a machine minimum, but it came from pre-barrier builds where
  the compiler deleted the guest's cell fills; every geometry draws with
  `2d29b79` + `tests/test_barrier.c` pinning it. See
  `docs/experiments/tilemap-minsize-retraction.md`
- palette index is **bank-relative**: `PALETTE[bank*16 + v]` —
  `CHEATSHEET.md:66,76`, `programs/arcade1.h:42`
- `.arc1` payload ≤ 262 144 B = ROM size — `CHEATSHEET.md:109`
- out-of-range colour components must **error**, not mask (walkdemo's
  own bug, F3)

Enumerable rules ⇒ a compiler can reject whole *classes* of error that
review only catches instance-by-instance. **[VERIFIED]**

**F5 — Cost visibility changes design decisions before code exists.**
walkdemo's ROM report (3.0 %) is what made the budget salient. For a
side-scroller the first budget line: a full screen is 20×15 cells
(`DEVICE_BUDGET.md:87`) × 4 B = 1 200 B/screen; a 128-cell-wide level
(15 rows) = 1 920 cells × 4 B = **7 680 B ≈ 2.9 % ROM** — ILLUSTRATIVE
width, verified 4 B/cell format (`gfx.h` cell word: `[15:0]` index,
`[19:16]` bank, `[20]` flipX, `[21]` flipY). Scrolling a full-screen
tilemap every frame is the *measured worst case* video row
(`DEVICE_BUDGET.md:64`), and full-screen scrolling is already exercised
by a golden game (`tests/golden/scroll.txt`). **[VERIFIED / ILLUSTRATIVE]**

**F6 — Taste is the irreducible human input, and its artifact must be an
input script.** Hashes can't judge a jump arc. F2 shows the storage and
replay halves ship today; only the interactive front-end (GAP) is
missing. The loop: human plays → `--record` → AI replays deterministically,
measures (apex, airtime, distance), tunes constants, re-replays *the
same script*. **[VERIFIED chain / GAP at the front-end]**

**F7 — Golden discipline scales to assets — with the walkdemo pattern as
precedent.** Source asset = master art, compiled artifact = committed
build truth, both deterministic (regeneration produced zero diff).
Update path must stay surgical: `make roms-update` rewrites **all** ROM
snapshots (`Makefile:802`) — the M29 lesson was adding one game's files
by hand. **[VERIFIED]**

**F8 — The entire flow is host-side; the repo invariant survives by
construction.** Asset phases 0–5 of `AssetEcosystem.md` §20 change no
machine, SDK-header, or guest bytes for existing games ⇒ older goldens
stay byte-identical without any special effort. **[VERIFIED — current
state: `make check` green, 9/9 goldens, older 8 unchanged since `f92bbc6`]**

**F9 — Found during this audit: `arcade1 test --golden` compares hashes
but not run *conditions*, and the docs' bare example fails on
input-driven goldens.** Experiment (all at HEAD):

| invocation | result |
|---|---|
| `build/arcade1 test tests/roms/m29game.arc1 --golden tests/golden/m29game.txt --frames 1800` | **exit 1** — differs from frame 0 (ran `0x2696…` vs golden `0xE667…`) |
| same **+ `--input docs/m29/scripts/restart.txt`** (the golden's actual conditions) | **exit 0** — "1800 frame lines match" |
| same + a tampered golden (hash on line 5 edited) | **exit 1** — "frame line 5 differs" |

The mechanism works; the gap is that the golden file records only
`frame N hash fb` lines — the input script, frame count, and ROM that
produced them live outside it. `make test-golden` gets conditions right
by re-invoking each game's full test target, but the documented
one-liner `arcade1 test game.arc1 --golden tests/golden/mygame.txt`
(`GAME_DEV_GUIDE.md:57`, cf. `SDK.md:186-191`, `CHEATSHEET.md:125-126`)
omits `--input`, so on m29game it fails despite correct bytes. Also:
no test script nor Makefile recipe invokes the `--golden` flag at all —
6 tests compare golden files by grep/cmp of `frame` lines instead
(`test_run.sh:51-68`, `test_m29game.sh:65-67`, …).
**Consequence for `AssetEcosystem.md` §2.2:** per-resource golden hooks
need either the conditions recorded beside the hashes (sidecar/manifest)
or the harness supplying them — §2.2 was amended accordingly.
**[VERIFIED]**

## 5. Claim ledger (quick audit index)

| claim | status | evidence |
|---|---|---|
| CLI = run/capture/test/build/inspect; flags `--input --record --vars --syms --strict --inspect --capture-dir` | VERIFIED | `tools/arcade1.c:409-420,976-984` |
| record → replay round trip is bit-exact and tested | VERIFIED | `tests/test_run.sh:146-169` |
| no live/interactive input exists | VERIFIED (GAP) | grep `--live\|keyboard\|SDL` in `tools/arcade1.c` → none |
| `--golden` works iff conditions reproduced; bare docs example fails; flag untested | VERIFIED | §4 F9 table |
| golden comparison in CI = grep+diff of `frame` lines | VERIFIED | `Makefile` (`test-golden`) |
| 9 goldens match at HEAD; older 8 untouched since `f92bbc6` | VERIFIED | `make check`; git log |
| ROM 256 KiB / RAM 64 KiB | VERIFIED | `CHEATSHEET.md:3,109` |
| odd 4-bit width → draws nothing | VERIFIED | `src/arcade1/video.c:303-304` |
| TILEMAP4 small-map draw rule | **RETRACTED** | pre-barrier build artifact — `docs/experiments/tilemap-minsize-retraction.md` |
| `PALETTE[bank*16+v]` | VERIFIED | `CHEATSHEET.md:66` |
| walkdemo numbers (7 840 B, x 20→100, pupil 23↔16, 1 fb-hash idle) | VERIFIED | `tests/test_walkdemo.sh:78-130`; `walkdemo.md` |
| generator = 225 lines, deterministic regeneration | VERIFIED | `wc -l`; `walkdemo.md` |
| full-screen scroll is measured worst case; 20×15 cells | VERIFIED | `DEVICE_BUDGET.md:64,87` |
| `arcade1 new`, `arc1-assetc`, `--live`, named-asset SDK, Studio | PROPOSED / GAP | `AssetEcosystem.md` §11-14, §20 |
| session transcript in §3 percentages | ILLUSTRATIVE | arithmetic shown in F5 |

## 6. How to re-verify (reviewer quick start)

```sh
make check                              # exit=0, 0 warnings, 9/9 goldens
wc -l tools/gen_walk_sheet.py           # 225
grep -c '' tools/arcade1.c              # 895; grep live/keyboard/SDL → 0 matches
make test-run                           # includes record/replay (test_run.sh:146-169)
make build/arcade1                      # host CLI
build/arcade1 test tests/roms/m29game.arc1 \
    --golden tests/golden/m29game.txt --frames 1800
#   → exit 1 (frame 0 differs: conditions missing)
build/arcade1 test tests/roms/m29game.arc1 \
    --golden tests/golden/m29game.txt --frames 1800 \
    --input docs/m29/scripts/restart.txt
#   → exit 0 ("1800 frame lines match")
```

## 7. Corrections this audit forced on `AssetEcosystem.md`

1. "230-line generator" → **225** (`wc -l`).
2. Tilemap trap rewritten to the then-measured size table
   (`docs/m29/M29_LOG.md:173-182`) — **the rule behind this was retracted
   entirely; see `docs/experiments/tilemap-minsize-retraction.md`**.
3. §2.2 amended: `--golden` comparison happens **under the golden's run
   conditions** (F9).

## 8. Not tested / open risks

- **Phase 0 SDL front-end**: not built, no measurements; effort estimate
  (~200 lines) is an assumption, not a result.
- **`arc1-assetc` does not exist**; determinism is a *requirement* in the
  doc, demonstrated only by its seed (`gen_walk_sheet.py` byte-identical
  regeneration).
- §3 transcript is illustrative; only its per-line arithmetic (F5) and
  the commands marked ✓ are real.
- No game here uses every budget worst case simultaneously;
  `DEVICE_BUDGET.md` covers measured rows, not a composed side-scroller.
- FPGA does not exist (`DEVICE_BUDGET.md:102`); "same asset model on
  hardware" is unproven by definition.

## 9. Recommended next steps (ordered by leverage)

1. **Fix F9 cheaply** — either a sidecar `# conditions:` header in golden
   files or the `--golden` docs line gaining `--input` (mind:
   `CHEATSHEET.md` snippets are test-pinned by `test_cheatsheet.sh`).
2. **Phase 0** — live front-end (key state → pad register/frame; record
   via existing `--record`). Unblocks F6 end-to-end.
3. **Phase 1** — generalize the 225-line generator into `arc1-assetc`
   (PNG → SPRITE4/PALETTE/TILESET, deterministic, ROM report) per F4.
