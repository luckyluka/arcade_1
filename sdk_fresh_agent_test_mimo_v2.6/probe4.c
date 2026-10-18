#include "include/arcade1.h"
volatile int p_add=0, p_mul=0, p_div=0;
int main(void){
  int i=0;
  for(;;){
    i=i+7;
    p_add=i;
    p_mul=i*12345;
    p_div=i/7;
    arcade1_clear(0);
    arcade1_present();
  }
}
