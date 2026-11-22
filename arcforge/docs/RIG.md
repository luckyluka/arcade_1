# Rigs: the short way to make a new hero

A **rig** is a ~50-line JSON file. `tools/rig_build.py` expands it into the full archetype (parts, layers, anchors,
animation frames), checks it with ArcForge's own validator, renders a preview and tells you what is wrong in one line
per problem. This exists so small local models can make archetypes: they only write the rig, never the 1000-line JSON.

## Workflow (the whole thing)
1. `cp rigs/knight_front.rig.json staging/<name>.rig.json` (or `rigs/mage_front.rig.json`; both are known good).
2. Edit it: new `name`, shapes, colours, animation part lists.
3. `python3 tools/rig_build.py staging/<name>.rig.json`  → read the output, fix, rerun (max 5 attempts).
4. When it prints `OK` and a `PREVIEW:` path, open that PNG (rows: base, then every animation).
5. Happy? `python3 tools/rig_build.py staging/<name>.rig.json --accept` adds it to `registry/` (runs the lint gate).
6. See variants: `python3 tools/gen_assets.py --archetype <name> --n 60 --out out/gen` and open `out/gen/sheet.png`.

## Canvas and coordinates
`"size": [96, 96]`. x goes right, y goes DOWN, (0,0) is the top-left pixel. The feet touch y = 93, the centre line is
x = 48. Keep every pixel inside the canvas (hat tips at y >= 2). Pixels outside the canvas are an error.

## Shapes (one list each, numbers only, in pixels)
| Shape | Form | Notes |
|---|---|---|
| capsule | `["capsule", fill, x1, y1, x2, y2, width]` | thick line with round ends; add `{"flat": true}` for square ends (torso, robe, belt) |
| ellipse | `["ellipse", fill, cx, cy, rx, ry]` | centre and radii |
| rect | `["rect", fill, x, y, w, h]` | flat colour only |
| poly | `["poly", fill, [[x,y],[x,y],[x,y]]]` | flat colour only (hat cones, spikes) |

Any shape may end with an options dict: `{"layer": "armor"}` puts just that shape on another layer.

## Fills
`T` = team colour, `S` = steel, `K` = skin, each as a ramp of 5 tones (`skin` has 4).
- `"S"` on a **capsule or ellipse** = shaded as a volume (looks round). This is the nice look.
- `"S.base"`, `"T.deep"`, `"S.high"` = one flat tone (`deep, shadow, base, light, high`). **rect and poly must be flat.**
- `"outline"` = the dark outline colour (eyes, details).
One asset can use at most 15 colours: outline + one team ramp + steel + skin. Do not invent other ramps.

## Layers and parts
Layers draw back to front: `back, body, armor, held, foreground, fx`. Inside a layer, parts and shapes draw in the
order they are written. Each part is `{"layer": "body", "shapes": [...]}`. A part moves as one piece in animations,
so split the character into parts that move separately (legs/feet, arm, head...). Keep the part names `head`, `helmet`,
`torso`, `arm_l`, `arm_r`, `leg_l`, `leg_r`, `pauldron_l`, `pauldron_r`, `plume` when they exist: the style draws
inner outlines for those.

`"anchors": {"hand_r": ["arm_r", x, y]}` marks where a held item attaches. Optional.

## Animations (use presets; the key is the template name)
```json
"idle_breathe": {"preset": "bob",   "parts": ["torso","head"], "fps": 4},
"walk_cycle":   {"preset": "walk",  "legs": ["leg_l","leg_r"], "body": ["torso","head"], "lift": 4, "fps": 10},
"attack_slash": {"preset": "swing", "part": "arm_r", "pivot": [60,50], "degrees": [0,-35,-70,-20,40,70,40,10],
                 "bob": [0,0,-1,0,1,1,0,0], "body": ["torso","head"], "hit_frames": [4,5]},
"hurt":         {"preset": "hurt",  "parts": ["torso","head","arm_l","arm_r"]},
"death":        {"preset": "death", "parts": ["every","part"], "keep": ["feet","parts"], "angle": 18, "drop": 6}
```
- `swing` rotates one part about the **pivot** (the shoulder, in canvas pixels). Positive degrees = clockwise.
  Put the arm AND the weapon in that one part so they swing together. Ellipses do not rotate (only their centre moves),
  so build limbs and weapons from capsules.
- `death` leans the character back about the feet. Tall or wide characters clip: lower `angle` (try 12) and `drop`.
- Names that start with `idle`, `walk`, `attack`, `hurt` or `death` are picked up automatically.

## Common errors, quick fixes
| Message | Fix |
|---|---|
| `fill 'X' must start with T, S, K` | use T, S, K or outline |
| `capsule = [...] (got 4 numbers)` | capsule needs x1,y1,x2,y2,width |
| `part 'tail' is not defined` | list only parts that exist in `parts` |
| `E_CLIPPED ... fall outside the canvas` | move or shrink shapes; for death lower angle/drop; for attack reduce the swing degrees |
| `palette_size` | you used a fill outside T, S, K, outline |

## Prompt to give a local model
> Read docs/RIG.md and rigs/mage_front.rig.json. Create a new hero rig at staging/<name>.rig.json: <describe it>.
> Copy the structure of the mage rig, change shapes and colours. Run `python3 tools/rig_build.py staging/<name>.rig.json`,
> fix every reported error from its message, and stop after 5 attempts. Do not edit anything under tools/ or schema/.
> When it prints OK, run it with `--accept` and tell me the PREVIEW path.
