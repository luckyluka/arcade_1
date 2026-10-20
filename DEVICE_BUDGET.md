# ARCADE-1 device work budget

Sizes a per-frame work budget **from measured data** so a future hardware
port (or a stricter emulator) has real numbers to design against. Every
figure below is labelled **[M] MEASURED** (read from the machine's own
counters) or **[E] ESTIMATED** (arithmetic on those measurements — no
hardware has been built or simulated).

> **Resolution note (M25).** Every pixel-based number in the sections below
> (`px`, `bytes`, the per-game and worst-case tables, and the derived FPGA
> cycle budgets) was measured at the **320×240** screen that was current
> during Task C. **M25 changed the screen to 480×270**, so full-screen pixel
> work scales up by 480·270 / 320·240 = **1.6875×**. The numbers below
> remain a valid record of the measurements taken then; **section 0
> re-measures the games at the real resolution**.
> The cost-model **decision is deferred** (see the end of this file).

## 0. Re-measured at 480×270 (2026-10-20)

**[M] MEASURED** at 480×270 on 2026-10-20 by `tools/measure_budget.sh`, which
runs `arcade1 inspect` over every golden game plus Petal Blade and writes
`docs/experiments/budget-480x270.csv` (the raw numbers, one row per game).
The script is measurement only — it changes no machine state, and no source
changed with it.

Sampling: `inspect` reports the **last** frame's counters, so the script
sweeps the frame count with a stride of 50 (one sample per step; `STRIDE=1`
measures every frame, at quadratic cost). `instret` is retired instructions
in that frame, `px` is pixels written after clipping, `cmd` is video commands
issued. **Overdraw** = `px` / 129 600 (the 480×270 screen): how many times
over the machine painted the whole screen in one frame.

| game | frames | samples | `inst` avg/max | `px` avg/max | `cmd` avg/max | overdraw avg/max |
|---|---:|---:|---:|---:|---:|---:|
| breakout       | 1190 | 24 |  1,716 /  1,819 | 144,356 / 144,356 | 105 / 105 | 1.114 / 1.114 |
| tetris         |  600 | 12 |  7,975 /  9,555 | 173,124 / 173,124 | 316 / 316 | 1.336 / 1.336 |
| platformer     |  616 | 13 |    907 /  6,324 | 173,838 / 173,846 |  18 /  18 | 1.341 / 1.341 |
| scroll         |  240 |  5 |  3,467 / 17,140 | 180,900 / 188,604 |   5 /   5 | 1.396 / 1.455 |
| color          |  240 |  5 | 11,881 / 59,115 | 256,964 / 258,211 |   7 /   7 | 1.983 / 1.992 |
| example_pong   | 2400 | 48 |     47 /    168 | 130,144 / 130,144 |   5 /   5 | 1.004 / 1.004 |
| example_snake  |   60 |  2 |  1,185 /  2,278 | 130,000 / 130,000 |   6 /   7 | 1.003 / 1.003 |
| example_color  |  100 |  2 |  8,600 / 17,158 | 209,872 / 212,252 |   4 /   4 | 1.619 / 1.638 |
| m29game        | 1800 | 36 |  7,914 / 34,765 | 163,092 / 171,044 | 247 / 306 | 1.258 / 1.320 |
| asteroid_run   |  760 | 16 |  2,456 /  4,950 | 152,371 / 156,315 |  53 /  62 | 1.176 / 1.206 |
| flappy         |  520 | 11 |    591 /    602 | 150,895 / 151,086 |  26 /  28 | 1.164 / 1.166 |
| **petalblade** | 1202 | 25 |  3,620 /  5,808 | 335,029 / 335,816 |  72 /  72 | **2.585 / 2.591** |

What the numbers say, without proposing a cost model:

* **Pixel work dominates, and it is not close.** The heaviest frame in the
  set is Petal Blade's: 335 816 pixels written in a frame whose screen holds
  129 600 — **2.59 screens painted per frame** — while that same frame
  retires only 3 620 instructions. The CPU is not the constraint anywhere in
  this table; the blitter's write port is.
* **A full-screen clear alone is 1.0.** Every game sits at or above 1.0
  because `CLEAR` writes every pixel; the games that stay near 1.0
  (example_pong 1.004, example_snake 1.003) draw almost nothing else, and
  the ones that draw a world on top pay for it (color 1.98, petalblade
  2.59).
* **Petal Blade is the only game above 2×**, and it is there because it
  paints a parallax background *and* a tilemap *and* the characters every
  frame. Its 72 commands per frame are unremarkable; the pixels are not.
* **The 320×240 numbers further down are not comparable to these.** At
  320×240 a full-screen clear was 76 800 pixels; the same clear at
  480×270 is 129 600 — the 1.6875× factor the resolution note predicts. The games'
  *own* work (sprites, tilemaps) did not change, which is why the ratios
  here are close to the old ones while the absolute pixel counts are not.

How the measurements were taken:

* The four game tests run their full scripted playthroughs and, each frame,
  fold `arcade1_run_frame`'s end-of-frame snapshot
  (`arcade1_video_frame_totals`) into a `video_budget_t`
  (`tests/video_budget.h`). Nothing here changes machine state; the four
  pinned state hashes are unchanged by the whole task.
* The synthetic worst cases are five hand-assembled one-frame guests in
  `tests/test_video_worst.c` (`make test-video-worst`) that drive the video
  device through the heaviest frames a guest can produce.

The four metrics, per frame:

| metric | what it counts | who pays for it on hardware |
|---|---|---|
| `inst`  | retired guest instructions | the CPU core |
| `cmd`   | video command invocations (CLEAR / SET_PIXEL / FILL / SPRITE / TILEMAP / PRESENT) | command decode + register read |
| `px`    | pixels actually written (post-clipping, transparent bits excluded) | the blitter **write port** |
| `bytes` | bitmap bytes fetched by the reference blitter (one per pixel examined) | the blitter **read port** |

---

## 1. Measured per-game usage

**[M]** All numbers are per frame, over the game's full scripted run. "avg"
is the integer mean over all frames; "max" is the single worst frame seen.

| game | frames | `cmd` avg/max | `px` avg/max | `bytes` avg/max | `inst` avg/max |
|---|---:|---:|---:|---:|---:|
| breakout   | 595 | 100 / 105   |  91,087 /  91,556 |      0 /      0 |  2,039 /  2,185 |
| tetris     | 300 | 321 / 488   | 122,096 / 129,692 |  2,981 / 12,000 |  8,079 / 14,309 |
| platformer | 295 |  17 / 18    | 109,316 / 112,442 | 13,592 / 14,228 |    841 /  6,324 |
| scroll     | 120 |   5 / 5     | 112,370 / 112,966 | 73,772 / 74,560 |    191 / 17,140 |

Reading across the worst column, the **heaviest shipped frame** is
**[M]**: `cmd` 488 (tetris), `px` 129,692 (tetris), `bytes` 74,560
(scroll), `inst` 17,140 (scroll). Note no single frame is worst in every
metric — the two extremes come from different games.

At measurement time the screen was 320x240 = **76,800 pixels [M]**
(`ARCADE1_FB_PIXELS`; 480×270 since M25 — see the resolution note above). `px`
exceeds this because a frame may *rewrite* pixels: overlapping sprites,
a `CLEAR` plus redraw, or a full-screen `TILEMAP` whose cells overlap the
same pixels. `bytes` is zero for breakout — its measured fetch count is
exactly 0 every frame, so it draws with commands that fetch no bitmap data
(`CLEAR` / `SET_PIXEL` / `FILL`) and uses no on-screen `SPRITE` or
`TILEMAP` — and largest for scroll (a full-screen `TILEMAP` every frame).

---

## 2. Synthetic worst cases

**[M]** Measured counters for one frame each, from
`tests/test_video_worst.c`. Each is the ceiling of what a guest can make
the device do; the assertions in that test are independent arithmetic, so
these are properties of the machine, not re-pinned constants.

> **Historical snapshot (v0.1 map, 320×240).** Measured before M25 (480×270)
> and M26 (map v0.2: RAM 64 KiB, was 4 KiB). At 4 KiB RAM only 128 distinct
> 16×16 tiles fit, capping row (c)'s `px`; with 64 KiB RAM all 480 cells'
> tiles fit, and the unrolled loop now reaches K = 65,533 stores (row (e)
> `cmd` 999,983). The test asserts derived arithmetic (from
> `ARCADE1_RAM_SIZE`), so the machine's ceilings stay exact — the table above
> is the pre-M25/M26 measurement.

| case | `cmd` | `px` | `bytes` | what it is |
|---|---:|---:|---:|---|
| (a) 1 CLEAR | 2 | 76,800 | 0 | one full-screen clear + present |
| (b) 100 CLEARs | 101 | 7,680,000 | 0 | 100 full-screen clears: `px` rewrites the screen 100x |
| (c) TILEMAP distinct | 2 | 32,768 | 78,000 | full-screen 20x15 cells; only 128 tiles fit in 4 KiB RAM |
| (c) TILEMAP reuse | 2 | 76,800 | 78,000 | same map, one all-set tile reused: true `px` worst case at identical fetch cost |
| (d) 128 sprites 32x32 | 129 | 131,072 | 131,072 | 128 overlapping all-set sprites |
| (e) max commands | 999,020 | 999,020 | 0 | fully-unrolled store loop, the most commands inside the 1,000,000-step cap |

Two of these — **(b) and (e)** — are pathological on purpose: they exist to
find the ceiling, and section 3 shows they cannot be guaranteed to finish in
one real display frame.

---

## 3. FPGA blitter cycles vs one 60 fps frame **[E]**

This section is **entirely ESTIMATED**: it converts the measured `px` and
`bytes` into hardware clock cycles using a stated model, then compares with
the cycle budget of a single display frame. No FPGA has been built.

**Assumptions (state them, they drive every number):**

1. A blitter with **one pixel-write port** and **one byte-read port**, each
   accepting **one operation per clock**. So `px` costs `px` cycles and
   `bytes` costs `bytes` cycles.
2. The guest's **CPU retires one instruction per clock**, so `inst` costs
   `inst` cycles. (The reference core is an interpreter; this is a
   first-cut single-cycle assumption, not a measured core timing.)
3. Each video **command** costs a small fixed decode overhead (a handful of
   cycles), which is negligible next to the pixel/byte work, so it is not
   added separately. `cmd` is reported for completeness.
4. **Serialized bound** = `px + bytes` (fetch and write never overlap — the
   conservative worst case). **Overlapped bound** = `max(px, bytes)` (a
   pipelined blitter fetches byte *N+1* while writing byte *N*'s pixels).
   Both are given; real hardware lands between them.
5. One **60 fps display frame** at clock `f` gives `f / 60` cycles:
   * **50 MHz** → **833,333 cycles/frame**
   * **100 MHz** → **1,666,667 cycles/frame**

### 3.1 Per-game worst frame

| game | blitter cycles (serialized) | % of 50 MHz | % of 100 MHz | (overlapped) | % of 50 MHz | % of 100 MHz |
|---|---:|---:|---:|---:|---:|---:|
| breakout   |  91,556 | 11.0% |  5.5% |  91,556 | 11.0% |  5.5% |
| tetris     | 141,692 | 17.0% |  8.5% | 129,692 | 15.6% |  7.8% |
| platformer | 126,670 | 15.2% |  7.6% | 112,442 | 13.5% |  6.7% |
| scroll     | 187,526 | 22.5% | 11.3% | 112,966 | 13.6% |  6.8% |

Every shipped frame fits **comfortably inside one display frame** at both
clocks: the worst case (scroll, serialized) uses **22.5 %** of a 50 MHz
frame and **11.3 %** of a 100 MHz frame. Even fully serialized, the four
games never approach the deadline. Overlapped, the worst is ~15 %.

### 3.2 Per-game CPU work

| game | `inst` worst | % of 50 MHz frame | % of 100 MHz frame |
|---|---:|---:|---:|
| breakout   |  2,185 | 0.26% | 0.13% |
| tetris     | 14,309 | 1.72% | 0.86% |
| platformer |  6,324 | 0.76% | 0.38% |
| scroll     | 17,140 | 2.06% | 1.03% |

CPU work is **negligible** next to pixel work — under **2.1 %** of a frame
for every shipped game. The device, not the core, is what a hardware port
must budget for.

### 3.3 Synthetic worst cases

| case | serialized cycles | display frames @ 50 MHz | @ 100 MHz | overlapped | @ 50 MHz | @ 100 MHz |
|---|---:|---:|---:|---:|---:|---:|
| (a) 1 CLEAR |  76,800 | 0.09 | 0.05 |  76,800 | 0.09 | 0.05 |
| (b) 100 CLEARs | 7,680,000 | **9.22** | **4.61** | 7,680,000 | **9.22** | **4.61** |
| (c) TILEMAP distinct | 110,768 | 0.13 | 0.07 |  78,000 | 0.09 | 0.05 |
| (c) TILEMAP reuse | 154,800 | 0.19 | 0.09 |  78,000 | 0.09 | 0.05 |
| (d) 128 sprites | 262,144 | 0.31 | 0.16 | 131,072 | 0.16 | 0.08 |
| (e) max commands | 999,020 | **1.20** | 0.60 | 999,020 | **1.20** | 0.60 |

**Key finding.** Cases **(b)** and **(e)** blow past a single display frame
even at 100 MHz: (b) needs **9.22 frames** at 50 MHz / **4.61** at 100 MHz,
and (e) needs **1.20** at 50 MHz. The CPU's own 1,000,000-step cap has the
same property — at one instruction per clock it is **1.20 display frames**
at 50 MHz (60 % at 100 MHz). So a guest is *allowed* by the spec (SPEC.md
§8) to overrun a real display frame, and the shipped games never do, but
the machine cannot promise every legal frame finishes in 1/60 s. Any
hardware port or strict emulator needs a deterministic overrun policy —
that is what section 4 (cost models) is for.

---

## 4. Candidate cost models **[proposal — nothing here is implemented]**

A **cost model** is a single integer charged to each guest frame, and a
**budget** the machine guarantees one display frame can absorb. If a frame's
cost stays within the budget it is presented on the very next display frame;
if it overruns, a deterministic policy (below) decides what happens. The
purpose is to give a hardware port one number to reserve for, and to give
guest authors one number to stay under.

The budget used throughout this section is one 60 fps display frame's cycle
count **[E]**: **833,333 units at 50 MHz**, **1,666,667 at 100 MHz** (the
same conversion as section 3).

**What we are sizing against.** The true "worst shipped frame" is the frame
that maximises the *combined* cost, but our per-metric maxima (section 1)
come from *different* frames (tetris holds max `cmd`/`px`; scroll holds max
`bytes`/`inst`). So we use the **sum of the per-metric maxima** as a
**conservative upper bound [M-measured components, E-summed]**:

| component | value | held by |
|---|---:|---|
| `inst`  |  17,140 | scroll |
| `cmd`   |    488 | tetris |
| `px`    | 129,692 | tetris |
| `bytes` |  74,560 | scroll |

Any single shipped frame costs **at most** what these models below compute;
the real worst frame is somewhere at or below that. This is the honest way
to state headroom without re-running every game to find the true joint
worst frame.

Each model is a weighted sum of the measured counters. The weights are the
design decision; the sums are arithmetic on measured data.

### Model 1 — serialized bound (`inst + px + bytes`)

Charges one unit per instruction, per pixel written, and per byte fetched.
This is the **most conservative**: it equals the hardware *serialized*
bound of section 3 (fetch and write never overlap), plus CPU cycles.
`cmd` is omitted because its decode cost is a rounding error next to
`px`/`bytes` (assumption 3 in section 3).

* **cost of the worst shipped frame** = 17,140 + 129,692 + 74,560
  = **221,392 [E]**.
* **headroom**: uses **26.6 %** of a 50 MHz frame → **3.76x** headroom;
  **13.3 %** at 100 MHz → **7.53x**.
* **on the worst synthetic cases**: (b) 100 CLEARs costs ~7,680,104 →
  **overruns**; (e) max-commands costs ~1,999,020 (its `inst` is the full
  1,000,000-step cap) → **overruns**.
* **overrun policy**: stretch. The frame is spread over
  `N = ceil(cost / budget)` display frames, deterministically. (b) needs
  **N=10** → 6 fps; (e) needs **N=3** → 20 fps. Nothing is dropped or
  clipped — the guest's frame completes, it simply presents late, and the
  *same* input replay produces the *same* N every run.

### Model 2 — pixels-only (`px`)

Charges only for pixels written; instructions and byte fetches are free.
This is the **simplest contract to document and to honour in hardware**:
"a guest frame may write at most `budget` pixels." It matches the physical
reality that the **write port is the scarce resource** (section 3 shows
every game writes ~91k–130k pixels per frame regardless of instruction
count), and it treats the read port / CPU as provisioned to keep up.

* **cost of the worst shipped frame** = **129,692 [M]** (`px` alone).
* **headroom**: uses **15.6 %** of a 50 MHz frame → **6.43x**; **7.8 %** at
  100 MHz → **12.85x**. The most generous of the three for shipped games.
* **on the worst synthetic cases**: (b) 7,680,000 px → **overruns**
  (**N=10**, 6 fps); (e) 999,020 px → **overruns** (**N=2**, 30 fps).
* **overrun policy**: stretch, exactly as Model 1 — `N = ceil(px / budget)`
  display frames, deterministic. Because only `px` is charged, the N is a
  pure function of pixels drawn, trivially reproducible on any port.
* **caveat [E]**: a guest that spins the CPU for the whole cap while drawing
  nothing would cost ~0 here yet still eat 1.2 display frames of *CPU* at
  50 MHz (section 3.3). Model 2 leaves CPU overrun unbudgeted; it assumes
  CPU headroom (≤2.1 % measured) absorbs it. If that assumption is
  unacceptable, Model 1 or 3 is safer.

### Model 3 — weighted (`inst + px/32 + bytes`)

Charges instructions and byte fetches in full but amortises pixels over 32
(a 32-pixel-wide burst write port, one byte per clock feeding 32 pixels).
This reflects a **realistic pipelined blitter**: the read port and CPU run
at full rate, the write port is wide.

* **cost of the worst shipped frame** = 17,140 + 129,692/32 + 74,560
  = 17,140 + 4,052 + 74,560 = **95,752 [E]**.
* **headroom**: uses **11.5 %** of a 50 MHz frame → **8.70x**; **5.7 %** at
  100 MHz → **17.41x**. The most permissive budget for shipped content.
* **on the worst synthetic cases**: (b) ~240,104 → **fits** in one 50 MHz
  frame (N=1) — the wide write port turns the 100-CLEAR flood into normal
  work; (e) ~1,031,219 (CPU-dominated) → **overruns** (**N=2**, 30 fps).
  Model 3 is the only one under which *every* shipped frame **and** the
  pathological pixel-flood fit; only the CPU-cap case overruns.
* **overrun policy**: stretch, `N = ceil(cost / budget)`, deterministic.
  Here overrun is driven by CPU/`bytes`, so the stretch protects the read
  port and core rather than the write port.
* **caveat [E]**: the "/32" is an assumed write-port width, not a measured
  property. It is the most aggressive and the least conservative; if the
  eventual hardware write port is narrower than 32, this model's budget is
  optimistic and shipped frames could overrun where the table says they
  fit.

### Summary

| model | cost of worst shipped frame | headroom @50 MHz | worst synthetic behaviour | overrun |
|---|---:|---:|---|---|
| 1 serialized (`inst+px+bytes`) | 221,392 | 3.76x | (b) and (e) overrun | stretch, N=10 / N=3 |
| 2 pixels-only (`px`) | 129,692 | 6.43x | (b) and (e) overrun | stretch, N=10 / N=2 |
| 3 weighted (`inst+px/32+bytes`) | 95,752 | 8.70x | only (e) overruns | stretch, N=2 |

All three keep the shipped games **far inside** one display frame (3.8x–8.7x
headroom at 50 MHz). They differ in **what they promise a hardware port**
(conservative vs. permissive), **what they leave unbudgeted** (Model 2
ignores CPU), and **how often a pathological guest is forced to stretch**.

**Common overrun policy (all three).** On overrun the frame is **stretched
over `N = ceil(cost / budget)` display frames, deterministically**. No
pixels are dropped, no commands are skipped, and the guest's state hash is
unaffected — the frame does exactly the same work, it just presents on a
later display frame. Because N is a pure function of the frame's own
measured counters, the same input replay yields the same N on the C
reference and on any hardware port, preserving the determinism guarantee.
The alternative — dropping work to fit one frame — is rejected: it would
make output depend on host speed and break the pinned-state-hash regression.

---

## DEFERRED (owner postponed until the FPGA milestone)

**Owner decision postponed; current reference behaviour stays.** No model is
chosen and nothing in `src/` was changed by this task. When the owner is ready,
pick one cost model to adopt as ARCADE-1's per-frame work budget (and, with
it, the hardware-port contract). This is a design decision, not a
measurement — the data above constrains but does not choose it:

* **Model 1 (serialized `inst+px+bytes`)** — safest, most conservative;
  promises the least to hardware and therefore can never be wrong about a
  frame fitting. Costs the most headroom (only 3.76x over the worst shipped
  frame at 50 MHz) and forces pathological guests to stretch the most
  (N=10 for the 100-CLEAR flood).
* **Model 2 (pixels-only `px`)** — simplest contract to document and honour;
  matches the measured reality that the write port is the scarce resource.
  Leaves CPU overrun unbudgeted (fine given measured CPU ≤2.1% of a frame,
  but an assumption).
* **Model 3 (weighted `inst+px/32+bytes`)** — most permissive; every shipped
  frame and even the pixel-flood synthetic fit in one frame, only the
  CPU-cap case stretches. Depends on an assumed 32-wide write port that has
  not been built; if the real port is narrower the budget is optimistic.

Also decide, once a model is chosen:

1. **The budget number itself** — one display frame's cycles at the target
   clock (833,333 @ 50 MHz / 1,666,667 @ 100 MHz), or a rounder, clock-
   agnostic cap (e.g. 1,000,000 units) that both clocks fit inside?
2. **Whether overrun-stretch is the committed policy**, or whether a
   hardware port may instead *trap* a guest that overruns (making the
   budget a hard limit rather than a stretch hint).

No model has been implemented; nothing in `src/` was changed by this task.
The counters that produced every number above are host-only, guest-invisible,
and in no state hash — the four pinned hashes are unchanged.
