#!/usr/bin/env python3
"""Exact host-side model of flappy.c, used to author input.txt.

Replicates the guest's integer maths, pipe recycling, seeded LCG
(arcade1_rng: state*1664525+1013904223 mod 2^32, arcade1_rand = (rng()>>16)%n)
and edge-triggered pad handling, frame for frame. The autopilot plays real
flights; the timeline injects intentional crashes and restarts. Output:
input.txt (run-length script) and a per-frame trace for cross-checking
against `arcade1 run --vars`.
"""
import sys

# --- constants, mirrored from flappy.c ---------------------------------
GROUND_Y, BIRD_X, BIRD_W, BIRD_H = 250, 96, 10, 8
PIPE_W, PIPE_GAP, SPACING, SPEED, N, FIRST_X = 26, 64, 150, 2, 4, 260
GRAV, FLAP, MAX_FALL = 1, -8, 14
GAP_MIN_TOP = 24
CRUISE_Y = 110

FRAMES = 520

# --- timeline (frames, inclusive) ---------------------------------------
# phase 1: fly for real (pipes arrive at frames 82, 157, 232, 307 ...)
# phase 2: stop flapping -> crash; wait; tap A to restart
# phase 3: fly again
# phase 4: stop -> crash; tap A; idle-crash into the ground
CRASH1_AT = 250      # stop flapping here: fall into a pipe/ground
RESTART1  = 280      # tap A here (flight 2)
CRASH2_AT = 405      # stop flapping again
RESTART2  = 435      # tap A here (flight 3: no flaps, ground crash ~473)


class Game:
    def __init__(self):
        self.state = 0
        self.score = 0
        self.bird_y4 = 440
        self.bird_vy = 0
        self.pad_prev = 0
        self.rng = 12345
        self.pipe_x = [FIRST_X + i * SPACING for i in range(N)]
        self.pipe_gap = [80 + ((i * 41) % 110) for i in range(N)]
        self.pipe_scored = [0] * N

    def rand(self, n):
        if n <= 1:
            return 0
        self.rng = (self.rng * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.rng >> 16) % n

    def step(self, pad):
        edged = pad & ~self.pad_prev & 0xFF
        self.pad_prev = pad
        edged_a, edged_start = edged & 0x10, edged & 0x40
        if self.state != 0:
            if edged_a or edged_start:
                self.__init_keep_rng()
            return
        if edged_a:
            self.bird_vy = FLAP
        self.bird_vy += GRAV
        if self.bird_vy > MAX_FALL:
            self.bird_vy = MAX_FALL
        self.bird_y4 += self.bird_vy
        crash = 0
        if self.bird_y4 < 0:
            self.bird_y4 = 0
            crash = 1
        if self.bird_y4 >= (GROUND_Y - BIRD_H) * 4:
            self.bird_y4 = (GROUND_Y - BIRD_H) * 4
            crash = 1
        bird_y = self.bird_y4 >> 2
        for i in range(N):
            self.pipe_x[i] -= SPEED
            if self.pipe_x[i] <= -PIPE_W:
                self.pipe_x[i] += SPACING * N
                self.pipe_gap[i] = GAP_MIN_TOP + self.rand(
                    GROUND_Y - 2 * GAP_MIN_TOP - PIPE_GAP + 1)
                self.pipe_scored[i] = 0
            if not self.pipe_scored[i] and self.pipe_x[i] + PIPE_W < BIRD_X:
                self.pipe_scored[i] = 1
                self.score += 1
            if BIRD_X < self.pipe_x[i] + PIPE_W and BIRD_X + BIRD_W > self.pipe_x[i]:
                if bird_y < self.pipe_gap[i] or \
                   bird_y + BIRD_H > self.pipe_gap[i] + PIPE_GAP:
                    crash = 1
        if crash:
            self.state = 1

    def __init_keep_rng(self):
        rng, pad_prev = self.rng, self.pad_prev
        self.__init__()
        self.rng, self.pad_prev = rng, pad_prev


def autopilot(g):
    """True = want to flap this frame (the game edge-triggers on press)."""
    if g.state != 0:
        return False
    upcoming = [i for i in range(N) if g.pipe_x[i] + PIPE_W >= BIRD_X]
    target = CRUISE_Y
    if upcoming:
        i = min(upcoming, key=lambda k: g.pipe_x[k])
        target = g.pipe_gap[i] + (PIPE_GAP - BIRD_H) // 2
    return (g.bird_y4 >> 2) >= target and g.bird_vy >= 2


def main():
    g = Game()
    holds = []
    trace = []
    prev_want = False
    for f in range(FRAMES):
        # timeline: fly / crash-and-wait / tap-to-restart, three flights
        if f < CRASH1_AT:
            want_a = autopilot(g)          # flight 1
        elif f == RESTART1:
            want_a = True                  # tap A: restart
        elif RESTART1 < f < CRASH2_AT:
            want_a = autopilot(g)          # flight 2
        elif f == RESTART2:
            want_a = True                  # tap A: restart
        else:
            want_a = False                 # let flight 3 fall into the ground
        # the game is edge-triggered: a held A never flaps twice, so force a
        # release frame between any two A frames (every flap is a real tap)
        if want_a and prev_want:
            want_a = False
        prev_want = want_a
        pad = 0x10 if want_a else 0
        g.step(pad)
        holds.append(want_a)
        trace.append((f, g.bird_y4 >> 2, g.bird_vy, g.score, g.state))

    # collapse the per-frame holds into 1-frame A taps (edge detection means
    # a 1-frame tap == any hold that starts on that frame)
    lines = [f"# flappy.arc1 session: {FRAMES} frames — three flights, crashes, restarts",
             "# generated by flappy_model.py (exact integer model of flappy.c)"]
    f = 0
    while f < FRAMES:
        if holds[f]:
            lines.append(f"{f}    {f}    a")
            f += 1
        else:
            s = f
            while f < FRAMES and not holds[f]:
                f += 1
            lines.append(f"{s}    {f-1}    none")
    with open("input.txt", "w") as fh:
        fh.write("\n".join(lines) + "\n")
    with open("model_trace.txt", "w") as fh:
        for f, y, vy, sc, st in trace:
            fh.write(f"{f} {y} {vy} {sc} {st}\n")
    events = []
    for f, y, vy, sc, st in trace:
        prev = trace[f - 1] if f else (0, 0, 0, 0, 0)
        if sc != prev[3]:
            events.append(f"frame {f}: score -> {sc}")
        if st != prev[4]:
            events.append(f"frame {f}: state -> {st}")
    print("events:", *events, sep="\n  ")
    print("final frame:", trace[-1])
    print("A taps:", sum(holds))


if __name__ == "__main__":
    main()
