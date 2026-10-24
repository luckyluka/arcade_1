# ARCADE-1 — the golden ROMs have drifted: `make roms-update` is not a no-op

**Found during the skydescent card** (game #6, port of the owner's
`shmup/` prototype). A card that only wants to *add* a new game runs
`make roms-update` to snapshot its own image — and ten of the fifteen
committed ROM/syms pairs come back modified. Rule 4 ("existing goldens
and ROMs are untouchable") turns a one-line snapshot step into a
revert ritual. This note records what drifted, why, and what a future
card that legitimately wants to refresh all of them must do.

## What happens

```
$ make roms-update
$ git status --porcelain tests/roms tests/golden
 M tests/roms/breakout.arc1        M tests/roms/breakout.syms
 M tests/roms/color_demo.arc1      M tests/roms/color_demo.syms
 M tests/roms/example_color.arc1   M tests/roms/example_color.syms
 M tests/roms/example_pong.arc1    M tests/roms/example_pong.syms
 M tests/roms/example_snake.arc1   M tests/roms/example_snake.syms
 M tests/roms/m29game.arc1         M tests/roms/m29game.syms
 M tests/roms/platformer.arc1      M tests/roms/platformer.syms
 M tests/roms/scroll_demo.arc1     M tests/roms/scroll_demo.syms
 M tests/roms/shmup.arc1           M tests/roms/shmup.syms
 M tests/roms/tetris.arc1          M tests/roms/tetris.syms
?? tests/roms/<the new game>
```

Four images are byte-identical to the committed ones — **flappy,
petalblade, petalblade_rig, brickstorm** — and ten are not. The size
change is structural, not noise: `tetris.arc1` grows 3192 -> 3264 B.

## Why

The ten stale images were last snapshotted at `3551898` ("behaviour
change: memory map v0.2"). Two later commits changed the **shared
guest runtime** every guest links against:

| commit | what it changed |
|---|---|
| `10c6550` | `memcpy`/`memset`/`memmove` added to the guest runtime |
| `cf32e6d` | crt0 alignment fix so `memcpy` cannot jump into rodata |

A fresh build therefore differs from those ten snapshots by the size of
the runtime plus the crt0 change. The four byte-identical images were
re-snapshotted *after* both commits (petalblade at `86a20d6`), which is
exactly the split the table above shows.

## What it means

* The committed regression is **self-consistent**: `test-golden` runs
  the *committed* `tests/roms` images and matches the *committed*
  goldens. The stale images simply do not contain the later guest
  runtime — they still pass because the runtime addition does not
  change their observable behaviour.
* `make roms-update` + `make golden-update` for the ten stale games is
  pending housekeeping. Doing it **rewrites ten ROMs and ten goldens in
  one commit** — it belongs in its own card ("refresh all golden
  snapshots"), never piggy-backed on a tooling or game card.

## The rule this card followed

> Existing goldens and ROMs are untouchable; a new game adds only its
> own files.

So the skydescent card ran `make roms-update`, kept only
`tests/roms/skydescent.{arc1,syms}`, and reverted the ten rewritten
pairs with `git checkout --`. If a future card *is* the refresh card,
the recipe is `make roms-update && make golden-update` followed by
`make clean && make check` (every golden is re-recorded, so a failure
there is a real behaviour change to explain, not snapshot noise).
