# SDK hardening round — findings

Round commissioned at the post-Wave-6 review (2026-10-20): the external
review of the SDK surfaced two traps that would stop a fresh agent building
from the docs alone, plus a documentation gap. This round fixes them, one
card per commit, each behind the full `make clean && make check` gate. Not a
ROADMAP_V3 wave — the V3 plan ends at Wave 6; these cards are candidates for
ROADMAP_V4's first wave if the owner ratifies it.

## Card 1 — memcpy/memset/memmove in the SDK

**The trap.** GCC compiles struct assignment, array initialisers and
`memset`-style aggregate zeroing into calls to `memcpy`/`memset`. The guest
build is `-nostdlib` with a hand-rolled crt0, so those symbols did not
exist: any guest that assigned a struct died at link with `undefined
reference to memcpy`. The repo contains circumvention folklore, not a fix —
`asteroid_run.c` lists every per-slot `anim_state` by hand *specifically*
because "a struct-copy assignment would make the freestanding build emit
memcpy" (its comment, still true and now harmless), and the external review
found fresh agents walking into it.

**The fix.** crt0.S grows the classic freestanding trio — `memcpy`,
`memset`, `memmove` — as straight RV32I byte loops (correctness over
speed: guest copies are struct-sized; big fills are the machine's job via
`arcade1_clear_fb`). `memmove` handles both overlap directions, which the
compiler does not emit on its own but guests may call.

**Why a new section (`.arcade1.text`).** The state hash covers PC and
instret, so any guest-code address shift rewrites every golden hash line.
The linker script's `*(.text .text.*)` wildcard would have swallowed a
`.text.*`-named section *before* the guest's own code (crt0.o is first on
the link line), shifting every guest symbol. Naming the section
`.arcade1.text` (matched by no existing wildcard) and placing
`*(.arcade1.text)` *after* the rodata wildcards makes the runtime append
itself at the tail of ROM: guest code and rodata addresses are untouched.

**Proof of stability** (before/after `riscv64-unknown-elf-nm` diff of a
fresh `example_pong`): every pre-existing symbol keeps its address exactly;
only `memcpy`/`memset`/`memmove` appear, past the guest's last byte. Text
grew 744 → 1076 bytes in ROM, appended.

**Also documented.** GAME_DEV_GUIDE.md gains a "There is no libc" paragraph:
the trio exists, nothing else does (no printf/malloc/strings), and 64-bit
arithmetic stays a link error (RV32IM, no libgcc helpers linked) — the one
gap deliberately left open, since pulling in libgcc would change every
fresh build's link recipe for little gain.

**Not refreshed:** no committed ROM changed. Committed golden ROMs predate
this runtime and still run byte-identically (their frames are what the
goldens hash); fresh builds gain the trio at the tail with identical
behavior.

## Card 2 — anim_tick: a 0 ms step length holds, never hangs

(see git history of this file for the card-2 section)

## Card 3 — input scripts: worked examples for the sharp edges

(see git history of this file for the card-3 section)
