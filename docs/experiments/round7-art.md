# Round 7 — the second clean-room round

ROADMAP_V4 card 7.8, run twice. The first round (a fantasy village) measured
the pipeline as it was; this one measures it **after** the gap-closing, with
a brief that is *less* helpful than round 6's: a lighthouse keeper on a cliff,
and nothing else.

**The result: the round passed, and the pipeline got measurably better —
but the round found nine more gaps, six of them in the documents written
for this very wave.**

| metric | the number |
|---|---|
| the game | builds, plays, golden-locked (420 frames), the golden verified with `arcade1 test --golden` |
| the assets | 9 / 9 pass `arc1-art check` |
| nothing hand-edited | the harness re-runs every generator and the compiler in a copy and compares the trees: byte-identical |
| the DOC GAPs | **9** |
| `art/CRITIQUE.md` used | yes |
| the first compliant asset | **~11 minutes** after the step began (and the time went into *reading*, not failing) |
| the harness's verdict | `"ok": true` |

```sh
python3 tools/measure_round.py round7
```

## What the agent built

**THE LIGHTHOUSE KEEPER** — a keeper on a cliff at dusk, in a world 480×1024
(nearly four screens tall), composed of **five map layers** drawn in world
space under one camera: the dusk sky, the cliff, the tower, the sea's surface
band, and the deep water under it. The tower and the cliff carry collision
layers, read with `map_solid()`.

* **UP/DOWN** climbs the iron ladder (4 px/frame; the collision layer stops
  him at the cliff's top and under the gallery);
* **A** (held ~48 frames) at a lamp's height lights that lamp;
* the sea rises **2 px/frame** — twice as fast as he climbs, so every
  second spent lighting a lamp is a second the water gains;
* **win**: three lamps lit and the keeper at the beacon (`state 1`,
  `score 800`); **lose**: the water's line reaches his feet (`state 2`).

The scripted session proves both: attempt one does nothing and the sea takes
him at frame 96; START at frame 100; then he climbs, lights all three lamps
and reaches the beacon at frame 384 — `state=1, score=800, lamps_lit=3,
lives=2`.

## The nine gaps, classified

**Docs (six) — all of them in the text written for this wave**

1. **`arc1-art check --frame WxH` cannot tell you what a legal frame size
   is.** The tool says *"the image holds no frame"*; the frame width must
   divide the strip's width, and for a one-frame tileset `--frame` is the
   whole image. The docs only show the command for a character.
2. **The outline checks cannot be skipped**, and the docs only give half the
   rule: a *fully opaque* strip is excused (round 6's fix), but a tileset
   with one transparent cell is graded like a character, with no flag to
   turn the outline/contrast checks off.
3. **Nothing tells a generator that the PNG's pixels must be *exactly* the
   bank's colours.** The default `"exact"` quantisation errors on anything
   else; the docs never mention `quantize`.
4. **There is no way to look at the world.** `arc1-art preview` (round 6's
   fix) draws the manifest's *sprites*; the maps are invisible. The agent
   wrote a scratch renderer to compose the 480×1024 world — and it found
   three faults that no strip-level check could see.
5. **The presets table is stale**: *"six 16-entry banks"*, while
   `art/palettes/` holds eight.
6. **The map's legend rules live only in the compiler's error messages** (a
   key must be exactly one character — the sky needs thirteen).

**Tool (one)**

7. **The machine half of the review never looks at a map.** The agent added
   an eleventh sky band without its legend character; assetc compiled it,
   all nine strips passed, the contact sheet looked fine — and only the
   *captured game frame* showed the truth (the stars had become gold
   squares). Nothing checks that a legend matches its tileset.

**Docs (two more, from the game step)**

8. **The one end-to-end asset example is in the directory the brief
   forbids.** `GAME_DEV_GUIDE.md` points at `programs/experimental/asset_demo.c`
   as *"a working end-to-end example"*, while the brief says *"Do not read
   … `programs/experimental/*`"*; the three `example_*.c` games use no
   assets at all. The agent had to derive the whole
   `assets.h` → `anim_tick()` → `map_solid()` → `gfx_pal_upload()` pattern
   from the header comments.
9. **Nothing says how a game composes several map layers.** The manifest
   section shows exactly one map; stacking sky/cliff/tower/sea layers — and
   needing a second map because a tilemap draws a fixed rectangle — is
   undocumented.

**A finding worth its own line:** *the game is the renderer.* `arcade1 run
--capture` shows the composed world, and that is where the art's real
faults appeared (a scarf that read as a beard, a raised arm that read as a
floating stick, a sea crest that read as icebergs). Without a game, no tool
can show a world — which is gap 4 and gap 7 saying the same thing from
two sides.

## What the rounds say together

| | round 6 | round 7 |
|---|---:|---:|
| DOC GAPs | 7 | 9 |
| of them: docs | 5 | 8 |
| of them: tool | 1 | 1 |
| of them: pipeline | 1 | 0 |
| the first compliant asset | ~16 min | ~11 min |
| the assets passed on the first try | yes | yes |

The second round found *more* gaps than the first, and eight of the nine
are in documents written for this wave — the gap-closing of round 6 made the
art documents longer, and longer documents have more places to be wrong.
That is the honest lesson: **the pipeline is faster to use than it is to
document**, and every fix adds prose that can contradict the code.

The measurable wins are real: the first compliant asset came 5 minutes
faster, all nine strips passed on the first try, the agent spent its time
*reading* rather than failing, and the two tools it needed most
(`arc1-art preview`, and the game's own capture) existed because round 6
asked for them.

## What I would fix next

1. **The map half of the review** (gap 7): `arc1-art check` should read the
   manifest's maps — a legend character with no tile, a map layer that
   draws nothing, a tileset that does not match its map.
2. **A way to look at the world** (gap 4): `arc1-art preview` should compose
   the map layers, not just the sprites.
3. **The three docs one-liners** (1, 3, 5): the legal frame sizes, the
   exact-quantisation rule, the preset count.
4. **The end-to-end example** (gap 8): move the asset demo out of
   `programs/experimental/` (or point the brief elsewhere) — a brief that
   forbids a directory the documentation tells you to read is a trap.
5. **Composing several map layers** (gap 9): document it in the manifest
   section.

The round's metrics are recorded here and **not tuned afterwards** — the
numbers above are what the round measured, gap for gap.
