# Sky Descent for ARCADE-1 (libretro core)

Build:  make                 -> arcade1_skydescent_libretro.so
Test:   make test            (determinism, boss fight, full descent; headless)
Run:    retroarch -L ./arcade1_skydescent_libretro.so

Controls (RetroPad / default RetroArch keyboard)
  D-pad / arrows   move
  B / Z            button A: hold = regular fire, tap fast = fastest, widest gun
  Y / A            button B: alternate A and B quickly = charge (gun stops)
  A / X            button C: release the beam
  Nothing held     slow auto-fire

Files
  shmup.h / shmup.c   game core: integer only, no libm, no malloc, deterministic
  tapcad.h            tap-speed + A/B charge library (same rules as speedpunch.h)
  tables.inc          sine table + 3x5 font (generated)
  libretro.c          thin core wrapper; -DSHMUP_ROTATE asks the frontend to rotate
  shmup_test.c        headless tests; ./shmup_test dump writes frames to /tmp
  smoke_test.c        dlopen harness that runs the built core headless

Notes
  - Output is portrait 240x320 XRGB8888. Use SHMUP_ROTATE for a sideways monitor.
  - Time comes from the frame counter (frame * 50/3 ms), so replays are exact.
    libretro polls input once per frame, so tap timing is 16.7 ms resolution; on the
    real cabinet feed FPGA timestamps to tc_press() instead.
  - Art is still placeholder rectangles except the player ship (built from a string map).
