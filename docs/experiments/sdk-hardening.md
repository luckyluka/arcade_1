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

**The trap.** `anim_tick`'s catch-up loop ran `while (sub >= ms * 60)`;
with `ms == 0` the threshold is 0 and the loop spins forever — the guest
hangs inside the frame that first ticks such a state. Asteroid Run's own
comment kept the folklore ("found the hard way — the guest hung inside
frame 0"), and the external review flagged it as a fresh-agent stopper.

**The fix.** `anim_tick` treats `ms <= 0` as "no step length": it holds
the current step and drops the carry (a state that was never given a
duration doesn't advance, and can't hang). Positive-ms playback is
byte-for-byte the old arithmetic (the loop now uses a hoisted `period`
local instead of recomputing `ms * 60`). The zero case is pinned by a new
probe in `test_arc1assetc.sh`'s host-side anim unit test (hold at t=30
and t=60, carry stays 0).

**The "one affected golden" — and why it is not a behavior change.**
`asteroid_run` is the only golden game built on asset.h, and its
committed ROM predates the guard, so it was refreshed deliberately:
`make experimental`, copy the new `.arc1`, re-`nm` the `.syms`, then
re-run the golden only for this game. The proof of innocence is in the
hashes: all **760 `fb=` values are byte-identical** old-vs-new (same
pixels, same frames — behavior unchanged), while the 760 `hash=` values
shift, exactly as they must: the state hash covers PC and instret, and
new codegen moves both. ROM payload grew 11128 → 11300 bytes (the card-1
trio plus the guard).

**Not touched:** the other 9 goldens (their ROMs don't include asset.h
and weren't rebuilt), walkdemo/asset_demo/map_demo (experimental, not in
`make check`), and the assetc emitter (asset.h compiles host-side in the
probe with `cc` — the guard is plain C99).

## Card 3 — input scripts: worked examples for the sharp edges

**The gap.** The reviewer's note was that input-script *semantics* were
documented (SDK.md "Input script format", the CHEATSHEET one-liner, the
runner's header comment) but only with one polite example — the sharp
edges an agent actually trips on had no worked form: a tap (one-frame
run), a gap (release), and above all the overlap rule.

**Verified, then written.** Before documenting, the runner was re-read to
pin the exact semantics: `input_for_frame` memsets the pad to zero and
lets each covering entry *replace* the whole word (`*out = entry.in`) —
later lines win entirely for the frames they cover; buttons are never
merged across entries. SDK.md now carries a "three sharp edges" block
with a tap, a hold-with-gap, and an overlap spelled out frame by frame
(including the write-it-as-three-lines idiom for "hold right THROUGH the
tap"), plus one sentence on why recordings look tidier than hand-written
scripts yet replay identically whenever the per-frame pad matches.

No code change in this card — documentation only, gated like the rest.
