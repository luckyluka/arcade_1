# Lumen for ARCADE-1

`lumen.arc1` is a ready-to-run ROM (13,476 bytes, 480x270). Built with the repo's Makefile on top of your
latest commit (b9eb949); `make check` passes (exit 0, no compiler warnings, no existing golden or ROM changed).

    arcade1 run lumen.arc1 --live      # arrows/WASD, Z = A, X = B, Enter = START
    git apply lumen.patch              # adds programs/lumen.c, test, input script, golden, ROM; 4 Makefile lines

Play: D-pad moves and aims. Your tap speed is your light.
  nothing held = dim, no sword   hold A or B = slow swings, brighter   tap fast = flurry, wide light, slow enemies
  alternate A and B = charge light (no swinging); tap the same button again, or START = flash + spin attack
  Creepers are fast in the dark, slow in your light. Turrets only shoot at what is bright.
  The key is far from the door; taking it sets off an alarm.

Architecture: game state -> build_dl() -> vector display list -> vec_submit() -> screen.
`vec_submit()` is the SOFTWARE FALLBACK (rect runs, 8 brightness levels, glow as a dim wide pass). A machine with a vector
block replaces only that function. Display list format: arc1_vector_displaylist_spec.md.

Measured: heaviest sampled frame 137k of the 1,000,000 instruction cap; display list peak 292 of 900 segments.
Observable globals: state room hp score kills haskey tap_speed charge lr n_enemies dl_n hero_x hero_y.
