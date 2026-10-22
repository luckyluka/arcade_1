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

## Observation not fixed

The horizontal grid runs `HUD_H + i * 20` for twelve lines: y = 20, 40,
..., 240. A line at **y = 260 is on screen but never drawn**, so the
bottom band is 29 px tall while every other band is 20 px. This is a
*visual design* question, not a stale-pixel defect (the lines are drawn
where they are drawn, every frame, on purpose), so it was left alone
rather than silently changing the art — and it would move the golden a
second time for a purely cosmetic reason. Flagged for an owner decision.
