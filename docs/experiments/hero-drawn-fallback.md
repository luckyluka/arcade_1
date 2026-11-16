# hero.drawn — the G1x fallback hero, and why swap poses leak one pixel

**Date:** 2026-11-16 · **Status:** delivered · **Scope:** `arcforge/examples/hero.drawn.json`
+ `arcforge/examples/fragments/hero/{small,big}/*.px` (G1x fallback, ROADMAP X2/Gate G1x).

## 1. What was built

Gate G1x failed on the blind rubric (`arcforge/art/reviews/001-g1x.md`), so the
fallback is in force: the hero is hand-drawn `.px` fragments riding the
`humanoid_side` skeleton, ArcForge keeps tiles/atlas/palette/level-preview/quality
tooling, v2.1 stays unfrozen, M4.1 stays blocked.

- **five fragments** per scale — `armb`, `legf`, `torso`, `armf`, `head` — as
  `custom` px_drawables over the mannequin; `features: []`.
- **`hero.drawn.small`** = height 16 (base doc), **`hero.drawn.big`** = height 32
  via `variants[].override` (RFC-7386: objects merge, arrays replace), **`hero.drawn.fire`**
  via `from: hero.drawn.big` + `palette_swap {red: gold}`.
- one file, three ids, 51 frames; `make check`/`make spec-check` ALL GREEN;
  goldens `tests/golden/hero.drawn.{small,big,fire}.json` added (new files only).

## 2. The finding: fragments cannot follow swap-pose geometry

`humanoid_side` animates legs with **swap poses** that replace the mannequin's
rect (`registry/archetypes/humanoid_side.archetype.json`):

| pose | shape | delta at h32 |
|---|---|---|
| `leg_f: lift` | rect moves right | `+max(1, leg_w//3)` = +1 → x22–26 (base x21–25) |
| `leg_b: back` | rect moves left | `-max(1, leg_w//2)` = −2 → x17–18 (base x19–20) |
| `leg_b: fwd` | rect jumps to `leg_f_x` | under the front leg — covered |

A `px_fragment` is placed at a **static anchor**; only `frame.offsets` move it.
Two consequences were measured, not assumed:

1. **Offsets cannot close the gap.** An offset shifts the fragment *and* the
   mannequin rect by the same amount — coverage requires
   `fragment ⊇ variant_rect`, independent of the offset. Swaps and offsets are
   orthogonal; nothing in the implemented language moves a fragment relative to
   its own part's variant geometry.
2. **Union-widening costs silhouette.** Painting the union rect (x17–26) in the
   fragment grows the *base* silhouette: the outer-outline post-pass
   (`arcforge_ref.py` outline pass) outlines every filled cell's empty
   neighbours, so a new fill at x17 adds outline at x16 and a fill at x26 adds
   outline at x27 — **+3 columns in every frame**, permanently.

### Resolution: align the mannequin ramp with the fragment art

The slivers are not just tolerated — at stride frames they *are* the visible
part of the pose (the fragment is static, so the shifted rect is what makes
`lift`/`back` read). The fragments draw legs **blue**, while the mannequin's
`params.armor_ramp` was still `red`, so show-through rendered as a bright red
stripe (`walk[2]/[5]`, x26 y30–37) and a maroon bar (`walk[4]/[5]`, x17 y31–38).

`hero.drawn.big` therefore overrides `archetype.params.armor_ramp: blue`
(inherits into `fire`; `palette_swap` still recolors the fragments' red→gold).
The same cells are now `blue.base`/`blue.shadow` — exact continuations of the
leg art, and `fire` keeps gold torso over blue legs. Small keeps `red`: its
only show-through cell is a torso corner that blends with the red torso.

**Cost of the alternative (rejected):** widening `legf` to the union would fix
the leak count but widen the hero's stance in all 17 frames (+3 outline
columns) — a visible base-art regression for a 1-pixel problem.

## 3. The `hurt[1]` corner (small)

`hurt[1]` offsets `head` by `[-2,-1]` but `torso` by `[-2,0]`: the head lifts
one pixel relative to the torso, exposing the mannequin rect's top-left corner
at `(19,28)` (red.base) where the fragment's top row starts one column later.
Fix: `small/torso.px` top row `....oooooo` → `...ooooooo` (one extra outline
cell). Provably silhouette-neutral: in all 17 frames the cell is already filled
with all four neighbours filled (16/17 are covered by the head fragment anyway,
pixel-identical outline), so the outer-outline pass adds nothing anywhere;
only `hurt[1]` changes, red → outline.

## 4. The leak-check harness (reusable recipe)

Bare vs covered, per frame, with the exact `render_all` build:

- **bare doc** = deepcopy of the compiled doc minus `custom`; compile both.
- **frames** = `[('base', 0, {}, None)]` + per animation frame: `offsets`,
  `rotate`, and `drawables = ir['states'][frame_state_key(fr)]` when the key is
  not None — **each ir must get its own states** (passing the bare states to the
  covered render drops the fragments → 375+ false leaks).
- `render_frame(ir, off, clip=False, drawables=..., rots=...)` — compare *before*
  `resolve_auto_cell` (auto-cell translation differs between the two).
- **leak** = `bare[p] == covered[p] and part is not None`; split by ramp:
  `outline` equalities are invisible (both renders agree), coloured equalities
  are mannequin show-through and must be judged for colour clash.

Measured (coloured leaks): before — small 1, big 36, fire 36 (red/gold);
after — small **0**, big/fire 32, all `blue.base`/`blue.shadow` in
`walk[2]/[4]/[5]` at exactly the swap-rect cells (accepted by design, §2).

## 5. Evidence

- `make lint` PASS (single `W_CLIPPED` pre-existing on `recipe:energy_sword`);
  `make render EX=examples/hero.drawn.json` PASS, 51 frames, 0 diagnostics;
  `make check` **ALL GREEN** (53 PASS); `make spec-check` **ALL GREEN**.
- LOOK: all 17 frames × 3 variants (preview sheets + upscaled `walk[2]/[4]/[5]`,
  `hurt[1]`) — cap/face/torso/arm/leg/boot coherent; stride frames read as a
  back leg in `blue.shadow` and a lifted leg in `blue.base`; no red/gold
  slivers remain.
- Frame-diff proof: `base` vs `walk[2]` differ in 15 cells (8 sliver + 7 outline
  follow); pre/post-fix `walk[2]` differ in exactly the 8 sliver cells
  (red→blue), column-top profiles identical (no pose change).

## 6. Consequences

- Gate G1x remains **FAILED**; v2.1 unfrozen; M4.1 blocked — unchanged.
- The swap-geometry limitation is a property of the implemented language
  (anchors + offsets only), not of the art. If a future milestone wants fragments
  that follow variant geometry, it is a language change and belongs to
  `spec-next/`, not to this card.
