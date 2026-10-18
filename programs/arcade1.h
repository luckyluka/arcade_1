#ifndef ARCADE1_H
#define ARCADE1_H
#include "gfx.h"

#define BTN_UP 0x1u
#define BTN_DOWN 0x2u
#define BTN_LEFT 0x4u
#define BTN_RIGHT 0x8u
#define BTN_A 0x10u
#define BTN_B 0x20u
#define BTN_START 0x40u
#define BTN_SELECT 0x80u
#define REG_INPUT_PAD (GFX_MMIO_BASE + 0x200u)
static inline unsigned arcade1_pad(void){ return *(volatile unsigned int*)REG_INPUT_PAD; }

static inline void arcade1_clear(unsigned c){ gfx_clear(c); }
static inline void arcade1_rect(int x,int y,int w,int h,unsigned c){ gfx_rect(x,y,w,h,c); }
/* Every command — including PRESENT — goes out through gfx_cmd() (gfx.h),
 * the single barriered writer of GFX_REG_CMD; see "Compiler barrier" there. */
static inline void arcade1_present(void){ gfx_present(); }

static inline void arcade1_pal(int i,unsigned rgb){ GFX_REG_PAL_INDEX=i; GFX_REG_PAL_DATA=rgb; }
static inline void arcade1_cam(int x,int y){ GFX_REG_CAM_X=x; GFX_REG_CAM_Y=y; }
static inline void arcade1_flags(unsigned f){ GFX_REG_FLAGS=f; }

/* --- Drawing helpers -------------------------------------------------------
 * Thin wrappers over the gfx.h commands — they only name the parameters and
 * issue the registers + command, adding no behaviour of their own. The full
 * contracts (clipping, transparency, ignore rules) are documented on the
 * gfx_* functions in gfx.h. Units, stated once:
 *   - sprite/tilemap x/y are WORLD pixels (the device subtracts the camera);
 *   - img/cells/tiles are byte addresses and may point into ROM or RAM;
 *   - tilemap cells are 32-bit words (see the cell layouts in gfx.h);
 *   - 4-bit images pack TWO pixels per byte, the LEFT pixel in the HIGH
 *     nibble, rows of tile_w/2 bytes (tile_w even). */

/* 1-bit sprite: h rows of (w+7)/8 bytes, MSB = leftmost pixel, set bits are
 * drawn in `color`, clear bits transparent. */
static inline void arcade1_sprite(int x,int y,const void *img,int w,int h,unsigned color){ gfx_sprite(x,y,img,w,h,color); }

/* 4-bit sprite from palette bank `bank` (0..15); flags: bit0 = flip X,
 * bit1 = flip Y. Pixel value 0 is transparent, v draws PALETTE[bank*16+v]. */
static inline void arcade1_sprite4(int x,int y,const void *img,int w,int h,unsigned bank,unsigned flags){ gfx_sprite4(x,y,img,w,h,bank,flags); }

/* 1-bit tilemap: cols*rows 32-bit cells; cell [15:0] = tile index (0 = empty,
 * background shows through), cell [31:16] = RGB565 tint for set bits. Tile
 * N (>=1) lives at tiles + (N-1)*tile_h*((tile_w+7)/8). */
static inline void arcade1_tilemap(int x,int y,const void *cells,const void *tiles,int cols,int rows,int tile_w,int tile_h){ gfx_tilemap(x,y,cells,tiles,cols,rows,tile_w,tile_h); }

/* 4-bit tilemap: cols*rows 32-bit cells; cell [15:0] = tile index (0 = empty),
 * [19:16] = palette bank, [20] = flip X, [21] = flip Y (the global PAL_BANK/
 * FLAGS registers are ignored — every cell carries its own). Tile N (>=1)
 * lives at tiles + (N-1)*(tile_w/2*tile_h). */
static inline void arcade1_tilemap4(int x,int y,const void *cells,const void *tiles,int cols,int rows,int tile_w,int tile_h){ gfx_tilemap4(x,y,cells,tiles,cols,rows,tile_w,tile_h); }

static unsigned arcade1_rng_state=12345;
static inline unsigned arcade1_rng(void){ arcade1_rng_state=arcade1_rng_state*1664525u+1013904223u; return arcade1_rng_state; }
static inline int arcade1_fp_mul(int a,int b){ return (int)((long long)a*b>>16); }

#endif
