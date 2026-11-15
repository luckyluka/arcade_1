# Style bible — `platformer_24`

The reference for every human review of ArcForge art (spec-next/05 §1,
rubric dimension 2 "style-bible consistency"). It documents **only what the
registry already implements** — the machine-readable half lives in
`registry/styles/platformer_24.style.json` and the archetypes.

## 1. Palette and ramp roles

`platformer_24` ships six ramps (max 24 colours per asset):

| ramp | shadow | base | light | used for |
|---|---|---|---|---|
| `outline` | — | `[26,22,34]` | — | every outline pixel (near-black, never `#000`) |
| `red` | `[140,24,36]` | `[232,60,56]` | `[255,150,130]` | armour, cap, danger |
| `blue` | `[30,54,130]` | `[54,116,235]` | `[150,196,255]` | armour, overalls |
| `gold` | `[148,100,22]` | `[236,176,44]` | `[255,234,140]` | buckles, fire swap |
| `steel` | `[62,66,80]` | `[118,126,148]` | `[198,206,222]` | blades, fittings |
| `skin` | `[156,96,72]` | `[226,166,124]` | `[250,206,166]` | face, hands |

Tone roles per shape (the pack has **no** `shading.light` vector — tones are
picked by hand, so this rule keeps renders consistent):

- `base` — the part's local colour, the default for any new shape.
- `light` — a small highlight on the **upper/front** surface of a form
  (e.g. the toe cap of a boot).
- `shadow` — undersides and openings: soles, brim undersides, cuffs.

There is no per-asset palette invention: a new ramp means a style-pack change.

## 2. Outline rule

- Outer outline, ramp `outline`, drawn **outside** the filled shape
  (`outline.mode: outer`).
- The `back` layer (far limbs) gets its own outline pass
  (`separate_layers: [back]`), so the far arm/leg reads behind the torso.
- Inner part outlines separate head / torso / arms / legs from each other.
- Outline colour is `[26,22,34]` — never pure black, never transparent gaps.

## 3. Light direction

Top-front: highlights (`light`) sit on the upper and forward surfaces,
shadow tone on undersides. No dither (`shading.dither` unset) — flat fills
only.

## 4. Size classes and proportions (`humanoid_side`)

Canvas: side view, facing right, `height` 12–64 px, cell contract
`min_cell = [height + 4, height + 2]`.

| class | height max | `p_head` (%) | head height |
|---|---|---|---|
| `tiny` | 16 | 36 | `max(4, height·p_head/100)` |
| `small` | 28 | 32 | same formula |
| `medium` | 44 | 28 | same formula |
| `large` | 64 | 24 | same formula |

Bigger heroes get proportionally smaller heads (the classic pixel-platformer
curve); every metric (limb widths, eye size) derives from `height` alone, so
one parameter scales the whole figure.

## 5. Silhouette rules

- The flat shape must identify the character at 12 px: cap + forward brim,
  moustache block, two strap buttons, boots wider than the legs.
- No part exists only for interior colour — every part must touch the
  silhouette or break the outline (belt, buckle, gloves all do).
- Depth order is fixed by the archetype: back limb → far arm → torso → near
  arm → head; per-part offsets and leg swaps carry the animation.

## 6. Animation principles

Bindable templates (the only animation keys `asset.schema` accepts today):

| template | frames | fps | loop |
|---|---|---|---|
| `idle_breathe` | 2 | 4 | yes |
| `walk_cycle_6` | 6 | 10 | yes |
| `attack_slash` | 3 | 12 | no |
| `hurt` | 2 | 6 | no |
| `death` | 3 | 4 | no |

Principles: one-pixel offsets for breath and impact (larger reads as jitter),
poses carried by `swap` (leg fwd/lift/back) rather than new geometry, every
template's first frame = the neutral pose.

## 7. Forbidden

- **Pure black** (`[0,0,0]`) anywhere, including outlines.
- **Single-pixel noise**: stray pixels that vanish at the min size; measured
  `min_feature` is ≥ 2 px in the hero sweep (pack floor is 1, characters
  hold 2).
- Colours outside the six ramps; more than 24 colours in one asset.
- Details smaller than the class's eye (a 2×2 eye at hero sizes is the
  floor — cap `crown` below 30% hides it).

## 8. Reference sheets

- Hero small/big/fire: `out/hero.knight.small/preview.png`,
  `out/hero.knight.big/preview.png`, `out/hero.knight.fire/preview.png`
- Family sweep: `out/sweep/humanoid_side.contact_sheet.png` (+ `.table.json`)
- Gate review record: `art/reviews/001-g1x.md`
