# ARCADE-1 Roadmap V3 — review-adjusted, executable

| | |
|---|---|
| Purpose | Order and amend the work proposed in `docs/ROADMAP_CREATION_FLOW.md` and `AssetEcosystem.md`, add the fixes the M29 fresh-agent rounds proved necessary, and give the implementing AI task cards it can execute without guessing |
| Audience | The implementing AI agent. Read §0-§2 fully before any card. |
| Supersedes | The *ordering* of `docs/ROADMAP_CREATION_FLOW.md`. Its cards stay valid where §4 says "keep"; amended cards are restated here. |
| Facts verified | 2026-10-19 at HEAD `ae22eff`, by an independent reviewer (clean rebuild; `make check` exit 0; self-contained-build gate; F9 claims re-run; tilemap root cause reproduced) |

## 0. How to work (binding)

1. **One card = one commit.** Message: what and why. Do not combine cards.
2. **Run the card's acceptance commands and read the output.** "Looks right" is not done. Paste the key output lines into your report.
3. **Before every commit:** `make clean && make check` exits 0 with **zero compiler warnings**.
4. **Older goldens are untouchable.** `git diff --stat -- tests/golden tests/roms` must be empty for every card unless the card says otherwise. Never run `make roms-update` or `make golden-update` as part of a tooling card. A new game adds only its own new files.
5. **No new dependencies.** Host `cc` and `python3` stdlib only. Guest code stays freestanding.
6. **No machine behaviour changes** in Waves A, B, 1-4. If a card seems to need one, stop and report.
7. **A card's premise may be wrong.** If a measurement contradicts a card, stop, log it in `docs/experiments/`, and report. Do not improvise around it. (This already happened once: see card A0.)
8. **Findings are deliverables.** Surprises go to `docs/experiments/<topic>.md` in the same commit as the fix.
9. **Stop after each wave** and write a report: cards done, acceptance output, anything surprising, anything not done.
10. **Scope guard.** New ideas go to `FUTURE_IDEAS.md`, never into the current card.

## 1. Verdict on the proposals

| Proposal | Verdict | Why |
|---|---|---|
| Creation-flow roadmap structure (waves, cards with acceptance, invariants) | **Keep** | Evidence-based, verifiable, small commits. Best-written planning document in the repo. |
| Wave 0: golden run-conditions (F9) | **Keep, compress, move after Wave 1** | The trap is real (re-verified: a short run with input exits 0 silently; a bare run exits 1). Low frequency, ~3 h of work. Not the leverage point. |
| Wave 1: `arc1-assetc` v0 | **Keep, amend (§4.2), run first among the creation-flow waves** | Round 2 confirms it: all three games wrote their own asset generators. |
| Wave 2: typed accessors | **Keep** | Removes stride math from game code. |
| Wave 3: animation resources | **Keep** | walkdemo is the proof. |
| Wave 4: map import | **Keep, amend (§4.3)** | **Its min-geometry validator is based on a false rule. Reject that part.** |
| Wave 5: Game #1 through the flow | **Keep, extend acceptance (§4.4)** | The decisive metric. |
| Play window `--live` | **Keep as optional parallel, low priority** | Useful to record human sessions as input scripts. RetroArch already lets a human play. |
| `AssetEcosystem.md` §14 "ARCADE-1 Studio" and the individual Studios | **Defer** | Pipeline first, editors later. Revisit only with Game #1 evidence. |
| Guest print/debug MMIO (a console device) | **Reject** | `AI_NATIVE_DEVELOPMENT.md` forbids debug MMIO. Use globals plus `--vars`. |
| HD / scaling mode in the machine | **Reject** | Resolution is fixed at 480x270. Bigger images come from upscaled captures (card A3). |
| Cost-model and cap decisions (`DEVICE_BUDGET.md`, `SPEC.md`) | **Stay deferred** to the FPGA milestone | Owner decision. |

**Missing from the proposals, added here:** Wave A (tooling and SDK friction proven by M29 rounds 1 and 2) and Wave B (`arcade1 inspect` v1). **Wave A runs first** because it is small (about 1-2 days), removes friction that every later wave and every fresh agent would hit, and its evidence is direct.

## 2. Order of work

| Order | Wave | Deliverable | Rough size |
|---|---|---|---|
| 1 | **A** | Friction fixes: symbols from `build`, output-name checks, PNG/scale captures, `--vars all`, docs pass, SDK helpers (rand, pixel, text) | 1-2 days |
| 2 | **1** | `arc1-assetc` v0 (amended) | 1-2 days |
| 3 | **0** (compressed) | Golden run-conditions | half a day |
| 4 | **B** | `arcade1 inspect` v1 | 1-2 days |
| 5 | **2** | Metadata and typed accessors | 1 day |
| 6 | **3** | Animation resources | 1 day |
| 7 | **4** (amended) | Map import | 1 day |
| 8 | **5** | Game #1 end to end | days (it is the point) |
| ∥ | Play window | optional, after Wave 1 | 1 day |
| 9 | **6** | Decide after Game #1: Audio v2 and any evidence-based machine change | placeholder |

## 3. Wave A — friction fixes (new; from `docs/m29/M29_LOG.md` and `docs/m29b/M29_LOG.md`)

### Card A0 — retract the tilemap "minimum geometry" rule (docs only, 30 min)

- **Why**: The experiments doc and the Wave 4 plan treat `minCOLS = 6,6,6,6,5,3,2,2,1` as a machine rule. It is not. The VM's TILEMAP4 code has no size-dependent branch. In the failing builds the compiler **deleted the guest stores that fill the map array**, because the device reads that memory through an integer address the compiler cannot see. With a compiler memory barrier before the draw, every geometry draws. The SDK barrier fix (commit `2d29b79`) and `tests/test_barrier.c` already pin this.
- **Files**: `docs/experiments/ai-creation-flow.md` (finding F4 bullet and the claim-ledger line "TILEMAP4 min-geometry table — VERIFIED"), `docs/ROADMAP_CREATION_FLOW.md` (Wave 4 validators), `AssetEcosystem.md` if it repeats the rule; NEW `docs/experiments/tilemap-minsize-retraction.md`. Do **not** edit `docs/m29/M29_LOG.md` (it is a record).
- **Steps**:
  1. `grep -rn 'minCOLS\|min-geometry\|COLS < 6' docs AssetEcosystem.md CHEATSHEET.md SDK.md GAME_DEV_GUIDE.md` and list every hit.
  2. Mark the ledger line **RETRACTED** and edit each hit to point at the new note.
  3. Write the note: symptom, root cause, evidence (in a failing build `--vars cells` reads 0 at frame 0 because the fill loop is absent; adding `__asm__ volatile("" ::: "memory")` before the first command makes 4x3, 5x1 and 1x8 maps draw), the fix commit, the pinning test, and the lesson: *a diagnosis that depends on code layout rather than on input values is a compiler or ABI suspect*.
- **Acceptance**: step-1 grep returns only the retraction note and this roadmap; `make check` green.

### Card A1 — symbols from `arcade1 build`, auto-loaded, with a build-id check

- **Why**: `--vars` needs a symbol file, but the docs say `build` leaves only the image. The round-2 agent had to invent a register-parking hack. Wrong symbols made `--vars` print plausible garbage with no error.
- **Files**: `tools/arcade1.c` (`cmd_build`, `run` option handling for `--syms/--vars`), `tests/test_build.sh`, NEW `tests/test_syms.sh`, Makefile target `test-syms` appended to `CHECK_TARGETS`, `SDK.md`, `GAME_DEV_GUIDE.md`.
- **Contract**:
  - `arcade1 build game.c [game.arc1]` also writes `game.syms` beside the image. Lines are `nm` format (`ADDR TYPE NAME`), RAM-resident objects only (addresses inside the RAM window). The first line is `# arc1-syms crc32=0x<image payload crc>` using the CRC stored in the `.arc1` header.
  - Symbols come from the same toolchain's `nm` (`riscv64-unknown-elf-nm`). If it is missing, the build still succeeds, writes no `.syms`, and prints `arcade1: warning: nm not found, no .syms written`.
  - No other file (ELF, objects) is left next to the image.
  - `run`, `test` and `inspect`: if `--vars` is given without `--syms`, auto-load `<image minus .arc1>.syms`. If absent, exit 2 with: `no symbols for game.arc1: build with 'arcade1 build' (writes game.syms) or pass --syms FILE`.
  - If a syms file carries the header and its CRC differs from the image: exit 2 with `syms file belongs to a different build (syms crc 0x… vs image 0x…); rebuild or pass the matching file`.
  - Header-less syms files (`tests/roms/*.syms`) keep working unchanged.
  - A name whose address is outside the RAM window: warn on stderr and drop it (not fatal).
- **Acceptance** (`tests/test_syms.sh`, in a clean temp dir containing only the `arcade1` binary):
  1. build a game with globals; `game.syms` exists, header CRC equals the image header CRC;
  2. `arcade1 run game.arc1 --frames 3 --vars score` (no `--syms`) prints the right value;
  3. a syms file from build X used with image Y → exit 2 and the message above;
  4. a header-less syms file still works;
  5. `make check` green; `git diff --stat -- tests/golden tests/roms` empty.
- **Size**: ~3 h.

### Card A2 — build output-name discipline

- **Contract**: `arcade1 build src.c out.ext` with `out.ext` not ending in `.arc1` → exit 2 `output must end in .arc1 (got 'out.ext')`. If the output is omitted, the default is `<source stem>.arc1` in the current directory (replaces `program.arc1`). The final line reports every file written: `arcade1: built game.arc1 (+ game.syms)`.
- **Files**: `tools/arcade1.c`, `tests/test_build.sh` (update the default-name expectation), `SDK.md`, `GAME_DEV_GUIDE.md`.
- **Acceptance**: tests for both rules; `make check` green. **Size**: ~1 h.

### Card A3 — PNG captures and `--scale`

- **Contract**:
  - `--capture FILE` picks the format by extension (`.png` or `.ppm`). `--capture-dir DIR` writes `frame_%04u.<ext>` using `--format png|ppm` (default `ppm`, so existing behaviour is unchanged).
  - `--scale N` (integer 1-8, nearest-neighbour pixel replication) applies to both formats. `--scale 4` on a 480x270 frame gives exactly 1920x1080.
  - PNG writer in C, no dependencies: 8-bit RGB (colour type 2), filter byte 0 per row, one zlib stream of **stored** deflate blocks (each at most 65,535 bytes), Adler-32 and CRC-32 computed by hand.
  - Document that `--frames N` captures the framebuffer after frame `N-1`.
- **Files**: `tools/arcade1.c`, NEW `tests/test_capture_png.sh` (+ Makefile target, appended to `CHECK_TARGETS`), `SDK.md`.
- **Acceptance**: the test decodes the PNG with python `zlib`+`struct` and compares every pixel with the PPM of the same frame at scale 1; at scale 4 checks 1920x1080 and pixel replication; two runs produce identical bytes; `make check` green. **Size**: ~2 h.

### Card A4 — `--vars all`

- **Contract**: remove the silent 1024-name cap (it prints every name in the symbol file). Keep the spelling `all`. Docs say "all names in the symbol file", not "all RAM globals".
- **Files**: `tools/arcade1.c`, `tests/test_run.sh` (a synthetic 3,000-name syms pointing at RAM addresses prints 3,000 lines), `SDK.md`.
- **Acceptance**: that test; `make check` green. **Size**: ~30 min.

### Card A5 — docs pass

- **Add, as plain prose** (outside ```` ```c ```` fences unless the snippet compiles under guest flags; `test-cheatsheet` compiles every c block):
  1. "The screen is always 480x270. There is no scaling or video-mode register. For a larger image upscale a capture (`--capture f.png --scale 4` gives 1920x1080)."
  2. A frame ends at the PRESENT command; without it the guest runs to the instruction cap and looks frozen.
  3. Capture semantics (`--frames N` captures frame `N-1`), the default output name, and that `build` writes `game.syms`.
  4. "State you want to observe must be a file-scope global. Its address comes from `game.syms`, never from declaration order."
  5. `arcade1 inspect` exists (updated by Wave B).
  6. `arcade1_rng` is consumed in program order, so balance changes re-route the stream; its low bits are weak; use `arcade1_rand` (card A6).
- **Files**: `CHEATSHEET.md`, `SDK.md`, `GAME_DEV_GUIDE.md`. **Acceptance**: `make check` green (cheatsheet snippets still compile). **Size**: ~1 h.

### Card A6 — SDK helpers: `arcade1_rand`, `arcade1_pixel`, `text.h`

- **Constraints**: opt-in only. **No existing guest's compiled bytes may change** (`git diff` of built artifacts of pre-existing guests is empty). Do not change `arcade1_rng`; goldens depend on it.
- **Contract**:
  - `arcade1_rand(n)` in `programs/arcade1.h`: returns a value in `[0, n)` using the **high** bits of the LCG state.
  - `arcade1_pixel(x, y, color)`: a barrier-correct wrapper over the existing SET_PIXEL command (screen space).
  - NEW `programs/text.h` (opt-in include): a 5x7 font for ASCII 0x20-0x5F stored as 8 bytes per glyph in ROM; `text_draw(x, y, str, color)` and `text_u32(x, y, value, digits, color)`, using the 1-bit SPRITE command per glyph. Document that SPRITE is world space: call `arcade1_cam(0, 0)` first for a HUD. Games that do not include `text.h` pay no ROM.
- **Files**: `programs/arcade1.h`, NEW `programs/text.h`, NEW `tests/guests/text_probe.c` + `tests/test_text.sh` (+ Makefile target), `CHEATSHEET.md` (one compiled snippet).
- **Acceptance**: the probe draws `A0` at a known position. The test asserts the exact pixel pattern of those two glyphs against a hand-written oracle inside the test (not derived from `text.h`), and that nothing is drawn outside the 8x8 cells. `arcade1_rand(3)` over 3,000 calls puts each bucket within 10-40 %. Older guests byte-identical; `make check` green. **Size**: ~3 h.

**Wave A report must state**: the `git diff` over pre-existing guest artifacts is empty; time of `make check` before and after.

## 4. Amendments to the creation-flow roadmap

### 4.1 Wave 0 (golden conditions) — compress and move after Wave 1

Do cards 0.1 (manifest) and 0.3 (a short run must fail), merge the teaching hint of 0.2 into 0.3, and keep 0.4's tests. Total about 3 hours. No other change.

### 4.2 Wave 1 (`arc1-assetc` v0) — keep, with these additions

- **Card 1.2 addition — automatic palettes.** A sprite may declare `"palette": "auto"` plus a `"bank"`. The tool collects the distinct opaque RGB565 colours in order of first appearance (row-major over all frames) and assigns indices 1-15. More than 15 → teaching error: `sprite player: 19 colours (max 15 + transparent) — reduce colours, use "quantize": "nearest" with an explicit palette, or split the sheet`. This matches how real art arrives (an indexed PNG of at most 16 colours) and removes the hand-written palette entry per sprite.
- **Card 1.3 addition — optional `"quantize"`.** Per asset: `"exact"` (default, as proposed) or `"nearest"` (minimum squared RGB distance to the palette, ties to the lowest index). `--report` prints `QUANT player maxerr=N pixels=M` for nearest. Exact stays the default so mistakes are loud.
- **Provenance rule (unchanged, binding)**: committed generated headers are the build truth; the tool is the master; output is byte-deterministic.
- **Art conventions to document in `GAME_DEV_GUIDE.md`**: sheets are horizontal strips, even width per frame, one transparent colour (alpha 0, or a declared key colour), at most 15 colours plus transparent per palette bank.

### 4.3 Wave 4 (map import) — remove the min-geometry validator

- Delete the validator that errors on `COLS < 6` with `ROWS ≤ 4`. The rule is false (card A0).
- **Replace with**: a fixture that imports a 4x3 map and a 5x1 map and asserts they **draw** (expected cell array computed independently, then the same pixel-count assertion style as `tests/test_barrier.c`). Keep the even-`tile_w` validator and the empty-map error.
- If a future card adds direct register pokes in generated code, they must go through the SDK helpers (which are compiler barriers).

### 4.4 Wave 5 (Game #1) — extra acceptance

1. 480x270; a scrolling level imported by Wave 4; an animated player with at least three states (idle, run, one more) from Wave 3; at least two enemy types; HUD with `text.h`.
2. Score, lives, win and lose states, and START restart, all provable through named variables with scripted input (the M29 style: score rises, game over, restart).
3. Art: from the owner's designer if provided (indexed PNG, at most 16 colours per sprite, even widths). If not available, committed generator-made placeholder art, clearly marked in the report.
4. Golden added by the deliberate procedure (new files only), input script committed, `docs/experiments/game1.md` with the metric (empty directory → playable `.arc1`, wall-clock time, count of hand-written byte tables, `make check` time).
5. `arcade1 build` of the game works from a clean directory with only the tool and the assets (Wave A's self-contained gate).

### 4.5 Play window — unchanged, optional

SDL3 optional, exit 3 when absent, not in `make check`, scripting and `--live` share no mutable state. Its only reason to exist is recording a human session as an input script. It is not part of the AI flow.

## 5. Wave B — `arcade1 inspect` v1

**Why**: today `inspect` prints 32 registers and is undocumented. The M29 round-1 agent spent about 140 probe builds on a draw-nothing failure that a one-call report ("last TILEMAP4: cols=4 rows=3 map=0x…; first cells: 0 0 0 0") would have diagnosed. All the underlying counters already exist host-side.

### Card B1 — structured report

- **Contract**: `arcade1 inspect <image> [--frames N] [--input F] [--syms F|auto] [--vars LIST] [--json] [--regs]` runs N frames and reports the **last frame**. Default output is greppable `KEY value` lines; `--json` is one object with the same keys; the register dump moves behind `--regs`.
  ```
  FRAME 120
  PC 0x20000ABC
  INSTRET_FRAME 8123
  STRAY 0
  VIDEO_CMDS CLEAR=1 FILL=40 SPRITE=12 TILEMAP=1 SPRITE4=3 TILEMAP4=1 PRESENT=1
  VIDEO_PIXELS_WRITTEN 91234
  VIDEO_OOB_DRAWS 0
  ROM_USED 3084 / 262144
  VAR score 120
  ```
- **Rules**: host-side only (no MMIO, no guest-visible change, nothing in any hash). Exit codes 0 ok, 2 usage. Document in `SDK.md` and `CHEATSHEET.md` prose.
- **Files**: `tools/arcade1.c`, NEW `tests/test_inspect.sh` (+ Makefile target), docs.
- **Acceptance**: on `tests/roms/m29game.arc1`: every key present; values agree with `arcade1 run --strict` for the same run; `--json` parses with `python3 -m json.tool`; all older goldens unchanged; `make check` wall time grows by no more than 5 %.
- **Size**: ~3 h.

### Card B2 — command log and pixel provenance

- **Contract**: when (and only when) `inspect` asks, the video device records, for the last frame, up to 4,096 command records `{id, type, x, y, w, h, addr, flags, camera, pc_of_issuing_store, px_written, clipped, oob}`. `inspect --commands` prints them in order. `inspect --at X,Y` prints the id and record of the **last command that wrote that pixel** (a per-pixel 16-bit id buffer allocated only under inspect).
- **Rules**: zero cost and zero hash change when disabled. The machine exposes the PC of the executing store to the host-side device hook; no guest-visible change.
- **Acceptance**: a test guest draws a sprite, then a FILL covering it. `--at` on a covered pixel names the FILL; `--commands` lists both in order with PCs inside the guest function's symbol range; hashes of runs with and without `--commands` are identical; goldens untouched; `make check` green.
- **Size**: ~1 day.

### Card B3 — source line mapping (optional, later)

`arcade1 build --debug` keeps `<game>.elf`; `inspect` maps a PC to `file:line` with `riscv64-unknown-elf-addr2line`. Do only if B2 proves valuable.

## 6. Wave 6 — placeholder (do not start before Game #1)

After Game #1, decide **from its report**, not from imagination: Audio v2 (several voices, a sound-effect asset type), any evidence-based machine change (ROM or RAM sizes), the per-frame work budget and the 1,000,000-step cap (still owner decisions, to be taken with the FPGA milestone). Follow the rule in `AI_NATIVE_DEVELOPMENT.md` §17: first ask whether the problem belongs to game code, SDK, tooling or reference VM; only then change the machine.

## 7. Open decisions for the owner (do not decide for them)

1. `"quantize": "nearest"` in v0 (added in §4.2) or exact-only until real art arrives.
2. `arc1-assetc` stays Python (fast iteration) or moves into the C `arcade1` binary later (one tool for fresh agents).
3. The built-in font (card A6): the 5x7 default is a proposal; a different look is a taste decision.
4. Whether the play window is wanted now.
5. When to start Audio v2.

## 8. First actions for the implementing AI

1. Read `§0-§2`, then `docs/m29b/M29_LOG.md` (the evidence for Wave A).
2. Execute **A0, A1, A2, A3, A4, A5, A6** in order. Stop and report.
3. Wait for the owner's go before Wave 1.
