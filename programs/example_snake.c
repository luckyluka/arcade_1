#include "arcade1.h"
#define GW 48
#define GH 27
int main(void){
  int sx[256],sy[256],len=3;
  int dx=1,dy=0; int fx=10,fy=5; int score=0; int state=0; int frame=0;
  sx[0]=5;sy[0]=5; sx[1]=4;sy[1]=5; sx[2]=3;sy[2]=5;
  while(1){
    unsigned p=arcade1_pad();
    if(state==0){
      if(p&BTN_UP && dy==0){dx=0;dy=-1;} if(p&BTN_DOWN && dy==0){dx=0;dy=1;}
      if(p&BTN_LEFT && dx==0){dx=-1;dy=0;} if(p&BTN_RIGHT && dx==0){dx=1;dy=0;}
      /* move body */
      for(int i=len-1;i>0;i--){sx[i]=sx[i-1];sy[i]=sy[i-1];}
      sx[0]+=dx; sy[0]+=dy;
      if(sx[0]<0||sx[0]>=GW||sy[0]<0||sy[0]>=GH) state=1;
      for(int i=1;i<len;i++) if(sx[i]==sx[0]&&sy[i]==sy[0]) state=1;
      if(sx[0]==fx && sy[0]==fy){ len++; if(len>255)len=255; score++; fx=(score*7+3)%GW; fy=(score*13+5)%GH; }
    } else {
      if(p&BTN_START){ len=3; sx[0]=5;sy[0]=5;sx[1]=4;sy[1]=5;sx[2]=3;sy[2]=5; dx=1;dy=0; score=0; state=0; fx=10;fy=5; }
    }
    arcade1_clear(0x0000);
    for(int i=0;i<len;i++) arcade1_rect(sx[i]*10,sy[i]*10,10,10,0xFFFF);
    arcade1_rect(fx*10,fy*10,10,10,0x07E0);
    arcade1_present();
    frame++;
  }
  return 0;
}
