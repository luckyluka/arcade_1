# ARCADE-1 — HD resolution experiment (measurement only)

| | |
|---|---|
| Purpose | Answer with data: *would a higher-resolution ARCADE-1 (960x540 or 1920x1080) be worth it?* The result is evidence for the owner. **No machine decision is part of this card.** |
| Audience | The implementing AI agent. |
| Rules | Same as `ROADMAP_V4.md` §0 (one card = one commit, `make check` green with zero warnings, goldens and ROMs untouched, stdlib-only tools, deterministic outputs, findings go to `docs/experiments/`). The additions below are binding. |
| Size | 2-3 days. Time-box H3 to one day; cut scope rather than overrun. |

## 0. Binding additions

1. **The default build and the default machine do not change.** The experiment builds *separate* binaries (`build/arcade1-hd2`, `build/arcade1-hd4`) from the same sources with `-DARCADE1_HD_SCALE=k`. `git diff --stat -- tests/golden tests/roms` is empty at the end; every existing state hash and frame hash is identical (`make check` proves it).
2. **`src/` may change only to add**: `#ifndef` guards around the resolution macros, an optional pixel-plot choke point, and a `k` multiplier compiled in only when `ARCADE1_HD_SCALE` is defined. At `k = 1` (the default) the compiled behaviour is byte-identical.
3. **Experiment code lives in `experiments/hd/`**; it is **not** part of `make check` except the small guard test in H1 that proves the default build is unchanged.
4. **Label every number** `measured` (with host CPU model) or `estimated` (with the formula). No number without a label.
5. **Do not recommend HD unless the gates in §6 are met.** Report honestly if they are not.

## 1. Card H1 — scaled-rendering harness (no art involved)

**Goal**: render the *same* game content at k times the linear resolution (k = 1, 2, 4 gives 480x270, 960x540, 1920x1080) by making the video device plot every logical pixel as a k x k block. This measures the device and host cost of HD-sized frames using real games, without needing HD art.

**Steps**
1. Find the resolution macros: `grep -rn 'ARCADE1_WIDTH\|ARCADE1_HEIGHT' src tools tests`. Wrap their definitions in `#ifndef` guards so a `-D` override compiles. Commit alone; verify hashes unchanged.
2. Introduce a single pixel-plot helper in `src/arcade1/video.c` through which **all** framebuffer writes pass. If commands currently write the framebuffer in several places, do this refactor first as its own behaviour-neutral commit (`make check` and all goldens identical).
3. Add the scale: with `ARCADE1_HD_SCALE=k` defined, logical resolution stays 480x270 for the guest (all coordinates, camera, tile maths remain in logical units); the framebuffer is `480k x 270k`; the plot helper writes k x k blocks; clipping happens in logical units before expansion. Counters (`pixels_written`, etc.) count **physical** pixels written.
4. `Makefile` target `hd-experiment` builds `build/arcade1-hd2` and `build/arcade1-hd4` (output names fixed). Capture/PNG code must follow the framebuffer size (it already reads the macros).
5. Add `arcade1 run --bench`: after the run print `BENCH_MS_MEDIAN`, `BENCH_MS_P95`, `BENCH_MS_MAX` over per-frame wall-clock times of `arcade1_run_frame` (monotonic clock), and the host CPU model line from `/proc/cpuinfo` when available. Host timing is diagnostic and never enters any hash.

**Acceptance** (script `experiments/hd/test_scale.sh`, run by `make hd-experiment`, plus a guard in `make check` that the default build's hashes are unchanged):
- For each of Petal Blade, Asteroid Run and Flappy (committed ROMs and input scripts): the `k = 2` and `k = 4` framebuffers at frames 60, 200 and 400 **equal the nearest-neighbour upscale of the `k = 1` framebuffer, byte for byte** (compare with a small python script over PPM captures).
- `k = 1` of the experiment binary produces hashes identical to the default binary.
- `pixels_written` at `k` equals `k^2` times the `k = 1` value (exact), for every command type.
- The default `make` and `make check` are unaffected (zero warnings; same `ok` count).

## 2. Card H2 — measurements and FPGA-side estimates

**Goal**: one table that shows what HD costs.

**Steps**
1. `experiments/hd/measure.sh` runs each game at k = 1, 2, 4 for 600 frames (committed input scripts) and writes `docs/experiments/hd-measurements.csv`: game, k, resolution, framebuffer bytes (`2 * W * H`), physical pixels written per frame (avg, max), overdraw ratio (pixels written / pixels on screen), host `BENCH_MS_MEDIAN` / `P95` / `MAX`, ROM bytes in use (`inspect`), instructions per frame (avg, max).
2. `experiments/hd/fpga_estimates.py` (stdlib) reads the CSV and, **labelled estimated**, computes per k: required pixel throughput at 60 fps (`pixels_written_max * 60`), clock needed at 1, 4 and 8 pixels per clock, framebuffer RAM in Mbit (single and double buffered) and whether it fits 2 / 4 / 8 Mbit of block RAM, scan-out read bandwidth (`W * H * 2 * 60` bytes/s), and write bandwidth (`pixels_written_max * 2 * 60`). State the formulas in the output.
3. A second table: the **resources a native-HD asset set would need** (computed, not drawn): hero frame at 96x120 / 192x240 4-bit (bytes per frame, per 8-frame cycle), one 64x64 / 128x128 tile (bytes), a full-width parallax strip (bytes), versus today's 256 KB ROM and 64 KB RAM budgets.

**Acceptance**: the script is deterministic apart from the host timing columns (re-running changes only those); three CSV rows are spot-checked against direct `inspect` output; the FPGA table is generated, not typed.

## 3. Card H3 — same scene, native pixel density (art evidence)

**Goal**: does a higher resolution make the *art* meaningfully better, or only bigger? Use the existing art tools; do not invent new ones.

**Scope (time-box one day; cut, do not overrun)**: one static scene, rendered in two guests:
- `experiments/hd/scene_480.c`: the Petal Blade valley at 480x270 (hero, 4 tiles, one parallax ridge, one pickup, HUD text).
- `experiments/hd/scene_960.c`: the **same composition with art regenerated at 2x native density** (hero via the rig animator at twice the height, tiles at twice the pixel size, a ridge generated at 2x, 2x HUD font size). Use the generators' existing parameters; **if a generator cannot be parametrized in under an hour, say so, exclude it, and keep the scene with the remaining parts**.

Build `scene_960` against the `k`-free 960x540 build: run the experiment binary with `-DARCADE1_HD_SCALE=2` and draw at physical resolution using a new `ARCADE1_LOGICAL_W/H` override (explain in the report how the guest sees coordinates; the guest must draw 960x540 pixels natively, not through the block expander). If this is not possible without machine changes beyond §0 rule 2, **stop and report instead**.

**Review protocol**: capture both scenes at the same on-screen size (the 480 scene at 4x, the 960 scene at 2x, both 1920x1080); review each with `art/CRITIQUE.md` (silhouette readability, lighting, palette harmony, outline consistency, noise). Record in `docs/experiments/hd-art.md`: per criterion, which is better and why, **plus the count of colours the hero needed to avoid banding** (try 15 vs a free palette and say what a 16-colour bank costs at 2x). Include the two screenshots (compressed PNG) and the asset byte totals.

**Acceptance**: both guests build with `arcade1 build` or the experiment tooling; both captures exist; the review is written criterion by criterion; asset byte totals match H2's computed table within 10 %. State plainly if the 960 scene looks *better*, *the same, only bigger*, or *worse*.

## 4. Card H4 — what an HD machine would additionally need (analysis, no code)

In `docs/experiments/hd-resolution.md`, one table: for each of these, the evidence from H1-H3 that it is or is not needed, and the cost: per-pixel alpha, more than 16 colours per sprite, larger ROM, larger RAM, wider blitter, double buffering, external framebuffer memory, wider scan-out. Mark each **needed**, **nice**, or **not evidenced**. Add the one-paragraph answer to: *does a higher resolution remove the AI-art consistency problem?* (Evidence: the owner's painterly sheet and the ingest result in `docs/experiments/` — frame consistency, background removal and pose variation do not depend on output resolution.)

## 5. Card H5 — report and stop

`docs/experiments/hd-resolution.md` contains: the method (what is exact and what is estimated), the measured table, the FPGA table, the art findings, the H4 table, the decision matrix of §6 filled in with the evidence, and **a recommendation with its confidence**. Also list what the experiment could *not* show. **Stop. The owner decides.**

## 6. Decision matrix (fill in; do not tune the thresholds afterwards)

HD is worth considering as a *separate future machine* only if **all** hold:

| Gate | Threshold |
|---|---|
| Host cost | 1920x1080 (`k = 4`) median frame time of the heaviest game ≤ 8 ms on the recorded laptop CPU, or 960x540 ≤ 4 ms |
| FPGA plausibility (estimated) | 960x540 worst-frame pixel work fits ≤ 1 pixel per clock at ≤ 100 MHz with the framebuffer outside block RAM only if bandwidth ≤ the stated external-memory budget; otherwise HD is out of reach for the target FPGA class |
| Art gain | The 960 scene beats the 480 scene on at least 3 of the 5 critique criteria, with the reviewer saying why |
| Asset cost | HD hero cycle + tile set + one ridge fit in ≤ 256 KB (today's ROM) or the report states the ROM size that would be needed |
| No new machine feature | The gain is not purely from features this card did not test (alpha, more colours) |

If any gate fails, the recommendation is: **stay at 480x270**; improve output-stage polish (smooth scaling, bloom/soft-CRT shaders outside the machine contract) and the pixel art itself (the hero shootout from ROADMAP V5 preparation).

## 7. First actions

1. Read `ARCHITECTURE.md`, `DEVICE_BUDGET.md`, `docs/experiments/budget-480x270.csv` and the art docs.
2. Execute **H1, H2, H3, H4, H5** in order; stop after each, committing per card, then report.
3. Do not start any other roadmap item until the owner has read `hd-resolution.md`.
