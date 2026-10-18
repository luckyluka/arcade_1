#!/usr/bin/env python3
"""gen_script.py — build an ARCADE-1 input script for Breakout-lite.

The runner is deterministic (SDK.md "Determinism contract"), so an input
script is a complete, reproducible record of a session. Writing one by hand
for a ball you cannot see is guesswork, so this helper does it in two steps:

  1. run the game headlessly with `--var-every 1`, replaying whatever script
     has been accumulated so far, and read the ball/paddle out of guest RAM
     by name (`--syms` + `--vars`);
  2. simulate the paddle (it moves exactly PADDLE_SPEED px per held
     direction, clamped to the screen — same rule as game.c) and emit
     left/right holds that either chase the ball or run away from it.

Usage:  gen_script.py MODE FRAMES OUTFILE [INTERVAL [PREV_SCRIPT]]
  MODE    chase - park the paddle under the ball -> long rally, score rises
          dodge - run the paddle away from the ball -> lives run out
  INTERVAL  first frame the new entries apply to (default 0), so a script
            can be extended segment by segment.
  PREV_SCRIPT  script replayed while tracing, so the extension continues the
            same session.

Each decision uses the ball position of the previous frame — exactly what
the machine will see when it applies the entry. Output is run-length
encoded, idle stretches omitted (an uncovered frame holds no buttons, per
the input script format in SDK.md).
"""
import re
import subprocess
import sys

PADDLE_W = 56
PADDLE_SPEED = 5
PADDLE_Y = 246
BALL_W = 8
SCREEN_W = 480
PADDLE_START = (SCREEN_W - PADDLE_W) // 2      # 212, matches game.c

VARLINE = re.compile(r"^vars\s+(\d+)\s+(.*)$")


def trace(frames, prev_input=None):
    """Run the game, returning {frame: {var: int}} from the --vars output."""
    cmd = ["./arcade1", "run", "game.arc1", "--frames", str(frames), "--quiet",
           "--syms", "game.syms",
           "--vars", "ball_x,ball_y,ball_vx,ball_vy,paddle_x,lives,state,score",
           "--var-every", "1"]
    if prev_input:
        cmd += ["--input", prev_input]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    rows = {}
    for line in out.splitlines():
        m = VARLINE.match(line.strip())
        if not m:
            continue
        vals = dict(kv.split("=") for kv in m.group(2).split())
        rows[int(m.group(1))] = {k: int(v) for k, v in vals.items()}
    return rows


def buttons_for(mode, ball, paddle_x):
    """Decide the paddle direction for the next frame.

    chase: predict where a descending ball will meet the paddle (straight
    line + reflections off the side walls) and go there; while the ball is
    on its way up, drift back towards the middle. dodge: always flee.
    """
    if mode in ("chase", "follow"):
        # follow: simply sit under the ball. chase: add a landing prediction
        # for descents from high up, where being early matters.
        target = ball.get("ball_x", PADDLE_START) - PADDLE_W // 2
        if mode == "chase":
            vx = ball.get("ball_vx", 0) / 16.0       # px per frame
            vy = ball.get("ball_vy", 0) / 16.0
            y = ball.get("ball_y", 0)
            if vy > 0.01 and y < 200:                # long descent: predict
                frames_left = (PADDLE_Y - y) / vy
                pred = int(round(ball["ball_x"] + vx * frames_left))
                lo, hi = 0, SCREEN_W - BALL_W
                for _ in range(64):                  # reflect off the walls
                    if pred > hi:
                        pred = 2 * hi - pred
                    elif pred < lo:
                        pred = 2 * lo - pred
                    else:
                        break
                target = pred - PADDLE_W // 2
        if paddle_x < target - 2:
            return "right"
        if paddle_x > target + 2:
            return "left"
        return None
    if mode == "dodge":
        # Always flee to the far side, so the ball finds empty floor.
        return "left" if ball["ball_x"] >= SCREEN_W // 2 else "right"
    raise SystemExit("unknown mode %r" % mode)


def simulate(mode, rows, interval):
    frames = sorted(rows)
    first, last = frames[0], frames[-1]
    seed = rows.get(interval - 1) or rows[first]
    paddle = seed.get("paddle_x", PADDLE_START)
    entries = []
    for frame in range(max(interval, first), last + 1):
        ball = rows.get(frame - 1) or rows.get(frame) or seed
        btn = None if ball.get("state", 0) != 0 else buttons_for(mode, ball, paddle)
        if btn == "left":
            paddle -= PADDLE_SPEED
        elif btn == "right":
            paddle += PADDLE_SPEED
        paddle = max(0, min(SCREEN_W - PADDLE_W, paddle))
        entries.append((frame, btn or "none"))
    return entries


def rle(entries):
    """Merge runs; idle ('none') stretches are dropped entirely."""
    lines, start, cur = [], None, None
    for frame, btn in entries:
        if btn == cur:
            continue
        if cur is not None and cur != "none":
            lines.append("%d %d %s" % (start, frame - 1, cur))
        start, cur = frame, btn
    if cur is not None and cur != "none":
        lines.append("%d %d %s" % (start, entries[-1][0], cur))
    return "\n".join(lines) + ("\n" if lines else "")


def main(argv):
    if len(argv) < 4:
        raise SystemExit(__doc__)
    mode, frames, outfile = argv[1], int(argv[2]), argv[3]
    interval = int(argv[4]) if len(argv) > 4 else 0
    prev = argv[5] if len(argv) > 5 else None
    rows = trace(frames, prev)
    text = ("# generated by gen_script.py mode=%s frames=%d interval=%d\n"
            % (mode, frames, interval))
    text += rle(simulate(mode, rows, interval))
    with open(outfile, "w") as f:
        f.write(text)
    sys.stderr.write("wrote %s (%d trace frames, %d lines)\n"
                     % (outfile, len(rows), text.count("\n")))


if __name__ == "__main__":
    main(sys.argv)
