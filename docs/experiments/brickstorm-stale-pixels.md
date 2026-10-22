# BRICKSTORM: pixels that outlived what drew them

Date: 2026-10-22. Found while reviewing Game #5 (BRICKSTORM) after its
test passed. Two defects, both of the same family: **a pixel that was
drawn once and never repainted.**

Neither was a machine bug — `arcade1 inspect --at X,Y` correctly
reported both, which is how they were found.

## 1. The bottom five rows never got the gradient

`draw_background()` painted twelve gradient bands of a 22 px pitch at
23 px tall:

```c
for (i = 0; i < 12; i++)
    arcade1_rect(0, i * 22, SCREEN_W, 23, c);   /* y = 0,22,...,242 -> ends at 265 */
```

`SCREEN_H` is **270**. The bands stop at y = 265, so rows **265..269**
received no gradient at all. Since the background is repainted every
frame, those five rows kept whatever first landed in them:

| frame | stray pixels in rows 265..269 |
|---|---|
| 5 (boot) | none yet — just a black strip with the grid's verticals poking into it |
| 200 | 124 |
| 400 | 160 |
| 900 | 191 |
| 1500 | 191, byte-identical to frame 900 |

The count only ever grows, and it stops growing the moment nothing
falls: rows 900 and 1500 had the same SHA-1 (`7eb3e4bf88c94cd1`)
because the game-over holds and no new sprite reaches the bottom edge.

**The mechanism.** A power-up drop is killed only when `d->y > SCREEN_H`
(line ~1451), so it is still *drawn* on the frame it crosses rows
265..269 — and nothing ever repaints the rows it touched. The drop's
blue capsule was therefore scarred at its x position **for the rest of
the run**. The scar's x range grew 61..115 -> 61..180 -> 61..405 as
bricks broke across the field, which is exactly where drops fall.

Proof that the pixels were stale rather than live:

```
$ arcade1 inspect tests/roms/brickstorm.arc1 --frames 200 ... --at 50,266
AT 50,266 id=0 AT 50,266 pixel never written in the recorded frame
```

`id=0` — nothing wrote that pixel this frame, yet it was not
background. It had been drawn in an earlier frame and nothing had
repainted it since.

**Fix.** The last band runs to the bottom edge (`SCREEN_H - i * 22`);
rows 0..264 are byte-identical to before.

## 2. The dead ball's trail outlived the ball

`trail_n` is cleared in exactly two places: `serve_ball()` and
`paddle_hit()`. **Never when the last ball dies.** So after the final
ball fell out of play the trail kept being drawn at the positions it
died at — for the whole game-over screen and the title after it:

```
frame 880..1520   var state = 5  var balls = 0  var ball_id = -1
frame 900         AT 401,268 id=84 CMD 84 FILL x=401 y=268 w=4 h=4 px=8
```

A 4x4 blob of the ball's ghost at the bottom edge, `balls == 0` the
entire time. Before the gradient fix this was **indistinguishable from
the scars** — both lived in rows 265..269 — which is why it surfaced
only once the seam was fixed and the 8 leftover pixels stood out.

**Fix.** The trail is drawn only while `balls > 0`.

## Verification

Against the committed pre-fix ROM, over the full 2800-frame session:

* **game state identical** — the `score/state/level/lives/boss_hp`
  trace is unchanged on every frame (drawing only; no logic moved);
* **112 frames sampled at a 25-frame stride: zero pixel differences
  above row 264.** Every difference is inside rows 265..269, i.e. the
  gradient now filling them;
* stray pixels in rows 265..269: frame 200 `124 -> 0`, frame 900
  `191 -> 0` (the 8 ghost-trail pixels went with the guard);
* the guard was live on 62 of the 112 sampled frames (`balls == 0`);
* `make test-brickstorm` green against the re-recorded golden.

The golden and the ROM snapshot for this game changed **because the
game's rendering changed** — deliberately, in the commit that says so.
No other game's golden or ROM moved.

## 3. The background was 87-99% of the game's pixel work

Both defects above were *caused* by the same decision: the static
background is repainted **every frame**, in full, behind movers that
touch a few hundred pixels each. Measured on the committed ROM:

| frame | base `FILL` px | base total px | background's share | after | speedup |
|---|---|---|---|---|---|
| 60 | 153 428 | 176 563 | 87% | 33 659 | 5.2x |
| 176 | 153 312 | 176 022 | 87% | 66 928 | 2.6x |
| 900 | 176 280 | 198 452 | 89% | 88 010 | 2.3x |
| 1900 | 153 348 | 154 589 | **99%** | 11 591 | **13.3x** |
| 2400 | 176 500 | 177 839 | 99% | 34 993 | 5.1x |

The framebuffer survives between frames, so the background is now
painted **once**, and each frame restores only the rectangles its movers
and overlays covered and then left.

**The invariant.** A frame must be pixel-identical to what a full
repaint would produce. It is, by construction: restore first, then draw
in a fixed order — `restore -> bricks -> boss -> fx -> balls -> paddle
-> hud`. Every patch is a rectangle of the *static* background, so
patches may overlap freely (repainting it is idempotent); what makes the
result correct is that anything a patch cut through — a brick, another
mover — is redrawn *after* it, and the HUD fills its own strip
unconditionally every frame so it never needs restoring at all.

Two facts from SPEC.md do the heavy lifting:

* `CLEAR`, `SET_PIXEL` and `FILL` are **screen-space** ("never scrolled
  — they are the HUD"), so the background's patches ignore the camera
  and go in exactly as issued;
* `SPRITE` is **world-space** (`X − CAM_X`), so a sprite's recorded rect
  is its position minus this frame's camera.

### The trap: world-space sprites that are not recorded

Bricks are drawn with `SPRITE4` — world space — but they are *not*
recorded every frame. There are up to 120 of them, and recording the lot
would cost more than the bug it prevents.

That is fine right up until the camera moves, because the background
does **not** move with it. On the first frame with a screen shake, the
bricks slide up to two pixels against the gradient and leave their
previous outline behind — a 1-2 px ring on every live cell:

```
frame 174, cell (0,0) = x20..63, y32..45, diff vs. full repaint
 y32 XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX...
 y33 XX.........................................X
 y44 ............................................
 y45 .XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX...
```

Note what is *not* there: the interior is identical (brick sprites are
uniform in the middle, so a 1 px shift changes only the border), and the
**dead** cell is identical too — background cannot leave a ghost, which
is why the artifact was invisible until a brick broke and the shake
began.

So on the frames where the camera actually moved, the background is put
back under every live brick where it was drawn *last* frame — computed
from the previous camera, not stored. A brick killed this frame is
covered by `break_brick()`'s own record, which is taken under last
frame's camera because `update()` runs before the camera is set for the
new frame.

Two details that are easy to get wrong and are load-bearing:

* `break_brick()` runs in `update()`, i.e. *before* this frame's camera
  is set — so its rect must use the camera the brick was last **drawn**
  with, which is the *previous* frame's;
* a brick's white hit-flash is a `FILL`, hence screen-space: it is drawn
  at the brick's world `x,y` and **does not shake**. Recording it with a
  camera adjustment misplaces it by exactly the shake amount. It is
  recorded as issued, and it must be recorded at all — the flash is
  drawn *under* the sprite, so the sprite's transparent pixels show it,
  and when the flash expires those pixels have to become background
  again.

### Verification

Against the pre-Card-C ROM, over the full 2800-frame session:

* **`fb=` hashes identical on 2800 of 2800 frames** — the dirty renderer
  reproduces the full repaint exactly, on every frame, not on a sample;
* the game-state trace (`score/state/level/lives/boss_hp`) unchanged;
* command count at frame 174: `128 -> 94` (`FILL 40 -> 6`);
* RAM `4360 -> 6912` bss (the 2560-byte rect list; 10.5% of 64 K), ROM
  payload `33512 -> 34580` bytes;
* the rect list overflows (`DIRTY_MAX` 160) only by falling back to one
  full repaint — correct by construction, and no frame in the scripted
  session needs it.

The `hash=` column differs on all 2800 frames, as expected: the
instruction count and addresses moved. Only `fb=` is an oracle of
pixels.

## Observations not fixed

**The horizontal grid runs `HUD_H + i * 20` for twelve lines: y = 20,
40, ..., 240. A line at y = 260 is on screen but never drawn**, so the
bottom band is 29 px tall while every other band is 20 px. This is a
*visual design* question, not a stale-pixel defect (the lines are drawn
where they are drawn, every frame, on purpose), so it was left alone
rather than silently changing the art — and it would move the golden a
second time for a purely cosmetic reason. Flagged for an owner
decision.

**The screen shake never ends.** `shake` is decremented only at the very
end of `update()`, but `update()` returns early for `ST_DEAD` (line
1335), `ST_OVER`/`ST_WIN` (1321), `ST_TITLE` (1310) and `ST_CLEAR`
(1356). So the sequence on losing a life is:

```
frame 170   var state = 2 (ST_PLAY)  var shake = 0
frame 174   var state = 3 (ST_DEAD)  var shake = 11   <- shake = 12, decremented once
frame 180   var state = 3 (ST_DEAD)  var shake = 11   <- frozen
frame 900   var state = 5 (ST_OVER)  var shake = 11   <- still frozen
frame 1500  var state = 5 (ST_OVER)  var shake = 11
```

The camera is therefore re-randomised **every frame for the rest of the
game-over screen**, jittering the whole world against the background
that cannot move with it. Pre-existing, and not a stale-pixel defect —
it is a shake that was meant to last ~12 frames and lasts forever. It is
recorded here rather than fixed because fixing it is a *behaviour*
change, not a rendering one, and it would move the golden for reasons
that have nothing to do with this card. It is also, incidentally, why
the game-over frames above are the slowest of the new renderer: the
camera moves every one of them, so the brick patch fires every one of
them. Flagged for an owner decision, alongside the grid observation
above — same reason: it is a behaviour change, not a rendering one, so
it belongs to whoever owns the game's feel rather than to a rendering
card.
