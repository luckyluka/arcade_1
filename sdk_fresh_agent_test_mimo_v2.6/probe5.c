#include "include/arcade1.h"
volatile int iters=0;
int main(void){
  for(;;){
    iters++;
    arcade1_clear(0);
    arcade1_present();
  }
}
