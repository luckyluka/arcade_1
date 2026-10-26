#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "libretro.h"
static int frame, btn_held[3], calls_video, calls_audio;
static uint32_t last[240*320]; static int dump_at[64], ndump;
static int sched[400][2], nsched;
static void add(int f,int b){sched[nsched][0]=f;sched[nsched][1]=b;nsched++;}
static bool env(unsigned c, void*d){ (void)d; if(c==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true; return true; }
static void vid(const void*d,unsigned w,unsigned h,size_t p){ calls_video++; if(w!=240||h!=320||p!=240*4){puts("bad geometry");exit(1);} memcpy(last,d,sizeof last);
  for(int i=0;i<ndump;i++) if(dump_at[i]==frame){ char n[64]; sprintf(n,"/tmp/f%03d.raw",frame); FILE*f=fopen(n,"wb"); fwrite(last,4,240*320,f); fclose(f);} }
static size_t aud(const int16_t*d,size_t n){ (void)d; calls_audio++; return n; }
static void poll(void){}
static int16_t inp(unsigned port,unsigned dev,unsigned idx,unsigned id){ (void)port;(void)dev;(void)idx;
  int b = id==RETRO_DEVICE_ID_JOYPAD_B?0: id==RETRO_DEVICE_ID_JOYPAD_Y?1: id==RETRO_DEVICE_ID_JOYPAD_A?2:-1; if(b<0)return 0;
  for(int i=0;i<nsched;i++) if(sched[i][1]==b && frame>=sched[i][0] && frame<sched[i][0]+2) return 1; return 0; }
int main(int argc,char**argv){
  void*h=dlopen("./arcade1_skydescent_libretro.so",RTLD_NOW); if(!h){puts(dlerror());return 1;}
  #define L(n) __typeof__(n)*n##_p=dlsym(h,#n)
  void(*set_env)(retro_environment_t)=dlsym(h,"retro_set_environment");
  void(*set_vid)(retro_video_refresh_t)=dlsym(h,"retro_set_video_refresh");
  void(*set_aud)(retro_audio_sample_batch_t)=dlsym(h,"retro_set_audio_sample_batch");
  void(*set_poll)(retro_input_poll_t)=dlsym(h,"retro_set_input_poll");
  void(*set_inp)(retro_input_state_t)=dlsym(h,"retro_set_input_state");
  bool(*load)(const struct retro_game_info*)=dlsym(h,"retro_load_game");
  void(*run)(void)=dlsym(h,"retro_run");
  set_env(env);set_vid(vid);set_aud(aud);set_poll(poll);set_inp(inp);
  if(!load(NULL)){puts("load failed");return 1;}
  add(5,0); for(int i=0;i<40;i++)add(30+i*4,0); for(int i=0;i<14;i++)add(220+i*5,i&1); add(300,2);
  int d[]={40,150,310}; for(unsigned i=0;i<sizeof d/sizeof*d;i++)dump_at[ndump++]=d[i];
  for(frame=0;frame<400;frame++) run();
  printf("video=%d audio=%d\n",calls_video,calls_audio); return 0; }
