# ARCADE-1 Developer SDK (Milestone 28 — AI SDK v0.1)

The developer-facing front door to the reference VM, for humans and AI agents
alike. Everything here drives `src/arcade1/` (the machine) directly — no
Libretro, no RetroArch, no GPU, no host input devices — so it runs identically
in a terminal, in CI, and inside an AI agent's sandbox.

This document covers the **foundation** landed in M28 step 1: the headless
runner `arcade1 run`, plus **named game variables** (step 2) and
**record/replay** (step 3). The remaining M28 deliverables (the full
`arcade1 build/test/capture` command set, a cheat sheet, and two example
games) are listed under [Remaining](#remaining-m28-deliverables) and are not
built yet.

---

## `arcade1 run`

```
arcade1 run <game.arc1|bin> [options]
  --frames N           run N frames (default 600)
  --input FILE         scripted input (see Input script format)
  --capture FILE       write the final framebuffer to FILE as a PPM (P6)
  --capture-dir DIR    write frame_NNNN.ppm into DIR every --capture-every frames
  --capture-every K    frame stride for --capture-dir (default 1)
  --syms FILE          load an `nm` symbol dump (<game>.syms)
  --vars LIST          print named game variables (comma list, or "all")
  --var-every K        also print the selected variables every K frames
  --record FILE         write the applied per-frame buttons to FILE, in the
                        input script format (run-length, "none" = idle)
  --quiet              suppress the per-frame hash lines (summary only)
```

The image may be a packaged `.arc1` game image (validated: magic, version,
size, CRC-32) or a raw flat `.bin` (like the tests). Exit codes:

| Code | Meaning                                             |
|------|-----------------------------------------------------|
| 0    | success                                             |
| 1    | runtime error (missing file, bad/oversized image)   |
| 2    | usage error (unknown command/option, missing ROM)   |

Build it with `make build/arcade1` (or as a prerequisite of `make test-run`).

### Output: one hash line per frame

Unless `--quiet`, each frame prints a machine-readable line in the exact format
the golden regression uses:

```
frame    0 hash=0x2B16C673E7C374B9 fb=0x15C55E8029F256AA
frame    1 hash=0x637D77CA20AD60B3 fb=0x9505ECA454A5B696
```

`hash` is an FNV-1a-64 fingerprint of the machine state (CPU registers, PC,
instruction counter, RAM, bus stray-counter); `fb` fingerprints the framebuffer
(RGB565, little-endian, row-major). They are the **same** fingerprints
`tests/state_hash.h` computes, so `arcade1 run` output is directly diffable
against `tests/golden/<game>.txt` — `make test-run` asserts exactly that for
`color_demo`.

A final summary line is always printed:

```
ran 600 frames  final state hash=0x...  fb hash=0x...
```

### Determinism contract

The same image + the same input script always yield the same per-frame hashes
and the same pixels, byte for byte, every run and every host (SPEC.md §10
rule 2). This is what makes scripted input usable as a **replay**: an input
script is the complete, reproducible record of a session's controls.

---

## Input script format

Plain text, deterministic, easy for a human or an agent to write. One
run-length entry per line:

```
<first_frame> <last_frame> <buttons...>
```

* Frames are **inclusive**. Entries may overlap; **later lines win** for a frame
  covered by more than one entry.
* Buttons are words: `up down left right a b start select`, plus `none`
  (explicitly hold nothing).
* A frame not covered by any entry is **no buttons held**.
* Blank lines and lines whose first non-space character is `#` are ignored; a
  trailing `#` comment on a line is also stripped.

Example — idle for a second, then hold right + A for half a second:

```
# serve, then steer
0   59  none
60  119 right a
```

The runner applies the entry set to each frame before `arcade1_run_frame`, so a
game that polls the pad sees exactly the scripted buttons. `make test-run`
verifies a script changes `breakout`'s state hash versus a no-input run, and
that the same script reproduces the same hash.

---

## Named game variables

Per-frame hashes answer "what does the machine believe?"; named variables
answer "what does the **game** believe?" — a game's own globals, read out of
guest RAM by name. The names come from an `nm` symbol dump (the same
`<game>.syms` the golden regression uses; `tests/roms/*.syms` are committed):

```
arcade1 run breakout.arc1 --frames 30 \
    --syms breakout.syms --vars score,lives
...
var score = 0
var lives = 3
```

* `--syms FILE` loads the dump (`<hexaddr> <type> <name>` per line, as `nm`
  prints it). Only RAM-resident data/bss symbols (`B/b/D/d` types inside the
  RAM window) count as variables — code and linker marks are rejected.
* `--vars LIST` selects what to print: a comma list of names, or `all` for
  every RAM variable in the dump (linker internals like `_sbss` skipped).
* Each variable is read as one 32-bit word — the guest's `int`, printed
  signed — exactly how the game tests read `score`/`lives`/`state`.
* `--var-every K` additionally prints a `vars <frame> <name>=<value> ...`
  trace line every K frames, for watching a variable move.
* Errors: `--vars` without `--syms` is a usage error (exit 2); an unknown
  name or a non-RAM symbol is a runtime error (exit 1).

`make test-run` checks the named reads (`breakout`'s `score = 0`,
`lives = 3` after 30 idle frames), the `--var-every` trace, and both error
exits. When the ROM directory has no `.syms` committed, the test generates
one from the freshly built ELF with `riscv64-unknown-elf-nm`.

---

## Record/replay

An input script is the complete, reproducible record of a session's controls
(see [Determinism contract](#determinism-contract)). `--record` is the write
side of that primitive: it logs the buttons actually applied each frame —
what `--input` resolved to, or nothing — to a file in the input script format:

```
arcade1 run breakout.arc1 --frames 30 --input session.txt --record out.txt
```

* Output is run-length encoded: consecutive frames in the same button state
  merge into one `<first> <last> <buttons…>` entry; an idle stretch records
  as `none`. Overlapping script entries (later lines win) are normalized into
  canonical runs, so a recording is the canonical form of a session.
* Recording works without `--input` too (an all-`none` log).
* A recording feeds straight back through `--input`: replaying it reproduces
  the original session **bit-identically** (same per-frame hashes). This is
  the round-trip `make test-run` asserts, along with recording determinism
  (two recordings are byte-identical) and the unwritable-file error (exit 1).

In a headless tool the recorded stream is the scripted one; when interactive
host sessions exist (RetroArch, tests), they can be recorded the same way and
replayed here — the replay format is the script format.

---

## Remaining M28 deliverables

Not built yet (future steps, then the rest of the roadmap):

- **Full `arcade1` command set** — `build` (RISC-V guest → `.arc1`), `test`
  (run + assert hashes), `capture` (dump frames), built on `run`.
- **Cheat sheet** — a short reference for humans and agents.
- **Two example games** built with the SDK.
