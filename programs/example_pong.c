#include "arcade1.h"
int main(void){
  int bx=240,by=135,dx=4,dy=3;
  int py=100, oy=100;
  int ps=0, os=0, state=0; /* 0 play,1 gameover */
  while(1){
    unsigned p=arcade1_pad();
    if(state==0){
      if(p&BTN_UP) py-=4; if(p&BTN_DOWN) py+=4;
      if(py<0)py=0; if(py>230)py=230;
      /* opponent AI */
      if(oy+20 < by) oy+=3; else if(oy+20 > by) oy-=3;
      if(oy<0)oy=0; if(oy>230)oy=230;
      bx+=dx; by+=dy;
      if(by<0||by>262) dy=-dy;
      if(bx<20 && by>=py && by<=py+40){ dx=-dx; }
      if(bx>452 && by>=oy && by<=oy+40){ dx=-dx; }
      if(bx<0){ os++; bx=240;by=135; dx=4; }
      if(bx>480){ ps++; bx=240;by=135; dx=-4; }
      if(ps>=5||os>=5) state=1;
    } else {
      if(p&BTN_START){ ps=0;os=0;state=0; bx=240;by=135; }
    }
    arcade1_clear(0x0000);
    arcade1_rect(10,py,6,40,0xFFFF);
    arcade1_rect(464,oy,6,40,0xFFFF);
    arcade1_rect(bx,by,8,8,0xF800);
    arcade1_present();
  }
  return 0;
}
