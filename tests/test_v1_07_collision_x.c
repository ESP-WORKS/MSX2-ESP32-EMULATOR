/* v1.07: X da primeira sobreposicao (evento de colisao no pixel correto). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fmsx/fMSX/MSXBootSpriteCollision.h"
static unsigned char vram[0x20000];
static void spr(int i,int y,int x,int pat,int color){
  unsigned at=0x7600+i*4; vram[at]=y; vram[at+1]=x; vram[at+2]=pat;
  for(int r=0;r<16;r++) vram[0x7400+i*16+r]=color;
}
int main(void){
  memset(vram,0,sizeof vram);
  for(int r=0;r<8;r++) vram[0x7800+r]=0xFF;     /* padrao 0: 8 pixels cheios */
  spr(0,10,100,0,15); spr(1,10,104,0,15); spr(2,10,20,0,15); spr(3,216,0,0,0);
  /* reg1=0 (8x8, sem zoom): linha 11 e a primeira linha visivel */
  assert(MSXBootSpriteCollisionX(vram,sizeof vram,0x7600,0x7800,0,0,0,11)==104);
  assert(MSXBootSpriteCollisionX(vram,sizeof vram,0x7600,0x7800,0,0,0,10)==-1);
  spr(2,10,96,0,15);                              /* sobreposicao mais a esquerda */
  assert(MSXBootSpriteCollisionX(vram,sizeof vram,0x7600,0x7800,0,0,0,12)==100);
  assert(MSXBootSpriteCollisionLine(vram,sizeof vram,0x7600,0x7800,0,0,0,12)==1);
  puts("v1.07 colisao: X da primeira sobreposicao, linha sem colisao: PASS");
  return 0;
}
