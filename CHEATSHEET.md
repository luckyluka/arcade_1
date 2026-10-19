# ARCADE-1 Cheat Sheet — guest API + tools

Memory map: ROM 256KiB at 0x00000000 (code + const data), RAM 64KiB at
0x20000000 (data/bss/stack), MMIO 4KiB at 0x40000000. Images and cell
maps must point into ROM or RAM.

## Input
Pad register 0x40000200: `unsigned p = arcade1_pad();` Bits: up 0x1,
down 0x2, left 0x4, right 0x8, a 0x10, b 0x20, start 0x40, select 0x80.

## Frame protocol
One loop iteration = one 60 fps frame: clear, draw, `arcade1_present()`. The machine steps the guest one frame per host frame.

**A frame ends when the guest writes the PRESENT command** — nothing else
ends it. The machine runs the guest until that write, or until the
per-frame instruction budget runs out. A loop without `arcade1_present()`
burns the whole budget in the first frame: nothing moves, nothing errors,
the game just looks frozen. Put the present at the end of every frame,
inside the loop:

```c
#include "arcade1.h"

#define BLACK 0u

static void draw_stuff(void) { arcade1_rect(0, 0, 8, 8, 0xFFFFu); }

int main(void)
{
    for (;;) {                  /* one iteration == one 60 fps frame */
        arcade1_clear(BLACK);
        draw_stuff();
        arcade1_present();      /* this write ENDS the frame */
    }
}
```

## Commands (GFX_REG_CMD values)
clear 1, set_pixel 2, present 3, fill 4, sprite 5, tilemap 6, sprite4 7, tilemap4 8.
Everything clips to the 480x270 screen; w/h <= 0, zero sizes or a zero tile index draw nothing.
The screen is always 480x270 — there is no scaling or video-mode register.
For a larger image upscale a capture: `--capture f.png --scale 4` gives 1920x1080.

## The device reads RAM: SDK helpers are barriers
Sprite images, tilemap cells and tile data live in guest RAM, and the device
reads them through the address it is given as an *integer* (ADDR / MAP_ADDR /
TILE_ADDR) — an access the compiler cannot see. So the stores that fill those
tables are only safe if nothing may sink or delete them past the draw.

* **SDK helpers are barriers**: every command is issued by `gfx_cmd()`
  (gfx.h), which brackets the `GFX_REG_CMD` write with `GFX_BARRIER()`. Call
  `arcade1_*` / `gfx_*` as documented and the data you filled is complete
  before the device draws it.
* **When poking registers directly, add the barrier** yourself: one
  `GFX_BARRIER()` before the register writes and one around the CMD write
  (compiled example below).
* `GFX_BARRIER()` is compile-time only — no instruction, no timing, no
  behaviour change; it only stops the compiler reordering/sinking stores.

## Coordinate spaces
clear/rect/set_pixel are screen-space (never scrolled — HUD). sprite and
tilemap are world-space: drawn at (x - cam_x, y - cam_y). Keep objects at
fixed world coordinates, move only the camera (`gfx_camera(x, y)`); the
power-on camera is (0,0), so early draws are unaffected.

## Palette (256 RGB565 entries)
`gfx_pal_upload(start, data, n)` writes n entries from `start`, auto-incrementing.
`gfx_pal_bank(b)` sets the bank (0..15) used by sprite4: pixel v draws PALETTE[bank*16 + v].

## Sprites, 1-bit
`gfx_sprite(x, y, img, w, h, color)`: img is h rows of (w+7)/8 bytes,
MSB = leftmost pixel; set bits draw `color`, clear bits are transparent.
`gfx_flip(flags)` (bit0 = X, bit1 = Y) is this command's global flip.

## Sprites, 4-bit
`gfx_sprite4(x, y, img, w, h, bank, flags)`: img is h rows of w/2 bytes
(w even), TWO pixels per byte, the LEFT pixel in the HIGH nibble. 0 is
transparent; v draws PALETTE[bank*16+v]. flags: bit0 = flip X, bit1 = Y.

## Tilemaps, 1-bit
`gfx_tilemap(x, y, cells, tiles, cols, rows, tile_w, tile_h)`: cells is
cols*rows 32-bit words; [15:0] tile index (0 = empty, background shows
through), [31:16] RGB565 tint for set bits. Tile N (>= 1) is at tiles +
(N-1) * tile_h * ((tile_w+7)/8); cell (col, row) lands at (x + col*tile_w,
y + row*tile_h), clipped like a sprite.

## Tilemaps, 4-bit
`gfx_tilemap4(...)`: same shape; cells are [15:0] index, [19:16] bank,
[20] flip X, [21] flip Y — the global PAL_BANK/FLAGS are ignored, every
cell carries its own. Tile N is at tiles + (N-1) * (tile_w/2 * tile_h);
tile_w/tile_h are written to REG_W/REG_H by the command.

## Observable state must be a global
Anything a test or tool should read — score, lives, state, length,
camera position — must be a file-scope global: the tools read those out
of guest RAM by name; locals are invisible to them.

## arcade1 helpers (programs/arcade1.h)
`#include "arcade1.h"` (pulls in gfx.h + the memory map): arcade1_pad,
clear, rect, present, pal, cam, flags, sprite, sprite4, tilemap,
tilemap4, rng, fp_mul, pixel, rand — thin wrappers over the gfx.h
commands. `arcade1_pixel(x, y, color)` plots one screen-space pixel (never
scrolled, HUD-safe); `arcade1_rand(n)` is `[0, n)` from the LCG's *high*
bits — its low bits are weak, and each call consumes one `arcade1_rng()`
step, so re-balancing re-routes the stream for every later caller. The
5x7 text font is opt-in: `#include "text.h"` for `text_draw`/`text_u32`
(a game that does not include it pays no ROM). Build a
guest: add it to programs/ + GUEST_PROGS in the Makefile, `make program`
→ programs/build/<name>.arc1 (needs riscv64-unknown-elf-gcc on PATH).

## The .arc1 image
A 16-byte little-endian header, then the flat guest binary (the bytes
`objcopy -O binary` produced). This is what `arcade1 build` writes:

    offset 0x00  char[4]  magic "ARC1"
    offset 0x04  u32      format version = 1
    offset 0x08  u32      payload size in bytes (1..262144 = ROM size)
    offset 0x0C  u32      CRC-32 of the payload (standard zlib CRC-32)
    offset 0x10  payload  flat RV32IM .bin, loaded at 0x00000000

Every load validates magic, version, size (it must equal the file length)
and CRC before the payload is mapped. `arcade1 run` also accepts a raw
.bin with no header. The writer and the parser share one implementation
(`src/arcade1/rom.c`) and are used by the CLI, the tests and the repo's
`build/packarc1` helper alike, so they cannot drift apart.

## CLI (arcade1)
    arcade1 run     <game.arc1|bin> [options]   headless run (options below)
    arcade1 capture <game.arc1|bin> [options]   run + dump frames; creates and
                                                uses captures/ by default
                                                (--capture-dir DIR, or a single
                                                final frame via --capture FILE)
    arcade1 test    <game.arc1|bin> [--golden FILE] [options]
                                                smoke run; --golden compares the
                                                `frame N hash=.. fb=..` lines
                                                against FILE (a golden's first
                                                pass) and fails on a difference
    arcade1 build   <program.c> [output.arc1]   compile a guest, self-contained:
                                                crt0, the linker script and the
                                                headers travel inside the CLI
                                                and the .arc1 is packed in
                                                process. Needs only
                                                riscv64-unknown-elf-gcc on PATH
                                                and runs in any directory.
    arcade1 inspect <game.arc1|bin> [options]   same as `run --inspect`

    options:
      --frames N / --input FILE / --record FILE / --capture FILE
      --capture-dir DIR / --capture-every K / --syms FILE
      --vars LIST / --var-every K / --quiet
      --inspect    print the final frame, pc, instruction count and all
                   32 registers (the `inspect` command is this flag)
      --strict     exit non-zero if any draw read a source address outside
                   ROM/RAM — those draws silently paint nothing. Without
                   --strict the run still prints one warning line on stderr
                   (stdout stays machine-readable).
Exit codes: 0 ok, 1 runtime error (or --strict tripped), 2 usage error.
Input script: `<first> <last> <buttons...>` per line, inclusive frames,
later lines win, `none` holds nothing, `#` comments. A recording feeds
straight back through --input.

## Tests
`make check`: unit tests, golden regression (tests/golden), CLI tests
(test-run) and example-game tests (pong, snake, color, cheatsheet).


## API examples (each block is compiled by `make test-cheatsheet`)
```c
#include "arcade1.h"
void ex_clear_rect_present(void) {
    arcade1_clear(0x0000);                  /* RGB565 */
    arcade1_rect(10, 10, 20, 20, 0xF800);   /* x, y, w, h, color */
    arcade1_present();
}
```
```c
#include "arcade1.h"
static const unsigned char ring[8] = {
    0x3C,0x42,0x81,0x81,0x81,0x81,0x42,0x3C };
void ex_sprite(void) {   /* 8x8 1-bit ring, set bits white */
    arcade1_sprite(100, 100, ring, 8, 8, 0xFFFF);
}
```
```c
#include "arcade1.h"
static const unsigned char img4[32] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
    0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
    0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00 };
void ex_sprite4(void) {  /* 8x8 4-bit, bank 1, flip X */
    arcade1_sprite4(100, 100, img4, 8, 8, 1, 1);
}
```
```c
#include "arcade1.h"
void ex_tilemaps(void) { /* 2x2 cells of 8x8 tiles, both formats */
    static unsigned int cells[4] = { 1, 1, 1, 1 };
    static const unsigned char tile1[8] = {
        0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xFF };
    static const unsigned char tile4[32] = {
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00 };
    arcade1_tilemap(0, 0, cells, tile1, 2, 2, 8, 8);
    arcade1_tilemap4(0, 0, cells, tile4, 2, 2, 8, 8);
}
```
```c
#include "arcade1.h"
/* Poking the video registers yourself (instead of an SDK helper): the device
 * reads the RAM tables through their addresses, so bracket the CMD write with
 * GFX_BARRIER() exactly as gfx_cmd() does. */
void ex_barrier_poke(unsigned int *cells, unsigned int *tiles) {
    GFX_BARRIER();
    GFX_REG_X         = 0;
    GFX_REG_Y         = 20;
    GFX_REG_MAP_ADDR  = (unsigned int)cells;
    GFX_REG_TILE_ADDR = (unsigned int)tiles;
    GFX_REG_COLS      = 4;
    GFX_REG_ROWS      = 3;
    GFX_REG_W         = 16;
    GFX_REG_H         = 8;
    GFX_BARRIER();
    GFX_REG_CMD       = GFX_CMD_TILEMAP4;
    GFX_BARRIER();
}
```
```c
#include "arcade1.h"
#include "text.h"                /* opt-in 5x7 font: text_draw / text_u32 */
void ex_text_hud(int score) {
    arcade1_cam(0, 0);           /* SPRITE is world space: pin for a HUD */
    arcade1_clear(0x0000);
    text_draw(8, 8, "SCORE", 0xFFFF);
    text_u32(8, 16, (unsigned)score, 4, 0xFFFF);   /* zero-padded field */
    arcade1_pixel(0, 269, 0xF800);                 /* one red pixel */
    (void)arcade1_rand(3);                         /* [0, 3), high bits */
    arcade1_present();
}
```
