#ifndef MSX_FRAMESKIP_AUTO_H
#define MSX_FRAMESKIP_AUTO_H
/* Controlador do frame skip AUTO. Sem chamadas a HAL ou alocacoes:
 * executado pelo core de emulacao uma vez por quadro concluido.
 * UPeriod (fMSX) continua decidindo quais quadros sao desenhados.
 * A medicao de EPS usa tempo real, NAO o numero de frames desenhados.
 */
#include <stdint.h>

#define MSX_AUTO_FS_WINDOW_US 1200000LL
#define MSX_AUTO_FS_MAX_SKIP 65

typedef struct {
    int skip_percent;
    unsigned target_hz;
    unsigned stable_windows;
    uint32_t emulated_frames;
    uint64_t pacing_us;
    int64_t started_us;
} MSXAutoFrameSkip;

static inline void msx_auto_fs_reset(MSXAutoFrameSkip *s)
{
    s->skip_percent = 0;
    s->target_hz = 0;
    s->stable_windows = 0;
    s->emulated_frames = 0;
    s->pacing_us = 0;
    s->started_us = 0;
}

/* Retorna -1 se nao mudou a porcentagem; senao a nova porcentagem.
 * 'paced_us' mede exclusivamente a espera do limitador de EPS.
 * Uma porcentagem exata para todos os jogos nao e calculavel a priori:
 * procuramos a menor taxa suficiente, com histerese e adaptacao lenta
 * para baixo para evitar oscillacao PAL/NTSC ou mudancas de cena.
 */
static inline int msx_auto_fs_tick(MSXAutoFrameSkip *s,
                                   int64_t now_us, unsigned target_hz,
                                   uint32_t paced_us)
{
    if (target_hz != 50u && target_hz != 60u) target_hz = 60u;
    if (!s->started_us || s->target_hz != target_hz ||
        now_us < s->started_us) {
        msx_auto_fs_reset(s);
        s->started_us = now_us;
        s->target_hz = target_hz;
        return -1;
    }
    ++s->emulated_frames;
    s->pacing_us += paced_us;
    const int64_t elapsed = now_us - s->started_us;
    if (elapsed < MSX_AUTO_FS_WINDOW_US) return -1;

    const uint64_t rate_milli = (uint64_t)s->emulated_frames * 1000000000ULL /
                                (uint64_t)(elapsed ? elapsed : 1);
    const uint64_t goal_milli = (uint64_t)target_hz * 1000ULL;
    const uint64_t idle_pct_x100 = s->pacing_us * 10000ULL / (uint64_t)elapsed;
    int next = s->skip_percent;

    /* Abaixo do alvo com pouca espera => renderizacao/cpu nao acompanha.
     * Descarta progressivamente mais quadros, no maximo 8% por janela. */
    if (rate_milli + 850 < goal_milli && idle_pct_x100 < 500ULL) {
        const uint64_t missing = (goal_milli - rate_milli + 999ULL) / 1000ULL;
        int step = (int)(missing > 8ULL ? 8ULL : missing);
        if (step < 1) step = 1;
        next += step;
        if (next > MSX_AUTO_FS_MAX_SKIP) next = MSX_AUTO_FS_MAX_SKIP;
        s->stable_windows = 0;
    }
    /* Quando atinge o alvo com tempo ocioso, tenta renderizar mais.
     * Exige duas janelas consecutivas para evitar 0/1% intermitente. */
    else if (rate_milli + 350 >= goal_milli && idle_pct_x100 >= 800ULL) {
        if (++s->stable_windows >= 2) {
            const int step = idle_pct_x100 >= 2200ULL ? 3 :
                             idle_pct_x100 >= 1400ULL ? 2 : 1;
            next -= step;
            if (next < 0) next = 0;
            s->stable_windows = 0;
        }
    } else {
        s->stable_windows = 0;
    }

    s->emulated_frames = 0;
    s->pacing_us = 0;
    s->started_us = now_us;
    if (next == s->skip_percent) return -1;
    s->skip_percent = next;
    return next;
}
#endif
