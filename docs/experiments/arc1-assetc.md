# arc1-assetc v0 — findings (Wave 1, ROADMAP_CREATION_FLOW cards 1.1–1.6)

Status: shipped as `tools/arc1_assetc.py` (python3, stdlib only), tested by
`make test-arc1assetc` (part of `make check`). Findings log per card 1.5 —
measured facts, not adjectives.

## What was built

| Piece | File |
|---|---|
| The compiler | `tools/arc1_assetc.py` |
| Test harness (fixtures built in-test) | `tests/test_arc1assetc.sh` |
| Manifest format | `assets.json`, self-describing `"format": 1` |

Pipeline: document validation → palette validation (declared + `"auto"`) →
image decode (PNG/P6 PPM, by magic) → geometry checks → quantization
(`exact`/`nearest`) → frame slicing → header emission + `--report`.

## Measured facts

| Fact | Value |
|---|---|
| Test checks (cards 1.1–1.5) | 79 |
| Representative header | 8×4 px, 2 frames + 1 palette → `player_sheet[16]` + `world_palette[16]`; ROM `48 / 262144`, header file 587 B |
| Compile time (that fixture) | 55 ms wall |
| Budget-exceeding fixture | 4096×130 solid strip = 266 240 B + 32 B palette → exit 1 before writing |
| `fail_asset` call sites (per-asset error paths) | 32 |
| Exit-2 (IO) sites | 2 (document read/parse; image read/decode/write) |
| `make check` wall time per Wave 1 commit | 54.75 / 55.81 / 56.83 / 57.94 s (cards 1.1→1.4) |
| Determinism | two independent processes over one fixture → `cmp`-identical header and report; delete + regenerate → identical again (both asserted in the test) |

## Validator classes caught (exit 1, one line per asset)

1. **Document structure** — `format` mismatch (teaching), container types.
2. **Palette rules** — bank 0..15, ≤16 colours, strict `#RRGGBB`,
   duplicate colour naming both indices.
3. **Sprite schema** — `auto` requires `bank`; `bank` never valid on a
   named palette; reference must resolve; `quantize` ∈ {exact, nearest};
   name must be a C identifier; `file`/`frames` present and sane.
4. **Image capability** — 16-bit/interlaced PNG, non-255 PPM maxval,
   unknown PNG colour type: error names the export to make.
5. **Image geometry** — dims ≥1; `image_width % frames`; **frame** width
   parity (an even image can hold odd frames — `video.c:303` checks the
   per-frame SPRITE4 width).
6. **Quantization** — exact mismatch with `not in palette … (nearest,
   d=…)`; auto palette overflow (true colour count); no drawable colours.
7. **Resource conflicts** — two palettes on one device bank; auto palette
   name clashing with a declared palette.
8. **ROM budget** — sheets + 32 B per palette > 262 144, checked on every
   compile, not only `--report`.

Plus **exit 2 = IO**: missing/corrupt `assets.json`, missing/corrupt
image, unwritable output.

## Bugs found while building (findings are deliverables)

1. **Near-colour collapse made pixels vanish silently (F3 class).** The
   first draft matched the transparent key on RGB565 equality. The test
   fixture's authored `#010203` collapses to `0x0000` under `>>3`, i.e.
   onto the key `#000000` — it would have been silently masked to
   transparent, exactly the walkdemo `&`-masking bug the card's rule 4
   forbids. Fix: the key is matched on the **authored 8-bit colour**; a
   colour that merely collapses onto the key now fails loud with the
   pinned `not in palette` error. Found by hand-computing the expected
   error line for the test — the oracle caught the design, not the code.

2. **Auto-overflow error undercounted.** First draft of `collect_auto`
   raised as soon as the 16th distinct colour appeared, so any
   over-coloured sheet would report "16 colours". ROADMAP_V3 §4.2's
   example reads `sprite player: 19 colours …`, i.e. the true total.
   Fix: collect all distinct first, then raise with `len(colors)`.

3. **Harness caught a bad fixture.** The mini-PNG-writer asserts
   `len(row) == w*3`; two hand-counted hex strings in the test were
   short (34 chars for a 6×1 strip) and the assert tripped immediately —
   the in-test writer's self-check works as intended.

## Premise gaps in the card text (reported, not improvised — V3 rule 10)

- Card 1.1 says "manual argv **like the seed**", but
  `tools/gen_walk_sheet.py` actually uses `argparse`. Followed the
  seed's real style; same flags-only contract, usage errors exit 2.
- Card 1.3's acceptance says "happy path → header emitted" while the
  header **contract** is card 1.4's text. Resolved by emitting the
  banner + sheet arrays at 1.3 (its acceptance green), completing the
  exact contract at 1.4 (defines, palette arrays, report, budget).
- Card 1.3's rules are written per-asset but `w`/`h` there mean
  **frame** dimensions (its cited `video.c:303` is the per-frame check);
  both image-level divisibility and frame-level parity are validated.

## Reproduce

```
python3 tools/arc1_assetc.py --assets assets.json --out-dir gen --report
make test-arc1assetc
```

---

# Wave 2 — metadata + typed accessors (ROADMAP_CREATION_FLOW §6)

Status: shipped in the same tool, header and SDK. New files:
`programs/asset.h` (`sprite4_ref` + `asset_sprite4`), the demo guest
`programs/experimental/asset_demo.c` with committed art
(`asset_demo_hero.png`, 109 B) and manifest (`asset_demo_sheet.json`),
its committed generated header (`asset_demo_sheet.h`, regenerate with
`make asset-demo-sheet`), and `tests/test_assetdemo.sh`
(`make test-assetdemo`, by hand like walkdemo — not in `make check`).
`tests/test_arc1assetc.sh` grew a Wave-2 section (79 → 104 checks).

## What the emitter gained

- Optional per sprite: `"origin": [x, y]`, `"collision": [x, y, w, h]`,
  validated for shape (two/four integers) and geometry (anchor inside
  the frame, edge allowed; box inside the frame; `[0,0,0,0]` ==
  omitting). Defaults `[0, 0]` / none.
- Per-frame refs: `static const sprite4_ref NAME_frames[NAME_FRAMES]`,
  one initializer per frame: `{ sheet + f * STRIDE, W, H, bank, ox, oy,
  cx, cy, cw, ch }`. Field order = struct order in `programs/asset.h`;
  `tests/test_arc1assetc.sh` COMPILES an emitted header against the real
  asset.h under host `cc -fsyntax-only`, so the two cannot drift.
- The banner now prints each sprite's metadata and a one-line include
  contract: the generated header names `sprite4_ref`, so `asset.h` must
  be included first. Nothing else in the header needs a type from it.

## Measured facts

| Fact | Value |
|---|---|
| Demo ROM payload | 1276 B (hero sheet 512 B + palette 32 B + 4 refs × 40 B = 160 B) |
| Ref cost vs sheet | 160/512 B = 31 % on the 16×16 demo; 280/7840 B = 3.6 % at walkdemo scale (7×40×56) |
| `sprite4_ref` | pointer + 9 ints = 40 B/frame on rv32 |
| Test checks | assetc 79 → 104; demo 15 (metadata, placement, flip, feet origin, collision box, no-stray-pixels) |
| Pixels pinned by the demo test | red 32 (two 16-px clusters, one mirrored), green 16, blue 192 = 12-bar + 180-box, white 16 |

## Findings

1. **The Wave-1 validator ate its own dog food on day one.** The first
   hand-run of the new geometry check failed exactly as designed — my
   demo-art generator emitted a VERTICAL strip (16×64); assetc answered
   `origin [8, 16] outside frame 4x64 — the anchor stays inside (0..4,
   0..64, the edge allowed)` and exit 1. The art was fixed to the
   horizontal-strip contract; the tool was right.
2. **`grep -q` vs teaching messages with `[x, y]`.** The test harness's
   `assert_has` used regex `grep`, so the new shape errors (`must be
   [x, y]`) were swallowed as character classes. `assert_has` now uses
   `grep -F` (fixed string) — correct for asserting exact teaching text;
   all older patterns were literal and unaffected.
3. **Design choices the card left open** (stated, not improvised):
   `int` fields in `sprite4_ref` (a 40000-px-wide frame fits the ROM
   budget with h=1, so shorts would need a validator to be correct —
   ints need none); refs emitted for EVERY sprite, even `frames: 1`
   (uniform contract); metadata duplicated per frame of a sprite (a
   strip has uniform frame geometry, and refs stay self-contained for
   `asset_sprite4`).
4. **Parked:** `programs/asset.h` is NOT in the CLI's embedded
   `SDK_FILES` (`arcade1 build`'s self-contained SDK), matching the
   card's text — add it when a self-contained build wants asset support.
   The GDG gained no new ```c fences, so `test-cheatsheet` is untouched.

---

# Wave 3 — animation resources (ROADMAP_CREATION_FLOW §7)

Status: shipped. `assets.json` gains a top-level `"animations"` array
(`{name, sprite, frames, ms}`) → per animation the emitter emits
`NAME_MS`, `NAME_STEPS` and a frame table
`static const sprite4_ref *const NAME_frames[NAME_STEPS]` of refs into
the sprite's frames, in play order. `programs/asset.h` gains
`anim_state` + `ANIM()` + `anim_reset(a, timer)` + `anim_tick(a, timer)`
(modes `ANIM_LOOP` / `ANIM_PINGPONG` / `ANIM_ONCE`), timed by the frame
timer via the new opt-in `arcade1_timer_count()` in `programs/arcade1.h`
(+1 per machine frame, never host time; unused static inline, so no
existing guest's bytes move — A6's precedent). `tests/test_arc1assetc.sh`
grew a Wave 3 section (104 → 134 checks).

## walkdemo migration (the card's proof)

`tools/gen_walk_sheet.py` no longer emits the header; it is now the
master of the ART only — it writes `walkdemo_sheet_art.png` (280×56,
every pixel authored in the palette's expanded 8-bit colour, transparent
= key `#000000`) and `walkdemo_sheet.json` (palette in the script's index
order, sprite `hero`, animations `idle` [0] 200 ms and `walk` [1..6]
67 ms — the old `WALK_TICK` = 4 frames at 60 fps). `arc1-assetc`
compiles the header; `make walkdemo-sheet` runs both halves.
`walkdemo.c` plays the emitted tables with `anim_tick()` and draws with
`asset_sprite4()` — the hand-rolled `WALK_TICK`/`tick`/`dir` counter is
gone; `anim` (the var the test greps) is computed as
`(int)(fr - hero_frames)`, the ref's own sheet index. `make test-walkdemo`
was NOT touched and stays green: idle 12 frames one fb hash, 2 px/frame
to x=100, cycle 1..6, pupil mirrors 23 → 16.

## Measured facts

| Fact | Value |
|---|---|
| Sheet bytes through the PNG→assetc round trip | **byte-identical** to the old direct pack (hero_sheet == walk_sheet, 7840 B; palette identical) |
| Walkdemo ROM const data | sheet 7840 + palette 32 (unchanged) + refs 7×40 = 280 B + anim tables 4+24 = 28 B |
| Walkdemo payload | 9168 B vs 8580 B before (+588: 280 B refs + 28 B tables + anim code, minus the removed tick counter) |
| `ms` exactness | one frame = 1000 units of 1/60 ms; 80 ms = 4800 units = 4.8 frames exactly, no drift |
| Test checks | assetc 104 → 134 (10 schema errors, table order, report/budget, host compile-proof, runnable anim_tick probe, determinism) |

## Findings

1. **The anim_tick unit probe caught its own oracle.** The first
   hand-written expected sequences were off by one call: I had written
   them as if the reset-anchored first tick consumed a frame, but the
   first call carries dt=0, so each step reads for exactly 3 frames and
   step 1 first shows at t=3. Nine `FAIL` lines proved the assertions
   bite; the fix was to the oracle, not to `anim_tick` (the probe's
   catch-up / wrap / clamp checks passed throughout). Lesson kept:
   timing oracles must state where the anchor sits.
2. **Mode is runtime, not schema.** The card's JSON has no mode field,
   so the emitter stays data-only: the table is the play ORDER, the
   game chooses `ANIM_LOOP`/`ANIM_PINGPONG`/`ANIM_ONCE` when it builds
   its `anim_state`. walkdemo plays the same sheet's frames as a
   ping-pong walk and a 1-step idle loop.
3. **Tables hold refs, not indices.** Emitting `unsigned char[]` of
   frame numbers would push `sprite_refs[table[step]]` back into game
   code; ref-pointer tables keep `anim_tick()` returning a ready
   `const sprite4_ref *` for `asset_sprite4()` — zero arithmetic, which
   is the whole point of Waves 2+3.
4. **The PNG round trip was verified byte-for-byte before migrating**
   (old `walk_sheet` vs new `hero_sheet` parsed and compared) — the
   walkdemo test's pupil-column assertions then re-verified it
   behaviourally. Two independent oracles, one migration.
5. **Design choices stated:** a stall reads as at most 600 frames
   (10 s) of catch-up so a hiccup never fast-forwards an animation;
   unsigned subtraction makes the timer's wrap-around exact;
   `anim_reset` takes the current timer so a state change can't
   inherit the old carry (walkdemo resets on the step-off, keeping the
   original's contact-pose start).

## Reproduce (Wave 3)

```
python3 tools/gen_walk_sheet.py
python3 tools/arc1_assetc.py --assets programs/experimental/walkdemo_sheet.json --out-dir programs/experimental
make test-walkdemo
make test-arc1assetc
```
