# Architecture

```
registry (archetypes, recipes, style packs)       asset.json (+ fragments/*.px)
                │                                          │
                └──────────────┬───────────────────────────┘
                               ▼
        1 schema validation (strict)  ──▶ E_SCHEMA / E_UNKNOWN_FIELD
        2 registry validation         ──▶ unknown names, params, slots, anchors, layers, templates
        3 compile to IR               ──▶ drawables: polygons/ellipses/pixel stamps in fixed-point
        4 rasterize per frame         ──▶ painter's algorithm, part offsets applied
        5 post-pass                   ──▶ outline, separate layers
        6 measure + assert            ──▶ palette, clipping, asset tests
        7 export                      ──▶ frames, spritesheet + atlas.json, (later) ARCADE-1 asset
```

## IR
A flat list of drawables `{kind, geometry, fill(ramp,tone), layer index, sequence, part, source}` plus anchors, hitboxes and animation frames. The IR is the stable interface between front end (language) and back end (rasterizer/exporters); tests can assert on it.

## Determinism contract
- Integer / 1/64-pixel fixed point for all raster decisions; pixel-centre sampling; even-odd rule with integer cross-multiplication.
- Trig from an integer-degree table rounded to 1e-6; rotated vertices rounded to 1/64 px with half-up integer rounding.
  **P0.4: the table is a generated artifact, not a per-run computation.**
  `tools/gen_trig_table.py` writes `c/gen/trig_table.h` (the C build
  includes it and **never calls libm**) and `tests/trig_table.json` from
  `int(round(cos|sin(deg) * 1e6))` for deg −180..180 inclusive,
  round-half-even exactly as Python's `round()` (no exact `.5` tie occurs
  in this table, so the tie rule never decides a value).
  `tests/test_trig_table.py` pins: both files byte-identical to a fresh
  regeneration, every entry equal to the oracle's `COS`/`SIN`, the
  header's array text equal to the JSON when parsed back, the `--check`
  CLI contract, and the values at the reference angles. Regenerate with
  `python3 tools/gen_trig_table.py`; `--check` verifies without writing.
- No RNG, no time, no map-order dependence (sort by (layer, sequence)).
- Registry and tool versions are part of the build identity; golden hashes are per frame (SHA-256 of RGBA).

## Targets
`arcade1-draft` profile only. Canvas sizes are archetype-supported (32×32 for `humanoid_hero`). The ARCADE-1 display budget (roughly 256×144 to 384×216) comfortably fits sprites of this size. The export format `arcade1_asset` is reserved until the console's asset/tile/sprite format is fixed (do not guess it).

## Where AI models fit
Only at the edges: (a) authoring/repairing the JSON through the diagnostic loop; (b) optionally producing `.px` fragments from reference images; (c) visually reviewing renders. The compiler itself never calls a model.

## Implementation plan
Python reference (this repo) until the format settles; then the standalone C compiler (ROADMAP M4) that must reproduce `tests/golden` and every diagnostics report exactly. Both read the same `schema/` and `registry/` data files.
