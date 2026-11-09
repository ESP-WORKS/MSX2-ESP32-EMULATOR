// v1.07: g++ -O2 -Itests/stub_linering -o lr tests/test_v1_07_linering.cpp -lpthread && ./lr
#include <cstdio>
#include <cstdarg>
#include "../src/hal_linering.cpp"
int main(){
  hal_lr_init();
  static uint8_t frame[3][256*228];
  unsigned seed=1; unsigned long lines=0;
  for(int f=0;f<300;f++){
    uint8_t *fb=frame[f%3];
    static uint8_t ref[256*228];
    for(int y=0;y<212;y++){
      uint8_t *p=hal_lr_acquire(fb+256*(8+y));
      for(int x=0;x<256;x++){ seed=seed*1103515245+12345; p[x]=ref[256*(8+y)+x]=seed>>16; }
      lines++;
    }
    hal_lr_flush();
    if(memcmp(fb+256*8,ref+256*8,256*212)){printf("MISMATCH frame %d\n",f);return 1;}
  }
  printf("line ring: %lu linhas, 300 quadros identicos: PASS\n",lines);
  return 0;
}
extern "C" void hal_log(const char *f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);}
