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
  --live               play window: an SDL3 window shows every frame and
                       live keys drive the pad (arrows/WASD d-pad, Z -> A,
                       X -> B, Enter -> START); mutually exclusive with
                       --input. Plays until R/ESC unless --frames bounds it
  --capture FILE       write the final framebuffer to FILE — format by
                       extension (.png or .ppm); the file holds the frame
                       after `--frames N`, i.e. the screen of frame N-1
  --capture-dir DIR    write frame_NNNN.<ext> into DIR every --capture-every
                       frames (<ext> is set by --format)
  --capture-every K    frame stride for --capture-dir (default 1)
  --format F           capture-dir image format: png or ppm (default ppm)
  --scale N            upscale captures N x, nearest neighbour, 1..8
                       (--scale 4 on 480x270 gives exactly 1920x1080)
  --png-store          write PNGs uncompressed (deflate "stored" blocks —
                       the historical mode). The default is real deflate
                       compression (LZ77 + fixed Huffman, no dependencies);
                       a 1080p capture is ~100x smaller.
  --syms FILE          load an `nm` symbol dump (<game>.syms); for --vars it
                       is optional — <image>.syms is auto-loaded if omitted
  --vars LIST          print named game variables (comma list, or "all")
  --var-every K        also print the selected variables every K frames
  --record FILE         write the applied per-frame buttons to FILE, in the
                        input script format (run-length, "none" = idle)
  --pad-trace FILE      write the applied pad per frame to FILE, one line
                        per frame: "frame N pad 0xXX" (the pad the guest
                        saw — see "Edge-triggered buttons and input scripts")
  --quiet              suppress the per-frame hash lines (summary only)
  --inspect            print the final frame, pc, instruction count and all
                       32 registers after the run (the `inspect` subcommand
                       is the structured form of this)
  --strict             exit non-zero if any draw read a source address
                       outside ROM/RAM — those draws silently paint nothing.
                       Without it the same condition still prints one
                       warning line on stderr (stdout stays machine-readable).
                       It also fails when a colour write carried bits above
                       15 (a 24-bit literal in an RGB565 register — see
                       CHEATSHEET.md's palette section).
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
| 3    | `--live` requested but the play window is          |
|      | unavailable (built without SDL3, or no display)    |

`--live` needs SDL3 and a display; a build or a machine without them exits 3
with a clean message, and every other mode (headless, input scripts,
captures) is unaffected — SDL3 is an optional dependency of the CLI only.
Inside the window, R ends the session and, with `--record FILE`, writes the
keys you pressed as a normal input script — every live session replays
bit-identically; ESC quits (finishing an open recording the same way), and
closing the window quits too. `--scale N` sets the window size (N x
480x270).

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

### Edge-triggered buttons and input scripts

**A press is an edge, not a hold.** Most games edge-detect their buttons
(press = "down now, up last frame"), because a hold is a hold. A script
line `6 40 a` therefore gives the guest **one** press — at frame 6 — and
then "still down" for 35 frames. It does not give 35 presses.

The trap: if the single edge lands while the game is in its game-over
state, the edge is spent on the **restart**, and the presses you meant to
give it afterwards never happen. Nothing in the output says so — the
session simply diverges from your plan (in Flappy, the bird fell out of the
sky ~40 frames later).

**The fix: release the button for one frame.** A released button is what
makes the next press a new edge.

Both scripts below are in the repo, and the test
(`tests/test_pad_trace.sh`) runs exactly them, so this example cannot
drift away from the truth:

* `tests/inputs/edge_trap.txt` — the trap: `6 40 a` (A held).
* `tests/inputs/edge_fixed.txt` — the fix: release A at frame 7, press
  again at frame 8, and keep releasing between presses.

Against `tests/guests/probe_edge.c` (the smallest guest that shows the
trap — it counts the presses, the flaps and the restarts):

```
$ arcade1 run probe_edge.bin --input tests/inputs/edge_trap.txt  --frames 40 --vars state,presses,flaps,restarts
var state = 1        # dead: the run ended again
var presses = 1      # one edge — the hold was ONE press
var flaps = 0        # and it was spent on the restart
var restarts = 1

$ arcade1 run probe_edge.bin --input tests/inputs/edge_fixed.txt --frames 40 --vars state,presses,flaps,restarts
var state = 0        # still alive
var presses = 5      # five real edges
var flaps = 4        # four of them flapped
var restarts = 1
```

To see the pad the guest actually saw, run with a pad trace:

```
$ arcade1 run probe_edge.bin --input tests/inputs/edge_fixed.txt --frames 9 --pad-trace pad.txt
$ cat pad.txt
frame    0 pad 0x00
...
frame    6 pad 0x10     # the press that restarts
frame    7 pad 0x00     # the release — this is what makes the next press an edge
frame    8 pad 0x10     # a fresh edge: this one flaps
```

`--pad-trace FILE` writes one line per frame — `frame N pad 0xXX` — the
pad the guest saw, in the same bit layout as the `INPUT_PAD` register
(up 0x01, down 0x02, left 0x04, right 0x08, A 0x10, B 0x20, START 0x40,
SELECT 0x80). Use it whenever a session diverges from your plan: it shows
exactly which frames carried a press.

### Worked examples — the three sharp edges

A tap is a one-frame run; a gap releases; and an overlap **replaces**,
it never merges:

```
# 1) tap A on exactly frame 120
120 120 a

# 2) steer right for a second, release for one, then right + A together
0   59  right
120 179 right a

# 3) overlap: the LATER line wins with its WHOLE button set
60  120 right       # frames 60..120: right
90  100 a           # frames 90..100: ONLY a — right drops in the overlap
```

Script 3 spelled out: frames 60..89 hold `right`, frames 90..100 hold `a`
alone, frames 101..120 hold `right` again. Want right *through* the tap?
The merge is yours to write — the runner never adds buttons across
entries:

```
60  89  right
90  100 right a
101 120 right
```

Recordings look tidier than hand-written scripts (the recorder merges
consecutive identical states and writes `none` for idle stretches), but
what matters is the per-frame pad: two scripts that hold the same buttons
on the same frames replay identically, however they are written.

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
  State you want to observe must be a file-scope global: its address
  comes from the `.syms`, never from declaration order.
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

## Golden files

A golden is a text file of per-frame hash lines. `arcade1 golden <game>
--out FILE` writes one; `arcade1 test <game> --golden FILE` compares
against one. The grammar:

```
# any line that does not start with "frame " is ignored
frame    0 hash=0x423419F69344CE80 fb=0x2BB88FC67027469D
frame    1 hash=0x67EB6F57FDF3BC86 fb=0xD4719AEC6C503FDD
```

* **Only `frame ` lines matter.** Blank lines, `#` comments and any header
  a tool writes above the hashes are ignored — so a golden can carry its
  own conditions (frames, input script, ROM) as comments.
* **The first pass is the sequence `frame 0`, `frame 1`, ...** and it ends
  at the first break in that sequence. A golden may carry a *second* pass
  after it (a reset-and-replay recording); `arcade1 test` compares the
  first pass only, because one `test` run cannot reproduce a reset.
* **A golden shorter than the run is compared over its own length** — the
  frames it has must match, and that is a pass.
* **A run shorter than the golden's first pass is NOT a pass** — the
  comparison reports how many of the golden's lines matched and exits 1,
  because the missing frames are unexplained. Run more frames
  (`--frames N`) to cover the golden.
* On a difference the report names the first differing frame line and
  prints the run line next to the golden line.

Authoring a golden is one command:

```sh
arcade1 golden game.arc1 --input session.txt --frames 600 --out game.golden
arcade1 test  game.arc1 --input session.txt --frames 600 --golden game.golden
```

The golden pins the exact image: the state hash folds in the guest's
instruction counter, so a golden matches one ROM and one input script
exactly. `tests/golden/conditions.txt` records those conditions per game.

## The other commands

```
arcade1 capture <game.arc1|bin> [arcade1 run options]
    run + dump frames. Creates and uses captures/ by default
    (--capture-dir DIR), or writes one final frame with --capture FILE.
    --format png|ppm picks the capture-dir extension (default ppm, PNG
    is also chosen automatically for --capture *.png), and --scale N
    upscales every written frame. Both forms write the framebuffer as it
    stands after the last frame that ran: --frames N captures frame N-1.
    PNGs are written with real deflate compression (LZ77 + fixed Huffman,
    no dependencies); --png-store keeps the historical uncompressed mode
    (the tests use it as the oracle).

arcade1 test <game.arc1|bin> [--golden FILE] [arcade1 run options]
    smoke run: the exit status propagates. --golden FILE compares the
    `frame N hash=.. fb=..` lines against FILE (a golden's first pass,
    the layout the tests/golden files use) and fails on any difference;
    --quiet is ignored in that mode.

arcade1 golden <game.arc1|bin> --out FILE [arcade1 run options]
    writes a golden file: the per-frame `frame N hash=.. fb=..` lines of
    a run, and nothing else (no summary line, no var lines). --out FILE is
    required; every other option is passed to `arcade1 run` (--frames N,
    --input FILE, --vars LIST, --syms FILE, ...). --quiet cannot be used
    (the frame lines ARE the golden) and is exit 2. The output is exactly
    what `arcade1 test --golden FILE` compares against, so a golden can be
    authored without reading the tool's source. Exit: 0 ok, 1 the run or the
    write failed, 2 usage.

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

arcade1 inspect <game.arc1|bin> [--frames N] [--input FILE] [--syms FILE]
                [--vars LIST] [--json] [--regs] [--commands] [--at X,Y]
    runs N frames (default 600) and reports the LAST frame as greppable
    KEY value lines:

      FRAME <n>                     frames completed (== --frames)
      PC 0x........                 final program counter
      INSTRET_FRAME <n>             instructions executed by the last frame
      STRAY <n>                     run total: bus accesses to unmapped or
                                    illegal addresses (diagnostic counter)
      VIDEO_CMDS CLEAR=.. FILL=.. SPRITE=.. TILEMAP=.. SPRITE4=..
              TILEMAP4=.. PRESENT=.. the last frame's command counts
      VIDEO_PIXELS_WRITTEN <n>      the last frame's framebuffer writes
      VIDEO_OOB_DRAWS <n>           run total: draws whose source address
                                    fell outside ROM/RAM — the same
                                    condition `run --strict` fails on
      VIDEO_COLOR_TRUNC <n>         run total: colour writes that carried
                                    bits above 15 (a 24-bit literal where
                                    RGB565 was meant) and were truncated —
                                    the same condition `run --strict` fails
                                    on
      ROM_USED <n> / 262144         payload bytes used / budget
      STATE_HASH 0x................ the CPU+RAM+stray fingerprint `run`
                                    prints — cross-check against any
                                    golden's `frame N hash=...` line
      FB_HASH 0x..................  the framebuffer fingerprint `run`
                                    prints
      VAR <name> <value>            one line per --vars selection (RAM
                                    globals by name; <image>.syms is
                                    auto-loaded if --syms is omitted)

    --commands records the LAST frame's video commands (only when asked:
    the record list and the per-pixel buffer are allocated just for that
    frame, up to 4096 records) and prints each as a CMD line, in
    execution order:

      CMD <id> <TYPE> x=.. y=.. w=.. h=.. addr=0x........ flags=<n>
              camera=0x........ pc=0x........ px=<n> clipped=<0|1>
              oob=<n>

    with id 1-based; x/y/w/h the parameter registers at execution; addr
    the source pointer (SPRITE*: ADDR, TILEMAP*: MAP_ADDR, else 0);
    camera = CAM_X | CAM_Y<<16; pc the PC of the store that issued the
    command; px the framebuffer writes it made; clipped 1 when part of
    the requested rectangle fell off-screen; oob the source reads that
    landed outside ROM/RAM (M29). A command beyond the 4096 cap executes
    unrecorded and stamps pixels with id 0.

    --at X,Y prints `AT X,Y id=<n>` plus the record of the LAST command
    that wrote that pixel during the recorded frame (id 0 = never
    written, with a note). Off-screen pixels are trivially id 0. Both
    flags imply recording; runs without them allocate nothing and the
    draw paths are loop-versioned so the disabled loops compile back to
    exactly the pre-recording ones (measured at baseline).

    --json prints one object with the same keys (VIDEO_CMDS nests, VAR
    maps names to values, ROM_USED maps used/size); with --commands /
    --at the object gains COMMANDS (array of the same records) / AT
    ({x, y, id, cmd}). The 32-register dump appears only with --regs
    (`run --inspect` still prints it inline). Host-side only: existing
    counters, no MMIO, no guest-visible change, nothing in any hash —
    STATE_HASH is identical with and without recording. Exit: 0 ok,
    2 usage. Nothing else is printed, so the report pipes straight into
    grep/awk/jq.
```

## Remaining M28 deliverables

Done since the earlier list was written: the full `arcade1` command set
(`build`, `test`, `capture`, `inspect` — see
[The other commands](#the-other-commands)); the cheat sheet (CHEATSHEET.md,
with a snippet compile test) and the example games (example_pong /
example_snake / example_color — each with a test, a golden regression and
a ROM snapshot; see GAME_DEV_GUIDE.md "Test and golden-lock it").

What remains is the rest of the roadmap beyond the SDK itself.
