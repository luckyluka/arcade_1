# Play window — `arcade1 run --live` (findings)

Status: built and passing (ROADMAP_V3 §4.5 as amended by the owner: SDL3,
exit 3 when absent, not in `make check`). Run its test by hand:
`make test-playwindow` → `test-playwindow: ok (15 checks, Wave 6 card:
play window --live)`. The card's design decisions and their basis are in
`wave6-evidence.md`; this file records what implementing it taught.

## What was built

| Piece | File |
|---|---|
| `--live` flag, live loop, key mapping, exit 3 path | `tools/arcade1.c` |
| SDL3 detection (`pkg-config sdl3`), `CLI_NOSDL`, `test-playwindow` | `Makefile` |
| XTEST key injector (no xdotool on this machine) | `tests/xkey.c` |
| 15-check acceptance test (by hand, never in `make check`) | `tests/test_playwindow.sh` |

The window consumes what already exists: the framebuffer the VM renders
every frame and the input the pad already reads. It touches only the input
source and the presentation — the frame loop, the hashes, the captures and
`--record` stay the shared scripted ones, which is why a live session and a
scripted session produce identical per-frame hashes.

## SDL3 API shape (3.2.16, from SDL2)

The owner's §4.5 edit chose SDL3; the surface actually used is small:

- `SDL_Init(SDL_INIT_VIDEO)` → `SDL_CreateWindow(title, w, h,
  SDL_WINDOW_RESIZABLE)` (SDL3 dropped SDL2's x/y/parent arguments) →
  `SDL_CreateRenderer(window, NULL)` → one
  `SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
  SDL_TEXTUREACCESS_STREAMING, 480, 270)` with
  `SDL_SetTextureScaleMode(..., SDL_SCALEMODE_NEAREST)`.
- Presentation is `SDL_UpdateTexture` (pitch = 960 bytes, straight from
  `arcade1_framebuffer_pitch`) → `SDL_RenderClear/Texture/Present`. A
  renderer without an accelerated path falls back to SDL's software
  renderer — both are tested (`SDL_VIDEODRIVER=dummy` covers the
  renderer-only shape headlessly).
- Pacing is `SDL_GetTicksNS`/`SDL_DelayNS` against a 60 Hz deadline that
  never owes a catch-up burst after an overrun — a slow frame skips ahead
  rather than fast-forwarding.
- Events: `SDL_EVENT_QUIT` (window close) and key events; held-pad keys are
  level-triggered statics read once per frame, while R/ESC are
  edge-triggered (`down && !repeat`).

If any step fails — including "no display at all" — the message names the
step on stderr and the run exits 3 with everything unwound.

## Driving it from a test: XTEST, focus, and mutter

There is no xdotool or python-xlib here, so `tests/xkey.c` calls XTEST
directly (hand-declared `XTestFakeKeyEvent` — no `<XTest.h>` headers are
installed; `libXtst.so.6` is runtime-only, so the Makefile-adjacent build
links `ls /usr/lib/*/libXtst.so.*`).

Two real-world traps, both of which cost test failures:

1. **The GNOME/mutter frame window inherits the client's title.** A naive
   "find the window whose name matches" injector focused the *frame*, and
   every keystroke went nowhere. `xkey` therefore matches on the window
   **class** (`arcade1`) instead of the title.
2. **Focus is not sticky.** Before each key the injector re-asserts focus
   (`_NET_ACTIVE_WINDOW` + `XSetInputFocus`, then a settle-verify loop);
   focus drift during a macro is reported as a warning, not a failure — a
   compositor racing the test is not a product bug.

The test proves the window with behavior, not pixels: Right/Left must move
walkdemo's `x` from its start value, the recording must contain `right` /
`left` ranges, and replaying that recording must reproduce the per-frame
hashes bit-for-bit.

## R and ESC both finish the recording

The card gives R "end the session writing `--record`'s script" and ESC
"quit". Implemented: both stop the loop, and **an open recording is
finalized on either exit** (and on window close) — a live session always
leaves a complete, replayable script. The card text does not say whether
ESC was meant to discard; the current reading (always finalize) is the one
that keeps "record a human session as an input script" true for every exit.
Reported as a semantics gap rather than invented away.

## SELECT: a premise gap, not a binding

The §10 mapping lists arrows/WASD, Z, X, Enter — and SELECT has no
binding anywhere in the card text. It is left unbound (the pad bit stays 0)
and reported here; inventing a key would be a §17-class change to the
human interface, which is the owner's call.

## Why none of this is in `make check`

SDL3 and a display are optional: a headless CI box without SDL3 must still
gate every other card. So the Makefile builds the same `tools/arcade1.c`
twice — once with `-DARCADE1_HAVE_SDL3` (the window) and once as
`build/arcade1_nosdl` (the absence path) — and `test-playwindow` (by hand)
checks both: exit 3 + clean message without SDL3, full live session with it.
`make check` never links SDL3 into anything but the normal CLI binary, and
never opens a window.

## The Makefile ordering bug this card flushed out

`make test-playwindow` initially ran with `build/arcade1_nosdl` missing:
the target listed `$(CLI_NOSDL)` as a prerequisite, but the variable was
defined *below* the rule. **Prerequisite lists expand when make reads the
rule; recipes expand when it runs it.** So every `$(CLI_BIN)` prerequisite
in rules above the (previously late) definition — `test-text`,
`test-mapdemo`, `test-inspect`, `test-inspect-commands`,
`test-golden-conditions`, `test-walkdemo`, `test-assetdemo` — had silently
expanded to *nothing*; the recipes still passed the right path (late
expansion), which hid it. Sequential `make check` never tripped because the
walk order happens to build the CLI at `test-run` (whose rule is below the
definition) before those targets run.

Two visible symptoms:

- `make clean && make check -j4` failed once with
  `test-golden-conditions: ... build/arcade1: not found` — with its
  prerequisite gone, the target was free to start before the CLI existed,
  and the `test-golden` recipe's recursive sub-makes (whose cc lines land in
  `build/golden/*.log`, not the main log) were racing the parent make for
  the same artifacts.
- `make clean && make test-playwindow` could not build its no-SDL CLI.

Fix: the CLI variable block (`CLI_BIN`, `CLI_SRC`, the SDL3/`PLAY_*` vars,
`CLI_NOSDL`) moved above the first rule that mentions it, with a comment
recording the expansion rule. Sequential `make check` re-passed after the
move; `make -n` on a deleted CLI now shows the `cc ... -o build/arcade1`
prerequisite step it was silently skipping. (A parallel `make check -j4`
still has a separate, pre-existing wrinkle — `test-golden`'s sub-makes and
the parent compete for the same build artifacts — which is outside this
card and unchanged by it.)

## Addendum 2026-10-20: key injection vs. a busy live desktop

Observed during the SDK-hardening round (card 2), on the owner's actively
used GNOME/mutter desktop: section 4's XTEST key checks failed three runs
in a row with the same signature — the recording carries
`0 <last> none`, i.e. no injected key ever reached the pad. The window
checks, the dummy-driver idle/replay checks and every other check passed.

Evidence that this is the environment, not the code:

- `xkey` itself reports `focus moved to 0x2600004 after the macro` — the
  owner's full-screen VS Code (the TUI running this session) re-grabs the
  keyboard focus, so the XTEST keys land in the editor. xkey's exit-4 path
  ("could not keep focus for a key") exists for exactly this.
- A manual reproduction caught the same: during a 3 s Right hold,
  `_NET_ACTIVE_WINDOW` ends back on the VS Code window, and the keyed
  run's final state hash is *identical to the no-input run's* — the game
  saw nothing.
- The same test, same binary, passed earlier the same morning before the
  desktop gained a full-screen mail composer and heavier TUI activity.

The live loop, recording and replay logic are still pinned headlessly by
section 3 (dummy driver), which is why this stays a by-hand test and never
enters `make check`. If this recurs, two options for a future card: run
the key section on a quiet desktop / spare X server, or harden `xkey` to
re-verify focus before each key word (a WM that bounces focus mid-hold
would then only lose the keys pressed while unfocused, not the whole
macro).
