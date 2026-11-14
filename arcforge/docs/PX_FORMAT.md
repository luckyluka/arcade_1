# `.px` fragment format (v1 + v2)

The escape hatch for anything the vocabulary cannot express. Plain text, diffable, palette-role based (so style packs can restyle it).

## v1 — static

```
px 1
name claw
size 6 6
anchor attach 2 0          # fragment pixel that sits on the host anchor (required)
legend
  . transparent
  m steel.base
  s steel.shadow
  e red.light
rows
  .mmmm.
  .msem.
  ...
```
Rules
- First statement `px 1`. `#` starts a comment line (so `#` cannot be a pixel character).
- `size W H`; exactly H rows of exactly W characters; every character must be in the legend.
- Legend values: `transparent`, `outline`, or `<ramp>.<tone>` from the style pack. No raw RGB in v1.
- `anchor attach X Y` must lie inside the fragment.
- Static only in v1. The fragment follows the part of the anchor it is attached to.
- Errors: `E_PX_NOT_FOUND`, `E_PX_PARSE` (with line number), `E_PX_LEGEND`.

## v2 — frames (2.1, spec 02 §G)

```
px 2
name coin
size 8 10
frames 4                  # how many frames follow; must match the blocks
fps 10                    # recorded for the animation half (X2/X5); not consumed by frame selection
anchor attach 4 9
legend
  . transparent
  y gold.base
  l gold.light
frame 0
  ...rows...
frame 1
  ...rows...
```
Rules
- First statement `px 2`. `frames N` (N ≥ 1) is required; `fps N` is optional and **recorded only** — the compiler never consumes it yet (spec 02 §G shows `fps` but never binds it; frame selection is explicit, see below).
- `frame i` blocks: exactly one block each for `i = 0..N-1` (any order), each exactly H rows of exactly W characters. All frames share `size`, `legend` and `anchor` — they are declared once, outside the blocks.
- v1 files stay valid; a v1 file may not use `frames`/`fps`/`frame`, and a v2 file may not use the v1 `rows` block. All frame-protocol violations are `E_PX_FRAMES`.
- **Binding (the spec's gap, minimal honest binding):** a `frame` field (integer ≥ 0, default 0) on the `px` shape and on `custom` selects which block is drawn; a frame the fragment does not have is `E_PX_FRAMES` at that field. Animation *consumption* (auto-advancing px frames) stays for X2/X5 — it is not implemented, so nothing claims it.
- Errors: `E_PX_NOT_FOUND`, `E_PX_PARSE`, `E_PX_LEGEND`, `E_PX_FRAMES`.

Source of fragments: hand-drawn, or produced by an importer (planned M2: image → quantize to style pack → `.px`). A generative image model is an optional *producer of `.px`*, never part of the compiler.
