# Wave 6 decision basis — evidence from Game #1 (ROADMAP_V3 §6)

Status: EVIDENCE ONLY. Wave 6 is a decision wave ("decide from its report,
not from imagination"), and §6/§7 mark its items as owner decisions. This
document supplies the measurements and the §17 classification; it does not
decide anything. Decisions and their rationale live in the wave report.

Governing rule (AI_NATIVE_DEVELOPMENT.md §17): before a machine change,
classify the problem as game code / SDK / tooling / reference VM / machine
architecture — *only the last may change the machine*. And §15: do not build
prematurely.

## 1. Measured resource use (Game #1, ASTEROID RUN)

Sampled with `arcade1 inspect` across the committed 760-frame script
(act 1 gameplay, the win frame, and both passive acts):

| Frame | INSTRET_FRAME | VIDEO_PIXELS_WRITTEN |
|---|---|---|
| 60 | 3107 | 135 834 |
| 150 | 3133 | 137 611 |
| 250 | 3303 | 137 814 |
| 370 (win) | **3398** | **158 653** |
| 470 (passive) | 2220 | 135 779 |
| 640 (passive) | 2377 | 156 315 |

| Resource | Game #1 peak | Machine limit | Headroom |
|---|---|---|---|
| CPU per frame | 3398 instructions | `FRAME_STEP_CAP` 1 000 000 (`src/arcade1/machine.c:31`) | **0.34 % used — 294× headroom** |
| RAM | ~1 004 B (top of .bss `0x200003EC`) | 64 KiB (`ARCADE1_RAM_SIZE`) | **1.5 % used** |
| ROM | 11 112 B | 262 144 B | **4.3 % used** (art 6 520 B = 2.5 %) |
| Pixels written per frame | 158 653 | 129 600 screen px → 1.22× overdraw | modest; TILEMAP4 + 13 SPRITE4 + 46 SPRITE |
| STRAY draws | 0 | — | clean |
| VIDEO_OOB_DRAWS | 0 | — | clean |

## 2. §17 classification of the friction actually hit in Wave 5

Every real problem Game #1 produced, and where it belongs:

| Friction (measured in Wave 5) | Belongs to | Machine change? |
|---|---|---|
| Zeroed `anim_state` (missing `ANIM()`) hangs `anim_tick`'s catch-up loop forever inside frame 0 | **SDK** (`asset.h`: an `ms <= 0` guard would return the first frame instead of hanging) | No |
| Struct-copy assignment of an `anim_state` makes the freestanding build emit `memcpy` (no libc) | **game code / SDK** (document; or an `anim_assign` helper) | No |
| `cmd_build`'s hand-sized `tmpfiles[8]` broke under 7 SDK files | **tooling** (fixed in Wave 5: sized from `sizeof sdk_names`) | No |
| Input-script holds only cover their own frames; a later window falls back to the earlier entry | **tooling / docs** (the script format is under-documented) | No |
| `--vars` needs an `.syms` dump | **tooling** (already solved by Wave A1's auto-load + nm fallback) | No |

**Result: zero candidates reach "machine architecture".** The machine change
list for Wave 6 is empty on evidence. No ROM/RAM/bandwidth/primitive change
is justified by Game #1 — every measured resource is between 30× and 294×
under its limit.

## 3. Audio: the current state (v1), measured

| Fact | Value |
|---|---|
| Voices | **1** (square wave) |
| Registers | 2 — `AUDIO_FREQ` (Hz), `AUDIO_CTRL` bit0 (enable); `src/arcade1/audio.h` |
| Waveform | fixed ±8000 square; no PCM, no mixing, no channels (stated in the header) |
| Render | once per frame after the guest runs (`audio_render`, `machine.c`) |
| Power-on | 440 Hz enabled (the M05 beep behaviour, pinned by `test-beep`) |
| Guest SDK surface | **none** — no audio helper in `programs/*.h`; a guest pokes MMIO directly |
| Game #1 audio calls | **0** |
| CLI audio output | **none** — no wav/pcm dump, no `--audio` flag, nothing in `tools/arcade1.c` |

### The observability gap (the load-bearing fact for the Audio v2 decision)

Audio exists end-to-end *inside* the VM — `m->audio[]` is filled each frame
and `tests/test_beep.c`-style harnesses read it in-process. But **no shipped
tool can produce a sound or a sample file**: the CLI has no audio path. So
today an AI (or a human) cannot listen to or inspect audio through any
documented workflow; only an in-process C test can assert on samples.

Audio v2 ("several voices, a sound-effect asset type") would therefore be
built **deaf** — new hardware with no way to hear it, and no asset type to
author sounds into until something can play them. Under §15 that is the
definition of premature. The prerequisite chain is:

1. something that can emit audio the user can hear (the play window, or a
   CLI wav dump) — otherwise v2 has no listener;
2. a game that actually needs more than one voice — Game #1 needed zero.

## 4. The play window (§2 row "∥", §4.5, §7 item 4)

§4.5 as amended by the owner: **SDL3** optional, exit 3 when absent, not in
`make check`, scripting and `--live` share no mutable state; its only reason
to exist is recording a human session as an input script; not part of the AI
flow.

Cost basis: the VM already renders every frame to a framebuffer and already
renders audio to `m->audio[]`; the CLI already parses input scripts
(`load_input`) and already has `--input`. The window is a host-side consumer
of both — no machine, SDK or guest change. It is also the natural first
listener for audio, which is why the two decisions are coupled.

## 5. What is NOT being decided here

Per §7 these remain owner decisions and are not resolved by this document:
the assetc Python-vs-C home (item 2), the built-in font look (item 3), the
quantize question (item 1 — shipped as exact+nearest in Wave 1 and left
alone since). The per-frame work budget and the 1 000 000-step cap (§6) are
tied to the FPGA milestone; the evidence above says Game #1's content needs
0.34 % of the current cap, so nothing in this wave forces the question.

## 6. Owner decisions at the wave boundary (executed)

1. **Play window — build now, SDL3.** Per §4.5 as amended: `arcade1 run
   --live`, SDL3 optional, exit 3 when absent, not in `make check`, records
   a human session as an input script. Implemented in Wave 6; acceptance:
   `make test-playwindow` → 15 checks (see docs/experiments/playwindow.md).
2. **Audio v2 — defer.** The v1 survey above shows one tone voice, two
   registers, no PCM and no voices; Game #1 makes zero audio calls. Audio v2
   waits for a real listener (the play window is the first one) *and* a game
   that needs more than one voice.
3. **Machine change — none.** `FRAME_STEP_CAP` stays at 1 000 000. The cap
   bounds guest-side compute per frame (Game #1 peaks at 3 398 instructions,
   0.34 % of it); raising it would let the VM golden-lock a frame budget the
   FPGA may not sustain at 60 fps. Revisit with the FPGA milestone, not
   before.
