#include <assert.h>
#include <stdio.h>
#include "../src/fmsx/fMSX/MSXBootGuideCleanup.h"
int main(void) {
    MSXBootGuideState s;
    MSXBootGuideReset(&s);
    assert(!s.hide_guides);
    assert(!MSXBootGuideObserve(&s,2,0x1f,0,0)); /* ordinary BIOS */
    assert(!MSXBootGuideObserve(&s,25,3,0,0)); /* two-page scroll starts */
    assert(!MSXBootGuideObserve(&s,2,0x3f,0,0));
    assert(!MSXBootGuideObserve(&s,25,0,12,3)); /* finish scroll */
    assert(!MSXBootGuideObserve(&s,2,0x1f,12,3)); /* not yet zero */
    assert(MSXBootGuideObserve(&s,2,0x1f,0,0)); /* actual ROM sequence */
    assert(s.hide_guides);
    assert(!MSXBootGuideObserve(&s,2,0x1f,0,0)); /* one-time latch */
    MSXBootGuideReset(&s);
    assert(!s.hide_guides && !s.saw_two_page_scroll);
    assert(!MSXBootGuideObserve(&s,25,0,0,0));
    assert(!MSXBootGuideObserve(&s,2,0x1f,0,0));
    assert(!s.hide_guides);
    puts("v1.05 boot sprite guides: lifecycle, end markers, no false positives: PASS");
    return 0;
}
