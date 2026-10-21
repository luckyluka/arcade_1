# The asset library

Reusable asset packs. Every pack is **code**: a generator, a
manifest fragment, a style sheet and the strips it produces —
regenerate a pack and you get the same bytes.

```sh
make art-library          # regenerate every pack
make art-library-check    # prove a fresh build is identical
arc1-art merge <pack>...  # compose packs into a game's manifest
```

| pack | what it is for | assets |
|---|---|---:|
| `icons` | Five small gameplay icons (coin, heart, key, potion, arrow) — the HUD's vocabulary. | 5 |
| `characters` | Two rigged characters (the humanoid reference and the samurai) — eight states each, built from parts and keyframes rather than drawn frame by frame. | 32 |
| `village` | A village's cast and ground: a farmer, a slime, four harvests and a three-tile ground (grass, a paved square, a pond). | 9 |
| `lighthouse` | A lighthouse's cast and tiles: the keeper (three states), the tower lamp, and four tilesets (a dusk sky, the tower, the cliff, the sea). | 13 |
| `tiles` | Twelve seamless 16x16 surface tiles in one tileset: grass, dirt, stone, brick, wood, sand, water, roof, floor, road, snow, lava. | 1 |
| `effects` | Five particle strips for a game's effects: dust, smoke, a spark, a splash and a flame. | 10 |
| `ui` | The interface kit: a panel in three sizes (a nine-piece tileset plus a map per size), a cursor and a counter pip. | 6 |
| `backgrounds` | What is behind the game: three seamless parallax ridge strips, a tileable star field, a cloud and a moon. | 8 |
| `enemies` | Five opponents: a slime, a bat, a rat, a spider and a walking skeleton (a rig). | 16 |

**100 assets in 9 packs.**

## `icons`

Five small gameplay icons (coin, heart, key, potion, arrow) — the HUD's vocabulary.

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* animation
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `coin` | sprite | 8x8 | 1 | props16 |
| `heart` | sprite | 8x8 | 1 | props16 |
| `key` | sprite | 8x8 | 1 | props16 |
| `potion` | sprite | 8x8 | 1 | props16 |
| `arrow` | sprite | 8x8 | 1 | props16 |

**Review:** **They work.** At 1x — the size the machine shows them — each of the five

## `characters`

Two rigged characters (the humanoid reference and the samurai) — eight states each, built from parts and keyframes rather than drawn frame by frame.

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* animation
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `humanoid_attack` | sprite | 24x32 | 4 | dusk16 |
| `humanoid_dead` | sprite | 24x32 | 1 | dusk16 |
| `humanoid_fall` | sprite | 24x32 | 1 | dusk16 |
| `humanoid_hurt` | sprite | 24x32 | 1 | dusk16 |
| `humanoid_idle` | sprite | 24x32 | 4 | dusk16 |
| `humanoid_jump` | sprite | 24x32 | 1 | dusk16 |
| `humanoid_run` | sprite | 24x32 | 8 | dusk16 |
| `humanoid_walk` | sprite | 24x32 | 8 | dusk16 |
| `samurai_attack` | sprite | 28x40 | 4 | hero16 |
| `samurai_dead` | sprite | 28x40 | 1 | hero16 |
| `samurai_fall` | sprite | 28x40 | 1 | hero16 |
| `samurai_hurt` | sprite | 28x40 | 1 | hero16 |
| `samurai_idle` | sprite | 28x40 | 4 | hero16 |
| `samurai_jump` | sprite | 28x40 | 1 | hero16 |
| `samurai_run` | sprite | 28x40 | 8 | hero16 |
| `samurai_walk` | sprite | 28x40 | 8 | hero16 |
| `humanoid_attack` | animation | — | 4 | 100 ms |
| `humanoid_dead` | animation | — | 1 | 300 ms |
| `humanoid_fall` | animation | — | 1 | 150 ms |
| `humanoid_hurt` | animation | — | 1 | 200 ms |
| `humanoid_idle` | animation | — | 4 | 200 ms |
| `humanoid_jump` | animation | — | 1 | 150 ms |
| `humanoid_run` | animation | — | 8 | 80 ms |
| `humanoid_walk` | animation | — | 8 | 120 ms |
| `samurai_attack` | animation | — | 4 | 100 ms |
| `samurai_dead` | animation | — | 1 | 300 ms |
| `samurai_fall` | animation | — | 1 | 150 ms |
| `samurai_hurt` | animation | — | 1 | 200 ms |
| `samurai_idle` | animation | — | 4 | 200 ms |
| `samurai_jump` | animation | — | 1 | 150 ms |
| `samurai_run` | animation | — | 8 | 80 ms |
| `samurai_walk` | animation | — | 8 | 120 ms |

**Review:** **The format is the value; the samurai's pixels are not.** A rig gives

## `village`

A village's cast and ground: a farmer, a slime, four harvests and a three-tile ground (grass, a paved square, a pond).

* 1. sprite grid and sizes
* 2. outline rule
* 3. light direction
* 4. shading steps
* 5. palette
* 6. silhouette rules
* 7. animation
* 8. provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `farmer` | sprite | 16x24 | 2 | farmer |
| `slime` | sprite | 16x16 | 2 | slime |
| `coin` | sprite | 12x12 | 1 | props |
| `apple` | sprite | 12x12 | 1 | props |
| `carrot` | sprite | 12x12 | 1 | props |
| `cabbage` | sprite | 12x12 | 1 | props |
| `ground` | tileset | 48x16 | 1 | ground |
| `farmer_walk` | animation | — | 2 | 160 ms |
| `slime_hop` | animation | — | 2 | 240 ms |

**Review:** It works: the field reads as a village at 1x (grass, a paved square, a

## `lighthouse`

A lighthouse's cast and tiles: the keeper (three states), the tower lamp, and four tilesets (a dusk sky, the tower, the cliff, the sea).

* 1. sprite grid and sizes
* 2. outline rule
* 3. light direction
* 4. shading steps
* 5. palette
* 6. silhouette rules
* 7. animation
* 8. provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `sky` | tileset | 224x16 | 1 | sky16 |
| `tower` | tileset | 96x32 | 1 | tower16 |
| `rock` | tileset | 64x32 | 1 | rock16 |
| `sea` | tileset | 48x32 | 1 | sea16 |
| `keeper_idle` | sprite | 24x32 | 2 | keeper16 |
| `keeper_climb` | sprite | 24x32 | 4 | keeper16 |
| `keeper_carry` | sprite | 24x32 | 2 | keeper16 |
| `lamp_off` | sprite | 16x16 | 1 | lamp16 |
| `lamp_on` | sprite | 16x16 | 2 | lamp16 |
| `keeper_idle_anim` | animation | — | 2 | 520 ms |
| `keeper_climb_anim` | animation | — | 4 | 130 ms |
| `keeper_carry_anim` | animation | — | 2 | 320 ms |
| `lamp_on_anim` | animation | — | 2 | 140 ms |

**Review:** It works: at 1x the tower reads as a tower, the cliff as rock, the sea as

## `tiles`

Twelve seamless 16x16 surface tiles in one tileset: grass, dirt, stone, brick, wood, sand, water, roof, floor, road, snow, lava.

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* animation
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `tiles` | tileset | 64x48 | 1 | terrain16 |

**Review:** **It works, and it is the most useful thing in the library.** A game needs

## `effects`

Five particle strips for a game's effects: dust, smoke, a spark, a splash and a flame.

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* animation
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `dust` | effect | 16x16 | 3 | fx16 |
| `smoke` | effect | 16x16 | 3 | fx16 |
| `spark` | effect | 16x16 | 3 | fx16 |
| `splash` | effect | 16x16 | 3 | fx16 |
| `fire` | effect | 16x16 | 4 | fx16 |
| `dust_anim` | animation | — | 3 | 120 ms |
| `smoke_anim` | animation | — | 3 | 140 ms |
| `spark_anim` | animation | — | 3 | 70 ms |
| `splash_anim` | animation | — | 3 | 110 ms |
| `fire_anim` | animation | — | 4 | 90 ms |

**Review:** **They work.** Every effect is drawn as a *shape* rather than as a drawn

## `ui`

The interface kit: a panel in three sizes (a nine-piece tileset plus a map per size), a cursor and a counter pip.

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* animation
* how the panel is used
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `panel_tiles` | tileset | 48x48 | 1 | ui16 |
| `cursor` | sprite | 8x8 | 1 | ui16 |
| `pip` | sprite | 8x8 | 1 | ui16 |
| `panel_small` | map | — | 0 | — |
| `panel_mid` | map | — | 0 | — |
| `panel_big` | map | — | 0 | — |

**Review:** **The panel is the useful part, and it works.** Nine pieces (the corners,

## `backgrounds`

What is behind the game: three seamless parallax ridge strips, a tileable star field, a cloud and a moon.

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* animation
* the sky
* how the ridges are used
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `ridge_far` | layer | 64x16 | 1 | dusk16 |
| `ridge_mid` | layer | 64x16 | 1 | dusk16 |
| `ridge_near` | layer | 64x16 | 1 | dusk16 |
| `stars` | layer | 32x32 | 1 | dusk16 |
| `cloud` | layer | 32x16 | 1 | dusk16 |
| `moon` | layer | 16x16 | 1 | dusk16 |
| `sky_bands` | tileset | 272x16 | 1 | dusk16 |
| `sky_dusk` | map | — | 0 | — |

**Review:** **The ridges are the useful part, and they work.** Each is a seamless

## `enemies`

Five opponents: a slime, a bat, a rat, a spider and a walking skeleton (a rig).

* sprite grid and sizes
* outline rule
* light direction
* shading steps
* palette
* silhouette rules
* facing
* animation
* provenance

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `slime` | sprite | 16x16 | 2 | enemy16 |
| `bat` | sprite | 16x16 | 2 | enemy16 |
| `rat` | sprite | 16x12 | 2 | enemy16 |
| `spider` | sprite | 12x12 | 2 | enemy16 |
| `skeleton_idle` | sprite | 24x32 | 2 | enemy16 |
| `skeleton_walk` | sprite | 24x32 | 4 | enemy16 |
| `skeleton_hurt` | sprite | 24x32 | 1 | enemy16 |
| `skeleton_dead` | sprite | 24x32 | 1 | enemy16 |
| `slime_anim` | animation | — | 2 | 160 ms |
| `bat_anim` | animation | — | 2 | 110 ms |
| `rat_anim` | animation | — | 2 | 150 ms |
| `spider_anim` | animation | — | 2 | 130 ms |
| `skeleton_idle_anim` | animation | — | 2 | 220 ms |
| `skeleton_walk_anim` | animation | — | 4 | 140 ms |
| `skeleton_hurt_anim` | animation | — | 1 | 200 ms |
| `skeleton_dead_anim` | animation | — | 1 | 300 ms |

**Review:** **The skeleton is the one that matters, and it works.** A walking enemy is

