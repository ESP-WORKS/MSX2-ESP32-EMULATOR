/* MSX2+ boot, V9938/V9958 sprite-mode-2 collision by visible scanline.
 * Copyright lineage: fMSX by Marat Fayzullin (non-commercial).
 * Isolated helper: called only during the original MSX2+ opening (SCREEN 6).
 * Input offsets are VRAM byte offsets; VRAM size is 64/128 KiB (power of 2).
 */
#ifndef MSX_BOOT_SPRITE_COLLISION_H
#define MSX_BOOT_SPRITE_COLLISION_H
#include <stddef.h>

/* v1.06b: returns the X (0..255) of the FIRST overlapping sprite pixel on the
 * scanline, or -1. The collision flag of the real VDP is raised when the
 * beam reaches that pixel, not for the whole line. */
static int MSXBootSpriteCollisionX(const unsigned char *vram,
    unsigned vram_bytes, unsigned sprite_table, unsigned pattern_table,
    unsigned reg1, unsigned reg8, unsigned vscroll, unsigned scanline)
{
  unsigned char occupied[32] = {0}; /* 256 horizontally visible sprite dots */
  int first = -1;
  unsigned mask, colors, i, sprites_on_line = 0;
  const unsigned input_height = (reg1 & 0x02) ? 16u : 8u;
  const unsigned zoom = (reg1 & 0x01) ? 2u : 1u;
  const unsigned output_height = input_height * zoom;

  if(!vram || vram_bytes < 0x4000 || (vram_bytes & (vram_bytes-1)) ||
     scanline >= 256) return -1;
  mask=vram_bytes-1;
  colors=(sprite_table-0x200u)&mask;
  for(i=0;i<32;++i)
  {
    const unsigned at=(sprite_table+i*4u)&mask;
    const unsigned sy=vram[at];
    unsigned row, color, pattern, pattern_addr, bits, b, zz;
    int start, dy, x;

    if(sy==216u) break; /* sprite-mode-2 end marker */
    start=(unsigned char)(sy-(unsigned char)vscroll);
    if(start > (int)(256u-input_height)) start-=256;
    dy=(int)scanline-start-1; /* first sprite pixel at Y+1 */
    if(dy<0 || dy>=(int)output_height) continue;
    if(++sprites_on_line>8u) break; /* V9938: maximum 8 sprites/line */

    row=(unsigned)dy/zoom;
    color=vram[(colors+i*16u+row)&mask];
    /* mode 2: IC/CC inhibit collision; color 0 transparent when TP=0 */
    if((color&0x60u) || (!(color&0x0Fu) && !(reg8&0x20u))) continue;
    x=(int)vram[(at+1u)&mask] - ((color&0x80u)?32:0);
    pattern=vram[(at+2u)&mask];
    if(input_height==16u) pattern &= 0xFCu;
    pattern_addr=(pattern_table+pattern*8u+row)&mask;
    bits=(unsigned)vram[pattern_addr]<<8;
    if(input_height==16u) bits|=vram[(pattern_addr+16u)&mask];
    if(!bits) continue;

    for(b=0;b<input_height;++b)
      if(bits & (0x8000u>>b))
        for(zz=0;zz<zoom;++zz)
        {
          const int sx=x+(int)(b*zoom+zz);
          unsigned char bit;
          if(sx<0 || sx>=256) continue;
          bit=(unsigned char)(1u<<(sx&7));
          if(occupied[(unsigned)sx>>3]&bit) { if(first<0 || sx<first) first=sx; }
          occupied[(unsigned)sx>>3]|=bit;
        }
  }
  return first;
}

static int MSXBootSpriteCollisionLine(const unsigned char *vram,
    unsigned vram_bytes, unsigned sprite_table, unsigned pattern_table,
    unsigned reg1, unsigned reg8, unsigned vscroll, unsigned scanline)
{
  return MSXBootSpriteCollisionX(vram,vram_bytes,sprite_table,pattern_table,
                                 reg1,reg8,vscroll,scanline)>=0;
}
#endif
