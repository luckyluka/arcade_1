# ARCADE-1 device work budget

Sizes a per-frame work budget **from measured data** so a future hardware
port (or a stricter emulator) has real numbers to design against. Every
figure below is labelled **[M] MEASURED** (read from the machine's own
counters) or **[E] ESTIMATED** (arithmetic on those measurements — no
hardware has been built or simulated).

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

The screen is 320x240 = **76,800 pixels [M]** (`ARCADE1_FB_PIXELS`). `px`
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

## 4. Candidate cost models

*(added in the next step — see `BEFORE_M25.md` task C step 5)*
