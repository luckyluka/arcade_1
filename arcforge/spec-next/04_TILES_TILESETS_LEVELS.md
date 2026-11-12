# 04 — Tiles, tilesets, levels

> PROPOSED.

## Tiles
- Archetype `tile`: cell = `g·cols × g·rows`, fixed by `g` (8/16/32) and multiples. `tileable: { "x": true, "y": true }`.
- Tiles use `outline_override: "none"` (no silhouette outline); edges are drawn by recipes (e.g. `surface_grass` draws its own top lip).
- `seamless` assertion (02 §H): tileable borders opaque, no outline/transparent pixels there.
- Pattern art uses `repeat`:
```json
{ "shape": "repeat", "var": "r", "count": "floor($g/4)", "max": 16, "step_y": 4, "shapes": [
    { "shape": "rect", "layer": "body", "x": 0, "y": "$r*4+3", "w": "$g", "h": 1, "fill": "$brick.shadow" },
    { "shape": "repeat", "var": "c", "count": "floor($g/8)+1", "max": 8, "step_x": 8, "shapes": [
        { "shape": "rect", "layer": "body", "x": "$c*8+(($r%2)*4)", "y": "$r*4", "w": 1, "h": 3, "fill": "$brick.shadow" } ] } ] }
```
(`%` is added with `repeat`; integer modulo, non-negative operands only.)
- **Autotiling**: `variants.matrix` expands a tile into edge variants: 
```json
"variants": { "matrix": { "edges": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15] }, "id_format": "{id}.e{edges}", "bind": { "edges": "archetype.params.edges" } }
```
bit 0=N,1=E,2=S,3=W "has neighbour". `blob47` mode later.

## Tileset / atlas manifest (`*.tileset.json`)
```json
{ "arcforge_tileset": "1.0", "id": "ts.level1", "padding": 1, "extrude": 1, "max_size": [512, 512],
  "pack": "shelf_height_desc_id", "trim": true,
  "entries": [ { "asset": "tile.ground.top" }, { "asset": "hero.knight.small" }, { "asset": "item.coin", "animations": ["spin"] } ],
  "output": { "png": "build/atlas/ts_level1.png", "json": "build/atlas/ts_level1.json" } }
```
Packing is deterministic: sort by (height desc, id), shelf algorithm, power-of-two optional (`pow2: true`). Output JSON per frame: `id, animation, index, x, y, w, h, trim{x,y,w,h}, pivot{x,y}, source_cell{w,h}, fps, loop, facing, hitboxes`. Errors: `E_ATLAS_OVERFLOW`, `E_UNKNOWN_ASSET`. Extrusion avoids sampling bleed.

## Level format (`*.level.json`)
```json
{ "arcforge_level": "1.0", "id": "level.1", "tile_size": 16, "sky": "sky_blue",
  "legend": { "G": "tile.ground.top", "g": "tile.ground.fill", "B": "tile.brick", "?": "tile.block_question", "P": "tile.pipe.top" },
  "layers": [
    { "id": "far",  "kind": "scenery", "parallax": 50, "items": [ { "asset": "scenery.hill.large", "at": [0, 11] } ] },
    { "id": "main", "kind": "tiles", "rows": [ "................", "................", "....?B?B........", "GGGGGGGGGGGGGGGG" ] },
    { "id": "ents", "kind": "entities", "items": [ { "asset": "enemy.walker.small", "at": [9, 12], "facing": "left" } ] } ],
  "spawn": [2, 12] }
```
`rows` arrays are padded with `.` (empty). Scenery positions are in tile coordinates (bottom-left pivot). Entities anchor by their `pivot`.

## Commands (proposed)
| command | purpose |
|---|---|
| `arcforge atlas TILESET` | pack and write atlas + JSON |
| `arcforge level-lint LEVEL` | legend ids exist, tile size matches `g`, no entity inside solid tiles unless allowed |
| `arcforge level-preview LEVEL [--scale N] [--frame K]` | composite the whole level to one PNG (and a per-screen strip), using real compiled assets |
| `arcforge contact-sheet ASSET...|--family PREFIX` | grid of all frames with labels, backgrounds from the level's sky and ground |
| `arcforge diff-render OLD NEW` | red/green overlay PNG of changed pixels per frame |

Judge art in context: always review the level preview, not a sprite on a flat background.

## Export to ARCADE-1
Reserved. The exporter reads atlas + JSON and emits the console's tile/sprite banks once the `.arc1` asset format is fixed. Do not guess the format.
