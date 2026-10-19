# ARCADE-1 Asset Ecosystem

Revision of `ARCADE-1_Creation_Studio_and_Asset_Ecosystem.pdf` (first draft).
Same vision — the machine, SDK and creative environment as one coherent
system — restructured around the foundations the first draft *assumed*:
determinism and golden-regression integration, the live-input gap, and
build-time resolution of named assets. Every machine claim here is
verified against this repo as it stands after M29 (SDK v0.1, nine golden
games, the `walkdemo` spritesheet experiment).

Companion documents: `CHEATSHEET.md` (guest API, `.arc1` layout),
`SDK.md` (CLI reference), `GAME_DEV_GUIDE.md` (how to make a game),
`DEVICE_BUDGET.md` (per-frame limits). This document **supersedes**
`ASSETS_PROPOSITION.md`, which posed the questions this doc answers.

## 0. What changed from the first draft

| Draft said | Revision |
|---|---|
| (silent on determinism) | §2.1: the asset compiler is *bit-deterministic and versioned*; compiled artifacts are checked in; the golden workflow is specified |
| (silent on verification) | §2.2: every resource ships with a canonical check, compared by `arcade1 test --golden` |
| "single PLAY action builds and runs" | §2.3: honest about the gap — the CLI is headless/scripted; a live-input window is a prerequisite, now Phase 0 |
| `sprite_play(player, "RUN")` | §4/§13: named assets resolve at *build time* to generated typed accessors; strings are source-level sugar, never `strcmp` on target |
| Studio-shaped phases | §20: pipeline first (`arc1-assetc`), studios later; importer before editor |
| sprite cost example (384 B for 8 frames) | §3: corrected — 32×24 4-bit is 384 B *per frame*; 8 frames are 3 072 B |
| (two docs on one topic) | Consolidated here; `ASSETS_PROPOSITION.md` marked superseded |

## 1. The ecosystem

ARCADE-1 should provide not only the machine, VM, SDK and game runtime,
but the complete environment required to make ARCADE-1 games: a small,
open arcade computer *and game factory*.

```
CREATION STUDIO                 MACHINE
 Art / Maps / Audio             VM (C, reference)
        |                       FPGA (eventual)
        v                        |
   ASSET COMPILER  --native-->  SDK  -->  GAME CODE  -->  .arc1
   (arc1-assetc)
```

The creative environment produces source assets. `arc1-assetc` converts
them into native ARCADE-1 resources. The SDK exposes those resources to
game code. The C VM is the reference development machine; FPGA is the
eventual physical machine. Everything required to create a game — code,
graphics, animation, maps, effects, audio, packaging, testing, profiling
— is part of one environment.

## 2. Foundations

These are not features. Everything else in this document fails without
them, and the first draft assumed all three.

### 2.1 Determinism

ARCADE-1's regression model is byte-exact: per-frame state and
framebuffer hashes in `tests/golden/*.txt`, replayed from committed ROM
snapshots. **An asset compiler that is not bit-deterministic destroys
this**: any recompile would reshuffle bytes, cascade through codegen into
every hash, and turn every art tweak into an unreviewable golden diff.

Rules:

1. **Same inputs + same `arc1-assetc` version → byte-identical output.**
   No timestamps, no unordered iteration, stable resource IDs.
2. **Compiled artifacts are checked in.** Source assets are the master
   art; compiled resources are the build's source of truth. (Precedent:
   `walkdemo`'s generated sheet header is committed; the generator is the
   master and must reproduce it exactly.)
3. **Format versioned from day one.** The `.arc1` container already
   carries magic `ARC1`, version, size and CRC-32 (CHEATSHEET.md); every
   compiled asset container does the same.
4. **Golden updates are deliberate and surgical.** Refreshing snapshots
   is manual and must be explained in the commit — and snapshot targets
   must be able to add *one* game without rewriting all of them (lesson
   from M29: `make roms-update` rewrites every snapshot; only the new
   game's files were kept).

### 2.2 Verification hooks

Each resource type ships with a canonical check — dimensions, palette
usage, byte cost, and where applicable a reference rendering — compared
through the existing `arcade1 test --golden` (per-frame hash lines vs a
golden file, **reproduced under the golden's run conditions** — input
script included; a golden records hashes, not the conditions, so the
harness must re-supply them exactly, as `make test-golden` does). This is what makes tool-assisted or AI-assisted asset
generation *safe*: structured input, deterministic output, machine-checkable
result. The verification infrastructure already exists; the ecosystem's
job is to make every asset flow through it.

### 2.3 The live-input gap (honest)

`arcade1 run` is headless and scripted (`--input`, `--record`); there is
no interactive mode. Every "edit → feel it" loop in this document —
walkcycle speed, jump arc, paddle response — is blocked on a
**keyboard→pad window**: a small host frontend that feeds key state into
the pad register each frame, while remaining deterministic when driven by
script. This is small, host-side, and unblocks the entire creation
vision; it is Phase 0 for that reason. `--record` from a live session
must produce a replayable script, closing the loop into §2.2.

## 3. Sprite Studio

A deliberately constrained pixel-art editor — or, first, an *importer*
for editors that already exist (§17). It must understand ARCADE-1's
graphics model: sprite dimensions, 4-bit indexed graphics, transparency
(nibble 0 draws nothing), palette banks, origins, collision boxes,
animation frames, flipping, attachment points, memory consumption.

The artist should not need to understand the binary representation — but
the constraints of the target machine stay visible. Corrected cost
example:

```
PLAYER_SHIP  32 × 24, 16 colours, palette bank 2, 8 frames

  pixels    32/2 × 24 × 8   = 3 072 B     (4-bit, 2 px/byte)
  metadata                    ~32 B       (origin, boxes)
  palette                     32 B        (bank 2 = entries 32..47)
  total                       3 136 B  ≈  1.2 % of the 256 KiB ROM
```

Validator rules the tools must enforce (all real machine constraints,
each found the hard way): 4-bit sprites need **even width**; `w/h <= 0`
or zero sizes draw nothing; 4-bit colour is `palette[bank*16 + nibble]` —
the entry index is bank-relative, not the palette itself.

## 4. Animation Studio

Animation is a first-class asset, not a sequence of manually selected
frames:

```
PLAYER
  IDLE   frame 0 (100 ms) … frame 3 (100 ms)
  RUN    frame 0 (80 ms)  … frame 5 (80 ms)
  EXPLODE frame 0 (60 ms) … frame 4 (60 ms)
```

The SDK exposes semantic operations — `sprite_play(player, "RUN")` —
**as source-level sugar only**: `arc1-assetc` generates typed accessors
(`asset_player_run_tick()` or `#define`s over frame tables) at build
time. Nothing is resolved at runtime; no strings live in the ROM unless
a game explicitly wants them. The low-level `gfx_*` API stays available
and unchanged beneath.

## 5. Sprite sheets

Sheets are supported, but as *interchange/build representation*: an
artist works in Aseprite or PNG; the importer produces the canonical
resource (sprites + named animations). The machine itself only ever sees
the compiled blob — a sheet is a contiguous ROM region and a frame is an
address (`sheet + frame * stride`, exactly as `walkdemo` does today).

## 6. Tilemap Studio

Understands the native cell format — verified from `gfx.h`: a map is
`cols × rows` 32-bit words, `[15:0]` tile index (0 = empty), `[19:16]`
palette bank, `[20]` flip X, `[21]` flip Y — plus tile palettes, scrolling
maps, collision metadata, spawn points, object markers and memory usage.

Machine-accurate cost example (47 tiles, 1 024 cells):

```
  tile memory   47 × 64 B      = 3 008 B
  map memory    1 024 × 4 B    = 4 096 B     (one word per cell)
  total                         7 104 B
```

The compiler should *derive* tiling automatically where it can: the M29
game draws its whole 10×3 brick field from **one** 48×12 tile, and the
device's tilemap walker makes that free at runtime. Image → tiles → map
deduplication is the single biggest ROM saver this machine offers, and it
belongs in `arc1-assetc`, not in the artist's head.

Validator rules: `tile_w` must be even (the 4-bit blit rejects odd widths,
`src/arcade1/video.c`), an empty map is an error, and **no minimum map size
exists** — the small-map draw trap once documented here was a pre-barrier
compiler artifact of deleted cell-filling stores, retracted in
`docs/experiments/tilemap-minsize-retraction.md`.

## 7. Palette Studio

4-bit graphics make palettes a design system, not a chore. The same
pixel data in different banks is a different-looking object for zero ROM:
one sprite, sixteen variants (WORLD / PLAYER / ENEMY / EXPLOSION / UI /
ICE / FIRE…). The tools should treat bank assignment as a first-class
axis — recolouring is an edit to 16 words, never to pixels.

## 8. Effects Studio

Effects should not require bitmap assets where computation is cheaper.
A small library of *deterministic* procedural effects — explosions,
sparks, screen shake, flashes — as an SDK/software facility first, not
dedicated hardware. Deterministic means replayable: effects consume
frame count, never host time, so goldens stay byte-exact.

## 9. Gameplay metadata

Assets carry structured metadata connecting art to gameplay: image,
animations, origin, collision box, hurt box, attack box, attachment
points. An image is a game resource with semantic geometry, not merely
pixels — and the tools can then answer "what does this cost, what does it
hit, what does it contain" without reading the game code.

## 10. Audio Studio

Same philosophy, eventually: sound-effect editing, trimming,
normalization, simple synthesis, looping, music sequencing, native
conversion, memory usage, playback timing. Audio is deliberately last in
this document because nothing about it blocks the visual pipeline.

## 11. `arc1-assetc`

The centre of the ecosystem: a dedicated compiler turning human-friendly
source assets into native resources.

```
PNG / Aseprite / WAV / JSON
        |
   validation   (dimensions, even widths, palette range, cell geometry,
                 ROM/RAM cost reported per asset and in total)
        |
  quantization  (to 4-bit with an explicit bank; out-of-range components
                 are ERRORS, not masked — a masking `&` once turned brown
                 hair dark green silently, in this repo, last week)
        |
     packing    (sheets, strides, tile blobs, cell words)
        |
  deduplication (identical tiles/sprites share bytes; images may become
                 tilemaps for free)
        |
 optional compression
        |
native ARCADE-1 resources  — byte-deterministic, versioned, checked in
```

The walkdemo experiment is the existence proof for Phase 1: a
225-line dependency-free generator rendered a 40×56 4-bit 7-frame sheet
(7 840 B = 3.0 % of ROM) with a ROM-cost report and caught two silent
colour corruptions by validating against independent expectations.
`arc1-assetc` is that, generalised.

## 12. Native asset representation

Source assets and runtime assets are separate. The canonical resource
model: `SPRITE`, `SPRITE4`, `TILESET`, `TILEMAP`, `PALETTE`, `ANIMATION`,
`FONT`, `EFFECT`, `SOUND`, `MUSIC` — each with a stable identifier and a
versioned container.

## 13. SDK abstraction

Game code may operate at a semantic level (named resources, generated
accessors) or at the register level (`gfx_*`). Both are first-class; the
machine stays small while the SDK becomes expressive. The rule from §4
stands: name resolution happens at build time.

## 14. ARCADE-1 Studio

Eventually the tools converge into one environment — code, art,
animation, maps, effects, audio, build, run, inspect, profile:

```
MY GAME/
  code/game.c   art/   animations/   maps/   effects/
  audio/   palettes/   tests/        game.arcade (project)
```

A single **PLAY** action builds assets, builds the game and runs it on
the reference machine; the same project produces `game.arc1` for
distribution. This depends on §2.3 existing first.

## 15. The development loop

```
CREATE -> EDIT -> BUILD -> RUN -> INSPECT -> TEST -> PROFILE -> (back to EDIT)
```

INSPECT, TEST and PROFILE already exist today: `--inspect` (registers,
instruction count), `--vars`/`--syms` (named globals), `--record`/
`--input` (scripted sessions), `arcade1 test --golden`, and the
out-of-window draw diagnostics (`--strict`). The ecosystem extends these
rather than inventing a parallel toolchain.

## 16. Machine-aware development

The tools understand the actual machine and report it *during*
creation, not at final build: ROM and RAM cost, active sprites, tile
cells, palette usage, frame rate, video work. Seeds that exist:
`DEVICE_BUDGET.md`, the worst-case video tests, the per-frame video
statistics (including skipped-draw counting). Making constraints visible
while creating is half of what "making the tenth game easier" means.

## 17. External tools are still welcome

ARCADE-1 does not replace every creative tool. Existing editors create
source material; ARCADE-1 owns the canonical runtime representation and
the compile step. The ecosystem grows by *importers* first and bespoke
editors only where no decent external tool exists. (A pixel-art editor
is a multi-year product; an Aseprite importer is a weekend. Ship the
weekend.)

## 18. AI-native asset tooling

AI is unusually effective when the assets are structured and constrained:
"generate a 16-frame 32×32 explosion in palette bank 3" is a request the
asset model can satisfy *and verify* — deterministic output, exact
memory/collision/animation properties, checked against goldens (§2.2).
AI operates through the asset model and the development tools, never
through special hardware in the machine. The constraint that makes ARCADE-1
hard to target is exactly what makes AI assistance trustworthy here:
there is one way to do things, and the machine can prove the result.

## 19. What should NOT happen

The ecosystem does not become a general-purpose game engine. No arbitrary
3D engines, no heavyweight physics or ECS frameworks, no GPU-style shader
systems, no enormous asset formats, no complex scripting languages, no
features without real-game justification. **ARCADE-1's advantage comes
from constraints; the tools' job is to make those constraints
productive.**

## 20. Development strategy

Phases are independently valuable; each ships something a game can use.

| Phase | Deliverable | Why here |
|---|---|---|
| **0** | Live-input window (keyboard→pad, `--record` out) | Unblocks every "feel" iteration; assumed by everything with a PLAY button |
| **1** | `arc1-assetc`: PNG → SPRITE4/PALETTE/TILESET, deterministic, checked-in artifacts, ROM-cost report | The pain felt *today*; seed exists (`tools/gen_walk_sheet.py`) |
| **2** | Native asset model + generated named accessors; metadata (origins, boxes) | Makes assets addressable by name, free at runtime |
| **3** | Animation resources + timing | `sprite_play` sugar over generated tables |
| **4** | Tilemap import (auto-tiling, dedup), then an editor | Biggest ROM saver; editor last |
| **5** | Deterministic effects library | Software-first, no hardware change |
| **6** | Studio integration (one project, PLAY) | Sits on 0–5 |
| **7** | FPGA integration: same project and asset model | The C VM stays the reference |

Promotion rule for anything experimental, matching repo convention:
works-in-progress live in `programs/experimental/` with hand-run tests;
they join `GUEST_PROGS` and the golden set only when they are meant to
last, and golden/ROM snapshot updates are deliberate, explained, and
surgical (§2.1 rule 4).

## 21. The larger idea

This changes what ARCADE-1 is. No longer VM + SDK + FPGA, but a coherent
creation system: the machine provides the constraints, the SDK provides
the programming model, the creative tools provide the content, the
compiler connects them, the VM provides instant testing, the FPGA
provides the physical target.

## Guiding principle

> The ultimate test of the ecosystem is not how sophisticated the editor
> is. It is: **is making the tenth ARCADE-1 game dramatically easier than
> making the first?**

Make it measurable: count the time from an empty directory to a playable
`.arc1` — with art, a golden test and a replayable input script — and
watch that number fall phase by phase. If every new game still requires
learning a collection of unrelated tools, exporters, formats and runtime
quirks, the ecosystem has failed its central purpose.
