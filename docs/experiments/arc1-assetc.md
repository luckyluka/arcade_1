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
