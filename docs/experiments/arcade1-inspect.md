# arcade1 inspect v1 + command records — findings (Wave B, ROADMAP_V3 §5, cards B1/B2)

Status: shipped as `arcade1 inspect` (`tools/arcade1.c`), device-side
recording in `src/arcade1/video.{h,c}`, tested by `make test-inspect` and
`make test-inspect-commands` (both part of `make check`). Findings log per
rule 8 — measured facts, not adjectives.

## What was built

| Piece | Card | File |
|---|---|---|
| Structured KEY-value report (`--json`, `--regs`) | B1 | `tools/arcade1.c` (`cmd_inspect`) |
| `tests/test_inspect.sh` (5 checks) | B1 | `tests/` |
| Command records `--commands`, pixel provenance `--at X,Y` | B2 | `src/arcade1/video.{h,c}`, `tools/arcade1.c` |
| PC-of-issuing-store wiring | B2 | `src/arcade1/machine.c` (`&m->cpu.pc`) |
| Acceptance guest (sprite then covering FILL) | B2 | `tests/guests/probe_cmds.c` |
| `tests/test_inspect_commands.sh` (4 checks) | B2 | `tests/` |

## Measured facts

| Fact | Value |
|---|---|
| B1 checks | 5 (keys, --regs gate, --strict agreement, --json parse, usage=2) |
| B2 checks | 4 (order+PC-in-range, --at ownership, hash parity, json/usage) |
| m29game @1800 frames + conditions | FRAME 1800, INSTRET_FRAME 8656, STRAY 0, ROM_USED 3068/262144, VIDEO_OOB_DRAWS 0 |
| probe per frame (timer-paced) | exactly 4 commands: CLEAR px=129600, SPRITE px=64, FILL px=1600, PRESENT px=0 |
| `--at 12,12` (sprite+FILL pixel) | id=FILL's record; `--at 200,200` → CLEAR; off-screen → id=0 + note |
| Hash parity (acceptance 4) | STATE_HASH/FB_HASH byte-identical: inspect = inspect --commands = run |
| Disabled cost (first draft) | +14 % test-golden, +21 % test-m29game — falsified the "zero cost when disabled" rule |
| Disabled cost (shipped) | test-golden 9.70 s vs B1's 9.52 s, test-m29game 1.96 s vs 1.98 s — at baseline |

## Premise gaps found (rule 7) and how they were resolved

1. **B2 acceptance presumes hash visibility inspect didn't have.** "Hashes
   of runs with and without `--commands` are identical" is untestable if
   the report prints no hashes (only `run` does). Resolved by adding
   `STATE_HASH`/`FB_HASH` keys — the same `state_hash`/`fb_hash` `run`
   prints, so a report also cross-checks against any golden's
   `frame N hash=…` line. B1's key set was "every key present" (its
   acceptance), not "exactly these keys", so the addition stays within
   both cards' text.
2. **B1 wall-time budget vs 1800-frame pair.** The B1 test's natural
   m29game run is the manifest's 1800 frames; inspect+strict at 1800 put
   `make check` at +5.1 % (budget ≤5 %). The card pins no frame count for
   m29game — only identical conditions on both sides — so both runs moved
   to inspect's default 600: +2.0 %.

## Facts that shaped the implementation

- **`cpu->pc` IS the issuing store's PC.** `exec.c` commits `cpu->pc =
  next` only after the instruction retires, and video commands execute
  synchronously inside the `REG_CMD` MMIO write — so the machine merely
  wires `m->video.store_pc = &m->cpu.pc` (like the borrowed fb/ROM/RAM
  pointers: kept across resets). No exec-path change at all.
- **`state_hash` covers regs, pc, instret, RAM and the stray counter;
  `fb_hash` the framebuffer** — never device state. Recording lives in
  device fields + two heap buffers, so hash changes are structurally
  impossible; the parity test pins it anyway.
- **`.syms` sidecars hold RAM symbols only** (A1's `--vars` scope filters
  non-RAM at write time), so the "PC inside the guest function's symbol
  range" assertion reads the kept guest `.elf` via `nm` (repo convention:
  test-only guests live in `tests/guests/probe_*.c`, `.elf` survives the
  `.bin`→`.arc1` chain).
- **Guest pacing.** `TIMER_COUNT` ticks exactly once per machine frame, so
  the probe draws one sequence per frame: the recorded last frame is a
  clean CLEAR/SPRITE/FILL/PRESENT, never near the 4096-record cap.
- **id 0 means "no record":** never-written pixels and commands past the
  4096 cap both stamp 0; an over-cap command executes unrecorded rather
  than distorting the frame.
- **B1's first JSON draft closed `VAR` but not the object** — `json.tool`
  caught it ("Expecting ',' delimiter"), which is exactly why the
  acceptance parses it; fixed by separating the VAR close, the optional
  REGS/COMMANDS/AT parts and the final `}`.
- **The rule "zero cost when disabled" is falsifiable — and the first
  draft failed it.** An `if (v->owner)` inside the per-pixel loops cost
  +14 %/+21 % on the video-heavy tests: the branch also stops GCC from
  treating `framebuffer[i] = c` as a vectorizable fill, and the aliasing
  store forces `v->owner` to be reloaded every iteration. Fix: loop
  versioning — hoist `owner`/`rec_cur_id` into loop-invariant locals and
  hand-split the two dense loops (clear, fill); the blit paths only need
  the hoist (their per-pixel decode work dwarfs one predicted branch).
  Disabled cost dropped to measurement noise. Measured with a
  `git stash` A/B on the same tree (B1: 9.52 s / 1.98 s; stamped draft:
  10.86 s / 2.40 s; versioned: 9.70 s / 1.96 s).
