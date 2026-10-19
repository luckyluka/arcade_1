# ARCADE-1 Developer SDK (Milestone 28 — AI SDK v0.1)

The developer-facing front door to the reference VM, for humans and AI agents
alike. Everything here drives `src/arcade1/` (the machine) directly — no
Libretro, no RetroArch, no GPU, no host input devices — so it runs identically
in a terminal, in CI, and inside an AI agent's sandbox.

This document covers the **foundation** landed in M28 step 1: the headless
runner `arcade1 run`, plus **named game variables** (step 2),
**record/replay** (step 3) and the **example games** (steps 4–7: pong,
snake, color — each golden-locked and covered by `make check`). The
cheat sheet lives in CHEATSHEET.md. The full `arcade1` command set
(`run`, `capture`, `test`, `build`, `inspect`) is built — see
[The other commands](#the-other-commands) below.

---

## `arcade1 run`

```
arcade1 run <game.arc1|bin> [options]
  --frames N           run N frames (default 600)
  --input FILE         scripted input (see Input script format)
  --capture FILE       write the final framebuffer to FILE — format by
                       extension (.png or .ppm); the file holds the frame
                       after `--frames N`, i.e. the screen of frame N-1
  --capture-dir DIR    write frame_NNNN.<ext> into DIR every --capture-every
                       frames (<ext> is set by --format)
  --capture-every K    frame stride for --capture-dir (default 1)
  --format F           capture-dir image format: png or ppm (default ppm)
  --scale N            upscale captures N x, nearest neighbour, 1..8
                       (--scale 4 on 480x270 gives exactly 1920x1080)
  --syms FILE          load an `nm` symbol dump (<game>.syms); for --vars it
                       is optional — <image>.syms is auto-loaded if omitted
  --vars LIST          print named game variables (comma list, or "all")
  --var-every K        also print the selected variables every K frames
  --record FILE         write the applied per-frame buttons to FILE, in the
                        input script format (run-length, "none" = idle)
  --quiet              suppress the per-frame hash lines (summary only)
  --inspect            print the final frame, pc, instruction count and all
                       32 registers after the run (the `inspect` command
                       is exactly this flag)
  --strict             exit non-zero if any draw read a source address
                       outside ROM/RAM — those draws silently paint nothing.
                       Without it the same condition still prints one
                       warning line on stderr (stdout stays machine-readable)
```

The image may be a packaged `.arc1` game image (validated: magic, version,
size, CRC-32) or a raw flat `.bin` (like the tests). Exit codes:

| Code | Meaning                                             |
|------|-----------------------------------------------------|
| 0    | success                                             |
| 1    | runtime error (missing file, bad/oversized image),  |
|      | or `--strict` tripped on an out-of-window draw      |
| 2    | usage error (unknown command/option, missing ROM), |
|      | or `--vars` with a missing/mismatched `.syms`      |

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
  RAM window) count as variables — names outside the RAM window are dropped
  with one summary warning on stderr. A dump written by `arcade1 build`
  starts with `# arc1-syms crc32=0x…`, the image's payload CRC.
* `--vars LIST` selects what to print: a comma list of names, or `all` for
  **every name in the symbol file** — every name it holds is printed, with
  no name cap (linker internals like `_sbss` are skipped). Without `--syms`
  it auto-loads `<image>.syms` beside the image — what `arcade1 build`
  writes.
* Each variable is read as one 32-bit word — the guest's `int`, printed
  signed — exactly how the game tests read `score`/`lives`/`state`.
* `--var-every K` additionally prints a `vars <frame> <name>=<value> ...`
  trace line every K frames, for watching a variable move.
* Errors: no sidecar for `--vars` is exit 2 with a build hint, and a
  headered `.syms` whose CRC differs from the image is exit 2
  (`syms file belongs to a different build`); an unknown name or a
  non-RAM symbol is a runtime error (exit 1). Header-less dumps (the
  committed `tests/roms/*.syms`) skip the CRC check.

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

## The other commands

```
arcade1 capture <game.arc1|bin> [arcade1 run options]
    run + dump frames. Creates and uses captures/ by default
    (--capture-dir DIR), or writes one final frame with --capture FILE.
    --format png|ppm picks the capture-dir extension (default ppm, PNG
    is also chosen automatically for --capture *.png), and --scale N
    upscales every written frame.

arcade1 test <game.arc1|bin> [--golden FILE] [arcade1 run options]
    smoke run: the exit status propagates. --golden FILE compares the
    `frame N hash=.. fb=..` lines against FILE (a golden's first pass,
    the layout the tests/golden files use) and fails on any difference;
    --quiet is ignored in that mode.

arcade1 build <program.c> [output.arc1]
    compile a guest into an .arc1 image. The output must end in .arc1 —
    anything else is exit 2, `output must end in .arc1 (got 'out.ext')` —
    and when omitted it defaults to the source stem in the current
    directory (game.c -> game.arc1). The final line reports every file
    written: `arcade1: built game.arc1 (+ game.syms)`. Self-contained:
    crt0, the linker
    script and the SDK headers are carried inside the CLI and the image is
    packed in process, so it works in any directory with no checkout and
    no build/ tree — only riscv64-unknown-elf-gcc (and objcopy) on PATH.
    Beside the image it writes <output>.syms (the RAM-window half of the
    same toolchain's `nm` dump, first line `# arc1-syms crc32=0x…` = the
    image's payload CRC) for --vars to auto-load; without riscv64-unknown-elf-nm
    the build still succeeds and prints "arcade1: warning: nm not found,
    no .syms written". The .arc1 header layout is in CHEATSHEET.md.

arcade1 inspect <game.arc1|bin> [arcade1 run options]
    alias for `arcade1 run --inspect`.
```

## Remaining M28 deliverables

Done since the earlier list was written: the full `arcade1` command set
(`build`, `test`, `capture`, `inspect` — see
[The other commands](#the-other-commands)); the cheat sheet (CHEATSHEET.md,
with a snippet compile test) and the example games (example_pong /
example_snake / example_color — each with a test, a golden regression and
a ROM snapshot; see GAME_DEV_GUIDE.md "Test and golden-lock it").

What remains is the rest of the roadmap beyond the SDK itself.
