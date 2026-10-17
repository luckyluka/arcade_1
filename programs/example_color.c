#include "arcade1.h"
int main(void){
  /* palette */
  for(int i=0;i<16;i++) arcade1_pal(i, 0x0000 + i*0x0841);
  int t=0;
  while(1){
    arcade1_clear(0x001F);
    arcade1_cam(t%100, 0);
    /* a 4x4 tilemap grid at 0 */
    static unsigned char tiles[16]={0,1,2,3,1,2,3,0,2,3,0,1,3,0,1,2};
    GFX_REG_MAP_ADDR = (unsigned int)tiles; /* NOTE: RAM address; ok */
    GFX_REG_TILE_ADDR = 0;
    GFX_REG_COLS = 4; GFX_REG_ROWS = 4;
    GFX_REG_CMD = GFX_CMD_TILEMAP4;
    /* sprite4 */
    static unsigned char spr[8]={0xF0,0xF0,0x0F,0x0F,0xF0,0xF0,0x0F,0x0F};
    GFX_REG_ADDR=(unsigned int)spr;
    GFX_REG_X=100; GFX_REG_Y=100; GFX_REG_W=4; GFX_REG_H=4;
    GFX_REG_PAL_BANK=0; GFX_REG_FLAGS=0;
    GFX_REG_CMD=GFX_CMD_SPRITE4;
    arcade1_present();
    t++;
  }
  return 0;
}
