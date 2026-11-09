/* MSX2+ opening guide sprites: display-only cleanup for the supplied Kanji ROM.
 * The ROM places two paired sprite timing guides at the sides (data at 7AB2h)
 * and eventually writes R#25=00h, R#2=1Fh after the horizontal animation.
 * The collision engine must still see those sprites, but after the ROM's
 * own finishing register sequence they must no longer be rendered over the
 * completed logo/background. Leave VRAM, VDP registers and collision unchanged.
 * Restrict this to the MSX2+ boot window (checked by the integration caller).
 */
#ifndef MSX_BOOT_GUIDE_CLEANUP_H
#define MSX_BOOT_GUIDE_CLEANUP_H

typedef struct {
    unsigned char saw_two_page_scroll;
    unsigned char saw_end_scroll;
    unsigned char hide_guides;
} MSXBootGuideState;

static void MSXBootGuideReset(MSXBootGuideState *state)
{
    state->saw_two_page_scroll=0;
    state->saw_end_scroll=0;
    state->hide_guides=0;
}

/* Returns 1 only when the guides transition from visible to hidden. */
static int MSXBootGuideObserve(MSXBootGuideState *state,
    unsigned reg, unsigned value, unsigned reg26, unsigned reg27)
{
    if(state->hide_guides) return 0;
    if(reg==25u)
    {
        if((value&3u)==3u) {
            state->saw_two_page_scroll=1;
            state->saw_end_scroll=0;
        } else if((value&3u)==0u && state->saw_two_page_scroll) {
            state->saw_end_scroll=1;
        }
    }
    if(reg==2u && value==0x1Fu && state->saw_end_scroll &&
       reg26==0u && reg27==0u)
    {
        state->hide_guides=1;
        return 1;
    }
    return 0;
}
#endif
