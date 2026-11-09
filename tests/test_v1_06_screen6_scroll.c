/* Host-side V9958 SCREEN6 scroll tests (v1.06 semantics, openMSX reference).
 * gcc -O2 tests/test_v1_06_screen6_scroll.c && ./a.out */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../src/fmsx/fMSX/MSXV9958Screen6Scroll.h"
static uint8_t vram[128*1024];

/* Independent reference written as openMSX does it: two page "lines"
 * (even/odd), hScroll = 8*(R26&0x1F) samples, first page = R26 bit 5 when
 * SP2, display shifted right by R27 samples with backdrop on the left. */
static unsigned reference(unsigned base,unsigned r26,unsigned r27,unsigned two,unsigned x)
{
    unsigned fine=r27&7;
    if(x<fine) return MSX_V9958_S6_BACKDROP;
    unsigned lines[2];
    lines[1]=base;                         /* odd page = R#2            */
    lines[0]=two? (base&~0x8000u):base;    /* even page (SP2 only)      */
    unsigned p1=two? ((r26>>5)&1):0, p2=two? (p1^1):0;
    unsigned h=8*(r26&0x1F);
    unsigned dx=x-fine;                    /* display column            */
    unsigned line, col;
    if(dx+h<256) { line=lines[p1]; col=dx+h; }
    else         { line=lines[p2]; col=dx+h-256; }
    unsigned b=vram[(line+col/2)&(sizeof(vram)-1)];
    return (col&1)? (b>>2)&3 : b>>6;
}
int main(void)
{
    for(unsigned i=0;i<sizeof(vram);++i) vram[i]=(unsigned char)(i*13u+(i>>7)*27u+(i>>14)*19u);
    unsigned long tests=0;
    for(unsigned two=0;two<=1;two++)
      for(unsigned pg=0;pg<4;pg++)
        for(unsigned r26=0;r26<64;r26++)
          for(unsigned fine=0;fine<8;fine++)
            for(unsigned x=0;x<256;x++)
            {
                unsigned base=pg*0x8000u+0x2800u;
                unsigned got=MSXV9958Screen6Sample(vram,sizeof(vram),base,r26,fine,two,x);
                unsigned want=reference(base,r26,fine,two,x);
                if(got!=want){fprintf(stderr,"Mismatch two=%u pg=%u r26=%u r27=%u x=%u: %u vs %u\n",two,pg,r26,fine,x,got,want);return 1;}
                ++tests;
            }
    /* No scroll == original renderer (bits 7-6 then 3-2). */
    for(unsigned x=0;x<256;x++){
        unsigned v=vram[0x2800+x/2];
        assert(MSXV9958Screen6Sample(vram,sizeof(vram),0x2800,0,0,0,x)==((x&1)?(v>>2)&3:v>>6));
    }
    /* SP2 with R#2 on page 1 (3Fh) must never touch page 2 (v1.04 bug). */
    for(unsigned r26=0;r26<64;r26++) for(unsigned x=0;x<256;x++){
        unsigned base=0x8000u, sx=(x+r26*8)&511, page=(sx&256)?0x8000u:0;
        unsigned v=vram[page+(sx&255)/2];
        assert(MSXV9958Screen6Sample(vram,sizeof(vram),base,r26,0,1,x)==((sx&1)?(v>>2)&3:v>>6));
    }
    /* R#27 leaves backdrop on the left instead of wrapped pixels. */
    assert(MSXV9958Screen6Sample(vram,sizeof(vram),0x2800,0,3,0,2)==MSX_V9958_S6_BACKDROP);
    assert(MSXV9958Screen6Sample(vram,sizeof(vram),0x2800,0,3,0,3)==
           MSXV9958Screen6Sample(vram,sizeof(vram),0x2800,0,0,0,0));
    /* Without SP2, R#26 bit 5 is ignored. */
    assert(MSXV9958Screen6Sample(vram,sizeof(vram),0x2800,0x21,0,0,10)==
           MSXV9958Screen6Sample(vram,sizeof(vram),0x2800,0x01,0,0,10));
    printf("V9958 SCREEN6 scroll v1.06: %lu cases PASS (SP2 even/odd pages, R26 5/6 bits, R27 backdrop)\n",tests);
    return 0;
}
