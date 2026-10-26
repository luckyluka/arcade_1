# Neon Tap for ARCADE-1

`neontap.arc1` is a ready-to-run ROM (13,420 bytes, 480x270, 60 fps). Built with the repo's own
Makefile; `make check` passes with it (exit 0, no compiler warnings, existing goldens and ROMs untouched).

    arcade1 run neontap.arc1 --live          # play (arrows/WASD, Z = A, X = B, Enter = START, Enter = START = bomb)

Controls: D-pad moves AND aims (you fire the way you last moved). A / B fire. START = bomb (during play).
  nothing held = slow auto-fire, hold A or B = regular fire, tap one button fast = fastest, widest fire,
  alternate A and B = charge (gun stops), START spends the charge as a shockwave.
The third button is `BOMB_BTN` at the top of neontap.c (BTN_START now, because the live window has no SELECT key; BTN_SELECT works in RetroArch).

Add it to the repo (new files only; no golden or ROM is changed):

    git apply neontap.patch        # Makefile (4 lines), programs/neontap.c + _sin.inc, tests, golden, ROM

Observable globals: state score lives mult kills tap_speed charge n_enemies secs.
Measured: heaviest sampled frame 282k of the 1,000,000 instruction cap.
