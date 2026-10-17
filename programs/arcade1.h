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
static inline void arcade1_present(void){ GFX_REG_CMD=GFX_CMD_PRESENT; }

static inline void arcade1_pal(int i,unsigned rgb){ GFX_REG_PAL_INDEX=i; GFX_REG_PAL_DATA=rgb; }
static inline void arcade1_cam(int x,int y){ GFX_REG_CAM_X=x; GFX_REG_CAM_Y=y; }
static inline void arcade1_flags(unsigned f){ GFX_REG_FLAGS=f; }

static unsigned arcade1_rng_state=12345;
static inline unsigned arcade1_rng(void){ arcade1_rng_state=arcade1_rng_state*1664525u+1013904223u; return arcade1_rng_state; }
static inline int arcade1_fp_mul(int a,int b){ return (int)((long long)a*b>>16); }

#endif
