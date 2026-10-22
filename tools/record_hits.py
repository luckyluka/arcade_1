#!/usr/bin/env python3
"""tools/record_hits.py — record a game's paddle contacts, frame by frame.

A game that publishes its contacts (BRICKSTORM does: `hit_frame`,
`hit_off`, `hit_side`, `hit_vx`, `hit_count`) can be *recorded* with the
CLI's own trace: `arcade1 run --vars ... --var-every 1` prints the named
globals every frame, and this turns that trace into a contact log — one
line per contact, with the exact frame it happened on.

    python3 tools/record_hits.py <rom> [--input SCRIPT] [--frames N]
                                   [--syms FILE] [--out FILE]

The log is one line per contact:

    frame  side  offset  vx
      812    -1    -186  -36     the ball landed 11.6 px left of the
                                centre and left to the left

and with --check it also asserts the game's own rule, if it has one: a
contact on the left must leave to the left, a contact on the right to
the right, and a contact dead centre must keep the way the ball was
already travelling — never straight up, which would let the ball bounce
up and down one column for ever. That is the rule the BRICKSTORM paddle
follows ("the catch point decides the direction, the sweep only bends
it").

Stdlib only.
"""
import argparse
import os
import re
import subprocess
import sys


def find_cli():
    """The CLI: $ARCADE1, else the repository's build, else the PATH."""
    env = os.environ.get("ARCADE1")
    if env:
        return env
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for cand in (os.path.join(root, "build", "arcade1"),
                 os.path.join(root, "arcade1")):
        if os.path.exists(cand):
            return cand
    return "arcade1"


def trace(rom, script, frames, syms, vars_list):
    """Run the game and return {frame: {name: value}} from its trace."""
    cmd = [find_cli(), "run", rom,
           "--frames", str(frames), "--vars", vars_list, "--var-every", "1",
           "--quiet"]
    if script:
        cmd += ["--input", script]
    if syms:
        cmd += ["--syms", syms]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(out.stderr)
        raise SystemExit("the run failed (exit %d)" % out.returncode)
    frames_seen = {}
    for line in out.stdout.splitlines():
        if not line.startswith("vars "):
            continue
        parts = line.split()
        frame = int(parts[1])
        values = {}
        for kv in parts[2:]:
            if "=" in kv:
                k, v = kv.split("=", 1)
                try:
                    values[k] = int(v)
                except ValueError:
                    values[k] = v
        frames_seen[frame] = values
    return frames_seen


def contacts(frames_seen):
    """The contacts, in order: one entry per change of hit_count."""
    out = []
    last = 0
    for frame in sorted(frames_seen):
        v = frames_seen[frame]
        count = v.get("hit_count")
        if count is None:
            continue
        if count > last:
            last = count
            out.append((frame, v))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--input", help="the input script")
    ap.add_argument("--frames", type=int, default=2600)
    ap.add_argument("--syms", help="the symbol dump (defaults to <rom>.syms)")
    ap.add_argument("--out", help="write the log here (default: stdout)")
    ap.add_argument("--check", action="store_true",
                    help="assert the catch-point rule over the whole log")
    args = ap.parse_args()

    rom = args.rom
    syms = args.syms
    if syms is None:
        guess = os.path.splitext(rom)[0] + ".syms"
        syms = guess if os.path.exists(guess) else None

    vars_list = "hit_frame,hit_off,hit_side,hit_vx,hit_vx_in,hit_count,paddle_w"
    frames_seen = trace(rom, args.input, args.frames, syms, vars_list)
    hits = contacts(frames_seen)
    if not hits:
        sys.stderr.write("record_hits: no contacts in %d frames — does the "
                         "game publish hit_count?\n" % args.frames)
        return 1

    lines = ["# the paddle contacts: the frame, the side of the centre the",
             "# ball landed on, how far from the centre (1/16 px), the",
             "# horizontal velocity it left with, and the one it arrived with.",
             "#  frame  side  offset      vx   vx in"]
    for frame, v in hits:
        lines.append("%6d  %4d  %6d  %6d  %6d" % (v.get("hit_frame", frame),
                                                   v.get("hit_side", 0),
                                                   v.get("hit_off", 0),
                                                   v.get("hit_vx", 0),
                                                   v.get("hit_vx_in", 0)))
    text = "\n".join(lines) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
        sys.stderr.write("record_hits: %d contacts -> %s\n"
                         % (len(hits), args.out))
    else:
        sys.stdout.write(text)

    if args.check:
        bad = []
        for frame, v in hits:
            side = v.get("hit_side", 0)
            vx = v.get("hit_vx", 0)
            vx_in = v.get("hit_vx_in", 0)
            off = v.get("hit_off", 0)
            half = v.get("paddle_w", 0) * 8      # half the paddle, in 1/16 px
            if half and abs(off) > half:
                bad.append((frame, side, vx,
                            "the catch point is off the paddle (%d, the "
                            "paddle is %d px wide)" % (off, v["paddle_w"])))
            elif side < 0 and vx > 0:
                bad.append((frame, side, vx, "a left catch left to the right"))
            elif side > 0 and vx < 0:
                bad.append((frame, side, vx, "a right catch left to the left"))
            elif side == 0:
                if vx == 0:
                    bad.append((frame, side, vx,
                                "a centre catch went straight up"))
                elif vx_in and (vx > 0) != (vx_in > 0):
                    bad.append((frame, side, vx,
                                "a centre catch changed sides"))
        if bad:
            for frame, side, vx, why in bad[:10]:
                sys.stderr.write("record_hits: frame %d: %s (side %d, vx %d)\n"
                                 % (frame, why, side, vx))
            sys.stderr.write("record_hits: %d contact(s) broke the rule\n"
                             % len(bad))
            return 1
        sys.stderr.write("record_hits: %d contacts, all of them followed the "
                         "catch-point rule\n" % len(hits))
    return 0


if __name__ == "__main__":
    sys.exit(main())
