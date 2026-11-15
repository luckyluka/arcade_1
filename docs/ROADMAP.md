# ARCADE-1 — Roadmap (current)

The binding working rules are in [AGENTS.md](../AGENTS.md) (§The rules).
This file is the forward plan: what is next, in order. The completed waves
and the per-card commit table live in the archive —
[ROADMAP_V4.md](archive/ROADMAP_V4.md) §9.

**Status: Waves 7a, 7 and 8 are done** (cards 4.1–4.9, 7.1–7.8, 8.1–8.3;
the owner's decisions of 2026-10-21 are in ROADMAP_V4 §7, and card 8.3
ends at the owner's Audio-v2 decision). The X1 milestone (ArcForge
variable-size core + language extensions) is merged to main. **Nothing
below has started.**

## Wave 9 — freeze-readiness dossier (no machine changes) — NOT STARTED

From [ROADMAP_V4.md](archive/ROADMAP_V4.md) §6:

- `docs/experiments/freeze-dossier.md`: one table of every machine-level
  question seen so far (cap contract, work budget with the 480x270 numbers
  from card 4.8, memory sizes, audio v2, resolution), with: evidence,
  decision needed, options, recommendation, who decides. The agent
  recommends; the owner decides.
- Games in evidence by then: Breakout, Tetris, Platformer, Asteroid Run,
  Flappy, Petal Blade (+ the clean-room art game). If fewer than three
  genres show a need, say so.

## Wave 10 — ARCADE-1 on FPGA — NOT STARTED (prerequisite: Wave 9)

From [ROADMAP_V4.md](archive/ROADMAP_V4.md) §10 (added 2026-11-09). The
RTL implements the *frozen* machine, so Wave 9's decisions (cap contract,
work budget, Audio v2) are inputs to this wave, not things to rediscover
in HDL.

Rules for every card here:

- **The C VM stays the reference.** `src/arcade1/` does not change; the
  RTL must match it, byte for byte, through the goldens.
- **RTL lives in `fpga/` — never in `src/`, never linked into the CLI.**
  FPGA targets are run **by hand**, never added to `make check`.
- **The HDL simulator (iverilog or verilator) is a new host dependency**,
  to be installed by the owner before card 10.1 — this box has no HDL
  tools today.
- Sizes come from `docs/experiments/hd-fpga-estimates.md` (estimated,
  never built): at 480x270 the framebuffer is **2.074 Mbit** (fits a
  4 Mbit BRAM, single-buffered); the worst measured frame writes
  336,078 px → **20.2 Mpx/s → 20.2 MHz at 1 px/clk**, a conservative
  design point with the whole video path in BRAM.

| card | what | acceptance (by hand, never in `make check`) | size |
|---|---|---|---|
| 10.1 | simulation harness + RV32IM core in `fpga/sim/` | 3 golden sessions × 60 frames run in the simulator with their state `hash=` lines matching the golden files; new `make test-fpga` | ~2 days (after the simulator install) |
| 10.2 | the video engine, pixel-exact + scan-out + audio | **all 17 game goldens** match in full (`hash=` and `fb=` lines), and a probe session's WAV is byte-identical to `arcade1 run --audio` | ~3 days |
| 10.3 | the board top (owner's hardware) | ≥600 frames of ≥3 goldens matched over UART, a visible 480x270@60 picture, the 440 Hz beep audible, write-up in `docs/experiments/fpga-port.md` | ~3–5 days + bring-up |

Owner prerequisites (blocking): for 10.1, the HDL simulator; for 10.3,
the exact board + FPGA part number, video path (VGA / HDMI / PMOD),
on-board memory (BRAM vs SDRAM), and the toolchain (vendor vs
yosys+nextpnr).

## ArcForge — its own roadmap

ArcForge is a first-class subsystem with its own plan:
[arcforge/docs/ROADMAP.md](../arcforge/docs/ROADMAP.md). M0 (spec +
executable reference implementation), X0 (variable-size core) and X1
(language extensions) are done and merged; next per that file: M1 (make
one hero look good, Gate G1) → breadth/animation (M2, M3) → Gate G1x (the
v2.1 freeze) → M4 (the C compiler, gated on G3 — a game consumes assets).
Its rule: no wave starts before the previous gate is met.

## After this

New ideas that are not yet cards go to [FUTURE_IDEAS.md](FUTURE_IDEAS.md),
not to this file. Historical roadmaps (V3, V4, the creation flow) are in
[archive/](archive/); what has already shipped is in
[PROGRESS.md](PROGRESS.md).
