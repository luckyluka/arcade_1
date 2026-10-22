# The crt0 runtime could be linked at an odd address — and a call to it jumped into the rodata

| | |
|---|---|
| Status | **FIXED** — a latent bug in the build, found by a playtest audit |
| Date | 2026-10-22 (the BRICKSTORM physics pass) |
| Found by | the contact recorder + the "glove detector" in the playtest audit |
| Fixed in | `programs/crt0.S` (`.balign 4`), `programs/link.ld` (`. = ALIGN(4)`) |
| Affects | every guest built with `programs/crt0.S` — four shipped games had a **misaligned** `memcpy` |

## Symptom

The BRICKSTORM playtest audit (16 scripted sessions) found a session that
froze: the ball stopped moving, the paddle stopped moving, the score
stopped, and the emulator reported

```
INSTRET_FRAME 1000000      <- the frame's instruction cap was hit
STRAY 941207               <- the guest was reading and writing outside the ROM
```

and, a few frames later, the game **restarted itself** — every global back
at its initial value (the score zero, the state back to the title). The
guest had run off the end of its own code.

## How it was found

The audit's *glove detector* flags any change of the ball's direction that
the physics cannot explain. It flagged a handful of them; the first two
turned out to be a measurement artifact (the observables reported the
position of the **last** ball alive and the velocity of the **first** —
different balls, after the MULTI power-up). Fixing that exposed the real
one: the game's state stopped changing entirely.

To find it, a **single-stepping tracer** was written on top of the VM
(`src/arcade1/exec.h` exposes the CPU and the bus): it runs the guest one
instruction at a time and reports the first PC that leaves the code.

```
frame 148: the PC left the code at 0x0000810C after 1516 steps
   ...
   0x0000053C      jalr  -> the call to memcpy
   0x00008050      <- the rodata, executed as code
   0x00008054
   ...
```

The call to `memcpy` had landed **one byte before** `memcpy`.

## Root cause

`programs/crt0.S` provides the three libc functions GCC emits on its own
(`memcpy`, `memset`, `memmove`) in the input section `.arcade1.text`.
`programs/link.ld` appends that section after the guest's own code and
rodata — but **neither the section nor the linker script asked for a
4-byte alignment**. GAS gives an `@progbits` section an alignment of 1
unless told otherwise, so `.arcade1.text` landed wherever the rodata
ended:

```
$ riscv64-unknown-elf-nm game.elf | grep memcpy
00008051 T memcpy        <- odd! a function must be 4-byte aligned
```

A RISC-V call reaches its target with a `jalr`, and the VM implements
`jalr` as the spec says — with **bit 0 of the target cleared**
(`next = (rs1 + imm) & ~1u`, `src/arcade1/exec.c:71`). A function at an
*odd* address therefore loses its last bit: the call to `memcpy` at
`0x8051` lands at `0x8050`, one byte before the function — in the
rodata. The rodata is then executed as instructions (whatever bytes
happen to be there), the PC walks off the end of the ROM, and the
machine ends up reading and writing outside it.

**It is layout-dependent, which is why it went unnoticed.** The bug only
fires when the rodata before `.arcade1.text` ends at an address that is
not a multiple of four. Half the shipped games were one byte lucky:

| game | `memcpy` before | after the fix |
|---|---|---|
| breakout | `0x716` | `0x718` |
| flappy | `0x72c` | `0x72c` |
| shmup | `0xa6c2` | `0xa6c4` |
| systems | `0xbb6` | `0xbb8` |
| brickstorm | `0x8010` | `0x8010` |
| tetris, platformer, petalblade, … | already aligned | unchanged |

Four games were carrying a misaligned runtime. A change to any of their
rodata — a new string, a new table — could have turned a working game
into a crashing one without touching its logic. (A game only breaks if
it actually *calls* the misaligned function: the committed `breakout` and
`shmup` images still run — a single-stepping trace of 600 frames of each
never leaves the code — so they never reached that path. BRICKSTORM did:
it copies a `Ball` struct for the MULTI power-up.)

## The fix

Two lines, belt and braces:

* `programs/crt0.S`: `.balign 4` at the top of the `.arcade1.text` section.
* `programs/link.ld`: `. = ALIGN(4);` before `*(.arcade1.text)`.

The linker script is the one that matters (it is the section's *alignment
requirement* that decides where it lands); the `crt0` line makes the
requirement travel with the code if the section is ever moved.

## Verification

The build that crashed now runs clean:

```
$ ./trace_pc k2.bin focus.mask 300 0x8109
no PC left the ROM in 300 frames
```

and every guest in `programs/` now has a 4-byte-aligned runtime:

```
$ for g in programs/*.c; do ... nm ... | grep ' T memcpy'; done
breakout: 00000718 T memcpy
...
```

## What this says about the tests

The golden regression runs the **committed** ROM images, not the ones the
current sources build. Several of those committed images predate their
sources (the M21 camera commit never regenerated the demo ROMs), so a
toolchain fix like this one does **not** show up in `make check` — the
tests keep using the old images. The toolchain fix therefore lands with
the game that found it, and the committed ROMs of the other games are
left alone (regenerating them would sweep in unrelated source changes).

The lesson: the ROMs are the regression, but they are only as current as
the last person who ran `make roms-update`. A build-input bug can sit in
the tree for a long time and only bite when a game's data grows by one
byte.
