# 06 — Project structure

> PROPOSED.

```
platformer-l1/
├─ arcforge.project.json         # engine version, search paths, style, thresholds, outputs
├─ arcforge/                     # engine: submodule or vendored (tools/, schema/, registry/)
├─ art/
│  ├─ STYLE_BIBLE.md
│  ├─ style/                     # platformer_24.style.json
│  ├─ registry/                  # project content, overrides engine registry only with "override": true
│  │  ├─ archetypes/             # humanoid_side, walker, shellback, tile, item, effect, scenery, font_bitmap, icon
│  │  └─ recipes/{hero,enemy,tile,item,fx,scenery}/
│  ├─ assets/
│  │  ├─ characters/             # hero.knight.json (variants: small, big, fire)
│  │  ├─ enemies/  tiles/  items/  scenery/  fx/  ui/
│  │  └─ fragments/              # .px / .px v2, grouped by asset
│  ├─ tilesets/                  # *.tileset.json
│  ├─ levels/                    # level1.level.json
│  ├─ golden/                    # per-frame hashes (committed)
│  └─ reviews/                   # NNN-topic.md + before/after sheets
├─ build/                        # generated, git-ignored: frames, atlas, previews, reports
├─ game/
│  └─ export/                    # ARCADE-1 exporters (later)
└─ Makefile
```

## Naming
Asset ids: `<kind>.<family>.<name>[.<variant>]` (`hero.knight.small`, `tile.ground.top`, `enemy.walker.small`). File path mirrors the id's kind/family. Ids are unique project-wide.

## `arcforge.project.json`
```json
{ "arcforge_project": "1.0", "engine": ">=2.1.0",
  "registry_paths": ["arcforge/registry", "art/registry"],
  "style_pack": "platformer_24",
  "defaults": { "canvas": { "mode": "auto", "margin": 1, "multiple_of": 1, "align": "pivot_bottom_center" }, "clip_policy": "error" },
  "backgrounds": { "sky_blue": [92, 148, 252], "ground_dark": [60, 40, 20] },
  "thresholds": { "min_feature_px": 2, "orphan_pixels": 2, "contrast_min": 3.0, "silhouette_iou_max": 0.8, "jitter_max": 2 },
  "tilesets": ["art/tilesets/level1.tileset.json"], "levels": ["art/levels/level1.level.json"],
  "outputs": { "build": "build", "golden": "art/golden" } }
```
Precedence: later `registry_paths` override earlier ones only for entries with `"override": true`; otherwise `E_REGISTRY_SHADOW`.

## Makefile targets
`check` (lint + size sweep + assertions + goldens), `render`, `contact-sheet`, `atlas`, `level-lint`, `level-preview`, `report`, `export`, `review-new`.

## CI order
lint → validate all assets → render → assertions → goldens → atlas → level-lint → level-preview → report; fail on any error, publish previews and the diff overlays as artefacts.

## Git conventions
Commit `art/**`, goldens and reviews. Ignore `build/`. One commit = one visual intent (recipe tweak, new asset, style change) with its golden diff and review note.
