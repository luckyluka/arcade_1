#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: raw2h W H < in.raw > out.h\n");return 2;}
  int w=atoi(argv[1]),h=atoi(argv[2]);
  printf("#ifndef RAW_IMG_H\n#define RAW_IMG_H\nstatic const unsigned short raw_img[%d*%d]={\n",w,h);
  for(int i=0;i<w*h;i++){unsigned char b[2]; if(fread(b,1,2,stdin)!=2){fprintf(stderr,"short read\n");return 1;}
    unsigned v=b[0]|(b[1]<<8); printf("0x%04X,%s",v,(i%8==7)?"\n":" ");}
  printf("};\n#endif\n");
  return 0;
}
