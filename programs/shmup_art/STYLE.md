# Art style sheet — TEMPEST (Game #4)

The vertical shoot-'em-up's art: the player's interceptor, nine kinds of
enemy, the boss, the shots, the effects and the terrain band.

Everything here is **procedural**: `programs/shmup_art/gen_art.py` paints
every pixel, and the strips it writes are the build truth. The generator
is the master; `programs/shmup_assets.h` is the compiled header.

## 1. Sprite grid and sizes

* **Frame size**: each sprite has its own size — the player is 24×24, the
  fighters 16×16, the bomber 48×32, the cruiser 64×40, the boss 120×88
* **Frames per state**: 1 for everything but the player (2: the engine
  flame brightens) and the boss (2: the damaged variant)
* **Strip layout**: one horizontal strip per sprite, frames left to right
* **Anchor**: the sprite's centre (`origin: [w/2, h]`) except the terrain
  band, which is anchored at its top left

## 2. Outline rule

* **Outline colour**: palette index 1 in every bank (the darkest colour
  in it)
* **Width**: 1 px, painted as the ring **outside** the silhouette
  (`outline()` — the shape grows by one pixel)
* **Where**: every opaque shape. The explosion and the enemy's bullet are
  the exceptions: they are *effects*, and the QA tool grades them as such

## 3. Light direction

* **Light comes from**: the top left
* **Highlight**: the lighter tones of each bank's ramp (the player's
  cyan, the boss's pale violet)
* **Shadow**: the darkest tone of the ramp, on the lower right

## 4. Shading steps

* **Steps per material**: 2 (a main tone and one shade)
* **Ramp order**: dark → light, indices 2..9
* **Dithering**: none — at these sizes a dither is noise

## 5. Palette

Eight banks, one per role:

| bank | palette | what it carries |
|---|---|---|
| 0 | `hero` | the player: cyan-steel with a gold engine |
| 1 | `foe` | the enemy air units: crimson and hot orange |
| 2 | `ground` | the ground units and the terrain: olive and steel |
| 3 | `shot` | the shots: the player's cyan bolts, the foes' orange beads |
| 4 | `fx` | the explosions and the pickups: white, yellow, green, blue |
| 5 | `rock` | the debris: grey stone with brown earth |
| 6 | `boss` | the boss: violet hull, crimson trim, a gold spine |
| 7 | `sky` | the star field |

* **Group**: one group per bank
* **Deviation from the preset**: none — the banks are the game's own
  ramps, written out in the generator

## 6. Silhouette rules

* **Readable at 1x**: the enemies are read by their *shape* — the
  fighter's swept wings, the copter's rotor bar, the bomber's wide wings,
  the cruiser's stepped tower, the boss's pointed prow and side blades
* **Minimum feature size**: 2 px (the boss's maedate, the turret's barrel)
* **Negative space**: the gap between the boss's pods, the turret's base
* **Never**: a limb thinner than 2 px, a sprite that is not one piece
  (the QA tool counts the pieces ✓ — every sprite in this pack is one,
  except the explosion and the bullet, which are effects)

## 7. Animation

* **Frame counts**: the player 2 (the engine flame), the boss 2 (the
  damaged variant), the boss's core 2 (it pulses in colour), everything
  else 1
* **Timing**: the flame cycles every 6 frames, the core every 8
* **Baseline**: the frames of a state stand on the same row — the flame
  brightens in place instead of growing, and the core pulses in colour
  instead of size

## Provenance

* **Tool**: procedural (seeded; the terrain's noise is a fixed seed)
* **Script**: `python3 programs/shmup_art/gen_art.py --dir
  programs/shmup_art --manifest shmup_assets.json`
* **Licence**: this repository
* **Recorded in**: `shmup_assets.json` (the `provenance` block per asset)
  and the generated `art/ART_LICENSES.md`
