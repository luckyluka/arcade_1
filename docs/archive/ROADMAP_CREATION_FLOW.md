# Creation Flow — Implementation Roadmap

| | |
|---|---|
| Purpose | Turn `AssetEcosystem.md` (vision) into an implemented, AI-centric creation flow, in waves, each wave proved by a real game need |
| Audience | Implementing AI agent (executes the cards) and reviewer AI (audits them) |
| Companion docs | `AssetEcosystem.md` (what/why), `docs/experiments/ai-creation-flow.md` (findings F1–F9, all evidence), `CHEATSHEET.md`/`SDK.md` (existing contracts) |
| Facts verified | 2026-10-19 at HEAD `9896afc` — every line reference below was read, every command run |

## 0. How to use this roadmap

- Each **task card** is one commit-able unit: goal → files → steps → contract →
  acceptance (exact commands, expected output) → invariants → size.
- Execute cards **in order** inside a wave. Waves are ordered by leverage; a
  later wave may start only when the previous wave's acceptance is green.
- Cards state their own acceptance — do not consider a card done because the
  code "looks right"; run the commands.
- When a card surprises you (bug, wrong assumption, measurement), log it in
  `docs/experiments/` and commit the log with the fix — findings are deliverables.

## 1. Standing invariants (apply to every card)

1. **`make clean && make check` exits 0 with zero compiler warnings before
   every commit.** Warnings are failures here, not noise.
2. **Older goldens are untouchable**: during Waves 0–1,
   `git diff --stat -- tests/golden tests/roms` must stay **empty** (new
   sidecar files may appear; existing `.txt`/`.arc1` bytes never change).
   Never run `make roms-update` / `make golden-update` as part of a tooling
   wave. When a *game* change legitimately moves a golden, refresh deliberately
   and say why in the commit — and add a new game by copying only its files.
3. **One commit per card**, message = what and why (see `git log` for style).
4. **No new dependencies**: host `cc`, `python3` stdlib only; guest code stays
   freestanding (`-Wall -Wextra`, test-pinned snippets in `CHEATSHEET.md`).
5. **Existing guests' bytes do not change** unless the card says so
   (`programs/experimental/*` is exempt — experiments may change freely).
6. **Findings are logged**, not just fixed (`docs/experiments/`).

## 2. AI-centric design rules (binding on every wave)

1. **Text contracts** — stable, greppable output (`frame N hash=…`, `var x = N`,
   `KEY value` reports), meaningful exit codes (0 ok, 1 mismatch/validation, 2 usage).
2. **Deterministic** — same inputs → same bytes; no timestamps, no unordered
   iteration in any emitted artifact.
3. **Errors teach** — every failure says *what* and *how to fix*, naming the
   asset/field (precedents: `--strict` out-of-window diagnostics, walkdemo's
   5/6/5 range error).
4. **One-command loops** — edit → build → verify is a single fast command.
5. **Reviewable artifacts** — generated C headers, not blobs (they diff).
6. **Machine-readable inventories** — budgets/reports parse line-by-line.
7. **No prompts, wizards, or TUIs** — flags only. The play window (§10) is a
   runtime, not tooling UI, and stays optional.

## 3. Wave overview

| Wave | Deliverable | Proves |
|---|---|---|
| **0** | Golden run-conditions contract (F9 fix) | verification hooks can't silently lie about coverage |
| **1** | `arc1-assetc` v0: PNG/PPM → SPRITE4 + PALETTE, deterministic, reported | the asset pain point from walkdemo disappears |
| **2** | Asset metadata + generated typed accessors (§13) | game code addresses art by name, zero stride math |
| **3** | Animation resources + timing (§4); walkdemo migrates | `anim_play` beats hand-rolled tick code |
| **4** | Map import: auto-tile, dedup, even-tile + empty-map validation (§6/F4; small-map rule retracted — ROADMAP_V3 §4.3) | a whole level is a data file, not a C array |
| **5** | **Game #1** built end-to-end through the flow | the roadmap's own tenth-game metric moves |
| **∥** | Play window (`--live`, keyboard→pad, R records) | human taste enters the loop (F6) — any time after Wave 1 |

## 4. Wave 0 — golden run-conditions contract (fixes F9)

Context: `arcade1 test --golden` compares per-frame hashes but records no
conditions; bare invocations mislead (F9 table in
`docs/experiments/ai-creation-flow.md`), and a short run currently **passes
silently** (`golden_compare` returns 0 when `n < total`, `tools/arcade1.c:689`).

### Card 0.1 — `tests/golden/conditions.txt` manifest

- **Goal**: every golden's reproduction conditions, machine-readable, without
  touching one byte of existing golden files.
- **Files**: NEW `tests/golden/conditions.txt`. Nothing else.
- **Format** (whitespace-separated, `#` comments allowed):

  ```
  # game  first_frames  rom  input  repro
  m29game   1800  tests/roms/m29game.arc1     docs/m29/scripts/restart.txt  cli
  snake     60    tests/roms/example_snake.arc1  none                       cli
  pong      2400  tests/roms/example_pong.arc1 generated:tests/test_pong.sh  cli
  platformer 308  tests/roms/platformer.arc1   harness:tests/test_platformer.c  harness
  ```

  Vocabulary: `input` = path | `none` | `generated:<script>`; `repro` =
  `cli` (reproduce with `arcade1 test … --frames N [--input …]`) |
  `harness` (in-process C harness; reproduce only via `make test-<game>`).
  `first_frames` = length of the golden's **first** frame pass (files may
  carry a reset-replay second pass; `golden_compare` uses pass 1 only).
- **Derivation**: first pass =
  `awk '/^frame /{f=$2+0; if(h&&f!=p+1){print c; exit} p=f;h=1;c++}' FILE`;
  rom/guest mapping `Makefile:71-73` + `GOLDEN_IMGS` `Makefile:798`;
  conditions from `test-<game>` recipes or `tests/test_<game>.sh` verbose runs.
- **Reference table (derived 2026-10-19; re-verify in acceptance)**:

  | game | first pass | input | repro |
  |---|---|---|---|
  | breakout | 595 | harness-scripted | harness |
  | color | 120 | none | cli ✅ exit 0 |
  | example-color | 100 | none | cli ✅ exit 0 |
  | m29game | 1800 | docs/m29/scripts/restart.txt | cli ✅ exit 0 |
  | platformer | 308 | harness-scripted | harness |
  | pong | 2400 | generated:tests/test_pong.sh | cli |
  | scroll | 120 | harness-scripted | harness |
  | snake | 60 | none | cli ✅ exit 0 |
  | tetris | 300 | harness-scripted | harness |

- **Acceptance**:
  1. manifest exists, all 9 games listed, fields parse;
  2. for one `cli` entry with input (m29game) and two without
     (color, snake): `build/arcade1 test <rom> --golden <txt> --frames N
     [--input …]` exits 0;
  3. `git diff --stat -- tests/golden tests/roms` empty.
- **Size**: ~1 h.

### Card 0.2 — teaching hint on golden mismatch

- **Goal**: a failed `--golden` run explains *why conditions matter* instead
  of leaving a bare hash diff (error rule 3).
- **Files**: `tools/arcade1.c` only — `golden_compare` (`:616`), the mismatch
  `fprintf` (`:665`), usage strings (`:701-703`, `:971`).
- **Steps**: after the existing two-line diff, print:
  ```
  arcade1 test: goldens record hashes only — reproduce the golden's run
  arcade1 test: conditions (frames, input script, ROM). See
  arcade1 test: tests/golden/conditions.txt
  ```
  Exit codes unchanged (mismatch → 1).
- **Acceptance**:
  1. bare F9 command (m29game, no `--input`) → exit 1 **and** stderr contains
     the hint (grep `conditions.txt`);
  2. F9 command with `--input docs/m29/scripts/restart.txt --frames 1800` →
     exit 0, message `1800 frame lines match …`;
  3. `make check` unchanged (verified: no test invokes `--golden`).
- **Size**: ~30 min.

### Card 0.3 — partial coverage must fail

- **Goal**: `n < total` (short run) must never report success — same trap
  class as F9.
- **Files**: `tools/arcade1.c` `golden_compare` end block (`:689`).
- **Steps**: `if (n < total) { printf("… %u of %u golden frame lines … "
  "use --frames %u\n", …); return 1; }` — keep the existing note text, flip
  the exit to 1.
- **Acceptance**: m29game with conditions but `--frames 100` → exit 1 printing
  `100 of 1800`; with `--frames 1800` → exit 0.
- **Notes**: no automated caller exists (grep verified), so behavior flip is
  free; SDK.md wording updated in 0.4.
- **Size**: ~15 min.

### Card 0.4 — pin it, wire it, document it

- **Goal**: the F9 experiment becomes a permanent test; docs stop lying.
- **Files**:
  - NEW `tests/test_golden_conditions.sh` (standard args:
    `$(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)` — mirror `Makefile:723-724`);
  - NEW Makefile target `test-golden-conditions` + **append** to
    `CHECK_TARGETS` (`Makefile:101`);
  - `GAME_DEV_GUIDE.md:57` example gains `--input …` + conditions pointer;
  - `SDK.md:186-191` gains the conditions sentence and the partial-fails rule;
  - `CHEATSHEET.md:125-126` one prose line — **only outside ```c fences**
    (`test-cheatsheet` compiles ```c blocks only; prose is unpinned).
- **Test cases** (all against copies in `$OUT`, never the originals):
  1. manifest sanity: 9 games, `first_frames` ≤ frame-line count of the file;
  2. m29game + manifest conditions → exit 0;
  3. m29game bare → exit 1 + hint;
  4. tampered copy (sed one hash) → exit 1 + `frame line … differs`;
  5. m29game `--frames 100` → exit 1 (partial);
  6. color cli entry → exit 0 (fast, 120 frames);
  7. every entry's `repro` ∈ {cli, harness} and `rom` file exists.
- **Acceptance**: `make clean && make check` → exit 0, **0 warnings**;
  `git diff --stat -- tests/golden tests/roms` empty; one commit.
- **Size**: ~2 h.

## 5. Wave 1 — `arc1-assetc` v0 (the walkdemo pain point, generalised)

Seed: `tools/gen_walk_sheet.py` (225 lines, stdlib-only, byte-deterministic —
the provenance pattern this tool must follow: **committed output is the build
truth, the tool is the master**).

### Card 1.1 — skeleton, contract, test harness

- **Files**: NEW `tools/arc1_assetc.py` (python3, stdlib only, manual argv
  like the seed); NEW `tests/test_arc1assetc.sh`; Makefile
  `test-arc1assetc` + append to `CHECK_TARGETS`.
- **CLI contract**:
  ```
  python3 tools/arc1_assetc.py --assets <assets.json> --out-dir <dir> [--report]
  exit 0 = compiled; 1 = validation error (stderr, one per asset);
  2 = usage/IO error
  ```
- **`assets.json` schema v0** (self-describing: `"format": 1`):
  ```json
  { "format": 1,
    "palettes": { "world": { "bank": 3, "colors": ["#000000", "#F8D8A8"] } },
    "sprites":  [ { "name": "player", "file": "art/player.png",
                    "palette": "world", "frames": 6 } ] }
  ```
  (sprite images are horizontal strips: width = frames × w.)
- **Test harness**: builds fixture images **in-test** via an embedded python
  mini-PNG-writer (`struct`+`zlib`, non-interlaced 8-bit RGB) — no binary
  fixtures in git; accepts the standard 3 test args for uniformity.
- **Acceptance**: `make test-arc1assetc` runs (empty happy-path ok);
  `make check` green.
- **Size**: ~2 h.

### Card 1.2 — palette module

- **Rules**: `RGB565 = (r>>3)<<11 | (g>>2)<<5 | (b>>3)` (device upload
  semantics — `CHEATSHEET.md:64-66`); exactly ≤ 16 colors; bank 0…15;
  hex parsing strict (`#RRGGBB`).
- **Teaching errors** (stderr, exit 1): 17th color →
  `palette world: 17 colors (max 16) — merge colors or add a second bank`;
  bank 16 → `palette world: bank 16 out of range 0..15`; duplicate entry →
  names both indices.
- **Acceptance**: test cases assert exit codes **and** the exact guidance
  substrings (grep).
- **Size**: ~1 h.

### Card 1.3 — image loader + quantization (validators = F4)

- **Inputs**: PNG (non-interlaced, bit depth 8, color types 0/2/3/6 — gray,
  RGB, palette, RGBA; stdlib `zlib` decode) and PPM (P6).
- **Rules**:
  1. `w, h ≥ 1`; **`w` even** (4-bit rows are w/2 bytes; odd draws nothing —
     `src/arcade1/video.c:303`) → error names the width and the fix;
  2. `image_width % frames == 0`, `frames ≥ 1`;
  3. alpha 0 → transparent (nibble 0 draws nothing);
  4. **exact-match quantization**: every pixel's RGB565 must equal a palette
     entry; otherwise
     `player: pixel (x,y) #AABBCC not in palette 'world' (nearest #RRGGBB, d=N) — fix the art or extend the palette`
     (no silent `&`-masking — walkdemo bug 1, F3);
  5. zero frames/dims → error, never a silent empty emit.
- **Acceptance**: fixtures: 3-px-wide PNG → exit 1 + `even`; unlisted color →
  exit 1 + `not in palette`; happy path → header emitted.
- **Size**: ~3 h.

### Card 1.4 — emitter + report

- **Header contract** (deterministic: sorted inputs, fixed 16-bytes-per-line
  hex, **no timestamps**):
  ```c
  /* generated by tools/arc1_assetc.py (format 1) — DO NOT EDIT.
   * player: 32x40 x6 frames, 4-bit; palette world (bank 3) */
  #define PLAYER_W 32
  #define PLAYER_H 40
  #define PLAYER_FRAMES 6
  #define PLAYER_STRIDE 768        /* (w/2)*h bytes per frame */
  static const unsigned char player_sheet[4608] = { /* … */ };
  static const unsigned short world_palette[16] = { /* RGB565 */ };
  ```
- **`--report` contract** (greppable `KEY` lines):
  ```
  SPRITE player 32x40 x6 = 4608 B
  PALETTE world bank 3 = 32 B
  ROM assets 4640 / 262144 (1.8 %)
  ```
  Total > budget (262 144, `CHEATSHEET.md:109`) → exit 1.
- **Acceptance**: test computes expected byte counts **independently**
  (`(w/2)*h*frames`, F3 oracle rule) and compares to both the header and the
  report; budget-exceeded fixture fails.
- **Size**: ~2 h.

### Card 1.5 — determinism proof + findings log

- **Acceptance**: test runs the tool twice over one fixture and `cmp`s the
  outputs byte-for-byte; deletes outputs, regenerates, `cmp`s again; NEW
  `docs/experiments/arc1-assetc.md` logs measured facts (bytes per asset,
  validator classes caught, any bug found — findings are deliverables).
- **Size**: ~1 h.

### Card 1.6 — integration doc

- **Files**: `GAME_DEV_GUIDE.md` new "Assets via arc1-assetc" section
  (usage, a complete `assets.json`, a real report sample); cross-link from
  `AssetEcosystem.md` §11 ("seed: implemented as tools/arc1_assetc.py, v0").
- **Rule**: any ```c snippet added must compile under guest flags
  (`test-cheatsheet` extracts and compiles every ```c block).
- **Acceptance**: `make clean && make check` green, 0 warnings.
- **Size**: ~1 h.

## 6. Wave 2 — metadata + typed accessors (condensed)

- **Goal**: emitters gain `origin`/`collision` metadata (from `assets.json`
  optional fields) and per-frame refs:
  `static const sprite4_ref player_frames[6] = {…}`; NEW `programs/asset.h`
  with `asset_sprite4(ref, x, y, flags)` wrapping `arcade1_sprite4`
  (`programs/gfx.h:210` region, flags bit0 = flip X).
- **Proof**: NEW guest `programs/experimental/asset_demo.c` (experimental —
  exempt from goldens) drawing via refs + a `make test-assetdemo` script
  asserting placement/flip like `tests/test_walkdemo.sh`.
- **Acceptance**: demo green; no existing guest recompiles to different bytes
  (`git diff` over built artifacts of pre-existing guests empty);
  `make check` green.
- **Rule from §13**: names resolve at build time only — no `strcmp`, no
  strings in ROM unless a game opts in.

## 7. Wave 3 — animation resources (condensed)

- **Goal**: `assets.json` `"animations": [{ "name": "run", "sprite": "player",
  "frames": [0,1,2,3,4,5], "ms": 80 }]` → emitted frame table + `anim_tick()`
  helper in `programs/asset.h` (modes: loop / ping-pong / once; time from the
  frame timer — never host time).
- **Proof**: **walkdemo migrates** (experimental guest — allowed to change)
  from its hand-rolled `WALK_TICK` ping-pong to the emitted tables;
  `make test-walkdemo` stays green (its assertions are behavioural: pace,
  flip, stability — byte changes are fine, golden rules untouched).
- **Acceptance**: walkdemo test green; `make check` green; findings appended.

## 8. Wave 4 — map import (condensed)

- **Goal**: ASCII/JSON map file → emitted `cells` u32 array + tileset blob,
  with **auto-tiling/dedup**: caller declares `tile_w × tile_h`; loader splits
  the source image, dedups identical cells exactly (the M29 brick lesson: 10×3
  field from ONE tile), emits tiles + cells (`[15:0]` index, `[19:16]` bank,
  `[20]` flipX, `[21]` flipY — `gfx.h`).
- **Validators (F4)**: even `tile_w`; empty map → error. The former
  small-map size error is **retracted** (ROADMAP_V3 §4.3): fixtures must
  instead assert that a 4×3 and a 5×1 map *draw* (pixel-count style of
  `tests/test_barrier.c`) — see
  `docs/experiments/tilemap-minsize-retraction.md`.
- **Acceptance**: fixture map; test computes the expected cells array
  **independently** and `cmp`s (F3 oracle rule); tiny-map fixture fails with
  the measured-rule message.
- **Size**: ~1 day.

## 9. Wave 5 — Game #1 through the full flow (condensed)

- **Goal**: one real game — either finish `programs/experimental/asteroid_run.c`
  (**read it first; treat as existing user work**) or a fresh micro
  side-scroller — built end-to-end: art via Wave 1, animation via Wave 3, map
  via Wave 4, verification via Wave 0's conditions + input scripts +
  `--record` replay.
- **Deliverables**: playable `.arc1`; **new** golden added by the deliberate
  procedure (copy only the new game's `tests/golden/<game>.txt` +
  `tests/roms/<game>.arc1` + `.syms` — **never** `roms-update`); input script
  committed; `docs/experiments/game1.md` with the tenth-game metric
  (empty directory → playable `.arc1`, wall-clock time).
- **Acceptance**: `make check` green including the new golden; older goldens
  byte-identical; metric recorded.

## 10. Parallel — play window (Phase 0, optional, any time after Wave 1)

- **Goal**: human taste in the loop (F6): `arcade1 run <game> --live` opens an
  SDL2 window, key state → pad register each frame (arrows/WASD, Z/X → A/B,
  Enter → START); **R** ends the session writing `--record`'s script format
  (already replayable through `--input`, tested `tests/test_run.sh:146-169`);
  ESC quits.
- **Constraints**: host-only, SDL2 optional — absence prints a clean message
  and exits 3; **not** in `make check` (optional dep); scripting path and
  `--live` must not share mutable state (determinism rule 2).
- **Acceptance**: walkdemo playable; recorded session replays with identical
  per-frame hashes (existing round-trip machinery).
- **Not AI tooling** — it exists so a human can judge feel; the AI flow never
  depends on it.

## 11. Definition of done (the metric that decides everything)

Per the guiding principle: **is the tenth game dramatically easier than the
first?** Track, in `docs/experiments/`:

1. time from empty directory to playable `.arc1` (with art, golden, input
   script) — must fall wave over wave;
2. count of hand-written hex/byte tables in guest code — must trend to zero;
3. `make check` wall time — must stay CI-friendly as waves land.

## 12. Open decisions (settle when a wave forces them)

1. exact-match vs nearest-with-threshold quantization (v0 exact; revisit when
   real artist PNGs arrive);
2. `--json` report variants (defer until a consumer exists);
3. opt-out flag for partial-golden failure (add only on request);
4. assetc stays python vs host-C (python for iteration speed today);
5. manifest/schema evolution: bump `"format"` on any breaking change from v0
   onward (precedent: `.arc1` version field, `CHEATSHEET.md:108`).
