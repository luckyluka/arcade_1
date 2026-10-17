# ARCADE-1 — Voxel Sandbox Research Experiment (post-M22)

> **Status:** research document. Not a milestone, not a roadmap change, not a
> commitment. `docs.md` and `FUTURE_MILESTONES.md` remain exactly as they are.
> Every number that is *measured* says so, every number that is *estimated*
> says so. **P0 (graphics micro-benchmarks), P1 (deterministic terrain
> generator), P2 (raycast renderer), P3 (interaction), P4 (game systems)
> and P5 (persistence) are complete** — their measured numbers are in
> §0.1–§0.6 and already folded into §2/§3/§8/§9.

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
| Guest instruction cost, rough | **measured at P0**: all-in SET_PIXEL = **8.0 guest inst/op** (loop + coordinate math + 4 register writes); FILL 1×1 = 10.0; SPRITE 16×16 = 13.0; one full-screen TILEMAP command = 13.4; one full-screen CLEAR = 4.0 — see §0.1 | `make test-bench` |
| Determinism | no interrupts, no entropy, no FP, reset-and-compare pinned hashes | `SPEC.md` §8 |

### 0.1 P0 results (measured, `make test-bench`, two identical 240-frame passes)

`programs/bench_gfx.c` + `tests/test_bench.c` run 8 fixed workloads (30 frames
each, phase selected by the guest from the frame timer) and report the
baseline-subtracted all-in cost. Guest instruction counts are exact and
reproduce across runs (state hash `0x3D7442D14497FE28`); host times are
indicative only (this host, the C device, never asserted):

| Operation (per frame) | ops | guest inst/op | host µs/op |
|-----------------------|----:|--------------:|-----------:|
| baseline (present only) | — | 0 (16/frame total) | — |
| SET_PIXEL ×1000 on-screen | 1000 | **8.00** | 0.15–0.20 |
| SET_PIXEL ×1000 off-screen | 1000 | 9.00¹ | 0.15 |
| FILL 1×1 ×1000 | 1000 | 10.00 | 0.18 |
| SPRITE 16×16 ×200 | 200 | 13.04 | 0.93 |
| TILEMAP 20×15 full-screen ×10 | 10 | **13.40** | **21** |
| CLEAR full-screen ×100 | 100 | 4.02 | 4.3 |
| FILL full-screen ×30 | 30 | 8.43 | 31 |

¹ the off-screen path costs one extra guest instruction (the `320 + …`
coordinate arithmetic), not a device effect; and clipping saves almost no
host time — the device clips per pixel either way.

**What this pins down:**

1. The old "~4–6 guest inst per pixel" estimate was **optimistic by ~1.5×**:
   the measured all-in cost is **8 inst/pixel** (4 register writes + loop +
   coordinate math). All §2/§3 pixel arithmetic is recomputed on 8.
2. **The offload ratio, measured both ways**: painting a full screen of 1-bpp
   tiles costs the guest **13.4 instructions** (TILEMAP command) vs ≈614,400
   (76,800 px × 8 guest SET_PIXELs) — ≈45,000×, consistent with the historical
   30–40× whole-frame wins (which include everything else a frame does).
3. **Device command shapes matter more than pixel counts** for the guest:
   CLEAR is 4 inst regardless of 76,800 pixels; a raycast painting vertical
   runs should issue **1-wide FILLs (10 inst per run)** rather than SET_PIXELs
   (8 inst × pixels) — break-even at run length ≈1.25 px, so FILL wins for
   essentially every ray step.
4. Host throughput of the C device's per-pixel path is ≈5–6.7 Mpix/s —
   irrelevant to guest budgets but why full-screen work must go through
   device commands (76,800 per-pixel writes ≈ 11–16 ms vs 4.3 µs CLEAR).

### 0.2 P1 results (measured, `make test-terrain`, two identical runs)

The terrain generator (`programs/terrain_gen.c`: splitmix32 lattice + 3-octave
fixed-point value noise + height bands, FNV-1a-64 digest) was built and
measured exactly as P1 specified:

- **Determinism proven three ways**: the RV32IM `-Os` guest agrees cell-by-cell
  and digest-for-digest with an independent host-C model of the same spec
  (cross-ISA); golden pins `row0[0]=178`, digest `0x626115CD2B9A0257` pin the
  spec against double edits; two reset-and-replay runs are frame-identical
  (state hash `0x5D35A196048EBD05`).
- **Generation cost (measured): 44,674 inst/frame avg, 44,902 max** to
  generate one 64-cell row (≈698 inst/cell) — one row per frame, 64 frames
  total, i.e. 4.5% of the frame cap per frame. The guest is 932 B of ROM and
  stores **no world data** (the P1 thesis holds: `(seed,x,z)` is reproducible
  from nothing).
- **Open question handed to P2**: ≈698 inst/cell includes the FNV digest
  (4 byte-mixes with 64-bit multiplies) which normal gameplay would only pay
  on save/edit. A real raycast budget needs the **height-evaluation cost
  alone**, measured inside an actual traversal loop — P2 measures that
  directly; until then any per-pixel terrain number in §2 remains an
  estimate-with-a-note rather than a measurement.

Two consequences fall out immediately:

1. **RAM, not CPU, is the constraint** (M23 already concluded this for 2D
   games; a voxel world multiplies it).
2. **Guest per-pixel work is affordable at low resolution and ruinous at full
   resolution** — so the rendering approach must minimize guest pixels touched,
   preferring device primitives (FILL/SPRITE/TILEMAP) over SET_PIXEL.

### 0.3 P2 results (measured, `make test-ray`, two identical 210-frame passes)

`programs/raycast_demo.c` is the experiment's primary rendering candidate
built for real: an 80-column first-person heightmap raycast, camera panning
over an infinite procedural world with **zero world storage** (the P1 thesis
survives first-person rendering — heights are evaluated per ray step, player
deltas would be the only state). Four timer-driven phases let the harness
measure each cost component in isolation and then the whole frame:

| Measurement | Value | Note |
|-------------|------:|------|
| baseline frame | 14 inst | present only |
| `height3` — full 3-octave P1-style height, **no digest** | **617.0 inst/eval** | P1's handoff answered: of P1's ≈698 inst/cell the digest was only ≈81 (≈12%) |
| `ray_height` — 2-octave function the raycast calls | **431.4 inst/eval** | measured inside the real traversal loop, not a synthetic micro |
| **full raycast frame** | **406,563 inst = 40.7% of the 1M cap** | ≤11 z-steps × 80 columns ≈ ≤880 evals + ≤880 FILL runs |
| host time, raycast frame | 6.9–8.7 ms | indicative only (this host, C device), never asserted |
| ROM | 1,288 B | fits the 4 KiB limit |
| determinism | sink `0x003322AA`, hash `0xF0FB04DA25A0DFE9`, raycast inst/frame pinned exactly | 210 frames ×2, identical |

**What this pins down:**

1. **The §2/§3.2 raycast estimate (60k–90k at 80×60) was optimistic by
   4–6×.** Per-step procedural evaluation dominates: 431 inst × ~880 steps
   ≈ 380k of the 407k frame. The estimate priced traversal overhead but
   under-counted what "evaluate height procedurally at every step" costs.
   This is the experiment's central measured correction.
2. **It still fits — with a caveat.** 40.7% of the frame cap is a real,
   shippable budget, but it is 28× Tetris's worst frame: game logic,
   physics or UI on top eats the remaining ~59%, and the raycast cannot
   grow much denser (z-step coarsening/fining scales cost ~linearly).
3. **Guest cost ∝ columns × steps, not pixel count.** Projection and
   FILL-run counts depend only on columns (each step issues one 4-px-wide
   FILL regardless of run length); 160 columns ≈ **2×** (~770k–810k est.)
   and 60 rows → 120 rows costs the guest nothing extra (taller FILLs are
   device-side). The earlier "160×120 = 150k–250k" conflated pixels with
   guest work; corrected in §3.2.
4. **The cheap fix, if 40% ever proves too much — a RAM window cache**
   (not implemented, estimate only): precomputing visible heights into a
   small RAM tile turns each step's 431-inst eval into a ~10–15-inst
   lookup, dropping the raycast to roughly **25k–60k inst** at the cost of
   ~1–2.5 KB RAM plus cache-refill logic — the classic trade this
   machine's RAM budget makes interesting.
5. **Rendering-correctness lesson** (found the hard way): the first
   version had no camera height (eye at h≈0), so any near terrain ≥9
   cells tall projected off the top of the screen, filled its whole column
   on step 1 and terminated the march — the frame "worked" (37.7k inst,
   deterministic, hash-pinnable) but was a flat one-color wall. The
   harness's palette/variation property checks caught it; a hash pin alone
   would have enshrined the broken render. Property checks earn their keep.

### 0.4 P3 results (measured, `make test-sandbox`, two identical 150-frame passes)

`programs/sandbox.c` wires P2's raycast to the delta overlay (the §1
"world stores nothing + edits" mechanism) and drives it from a scripted pad
(walk 30 cells → 4 places + 1 dig on watched cells → idle):

| Measurement | Value | Note |
|-------------|------:|------|
| idle view + 16-entry linear delta scan | **427,816 inst (42.8% cap)** | +21,253 over P2's 406,563 — the scan's price |
| walk / dig / place frames | 424,372 / 429,694 / 429,103 | interaction adds ≈nothing over the idle view |
| ROM | 1,708 B | +420 B over P2 for the whole interaction layer |
| edit-model truth | digest `0x00C134B1` = independent re-simulation | harness replays the 5-edit sequence (±32, clamping, script order) against its own `ray_height` model; pins x, z and final h of both edited cells |
| determinism | hash `0x360883B3C544A0CA` | 150 frames ×2, identical |
| render | both placed cells visible as silhouette-breaking towers | per-edit visibility asserted via fb digests |

**What this pins down:**

1. **The linear delta scan is cheap enough at the design's size**: a
   16-entry table costs +21,253 inst/frame (≈5.2%). Scaling linearly, the
   §1/§3.1 "~128 deltas" figure would cost ≈+170k → ≈577k total (58% of
   cap) — feasible, but that is where a hashed overlay (est. ~25 inst
   lookup regardless of size) becomes worth its code. The demo never needs
   it: recorded, not implemented.
2. **Edit visibility is a first-class rendering property, and fixed
   targets can't guarantee it.** The first version aimed at fixed cells
   (camx+1, z=4): geometrically below the screen at eye height (flat
   ground at z=4 projects to y≈76 vs the bottom at 60), then (z=8)
   behind the silhouette because the camera happened to stand on a bump
   whose surrounds fell away — five real, digest-verified edits, all
   invisible, and the harness caught it. The fix that generalizes: **edit
   the render's own nearest-visible-ground cells** (recorded during the
   raycast), so an edit lands on the silhouette by construction. All 5
   edits then verified visible. A future real game can reuse this trick
   for "what is the dig target under my crosshair".
3. **Digs and places are symmetric in cost, asymmetric in visibility**: a
   dig shortens/repaints a silhouette run (visible if the cell was on it);
   a place extends one (visible if it beats the current silhouette). Both
   are one table write + one `update_digest` — negligible frame cost
   either way.
4. **The collision/ground truth held bit-exactly**: the camera-height
   `py` matched the independent host model at the final camera, and the
   cross-ISA edit-model digest matched — the P1 determinism guarantee
   extends through interactive edits, not just pristine terrain.

### 0.5 P4 results (measured, `make test-systems`, two identical 270-frame passes)

`programs/systems.c` layers the full game-systems set on P3's interactive
raycast, all in software: inventory (a dig yields 1 dirt, a place consumes
1 and is REJECTED on an empty inventory), crafting (DOWN: 3 dirt → 1
stone), two patrolling slime entities (16.16 fixed-point motion, 1-bpp
8x8 sprites), day/night (dusk/night darken the view with ONE full-screen
TILEMAP of dither tiles — the section-5 no-blend overlay mechanism), and
a HUD (inventory squares + time indicator). The scripted scenario: walk
30 cells → dig, place, dig×3, craft, rejected place → idle through
nightfall.

| Measurement | Value | Note |
|-------------|------:|------|
| idle view + full systems layer | **431,559 inst (43.2% cap)** | +3,743 over P3 idle (427,816) — entities + overlay + HUD + inventory ≈ free |
| dig / place / craft frames | 431,931 / 432,449 / 432,194 | ≈ idle + 400 |
| ROM | 3,018 B | +734 B over P3 for the whole systems layer |
| edit-model truth | digest `0x2AA3A3F1` = replayed exactly | 5 accepted edits, recorded per-action targets, model table + stepwise ±32 + FNV matched, entry count matched |
| day/night timing | tod read 0/1/2 at frames 15/45/75 | pinned `(timer/30)%3` |
| entity motion | e1x == 4 at frame 10, bands held | pins patrol speed (1/4 cell/frame) + bounce bounds |
| entity visibility | magenta + orange present in final fb | pixel scan, 8x8 sprites on-screen |
| determinism | hash `0x26E54043DD80BE92` | 270 frames ×2, identical |

**What this pins down:**

1. **Render-derived edit targets are live state.** P3 recorded two
   watched silhouette cells and edited them; P4's scripted run proved
   those targets SHIFT as edits move the silhouette — five accepted edits
   landed on only two distinct cells, and the final watched pair had
   collapsed to the same cell. The fix that generalizes: every action
   records the exact target it used (guest arrays replayed by the
   harness), instead of assuming targets are constants. Anything
   raycast-derived that feeds game logic must be versioned per frame.
2. **The day/night overlay costs 64 B of ROM and ~50 guest instructions**
   per night frame (one TILEMAP command, tint per cell). A full-screen
   1-bpp SPRITE would need 9.6 KB — the tilemap device is the overlay
   mechanism, and P0's 13.4-inst full-screen tilemap number carries over
   unchanged with per-cell tint on top.
3. **The systems layer is guest-cheap**: +3,743 inst/frame idle for two
   entity projections + sprites, HUD rects, inventory/craft state and the
   tod math — under 1% of the cap on top of the 43% raycast. Device
   offload keeps paying: nothing here needs hardware.
4. **The rejected-place path is a first-class case**: act_ok=0 records
   the attempt without touching the table; the model replays only
   accepted edits, and delta_count matched — inventory gating cannot
   corrupt world state.

### 0.6 P5 results (measured, `make test-persist`, over the P4 scenario)

Persistence has no guest code: the machine has no storage device, so a
"save file" is a mechanism claim, and §1/§3.3 make two. P5 verifies both
against `systems.c` unchanged, with the harness playing the filesystem:

| Measurement | Value | Note |
|-------------|------:|------|
| input-log replay | **bit-exact, all 270 `state_hash`es** | fresh boot + 270-byte pad log reproduces the whole run including the boot frame |
| snapshot load-equals-replay | **bit-exact, all 147 continuation frames** | reboot + restore RAM/timer/presents at frame 122, continue from the log: every frame's RAM digest matches, final framebuffer byte-identical, all end-state symbols match (incl. tdigest `0x2AA3A3F1`) |
| final state hash | `0x26E54043DD80BE92` | equals P4's pin exactly — same guest + script, an independent cross-check |
| continuation RAM digest | `0xE67E8DF2E722A598` | pinned |
| save sizes (this scenario) | log 270 B; full-RAM restore 4,096 B; game save **56 B** | game save = seed 0 B (baked in ROM) + 2 deltas × 12 B + 8 scalars × 4 B |
| 128-delta design budget | **1,568 B** | measured correction: a delta entry is three 32-bit words = **12 B**, not the 6 B §1/§3.1 estimated — "≈1 KB" becomes ≈1.6 KB; still ✅ |

**What this pins down:**

1. **Both save mechanisms are bit-exact, and the load model is "reboot
   + restore RAM"**: the guest's whole mutable world is its RAM (globals
   + delta table); one real frame runs after boot (main()'s one-time
   initialisers), then RAM/timer/presents are restored and the machine
   continues. RAM evolution from the loaded frame on is *identical* to
   the recorded run — including the boundary frame itself.
2. **`state_hash` cannot compare across a load — by design**: it folds
   in the cumulative `instret` and the transient CPU registers, which a
   loaded machine necessarily recomputes. The load test therefore hashes
   RAM only (all game state lives there); `state_hash` remains the tool
   for whole-run replay (pass B), where the boots are identical.
3. **The save-size estimates were conservative in one direction and
   optimistic in the other**: entries are 12 B, not 6 B (+100%), but the
   input log is 1 B/frame, not ~2 (−50%). Both stay trivially small; the
   numbers in §3.1/§3.3 are corrected to the measured values.
4. **Replay is a first-class save format here**: because the machine is
   deterministic (P1–P4), the 270-byte input log is a complete,
   verified save of the entire scenario — the cheapest possible
   persistence, and the one a speedrun-style "verify your run" feature
   would use for free.

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
- **State snapshot**: seed + ≤128 deltas + player/inventory ≈ 1.6 KB —
  small enough to be a RAM region the host reads out (the test harnesses
  already read guest RAM via `nm` symbols). *(P5 measured: a delta entry
  is three 32-bit words = 12 B, not the 6 B estimated here — the full
  budget is 128×12 + scalars ≈ 1,568 B; the shipped scenario's save is
  56 B. Section 0.6.)*
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
| 1 | True low-res voxel rendering (blocky DDA raycast, Minecraft-style) | per column, integer DDA traverses blocks until hit; draw each visible face run as FILL spans with per-material tint | ~40k–100k at 80×60 (est., revised after P0: spans cost 10 inst each) | needs a **volume** store unless surface-only → §3 shows 256 KB for 64³ | feasible CPU-wise; **memory-bound** → stage experiment, surface subset only |
| 2 | **Raycasting / heightmap voxel traversal (Comanche "voxel space")** | front-to-back column traversal over `(seed,x,z)` height function; each step paints the vertical gap with one 4-px-wide FILL (P0: cheaper than SET_PIXEL for runs ≥1.25 px); occlusion stops each column early | **measured 406,563 inst/frame at 80×60 (40.7% of cap, P2)** — the 60k–90k estimate was 4–6× low (per-step height eval dominates, 431 inst/eval, §0.3); ~2× at 160 columns (est.) | **~0 world bytes** + deltas | **primary candidate**: best richness-per-byte by far; fits, but it is the frame budget |
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
was 14,309. ~~Even the pessimistic raycast estimates (≈230k at 160×120) sit
under ~25% of the cap~~ — **corrected after P2: the measured raycast is
406,563 (40.7%) at 80×60, and 160 columns would be ~770k–810k (≈80%)**; the
raycast fits, but it is the budget, not a rounding error, and it belongs at
low resolution (80×60 upscaled by the host/frontend) with a pixel-art
aesthetic.

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
| **Procedural terrain (0 B) + 128 deltas (12 B each = ~1.6 KB, measured P5)** | **< 2 KB** | ✅ |
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
| Raycast 80×60, 11 z-steps/column | **406,563 (measured, P2)** | 28× | **40.7%** |
| Raycast + 16-entry linear delta scan | **427,816 (measured, P3)** | 30× | **42.8%** |
| Raycast + full systems layer (entities, day/night, HUD, inventory) | **431,559 (measured, P4)** | 30× | **43.2%** |
| Raycast + 128-entry linear delta scan | ~577k (est., linear in table size from P3) | 40× | ~58% |
| Raycast 160×120 (160 columns) | ~770k–810k (est., ×~2: cost ∝ columns not rows, §0.3) | 54–57× | ~77–81% |
| Blocky DDA 80×60 (surface faces only) | 40k–100k | 3–7× | 4–10% |
| Guest full-screen per-pixel SET_PIXEL (avoid) | ~614k (**measured** 8 inst/px × 76,800) | 43× | 61% |
| Guest full-screen TILEMAP command (**measured**) | **13.4** | — | 0.001% |

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

≈ 1.6 KB (seed + ≤128 deltas at 12 B each + player/inventory — measured
correction at P5, was estimated 1 KB at 6 B/delta). Replay alternative:
**1 byte input/frame** (measured P5: 270 B for the 270-frame scenario;
M22's 295-frame playthrough ≈ 0.3 KB). Both are host-side. P5 verified
both mechanisms are bit-exact (§0.6).

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
| **P0** Micro-benchmarks | guest measuring exact inst/frame for SET_PIXEL-per-pixel, FILL, SPRITE, TILEMAP at fixed workloads | **DONE** — `make test-bench`; numbers pinned in §0.1, estimates in §2/§3 revised |
| **P1** Terrain generator | fixed-point hash/noise `(seed,x,z)→h,material` | **DONE** — `make test-terrain`; hash of 64x64 pinned (digest `0x626115CD2B9A0257`), identical across ISAs/hosts; load 44,674 inst/frame per row (§0.2) |
| **P2** Raycast renderer | 80×60 first-person heightcast, static scene, camera pan | **DONE** — `make test-ray`; 406,563 inst/frame (40.7% cap), height eval 431 inst (P1 handoff), render property-checked + dumped (§0.3); estimate corrected 4–6× |
| **P3** Interaction | walk/collision/dig/place → delta table re-render | **DONE** — `make test-sandbox`; walk 30 cells + 4 places + 1 dig scripted, edit-model digest `0x00C134B1` cross-checked, +21,253 inst for the 16-entry scan (§0.4); visibility-by-construction finding |
| **P4** Game systems | inventory, crafting, entities, day/night dither, UI | **DONE** — `make test-systems`; scripted scenario (dig/place/craft/rejected-place) with inventory truth, replayed edit-model digest `0x2AA3A3F1`, tod timing, entity motion/visibility (§0.5); shifting-targets finding |
| **P5** Persistence | snapshot save/load + input replay | **DONE** — `make test-persist`; input-log replay bit-exact (270/270 state hashes), snapshot load-equals-replay bit-exact (147/147 RAM digests + final fb), save sizes measured (§0.6); §1/§3 save-size estimates corrected |
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
   FILL vs TILEMAP — validating or refuting the ~4–6 inst/pixel estimate?
   **Answered by P0: 8.0 inst/pixel, and TILEMAP 13.4 inst/full screen**
   (§0.1); the estimate was optimistic by ~1.5×, and per-pixel full-screen
   painting is 61% of the frame cap — device commands remain mandatory.
3. **RAM sufficiency:** can a "surprisingly rich" world live on procedural
   generation + deltas inside 4 KiB, or is RAM growth the *first real* v0.2
   requirement with a shipped game attached? (P3/P6)
4. **ROM sufficiency:** does a from-scratch fixed-point 3D-ish engine fit
   4 KiB alongside art? (P2/P6)
5. **Does the command model extend beyond 2D games** — camera + FILL + tint +
   dither — without new device features? (P2–P4)
6. **Is determinism structurally preserved** under procedural generation,
   raycasting and save/load (it should be — no FP, no entropy, no I/O)?
   **Partially answered by P1: procedural generation is — cross-ISA identical,
   golden-pinned** (§0.2); raycast + save/load still open (P2/P5).
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
