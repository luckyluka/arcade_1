# Retraction: the TILEMAP4 "minimum geometry" rule was never a machine rule

| | |
|---|---|
| Status | **RETRACTED** — a claim previously documented as VERIFIED |
| Date | 2026-10-19 (card A0 of `ROADMAP_V3.md`) |
| Retracts | `docs/experiments/ai-creation-flow.md` F4 bullet + claim ledger; `docs/ROADMAP_CREATION_FLOW.md` Wave 4 validator; `AssetEcosystem.md` §6 |
| Record kept | `docs/m29/M29_LOG.md` is **not** edited — its sweep table stands as historical data |

## What was claimed

Round 1 of the M29 fresh-agent test measured, by sweeping COLS × ROWS, a
"minimum geometry" for TILEMAP4: maps below a size-dependent boundary
(reported as `minCOLS = 6,6,6,6,5,3,2,2,1` for `ROWS 1..9`) silently drew
nothing, while 10×3 drew its full field. `docs/experiments/ai-creation-flow.md`
promoted this to a machine rule (tagged VERIFIED) and Wave 4 of the
creation-flow roadmap planned an importer validator on top of it. **All of
that is retracted.**

## Symptom (real, honestly measured)

The round-1 sweep (`docs/m29/M29_LOG.md:162-182`) genuinely observed small
maps drawing zero pixels and larger ones drawing. The measurements were not
falsified — they were **taken from contaminated builds** (see below), and a
graded pattern (`minCOLS` falling as `ROWS` rises) looked systematic enough
to read as hardware behaviour.

## Root cause

The SDK draw helpers were not compiler barriers yet (defect #1 of round 1).
`gfx_tilemap4` fills the guest's `cells[]` array with ordinary stores, then
hands the *address* to the device through register writes. The compiler,
unable to see that an integer address makes the device read that memory,
**deleted the cell-filling stores as dead code** in the failing builds —
so the device walked an all-zero map and drew nothing. The failure depended
on *code layout*, not on map dimensions: each sweep point was its own build,
and the compiler's decisions happened to correlate with the probe's size.

With a memory barrier before the first draw command, **every geometry
draws**.

## Evidence chain

1. **Timeline**: the sweep lives in `docs/m29/M29_LOG.md`, created at
   `74359fd` (round 1, 2026-10-18) — *before* `2d29b79`, the barrier fix
   (same chain: `74359fd` → `2d29b79`). Every swept build predated the fix.
2. **Root-cause reproduction** (independent reviewer, `ROADMAP_V3.md` card
   A0): in a failing build `--vars cells` reads **0 at frame 0** — the fill
   loop is absent from the binary; adding `__asm__ volatile("" ::: "memory")`
   before the first command makes 4×3, 5×1 and 1×8 maps draw.
3. **The fix**: `2d29b79` — "sdk: make the SDK helpers compiler barriers".
4. **The pin**: `tests/test_barrier.c:40-43` requires `probe_tm4_4x3`,
   `probe_tm4_5x1`, `probe_tm4_1x8`, `probe_tm4_6x3` and `probe_tm4_10x3`
   to paint exactly `cols*rows*16*8` pixels each, and documents that without
   the barrier they paint nothing. Runs green inside `make check`.
5. **VM inspection** (independently re-checked for this note):
   `video_tilemap4` (`src/arcade1/video.c`) has **no size-dependent branch**
   — it exits only on a missing framebuffer or zero W/H/COLS/ROWS, then
   windows the loop to the visible screen. A 4×3 map on screen iterates all
   12 cells.

## What survives

- **Even `tile_w` is a real rule** — the 4-bit blit returns on odd widths
  (`src/arcade1/video.c:303-304`, tilemap4 shares it at `:460`).
  Code-based, layout-independent.
- The other F4 validator bullets (palette bank-relative indexing, `.arc1`
  payload cap) are untouched.
- The round-1 sweep table remains in `docs/m29/M29_LOG.md` as the record of
  what round 1 observed — with this note as its correction.

## What Wave 4 does instead

No size validator. Fixtures import a 4×3 and a 5×1 map and assert they
**draw** (expected cell array computed independently, pixel-count style of
`tests/test_barrier.c`), keeping only the even-`tile_w` and empty-map rules.

## Lesson

> **A diagnosis that depends on code layout rather than on input values is
> a compiler or ABI suspect.** When a "hardware rule" changes shape between
> builds of the same input, check the compiler's output before the device's.

## Reproduce

```sh
grep -rn 'video_tilemap4' -A4 src/arcade1/video.c   # only zero-size exits
sed -n '38,46p' tests/test_barrier.c                # small-geometry probes
make test-barrier                                   # green = barrier pinned
git log --oneline --follow --diff-filter=A -- docs/m29/M29_LOG.md   # 74359fd < 2d29b79
```
