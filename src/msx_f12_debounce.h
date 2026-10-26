#ifndef MSX_F12_DEBOUNCE_H
#define MSX_F12_DEBOUNCE_H

#include <stdint.h>

/* Anti-glitch de PS/2: F11 (OSD ou Frame Skip) e F12 (reset) devem
 * produzir UMA borda por pressionamento fisico. Uma leitura isolada de 'solto' durante uma tecla
 * mantida nao pode armar outro toggle do OSD. Rearma apenas depois de
 * 100 ms CONTINUOS de tecla liberada; millis() pode atravessar overflow. */
typedef struct {
    uint32_t release_ms;
    uint8_t was_down;
    uint8_t armed;
} MSXF12Debounce;

static inline int msx_f12_edge(MSXF12Debounce *state, int is_down, uint32_t now_ms)
{
    if (is_down) {
        const int edge = !state->was_down && state->armed;
        state->was_down = 1;
        state->armed = 0;
        return edge;
    }
    if (state->was_down) {
        state->was_down = 0;
        state->release_ms = now_ms;
    } else if (!state->armed && (uint32_t)(now_ms - state->release_ms) >= 100u) {
        state->armed = 1;
    }
    return 0;
}

#endif
