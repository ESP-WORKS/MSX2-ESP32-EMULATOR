/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                         Common.h                        **/
/**                                                         **/
/** This file contains standard screen refresh drivers      **/
/** common for X11, VGA, and other "chunky" bitmapped video **/
/** implementations. It also includes dummy sound drivers   **/
/** for fMSX.                                               **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/

static int FirstLine = 18;     /* First scanline in the XBuf */

static void  Sprites(byte Y,pixel *Line);
static void  ColorSprites(byte Y,byte *ZBuf);
static pixel *RefreshBorder(byte Y,pixel C);
static void  ClearLine(pixel *P,pixel C);
static pixel YJKColor(int Y,int J,int K);

/* v27: o MC-1000 usa caminhos compactos para linhas sem sprites.
 * Compartilhado pelos renderizadores 8/16/32-bit instanciados por CommonMux.
 * O framebuffer ativo desta placa usa BPP16 e permanece RGB565 -> VGA RGB222.
 * A verificacao e exclusiva do plano VISUAL: estado/overflow/colisoes
 * continuam sendo calculados pela rotina normal de sprites. */
#ifndef MSX_SPRITE_LINE_EMPTY_DECLARED
#define MSX_SPRITE_LINE_EMPTY_DECLARED
static int MSXSpriteLineEmpty = 0;
#endif


/** RefreshScreen() ******************************************/
/** Refresh screen. This function is called in the end of   **/
/** refresh cycle to show the entire screen.                **/
/*************************************************************/
void RefreshScreen(void) { PutImage(); }

/** ClearLine() **********************************************/
/** Clear 256 pixels from P with color C.                   **/
/*************************************************************/
static void ClearLine(register pixel *P,register pixel C)
{
  register int J;

#if defined(MSX_ESP32) && defined(BPP8)
  if(!((uintptr_t)P&3)) {
    const unsigned int cc=(unsigned int)C * 0x01010101u;
    unsigned int *D=(unsigned int *)P;
    for(J=0;J<64;++J) D[J]=cc;
    return;
  }
#elif defined(MSX_ESP32) && defined(BPP16)
  /* Quadros vazios / SCREEN OFF: 128 stores alinhadas em vez de 256. */
  if(!((uintptr_t)P&3))
  {
    const unsigned int cc=(unsigned int)C|((unsigned int)C<<16);
    unsigned int *D=(unsigned int *)P;
    for(J=0;J<128;++J) D[J]=cc;
    return;
  }
#endif
  for(J=0;J<256;J++) P[J]=C;
}

#if defined(MSX_ESP32) && defined(BPP16) && !defined(MSX_DRAW8_PACKED_DEFINED)
#define MSX_DRAW8_PACKED_DEFINED
/* SCREEN1/2: quatro stores de 32 bits em vez de oito stores de 16 bits
 * por bloco de oito pixels. P e alinhado quando HAdjust e par.
 * Faz o empacotamento no byte order nativo do framebuffer; nao confundir
 * com o rearranjo dos bytes feito somente no DMA da FabGL. */
static inline void MSXDraw8Packed(unsigned short *P, byte pattern, unsigned short fg, unsigned short bg)
{
  const unsigned int bb = (unsigned int)bg | ((unsigned int)bg << 16);
  const unsigned int bf = (unsigned int)bg | ((unsigned int)fg << 16);
  const unsigned int fb = (unsigned int)fg | ((unsigned int)bg << 16);
  const unsigned int ff = (unsigned int)fg | ((unsigned int)fg << 16);
  const unsigned int pair[4] = {bb, bf, fb, ff};
  unsigned int *out = (unsigned int *)P;
  out[0] = pair[(pattern >> 6) & 3];
  out[1] = pair[(pattern >> 4) & 3];
  out[2] = pair[(pattern >> 2) & 3];
  out[3] = pair[pattern & 3];
}
#endif

#if defined(MSX_ESP32) && defined(BPP8) && !defined(MSX_DRAW8_NATIVE222_DEFINED)
#define MSX_DRAW8_NATIVE222_DEFINED
/* v38: 8 pixels de padrao monocromatico em apenas duas palavras 32-bit.
 * A máscara de nibble produz 0xFF exclusivamente nos bytes foreground.
 * O XOR aplica fg/bg sem montar os quatro pares dinâmicos por caractere. */
static const uint32_t MSXNibbleMask[16] = {
  0x00000000u, 0xFF000000u, 0x00FF0000u, 0xFFFF0000u,
  0x0000FF00u, 0xFF00FF00u, 0x00FFFF00u, 0xFFFFFF00u,
  0x000000FFu, 0xFF0000FFu, 0x00FF00FFu, 0xFFFF00FFu,
  0x0000FFFFu, 0xFF00FFFFu, 0x00FFFFFFu, 0xFFFFFFFFu
};
static inline void MSXDraw8Native222(unsigned char *P, byte pattern, byte fg, byte bg)
{
  const uint32_t base = (uint32_t)bg * 0x01010101u;
  const uint32_t delta = (uint32_t)(fg ^ bg) * 0x01010101u;
  if(!((uintptr_t)P & 3))
  {
    ((uint32_t *)P)[0] = base ^ (delta & MSXNibbleMask[pattern >> 4]);
    ((uint32_t *)P)[1] = base ^ (delta & MSXNibbleMask[pattern & 15]);
  }
  else
  {
    P[0]=pattern&0x80?fg:bg; P[1]=pattern&0x40?fg:bg;
    P[2]=pattern&0x20?fg:bg; P[3]=pattern&0x10?fg:bg;
    P[4]=pattern&0x08?fg:bg; P[5]=pattern&0x04?fg:bg;
    P[6]=pattern&0x02?fg:bg; P[7]=pattern&0x01?fg:bg;
  }
}
#endif

#if defined(MSX_ESP32) && defined(BPP8) && !defined(MSX_DRAW4_NATIVE222_DEFINED)
#define MSX_DRAW4_NATIVE222_DEFINED
static inline void MSXDraw4Native222(unsigned char *P, byte a, byte b, byte c, byte d)
{
  if(!((uintptr_t)P & 3))
    *(unsigned int *)P = (unsigned int)a | ((unsigned int)b<<8) |
                          ((unsigned int)c<<16) | ((unsigned int)d<<24);
  else { P[0]=a; P[1]=b; P[2]=c; P[3]=d; }
}
#endif

#if defined(MSX_ESP32) && defined(BPP16) && !defined(MSX_DRAW_PAIR_DEFINED)
#define MSX_DRAW_PAIR_DEFINED
/* MC-1000-inspired compact pixel output. Native LE RGB565-swapped memory;
 * RGB222 conversion remains in hal_video.cpp, unchanged.  Runtime palette
 * lookup is essential: MSX2 can modify R#16-R#31 at any point. */
static inline void MSXDrawPair(unsigned short *P,unsigned short p0,unsigned short p1)
{
  if(!((uintptr_t)P&3))
    *(unsigned int *)P=(unsigned int)p0|((unsigned int)p1<<16);
  else
  {
    P[0]=p0;
    P[1]=p1;
  }
}
#endif

/** YJKColor() ***********************************************/
/** Given a color in YJK format, return the corresponding   **/
/** palette entry.                                          **/
/*************************************************************/
INLINE pixel YJKColor(register int Y,register int J,register int K)
{
  register int R,G,B;
		
  R=Y+J;
  G=Y+K;
  B=((5*Y-2*J-K)/4);

  R=R<0? 0:R>31? 31:R;
  G=G<0? 0:G>31? 31:G;
  B=B<0? 0:B>31? 31:B;

  return(BPal[(R&0x1C)|((G&0x1C)<<3)|(B>>3)]);
}

/** RefreshBorder() ******************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** the screen border. It returns a pointer to the start of **/
/** scanline Y in XBuf or 0 if scanline is beyond XBuf.     **/
/*************************************************************/
pixel *RefreshBorder(register byte Y,register pixel C)
{
  register pixel *P;
  register int H;

  /* First line number in the buffer */
  if(!Y) FirstLine=(ScanLines212? 8:18)+VAdjust;

  /* Return 0 if we've run out of the screen buffer due to overscan */
  if(Y+FirstLine>=HEIGHT) return(0);

  /* Set up the transparent color */
  XPal[0]=(!BGColor||SolidColor0)? XPal0:XPal[BGColor];

  /* Start of the buffer */
  P=(pixel *)XBuf;

  /* Paint top of the screen */
#ifndef MSX_ESP32
  if(!Y) for(H=WIDTH*FirstLine-1;H>=0;H--) P[H]=C;
#endif

  /* Start of the line */
  P+=WIDTH*(FirstLine+Y);

  /* Paint left/right borders */
  for(H=(WIDTH-256)/2+HAdjust;H>0;H--) P[H-1]=C;
  for(H=(WIDTH-256)/2-HAdjust;H>0;H--) P[WIDTH-H]=C;

  /* Paint bottom of the screen */
  H=ScanLines212? 212:192;
#ifndef MSX_ESP32
  if(Y==H-1) for(H=WIDTH*(HEIGHT-H-FirstLine+1)-1;H>=WIDTH;H--) P[H]=C;
#endif

  /* Return pointer to the scanline in XBuf */
  return(P+(WIDTH-256)/2+HAdjust);
}

/** Sprites() ************************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** sprites in SCREENs 1-3.                                 **/
/*************************************************************/
void Sprites(register byte Y,register pixel *Line)
{
  static const byte SprHeights[4] = { 8,16,16,32 };
  register pixel *P,C;
  register byte OH,IH,*PT,*AT;
  register unsigned int M;
  register int L,K;

  /* No extra sprites yet */
  VDPStatus[0]&=~0x5F;

  /* TMS9918/TMS9928: atributo 208 na primeira posicao encerra
   * a lista. Evita preparar 32 sprites quando a tela e vazia. */
#if defined(MSX_ESP32) && (defined(BPP16) || defined(BPP8))
  if(SprTab[0]==208) return;
#endif

  /* Assign initial values before counting */
  OH = SprHeights[VDP[1]&0x03];
  IH = SprHeights[VDP[1]&0x02];
  AT = SprTab-4;
  Y += VScroll;
  C  = MAXSPRITE1+1;
  M  = 0;

  /* Count displayed sprites */
  for(L=0;L<32;++L)
  {
    M<<=1;AT+=4;        /* Iterating through SprTab      */
    K=AT[0];            /* K = sprite Y coordinate       */
    if(K==208) break;   /* Iteration terminates if Y=208 */
    if(K>256-IH) K-=256; /* Y coordinate may be negative  */

    /* Mark all valid sprites with 1s, break at MAXSPRITE1 sprites */
    if((Y>K)&&(Y<=K+OH))
    {
      /* If we exceed the maximum number of sprites per line... */
      if(!--C)
      {
        /* Set 5thSprite flag in the VDP status register */
        VDPStatus[0]|=0x40;
        /* Stop drawing sprites, unless all-sprites option enabled */
        if(!OPTION(MSX_ALLSPRITE)) break;
      }

      /* Mark sprite as ready to draw */
      M|=1;
    }
  }

  /* Mark last checked sprite (5th in line, Y=208, or sprite #31) */
  VDPStatus[0]|=L<32? L:31;

  /* Draw all marked sprites */
  for(;M;M>>=1,AT-=4)
    if(M&1)
    {
      C = AT[3];                  /* C = sprite attributes */
      L = C&0x80? AT[1]-32:AT[1]; /* Sprite may be shifted left by 32 */
      C&= 0x0F;                   /* C = sprite color */

      if((L<256)&&(L>-OH)&&C)
      {
        K = AT[0];                /* K = sprite Y coordinate */
        if(K>256-IH) K-=256;      /* Y coordinate may be negative */

        P  = Line+L;
        K  = Y-K-1;
        PT = SprGen+((int)(IH>8? AT[2]&0xFC:AT[2])<<3)+(OH>IH? (K>>1):K);
        C  = XPal[C];

        /* Mask 1: clip left sprite boundary */
        K=L>=0? 0xFFFF:(0x10000>>(OH>IH? (-L>>1):-L))-1;

        /* Mask 2: clip right sprite boundary */
        L+=(int)OH-257;
        if(L>=0)
        {
          L=(IH>8? 0x0002:0x0200)<<(OH>IH? (L>>1):L);
          K&=~(L-1);
        }

        /* Get and clip the sprite data */
        K&=((int)PT[0]<<8)|(IH>8? PT[16]:0x00);

        /* If output size is bigger than the input size... */
        if(OH>IH)
        {
          /* Big (zoomed) sprite */

          /* Draw left 16 pixels of the sprite */
          if(K&0xFF00)
          {
            if(K&0x8000) P[1]=P[0]=C;
            if(K&0x4000) P[3]=P[2]=C;
            if(K&0x2000) P[5]=P[4]=C;
            if(K&0x1000) P[7]=P[6]=C;
            if(K&0x0800) P[9]=P[8]=C;
            if(K&0x0400) P[11]=P[10]=C;
            if(K&0x0200) P[13]=P[12]=C;
            if(K&0x0100) P[15]=P[14]=C;
          }

          /* Draw right 16 pixels of the sprite */
          if(K&0x00FF)
          {
            if(K&0x0080) P[17]=P[16]=C;
            if(K&0x0040) P[19]=P[18]=C;
            if(K&0x0020) P[21]=P[20]=C;
            if(K&0x0010) P[23]=P[22]=C;
            if(K&0x0008) P[25]=P[24]=C;
            if(K&0x0004) P[27]=P[26]=C;
            if(K&0x0002) P[29]=P[28]=C;
            if(K&0x0001) P[31]=P[30]=C;
          }
        }
        else
        {
          /* Normal (unzoomed) sprite */

          /* Draw left 8 pixels of the sprite */
          if(K&0xFF00)
          {
            if(K&0x8000) P[0]=C;
            if(K&0x4000) P[1]=C;
            if(K&0x2000) P[2]=C;
            if(K&0x1000) P[3]=C;
            if(K&0x0800) P[4]=C;
            if(K&0x0400) P[5]=C;
            if(K&0x0200) P[6]=C;
            if(K&0x0100) P[7]=C;
          }
   
          /* Draw right 8 pixels of the sprite */
          if(K&0x00FF)
          {
            if(K&0x0080) P[8]=C;
            if(K&0x0040) P[9]=C;
            if(K&0x0020) P[10]=C;
            if(K&0x0010) P[11]=C;
            if(K&0x0008) P[12]=C;
            if(K&0x0004) P[13]=C;
            if(K&0x0002) P[14]=C;
            if(K&0x0001) P[15]=C;
          }
        }
      }
    }
}

/** ColorSprites() *******************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** color sprites in SCREENs 4-8. The result is returned in **/
/** ZBuf, whose size must be 320 bytes (32+256+32).         **/
/*************************************************************/
void ColorSprites(register byte Y,byte *ZBuf)
{
  static const byte SprHeights[4] = { 8,16,16,32 };
  register byte C,IH,OH,J,OrThem;
  register byte *P,*PT,*AT;
  register int L,K;
  register unsigned int M;

  /* No extra sprites yet */
  VDPStatus[0]&=~0x5F;

#if defined(MSX_ESP32) && (defined(BPP8) || defined(BPP16))
  /* V9938: primeiro sprite Y=216 encerra a tabela. O loop original
   * deixa status baixo=0 e nenhum pixel, exatamente como aqui. */
  if(SpritesOFF || SprTab[0]==216) { MSXSpriteLineEmpty=1; return; }
#endif

  /* Nada visivel nesta linha: nao precisamos zerar os 256 bytes do
   * sprite buffer quando nao serao consultados. Mantem a semantica
   * de status/overflow do VDP: so o caminho de pixels e abreviado. */
  MSXSpriteLineEmpty = 1;
#if !defined(MSX_ESP32) || (!defined(BPP16) && !defined(BPP8))
  memset(ZBuf+32,0,256);
#endif
  if(SpritesOFF) return;

  /* Assign initial values before counting */
  OrThem = 0x00;
  OH = SprHeights[VDP[1]&0x03];
  IH = SprHeights[VDP[1]&0x02];
  AT = SprTab-4;
  C  = MAXSPRITE2+1;
  M  = 0;

  /* Count displayed sprites */
  for(L=0;L<32;++L)
  {
    M<<=1;AT+=4;              /* Iterating through SprTab      */
    K=AT[0];                  /* Read Y from SprTab            */
    if(K==216) break;         /* Iteration terminates if Y=216 */
    K=(byte)(K-VScroll);      /* Sprite's actual Y coordinate  */
    if(K>256-IH) K-=256;       /* Y coordinate may be negative  */

    /* Mark all valid sprites with 1s, break at MAXSPRITE2 sprites */
    if((Y>K)&&(Y<=K+OH))
    {
      /* If we exceed the maximum number of sprites per line... */
      if(!--C)
      {
        /* Set 9thSprite flag in the VDP status register */
        VDPStatus[0]|=0x40;
        /* Stop drawing sprites, unless all-sprites option enabled */
        if(!OPTION(MSX_ALLSPRITE)) break;
      }

      /* Mark sprite as ready to draw */
      M|=1;
    }
  }

  /* Mark last checked sprite (9th in line, Y=216, or sprite #31) */
  VDPStatus[0]|=L<32? L:31;

  /* Sem sprite nesta linha: a imagem de background basta. */
  if(!M) return;
  MSXSpriteLineEmpty = 0;
#if defined(MSX_ESP32) && (defined(BPP16) || defined(BPP8))
  memset(ZBuf+32,0,256);
#endif

  /* Draw all marked sprites */
  for(;M;M>>=1,AT-=4)
    if(M&1)
    {
      K = (byte)(AT[0]-VScroll);  /* K = sprite Y coordinate */
      if(K>256-IH) K-=256;        /* Y coordinate may be negative */

      J = Y-K-1;
      J = OH>IH? (J>>1):J;
      C = SprTab[-0x0200+((AT-SprTab)<<2)+J];
      OrThem|=C&0x40;

      if(C&0x0F)
      {
        PT = SprGen+((int)(IH>8? AT[2]&0xFC:AT[2])<<3)+J;
        P  = ZBuf+AT[1]+(C&0x80? 0:32);
        C &= 0x0F;
        J  = PT[0];

        if(OrThem&0x20)
        {
          if(OH>IH)
          {
            if(J&0x80) { P[0]|=C;P[1]|=C; }
            if(J&0x40) { P[2]|=C;P[3]|=C; }
            if(J&0x20) { P[4]|=C;P[5]|=C; }
            if(J&0x10) { P[6]|=C;P[7]|=C; }
            if(J&0x08) { P[8]|=C;P[9]|=C; }
            if(J&0x04) { P[10]|=C;P[11]|=C; }
            if(J&0x02) { P[12]|=C;P[13]|=C; }
            if(J&0x01) { P[14]|=C;P[15]|=C; }
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) { P[16]|=C;P[17]|=C; }
              if(J&0x40) { P[18]|=C;P[19]|=C; }
              if(J&0x20) { P[20]|=C;P[21]|=C; }
              if(J&0x10) { P[22]|=C;P[23]|=C; }
              if(J&0x08) { P[24]|=C;P[25]|=C; }
              if(J&0x04) { P[26]|=C;P[27]|=C; }
              if(J&0x02) { P[28]|=C;P[29]|=C; }
              if(J&0x01) { P[30]|=C;P[31]|=C; }
            }
          }
          else
          {
            if(J&0x80) P[0]|=C;
            if(J&0x40) P[1]|=C;
            if(J&0x20) P[2]|=C;
            if(J&0x10) P[3]|=C;
            if(J&0x08) P[4]|=C;
            if(J&0x04) P[5]|=C;
            if(J&0x02) P[6]|=C;
            if(J&0x01) P[7]|=C;
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) P[8]|=C;
              if(J&0x40) P[9]|=C;
              if(J&0x20) P[10]|=C;
              if(J&0x10) P[11]|=C;
              if(J&0x08) P[12]|=C;
              if(J&0x04) P[13]|=C;
              if(J&0x02) P[14]|=C;
              if(J&0x01) P[15]|=C;
            }
          }
        }
        else
        {
          if(OH>IH)
          {
            if(J&0x80) P[0]=P[1]=C;
            if(J&0x40) P[2]=P[3]=C;
            if(J&0x20) P[4]=P[5]=C;
            if(J&0x10) P[6]=P[7]=C;
            if(J&0x08) P[8]=P[9]=C;
            if(J&0x04) P[10]=P[11]=C;
            if(J&0x02) P[12]=P[13]=C;
            if(J&0x01) P[14]=P[15]=C;
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) P[16]=P[17]=C;
              if(J&0x40) P[18]=P[19]=C;
              if(J&0x20) P[20]=P[21]=C;
              if(J&0x10) P[22]=P[23]=C;
              if(J&0x08) P[24]=P[25]=C;
              if(J&0x04) P[26]=P[27]=C;
              if(J&0x02) P[28]=P[29]=C;
              if(J&0x01) P[30]=P[31]=C;
            }
          }
          else
          {
            if(J&0x80) P[0]=C;
            if(J&0x40) P[1]=C;
            if(J&0x20) P[2]=C;
            if(J&0x10) P[3]=C;
            if(J&0x08) P[4]=C;
            if(J&0x04) P[5]=C;
            if(J&0x02) P[6]=C;
            if(J&0x01) P[7]=C;
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) P[8]=C;
              if(J&0x40) P[9]=C;
              if(J&0x20) P[10]=C;
              if(J&0x10) P[11]=C;
              if(J&0x08) P[12]=C;
              if(J&0x04) P[13]=C;
              if(J&0x02) P[14]=C;
              if(J&0x01) P[15]=C;
            }
          }
        }
      }

      /* Update overlapping flag */
      OrThem>>=1;
    }
}

/** RefreshLineF() *******************************************/
/** Dummy refresh function called for non-existing screens. **/
/*************************************************************/
void RefreshLineF(register byte Y)
{
  register pixel *P;

  if(Verbose>1)
    printf
    (
      "ScrMODE %d: ChrTab=%X ChrGen=%X ColTab=%X SprTab=%X SprGen=%X\n",
      ScrMode,
      (int)(ChrTab-VRAM),
      (int)(ChrGen-VRAM),
      (int)(ColTab-VRAM),
      (int)(SprTab-VRAM),
      (int)(SprGen-VRAM)
    );

  P=RefreshBorder(Y,XPal[BGColor]);
  if(P) ClearLine(P,XPal[BGColor]);
}

/** RefreshLine0() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN0.                 **/
/*************************************************************/
void RefreshLine0(register byte Y)
{
  register pixel *P,FC,BC;
  register byte X,*T,*G;

  BC=XPal[BGColor];
  P=RefreshBorder(Y,BC);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BC);
  else
  {
    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=P[7]=P[8]=BC;

    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+((Y+VScroll)&0x07);
    T=ChrTab+40*(Y>>3);
    FC=XPal[FGColor];
    P+=9;

    for(X=0;X<40;X++,T++,P+=6)
    {
      Y=G[(int)*T<<3];
      P[0]=Y&0x80? FC:BC;P[1]=Y&0x40? FC:BC;
      P[2]=Y&0x20? FC:BC;P[3]=Y&0x10? FC:BC;
      P[4]=Y&0x08? FC:BC;P[5]=Y&0x04? FC:BC;
    }

    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=BC;
  }
}

/** RefreshLine1() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN1, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine1(register byte Y)
{
  register pixel *P,FC,BC;
  register byte K,X,*T,*G;

  P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    Y+=VScroll;
    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+(Y&0x07);
    T=ChrTab+((int)(Y&0xF8)<<2);

    for(X=0;X<32;X++,T++,P+=8)
    {
      K=ColTab[*T>>3];
      FC=XPal[K>>4];
      BC=XPal[K&0x0F];
      K=G[(int)*T<<3];
#if defined(MSX_ESP32) && defined(BPP8)
      MSXDraw8Native222(P,K,FC,BC);
      continue;
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!((uintptr_t)P & 3)) MSXDraw8Packed(P,K,FC,BC);
      else
#endif
      {
        P[0]=K&0x80? FC:BC;P[1]=K&0x40? FC:BC;
        P[2]=K&0x20? FC:BC;P[3]=K&0x10? FC:BC;
        P[4]=K&0x08? FC:BC;P[5]=K&0x04? FC:BC;
        P[6]=K&0x02? FC:BC;P[7]=K&0x01? FC:BC;
      }
    }

    if(!SpritesOFF) Sprites(Y,P-256);
  }
}

/** RefreshLine2() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN2, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine2(register byte Y)
{
  register pixel *P,FC,BC;
  register byte K,X,*T;
  register int I,J;

  P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    Y+=VScroll;
    T=ChrTab+((int)(Y&0xF8)<<2);
    I=((int)(Y&0xC0)<<5)+(Y&0x07);

    for(X=0;X<32;X++,T++,P+=8)
    {
      J=(int)*T<<3;
      K=ColTab[(I+J)&ColTabM];
      FC=XPal[K>>4];
      BC=XPal[K&0x0F];
      K=ChrGen[(I+J)&ChrGenM];
#if defined(MSX_ESP32) && defined(BPP8)
      MSXDraw8Native222(P,K,FC,BC);
      continue;
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!((uintptr_t)P & 3)) MSXDraw8Packed(P,K,FC,BC);
      else
#endif
      {
        P[0]=K&0x80? FC:BC;P[1]=K&0x40? FC:BC;
        P[2]=K&0x20? FC:BC;P[3]=K&0x10? FC:BC;
        P[4]=K&0x08? FC:BC;P[5]=K&0x04? FC:BC;
        P[6]=K&0x02? FC:BC;P[7]=K&0x01? FC:BC;
      }
    }

    if(!SpritesOFF) Sprites(Y,P-256);
  }
}

/** RefreshLine3() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN3, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine3(register byte Y)
{
  register pixel *P;
  register byte X,K,*T,*G;

  P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    Y+=VScroll;
    T=ChrTab+((int)(Y&0xF8)<<2);
    G=ChrGen+((Y&0x1C)>>2);

    for(X=0;X<32;X++,T++,P+=8)
    {
      K=G[(int)*T<<3];
#if defined(MSX_ESP32) && defined(BPP8)
      /* SCREEN 3: dois grupos de 4 pixels em dois stores RGB222.
       * Em alinhamentos incomuns, helper conserva acesso byte-a-byte. */
      { const byte c0=XPal[K>>4],c1=XPal[K&0x0F];
        MSXDraw4Native222(P,c0,c0,c0,c0);
        MSXDraw4Native222(P+4,c1,c1,c1,c1);
      }
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!((uintptr_t)P&3))
      {
        const unsigned int a=(unsigned int)XPal[K>>4]*0x00010001u;
        const unsigned int b=(unsigned int)XPal[K&0x0F]*0x00010001u;
        unsigned int *out=(unsigned int *)P;
        out[0]=out[1]=a;out[2]=out[3]=b;
      }
      else
      {
        P[0]=P[1]=P[2]=P[3]=XPal[K>>4];
        P[4]=P[5]=P[6]=P[7]=XPal[K&0x0F];
      }
#else
      P[0]=P[1]=P[2]=P[3]=XPal[K>>4];
      P[4]=P[5]=P[6]=P[7]=XPal[K&0x0F];
#endif
    }

    if(!SpritesOFF) Sprites(Y,P-256);
  }
}

/** RefreshLine4() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN4, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine4(register byte Y)
{
  register pixel *P,FC,BC;
  register byte K,X,C,*T,*R;
  register int I,J;
  byte ZBuf[320];

  P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // V9938/V9958 SCREEN4: mesmo padrao/cores da SCREEN2, sem sprite.
      Y+=VScroll;
      T=ChrTab+((int)(Y&0xF8)<<2);
      I=((int)(Y&0xC0)<<5)+(Y&0x07);
      for(X=0;X<32;X++,T++,P+=8)
      {
        J=(int)*T<<3;
        K=ColTab[(I+J)&ColTabM];
        FC=XPal[K>>4];BC=XPal[K&0x0F];
        K=ChrGen[(I+J)&ChrGenM];
#if defined(MSX_ESP32) && defined(BPP8)
        MSXDraw8Native222(P,K,FC,BC);
        continue;
#elif defined(MSX_ESP32) && defined(BPP16)
        if(!((uintptr_t)P&3)) MSXDraw8Packed(P,K,FC,BC);
        else
#endif
        {
          P[0]=K&0x80?FC:BC;P[1]=K&0x40?FC:BC;
          P[2]=K&0x20?FC:BC;P[3]=K&0x10?FC:BC;
          P[4]=K&0x08?FC:BC;P[5]=K&0x04?FC:BC;
          P[6]=K&0x02?FC:BC;P[7]=K&0x01?FC:BC;
        }
      }
      return;
    }
    R=ZBuf+32;
    Y+=VScroll;
    T=ChrTab+((int)(Y&0xF8)<<2);
    I=((int)(Y&0xC0)<<5)+(Y&0x07);

    for(X=0;X<32;X++,R+=8,P+=8,T++)
    {
      J=(int)*T<<3;
      K=ColTab[(I+J)&ColTabM];
      FC=XPal[K>>4];
      BC=XPal[K&0x0F];
      K=ChrGen[(I+J)&ChrGenM];

#if defined(MSX_ESP32) && defined(BPP8)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        MSXDraw8Native222(P,K,FC,BC);
        continue;
      }
#elif defined(MSX_ESP32) && defined(BPP16)
      // Sprite em outro tile: usar as quatro stores empacotadas.
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]) && !((uintptr_t)P&3))
      {
        MSXDraw8Packed(P,K,FC,BC);
        continue;
      }
#endif
      C=R[0];P[0]=C? XPal[C]:(K&0x80)? FC:BC;
      C=R[1];P[1]=C? XPal[C]:(K&0x40)? FC:BC;
      C=R[2];P[2]=C? XPal[C]:(K&0x20)? FC:BC;
      C=R[3];P[3]=C? XPal[C]:(K&0x10)? FC:BC;
      C=R[4];P[4]=C? XPal[C]:(K&0x08)? FC:BC;
      C=R[5];P[5]=C? XPal[C]:(K&0x04)? FC:BC;
      C=R[6];P[6]=C? XPal[C]:(K&0x02)? FC:BC;
      C=R[7];P[7]=C? XPal[C]:(K&0x01)? FC:BC;
    }
  }
}

/** RefreshLine5() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN5, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine5(register byte Y)
{
  register pixel *P;
  register byte I,X,*T,*R;
  byte ZBuf[320];

  P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // SCREEN5: 2 pixels por byte, evita 256 testes de sprite/linha.
      T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);
      for(int N=0;N<128;++N)
      {
        byte V=T[N];
#if defined(MSX_ESP32) && defined(BPP8)
        if((N&1)==0 && N+1<128) {
          byte W=T[N+1];
          MSXDraw4Native222(P+N*2,XPal[V>>4],XPal[V&15],XPal[W>>4],XPal[W&15]);
          ++N;
        }
#elif defined(MSX_ESP32) && defined(BPP16)
        MSXDrawPair(P+N*2,XPal[V>>4],XPal[V&0x0F]);
#else
        P[N*2]=XPal[V>>4];
        P[N*2+1]=XPal[V&0x0F];
#endif
      }
      return;
    }
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);

    for(X=0;X<16;X++,R+=16,P+=16,T+=8)
    {
#if defined(MSX_ESP32) && defined(BPP8)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]|
           R[8]|R[9]|R[10]|R[11]|R[12]|R[13]|R[14]|R[15]))
      {
        for(int t=0;t<8;t+=2) {
          byte a=T[t],b=T[t+1];
          MSXDraw4Native222(P+t*2,XPal[a>>4],XPal[a&15],XPal[b>>4],XPal[b&15]);
        }
        continue;
      }
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]|
           R[8]|R[9]|R[10]|R[11]|R[12]|R[13]|R[14]|R[15]))
      {
        for(int t=0;t<8;++t)
          MSXDrawPair(P+t*2,XPal[T[t]>>4],XPal[T[t]&15]);
        continue;
      }
#endif
      I=R[0];P[0]=XPal[I? I:T[0]>>4];
      I=R[1];P[1]=XPal[I? I:T[0]&0x0F];
      I=R[2];P[2]=XPal[I? I:T[1]>>4];
      I=R[3];P[3]=XPal[I? I:T[1]&0x0F];
      I=R[4];P[4]=XPal[I? I:T[2]>>4];
      I=R[5];P[5]=XPal[I? I:T[2]&0x0F];
      I=R[6];P[6]=XPal[I? I:T[3]>>4];
      I=R[7];P[7]=XPal[I? I:T[3]&0x0F];
      I=R[8];P[8]=XPal[I? I:T[4]>>4];
      I=R[9];P[9]=XPal[I? I:T[4]&0x0F];
      I=R[10];P[10]=XPal[I? I:T[5]>>4];
      I=R[11];P[11]=XPal[I? I:T[5]&0x0F];
      I=R[12];P[12]=XPal[I? I:T[6]>>4];
      I=R[13];P[13]=XPal[I? I:T[6]&0x0F];
      I=R[14];P[14]=XPal[I? I:T[7]>>4];
      I=R[15];P[15]=XPal[I? I:T[7]&0x0F];
    }
  }
}

/** RefreshLine8() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN8, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine8(register byte Y)
{
  static byte SprToScr[16] =
  {
    0x00,0x02,0x10,0x12,0x80,0x82,0x90,0x92,
    0x49,0x4B,0x59,0x5B,0xC9,0xCB,0xD9,0xDB
  };
  register pixel *P;
  register byte C,X,*T,*R;
  byte ZBuf[320];

  P=RefreshBorder(Y,BPal[VDP[7]]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BPal[VDP[7]]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // SCREEN8: 256 indices diretos GRB (V9938/V9958).
      T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
#if defined(MSX_ESP32) && defined(BPP8)
      for(int N=0;N<256;N+=4)
        MSXDraw4Native222(P+N,BPal[T[N]],BPal[T[N+1]],BPal[T[N+2]],BPal[T[N+3]]);
#elif defined(MSX_ESP32) && defined(BPP16)
      for(int N=0;N<256;N+=2)
        MSXDrawPair(P+N,BPal[T[N]],BPal[T[N+1]]);
#else
      for(int N=0;N<256;++N) P[N]=BPal[T[N]];
#endif
      return;
    }
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);

    for(X=0;X<32;X++,T+=8,R+=8,P+=8)
    {
#if defined(MSX_ESP32) && defined(BPP8)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        MSXDraw4Native222(P,BPal[T[0]],BPal[T[1]],BPal[T[2]],BPal[T[3]]);
        MSXDraw4Native222(P+4,BPal[T[4]],BPal[T[5]],BPal[T[6]],BPal[T[7]]);
        continue;
      }
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        MSXDrawPair(P+0,BPal[T[0]],BPal[T[1]]);
        MSXDrawPair(P+2,BPal[T[2]],BPal[T[3]]);
        MSXDrawPair(P+4,BPal[T[4]],BPal[T[5]]);
        MSXDrawPair(P+6,BPal[T[6]],BPal[T[7]]);
        continue;
      }
#endif
      C=R[0];P[0]=BPal[C? SprToScr[C]:T[0]];
      C=R[1];P[1]=BPal[C? SprToScr[C]:T[1]];
      C=R[2];P[2]=BPal[C? SprToScr[C]:T[2]];
      C=R[3];P[3]=BPal[C? SprToScr[C]:T[3]];
      C=R[4];P[4]=BPal[C? SprToScr[C]:T[4]];
      C=R[5];P[5]=BPal[C? SprToScr[C]:T[5]];
      C=R[6];P[6]=BPal[C? SprToScr[C]:T[6]];
      C=R[7];P[7]=BPal[C? SprToScr[C]:T[7]];
    }
  }
}

/** RefreshLine10() ******************************************/
/** Refresh line Y (0..191/211) of SCREEN10/11, including   **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine10(register byte Y)
{
  register pixel *P;
  register byte C,X,*T,*R;
  register int J,K;
  byte ZBuf[320];

  P=RefreshBorder(Y,BPal[VDP[7]]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BPal[VDP[7]]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // V9958 YAE: caminho completo YJK/YAE, sem checagem de sprite.
      T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
      P[0]=P[1]=P[2]=P[3]=BPal[VDP[7]];
      P+=4;
      for(X=0;X<63;++X,T+=4,P+=4)
      {
        K=(T[0]&7)|((T[1]&7)<<3);if(K&32)K-=64;
        J=(T[2]&7)|((T[3]&7)<<3);if(J&32)J-=64;
#if defined(MSX_ESP32) && defined(BPP8)
        int y0=T[0]>>3, y1=T[1]>>3, y2=T[2]>>3, y3=T[3]>>3;
        MSXDraw4Native222(P,(y0&1)?XPal[y0>>1]:YJKColor(y0,J,K),
                            (y1&1)?XPal[y1>>1]:YJKColor(y1,J,K),
                            (y2&1)?XPal[y2>>1]:YJKColor(y2,J,K),
                            (y3&1)?XPal[y3>>1]:YJKColor(y3,J,K));
#elif defined(MSX_ESP32) && defined(BPP16)
        int y0=T[0]>>3, y1=T[1]>>3, y2=T[2]>>3, y3=T[3]>>3;
        MSXDrawPair(P+0,(y0&1)?XPal[y0>>1]:YJKColor(y0,J,K),
                         (y1&1)?XPal[y1>>1]:YJKColor(y1,J,K));
        MSXDrawPair(P+2,(y2&1)?XPal[y2>>1]:YJKColor(y2,J,K),
                         (y3&1)?XPal[y3>>1]:YJKColor(y3,J,K));
#else
        for(int N=0;N<4;++N)
        {
          int lum=T[N]>>3;
          P[N]=(lum&1)?XPal[lum>>1]:YJKColor(lum,J,K);
        }
#endif
      }
      return;
    }
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);

    /* Draw first 4 pixels */
    C=R[0];P[0]=C? XPal[C]:BPal[VDP[7]];
    C=R[1];P[1]=C? XPal[C]:BPal[VDP[7]];
    C=R[2];P[2]=C? XPal[C]:BPal[VDP[7]];
    C=R[3];P[3]=C? XPal[C]:BPal[VDP[7]];
    R+=4;P+=4;

    for(X=0;X<63;X++,T+=4,R+=4,P+=4)
    {
      K=(T[0]&0x07)|((T[1]&0x07)<<3);
      if(K&0x20) K-=64;
      J=(T[2]&0x07)|((T[3]&0x07)<<3);
      if(J&0x20) J-=64;

      C=R[0];Y=T[0]>>3;P[0]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
      C=R[1];Y=T[1]>>3;P[1]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
      C=R[2];Y=T[2]>>3;P[2]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
      C=R[3];Y=T[3]>>3;P[3]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
    }
  }
}

/** RefreshLine12() ******************************************/
/** Refresh line Y (0..191/211) of SCREEN12, including      **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine12(register byte Y)
{
  register pixel *P;
  register byte C,X,*T,*R;
  register int J,K;
  byte ZBuf[320];

  P=RefreshBorder(Y,BPal[VDP[7]]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BPal[VDP[7]]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // V9958 YJK: preserva deslocamento horizontal do VDP e paleta.
      T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF)
        +(HScroll512&&(HScroll>255)?0x10000:0)+(HScroll&0xFC);
      P[0]=P[1]=P[2]=P[3]=BPal[VDP[7]];
      P+=4;
      for(X=1;X<64;++X,T+=4,P+=4)
      {
        K=(T[0]&7)|((T[1]&7)<<3);if(K&32)K-=64;
        J=(T[2]&7)|((T[3]&7)<<3);if(J&32)J-=64;
#if defined(MSX_ESP32) && defined(BPP8)
        MSXDraw4Native222(P,YJKColor(T[0]>>3,J,K),YJKColor(T[1]>>3,J,K),
                            YJKColor(T[2]>>3,J,K),YJKColor(T[3]>>3,J,K));
#elif defined(MSX_ESP32) && defined(BPP16)
        MSXDrawPair(P+0,YJKColor(T[0]>>3,J,K),YJKColor(T[1]>>3,J,K));
        MSXDrawPair(P+2,YJKColor(T[2]>>3,J,K),YJKColor(T[3]>>3,J,K));
#else
        P[0]=YJKColor(T[0]>>3,J,K);
        P[1]=YJKColor(T[1]>>3,J,K);
        P[2]=YJKColor(T[2]>>3,J,K);
        P[3]=YJKColor(T[3]>>3,J,K);
#endif
      }
      return;
    }
    R=ZBuf+32;
    T = ChrTab
      + (((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF)
      + (HScroll512&&(HScroll>255)? 0x10000:0)
      + (HScroll&0xFC);

    /* Draw first 4 pixels */
    C=R[0];P[0]=C? XPal[C]:BPal[VDP[7]];
    C=R[1];P[1]=C? XPal[C]:BPal[VDP[7]];
    C=R[2];P[2]=C? XPal[C]:BPal[VDP[7]];
    C=R[3];P[3]=C? XPal[C]:BPal[VDP[7]];
    R+=4;P+=4;

    for(X=1;X<64;X++,T+=4,R+=4,P+=4)
    {
      K=(T[0]&0x07)|((T[1]&0x07)<<3);
      if(K&0x20) K-=64;
      J=(T[2]&0x07)|((T[3]&0x07)<<3);
      if(J&0x20) J-=64;

      C=R[0];P[0]=C? XPal[C]:YJKColor(T[0]>>3,J,K);
      C=R[1];P[1]=C? XPal[C]:YJKColor(T[1]>>3,J,K);
      C=R[2];P[2]=C? XPal[C]:YJKColor(T[2]>>3,J,K);
      C=R[3];P[3]=C? XPal[C]:YJKColor(T[3]>>3,J,K);
    }
  }
}

#ifdef NARROW

/** RefreshLine6() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN6, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine6(register byte Y)
{
  register pixel *P;
  register byte X,*T,*R,C;
  byte ZBuf[320];

  P=RefreshBorder(Y,XPal[BGColor&0x03]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor&0x03]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // SCREEN6: duas amostras visiveis de cada byte (512 -> 256).
      T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);
      for(int N=0;N<128;++N)
      {
        byte V=T[N];
#if defined(MSX_ESP32) && defined(BPP8)
        if((N&1)==0 && N+1<128) {
          byte W=T[N+1];
          MSXDraw4Native222(P+N*2,XPal[V>>6],XPal[(V>>2)&3],XPal[W>>6],XPal[(W>>2)&3]);
          ++N;
        }
#elif defined(MSX_ESP32) && defined(BPP16)
        MSXDrawPair(P+N*2,XPal[V>>6],XPal[(V>>2)&0x03]);
#else
        P[N*2]=XPal[V>>6];
        P[N*2+1]=XPal[(V>>2)&0x03];
#endif
      }
      return;
    }
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);

    for(X=0;X<32;X++)
    {
#if defined(MSX_ESP32) && defined(BPP8)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        for(int t=0;t<4;t+=2) {
          byte a=T[t], b=T[t+1];
          MSXDraw4Native222(P+t*2,XPal[a>>6],XPal[(a>>2)&3],XPal[b>>6],XPal[(b>>2)&3]);
        }
        R+=8;P+=8;T+=4;
        continue;
      }
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        for(int t=0;t<4;++t)
          MSXDrawPair(P+t*2,XPal[T[t]>>6],XPal[(T[t]>>2)&3]);
        R+=8;P+=8;T+=4;
        continue;
      }
#endif
      C=R[0];P[0]=XPal[C? C:T[0]>>6];
      C=R[1];P[1]=XPal[C? C:(T[0]>>2)&0x03];
      C=R[2];P[2]=XPal[C? C:T[1]>>6];
      C=R[3];P[3]=XPal[C? C:(T[1]>>2)&0x03];
      C=R[4];P[4]=XPal[C? C:T[2]>>6];
      C=R[5];P[5]=XPal[C? C:(T[2]>>2)&0x03];
      C=R[6];P[6]=XPal[C? C:T[3]>>6];
      C=R[7];P[7]=XPal[C? C:(T[3]>>2)&0x03];
      R+=8;P+=8;T+=4;
    }
  }
}
  
/** RefreshLine7() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN7, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine7(register byte Y)
{
  register pixel *P;
  register byte C,X,*T,*R;
  byte ZBuf[320];

  P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    ColorSprites(Y,ZBuf);
    if(MSXSpriteLineEmpty)
    {
      // SCREEN7: pixels 512->256, uma amostra por byte.
      T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
#if defined(MSX_ESP32) && defined(BPP8)
      for(int N=0;N<256;N+=4)
        MSXDraw4Native222(P+N,XPal[T[N]>>4],XPal[T[N+1]>>4],XPal[T[N+2]>>4],XPal[T[N+3]>>4]);
#elif defined(MSX_ESP32) && defined(BPP16)
      for(int N=0;N<256;N+=2)
        MSXDrawPair(P+N,XPal[T[N]>>4],XPal[T[N+1]>>4]);
#else
      for(int N=0;N<256;++N) P[N]=XPal[T[N]>>4];
#endif
      return;
    }
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);

    for(X=0;X<32;X++)
    {
#if defined(MSX_ESP32) && defined(BPP8)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        MSXDraw4Native222(P,XPal[T[0]>>4],XPal[T[1]>>4],XPal[T[2]>>4],XPal[T[3]>>4]);
        MSXDraw4Native222(P+4,XPal[T[4]>>4],XPal[T[5]>>4],XPal[T[6]>>4],XPal[T[7]>>4]);
        /* v38: avancar os ponteiros antes do continue, como nas outras
         * profundidades. Sem isto o mesmo bloco era repetido 32 vezes. */
        R+=8;P+=8;T+=8;
        continue;
      }
#elif defined(MSX_ESP32) && defined(BPP16)
      if(!(R[0]|R[1]|R[2]|R[3]|R[4]|R[5]|R[6]|R[7]))
      {
        for(int t=0;t<8;t+=2)
          MSXDrawPair(P+t,XPal[T[t]>>4],XPal[T[t+1]>>4]);
        R+=8;P+=8;T+=8;
        continue;
      }
#endif
      C=R[0];P[0]=XPal[C? C:T[0]>>4];
      C=R[1];P[1]=XPal[C? C:T[1]>>4];
      C=R[2];P[2]=XPal[C? C:T[2]>>4];
      C=R[3];P[3]=XPal[C? C:T[3]>>4];
      C=R[4];P[4]=XPal[C? C:T[4]>>4];
      C=R[5];P[5]=XPal[C? C:T[5]>>4];
      C=R[6];P[6]=XPal[C? C:T[6]>>4];
      C=R[7];P[7]=XPal[C? C:T[7]>>4];
      R+=8;P+=8;T+=8;
    }
  }
}

/** RefreshLineTx80() ****************************************/
/** Refresh line Y (0..191/211) of TEXT80.                  **/
/*************************************************************/
void RefreshLineTx80(register byte Y)
{
  register pixel *P,FC,BC;
  register byte X,M,*T,*C,*G;

  BC=XPal[BGColor];
  P=RefreshBorder(Y,BC);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BC);
  else
  {
    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=P[7]=P[8]=BC;
    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+((Y+VScroll)&0x07);
    T=ChrTab+((80*(Y>>3))&ChrTabM);
    C=ColTab+((10*(Y>>3))&ColTabM);
    P+=9;

    for(X=0,M=0x00;X<80;X++,T++,P+=3)
    {
      if(!(X&0x07)) M=*C++;
      if(M&0x80) { FC=XPal[XFGColor];BC=XPal[XBGColor]; }
      else       { FC=XPal[FGColor];BC=XPal[BGColor]; }
      M<<=1;
      Y=*(G+((int)*T<<3));
      P[0]=Y&0xC0? FC:BC;
      P[1]=Y&0x30? FC:BC;
      P[2]=Y&0x0C? FC:BC;
    }

    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=XPal[BGColor];
  }
}

#endif /* NARROW */
