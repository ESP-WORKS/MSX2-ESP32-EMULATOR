#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "fmsx/fMSX/MSXBootSpriteCollision.h"
#define VRAM_SIZE 131072u
static unsigned char vram[VRAM_SIZE];
static const unsigned sat=0x1B800u, sgt=0x08000u;
static void sprite(unsigned n, unsigned x, unsigned y, unsigned pat, unsigned color)
{
  vram[sat+4*n+0]=(unsigned char)y;
  vram[sat+4*n+1]=(unsigned char)x;
  vram[sat+4*n+2]=(unsigned char)pat;
  vram[sat+4*n+3]=0;
  for(unsigned i=0;i<16;i++)vram[sat-0x200u+16*n+i]=(unsigned char)color;
}
static void init(void)
{
  memset(vram,0,sizeof(vram));
  for(unsigned n=0;n<32;n++) vram[sat+4*n]=216;
  for(unsigned i=0;i<8;i++){
    vram[sgt+0*8+i]=0xFF;
    vram[sgt+1*8+i]=0xFF;
  }
}
static int hit(unsigned reg1,unsigned reg8,unsigned vscroll,unsigned scan)
{
  return MSXBootSpriteCollisionLine(vram,VRAM_SIZE,sat,sgt,reg1,reg8,vscroll,scan);
}
int main(void)
{
  init(); sprite(0,40,40,0,0x0F);sprite(1,44,40,1,0x0F);
  assert(!hit(0,0,0,40)); /* sprite Y applies to next line */
  for(unsigned n=41;n<=48;n++)assert(hit(0,0,0,n));
  assert(!hit(0,0,0,49));
  /* S#0 latches on line 41 and is re-detected on the second consecutive read. */
  {unsigned char s0=0; if(hit(0,0,0,41))s0|=0x20;
   assert((s0&0x20)!=0);s0&=~0x20;
   if(hit(0,0,0,41))s0|=0x20;
   assert(s0&0x20);}
  sprite(1,90,40,1,0x0F); assert(!hit(0,0,0,41));
  sprite(1,44,40,1,0x2F); assert(!hit(0,0,0,41)); /* IC */
  sprite(1,44,40,1,0x4F); assert(!hit(0,0,0,41)); /* CC */
  sprite(1,44,40,1,0x00); assert(!hit(0,0,0,41)); /* transparent */
  assert(hit(0,0x20,0,41)); /* TP=1 makes color zero opaque */
  sprite(1,44,40,1,0x0F);
  assert(hit(0,0,5,36)); assert(!hit(0,0,5,44)); /* vertical scroll */
  sprite(0,30,40,0,0x8F);sprite(1,0,40,1,0x0F);
  assert(hit(0,0,0,41)); /* Early clock x=16-32, 4 visible pixels */
  init();
  /* 16x16 four patterns, 2 bytes each scanline; magnification x2. */
  for(unsigned i=0;i<16;i++){
    vram[sgt+(4*8)+i]=0xFF;vram[sgt+(4*8)+16+i]=0xFF;
    vram[sgt+(8*8)+i]=0xFF;vram[sgt+(8*8)+16+i]=0xFF;
  }
  sprite(0,40,40,4,0x0F);sprite(1,60,40,8,0x0F);
  assert(!hit(0x02,0,0,41)); /* 16px sprites do not overlap */
  assert(hit(0x03,0,0,41));  /* zoom x2 => 32px wide */
  assert(hit(0x03,0,0,72));
  assert(!hit(0x03,0,0,73));
  init();
  for(unsigned i=0;i<9;i++) sprite(i,(i<8)?(i*20):0,40,(i&1),0x0F);
  /* No collisions in first eight, 9th must be ignored in sprite mode 2. */
  assert(!hit(0,0,0,41));
  puts("v1.03: 8x8, 16x16, zoom, VScroll, EC, TP, IC, CC, max 8, double read: PASS");
  return 0;
}
