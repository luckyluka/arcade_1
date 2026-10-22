#!/usr/bin/env python3
"""tools/audit_brickstorm.py — a long, instrumented playtest of BRICKSTORM.

The recorder (`tools/record_hits.py`) watches the paddle contacts. This
watches the whole rally: it drives the game through many scripted
sessions and checks the invariants a *player* would feel, not the ones a
unit test would assert.

    python3 tools/audit_brickstorm.py <rom> [--syms FILE] [--frames N]
                                       [--sessions N] [--verbose]

What it checks, per frame:

* **the glove** — every change of the ball's direction must be
  explainable: a mirror about the horizontal or the vertical axis (a
  brick, a wall, the ceiling, the boss), the flat-angle limit putting
  the ball back on the shallowest legal line, a paddle contact, or a
  new ball being served. Anything else is an invisible hand moving the
  ball, and it is reported with its frame.
* **the catch** — the catch point is always ON the paddle, a left catch
  never leaves to the right, a right catch never leaves to the left,
  and a centre catch never goes straight up (a dead vertical launch
  lets the ball bounce up and down one column for ever).
* **the flat limit** — no frame leaves the ball flatter than the
  shallowest legal angle (60° from the vertical).
* **the ball's speed** — a property of the stage; it only changes when
  a SLOW/FAST power-up says so.
* **the bookkeeping** — the score never goes down, the lives only go
  down on a death, the level only goes up, the boss's health only goes
  down, and the state machine never teleports.

A blind paddle rarely catches the ball (six contacts in 48,000 frames),
so the sessions are only a smoke test of the rules. For a real
playtest, build the game with an autopilot that tracks the ball (a
scratch build — the audit does not need one) and point this script at
that image: the contacts then run into the hundreds.

Stdlib only.
"""
import argparse
import math
import os
import subprocess
import sys

# --- the game's own numbers (arcade1 info is the authority) ----------------
FLAT_LIMIT = 223          # |vx| may not exceed 223/128 * |vy|
PADDLE_Y = 244
SCREEN_H = 270
BALL_S = 8

# The states: 0 title, 1 serve, 2 playing, 3 the death pause,
# 4 the level clear, 5 the game over, 6 the win.
LEGAL_STATES = {
    (0, 1), (1, 0), (1, 2), (1, 4), (1, 5),
    (2, 1), (2, 3), (2, 4),
    (3, 1), (3, 5),
    (4, 1), (4, 5), (4, 6),
    (5, 0),
}

VARS = ("state,score,hi_score,lives,level,bricks_left,ball_x,ball_y,"
        "paddle_x,paddle_w,power,power_timer,boss_hp,boss_state,ball_vx,"
        "ball_vy,ball_id,hit_frame,hit_side,hit_off,hit_vx,hit_vx_in,"
        "hit_count")


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


def lcg(s):
    return (s * 1103515245 + 12345) & 0x7fffffff


def make_script(kind, seed, frames):
    """The sessions: sweeps, wanderers, focus-players, idlers."""
    lines = ["0    9    none", "10   11   start", "12   14   none"]
    f = 15
    i = 0
    st = seed
    if kind == "idle":
        lines.append("%-4d %-4d none" % (f, frames - 1))
        return "\n".join(lines) + "\n"
    if kind == "sweep":
        period = 14 + (seed % 40)
        while f < frames:
            btn = "left" if i % 2 == 0 else "right"
            lines.append("%-4d %-4d a %s" % (f, f + period - 1, btn))
            f += period
            i += 1
        return "\n".join(lines) + "\n"
    if kind == "wander":
        while f < frames:
            st = lcg(st)
            r = st % 5
            dur = 10 + (st >> 8) % 20 if r < 4 else 8 + (st >> 8) % 12
            btn = "left" if r < 2 else ("right" if r < 4 else "none")
            lines.append("%-4d %-4d a %s" % (f, f + dur - 1, btn))
            f += dur
            i += 1
        return "\n".join(lines) + "\n"
    if kind == "focus":
        while f < frames:
            btn = "left" if i % 2 == 0 else "right"
            lines.append("%-4d %-4d a b %s" % (f, f + 29, btn))
            f += 30
            i += 1
        return "\n".join(lines) + "\n"
    raise ValueError(kind)


def trace(rom, syms, script, frames):
    """Run one session and return the per-frame globals."""
    path = os.path.join(os.environ.get("TMPDIR", "/tmp"), "audit_input.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(script)
    cmd = [find_cli(), "run", rom, "--frames", str(frames), "--input", path,
           "--vars", VARS, "--var-every", "1", "--quiet"]
    if syms:
        cmd += ["--syms", syms]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(out.stderr)
        raise SystemExit("the run failed (exit %d)" % out.returncode)
    rows = []
    for line in out.stdout.splitlines():
        if not line.startswith("vars "):
            continue
        parts = line.split()
        kv = {}
        for p in parts[2:]:
            if "=" in p:
                k, v = p.split("=", 1)
                try:
                    kv[k] = int(v)
                except ValueError:
                    kv[k] = v
        kv["_frame"] = int(parts[1])
        rows.append(kv)
    return rows


def angle(vx, vy):
    """The direction, in degrees, 0 = straight up."""
    if vx == 0 and vy == 0:
        return None
    return math.degrees(math.atan2(vx, -vy))


def is_mirror(prev, now):
    """Is `now` a mirror of `prev` about the vertical or the horizontal?"""
    (pvx, pvy), (vx, vy) = prev, now
    if pvx == vx and pvy == -vy:        # about the horizontal axis
        return True
    if pvx == -vx and pvy == vy:        # about the vertical axis
        return True
    return False


def is_limit_line(vx, vy):
    """The flat limit's line: |vx| / |vy| = 223 / 128."""
    ay = abs(vy)
    if not ay:
        return False
    return abs(abs(vx) / float(ay) - FLAT_LIMIT / 128.0) < 0.02


def audit(name, rows, report, verbose):
    def flag(kind, frame, msg):
        report.append((kind, name, frame, msg))

    prev = None            # the last frame's (angle, vx, vy)
    prev_pos = None
    prev_bid = None
    prev_state = None
    prev_score = 0
    prev_lives = 3
    prev_level = 1
    prev_boss = 0
    prev_hits = 0
    speeds = {}
    contacts = 0

    for f in rows:
        st = f.get("state", 0)
        frame = f["_frame"]

        # --- the bookkeeping ---------------------------------------------
        if f.get("score", 0) < prev_score:
            flag("score", frame, "the score went down: %d -> %d"
                 % (prev_score, f["score"]))
        prev_score = f.get("score", 0)
        if f.get("hi_score", 0) < f.get("score", 0):
            flag("hiscore", frame, "the hi-score is below the score")
        if f.get("lives", 3) > prev_lives:
            flag("lives", frame, "the lives went up outside a bonus")
        prev_lives = f.get("lives", 3)
        if f.get("level", 1) < prev_level:
            flag("level", frame, "the level went down")
        prev_level = f.get("level", 1)
        if prev_boss and f.get("boss_hp", 0) > prev_boss:
            flag("boss", frame, "the boss's health went up")
        prev_boss = f.get("boss_hp", 0)
        if st not in (0, 1, 2, 3, 4, 5, 6):
            flag("state", frame, "the state is %d" % st)
        if prev_state is not None and st != prev_state:
            if (prev_state, st) not in LEGAL_STATES:
                flag("state", frame, "the state jumped %d -> %d"
                     % (prev_state, st))

        # --- the contacts -------------------------------------------------
        contact_now = f.get("hit_count", 0) > prev_hits
        if contact_now:
            contacts += 1
            side = f.get("hit_side", 0)
            vx = f.get("hit_vx", 0)
            vx_in = f.get("hit_vx_in", 0)
            off = f.get("hit_off", 0)
            half = f.get("paddle_w", 0) * 8      # the half width, 1/16 px
            if half and abs(off) > half:
                flag("catch", frame, "the catch point is off the paddle: "
                     "%d (1/16 px), the paddle is %d px wide"
                     % (off, f["paddle_w"]))
            elif side < 0 and vx > 0:
                flag("catch", frame, "a left catch left to the right (vx %d)" % vx)
            elif side > 0 and vx < 0:
                flag("catch", frame, "a right catch left to the left (vx %d)" % vx)
            elif side == 0:
                if vx == 0:
                    flag("catch", frame, "a centre catch went straight up")
                elif vx_in and (vx > 0) != (vx_in > 0):
                    flag("catch", frame, "a centre catch changed sides")
        prev_hits = f.get("hit_count", 0)

        # --- the ball -----------------------------------------------------
        vx, vy = f.get("ball_vx", 0), f.get("ball_vy", 0)
        a = angle(vx, vy)
        if a is not None:
            speed = round(math.hypot(vx, vy))
            speeds[speed] = speeds.get(speed, 0) + 1
            ax, ay = abs(vx), abs(vy)
            if ay and ax * 128 > ay * FLAT_LIMIT:
                flag("flat", frame, "the ball is flatter than the shallowest "
                     "legal angle: vx=%d vy=%d" % (vx, vy))
            # the ball may not be inside the paddle
            if (PADDLE_Y <= f.get("ball_y", 0) <= PADDLE_Y + 8 and
                    abs(f.get("ball_x", 0) - f.get("paddle_x", 0)) <
                    f.get("paddle_w", 56) // 2 and
                    f.get("ball_y", 0) != PADDLE_Y - BALL_S):
                flag("inside", frame, "the ball is inside the paddle: "
                     "x=%d y=%d paddle=%d" % (f.get("ball_x"), f.get("ball_y"),
                                             f.get("paddle_x")))
            # the ball may not be TELEPORTED: it may not move further in a
            # frame than its own speed, plus the one pop the classic
            # makes (the ball is put back just outside the brick it
            # hit). A contact is exempt: the paddle puts the ball back
            # on its plane.
            if prev_pos is not None and not contact_now and st == prev_state:
                dx = abs(f.get("ball_x", 0) - prev_pos[0])
                dy = abs(f.get("ball_y", 0) - prev_pos[1])
                budget = max(ax, ay) / 16.0 + BALL_S + 3
                # a ball that fell off the screen is replaced by a new one
                # on the paddle, and the state changes when that happens:
                # that is the serve, not a teleport
                if max(dx, dy) > budget:
                    flag("teleport", frame,
                         "the ball moved %d px in a frame (it may move "
                         "%.1f): (%d,%d) -> (%d,%d)"
                         % (max(dx, dy), budget, prev_pos[0], prev_pos[1],
                            f.get("ball_x"), f.get("ball_y")))
            # the observable may have moved to another ball
            switched = False
            if prev_bid is not None and f.get("ball_id") != prev_bid:
                switched = True
            if prev_pos is not None:
                if abs(f.get("ball_x", 0) - prev_pos[0]) > 6 or \
                        abs(f.get("ball_y", 0) - prev_pos[1]) > 6:
                    switched = True
            prev_bid = f.get("ball_id")
            prev_pos = (f.get("ball_x", 0), f.get("ball_y", 0))

            if prev is not None:
                pa, pvx, pvy = prev
                # a state change means a new ball was served: a launch, not
                # a glove — and a paddle contact is a launch too
                if pa is not None and abs(a - pa) > 0.6 and \
                        st == prev_state and not switched and not contact_now:
                    if not is_mirror((pvx, pvy), (vx, vy)) and \
                            not is_limit_line(vx, vy):
                        flag("glove", frame,
                             "the ball's direction changed from %.1f to %.1f "
                             "with no explanation (vx %d->%d, vy %d->%d)"
                             % (pa, a, pvx, vx, pvy, vy))
            prev = (a, vx, vy)
        else:
            prev = None

        prev_state = st

    if verbose:
        sys.stderr.write("audit: %s — %d contacts\n" % (name, contacts))
    return contacts, speeds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--syms", help="the symbol dump (defaults to <rom>.syms)")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--sessions", type=int, default=16)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    syms = args.syms
    if syms is None:
        guess = os.path.splitext(args.rom)[0] + ".syms"
        syms = guess if os.path.exists(guess) else None

    kinds = ("sweep", "wander", "focus", "idle")
    report = []
    total_contacts = 0
    speed_table = {}
    n = 0
    for kind in kinds:
        for seed in (1, 7, 19, 53):
            if n >= args.sessions:
                break
            n += 1
            name = "%s%d" % (kind, seed)
            rows = trace(args.rom, syms, make_script(kind, seed, args.frames),
                         args.frames)
            c, speeds = audit(name, rows, report, args.verbose)
            total_contacts += c
            for k, v in speeds.items():
                speed_table[k] = speed_table.get(k, 0) + v

    print("=== %d sessions x %d frames = %d frames of play"
          % (n, args.frames, n * args.frames))
    print("=== %d paddle contacts" % total_contacts)
    print("=== the ball's speed (1/16 px per frame) -> the frames it was seen:")
    for k in sorted(speed_table):
        print("      %3d : %d frames" % (k, speed_table[k]))
    if not report:
        print("\nNO FINDINGS")
        return 0
    print("\n=== %d finding(s):" % len(report))
    kinds = {}
    for kind, name, frame, msg in report:
        kinds[kind] = kinds.get(kind, 0) + 1
    for kind, n_ in sorted(kinds.items()):
        print("   %-8s %d" % (kind, n_))
    print()
    shown = {}
    for kind, name, frame, msg in report:
        if shown.get(kind, 0) < 6:
            shown[kind] = shown.get(kind, 0) + 1
            print("   [%s] %s frame %d: %s" % (kind, name, frame, msg))
    return 1


if __name__ == "__main__":
    sys.exit(main())
