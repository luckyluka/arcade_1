# X1 plan — arcforge language extensions (v2.1 subset)

Branch `x1-language-extensions` (from `x0-variable-size`, pushed at `d96af7f`),
tag `pre-x1` (both gates ALL GREEN at the tag). Scope = ROADMAP.md X1, read
through spec-next/02 + 01 §6. NOT in scope: `affects` (02 §D), required-template
list / `facing` flip logic (02 §F runtime half), px importer, tiles/atlas/levels
(X3), quality metrics + project file (X4), content (X5), M1–M3 waves, C/P0/M4.

**Exit-criteria conflict (recorded, see §Gaps):** ROADMAP says "every example in
`spec-next/examples` compiles", but those examples need X2 archetypes
(`humanoid_side`, `tile`), a `platformer_24` style pack, X2 recipes and the
X3 project/tileset/level formats. X1's exit is therefore reinterpreted as below
(fixtures + an in-repo exercise asset); the literal criterion moves after X2/X3.

## Scope → commit (one theme per commit, each gated on ALL GREEN)

| commit | message | scope |
|---|---|---|
| 1 | x1: shape vocabulary — mirror, line, repeat, px shape | 02 §C + 01 §6: `mirror:"x"` (+ archetype `mirror_axis`), `line` (Bresenham, odd thickness), `repeat` (`$i`, count ≤ max ≤ 256, nesting ≤ 2, `E_REPEAT_BOUND`), `px {file,anchor?,flip?,when?}` in archetype/recipe geometry (no `frame` yet — §C has none; it lands with px 2 in commit 3); codes: `E_REPEAT_BOUND` (§J), `E_SHAPE_PARAM` (even line thickness, `mirror`+`rotate`, missing fill — beyond §J) |
| 2 | x1: extends, variants, palette_swap | 02 §E: RFC-7386 `extends` (depth ≤ 3), `variants` array form (`id`, `from`, `override`, `palette_swap`), top-level `palette_swap`; ids unique per file (`E_DUPLICATE_ID`); test/golden/render iterate base + variants (golden per id, loop lives in the CLI — `run_tests.py` needs no change); codes `E_EXTENDS_CYCLE`, `E_DUPLICATE_ID` (§J), `E_EXTENDS_PATH`, `E_EXTENDS_DEPTH`, `E_UNKNOWN_VARIANT`, `E_UNKNOWN_RAMP` (named in §E, absent from §J — reported) |
| 3 | x1: px 2 multi-frame | 02 §G: `px 2` header, `frames N` + `frame i` blocks, shared size/legend, v1 unchanged; `frame` field on `px` shape and `custom` (default 0) as the minimal honest binding (see D9); code `E_PX_FRAMES` (§J) |
| 4 | x1: tracks, swap frames, hold, atlas facing | 02 §F archetype-frame half: `params` tracks (kinds size/int/bool/enum/percent; size clamped), `swap {part: set}`, `hold`; atlas gains `facing:"right"`; codes `E_TRACK_PARAM` (§J), `E_UNKNOWN_SWAP_SET` (beyond §J) |
| 5 | x1: §H assertions + exercise asset, docs/spec-next marked | 7 per-asset assertions (D13), one new example asset exercising every X1 feature on existing archetypes (+ own new golden), specs 02 status line updated, LANGUAGE/RECIPES/DECISIONS reconciliation |

## Files per commit (all paths relative to repo root)

1. `docs/experiments/x1-plan.md` (this file); `arcforge/schema/archetype.schema.json`
   (`mirror_axis`, shape oneOf + `mirror`/`line`/`repeat`/`px` arms);
   `arcforge/schema/recipe.schema.json` (same shape arm via `$defs.shape`);
   `arcforge/tools/arcforge_ref.py` (`build_shape` family: reflect, Bresenham,
   bounded repeat, px ingest); `arcforge/tests/invalid/{repeat_bound,
   line_even_thickness, mirror_rotate}.archetype.json` + `expectations.json`
   (sweep form); `arcforge/docs/LANGUAGE.md`.
2. `arcforge/schema/asset.schema.json` (`extends`, `variants`, `palette_swap`);
   `arcforge/tools/arcforge_ref.py` (merge-patch loader, variant expansion, CLI
   iteration over ids); `arcforge/tests/invalid/{extends_cycle, extends_path,
   extends_depth, extends_depth2..4, duplicate_id, unknown_variant,
   palette_swap_ramp}.json` + `expectations.json`; `arcforge/docs/LANGUAGE.md`.
3. `arcforge/tools/arcforge_ref.py` (`parse_px` px-2, `px` shape + `custom`
   `frame`); `arcforge/schema/{asset,recipe,archetype}.schema.json` (`frame`
   fields); fixture `tests/invalid/px_frames.json` +
   `tests/invalid/fragments/bad_frames.px` + `expectations.json`;
   `arcforge/docs/{LANGUAGE,PX_FORMAT}.md`.
4. `arcforge/schema/archetype.schema.json` (frame `params`/`swap`/`hold`,
   `parts.*.variants`); `arcforge/tools/arcforge_ref.py` (per-frame env override,
   part shape-set swap, hold → exported durations, atlas `facing`);
   `arcforge/tests/invalid/{track_param, swap_set}.archetype.json` (sweep form)
   + `expectations.json`; `arcforge/docs/LANGUAGE.md`.
5. `arcforge/tools/arcforge_ref.py` (§H assertion arms in `run_tests`);
   `arcforge/schema/asset.schema.json` (tests enum + arg fields);
   `arcforge/registry/archetypes/x1_showcase.archetype.json` (new inventory
   archetype) + `arcforge/examples/fragments/x1_showcase.px` (new px-2
   fragment); `arcforge/examples/x1_showcase.json` (new) +
   `arcforge/tests/golden/x1_showcase*.json` (**new files only!**);
   `arcforge/tests/invalid/assert_baseline.json` (E_TEST_FAILED fixture) +
   `expectations.json`; `arcforge/spec-next/02_LANGUAGE_EXTENSIONS.md`
   (status line: §C/E/F/G/H marked, PROPOSED banner stays);
   `arcforge/docs/{LANGUAGE,RECIPES,DECISIONS}.md`.

## Design decisions (D1–D15)

- **D1 mirror.** `"mirror": "x"` on rect/ellipse/poly/line draws the shape and
  its integer reflection about the axis = archetype `mirror_axis` metric, else
  `floor($W/2)`; reflection of the *geometry* before rasterization (rect
  `[x,x+w)` → `[2a-(x+w), 2a-x)`), same fill/`when`. `rotate` + `mirror` on one
  shape → `E_SHAPE_PARAM` (reflections of rotated art are out of scope). Reuse
  of the flag inside `repeat` is allowed (outer iteration, then reflect).
- **D2 line.** Integer Bresenham, square brush (`thickness²` block centred on the
  step), `thickness` must be a positive odd integer → else `E_SHAPE_PARAM`
  (spec names no code; beyond §J, reported). Pure integer, no float.
- **D3 repeat.** `{var, count, step_x, step_y, max, shapes[]}`; `$var`+`$i`
  bound per iteration, fixed order `0..count-1`; `count` must evaluate ≤ `max`,
  `max` ≤ 256, nesting ≤ 2 → `E_REPEAT_BOUND` (§J) on violation. Children may be
  any shape (`px` included); nested repeat within the nesting limit. A broken
  child compiles once per iteration — each distinct diagnostic is reported once.
- **D4 px shape.** `file` resolved against the asset base dir (like `custom`),
  `anchor` defaults to the enclosing shape's anchor, `flip` ∈ {"x","y"} (mirror
  of the fragment's columns/rows about the attach pixel, integer), `when` gates.
  `frame` is NOT in commit 1: spec 02 §C has no such field, so it ships with
  px 2 (D9, commit 3). Legend → ramps resolution identical to `custom`;
  missing file reuses `E_PX_NOT_FOUND`, bad legend `E_PX_LEGEND`.
- **D5 extends.** Path relative to the extending file, must stay inside the
  repo root (escape/missing/non-asset/invalid target → `E_EXTENDS_PATH`,
  beyond §J); depth ≤ 3 links → `E_EXTENDS_DEPTH` (beyond §J; §J names only
  the cycle code for extends); RFC 7386 merge (objects merge, everything else
  replaced whole); cycle → `E_EXTENDS_CYCLE` (§J). Application order: `extends`
  chain first, then `variants` overrides. A document *with* `extends` is a
  patch: the raw schema check runs only on documents without one (required
  fields come from the target), so types/unknown keys are caught on the merged
  document; leaf targets are fully schema-checked when loaded.
- **D6 variants.** Array form only — the proposed schema's undocumented
  `matrix` form stays unimplemented (gap recorded). `from` may reference the
  base document or an **earlier** entry (forward refs and self refs →
  `E_UNKNOWN_VARIANT`); a `from` chain is not nested (entry composes from base
  ∪ its own `override`). Ids unique across base+variants **within the file**
  (`E_DUPLICATE_ID`); true project-wide uniqueness needs the X4 project file
  (recorded as deferred).
- **D7 CLI/golden over variants.** `test`/`golden` compile base + every variant;
  golden files are named per id (`tests/golden/<id>.json`, new files only);
  `render` writes `out/<id>/`. Existing goldens untouched (ids are new).
- **D8 palette_swap.** Ramp-name map applied when fills resolve, only for the
  document that declares it; both ramps must exist in the style pack
  (`E_UNKNOWN_RAMP` — named by 02 §E but absent from §J: a new code, reported).
  Assertions (palette_size) see the swapped palette.
- **D9 px 2.** `px 2` header + `frames N` + `frame i` blocks; all frames share
  size and legend; `N` must match the blocks (`E_PX_FRAMES`). Spec 02 §G never
  says how an animated px is *bound* (gap) — minimal honest binding: optional
  `frame` on the `px` shape and on `custom` (default 0). Animated consumption
  beyond frame selection stays for X2/X5.
- **D10 tracks.** Frame `params` live on **archetype** templates (assets only
  name templates). At compile, each declared param must be (a) an archetype size
  param (clamped to range, silently, per spec), (b) an archetype param of kind
  int/bool/enum/percent, or (c) a recipe param **of a feature this asset uses**
  whose kind is allowed in *every* used recipe declaring it → else `E_TRACK_PARAM`.
  Track values participate in the expression env for that frame; percent values
  are percents (resolved with the same `of` rules); unresolved/unavailable
  percent-of in a track → the same `E_UNKNOWN_SIZE_PARAM` rule as supplied
  params. Non-integer size track → `E_TRACK_PARAM` (§J names no better code);
  archetype/recipe track values otherwise validate exactly like supplied values
  (`E_PARAM_TYPE`/`E_PARAM_RANGE`/`E_PARAM_ENUM`). Auto-canvas stays correct
  because the union bbox already covers all frames. A state re-derives
  size/class/parameters/metrics and rebuilds archetype **and** feature shapes;
  anchors, hitboxes and part offsets stay at their base values, and recipe
  `sizes` spans are not re-checked per frame (documented in LANGUAGE.md).
- **D11 swap.** Archetype parts gain `"variants": {"<set>": [shapes...]}`; a
  frame's `swap: {"<part>": "<set>"}` replaces that part's shape list for that
  frame (anchors/offsets unchanged). Unknown part → `E_UNKNOWN_PART`, unknown
  set → `E_UNKNOWN_SWAP_SET` (beyond §J), both at
  `/archetype/animations/<t>/frames/<i>/swap/<part>`. Swap interacts with tracks
  by composing in the same frame state (one drawable set per distinct
  `(params, swap)` pair, built at compile).
- **D12 hold.** Frame `hold: n` (int ≥ 1, default 1) repeats the frame's
  duration: `atlas.json` gains `durations: [hold per frame]` **in ticks at the
  template's fps** (exact, no rounding; `fps` alongside converts), while
  `frame_count`/`frames` count frames, not holds (existing semantics
  preserved). Structure is schema-enforced with the archetype — same contract
  as `fps` — so no separate compile-time code.
- **D13 assertions.** IN: `baseline_stable`, `pivot_stable`, `min_feature`,
  `cell_fill`, `seamless`, `jitter_max`, `orphan_pixels` (all per-asset).
  OUT: `contrast_min` (needs project `backgrounds`), `silhouette_distinct`
  (needs peer assets) — both deferred to X4 with the project file (reported).
  Fixture: failing assertion → `E_TEST_FAILED` via `run=test`.
  Semantics pinned while implementing: every assertion sees **all** rendered
  frames (base + animations); `baseline_stable`/`pivot_stable` use anchor
  positions incl. part offsets (pivot = Chebyshev displacement vs the base
  frame); `min_feature` = min over base drawables **and** every frame state,
  with **every shape counting** (`affects: ["detail"]` is documentation-only,
  no runtime marker exists — the conservative reading, recorded as a gap);
  `cell_fill` = per-frame opaque ratio, observed band must sit inside the
  declared one; `seamless` = per frame, each border cell present in the grid
  AND not outline-ramp, `axes` is an array of `x`/`y` (02 §H example +
  proposed schema); `jitter_max` = per animation, Chebyshev step between
  floored COM means of consecutive frames; `orphan_pixels` = max per-frame
  count of pixels with no 4-neighbour.
- **D14 exercise asset + exit.** `examples/x1_showcase.json` **extends
  `hero.scalable_small.json`** (exercising §E on a real file) and carries one
  variant that re-points the merged document at the new registry inventory
  archetype `x1_showcase` (archetype `params`/`size` deleted via RFC-7386
  nulls, `features` emptied, `tests` replaced) with entry-level
  `palette_swap` (steel→gold). Base doc = humanoid_front_scalable plus the
  six §H assertions that can pass on a character (all but `seamless`);
  variant = the tile archetype plus all seven (full-bleed cells make
  `seamless`/`cell_fill` meaningful) with both px-2 frames bound via `custom`
  (`frame` 0 and 1). NO existing archetype/recipe/example is modified —
  knight/seraph/scalable goldens stay byte-identical; only the two
  `x1_showcase*.json` goldens are new. Recipe tracks and a px **shape** are
  not showcased (a new recipe is out of scope; a px shape in a registry
  archetype cannot survive the lint sweep, see gap 10) — both stay ad-hoc
  verified from commits 3–4 instead.
- **D15 spec-next.** Proposed schemas stay untouched (X0 precedent): the
  incomplete frame schema (no `swap`/`params`/`hold`) is recorded as a gap
  rather than repaired, since 02/03 prose is the source X1 implements.
  `check_spec` keeps validating examples against the proposed schemas.

## Spec clarifications / gaps found (to report)

1. **X1 exit criterion is unmeetable as written** (needs X2/X3 content);
   reinterpreted per spec-next/07's "only what X2 needs" (see header).
2. 02 §G never binds px-2 frames to anything (no `frame` field anywhere) → D9's
   minimal binding is an addition beyond the prose.
3. Proposed archetype schema's frames lack `swap`/`params`/`hold` though 02 §F
   and 03 promise them; proposed asset schema's `variants` has an **undocumented
   `matrix` form** the prose never describes.
4. 02 §C names no code for `line` thickness violations, `mirror`+`rotate`, or
   an unknown swap set → new codes beyond §J: `E_SHAPE_PARAM`,
   `E_UNKNOWN_SWAP_SET` (+ `E_EXTENDS_PATH`, `E_EXTENDS_DEPTH`,
   `E_UNKNOWN_VARIANT` if triggered). 02 §E names `E_UNKNOWN_RAMP` in prose
   but leaves it out of §J's list (spec drift, recorded; implemented anyway).
5. §J lists `E_DUPLICATE_ID` as project-wide; without the project file only
   per-file uniqueness can be enforced now.
6. 02 §H `contrast_min`/`silhouette_distinct` presuppose the project file
   (backgrounds, peer assets) → split out per D13.
7. `facing`: only the atlas metadata half is implementable now (`"right"`);
   the runtime flip lives in the game.
8. Track validation scope: templates the asset does not bind are not compiled —
   `E_TRACK_PARAM` can only fire for bound templates (documented).
9. `min_feature`'s non-`detail` exclusion has no runtime marker (`affects` is
   documentation-only, 02 §D) → every shape counts; recorded in LANGUAGE.md
   and DECISIONS #15.
10. Showcase coverage limits: a px **shape** (vs a `custom` entry) inside a
    registry archetype cannot be linted — the size sweep compiles with
    `base_dir=ROOT` while assets resolve fragments against their own
    directory, and one fragment path cannot serve both — and a recipe-track
    showcase would need a new recipe; both stay ad-hoc verified (commits 3–4).

## Risks & mitigations

- **Existing goldens move** → X1 touches no shipped archetype/recipe geometry;
  acceptance runs `git diff pre-x1 --stat -- arcforge/tests/golden arcforge/tests/roms`
  (new files only) after every commit.
- **Repeat/mirror explosion in sweep cost** (bounded shapes × sizes × variants)
  → caps in D3 (256/2-levels); measure `make check` time — parent suite was
  457 s at `d96af7f`; if a commit would push it over the 500 s rule, stop and
  report first.
- **Variants double goldens/renders** → new ids only; loop cost is linear and
  exercised by one asset.
- **Tracks re-render frames with changed env** → auto-canvas union bbox already
  spans frames; determinism: no clocks, sorted iteration, integer only.
- **Fixture exact-set matching** → craft minimal assets, run before writing
  expectations (X0 practice).
- **Spec drift** → 02 status line updated in commit 5, PROPOSED banners stay,
  `make spec-check` green each commit.

## Acceptance (each commit)

1. `make check` + `make spec-check` ALL GREEN (arcforge), zero warnings.
2. Before every commit: repo-root `make clean && make check` exits 0, zero
   compiler warnings, < 500 s.
3. Every new `E_*` has a negative fixture with exact-set expectations; every
   test run twice is byte-identical (fixtures/goldens deterministic).
4. `git diff pre-x1 --name-status -- arcforge/tests/golden arcforge/tests/roms`
   shows only files the commit says it adds.
5. Render + LOOK at previews/contact sheet before any `golden` update; only
   per-asset `golden <file> --update` for new assets.
6. Commit 5: spec-next status line marks §C/E/F/G/H implemented-or-split, both
   gates green, report with last-3-lines of both make targets. Push only on
   explicit owner request.
