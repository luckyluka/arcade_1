# Audio v2 — decision brief for the owner (ROADMAP_V4 card 8.3)

Status: **brief only — no implementation, no registers, no SPEC change.**
Written 2026-11-07 after cards 8.1 (WAV dump) and 8.2 (live playback).
Every number below was measured on this tree; the card's rule applies:
**the agent recommends, the owner decides.** This brief feeds Wave 9's
freeze dossier (ROADMAP_V4 §6 lists "audio v2" as a machine-level
question).

## 1. Where audio stands today (v0.1, measured)

The device is one square-wave voice, register-driven (SPEC §6):

| fact | value |
|---|---|
| registers | `AUDIO_FREQ` (0x300) Hz, `AUDIO_CTRL` (0x304) bit0 = enable |
| waveform | square, fixed amplitude **±8000** — no volume control at all |
| rate | 48 000 Hz, 800 stereo samples per frame; rendered after the guest runs |
| power-on | 440 Hz enabled (the M05 demo beep) |
| implementation | `src/arcade1/audio.c` = **43 lines**; `tests/test_beep.c` (146 lines) pins waveform, 880 flips/s, determinism |

Machine budget: ROM **262 144 B**, RAM **65 536 B**, MMIO window
**4 096 B** (`arcade1 info`).

**Who actually uses it** (grep of every guest source, 2026-11-07):

- six shipped games write `AUDIO_*`: breakout, brickstorm, neontap,
  platformer, shmup, tetris;
- Petal Blade, Flappy and Skydescent do not — **Petal Blade, the
  flagship art game, is completely silent** (zero sound references);
- the two in-progress owner games (lumen, tap_racer) also write it.

**What the six do with one voice** — the constraint's creative answer and
its cost:

- *brickstorm*: per-event pitch sweeps (`f += sweep`, floor 40 Hz) and
  the charge whine (`freq = 160 + charge>>5`), gated by `AUDIO_CTRL`;
- *neontap M6*: a full effort-driven music engine **on the single
  voice** — a 44-line guest block (`programs/neontap.c` M6), 30 B of
  note tables (`MUS_PAT`/`MUS_ROOT`/`MUS_STEP`), 40 B of state
  (`mus_*` 24 B + `snd_*` 16 B, measured via `nm`). Priority is
  hand-juggled: **any SFX steals the voice and the music rests** — the
  audible cost of one channel.

Three concrete limits fall out of v0.1: **no volume** (a sound can only
be on or off — nothing can fade), **no noise** (nothing can whoosh), and
**one voice** (music and sound effects cannot coexist).

## 2. Petal Blade's sound list, and the minimum each needs

The card's five sounds, with what each actually requires:

| sound | what the player expects | on v0.1 today | minimum machine feature that would fix/improve it |
|---|---|---|---|
| **jump** | short rising blip, ~0.15 s | **works**: per-frame sweep up + `CTRL` off (neontap's `snd_frame` pattern) | an envelope (auto decay) or a volume register — today nothing can fade out |
| **slash** | whoosh: a noise burst, ~0.2 s | **the weak one**: square sweeps read as "pew", never "swish" | a **noise waveform** (LFSR) — the only sound on the list v0.1 genuinely cannot do |
| **coin** | two-note chime (B→E), ~0.15 s | **works**: two `CTRL`-gated tones, back to back | nothing; a volume register would add the fade |
| **hurt** | descending power-down, ~0.3 s | **works**: downward sweep (brickstorm's death pattern) | envelope/volume for the fade; nothing else |
| **win** | fanfare melody, 1–2 s | **works** via a guest sequencer (neontap M6 pattern, ~44 lines + 70 B); costs the voice — music stops while it plays | a **second voice** if music must continue underneath; otherwise nothing |

**Feature matrix across the five** (the card's four axes):

- **voices** — needed only so *win* (or any SFX) can coexist with music;
  today each game re-implements the priority juggle.
- **waveforms** — **noise** is needed by *slash* and nothing else;
  square suits jump/coin/hurt, sequenced square suits win.
- **envelopes** — improve jump/hurt/coin (fades); note that v0.1 has no
  amplitude control to envelope *at all*, so the true minimum is a
  **volume register**, with an envelope as its automatic version.
- **sample playback** — **none of the five needs it** (all are
  synthetic-friendly); it buys realism (real voices, instruments,
  ambience) at the ROM and tooling cost in §3.

## 3. ROM/RAM estimates (measured headroom, current costs)

Headroom (measured `riscv64-unknown-elf-size`, 2026-11-07):

- Petal Blade: ROM **59 984 / 262 144 B = 22.9 %** (202 KiB free);
  RAM **1 100 / 65 536 B = 1.7 %** (64.4 KiB free).
- The machine's own audio code is host-side (43 lines) and does not
  occupy guest ROM at all.

Costs by piece:

| piece | guest ROM | guest RAM | machine |
|---|---|---|---|
| SFX driver on v0.1 (sweeps + gating) | ~0.3–0.5 KiB per game | ~16 B | none |
| music sequencer (neontap M6 pattern) | ~0.6–1.2 KiB incl. tables (M6: 44 lines + 30 B tables) | 40 B (measured) | none — but it owns the only voice |
| **Option 2**: 4 voices + volume + noise (+ envelope) | **0** (drivers shrink instead) | ~16 B driver state | **~80–100 B** device state (4 × freq/phase/vol/ctrl + LFSR), **~+32 B** of the 4 096 B MMIO window |
| **Option 3**: sample playback, 5 sounds × 0.4 s | 48 kHz 8-bit = **96 KiB (36.6 % — too much)**; 8 kHz = 16 KiB (6.1 %); 6 kHz = 12 KiB (4.6 %) | 8 B cursor/voice | mixer + fetch path; plus an `assetc` conversion step (byte-deterministic, AGENTS rule 11) |

## 4. Options and their consequences

### Option 1 — stay on v0.1; guest-side sound (no machine change)

All five sounds ship: jump/coin/hurt are straightforward sweeps, win is
the M6 sequencer, slash is approximated as a fast sweep. Cost per game:
~0.5–1.5 KiB ROM, ~50–90 B RAM.

- **Consequences**: six games already carry their own driver; Petal
  Blade adds a seventh copy. No fades (fixed ±8000). Music and SFX keep
  fighting for the one voice (neontap's audible note-drops). SPEC §6
  untouched, `test_beep` untouched, **the Wave 9 freeze is unaffected**,
  zero golden risk.

### Option 2 — v0.2 additive PSG: 4 voices + per-voice volume + noise (+ envelope)

Registers for voices 1–3, a volume field, and a noise source are **added**
beside the existing pair; voice 0 keeps `0x300`/`0x304` **byte-identical**
so the M05 beep, `test_beep`, all six shipped games and every golden stay
bit-for-bit unchanged.

- **Consequences**: the smallest machine change that covers everything
  the five sounds need — slash gets its whoosh, music and SFX coexist,
  a volume register gives fades without per-frame guest work. Cost:
  `audio.c`/`audio.h` grow from 43/53 lines to roughly 150–250 + ~100
  lines of device tests, a SPEC §6 rewrite, docs — **all before the
  Wave 9 freeze**. RAM +~100 B device state; MMIO +~32 B; determinism
  unchanged (rendered from integer device state, exactly like v0.1).
  The consequence of *not* choosing it before the freeze: reopening a
  frozen machine later is precisely what the dossier exists to avoid.

### Option 3 — wavetable / sample playback (real timbres)

- **Consequences**: the biggest machine change (mixer, fetch path, bank
  registers), real ROM cost per sample (§3: 4.6–36.6 % of ROM for five
  sounds depending on rate), and a new tooling surface — `assetc` must
  gain a deterministic WAV→ROM conversion. It buys realism none of the
  five listed sounds requires, and it is the hardest option to retract
  after the freeze.

## 5. Recommendation (the agent recommends; the owner decides)

1. If Petal Blade should simply **make its five sounds** → **Option 1**
   now: nothing blocks it, the freeze is untouched, costs are measured
   above.
2. If the slash must **whoosh** and music must keep playing during
   sounds → **Option 2**: the smallest machine change that covers every
   need on the list, additive by construction (v0.1 stays
   byte-identical), and cheapest to do **before** Wave 9 freezes the
   machine.
3. **Option 3 is not recommended now**: no sound on the list needs it,
   its ROM and pipeline costs are real, and it is the hardest to undo.

**Decision points — this is where the card stops:**

1. Ship Petal Blade on v0.1 sounds (Option 1), or fund Option 2 before
   the Wave 9 freeze?
2. Is a real **noise** (the slash's whoosh) a must-have? That single
   feature is what changes the answer from Option 1 to Option 2.
3. If Option 2: is **4 voices + volume + noise** the right size, or is
   **2 voices** enough?
4. If neither now: record "audio v2 = deferred" in the freeze dossier.

**STOP — no code, no registers, no SPEC change. The owner decides.**
