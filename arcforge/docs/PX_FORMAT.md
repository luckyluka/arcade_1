# `.px` fragment format (v1)

The escape hatch for anything the vocabulary cannot express. Plain text, diffable, palette-role based (so style packs can restyle it).

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

Source of fragments: hand-drawn, or produced by an importer (planned M2: image → quantize to style pack → `.px`). A generative image model is an optional *producer of `.px`*, never part of the compiler.
