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

**59 assets in 4 packs.**

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

## `village`

A village's cast and ground: a farmer, a slime, four harvests and a three-tile ground (grass, a paved square, a pond).

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `farmer` | sprite | 16x24 | 2 | farmer |
| `slime` | sprite | 16x16 | 2 | slime |
| `coin` | sprite | 12x12 | 1 | props |
| `apple` | sprite | 12x12 | 1 | props |
| `carrot` | sprite | 12x12 | 1 | props |
| `cabbage` | sprite | 12x12 | 1 | props |
| `ground` | sprite | 48x16 | 1 | ground |
| `farmer_walk` | animation | — | 2 | 160 ms |
| `slime_hop` | animation | — | 2 | 240 ms |

## `lighthouse`

A lighthouse's cast and tiles: the keeper (three states), the tower lamp, and four tilesets (a dusk sky, the tower, the cliff, the sea).

| asset | kind | size | frames | palette |
|---|---|---|---:|---|
| `sky` | sprite | 224x16 | 1 | sky16 |
| `tower` | sprite | 96x32 | 1 | tower16 |
| `rock` | sprite | 64x32 | 1 | rock16 |
| `sea` | sprite | 48x32 | 1 | sea16 |
| `keeper_idle` | sprite | 24x32 | 2 | keeper16 |
| `keeper_climb` | sprite | 24x32 | 4 | keeper16 |
| `keeper_carry` | sprite | 24x32 | 2 | keeper16 |
| `lamp_off` | sprite | 16x16 | 1 | lamp16 |
| `lamp_on` | sprite | 16x16 | 2 | lamp16 |
| `keeper_idle_anim` | animation | — | 2 | 520 ms |
| `keeper_climb_anim` | animation | — | 4 | 130 ms |
| `keeper_carry_anim` | animation | — | 2 | 320 ms |
| `lamp_on_anim` | animation | — | 2 | 140 ms |

