# ARCADE-1 — Voxel Sandbox Research Experiment (post-M22)

> **Status:** research document. Not a milestone, not a roadmap change, not a
> commitment. `docs.md` and `FUTURE_MILESTONES.md` remain exactly as they are.
> Nothing in this file is implemented or tested; every number that is *measured*
> says so, every number that is *estimated* says so.

The experiment asks one question:

> How far can a deliberately small specialized game machine go toward a
> Minecraft-like experience — **without becoming a conventional 3D computer?**

The goal is not to reproduce Minecraft. The goal is to use a voxel sandbox as
an **architectural stress test** of the machine frozen in `SPEC.md` (v0.1): if
the machine already supports it, that validates the design; if it reveals a
genuine limitation, the limitation gets documented (and possibly priced as a
v0.2 item); if the "fix" would turn ARCADE-1 into a general-purpose 3D
machine, that is the signal *not* to do it.

**The objective is the smallest machine capable of producing surprisingly rich
games, not the largest machine we can build.**

---

## 0. Grounding data (measured, not estimated)

Everything below is extrapolated from facts measured on the reference VM:

| Fact | Value | Source |
|------|-------|--------|
| Display | 320×240 @ 60 fps, RGB565 | `machine.h` (placeholder) |
| Worst shipped frame | 14,309 inst/frame (Tetris line clear) | M18/M20 harnesses |
| Heaviest avg shipped game | 8,079 inst/frame (Tetris) | harnesses |
| Lightest avg shipped game | 191 inst/frame (scroll demo) | M21 harness |
| Per-frame instruction cap | 1,000,000 guest inst | `machine.c` `FRAME_STEP_CAP` |
| Cost of per-pixel guest loops | Breakout 77,836 inst/frame → 2,039 with device FILL (**38×**) | M15→M19 history |
| Cost of per-cell guest loops | Tetris 245,797 → 8,079 with device TILEMAP (**30×**) | M18→M20 history |
| Device commands available | CLEAR, SET_PIXEL, FILL, SPRITE, TILEMAP, PRESENT + camera | `SPEC.md` §5 |
| Device-side niceties | per-cell 16-bit tint on TILEMAP, 1-bpp transparent blit, world-space camera, windowing | M19–M21 |
| RAM | 4 KiB total (platformer already uses 2,680 B, ~1.4 KiB stack free) | measured `_ebss` |
| ROM | 4 KiB (largest game: Tetris 3,176 B) | measured payloads |
| Guest instruction cost, rough | ~4–6 guest inst per guest per-pixel loop iteration (address math + `sw` + branch) — **estimate**, P0 below measures it exactly | interpreter structure |
| Determinism | no interrupts, no entropy, no FP, reset-and-compare pinned hashes | `SPEC.md` §8 |

Two consequences fall out immediately:

1. **RAM, not CPU, is the constraint** (M23 already concluded this for 2D
   games; a voxel world multiplies it).
2. **Guest per-pixel work is affordable at low resolution and ruinous at full
   resolution** — so the rendering approach must minimize guest pixels touched,
   preferring device primitives (FILL/SPRITE/TILEMAP) over SET_PIXEL.

---

## 1. Proposed voxel sandbox game design (the smallest interesting one)

"Smallest interesting" means: an adult player can dig, build, get lost, come
back, and care where their house is — in a world that feels bigger than the
screen — with content that fits **today's** 4 KiB ROM / 4 KiB RAM if at all
possible.

### Recommended concept: **"Little Digger"** — a heightmap voxel world

A procedural overworld rendered as a raycast heightmap (Comanche-style), where
the player walks the surface, digs into hills, builds on them, and survives a
day/night cycle. It *reads* as a 3D voxel world from a first-person/overhead
view while storing almost nothing.

| Aspect | Design | Why |
|--------|--------|-----|
| World model | **infinite in principle, ~64–128 columns visible**; base terrain is a **pure function** `(seed, x, z) → height, material` evaluated during the raycast | zero world storage — determinism makes procedural generation free |
| Chunk size (logical) | 16×16 columns, evaluated on the fly, never stored | chunking exists as a *concept* for generation only |
| Edits (player changes) | **sparse delta table**: `{x, z, height/material delta}` — cap **128 entries** (~1 KB) | only *changes* cost memory; the common case (unmodified world) costs 0 |
| Block types | 8–16 materials by height band + noise (grass, dirt, stone, sand, snow, water level, wood, ore) | materials derived from height ⇒ no material map stored |
| Player mechanics | walk, jump, climb 1-block steps, dig 1 column, place 1 block | column-based interaction matches the data model exactly |
| Terrain generation | deterministic integer hash + value noise (xorshift-family), fixed-point only | RV32IM has no FP ⇒ determinism is structural, not enforced |
| Collision | heightmap lookup: `(x, z) → ground y`, step ≤ 1 block | one array-free function call per move |
| Inventory | 9 slots × `{id, count}`, dig yields material, place consumes | ~60 B |
| Crafting | 4–6 recipes, table lookup (plank ← wood, tool ← stick+stone…) | ~50 B of recipe data in ROM |
| Entities | 4–8 simple creatures (wander toward player at night), sprite-rendered | ~16 B each ⇒ ≤128 B |
| Lighting / day-night | day phase (0–255) drives (a) a **dithered 1-bpp darkness SPRITE** overlay (transparent gaps = free 50% shading), (b) tint selection per band | uses *existing* primitives; no blending hardware |
| Persistence | **save = seed + delta table + player state ≈ 1 KB**; load = preload before boot. Cross-session storage is **host-side** (outside the machine contract) | no storage device needed inside the VM |
| Audio | event beeps (dig/place/night) via AUDIO_FREQ/CTRL | as shipped games |

**Save/load models** (two, both cheap):
- **State snapshot**: seed + ≤128 deltas + player/inventory ≈ 1 KB — small
  enough to be a RAM region the host reads out (the test harnesses already read
  guest RAM via `nm` symbols).
- **Input replay**: because the machine is deterministic, a run is fully
  reproduced by `(seed, input script)` — free, and it is literally how the
  existing harnesses test games.

**Rejected for the smallest version** (documented, not silently dropped): full
3D cave systems with free volume editing (storage explodes, §3), swimming
fluids (non-deterministic-looking animation + no blending), livestock/farming,
redstone-like mechanics (logic circuits need per-block state storage).

**Answer to "is this Minecraft-like enough?"** It has procedural terrain, block
dig/place, inventory, crafting, creatures, day/night and save/load — the
*systemic* Minecraft experience — while rendering through a heightmap instead
of a block lattice. That is the "visually convincing, internally much simpler"
option (approach 9 in the brief) taken deliberately.

### Fallback design: **"Cube Country"** — isometric tilemap sandbox

If the raycast proves too heavy, the same game as an **isometric 2.5D world on
the existing tilemap pipeline**: a fixed 64×64 mutable cell grid (platformer
pattern, 4 KB… or 32×32 = 2 KB), rows drawn back-to-front, block height as row
offsets, camera scroll. This is approach 5/6 (layered/tilemap-based) and is
**near-certain to work today** at platformer-class cost. Less "first-person
wow", same mechanics.

---

## 2. Rendering approaches compared

Nine approaches from the brief, plus honest numbers. `inst/frame` = estimated
guest instructions per frame (**estimates**; P0 measures the primitives).

| # | Approach | How it works on ARCADE-1 | Est. inst/frame | Memory | Verdict |
|---|----------|--------------------------|----------------:|--------|---------|
| 1 | True low-res voxel rendering (blocky DDA raycast, Minecraft-style) | per column, integer DDA traverses blocks until hit; draw each visible face run as FILL spans with per-material tint | ~30k–90k at 80×60 (est.) | needs a **volume** store unless surface-only → §3 shows 256 KB for 64³ | feasible CPU-wise; **memory-bound** → stage experiment, surface subset only |
| 2 | **Raycasting / heightmap voxel traversal (Comanche "voxel space")** | front-to-back column traversal over `(seed,x,z)` height function; each step paints the vertical gap with one SET_PIXEL run or FILL; occlusion stops each column early | ~58k–80k at 80×60; ~130k–230k at 160×120 (est.) | **~0 world bytes** + deltas | **primary candidate**: best richness-per-byte by far |
| 3 | Isometric voxel rendering | draw top/left/right face of each visible cube as 3 FILLs (or one 1-bpp SPRITE tinted); painter's order back-to-front | ~60k–100k for ~4k faces (est.); drops to ~1–3k if static ground stays a TILEMAP and only changed blocks FILL | cell grid 16–64 KB, or 2 KB at 32×32 | **fallback candidate** ("Cube Country"); existing-pipeline safe |
| 4 | 2.5D voxel rendering | fixed camera pitch; depth as y-shear (mode-7-like row scaling not available → use per-column FILLs) | ~50k–120k (est.) | same as #3 | workable, no advantage over #2/#3 |
| 5 | Layered sprite-based voxel rendering | 3–4 parallax layers of 1-bpp block silhouettes (already proven in M22 parallax) | ~1k–5k (near platformer) | tiny | cheapest, reads as "diorama" not voxel world |
| 6 | Tilemap-based voxel world | the world **is** the tilemap (Tetris/platformer pattern), edits = rewrite cells; height faked by tint + sprite caps | ~0.8k–3k (**measured class**: platformer 841, Tetris 8,079) | 2–16 KB cells | safest; fallback #3's engine |
| 7 | Hybrid CPU + specialized rendering | CPU does traversal/occlusion, issues only FILL/SPRITE commands (device does pixels) — this is what all of the above already are | — | — | not a separate option; it is the architecture |
| 8 | Specialized voxel renderer added to ARCADE-1 | device command that ray-marches a heightmap in hardware | guest cost ≈ 1 cmd/frame | — | **do not add** until measurement proves software short (§5) |
| 9 | Convincing "Minecraft-like", internally simpler | design #1 above | — | — | **the recommendation** |

**Key structural insight:** every candidate above reduces to *few device
primitives per frame* or *a bounded number of guest pixels per frame at low
resolution*. The instruction cap is 1,000,000; the heaviest shipped frame ever
was 14,309. Even the pessimistic raycast estimates (≈230k at 160×120) sit
**under ~25% of the cap** — and a raycast that low-res (80×60 upscaled by the
host/frontend) is visually acceptable for a pixel-art aesthetic.

**Ranking (richness ÷ risk):** #2 heightmap raycast ≫ #3/#6 isometric tilemap
> #1 blocky DDA (memory-hostile) > #4 ≈ #5 > #8 (premature).

---

## 3. Resource estimates

### 3.1 Memory (the binding constraint)

| Item | Est. size | Fits 4 KiB RAM today? |
|------|----------:|----------------------|
| Full block volume 64×64×64 @ 1 B/block | 256 KB | ❌ (64× over) |
| One chunk 16³ @ 1 B | 4 KB | ❌ (entire RAM, no stack) |
| Heightmap 128×128 @ 1 B | 16 KB | ❌ |
| Heightmap 64×64 @ 1 B | 4 KB | ❌ (rest of RAM gone) |
| **Procedural terrain (0 B) + 128 deltas (~768 B)** | **< 1 KB** | ✅ |
| Palette/material table (16 × 2 B) | 32 B | ✅ |
| Entities (8 × ~16 B) | 128 B | ✅ |
| Player + inventory + crafting state | ~150 B | ✅ |
| Dither overlay + UI sprites (ROM) | 0 RAM | ✅ |
| **Game total (recommended design)** | **≈ 1.5–2 KB RAM** | ✅ **yes, fits today** |
| Save snapshot (seed + deltas + player) | ≈ 1 KB | ✅ (as a RAM region) |

| ROM item | Est. size | Note |
|----------|----------:|------|
| Engine (DDA raycast, fixed-point noise, physics, UI, save) | 2–3 KB | est.; raycast DDA + noise are compact integer code |
| Art (font, 1-bpp sprites, dither pattern) | 0.3–0.6 KB | 1-bpp + tint keeps art tiny |
| **Total** | **2.3–3.6 KB** | ⚠️ tight against Tetris's 3,176 B precedent; fits, barely |
| Isometric fallback engine | 1.5–2.5 KB | smaller (no raycast/noise) |

**Conclusion:** the *recommended* design fits **today's** 4 KiB + 4 KiB. A
genuine block-volume world does not fit RAM under any trick — that is the
honest architectural finding this experiment exists to surface, and it prices
out as the already-flagged **v0.2 RAM growth** (M23's ≥16 KiB), not as a
graphics feature.

### 3.2 CPU / bandwidth

| Workload | Est. guest inst/frame | vs worst shipped (14,309) | vs cap (1,000,000) |
|----------|----------------------:|---------------------------:|-------------------:|
| Isometric/tilemap sandbox (fallback) | 1k–3k | ≤2× | 0.3% |
| Raycast 80×60, ~60 steps/column avg | 58k–80k | 4–6× | 6–8% |
| Raycast 160×120 | 130k–230k | 9–16× | 13–23% |
| Blocky DDA 80×60 (surface faces only) | 30k–90k | 2–6× | 3–9% |
| Guest full-screen per-pixel SET_PIXEL (avoid) | ~300k+ | 21×+ | 30%+ |

Device-side bandwidth stays trivial: the *device* paints at most one low-res
view's worth of pixels (≤19,200 at 160×120) via FILL/SPRITE commands —
far below the 76,800-px full-screen worst case already analyzed in M23, and
fetch is only palette/tint reads (a few hundred bytes/frame).

**Reference-VM sanity:** at 60 fps the C interpreter sustains ≥60M guest
inst/s worst case inside `FRAME_STEP_CAP`; native C interprets orders of
magnitude more. The cap is a runaway guard, not a budget we are near with
shipped games — but a 230k-instruction raycast **does** occupy a real fraction
of it, so experiment P2 measures wall-clock frame time as well as instret.

**FPGA note (honest):** a soft RV32IM core *interpreting* at ~5–10 MIPS would
feel a 60M inst/s workload. If a raycast game ever targets FPGA, the core must
be a hardware-pipelined implementation (execute, not interpret) — that is an
M33 consideration, not something this experiment can verify, and not a reason
to change v0.1.

### 3.3 Save data

≈ 1 KB (seed + ≤128 deltas + player/inventory). Replay alternative: ~2 bytes
input/frame (M22's 295-frame playthrough ≈ 0.6 KB). Both are host-side.

---

## 4. Software-only feasibility analysis

**What the existing machine (v0.1, `SPEC.md`) already covers in software:**

| Capability | How, with existing primitives | Verdict |
|------------|-------------------------------|---------|
| Terrain generation | integer hash/noise in guest code; no FP exists so determinism is structural | ✅ software |
| World storage | procedural eval + sparse delta table | ✅ software |
| Raycast / voxel traversal | integer DDA in guest (RV32IM `mul`/`div` measured present in every game) | ✅ software |
| Painting the view | FILL columns / SET_PIXEL at low res / TILEMAP+SPRITE (isometric) | ✅ software |
| Lighting, day/night | per-cell tint (M20) + dithered 1-bpp SPRITE overlay (M19 transparency) | ✅ software |
| Parallax sky/water behind terrain | screen-space rects at half/quarter camera (M22 pattern) | ✅ software |
| Collision, physics, step-up | heightmap function lookups | ✅ software |
| Entities, creatures, AI | sprites + deterministic state machines | ✅ software |
| Inventory, crafting, UI | RAM structs + 1-bpp font sprites (Tetris precedent) | ✅ software |
| Animation (water shimmer, trees) | per-frame tint cycling / sprite frames | ✅ software |
| Save/load | snapshot RAM region or input replay; host persists | ✅ software |
| Audio events | AUDIO_FREQ/CTRL beeps | ✅ software |
| Camera / world panning | CAM_X/CAM_Y (M21) | ✅ software |

**Nothing in the game loop requires hardware that does not exist.** The only
open questions are *measurements* (is the raycast fast enough in guest
instructions? does the engine fit in 4 KiB ROM?), not capabilities.

**Capabilities that *might* justify future hardware — each must answer
"Why can't the CPU/software reasonably do this?":**

| Candidate | Why software *can* do it | Honest verdict |
|-----------|--------------------------|----------------|
| Device-side pixel shading (LUT tint per span) | FILL + per-command COLOR already does flat shading; per-cell tint does tilemap shading | software-adequate; add only if P2 measures fill-rate starvation |
| Hardware ray-march ("voxel renderer" command) | guest DDA at 80×60 is <8% of the instruction cap (est.) | **not justified** — this would be hardware for convenience, which the brief forbids |
| Framebuffer mapped into guest RAM | guest could SET_PIXEL-free paint; but it bypasses the command model, breaks the device-executes-commands contract and the deterministic framebuffer tests | **rejected** — easier, not necessary |
| More RAM (v0.2, not a rendering feature) | **capacity is the one thing software cannot conjure** — a block-volume world is 256 KB | the *only* candidate with a real "software can't" answer; defer, price separately |
| Z-buffer / texture unit / polygon pipeline | requires a conventional 3D design | **excluded by `SPEC.md` §10** |

**Architectural limitations this experiment may legitimately document:**

1. **Block-volume worlds don't fit v0.1 RAM** — certain today by arithmetic,
   not by experiment. Recommendation: keep v0.1, record "volume storage" as a
   v0.2 RAM question (already flagged ≥16 KiB in M23; volume worlds would want
   much more).
2. **ROM tightness for a from-scratch 3D-ish engine** (2.3–3.6 KB vs 4 KiB) —
   must be measured in P2; if it overflows, that is a ROM-size data point,
   not a CPU one.
3. **No direct framebuffer access** means guest pixels cost ~4–6 inst each —
   affordable only at reduced resolution; P0 measures this precisely.
4. **Persistence is outside the contract** — save/load needs host cooperation
   (RAM preload/readback). Document as an SDK/host feature, deliberately not a
   machine feature.

---

## 5. Possible hardware extensions

Short list, each pre-qualified by the §4 table. **Default answer for all of
them is "no, not yet":**

1. **Nothing, first.** The recommended path needs zero hardware. Run P0–P3 and
   let measurements speak.
2. **Contingent:** a span/column shading or "colored run" device command —
   only if P2 shows the guest spending >30% of its budget re-writing FILL
   parameters per span. FILL already amortizes this; suspicion is low.
3. **Legitimate but deferred:** RAM expansion (v0.2) — capacity, not
   rendering; justified only if a *shipped game* needs it, which the
   recommended design does not.
4. **Rejected outright:** framebuffer CPU mapping, polygon/z-texture units,
   floating-point, interrupts, entropy — each breaks the v0.1 contract
   (`SPEC.md` §10) or the determinism test without a demonstrated need.

The test for any future proposal stays:

> Why can't the CPU/software reasonably do this?

If the answer is "it would be easier" — no.

---

## 6. Recommended approach

**Staged, software-first, measurements before opinions:**

- **Primary:** approach #2 — *heightmap raycast over procedural terrain with a
  sparse delta table* ("Little Digger"). Best richness-per-byte; fits v0.1 by
  arithmetic; exercises determinism, fixed-point math, low-res rendering and
  the existing primitives end-to-end.
- **Fallback (pre-approved):** approach #3/#6 — *isometric tilemap sandbox*
  ("Cube Country") on the proven Tetris/platformer pipeline. If P2's raycast
  misses its budget, switch; do not add hardware to save the raycast.
- **Explicitly not recommended:** approach #1 full block-volume worlds on
  v0.1 (RAM-hostile), #8 hardware voxel renderer (premature), and any
  conventional polygon pipeline (excluded by the frozen contract).

Visual plan for the 2.5D pipeline (the brief's diagram, made concrete):

```text
voxel world            (seed, x, z) → height, material   [pure function]
    ↓
visible block faces    DDA traversal, front-to-back, occlusion-stopped
    ↓
sprites / tiles / layers   FILL column runs + tint; sky/water as parallax
                           rects (M22); entities/UI as 1-bpp SPRITEs;
                           night as dithered SPRITE overlay
    ↓
ARCADE-1 graphics      world-space via CAM_X/CAM_Y, synchronous commands
```

An attractive game is achievable with **no new machine features**: transparency
(1-bpp clear bits), shading (tint), depth (painter order + occlusion),
atmosphere (dither overlay), parallax (screen-space rects) — all shipped
between M19 and M22.

---

## 7. What should NOT be added to ARCADE-1

Because of this experiment, the following stay out (reaffirming `SPEC.md`
§10): polygon/3D pipeline, z-buffer, texture units, floating point, alpha
blending (dither instead), hardware layers/priority, direct framebuffer
mapping, interrupts, entropy, a storage/filesystem device inside the contract
(persistence is host-side), additional audio channels *for this game*, and any
"voxel renderer" device command until measurement proves software inadequate.
Also: **no roadmap change** — this experiment does not insert, renumber or
justify milestones; if its findings matter, they enter as a documented
v0.2/architecture-revision item via M27's rule (*change only where SDK/game
evidence justifies it*).

---

## 8. Development plan (hypothetical, for the game — not scheduled)

Each phase reuses the proven harness patterns (`test_breakout`/
`test_platformer`: firmware + scripted input + `nm` globals + per-frame
hashes). Effort is in "test-harness rounds", not calendar time.

| Phase | Deliverable | Exit criterion (test) |
|-------|-------------|----------------------|
| **P0** Micro-benchmarks | guest measuring exact inst/frame for SET_PIXEL-per-pixel, FILL, SPRITE, TILEMAP at fixed workloads | pinned numbers replace the estimates in §2/§3 |
| **P1** Terrain generator | fixed-point hash/noise `(seed,x,z)→h,material` | hash of a 64×64 heightmap pinned; identical across hosts |
| **P2** Raycast renderer | 80×60 first-person heightcast, static scene, camera pan | framebuffer vs independent model **or** pinned frame hashes; inst/frame + wall-clock under budget; ROM size measured |
| **P3** Interaction | walk/collision/dig/place → delta table re-render | scripted dig/build playthrough, determinism 2-pass |
| **P4** Game systems | inventory, crafting, entities, day/night dither, UI | scripted scenario assertions (state read via `nm`) |
| **P5** Persistence | snapshot save/load + input replay | load-equals-replay: same frames, same hash sequence |
| **P6** Verdict | write findings into this file: measured vs estimated, ROM/RAM actuals, go/no-go on raycast vs fallback | answers §9 with data |

**Development loop for an AI-assisted developer (question 6):**

```text
AI → game source → ARCADE-1 SDK → build → game.arc1
    → deterministic headless run (input script)
    → per-frame state hash + framebuffer dump
    → AI compares against expectation (diff / pixel assertions)
    → iterate
```

What the SDK (M25) must expose to make this practical — **requirements this
experiment feeds directly into M25**:

1. **Headless runner** with an input-script format (the harnesses already do
   this in C; it must become a productized CLI).
2. **Per-frame state hash** as a first-class output (`state_hash.h` exists).
3. **Framebuffer → image dump** (PPM/PNG) per frame or on demand — new but
   trivial, and the single most useful artifact for visual iteration.
4. **Symbol-address state readout** (`nm`-based, as tests use) so the AI can
   assert on game state, not just pixels.
5. **Frame diff tool** — run A vs run B, report first divergent frame + hash
   (the determinism failure experience, productized).
6. **Region pixel assertions** in the test SDK (tests already hand-roll
   `screen_has`).
7. Reproducible build with pinned toolchain flags (guest builds are already
   deterministic).

All six exist as test-harness techniques today; the SDK's job is to make them
*first-class* rather than per-test boilerplate.

---

## 9. Architectural questions this experiment would answer

1. **Instruction budget:** does a usable-resolution raycast stay under ~10% of
   the 1,000,000-instruction frame cap, or does it eat the budget? (P0/P2)
2. **Guest pixel cost:** what is the exact inst/frame price of SET_PIXEL vs
   FILL vs TILEMAP — validating or refuting the ~4–6 inst/pixel estimate? (P0)
3. **RAM sufficiency:** can a "surprisingly rich" world live on procedural
   generation + deltas inside 4 KiB, or is RAM growth the *first real* v0.2
   requirement with a shipped game attached? (P3/P6)
4. **ROM sufficiency:** does a from-scratch fixed-point 3D-ish engine fit
   4 KiB alongside art? (P2/P6)
5. **Does the command model extend beyond 2D games** — camera + FILL + tint +
   dither — without new device features? (P2–P4)
6. **Is determinism structurally preserved** under procedural generation,
   raycasting and save/load (it should be — no FP, no entropy, no I/O)? (P5)
7. **Is the save story viable** without a storage device — snapshot vs input
   replay, and what must the *host/SDK* provide? (P5)
8. **What must the SDK expose** for an AI loop to iterate on a game visually,
   not just numerically? (§8 → M25/M26 requirements)
9. **Does "smallest machine" hold?** Does the experiment produce a good game
   with zero hardware additions — and if a hardware idea arises, does it
   survive the "why can't software do this?" test? (P6)
10. **Resolution pressure:** is the 320×240 placeholder resolution a limiting
    factor for first-person views, and does scaling-up hurt the device (it
    should not — device cost is pixel-bound, not resolution-bound)? (P2)

**Failure modes that are also useful outcomes:** if the raycast misses budget →
the isometric fallback proves the 2.5D thesis anyway, and we have a measured
CPU ceiling for future games. If ROM overflows → a concrete ROM-size data
point. If RAM can't hold the delta table + engine → the first
game-justified v0.2 item. None of these outcomes change v0.1 silently; all get
written down here, then go through M27's evidence rule if they want to become
architecture.

---

*Companion documents: `ARCHITECTURE.md` (M23 review), `SPEC.md` (v0.1 machine
specification), `docs.md` (roadmap + pinned decisions), `PROGRESS.md` (status).*
