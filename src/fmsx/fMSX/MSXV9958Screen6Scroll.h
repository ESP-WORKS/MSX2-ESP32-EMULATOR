/* V9958 horizontal scroll for SCREEN 6 (GRAPHIC 5, 512 dots -> 256 VGA samples).
 * v1.06: semantics aligned with openMSX (SDLRasterizer/VDP):
 *  - R#26 (HO8..HO3): coarse offset in units of 8 "256-dot" samples
 *    (= 16 hardware dots in 512-dot modes). Without SP2 only bits 4..0
 *    count and the image wraps inside the page selected by R#2.
 *  - SP2=1 (R#25 bit 0): 512-sample field made of TWO pages:
 *    even page = R#2 page with A15 cleared, odd page = R#2 page.
 *    R#26 bit 5 chooses which of them starts at the left edge.
 *    (v1.04 added +32 KiB to the R#2 page: with R#2=3Fh it read page 2.)
 *  - R#27 (HO2..HO0): the bitmap starts R#27 samples later; those left
 *    samples show the backdrop colour (not wrapped image data).
 *  - MSK (R#25 bit 1) is applied by the caller over the first 8 samples,
 *    covering sprites too, as on the real VDP.
 * Returns the 2-bit SCREEN 6 colour of output sample x, or 0xFF when the
 * sample belongs to the R#27 backdrop gap.  No side effects.
 */
#ifndef MSX_V9958_SCREEN6_SCROLL_H
#define MSX_V9958_SCREEN6_SCROLL_H

#define MSX_V9958_S6_BACKDROP 0xFFu

static inline unsigned MSXV9958Screen6Sample(const unsigned char *vram,
    unsigned vram_size, unsigned row_address,
    unsigned reg26, unsigned reg27, unsigned two_pages, unsigned x)
{
    const unsigned fine = reg27 & 7u;
    if (x < fine) return MSX_V9958_S6_BACKDROP;
    const unsigned span = two_pages ? 512u : 256u;
    const unsigned coarse = (reg26 & (two_pages ? 0x3Fu : 0x1Fu)) << 3;
    const unsigned sx = (x - fine + coarse) & (span - 1u);
    const unsigned odd = row_address;
    const unsigned even = two_pages ? (row_address & ~0x8000u) : row_address;
    const unsigned base = (sx & 256u) ? odd : even;
    const unsigned char b = vram[(base + ((sx & 255u) >> 1)) & (vram_size - 1u)];
    /* Byte = 4 hardware dots; the 256-sample output keeps dots 0 and 2. */
    return (sx & 1u) ? ((b >> 2) & 3u) : (b >> 6);
}
#endif
