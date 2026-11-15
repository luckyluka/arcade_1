Task A: add a real regression suite. Follow docs.md "Agent behavior". No roadmap change, no machine behaviour change.

Problem: SPEC.md section 10 says the pinned state hashes are the regression suite, but no golden values are stored anywhere. The game tests only compare two runs of the same build. The per-frame hash (tests/state_hash.h) covers CPU + RAM only, not the framebuffer.

Before changing anything, record the final "state hash" printed by test-breakout, test-tetris, test-platformer and test-scroll. After all your changes these four values MUST be identical.
Reference values from another host (Ubuntu 24.04, riscv64-unknown-elf-gcc 13.2.0):
  breakout   0x7816DD30A2B9D131
  tetris     0x782EA2CDAA0C51A7
  platformer 0x76D2D3BA35B3A5F4
  scroll     0xB71FDB994B5D6576
Compare your values with these in the report. If they differ, print `riscv64-unknown-elf-gcc --version` and say so: it probably means a different compiler produced different guest binaries.

Do, in order, one commit per step, full regression after each (`make clean && make && make test-*` for every existing test target; build stays warning-free):
1. tests/state_hash.h: add fb_hash(machine) = FNV-1a 64-bit over the framebuffer, each RGB565 pixel as 2 bytes little-endian, row-major. Do NOT change state_hash().
2. In test-breakout, test-tetris, test-platformer and test-scroll, extend the per-frame line to:
     frame %4u hash=0x%016llX fb=0x%016llX
   (keep the existing prefix; append fb=). Do not change any assertion.
3. Commit the built ROMs of those four games as tests/roms/<name>.arc1 (exactly the files the tests load today). Add `make roms-update` that rebuilds them and copies them there. Document that this is a manual, deliberate action.
4. Make it possible to run those four tests against tests/roms instead of the freshly built ROMs (for example an ARC1_ROM_DIR environment variable that defaults to the current build directory). If a test needs the ELF for symbol addresses, find the least invasive way to keep that test's assertions in the normal run, and make the golden run need only the per-frame hash lines. Do not weaken any assertion of the normal run.
5. Create tests/golden/<name>.txt = the full per-frame output lines for each game from a golden run. Add `make golden-update` (manual, regenerates them) and `make test-golden` which runs each game against tests/roms and diffs against tests/golden. On failure print the first differing frame. `make test-golden` must pass WITHOUT a RISC-V compiler on PATH: verify this by running it with PATH stripped of the cross compiler.
6. Add `make check` = every existing test target + test-golden.
7. docs.md and SPEC.md section 10: replace "pinned state hashes" wording with the real mechanism (tests/golden, tests/roms), and add the policy: a golden change means a behaviour change, so the commit message must say why and which spec section changes.

Rules:
- Do not change machine behaviour or any existing test expectation.
- Simplest solution that works. List every ambiguity under "Assumptions".
- Update PROGRESS.md with a short note.

Final report: files changed per step, the output tail of `make check`, the before/after state hashes for the four games (they must be equal) and the comparison with the reference values above.


Task B: make SPEC.md state exactly what the reference does at the end of a frame, and fix a contradiction. Follow docs.md "Agent behavior". NO behaviour change in src/. Spec text, docs and tests only.

Read first: src/arcade1/machine.c (run_firmware, arcade1_run_frame), video.c (PRESENT), timer.c, src/libretro/ (how the framebuffer reaches the host), SPEC.md sections 4, 5, 8 and 10.

Facts to verify against the code before writing them (a test proves each one):
1. A frame ends at the FIRST of: (a) the step that executes PRESENT: nothing after it runs this frame; (b) the first step that leaves PC unchanged (a self-loop such as `j .`), after which every later frame ends immediately too; (c) 1,000,000 steps.
2. The next frame resumes at the next instruction.
3. What is the timer tick on each kind of frame end? What is STATUS (the PRESENT count)? What does the host see as the output of the frame?
If the code does something different from this list, DO NOT change the code: report the difference.

Do:
1. SPEC.md section 8: add "Frame boundaries" with the exact rules above, including that the output of a frame is the framebuffer contents when arcade1_run_frame returns, and that for a PRESENT-ended frame this equals the contents at PRESENT because no instruction runs after it. State that instret and step counts are diagnostic and not part of conformance.
2. Fix the contradiction: section 8 says the 1,000,000 cap is "not part of the game contract", but section 10 rule 6 puts it in the contract. Write both options with consequences (A: truncation at exactly 1,000,000 steps is contract, so hardware must replicate it; B: reaching the cap is a program error, behaviour then is implementation-defined, and conformance tests never reach it). Mark it clearly "DECISION NEEDED (owner)". Add a recommendation for B. Do not pick silently.
3. SPEC.md section 5: state the conformance rule for display: "The presented frame is the framebuffer contents at the end of the frame. How an implementation buffers or scans out is not specified, as long as this is what the viewer sees." Remove or fix any text implying PRESENT does more than that.
4. Add tests/test_frame_rules.c (+ make target test-frame-rules, included in `make check` if it exists) with tiny hand-assembled guests, no compiler needed:
   - PRESENT ends the frame immediately; a marker instruction placed after it runs only in the next frame.
   - A parked guest ends every frame immediately.
   - A 2-instruction counting loop with no PRESENT: after one frame the counter equals exactly what 1,000,000 steps give; the next frame continues the count.
   - Timer and STATUS values for each of the three cases, as the code actually behaves.
5. docs.md: add a short "Frame boundaries" note that points at the spec section.

Rules:
- No change under src/. If a test reveals a bug, report it and leave it.
- Where behaviour is accidental or unclear, list it under "Decisions needed" with options.
- Update PROGRESS.md.

Final report: the exact text added to SPEC.md, the test output, every difference between the facts list and the code, and the Decisions needed.


Task C: measure how much work the video device does per frame, so a per-frame work budget can be sized from data. MEASUREMENT ONLY. Follow docs.md "Agent behavior". No behaviour change, no new machine feature.

Before changing anything, record the final state hash of test-breakout, test-tetris, test-platformer, test-scroll. After your changes they MUST be identical.

Do:
1. Add host-side counters to the video device (not MMIO, not guest-visible, not in tests/state_hash.h): per frame, reset at frame start:
   - commands executed, by type (CLEAR, SET_PIXEL, FILL, SPRITE, TILEMAP, PRESENT)
   - pixels_written: framebuffer writes after clipping (a transparent bit that writes nothing does not count)
   - bytes_fetched: tile-cell bytes + tile/sprite bitmap bytes actually read
   - cells_visited: TILEMAP cells after windowing
   Expose them through an accessor such as arcade1_video_stats(machine), plus a "frame totals" snapshot taken at the end of each frame.
2. In the four game tests, keep a running avg and max over the scripted run of: instructions, commands, pixels_written, bytes_fetched. Print one summary line per game. Do not change assertions.
3. Add a synthetic worst-case test (tests/test_video_worst.c, target test-video-worst) with hand-assembled guests measuring, per frame: (a) one full-screen CLEAR, (b) 100 full-screen CLEARs, (c) a full-screen TILEMAP where every cell is a distinct non-empty 16x16 tile with a tint, (d) 128 overlapping 32x32 sprites, (e) the maximum number of commands a guest can issue within the 1,000,000-step cap using the cheapest instruction sequence (report the arithmetic and the measured result).
4. Write DEVICE_BUDGET.md with: a table per game (avg/max of the four metrics), the synthetic worst-case table, and for each metric the cycles an FPGA blitter would need at 1 pixel per clock and at 1 byte fetched per clock (state the assumption), compared with one 60 fps frame at 50 MHz and at 100 MHz.
5. In DEVICE_BUDGET.md propose 2-3 candidate cost models, for example "1 unit per instruction + 1 unit per N pixels written + 1 unit per M bytes fetched", with a concrete budget for each, computed from the measured data (state the headroom over the worst shipped frame) and what each would do to the worst synthetic case. For each model, say what should happen on overrun (for example the frame is stretched over several display frames, deterministically). DO NOT implement any model. End with "DECISION NEEDED (owner)".

Rules:
- Counters must not change any behaviour or any existing state hash.
- Label every number "measured" or "estimated".
- Update PROGRESS.md.

Final report: the before/after state hashes (must be equal), the per-game summary lines, the synthetic worst-case table, and the candidate models.