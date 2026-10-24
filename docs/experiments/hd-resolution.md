# HD experiment — what an HD machine would additionally need

**Card H4** of `HD_EXPERIMENT.md` (analysis, no code). H5 appends the method,
the measured table, the FPGA table, the art findings, the §6 decision matrix
and the recommendation to this same file.

**How to read the marks.** `needed` = H1–H3 measured or estimated a real cost
that only this item pays; `nice` = would improve things but nothing measured
fails without it; `not evidenced` = H1–H3 found no case that needs it. Marks
are for the resolution §6 gates, **960×540**; the `k = 4` (1920×1080) figures
are given in the same cell for context, since H2 measured all three scales.

**Measured vs estimated.** `measured` = read out of the committed artifacts of
H1–H3 (`hd-measurements.csv`, the generated `assets.h` headers, the captures,
`arc1-art ingest`'s own `report.json`); the only measured *timing* on any row
is host wall clock, on an **Intel(R) Core(TM) i7-6820HQ CPU @ 2.70GHz**.
`estimated` = a printed formula — H2's, unless the cell prints its own — with
`1 Mbit = 1e6 bits` as H2's file states. No FPGA was built and no device was
timed, so every FPGA-side figure is an estimate by construction.

---

## 1. The table

| item | mark | the evidence from H1–H3 | the cost |
|---|---|---|---|
| **per-pixel alpha** | **not evidenced** | SPEC excludes alpha blend (v0.1 rule 8: adding it needs an architecture revision and a re-test of the games). **H1**: every framebuffer write already funnels through one plot path, and it is a plain store — there is no read-modify-write in the machine. **H3**: both scenes need only *hard* transparency (palette index 0 = transparent, opaque everything else) plus opaque `FILL` rects for the sky, and both are correct; `docs/art/INGEST.md` keys the background with hard alpha ("the alpha is hard (0 or 1)"), so the art pipeline never produces a partial alpha to draw. | A blend makes every plot a read-modify-write: the write side's bus traffic at least doubles, from H2's measured 161.32 MB/s at 960×540 to **≥ 322.64 MB/s** (estimated, formula: `px_max × 2 B × 60 × 2`). |
| **more than 16 colours per sprite** | **not evidenced** | **H3 measured** every drawn sprite's visible colours: hero **8/15** — the *same* 8 at 28×40 and at 56×80 — ridge 5/15, coin 4/15, tiles 14/15; the tightest pair anywhere is **9/255 (3.5%)**, in the tileset. Density did not change a single count, because the bank is a property of the sprite, not of the pixel grid. The ingest saturates 15/15 on the test's messy sheet (`colour_count = 15` in its `report.json`), but that is median cut filling its budget by construction, not a measured want. H3's sky already escapes the bank entirely with direct RGB565 `FILL` rects. | 8 bpp instead of 4 doubles asset bytes (H2's own formula `W×H×bits/8`): all sprites 39,952 → **79,904 B**, the scene's asset set 49,704 → **99,408 B = 37.9% of 262,144** (estimated). The bank upload itself is 16 × 16-bit = **32 B** and does not scale with anything. |
| **larger ROM** | **needed** | **H3 measured** the rule H2 predicted: regenerated assets quadruple **exactly** — 9,440 → 37,760 B = **4.000×, +0.0% dev** — and the scene's asset set goes 21,384 B (**8.2%**) → 49,704 B (**19.0%**) of 262,144. **H2 estimated** one full-width parallax strip at 960×540 = 259,200 B = **98.9% of today's ROM**, and an 8-frame HD hero cycle = 184,320 B = **70.3%**. H2 also **measured** that guest instructions are identical at every k (petalblade 3783 avg / 5895 max at k = 1, 2 and 4), so code bytes do not scale — the pressure is assets alone. | ≈ **4× the asset bytes per density step**. A full native-HD art set does not fit 262,144 B (H2's single strip is 98.9% of it); today's art does, because H3's scene kept tiles and the pickup at 1× and paid only 19.0%. |
| **larger RAM** (device framebuffer) | **needed** | **H2 estimated** framebuffer RAM = `W×H×16` bits: **2.074 Mbit** (480×270) → **8.294 Mbit** (960×540) → **33.178 Mbit** (1920×1080). Under H2's stated convention, 960×540 fits **none** of the 2/4/8 Mbit budgets §6 considers. The *guest's* 64 KB is a separate question and is **not evidenced** as needing to grow: assets are `static const` → `.rodata` → **ROM only** (`programs/link.ld`), so an HD asset set never enters RAM unless it is made resident (H2: an HD hero cycle would be 281.2% of 64 KB *if* resident — a reason not to make it resident). | ×4 per density step, and ×2 again if double-buffered (16.589 Mbit at 960×540). The framebuffer is in neither guest map (`arcade1 info`: rom 262144, ram 65536), so this is device-side silicon, not a guest-visible budget. |
| **wider blitter** | **not evidenced** (at 960×540) | **H2 measured** pixel work scaling as k² — petalblade's worst frame 336,078 → 1,344,312 → 5,377,248 px — with **overdraw unchanged** (2.5829 avg / 2.5932 max at every k) and guest instructions unchanged, so the pixel *rate* is the only thing that quadruples. **H2 estimated** the clock it implies: **80.66 MHz at 1 px/clk** for 960×540, which is inside §6's ≤ 100 MHz gate — one pixel per clock is enough at the gated resolution. At 1920×1080 it is not: 322.63 MHz at 1 px/clk. | Nothing at 960×540 (the gate fits at 1 px/clk). At 1920×1080 the blitter must be **≥ 4 pixels per clock** (80.66 MHz) or the clock must reach 322.63 MHz (estimated, formula: `px_max × 60 / P`). |
| **double buffering** | **not evidenced** | SPEC excludes it (v0.1 rule 8), and SPEC's display-conformance clause makes *how* the host buffers, double-buffers or scans out **not part of the contract** — an implementation may do it internally without a guest-visible feature. H1–H3 measured nothing that shows tearing: every golden and every capture compares framebuffer **content**, and H3's two scenes are static (`fb` hash identical on all 5 frames), so a tear would have been invisible **by construction**. That absence of evidence is not evidence of absence, and the row is marked accordingly. | H2: ×2 framebuffer RAM → **4.147 / 16.589 / 66.355 Mbit** at k = 1 / 2 / 4. At 960×540 that fits none of 2/4/8 Mbit, so double buffering forces the framebuffer to external memory (next row). |
| **external framebuffer memory** | **needed** | **H2 estimated** 8.294 Mbit for 960×540 against an 8 Mbit budget → **does not fit**; 1920×1080 needs 33.178 Mbit = **4.1×** it. Caveat, so the conclusion can be checked rather than trusted: H2 defines `1 Mbit = 1e6 bits`, and under the binary convention (8 Mbit = 8,388,608 bits) 960×540 would occupy **98.9%** of one block RAM — it fits, with ~1% spare and no room for a second buffer, so the row's verdict does not turn on the convention. | The external part must carry scan-out **and** writes: **223.53 MB/s** at 960×540 and **894.10 MB/s** at 1920×1080 (estimated, formula: the sum of H2's two bandwidth rows for that k). |
| **wider scan-out** | **not evidenced** | There is **no scan-out path to widen** in the reference implementation: the VM draws into one RGB565 framebuffer and the host reads it out-of-band, and H1's choke point is a framebuffer **write** path — H2 measures writes only, and SPEC makes scan-out an implementation detail. So H1–H3 measure this item at all: never. | The *requirement* still scales, from H2's formula `W×H×2×60`: **15.55 → 62.21 → 248.83 MB/s** per density step (estimated). The write : scan-out ratio is a constant **2.59×** at all three scales because measured overdraw does not change with k — the write side is dominated by redrawn pixels, not by the resolution. |

**Summary of the marks:** two `needed` that pay for the pixel grid itself
(larger ROM, larger RAM — plus external framebuffer memory, which is where the
RAM goes), and five `not evidenced`. No item in H1–H3 was needed by the
machine's *instruction set*: H1 rendered k× sizes with the resolution macros
alone, and no guest needed a new command, a new pixel format or a blend.

---

## 2. Does a higher resolution remove the AI-art consistency problem?

**No — and H4 can say why from measurements rather than opinion.** Every
failure mode the art pipeline fights is a property of the *drawing*, decided
long before any output resolution: `docs/art/INGEST.md` lists exactly what it
fixes (background keyed from the border colour, frames aligned onto one feet
baseline, one shared palette of at most 15 colours, specks and stray blobs
dropped) and the one thing it cannot fix ("a drawing that is inconsistent
between the frames") — and not one of those operations reads the output size:
keying floods the source corners, alignment crops to content, quantisation
counts source colours, so the same sheet ingested at 24 px tall or at 48 px
tall comes out equally consistent or equally inconsistent. Measured on the
test's own messy sheet (128×64, 8 cells, a per-cell gradient background, soft
edges and 40 specks — synthesised "the way an image model leaves things",
`tests/test_art_ingest.sh`), one ingest produced 8 frames at 27×24, **15/15
palette colours, 0 orphans, 7 blobs removed, and identical bounding boxes in
all 8 frames** (`bbox_jitter` is the same `[0,0,27,24]` eight times over) —
a run whose parameters come from the source sheet's own geometry
(`--grid 4x2 --target-h 24`), with no machine resolution anywhere in the
command line, the manifest or the report. Where consistency
*is* achieved it is achieved **by construction**, not by pixels:
`docs/experiments/art-pass.md` found the rig's 28 frames are "the same
character by construction" where 11 hand-painted frames had to be
hand-checked, and H3 re-measured the same fact at twice the density — the
hero needed **8 visible colours at 28×40 and at 56×80**, the tileset 14/15
in both, so the constraint that shapes the art (15 colours per bank, a
silhouette that reads at 28 px) is unchanged by the grid it is drawn on. What
a higher resolution *does* change is the bill: H3 measured the regenerated art
at exactly **4.000×** the bytes for the same picture, and H3's verdict is that
the 960 scene is "the same, only sharper". More pixels buy edge fineness on
whatever consistency you already have; they buy none of the consistency
itself.

---

## 3. Where the evidence comes from

| claim | file |
|---|---|
| pixel work, overdraw, guest instructions, host timing, framebuffer RAM, bandwidth, the HD asset table | `docs/experiments/hd-measurements.csv`, `docs/experiments/hd-fpga-estimates.md` (H2) |
| compiled asset bytes, colour counts, tightest colour pair, outline ring, speckle rate, both captures | `experiments/hd/art_metrics.py` output, quoted in `docs/experiments/hd-art.md` (H3) |
| every framebuffer write through one path, k = 1/2/4 rendered by the same sources, default machine unchanged | H1's commits (`src/arcade1/` guards + choke point, `tests/test_hd_default.sh`) |
| what is excluded from v0.1 (alpha blend, double buffering), the 4-bit formats, palette banks, display conformance | `SPEC.md` |
| the ingest pipeline, its fixed list and its one stated limit | `docs/art/INGEST.md`, `tests/test_art_ingest.sh` |
| consistency from construction, and its honest cost | `docs/experiments/art-pass.md` |

**One correction to the card's pointer.** H4 cites "the owner's painterly
sheet and the ingest result in `docs/experiments/`". No painterly sheet is
committed anywhere in the repository — `tests/test_art_ingest.sh` states that
deliberately ("No third-party image is committed, and none is needed") and
synthesises the equivalent messy sheet instead — and the ingest result lives
in `docs/art/INGEST.md` plus the test's own pinned acceptance, not in
`docs/experiments/`. The paragraph above therefore cites the committed
evidence; the card's pointer is stale. The conclusion it pre-states — that
frame consistency, background removal and pose variation do not depend on
output resolution — is the one the evidence supports.
