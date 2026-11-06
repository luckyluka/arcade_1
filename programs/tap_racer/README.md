# Tap Racer for ARCADE-1

`tapracer.arc1` is a ready-to-run ROM (11,400 bytes, 480x270). Built with the repo's Makefile on your latest commit
(b9eb949); `make check` passes (exit 0, no compiler warnings, no existing golden or ROM changed).

    arcade1 run tapracer.arc1 --live     # arrows: steer, Z = A, X = B, Enter = START
    git apply tapracer.patch             # programs/tapracer.c, test, two input scripts, golden, ROM; 4 Makefile lines

Play: left/right steers. Tap speed is your throttle: nothing held coasts, hold A or B cruises, tapping fast is top speed.
Fast corners throw you wide (the pull grows with speed), so tap speed trades against control. Alternate A and B to charge
(you slow down), then tap the same button again or press START to boost. Orange road is rough and caps your speed unless
you are boosting. One lap, three rivals.

Architecture: game state -> build_dl() -> vector display list -> vec_submit() -> screen. The 3D projection is fixed point
(Q12 scale, one divide per road boundary, no FPU). vec_submit() is the SOFTWARE FALLBACK (rect runs, 8 brightness levels,
glow as a dim wide pass); a machine with a vector block replaces only that function.

Tests (tests/test_tapracer.sh): determinism; the story (title, countdown, race, charge and boost, top speed, finish);
tier ordering (the idle session is still racing when the tapping one has finished); record/replay; instruction budget.
Both input scripts were generated closed-loop against the ROM (a steering bot), so they are deterministic and stay valid.

Measured: tapping session finishes in 1,505 frames (25 s), rank 1; heaviest sampled frame 192k of the 1,000,000 cap;
display list peak 461 of 900 segments (the machine's 64 KiB of RAM sets the cap, not the format).
Observable globals: mode pz px pv sp rank boost charge tap_speed dl_n tframe fin.
