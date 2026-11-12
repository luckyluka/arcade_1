# 05 — Quality and iteration

> PROPOSED. Principle: structure and numbers are checked by machines, taste is reviewed by humans (and models), and every review is recorded.

## 1. Style bible (required before any asset)
`art/STYLE_BIBLE.md` with: palette and ramp roles, outline rule, light direction, size classes in use, silhouette rules (each character recognisable as a flat shape), proportion tables per class, animation principles (frame counts, timing), forbidden things (pure black, single-pixel noise), reference sheet. The machine-readable part lives in the style pack (`min_feature_px`, `max_colors`, ramps, outline) and archetype tables.

## 2. Four loops
| loop | scope | cadence | tools |
|---|---|---|---|
| asset | one asset | minutes | validate → render → look → JSON Patch |
| family | all assets sharing a recipe/archetype | per recipe change | size sweep, contact sheet, `impact` |
| level | everything in context | per milestone | level preview at 1× and 4×, in-game capture |
| release | whole level | before freeze | blind review, metrics report |

## 3. Commands that make iteration cheap
- `contact-sheet`: every frame of a family on the level's real backgrounds.
- `diff-render OLD NEW`: changed pixels per frame as an overlay.
- `impact RECIPE|ARCHETYPE|STYLE`: list of assets that depend on it, so a recipe tweak shows its blast radius before it is committed.
- `size-sweep ASSET`: the asset at neighbouring sizes.
- `report`: all metrics for the project as JSON and a one-page HTML.

## 4. Automatic quality metrics (precision)
| metric | definition | default threshold |
|---|---|---|
| palette adherence | every pixel uses a style pack colour; colours ≤ `max_colors` | exact |
| min feature | smallest filled-shape dimension of non-detail shapes | ≥ `min_feature_px` (2) |
| outline consistency | every opaque pixel bordering transparency is outline (when the style requires it) | 100 % |
| orphan pixels | isolated single pixels | ≤ 2 per frame |
| contrast | WCAG-style luminance ratio of main body colour vs each level background | ≥ 3 : 1 |
| silhouette distinctness | IoU between base silhouettes of characters at matched pivots | ≤ 0.80 |
| baseline stability | foot anchor y across a ground animation | 0 px drift |
| jitter | centre-of-mass step between consecutive frames | ≤ 2 px |
| cell fill | opaque / cell | 0.25–0.75 for characters |
| tile seam | `seamless` | pass |
Thresholds are project settings (`arcforge.project.json`), versioned.

## 5. Human review protocol
- Blind: the reviewer sees contact sheets without recipe names.
- Rubric (1–5 each): silhouette readability, consistency with the style bible, animation feel, charm/character, fit in the level context.
- Pass rule per family: mean ≥ 3.5 and no score below 2. Recorded in `art/reviews/NNN-<topic>.md` with before/after sheets, decision and next changes.
- Gate G1 (extended): hero small and big pass the rubric, and the size sweep shows no failing size inside the declared range.

## 6. AI-assisted review
A vision model receives the contact sheet, the style bible and the metrics report and returns critique mapped to **parameter changes** as JSON Patch proposals against specific assets/recipes. Proposals go through `validate` and `diff-render`; a human approves. The model never edits goldens or thresholds.

## 7. Versioning and goldens
- Registry content is semver'd: recipe patch = no pixel change; minor = pixel change within params; major = interface change.
- Any pixel change bumps the version and regenerates goldens in the same commit; the `diff-render` overlay is attached to the review note.
- Assets pin `compat` ranges for archetype and style pack; mismatch → `E_COMPAT`.
- Golden hashes are per frame; large refactors verify first with "no diff" renders.

## 8. Iterating a single hero (worked loop)
1. Set `height: 16`, class `tiny`; compile; contact sheet at 8×.
2. Fix silhouette first (proportion table `p_head`, `p_leg`), then colour roles, then detail.
3. Re-run size sweep to see which neighbouring sizes broke; add `when` detail tiers or class overrides.
4. Compare `diff-render` against the previous commit; write the review note.
5. Move to the big variant: `variants` override only what differs.
