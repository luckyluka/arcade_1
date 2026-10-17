| # | Phase | Goal |
|---|---|---|
| **23** | **Architecture Review** | Review everything learned from Breakout, Tetris and Platformer. Identify actual machine requirements, bottlenecks, unnecessary features, and missing capabilities. |
| **24** | **ARCADE-1 v0.1 Candidate** | Write the first complete machine specification: CPU, memory, MMIO, timing, video, audio, input, ROM/program model and compatibility rules. |
| **25** | **Machine v0.2a: 480×270 Screen** | Change the game resolution to 480×270 (16:9, exact 4× scaling to 1080p and 8× to 4K). Existing games keep their guest code and draw in the top-left region; golden hashes are regenerated deliberately. |
| **26** | **Machine v0.2b: Memory** | ROM 256 KiB, RAM 64 KiB, and a growth-friendly memory map (ROM at 0x00000000, RAM at 0x20000000, MMIO at 0x40000000). One shared map header for all guests, larger `.arc1` limit. |
| **27** | **Machine v0.2c: Color Graphics** | Add paletted 4-bit graphics: a 256-entry palette, `SPRITE4` and `TILEMAP4` with per-cell palette bank and flips. The existing 1-bit commands and their goldens stay unchanged. |
| **28** | **AI SDK v0.1** | Build the real developer-facing SDK and toolchain: headless runner, scripted input files, frame image dump, per-frame hashes, named game variables, record/replay, `arcade1 build/run/test/capture`, a short cheat sheet and two example games. Make development pleasant for humans and AI without exposing VM/Libretro/FPGA internals. Additionally, AI_NATIVE_DEVELOPMENT.md should be used at this stage.|
| **29** | **SDK Validation** | A fresh AI agent builds a small game using only the SDK and its docs. Sort every failure into game code, SDK, tooling, reference VM or machine, and fix the gaps. Change the machine **only where the evidence justifies it**. |
| **30** | **Asset Pipeline** | Define source assets (PNG, palettes, maps, audio) → conversion → native ARCADE-1 resources → `.arc1`. Build the asset tools and packaging. |
| **31** | **First Real Game** | Build one complete game with real art (made with the designer) using only the SDK and asset pipeline. Use it as evidence for what is missing: animation, UI, sound, memory, performance. |
| **32** | **Audio v2** | Add the audio capabilities the first real game justifies: several voices, basic waveforms or samples, music support. Keep determinism. |
| **33** | **ARCADE-1 v0.1 Freeze** | Freeze the machine contract and SDK baseline after the first real game. Decide the items deferred earlier (the 1,000,000-step cap and the per-frame work budget). From here, compatibility becomes a serious requirement. |
| **34** | **FPGA Implementation** | Implement the frozen ARCADE-1 machine in RTL. C VM remains the reference implementation. Measure CPU speed and memory needs before choosing hardware. |
| **35** | **Conformance / Lockstep** | Same ROMs and test programs run against C VM and FPGA; golden hashes are the shared reference; establish machine compatibility tests. |
| **36** | **More Games & Ecosystem Review** | Build increasingly ambitious games (shmup, RPG, racing, tactics, pseudo-3D) to validate the final machine, then evaluate the whole platform: machine, SDK, compiler, assets, FPGA, VM, tooling and games. Decide what ARCADE-1 actually is as a platform. |



STEP1:
Task: change the screen to 480x270 and update the roadmap. Follow docs.md "Agent behavior". Do ONLY this step, then stop.

Decision (owner): game resolution is 480x270 (16:9; scales exactly 4x to 1080p, 8x to 4K). The three shipped games and the demos keep their current guest code: they keep drawing in the top-left 320x240 region, and the rest of the screen shows whatever their CLEAR color leaves. Re-laying them out is NOT part of this step.

Before changing anything record, for test-breakout, test-tetris, test-platformer, test-scroll: the final "state hash" line. Guests do not change in this step, so after your changes these four state hashes MUST be identical. Only the fb= values and the golden files change.

Do, one commit per step, full regression after each (`make clean && make check`; zero warnings):
1. src/arcade1/machine.h: ARCADE1_WIDTH 480, ARCADE1_HEIGHT 270. Everything else must derive from these macros. Find every other hard-coded 320, 240, 76,800 and 153,600 in src/, tests/ and tools/ (guest layout constants in programs/ are intentionally left alone) and replace them with the macros or computed values.
2. Libretro: update the geometry (base and max width/height, aspect ratio 16:9). Keep the pixel format.
3. Tests: fix every expectation that depended on the old size (clipping edges, windowing, full-screen TILEMAP cell counts, worst-case harness arithmetic). Do not weaken any assertion; compute expected values from the macros.
4. `make golden-update` and commit the regenerated tests/golden/*.txt. The commit message must say: "behaviour change: framebuffer is 480x270 (spec section 8)". Verify the four state hashes are unchanged and report them. Committed ROMs in tests/roms must also be unchanged (guests did not change); say so.
5. SPEC.md and docs.md: update the resolution everywhere. In SPEC.md mark the two open decisions (1,000,000-step-cap contradiction in sections 8/10, and the per-frame work budget in DEVICE_BUDGET.md) as "DEFERRED: owner postponed until the FPGA milestone; current reference behaviour stays". Do not pick an option. Add a note to DEVICE_BUDGET.md that its pixel numbers were measured at 320x240.
6. Replace the roadmap table in docs.md with the table below, verbatim, keeping the existing format. Rows 23 and 24 are unchanged. Note the voxel experiment as finished research.

Rules: no guest code changes, no machine behaviour changes other than the resolution. Pin the decision in docs.md. Update PROGRESS.md. If something is ambiguous, pick the simplest option and list it under "Assumptions".

Final report: the four state hashes before and after (must match), the tail of `make check`, files changed, assumptions.


STEP2:
Task: M26 - enlarge ROM and RAM and move to a growth-friendly memory map. Follow docs.md "Agent behavior". Do ONLY this step, then stop.

Decision (owner, provisional sizes): ROM 256 KiB, RAM 64 KiB. Put the regions far apart so sizes can grow later without moving anything again:
  ROM  base 0x00000000, 256 KiB   (reset vector stays 0x0000)
  RAM  base 0x20000000,  64 KiB   (stack grows down from 0x20010000)
  MMIO base 0x40000000,   4 KiB   (device offsets unchanged: timer 0x000, video 0x100, input 0x200, audio 0x300)
All three sizes and bases must live in ONE place (src/arcade1/memory.h) and ONE guest-side header (programs/arcade1_map.h, new) that every guest includes instead of hard-coded addresses; the linker script must take the same numbers (document how they are kept in sync, or generate it from the header).

Before changing anything, record the final state hash of the four golden games. This step changes RAM size and addresses, so those hashes WILL change: report old and new, and explain why (RAM is hashed, the stack moved).

Do, one commit per step, full regression after each (`make clean && make check`; zero warnings):
1. memory.h/memory.c: new bases and sizes. Keep every existing bus rule: no faults, out-of-range reads 0, writes only if fully inside RAM, ROM writes ignored, MMIO 32-bit only, stray counter semantics unchanged. Update the stray tests to the new map (add one case that proves the OLD addresses 0x1000 and 0x4000 are now stray).
2. .arc1: raise the payload limit to the ROM size. Keep image version 1 (a larger limit is backward compatible); keep the CRC rule. Update tests/test_rom.c.
3. Guests: add programs/arcade1_map.h, switch every program in programs/ to it, update programs/link.ld and crt0 (stack top 0x20010000). Do not change any guest behaviour.
4. Tests that use fixed addresses or .syms: update them; do not weaken assertions.
5. Regenerate tests/roms/*.arc1 (`make roms-update`) and tests/golden (`make golden-update`). Commit message: "behaviour change: memory map v0.2 (spec section 6)". Verify per-frame fb= values in the golden files are UNCHANGED (only the state hash may change): report this explicitly.
6. SPEC.md section 6 and docs.md: new map. Mark the old "placeholder 4 KiB" notes as superseded. Update the voxel and architecture docs only where they quote the old addresses.

Rules: no machine behaviour changes other than the map. Pin decisions in docs.md; update PROGRESS.md; list ambiguities under "Assumptions".

Final report: old/new state hashes and why they differ, confirmation that fb= values did not change, tail of `make check`, files changed, assumptions.


STEP3:
Task: M27 - add paletted 4-bit graphics. Follow docs.md "Agent behavior". Do ONLY this step, then stop.

Goal: sprites and tiles with up to 15 colors each, so real pixel art is possible. Existing 1-bit commands (FILL, SPRITE, TILEMAP, ...) stay exactly as they are; their golden hashes must not change.

Design (owner-approved defaults; document every choice in docs.md):
- Palette: 256 entries of RGB565 inside the video device. Registers: PAL_INDEX (write sets the entry, auto-increments after each PAL_DATA write) and PAL_DATA. Choose free offsets in the video block (0x100-0x1FF) and document them. Power-on palette: entry 0 = black, entries 1-15 = a fixed 16-colour default (write down the 16 values), entries 16-255 = black.
- 4-bit image format: two pixels per byte, leftmost pixel in the HIGH nibble, rows padded to whole bytes, width must be even. Pixel value 0 = transparent. Pixel value v (1-15) draws PALETTE[PAL_BASE + v], where PAL_BASE = (PAL_BANK register & 0xF) * 16.
- New register FLAGS: bit0 flip X, bit1 flip Y (for SPRITE4; for TILEMAP4 flips come from the cell).
- New command SPRITE4: draws the 4-bit image at ADDR, size W x H, at X,Y (world space like SPRITE: shifted by the camera), clipped to the screen.
- New command TILEMAP4: like TILEMAP, but tiles are 4-bit images of size W x H (tile size), tile N at TILE_ADDR + (N-1) * (W/2 * H) bytes. Cell format (32-bit): [15:0] tile index (0 = empty), [19:16] palette bank, [20] flip X, [21] flip Y, rest reserved 0. Same windowing and camera rules as TILEMAP.
- Update the host-side work counters (video stats) for the new commands, counting bytes fetched as one per two pixels examined.
- Image addresses may point into ROM or RAM (as today).
Stay inside the existing rules: commands execute synchronously at the store, no faults (invalid sizes or addresses are clipped or ignored; document exactly what), nothing guest-visible besides the registers above.

Before changing anything record the final state hashes and fb= values of the four golden games; after your changes they MUST be identical.

Do, one commit per step, full regression after each (`make clean && make check`; zero warnings):
1. Palette registers + a test: write entries, draw, read the framebuffer; check auto-increment and the default palette.
2. SPRITE4 with tests: transparency (nibble 0), palette bank, flip X/Y, clipping at all four screen edges and with the camera, even-width rule.
3. TILEMAP4 with tests: palette bank per cell, flips, windowing with the camera, empty cells, cells at screen edges.
4. programs/color_demo.c (guest, 480x270): a scrolling tilemap built from 16x16 4-bit tiles, a few 4-bit sprites (one flipped, one with another palette bank) and a HUD using the old 1-bit commands. Build, add test-color with: determinism (two identical runs), a pinned state hash and fb hash, and property checks that the expected colours really appear (not only a hash). The test also dumps a PPM frame to $TMPDIR so the owner can open it.
5. Add color_demo to tests/roms and tests/golden (make roms-update, golden-update). The four older goldens must stay unchanged.
6. SPEC.md section 8 (video) and docs.md: document the palette, the 4-bit format, SPRITE4, TILEMAP4, FLAGS, cell format, and every clipping/ignore rule.

Rules: do not change existing commands, behaviour or goldens; no audio, no asset converter, no memory-map change in this step. Pin decisions in docs.md. Update PROGRESS.md. List ambiguities under "Assumptions".

Final report: files changed per step, the unchanged hashes (before = after), tail of `make check`, the PPM path, assumptions.