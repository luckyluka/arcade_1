#include "gfx.h"
#define REG_INPUT_PAD (GFX_MMIO_BASE + 0x200u)
#define R(a) (*(volatile unsigned int*)(a))
int main(void){
  int sx=5,sy=5,dx=1,dy=0; int tx=20,ty=10; int score=0;
  while(1){
    unsigned p=R(REG_INPUT_PAD);
    if(p&0x1){dx=0;dy=-1;} if(p&0x2){dx=0;dy=1;} if(p&0x4){dx=-1;dy=0;} if(p&0x8){dx=1;dy=0;}
    sx+=dx; sy+=dy;
    if(sx<0)sx=0; if(sx>47)sx=47; if(sy<0)sy=0; if(sy>26)sy=26;
    if(sx==tx && sy==ty){ score++; tx=(score*7)%48; ty=(score*13)%27; }
    gfx_clear(0x0000);
    gfx_rect(tx*10,ty*10,10,10,0x07E0);
    gfx_rect(sx*10,sy*10,10,10,0xFFFF);
    GFX_REG_CMD=GFX_CMD_PRESENT;
  }
  return 0;
}
