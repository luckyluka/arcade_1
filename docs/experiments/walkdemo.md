# Walkdemo — walking-spritesheet experiment (findings)

Status: experiment, done. Not part of `make check` (the guest lives in
`programs/experimental/`, built by `make experimental`; its test is run by
hand: `make test-walkdemo`).

The question this answers: *can ARCADE-1 carry a "premade spritesheet" —
a big, relatively detailed character with a left/right walk cycle — and is
a premade sheet actually necessary?* (Short answers: yes, and no — but for
detail, effectively yes, via a host-side converter. Details below.)

## What was built

| Piece | File |
|---|---|
| Sheet generator (the "art tool") | `tools/gen_walk_sheet.py` → `walkdemo_sheet_art.png` + `walkdemo_sheet.json` |
| Generated sheet (checked in) | `programs/experimental/walkdemo_sheet.h` (arc1-assetc output since Wave 3) |
| The guest | `programs/experimental/walkdemo.c` |
| The test | `tests/test_walkdemo.sh` (`make test-walkdemo`) |

A 40×56 4-bit character, 7 frames (1 idle + a 6-step walk cycle), 16-colour
palette in bank 3 (entries 48–63). Hold left/right to walk: 2 px per frame,
the cycle advances every 4 frames (~15 steps/s) and ping-pongs 1→6→1; the
blitter's flip-X mirrors the art for the other direction. Standing still
shows the idle pose.

Run it:

    make experimental
    build/arcade1 run programs/build/walkdemo.arc1 --frames 120 \
        --input <(printf '0 119 right\n') \
        --syms <(riscv64-unknown-elf-nm programs/build/walkdemo.elf) \
        --vars x,facing,anim,moving --capture frame.ppm

## Findings

1. **"1080p" is a clean 4× upscale, and the character is big where it
   counts.** The canvas is 480×270 by hardware (the framebuffer lives in
   video RAM; a 1080p frame is ~4 MB and could not exist in 64 KiB guest RAM
   even in principle). 480×4 = 1920, 270×4 = 1080 exactly, so the image
   lands pixel-perfect on an HD display. A 40×56 character is 21 % of the
   screen height — on the TV that's a 160×224 sprite. Detail is limited by
   the 480×270 grid, not by the display.

2. **A spritesheet needs nothing new from the machine.** The sheet is a
   contiguous ROM blob; each frame is just an address: `sheet + frame *
   WALK_STRIDE`. The blit itself (`arcade1_sprite4`) already takes an
   arbitrary source address, so the SDK gained zero commands. Transparency
   is the existing convention (nibble 0 skips).

3. **Flip-X is free, and one sheet serves both directions.** The blitter
   has flip bits (`flags` bit0 = flip X) — no mirrored duplicate frames, no
   guest-side mirroring, no extra ROM. Verified from captured frames: the
   eye pupil sits at sheet column 23 walking right and at 39−23 = 16
   walking left, exactly mirrored. This is what makes a walking sheet
   practical: 6 frames cover a full cycle for both directions.

4. **Premade art reaches the ROM only as compiled data — and the converter
   is ~230 lines of dependency-free Python.** There is no asset pipeline and
   none is needed: the generator emits a C header of arrays, the guest
   includes it, the build compiles it in. The committed header is the source
   of truth for builds (so `make experimental` needs no Python); the
   generator is the master artwork, rerun by hand when the art changes.
   *(Superseded by Wave 3: walkdemo now runs through arc1-assetc — the
   generator emits a PNG + assets.json, the header is the compiler's output
   with refs and animation tables, and the guest plays them with
   anim_tick(). The sheet's nibbles stayed byte-identical; see
   `docs/experiments/arcade1-assetc.md`, Wave 3.)*
   Because the M29 step-3 CLI is self-contained, the whole experiment also
   builds from a bare directory — `arcade1 build walkdemo.c walkdemo.arc1`
   with only the binary + two sources present (the sheet header resolves
   next to the source).

5. **Is a premade sheet *necessary*? No. Is it necessary for *detail*?
   Effectively yes.** Procedural art (what all nine golden games do) got a
   recognisable walker here because the figure is parametric: limbs are
   thick lines interpolated between hip/foot and shoulder/hand anchors, and
   the walk cycle is five numbers per step (leg offsets, foot lifts, body
   bob). That is genuinely enough for a simple character and cost an
   afternoon. But hand-computed pixels do not scale to *detailed* art — hair
   strands, faces, shading — and a real art tool (Aseprite et al.) cannot
   output this machine's format today. The missing piece is a PNG→header
   converter of exactly the same shape as this generator (swap the renderer
   for a pixel read; ~40 lines with PIL). That, not any machine change, is
   what "asset support" would mean for ARCADE-1.

6. **Costs: trivial.** Sheet 7 840 B = 3.0 % of the 256 KiB ROM; whole
   image 8 580 B (3.3 %). The guest retires ~54 instructions per frame —
   the 40×56 (2 240 pixel) blit is entirely device-side; the guest's per
   frame cost is the pad read, the position update and four register writes
   per draw. Animation is free per frame beyond `ADDR = sheet + anim *
   STRIDE`. Scaling up is linear: a 64×64 8-frame sheet is 16 KiB = 6.3 %
   of ROM, still noise. (The machine's per-frame pixel budget is documented
   in `DEVICE_BUDGET.md`/`test-video-worst`; one large character per frame
   is nowhere near the edge — the M29 game already blits a 48×36 tilemap
   plus sprites at 60 fps.)

7. **Bugs the experiment found — all in the tooling, none in the machine:**
   - *Palette truncated.* The generator emitted 16-bit RGB565 values into an
     `unsigned char` array, so every palette entry arrived as its low byte.
     The screen showed plausible-but-wrong colours, and the PNG-style
     preview agreed with the wrong output (same bug both sides). What caught
     it: checking rendered pixels against *independently computed*
     expectations (skin should be RGB565 28,44,18 → (230,178,148), screen
     said (90,186,131)).
   - *Silent colour truncation.* Hair was specified as r=40, out of the 5-bit
     range; `rgb565()` masked with `& 31` and quietly produced dark green in
     the ROM palette while the preview showed brown. The generator now
     raises on out-of-range components — a masking `&` had been hiding a
     real authoring error.
   - *Shoulder gap.* The body bob was applied to the torso only, leaving a
     1 px transparent row between neck and torso on bob frames. Fixed by
     bobbing head+neck+torso together.
   - *Idle-pose slide.* For the first 3 frames after pressing a direction
     the character walked in the idle pose (the cycle only stepped on the
     4th frame). Now the first held frame sets the contact pose immediately.

8. **Machine facts re-confirmed along the way** (all behaved exactly as
   documented — good sign for the SDK docs written in the M29 steps): nibble
   0 of a 4-bit sprite draws nothing; a frame ends at the PRESENT write;
   `--vars`/`--syms` read the guest's globals straight out of the `nm`
   dump, which is what makes the test's pace/arithmetic assertions possible;
   the pad is level-triggered, so the guest decodes "held = walking".

9. **Making it a golden game would be a one-liner set** — add `m29game`-style
   entries to `GUEST_PROGS` + `GOLDEN_GAMES`/`GOLDEN_IMGS`, then
   `make roms-update` + `make golden-update`. Caveat from the M29 work:
   `roms-update` rewrites *every* ROM snapshot, so add only the new game's
   files if the older snapshots must stay byte-identical. Left un-promoted
   on purpose: it is an experiment, and goldens are forever.

## Test assertions (`make test-walkdemo`)

- idle: 12 frames, one stable framebuffer hash, `x` unmoved, `anim` = 0
- walking right 40 frames: exactly `x` 20 → 100 (2 px/frame), `facing` = 0,
  cycle values only 1..6 with ≥3 distinct steps, ≥4 distinct pictures
- flip: pupil (RGB888 8,4,49) at char-relative x 23 walking right and 16
  walking left (39−23), 2 pupil pixels each; ≥40 skin pixels prove the
  character is drawn at all
- walk out and back (40 right + 40 left): lands exactly on `x` = 20,
  `facing` = 1

## Ideas if this grows

- A PNG→header converter (same shape as `gen_walk_sheet.py`, PIL-based) so
  real art tools can author sheets. *(Done as `arc1-assetc`, Waves 1-3:
  PNG/PPM in, refs + animations out, no PIL — walkdemo itself migrated.)*
- Two palette banks for the character (32 colours) — the blit takes a bank
  per call, so a detail pass (face, shading) can live in a second bank.
- A run cycle (longer strides, more bob) and a jump pose; the sheet stride
  arithmetic already supports any frame count.
- A taller 64-px character (16 KiB sheet) with a scrolling parallax ground —
  the camera register (`arcade1_cam`) exists and is unused here.
