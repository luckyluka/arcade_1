# ARCADE-1 Roadmap V4 — fixes, an AI art pipeline, audio observability

| | |
|---|---|
| Purpose | Continue after `ROADMAP_V3.md` (Waves A-6 executed). Fix what rounds 3 and 5 found, and **solve art without a human artist**: a generator pipeline an AI agent can drive. Audio observability follows. |
| Audience | The implementing AI agent. Read §0-§2 before any card. |
| Facts verified | 2026-10-20 at HEAD `711a2e4` by an independent reviewer: clean rebuild, zero warnings, `make check` exit 0 (200 ok lines, ~57 s), `src/` untouched since the last review, hardening cards effective, clean-room Flappy passes, Petal Blade plays and captures at 1080p |
| Art premise | **No human artist is available.** The owner's partner art is not coming. All art must be producible by an AI agent, deterministically, inside the repo. |

## 0. How to work (binding — unchanged from V3, restated)

1. One card = one commit; the message says what and why.
2. Run the card's acceptance commands and read the output. Paste key lines in your report.
3. Before every commit: `make clean && make check` exits 0 with **zero compiler warnings**.
4. **Existing goldens and ROMs are untouchable.** `git diff --stat -- tests/golden tests/roms` is empty unless a card says "adds new files". Never run `make roms-update` or `make golden-update` inside a tooling card. A new game adds only its own files.
5. **Dependencies:** host `cc` and `python3` **stdlib only** for everything built or tested by `make check`. The single exception is the optional offline tool in card 7.5 (see §6, decision 1): it may use Pillow, must never be required by `make check`, and its tests exit 77 ("skipped") when Pillow is absent.
6. **No machine behaviour changes.** `src/` may gain host-side diagnostics only (cards 4.4, 8.1), with zero effect on any hash. If a card seems to need more, stop and report.
7. **A card's premise may be wrong.** If a measurement contradicts it, stop, write `docs/experiments/<topic>.md`, report. (The V3 min-geometry retraction is the precedent.)
8. **Findings are deliverables.** Surprises go to `docs/experiments/` in the same commit.
9. **Stop after each wave** and report: cards done, acceptance output, surprises, not-done items.
10. **Scope guard.** New ideas go to `FUTURE_IDEAS.md`, not into the current card.
11. **Determinism.** Every generator, converter and encoder is byte-deterministic: no wall-clock, no unseeded randomness, no dict-order dependence, sorted iteration.

## 1. State at the start of V4

- V3 Waves A, 1, 0, B, 2, 3, 4, 5 and the play window are done. Hardening (memcpy/memset/memmove in the guest runtime, `anim_tick` 0 ms guard, input-script examples) is done.
- Game #2 (Flappy) came from a **clean-room** agent and was promoted. Friction found there was documentation only.
- Petal Blade (`round5/`) proves 480x270 can look modern, but its art is a 678-line procedural generator, it has **no test, no golden, nothing in `make check`**, and its repo footprint is 22 MB (1080p PNG captures are 6.2 MB each because the PNG writer stores uncompressed).
- Measured on Petal Blade: 3.3k-4.5k instructions per frame, **334k-352k pixels written per frame (2.6-2.7x the screen)**, ROM 60 KB of 256 KB. CPU is irrelevant; device pixel work dominates. `DEVICE_BUDGET.md` was measured at 320x240 and is stale.
- Still deferred (owner decisions, not part of V4): the 1,000,000-step cap contract, the per-frame work budget, the machine freeze.

## 2. Order of work

| Order | Wave | Deliverable | Rough size |
|---|---|---|---|
| 1 | **7a** | Doc and trap fixes, compressed PNG, collision layer, promotion and hygiene, budget re-measure | 2-3 days |
| 2 | **7** | **AI art pipeline**: palettes, drawing library, text sprites, rig animator, raster ingest, QA, art pass, clean-room art round | 5-8 days |
| 3 | **8** | Audio observability (WAV dump, live audio), Audio v2 decision | 1-2 days |
| 4 | **9** | Freeze-readiness dossier (evidence for the owner's decisions) | 1 day |

## 3. Wave 7a — fixes and hygiene

### Card 4.1 — `arcade1 info`: one source of truth for machine constants

- **Why**: a brief said 320x180 while the repo said 480x270; a newcomer had no rule for which to trust.
- **Contract**: `arcade1 info [--json]` prints, from the **same macros the VM is compiled with**: screen size, framebuffer format, memory map (ROM/RAM/MMIO bases and sizes), instruction cap, tick rate, embedded SDK header list (`arcade1.h`, `gfx.h`, `arcade1_map.h`, `asset.h`, `text.h`, … enumerated from the embedded set, not hand-typed), CLI version string.
- **Docs**: add the **trust rule** to `GAME_DEV_GUIDE.md` and `CHEATSHEET.md`: *"If a brief, a tutorial or a model disagrees with `arcade1 info`, `arcade1 info` is right."*
- **Files**: `tools/arcade1.c`, NEW `tests/test_info.sh` (+ Makefile target in `CHECK_TARGETS`).
- **Acceptance**: `--json` parses with `python3 -m json.tool`; a tiny C test asserts every numeric field equals the corresponding macro; header list equals the embedded set. **Size**: ~2 h.

### Card 4.2 — golden authoring from the CLI

- **Why** (round 3 findings 2, 3, 4, 7): the guide only documents `make golden-update`; the file grammar lives in source.
- **Contract**: `arcade1 golden game.arc1 --input F --frames N --out file.txt` writes frame lines only (no summary line). `SDK.md` states the grammar: lines not starting with `frame ` are ignored; a golden shorter than the run is checked over its own length; a run shorter than the golden's first pass is reported (and is **not** a pass).
- **Acceptance**: `golden` then `test --golden` round-trips on `tests/roms/flappy.arc1`; a tampered golden exits 1 and names the first differing frame; docs updated. **Size**: ~2 h.

### Card 4.3 — pad trace and the edge-trigger trap

- **Why** (round 3 finding 6): a held button across a restart silently eats the next edge; the session diverges ~40 frames later.
- **Contract**: `arcade1 run --pad-trace FILE` writes one line per frame: `frame N pad 0xXX` (the applied pad). `SDK.md` and `GAME_DEV_GUIDE.md` get a worked example titled "Edge-triggered buttons and input scripts" showing the trap and the fix (release for one frame).
- **Acceptance**: trace equals the script's expansion on `tests/inputs/flappy.txt`; example in the docs actually reproduces the divergence (test it); `make check` green. **Size**: ~2 h.

### Card 4.4 — `arcade1_rgb()` and colour-truncation diagnostics

- **Why** (round 5 finding 1): helpers take RGB565; a 24-bit literal is truncated silently (it bit twice).
- **Contract**:
  - `arcade1_rgb(r, g, b)` in `programs/arcade1.h` (opt-in; no existing guest's bytes change): returns `(r>>3)<<11 | (g>>2)<<5 | (b>>3)`.
  - Host-side only: the video device counts COLOR-register writes with any bit above 15 set. `inspect` prints `VIDEO_COLOR_TRUNC N`; `arcade1 run` warns when N > 0; `--strict` fails (exit 1).
  - `CHEATSHEET.md` states "colours are 16-bit RGB565; use `arcade1_rgb`".
- **Acceptance**: a test guest writing `0xE8C0A0` gives `VIDEO_COLOR_TRUNC >= 1` and a `--strict` failure; a clean guest silent; all hashes unchanged; goldens untouched. **Size**: ~3 h.

### Card 4.5 — compressed PNG output

- **Why**: V3 card A3 specified stored (uncompressed) deflate to avoid dependencies; 1080p captures are 6.2 MB each and bloat the repo.
- **Contract**: PNG encoder gains real compression with no dependencies: LZ77 (32 KB window, hash chains, deterministic tie-breaking) with the **fixed Huffman** table, one stream. `--png-store` keeps the old mode (tests use it as an oracle).
- **Acceptance**: python `zlib` decodes the output and every pixel equals the PPM for the same frame; two runs are byte-identical; report the size of one 1080p Petal Blade frame before and after (expect at least 8x smaller; report the real number); `test_capture_png.sh` extended; `make check` time grows by at most 5 %. **Size**: ~4 h.

### Card 4.6 — collision layer in map import

- **Why** (round 5 finding 3): assetc deduplicates tiles, so a cell index is not a tile type; every game wrote a parallel collision generator.
- **Contract**: a map in `assets.json` may declare `"solid"`: either a second ASCII layer of the same size (`#` = solid, `.` = empty, optional `^` = hazard) or a per-tile-type table. assetc emits `<MAP>_solid[]` (one byte per cell, 0 empty, 1 solid, 2 hazard) and `<MAP>_SOLID_COLS/ROWS` (same prefix rule as the other map defines). `programs/asset.h` gains `map_solid(map, tx, ty)` (out of range returns 1 = solid).
- **Acceptance**: fixture 4x3 and 5x1 maps (they must also **draw**, per the V3 retraction) with solid layers; dimension-mismatch error teaches; `asset.h` accessor test via a guest; assetc output byte-deterministic; older generated headers unchanged. **Size**: ~4 h.

### Card 4.7 — promote Petal Blade, clean the repo

- **Contract**:
  1. Promote Petal Blade as **Game #3** exactly like Flappy: `programs/petalblade.c` (+ generated headers kept as build truth), `tests/inputs/petalblade.txt` (a scripted run reaching `state == 1`, plus a death and a restart), `tests/test_petalblade.sh` (determinism, story through `--vars`, record-replay), `tests/roms/petalblade.{arc1,syms}`, `tests/golden/petalblade.txt`, wired into `GUEST_PROGS`, `GOLDEN_GAMES`, `GOLDEN_IMGS` and `make check`. Adds new files only.
  2. Repo hygiene: remove committed preview and capture PNGs larger than 500 KB (`round4/smoke.png`, `round5/final*.png`) and add a `.gitignore` rule for `round*/**/*.png` over that size and for `captures/`; keep sources and small previews.
  3. The four "Firefly_…" JPG reference sheets: **move them out of the repo** and add `docs/art/REFERENCES.md` recording their provenance as known (tool, date, prompt if known). The owner confirms what stays public (see §6, decision 3). They remain in git history.
- **Acceptance**: `make check` green with the new test; `git diff --stat -- tests/golden tests/roms` shows only added files; repo size reduction reported (`du -sh .git` and working tree before/after). **Size**: ~4 h.

### Card 4.8 — re-measure device work at 480x270 (measurement only)

- **Contract**: NEW `tools/measure_budget.sh` runs `arcade1 inspect` per frame (or a `--frames` sweep) over every golden game plus Petal Blade and writes `docs/experiments/budget-480x270.csv`: per game, avg/max of instructions, pixels written, commands, **overdraw ratio** (pixels written / 129,600). Update `DEVICE_BUDGET.md` header with "measured at 480x270 on <date>" and a table from the CSV. **Do not propose or implement a cost model.**
- **Acceptance**: script deterministic (two runs identical); numbers match `inspect` for three spot-checked frames; no source changes. **Size**: ~3 h.

### Card 4.9 — hygiene

- NEW `AGENTS.md` (≤ 80 lines): reading order, canonical docs (CHEATSHEET = guest API, SDK = CLI, GAME_DEV_GUIDE = workflow, SPEC = machine), where tests and goldens live, the rules of §0.
- Add a status table to `ROADMAP_V3.md` and this file (card, commit hash, date).
- `make check` wall time recorded in `PROGRESS.md` with a budget of 120 s; if exceeded, report before adding tests.
- **Acceptance**: `AGENTS.md` links resolve (a test greps every relative link); `make check` green. **Size**: ~2 h.

**Wave 7a report must state**: PNG size before/after, repo size before/after, `make check` time before/after.

## 4. Wave 7 — the AI art pipeline

### 4.0 Design (read before the cards)

**Premise.** Image-generation models do not produce pixel-exact art or consistent animation frames. A pipeline that depends on dropping their output straight into the machine fails (the round-5 reference sheets were scanned/AI sheets with more than 15 colours, not strip-shaped). Therefore **assets are split by kind**, each with the method that actually works for an agent:

| Asset kind | Method | Why |
|---|---|---|
| Animated characters (idle, walk, run, jump, attack…) | **Rig animator** (card 7.4): parts, joints, keyframes, rasterized with a fixed palette | Frame-to-frame consistency by construction |
| Small sprites, icons, items, UI, tiles | **Text pixmaps `.px`** (card 7.3) written by the agent | LLMs write small pixel grids well; diffable; previewable |
| Procedural scenery (parallax strips, skies, terrain textures) | **Drawing library** (card 7.2) scripts, like `gen_art.py` | Already proven by Petal Blade |
| Backgrounds, props, concept art from an image model | **Raster ingest** (card 7.5) with deterministic slicing, keying, downscale, quantization | Useful for *static* assets and as style reference; not for animated characters |

Everything ends in the same place: strip PNGs plus an `assets.json` consumed by the existing `arc1_assetc.py`. **assetc stays the only path into the game.**

**Standing rules for all art code**: palette index 0 is transparent; at most 15 colours plus transparent per bank; palette entries distinct as RGB565; one palette bank per asset group; frames of one state share size and baseline; every output file is regenerated by one command and never hand-edited; provenance is recorded per asset (tool, model/prompt/seed where applicable, licence).

### Card 7.1 — workspace, palette presets, style guide, provenance

- **Files**: NEW `art/README.md`, `art/palettes/*.json` (presets: `neutral16`, `warm16`, `cool16`, `dusk16`, `forest16`, `ui16`; each exactly 16 entries, entry 0 transparent, 15 colours distinct as RGB565, hand-built ramps with a dark outline colour at a fixed index), `art/STYLE_TEMPLATE.md` (fields: sprite grid and sizes, outline rule, light direction, shading steps, palette, silhouette rules, animation frame counts), NEW package `tools/arc1_art/` (empty modules for 7.2-7.6), `docs/art/PROVENANCE.md` (schema).
- **Provenance schema**: optional `"provenance": {"tool", "model", "prompt", "seed", "licence"}` per asset in `assets.json`; assetc ignores it for output but validates the keys; `arc1_assetc.py --require-provenance` fails when absent; NEW `art/ART_LICENSES.md` is generated from the manifest.
- **Acceptance**: `tests/test_art_palettes.sh` validates every preset (16 entries, entry 0 transparent, distinct RGB565, outline index present); a lint for `STYLE_TEMPLATE.md` fields; assetc tests still green; older generated headers byte-identical. **Size**: ~4 h.

### Card 7.2 — drawing library (extract, do not rewrite)

- **Contract**: extract the reusable canvas/primitives from `round5/gen_art.py` into `tools/arc1_art/canvas.py` **without changing any output byte** and re-point `gen_art.py` at it.
- **API** (deterministic, stdlib-only): `Canvas(w, h, palette)`, `px`, `line`, `rect`, `ellipse`, `polygon`, `flood`, `outline(color, neighbours=4|8)`, `ramp_shade`, `ordered_dither(bayer=2|4)`, `mirror_h`, `rotate90`, `stamp(src, x, y)`, `recolor(map)`, `Rng(seed)` (xorshift32, documented), `to_strip(frames)`, `contact_sheet(items, scale, labels)`, indexed-PNG writer.
- **Acceptance (regression-first)**: re-running `round5/gen_art.py` through the library regenerates `assets.json` and every strip **byte-identical** to the committed ones (`sha256` list in the test); unit tests per primitive with hand-computed pixel expectations (e.g. a 5x5 ellipse, 3x3 outline); determinism (two runs identical). **Size**: ~1 day.

### Card 7.3 — text pixmaps `.px` and the sprite lab

- **Format**:
  ```
  # name: coin   size: 8x8   palette: warm16   bank: 3
  ..1111..
  .122221.
  12333321
  ...
  --
  (next frame, same size)
  ```
  `.` = transparent, `0-9a-f` = palette index, `--` separates frames, `#` lines are headers (`name`, `size`, `palette`, `bank`, optional `anchor: feet|center`).
- **Tools**: `arc1-art px2assets file.px... --out-dir art/build/` (writes the strip PNG and an `assets.json` fragment), `arc1-art sheet file.px --scale 8 --out preview.png` (grid, frame numbers, 1x and 8x).
- **Teaching errors**: `coin.px:14: row has 7 pixels, expected 8`; `coin.px:3: unknown character 'g' (use . or 0-9a-f)`; `coin.px: frame 2 is 8x7, expected 8x8`.
- **Acceptance**: round trip `.px` -> strip -> `arc1_assetc.py` -> header -> a guest that draws it (pixel-count assertion); each error message pinned by a test; five sample sprites (coin, heart, key, potion, arrow) committed as `art/px/*.px` with provenance `{"tool":"hand-authored by agent"}`; deterministic. **Size**: ~1 day.

### Card 7.4 — rig animator (characters)

- **Why**: the way to get *consistent* animation frames without an artist.
- **Model**: a character is a list of **parts** (name, parent, pivot offset, shape: capsule / rect / ellipse / polygon given in part-local coordinates, size, colour index, z-order) and a **pose** (per part: angle in whole degrees, optional integer offset). Animations are keyframe lists with linear interpolation and **integer rounding rules documented** (round half away from zero). Facing is a mirror, not a re-draw.
- **Output**: per state one horizontal strip, every frame the same size, feet on the same row, horizontal anchor at the same column; `assets.json` fragment with `anims` and a **computed** `collision` box (union of opaque pixels of the idle frame, written into the fragment). A fixed outline pass (palette outline index) runs last.
- **Presets** in `art/rigs/humanoid.json`: idle (4), walk (8), run (8), jump (1), fall (1), attack (4), hurt (1), dead (1). One weapon part is optional. Parametric knobs: height (24-48 px), palette, proportions (head ratio, limb length).
- **Acceptance**:
  1. determinism (two builds identical);
  2. within one state all frames have the same size and the lowest opaque pixel on the same row;
  3. across a walk cycle the opaque-pixel count of any frame is within ±25 % of the cycle mean (consistency);
  4. every frame uses at most 15 colours plus transparent;
  5. the contact sheet is written and **the implementing agent reviews it with the card 7.6 checklist at 1x and 4x**, recording the verdict in `art/reports/humanoid.md`; a second review item states what looked wrong and what was changed.
- **Risk to report honestly**: capsule rasterization at 28-40 px height can look crude. Allowed mitigations: a snap-to-pixel pass, a 1-px outline, and light shading ramps. Do not add machine features.
- **Size**: ~2 days.

### Card 7.5 — raster ingest for image-model sheets (optional offline tool)

- **Why**: image models are good at *static* props, backgrounds and style references. This tool turns a messy PNG/JPG sheet into compliant strips.
- **Dependency**: Pillow, **only here** (§6 decision 1). Tests exit 77 when Pillow is missing; `make check` must stay green without it.
- **Pipeline** (every step deterministic, ties broken by sorted order):
  1. load; `--grid CxR` or `--cell WxH` slicing;
  2. background key: the most common corner colour with `--tolerance`, flood-fill from the border; hard alpha (0/1); remove isolated opaque pixel groups smaller than `--min-blob`;
  3. crop each cell to its content, pad to a common frame size, align to the anchor (`feet` = bottom-centre of the content, or `center`);
  4. resize to the target frame height with an **area-average** filter (box), width by aspect;
  5. quantize with **median cut** (fixed split order and tie-breaks) to a palette **shared by all frames** of the sheet, at most 15 colours plus transparent; `--palette preset.json` forces nearest-colour mapping to a preset; no dithering unless `--dither bayer2|bayer4`;
  6. optional `--outline INDEX` pass; remove orphan pixels;
  7. write the strip PNG, an `assets.json` fragment, a before/after contact sheet, and `report.json` (colours used, per-frame bounding-box jitter, orphan count, blobs removed).
- **Acceptance**: tests synthesize an "AI-like" messy sheet (soft edges, gradient background, noise) using card 7.2's library — **no third-party images committed**. After ingest: ≤ 15 colours; feet baseline identical in all frames; orphan count 0; two runs byte-identical; `report.json` keys present; teaching errors for non-integer grids and sheets with too few opaque pixels.
- **State clearly in `docs/art/INGEST.md`**: ingest fixes alignment, background and palette; it cannot fix drawing inconsistency between frames. Use it for static assets and as reference. **Size**: ~2 days.

### Card 7.6 — visual QA: automated checks and a critique protocol

- **Tool**: `arc1-art check file.png [--sheet]` emits a JSON report and exits 1 on failures. Checks: colour count, orphan pixels, silhouette connectivity (one connected component per frame, with an allowance flag), baseline consistency, per-frame bounding-box jitter, outline coverage ratio, palette distinctness as RGB565, readability (the frame bounding box is at least 8 px tall), fill-to-outline luminance contrast.
- **`art/CRITIQUE.md`**: a checklist for an AI with vision to review a contact sheet at 1x and 4x: silhouette readable at 1x, one light direction, palette harmony, outline consistency, animation timing and arcs, anything that reads as noise. Each review is written to `art/reports/<asset>.md` as: verdict, three observations, changes made, re-check result.
- **Acceptance**: fixture sprites (one good, three deliberately bad: too many colours, jittery baseline, orphan pixels) produce the expected failures and messages; the sample `.px` sprites and the humanoid rig all pass. **Size**: ~1 day.

### Card 7.7 — art pass on a real game (evidence)

- **Task**: rebuild **Petal Blade's hero** with the rig animator (state set: idle, walk, jump, fall, attack, hurt, dead) and add **five props/icons** from `.px` files. Keep the procedural parallax and tiles. Use the owner's reference sheets only as *style guidance* in the rig parameters.
- **Rules**: build with one command (`make art-petalblade` or a script) producing byte-deterministic assets; the game becomes a **new variant** (`petalblade_rig`) with its own golden; the promoted Petal Blade golden stays untouched.
- **Deliverable**: `docs/experiments/art-pass.md` with 4x before/after screenshots (compressed PNG), time spent, number of assets, ROM bytes, number of hand-tuned numbers in the rig, and an honest verdict: *does the rig hero look as good as the procedural one? Where is it worse?* **Size**: ~2 days.

### Card 7.8 — clean-room art round (the acceptance test of Wave 7)

- **Procedure** (the operator runs it; the implementing agent prepares the folder): a **fresh agent with no context** gets only `AGENTS.md`, the art docs, the tools and one brief: *"A fantasy village with a farmer, a slime enemy, 4 pickups and a 3-tile ground. Produce a playable 1-screen game including its art, using only the repo tools."* It keeps `ROUND_LOG.md` with every attempt, every DOC GAP, and the time to the first compliant asset.
- **Success metrics** (record, do not tune afterwards): attempts to first compliant strip; number of DOC GAPs; whether `art/CRITIQUE.md` was used; whether any hand-edited generated file exists (must be none); `arc1-art check` clean on all assets; game golden-locked via card 4.2.
- **Deliverable**: `docs/experiments/round6-art.md` with the classification of every friction item (docs / tool / pipeline / game). If the agent produced something unattractive but compliant, say so; compliance is the test, taste is a separate section.

**Wave 7 report must state**: results of 7.7 and 7.8, honest quality verdicts, and which asset kinds from the 4.0 table each method actually served.

## 5. Wave 8 — audio observability (small)

### Card 8.1 — deterministic WAV dump

- **Contract**: `arcade1 run --audio out.wav` writes the audio the audio device produces: 48 kHz, 16-bit, stereo, 800 samples per frame, from the existing mixing model (no new audio features). A header-only WAV writer in C.
- **Acceptance**: a guest that enables a 440 Hz tone yields a WAV whose zero-crossing count over one second is 880 ±2; two runs byte-identical; the WAV length equals frames × 800 samples; hashes unchanged. **Size**: ~2 h.

### Card 8.2 — audio in the play window (optional)

- `--live` plays the same samples via SDL3 audio when SDL3 exists (exit 3 otherwise, as today). Not part of `make check`; `docs/experiments/playwindow.md` records what it taught. **Size**: ~3 h.

### Card 8.3 — Audio v2 decision brief (no implementation)

- After 8.1: write `docs/experiments/audio-v2-brief.md` containing a **sound list** for Petal Blade (jump, slash, coin, hurt, win), the minimum machine features each would need (voices, waveforms, envelopes, sample playback), a ROM/RAM estimate, and 2-3 options with consequences. **Stop. The owner decides.**

## 6. Wave 9 — freeze-readiness dossier (no machine changes)

- `docs/experiments/freeze-dossier.md`: one table of every machine-level question seen so far (cap contract, work budget with the 480x270 numbers from card 4.8, memory sizes, audio v2, resolution), with: evidence, decision needed, options, recommendation, who decides. The agent recommends; the owner decides.
- Games in evidence by then: Breakout, Tetris, Platformer, Asteroid Run, Flappy, Petal Blade (+ the clean-room art game). If fewer than three genres show a need, say so.

## 7. Decisions for the owner (TAKEN 2026-10-21)

1. **Pillow: NO — stdlib only.** The ingest tool (card 7.5) reads PNG and
   nothing else; zlib is in the standard library, so no new dependency and
   no JPG. The tool is local-only (see decision 2), so the loss is small.
2. **Image-model output: NEVER committed.** It may be used locally as a
   style reference; what ships is always code-generated. The consequence:
   card 7.5 is a *local* helper, not a path into the build, and the
   provenance schema (card 7.1) records that a shipped asset is
   `procedural` or `agent-authored`.
3. **The reference JPGs: the history stays as it is.** They are already
   out of the working tree (card 4.7) and their provenance is recorded in
   `docs/art/REFERENCES.md`; they remain in the git history. Nothing in
   the repository derives from their pixels.
4. **Art direction: dusk-Japan for card 7.7** (the rig hero has to beat a
   known-good procedural one), **the neutral fantasy village stays card
   7.8's** blind clean-room test.
5. **Audio v2: decided after card 8.3's brief**, as this roadmap says.

## 8. First actions for the implementing AI

1. Read §0-§2 and `docs/experiments/round3-cleanroom.md`, `docs/experiments/round5-petalblade.md`.
2. ~~Execute **4.1 through 4.9** in order. Stop and report.~~ **Done** — see
   the status table in §9.
3. The owner's go for Wave 7 was given on 2026-10-21 (§7). Cards 7.1-7.6
   are done; **7.7** (the art pass on Petal Blade) is next.

## 9. Status

Wave 7a is **done** (cards 4.1-4.9); Wave 7 (the AI art pipeline) waits
for the owner's go (§8, step 3). The table is the record: card, the commit
that carried it, and the day it landed.

| card | what | commit | date |
|---|---|---|---|
| 4.1 | `arcade1 info` — one source of truth for the machine's constants | `37a6ca5` | 2026-10-20 |
| 4.2 | `arcade1 golden` — authoring a golden from the CLI | `8f67a37` | 2026-10-20 |
| 4.3 | the pad trace, and the edge-trigger trap it makes visible | `e5db404` | 2026-10-20 |
| 4.4 | `arcade1_rgb()`, and colour truncation you can see | `5eafdb2` | 2026-10-20 |
| 4.5 | real deflate compression in the PNG writer | `db747ce` | 2026-10-20 |
| 4.6 | a collision layer in the map import | `fa88982` | 2026-10-20 |
| 4.7 | Petal Blade promoted to Game #3; the repo cleaned | `86a20d6` | 2026-10-20 |
| 4.8 | the device work re-measured at 480x270 | `22fdb0d` | 2026-10-20 |
| 4.9 | AGENTS.md, the status tables, the `make check` budget | (this commit) | 2026-10-20 |
| 7.1 | the art workspace: presets, style template, provenance | `ae8f234` | 2026-10-21 |
| 7.2 | the drawing library, extracted without changing a byte | `f55e3db` | 2026-10-21 |
| 7.3 | the .px text pixmaps and the sprite lab | `4297e2f` | 2026-10-21 |
| 7.4 | the rig animator (the humanoid preset) | `d12e20d` | 2026-10-21 |
| 7.5 | raster ingest — LOCAL ONLY, stdlib only (PNG, no JPG) | `5f4321d` | 2026-10-21 |
| 7.6 | the visual QA tool and the critique protocol | (this commit) | 2026-10-21 |
| 7.7-7.8 | the AI art pipeline | — | not started |
| 8.1-8.3 | audio observability | — | not started |
| 9 | the freeze-readiness dossier | — | not started |

**Findings along the way** (all in `docs/experiments/`):

* the round-5 asset header was **stale** — it held a terrain tile the
  committed art no longer contained, and no committed version of
  arc1-assetc reproduces it (`docs/experiments/round5-petalblade.md`);
* `make check` is now **faster** than before Wave 7a (82 s against 102 s):
  the compressed PNG writer stopped the suite writing 6 MB of stored
  deflate blocks per capture;
* the roadmap's own claim that Petal Blade paints "2.6-2.7x the screen"
  per frame is **measured**: 2.59 (`docs/experiments/budget-480x270.csv`).
