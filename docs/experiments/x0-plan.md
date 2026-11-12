# X0 plan — arcforge variable-size core (v2.1 subset)

Branch `x0-variable-size`, tag `pre-x0` (both gates ALL GREEN at the tag).
Scope A–H of the X0 card exactly; NOT in scope: mirror/line/repeat/px-shape/
locals beyond recipe locals, variants/extends/palette_swap, px 2, parameter
tracks, swap frames, spec 02 §H assertions, tiles/atlas/levels, C, P0.

## Scope → commit (as prescribed by the card)

| commit | message | scope |
|---|---|---|
| 1 | x0: expression env, metrics, size params, percent, new diagnostics + fixtures | A, B, D, E minus sweep codes |
| 2 | x0: auto canvas, clip_policy, atlas pivot | C (+ E_CLIPPED, fixture harness object form) |
| 3 | x0: size-sweep lint + contact sheet | F, W_FEATURE_TOO_SMALL, W_OCCLUDED, E_BODY_ASYMMETRY |
| 4 | x0: humanoid_front_scalable archetype, recipes, examples, new goldens | G (+ keyed geometry, E_RECIPE_GEOMETRY; size fixtures E_SIZE_RANGE/E_CELL_TOO_SMALL/E_RECIPE_SIZE_RANGE land here because they need the ranged archetype/sized recipe to exist) |
| 5 | x0: migrate schemas/docs out of spec-next, mark implemented | H |

## Files per commit (all paths relative to repo root)

1. `docs/experiments/x0-plan.md` (this file); `arcforge/schema/archetype.schema.json`
   (size, classes, proportions, metrics, min_cell, anchors `at` int|string);
   `arcforge/schema/asset.schema.json` (`arcforge` enum 2.0|2.1, `archetype.size`);
   `arcforge/schema/recipe.schema.json` (`sizes`, `locals`, `percent`);
   `arcforge/tools/arcforge_ref.py`; `arcforge/docs/LANGUAGE.md`;
   `arcforge/docs/RECIPES.md`; `arcforge/tests/invalid/unknown_size_param.json`
   + `expectations.json`.
2. `arcforge/schema/asset.schema.json` (canvas oneOf fixed|auto, `clip_policy`);
   `arcforge/tools/arcforge_ref.py`; `arcforge/tests/run_tests.py`
   (object-form expectations); `arcforge/tests/invalid/clipped.json` +
   `tests/invalid/fragments/block.px` + `expectations.json`;
   `arcforge/docs/LANGUAGE.md`.
3. `arcforge/tools/arcforge_ref.py` (sweep fn + `sweep` command + lint wiring,
   W_* checks); `arcforge/schema/style_pack.schema.json` (`min_feature_px`);
   `arcforge/registry/styles/arcade_16.style.json` (min_feature_px 1) +
   `registry/styles/arcade_16_min3.style.json` (new, fixture pack);
   `arcforge/tests/{run_tests.py,invalid/*}` + `expectations.json`;
   `arcforge/docs/LANGUAGE.md`.
4. `arcforge/registry/archetypes/humanoid_front_scalable.archetype.json` (new);
   `arcforge/registry/recipes/humanoid/{helmet_closed,boots_heavy,energy_sword}.recipe.json`;
   `arcforge/schema/recipe.schema.json` (geometry array|keyed object);
   `arcforge/tools/arcforge_ref.py` (keyed geometry, recipe-doc dispatch);
   `arcforge/examples/hero.scalable_{small,big,auto}.json` (new);
   `arcforge/tests/golden/hero.scalable_{small,big,auto}.json` (new only!);
   `arcforge/tests/invalid/{size_range,cell_too_small,recipe_size_range,
   percent_unknown_of,keyed_geometry}.json` + `expectations.json`;
   `arcforge/docs/{DECISIONS,RECIPES,LANGUAGE}.md`.
5. `arcforge/spec-next/{01_VARIABLE_SIZE,02_LANGUAGE_EXTENSIONS}.md`
   (IMPLEMENTED markers, PROPOSED banners stay); `arcforge/spec-next/check_spec.py`
   (sweep check → real implementation); `arcforge/spec-next/README.md`;
   `arcforge/docs/{LANGUAGE,RECIPES,ARCHITECTURE}.md` (final reconciliation).

## Design decisions

**D1 — per-archetype recipe geometry (the G decision).** `geometry` may be an
array (today's meaning: shared by every archetype in `applies_to`) or an object
keyed by archetype name. `applies_to` is unchanged and stays the applicability
source. A missing key for the compiling archetype (or a key naming an archetype
not in `applies_to`) is `E_RECIPE_GEOMETRY`, checked at asset compile and when a
recipe document is validated/linted. Reason: one recipe file keeps one identity,
one parameter set and one `examples` list while serving `humanoid_hero` (fixed
art) and `humanoid_front_scalable` (metric art); the array form means the three
existing recipes' hero geometry is byte-identical after the wrap, so knight and
seraph goldens cannot move. Rejected: per-archetype `parameters` (two parameter
vocabularies per recipe; percent/locals already adapt through the env), and
separate `*_scalable` recipes (duplicated identity/`applies_to`/examples, the
RECIPES anti-pattern "two recipes differing only in geometry params").

**D2 — expression environment & resolution order** in `compile_asset`:
cell (fixed canvas, or archetype `canvas.default` as *provisional* under auto)
→ size params (asset values validated: unknown key `E_UNKNOWN_SIZE_PARAM`
with suggestions+fix, out of range `E_SIZE_RANGE` with clamp fix; missing keys
take archetype defaults) → class resolution + `$p_*` proportions row →
archetype params (percent-`of` resolution needs size params first) → metrics,
ordered, each floored (`to_int`) → `min_cell` (fixed canvas: `E_CELL_TOO_SMALL`)
→ anchors (`at` may be expressions, evaluated once per asset) → shapes /
recipes (`locals` ordered, floored, recipe-local) / hitboxes.
`$class`/`$class_idx`: resolved class = smallest `max >= size`, else last class;
archetypes without `classes` get `$class="default"`, `$class_idx=0`.
Env is a superset (W,H,size,class,p_*,archetype params,metrics); `E_PARAM_SHADOW`
keeps recipe params from clashing with it.

**D3 — percent parameters.** `{type:"percent", of, default, min_px, min?, max?}`;
`px = max(min_px, (of*pct + 50) // 100)` bound as the parameter's value.
`of` names an env dimension (size parameter or metric); if the archetype has no
such dimension the parameter is *unavailable*: supplying it is
`E_UNKNOWN_SIZE_PARAM`, omitting it leaves it unbound (so hero recipes keep
working — see D9). `min`/`max` are percent bounds, default 0..100, range error =
`E_PARAM_RANGE`. Spec gap: 01 §7 defines only `{of, default, min_px}`; bounds are
required so RECIPES' "every combination stays inside the canvas" and the sweep's
extreme-parameter pass are authorable at all.

**D4 — auto canvas (one pass, no re-render of geometry).** Metrics/anchors are
evaluated at the provisional cell = archetype `canvas.default`. Every frame of
every animation renders on an *unbounded* plane (`render_frame(..., clip=False)`,
outline included); union bbox → +`margin` → round each dimension up to
`multiple_of` → place per `align` → translate grid, anchors, hitboxes (drawables
are already rasterized, so translation is a grid shift; nothing is re-derived).
`align`: `top_left` puts bbox at (margin,margin); `pivot_center` /
`pivot_bottom_center` aim the origin anchor at cell centre / bottom-centre, then
clamp so the bbox keeps ≥ margin on all sides (spec ambiguity, report).
Final cell = round_up(max(bbox+2·margin, min_cell)) ⇒ auto can never clip.
`atlas.json` gains `pivot` = base-frame origin-anchor position in the cell
([0,0] if the archetype has no `origin`). Metrics seeing the provisional cell
rather than the final cell is a spec gap → report.

**D5 — clip_policy.** Default `error` for all documents (01 §2; knight/seraph
measure 0 clipped px, verified). `run_tests` implicit check emits `E_CLIPPED`
(error) or `W_CLIPPED` (warn) accordingly. `lint`'s synthetic per-recipe assets
pin `clip_policy:"warn"` to preserve the exact baseline: `recipe:energy_sword
example#1` clips 1 px at 32×32 today (pre-existing contradiction of AGENTS
checklist rule 5 — recorded here; not fixed in X0 to avoid touching recipe
examples without a render review).

**D6 — size sweep (F).** Lives inside `lint` and as a standalone `sweep` command
(the fixture vehicle; it schema-validates the archetype file first). For every
archetype with `size` ranges: sizes = `size.min`, each class boundary `max` and
`max+1` (both sides), `size.max`, deduped/clamped, primary param varied, others
at default. Cell = `min_cell(size)` (else archetype `canvas.default`) — this
makes the sweep an executable check of the archetype's own min-cell formula.
Per size: body-only symmetry on the base frame (mirror about `$cx`, colours
(ramp,tone)) when `symmetric`; clipping over the base frame **and every frame of
every animation template**; then every applicable recipe (in `applies_to`) alone
on the body, at param sets = defaults + authored `examples` + derived extremes
(int min/max, bool both values, enum/ramp all values, percent declared min/max
else 0/100), deduped, skipped outside a recipe's `sizes` range. Checks reuse the
always-on `run_tests` diagnostics (palette budget, min feature, occlusion,
clipping). Output: JSON table in the report (`stats.sweep`) + written to
`out/sweep/<archetype>.table.json`, contact-sheet PNG to
`out/sweep/<archetype>.contact_sheet.png` (bottom-aligned cells, integer scale).
Sweep failures are errors ⇒ `lint` fails ⇒ `make check` fails.

**D7 — diagnostics placement & fixtures.**

| code | commit | fires | fixture (tests/invalid) | run |
|---|---|---|---|---|
| E_UNKNOWN_SIZE_PARAM | 1 | compile (archetype.size keys; percent `of`) | `unknown_size_param` (+ `percent_unknown_of`, c4) | validate |
| E_SIZE_RANGE | 1 | compile | `size_range` (c4) | validate |
| E_CELL_TOO_SMALL | 1 | compile, fixed canvas | `cell_too_small` (c4) | validate |
| E_RECIPE_SIZE_RANGE | 1 | compile | `recipe_size_range` (c4) | validate |
| E_CLIPPED | 2 | run_tests, policy error | `clipped` (px fragment overhang) | test |
| W_FEATURE_TOO_SMALL | 3 | run_tests always-on (IR shape bbox min dim < `min_feature_px`) | `feature_too_small` (arcade_16_min3, sword thickness 1) | test |
| W_OCCLUDED | 3 | run_tests always-on (feature with 0 surviving painter pixels in *every* frame) | `occluded_feature` (full-cover px on `fx`) | test |
| E_BODY_ASYMMETRY | 3 | sweep, base-frame body | `symmetric_violation.archetype.json` | sweep |
| E_RECIPE_GEOMETRY | 4 | compile + recipe-doc validate/lint | `keyed_geometry.json` (a recipe document) | validate |

`expectations.json` keeps the legacy list form (run `validate`, exact error-set
match) and gains an object form
`{"run": "test"|"sweep", "errors": [...], "warnings": [...]}`; `run_tests.py`
matches the exact error set and exact warning set, and the status must be FAIL
iff `errors` is non-empty. `arcforge_ref.py validate` learns to dispatch on a
recipe document (like the existing style-pack dispatch).

**D8 — style-pack min feature.** `min_feature_px` (optional int ≥1) in the style
pack schema. `arcade_16` declares 1 (inert, honest); the fixture pack
`arcade_16_min3` declares 3. `W_FEATURE_TOO_SMALL` compares the *compiled*
shape bbox (integer, deterministic, no raster dependency); sweep reuses it.

**D9 — energy_sword / percent interplay (goldens guard).** The prototype's
percent `length`/`thickness` cannot replace the hero's int params (knight
passes `length:14`, seraph `length:10` — px today). The registry recipe keeps
its int `length`/`thickness`/`glow`/`angle` untouched and *adds* percent
`reach` (`of:height`, default 45, min_px 4) and `blade_w` (`of:height`,
default 8, min_px 1, bounds tuned) used only by the keyed scalable geometry.
On `humanoid_hero` (no `height`) both are unavailable and unbound ⇒ hero
rendering is bit-identical. Recipe `sizes` ranges bind the archetype's primary
size param = **first key of `size` in document order** (spec gap: 01 §9 does not
say; irrelevant for single-parameter archetypes).

**D10 — archetype nominal-extent contract.** `min_cell` may only reference size
parameters and metrics (it is archetype-level, recipe params are asset-level).
The scalable archetype carries nominal metrics (spec 01 §3's `$sword_reach`
pattern, e.g. `reach = max(4, round($height*60/100))` == the percent formula at
max pct) so every recipe percent bound fits inside `min_cell`; the sweep verifies
this at every size and parameter extreme.

## Spec clarifications / gaps found (to report)

1. `align` has three enum values (proposed asset schema) but no placement rule;
   pivot modes can conflict with the margin guarantee → clamp rule (D4).
2. Auto mode: which cell `$W/$H` see before the cell exists → provisional =
   archetype `canvas.default` (D4).
3. 01 §7 percent spec lacks range bounds and a missing-`of` rule (D3).
4. 01 §9 does not say which size parameter a recipe `sizes` range applies to
   (D9), nor whether sweep covers animation frames (we cover all frames for
   clipping, base frame for symmetry).
5. 01 §4 lists `odd/even/half`; the task scope matches, but 02 §A also lists
   `sign` and repeat variables → NOT implemented in X0 (X1).
6. `//` and `%` operators are added with floor semantics for negatives
   (task A's integer semantics); 02 §C only promises `%` with `repeat`.
7. Codes beyond spec 02 §J: `E_RECIPE_GEOMETRY`, `E_BODY_ASYMMETRY` — both
   required by X0 decisions, each with a fixture (rule 6).
8. Spec 01 §9 sweep check "recipe stays on its slot region" is NOT in the card's
   check list → deferred (reported, not implemented).
9. Baseline: lint already reports one `W_CLIPPED` (D5) — AGENTS checklist rule 5
   is violated at `pre-x0`.

## Risks & mitigations

- **Existing goldens move** (knight/seraph): v2.0 path must stay bit-identical;
  env only *gains* keys, anchors eval to the same ints, seq order unchanged.
  Verify every commit with `git diff pre-x0 --stat -- arcforge/tests/golden`
  (only new files ever) + `make check` golden tests.
- **clip_policy=error breaks lint** → D5 (lint pins warn; baseline output
  unchanged).
- **Auto-canvas chicken/egg** → provisional cell + translate (D4); fixture
  `clipped` proves the fixed-canvas error path.
- **Sweep/min_cell authoring** (cells too small ⇒ E_CLIPPED in sweep; too big ⇒
  empty sprites) → author from D10, render, LOOK at the contact sheet and the
  example previews *before* any `golden --update`, tune, re-run.
- **Fixture exact-set matching** (a second code appearing in a fixture output
  fails it) → craft minimal assets, verify outputs before writing expectations.
- **spec-check drift** → commit 5 points the sweep check at `arcforge_ref.py
  sweep` on the real archetype; PROPOSED banners stay (check_spec still asserts
  them), proposed schemas stay untouched (they document the whole proposal;
  implemented parts now exist in `schema/`).
- **Prototype compatibility** → `render_frame` only gains keyword-default
  parameters; `size_sweep.py` keeps working.
- **Determinism** → sorted iteration everywhere, integer math only, PNG/zlib
  fixed settings; acceptance runs each sweep/example twice and diffs bytes.

## Acceptance (each commit)

`make check` + `make spec-check` ALL GREEN (zero warnings), goldens diff vs
`pre-x0` shows only new files, every new code has a fixture, `out/` sweep
artifacts never staged, no push.
