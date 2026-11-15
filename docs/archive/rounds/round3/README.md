# round3/ — clean-room round-3 evidence archive

This directory is the untouched output of the round-3 fresh-agent exercise
(a context-free agent built `flappy` from the repo documentation alone —
see ../docs/experiments/round3-cleanroom.md for the distilled run report
and findings). It is committed as-is, verbatim, as primary evidence:

- `REPORT.md`      — the agent's own final report, incl. the full friction list
- `flappy.c`       — the game as the agent wrote it (promoted, with a
                     provenance header, as ../programs/flappy.c)
- `input.txt`      — the model-authored 520-frame session
                     (promoted as ../tests/inputs/flappy.txt)
- `flappy.arc1` (+ `.syms`), `flappy.golden.txt` — the agent's build and
                     golden (byte-identical to the promoted repo copies)
- `flappy_model.py`— exact host-side model of the game's integer maths,
                     used as an autopilot to author the input script
- `run_a.txt` / `run_b.txt`, `emu_vars.txt`, `model_trace.txt` — determinism
                     and model-vs-emulator evidence (520/520 frames agree)
- `verify.sh`      — build + determinism + golden in one script
- `mid_flight.png`, `dead.png` — captured frames (flight / game over)
- `session_norm.txt` — the `--record`-normalized session

Nothing here feeds `make check`; it is evidence, kept for the record.
