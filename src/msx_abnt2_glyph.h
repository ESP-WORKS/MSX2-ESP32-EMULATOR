#ifndef MSX_ABNT2_GLYPH_H
#define MSX_ABNT2_GLYPH_H
/* O caractere ASCII 27h e apóstrofo, mas varias BIOS MSX o desenham
 * como acento agudo: 10 20 40. Corrigir apenas a fonte de video da BIOS,
 * se e somente se o bitmap for exatamente o conhecido; sem mexer na ROM
 * nem no ASCII de entrada. Disponivel em C e C++ para teste de regressao. */
#include <stdint.h>
#include <string.h>
static const uint8_t msx_abnt2_quote_old[8] = {0x10,0x20,0x40,0,0,0,0,0};
static const uint8_t msx_abnt2_quote_new[8] = {0x30,0x30,0x20,0x40,0,0,0,0};
static inline void msx_abnt2_update_quote_glyph(uint8_t *glyph,int br_on)
{
    if(!glyph) return;
    if(br_on && !memcmp(glyph,msx_abnt2_quote_old,8))
        memcpy(glyph,msx_abnt2_quote_new,8);
    else if(!br_on && !memcmp(glyph,msx_abnt2_quote_new,8))
        memcpy(glyph,msx_abnt2_quote_old,8);
}
#endif
