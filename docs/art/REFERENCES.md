# Art references — provenance

Every image the repository does **not** ship, and where it went.

## The round-5 "Firefly" reference sheets

Four JPEG sheets were used as *style reference* for Petal Blade's hero
(the samurai walk cycle). They are **not** in the repository any more: an
image model's output has an unclear licence, and whether such files may
be published is the owner's decision (ROADMAP_V4 §7, decision 3 — still
open). They were moved out of the working tree on 2026-10-20; they remain
in the git history (commit `711a2e4` "round5-game" and its ancestors).

| File | Size | What it is |
|---|---|---|
| `Firefly_…50X50 PIXELS.game art. 384384.jpg` | 783 KB | walk-cycle sheet, frames 1-4 |
| `Firefly_…50X50 PIXELS.game art. 394298.jpg` | 919 KB | walk-cycle sheet, frames 5-8 |
| `Firefly_…50X50 PIXELS.game art. 779634.jpg` | 882 KB | walk-cycle sheet, frames 9-12 |
| `Firefly_…50X50 PIXELS.game art. 803714.jpg` | 852 KB | walk-cycle sheet, frames 13-16 |

**Provenance as far as it is known**

* **Tool**: an image-generation model (the file names carry the prompt
  stem "Use the picture as template to create a spritesheet. Walking.
  50X50 PIXELS.game art" plus a numeric id). The exact model and version
  were not recorded — that is the gap this file exists to close.
* **Date**: 2026-10-20 (the day the round-5 art was produced; the files
  carry no embedded timestamp).
* **Prompt**: as quoted in the file names above.
* **Licence**: unknown — generated output with no stated terms. This is
  why the files are not published with the repository.
* **Use**: style guidance only. Nothing in the repository is derived from
  these pixels: every sprite is generated procedurally by
  `programs/petalblade_art/gen_art.py` (stdlib only, deterministic), and
  the sheets were never ingested (they have more than 15 colours and are
  not strip-shaped, so they could not have been used as-is anyway).

**Where they are now**: outside the working tree, in a sibling directory
next to the repository. The `.gitignore` rules keep them out.

**Open decision** (ROADMAP_V4 §7, decision 3): keep public, keep local
only, or purge from history. The agent does not decide this.

## Rule for the future

Any image that comes from outside the repository — an image model, a
scanned sheet, a photo — is a *reference*, not an asset:

1. keep it out of the repository until its licence is known;
2. record it here: tool, model/version, prompt, date, licence, and what
   it was used for;
3. build the shipped art from code (the generators), so the repository
   stays reproducible and the assets are auditable.

`docs/art/PROVENANCE.md` (ROADMAP_V4 card 7.1) will extend this to the
assets the repository *does* ship.
