# ARCADE-1 Developer SDK (Milestone 28 — AI SDK v0.1)

The developer-facing front door to the reference VM, for humans and AI agents
alike. Everything here drives `src/arcade1/` (the machine) directly — no
Libretro, no RetroArch, no GPU, no host input devices — so it runs identically
in a terminal, in CI, and inside an AI agent's sandbox.

This document covers the **foundation** landed in M28 step 1: the headless
runner `arcade1 run`. The remaining M28 deliverables (record/replay capture,
the full `arcade1 build/test/capture` command set, named game variables, a
short cheat sheet, and two example games) are listed under
[Remaining](#remaining-m28-deliverables) and are not built yet.

---

## `arcade1 run`

```
arcade1 run <game.arc1|bin> [options]
  --frames N           run N frames (default 600)
  --input FILE         scripted input (see Input script format)
  --capture FILE       write the final framebuffer to FILE as a PPM (P6)
  --capture-dir DIR    write frame_NNNN.ppm into DIR every --capture-every frames
  --capture-every K    frame stride for --capture-dir (default 1)
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

## Remaining M28 deliverables

Not built yet (future steps, then the rest of the roadmap):

- **Record/replay capture** — `--record FILE` to log a live session's input to
  the script format above (the read side, `--input`, already exists).
- **Full `arcade1` command set** — `build` (RISC-V guest → `.arc1`), `test`
  (run + assert hashes), `capture` (dump frames), built on `run`.
- **Named game variables** — inspect guest globals by symbol name (via the
  `.syms` `nm` dumps) instead of raw addresses.
- **Cheat sheet** — a short reference for humans and agents.
- **Two example games** built with the SDK.
