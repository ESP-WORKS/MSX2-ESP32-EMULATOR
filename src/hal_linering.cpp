// hal_linering.cpp - v1.06b: anel de scanlines na RAM interna.
//
// Ate a v1.06 o renderizador do fMSX (nucleo 0) escrevia cada scanline
// direto no framebuffer da PSRAM: 58 KiB por quadro atravessavam o cache
// de memoria externa do nucleo 0, expulsando codigo/dados do Z80 (bancos
// MegaROM, RAM do MSX, VRAM) e pagando leitura+escrita de cada linha de
// cache de 32 bytes no barramento QSPI. Em jogos pesados esse custo
// aparecia como queda de EPS.
//
// Agora o nucleo 0 desenha cada linha num slot de RAM interna (sem cache
// e sem barramento externo) e uma tarefa no nucleo 1 copia o slot para o
// framebuffer da PSRAM. PutImage() espera o anel esvaziar antes de
// entregar o quadro ao conversor VGA, portanto a imagem e identica.
// msx_linering=0 no bootl.rc volta ao caminho antigo.
#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal.h"

#define LR_SLOTS   32u                 // potencia de 2
#define LR_PAD     16u                 // R#18 (HAdjust) -7..+8 escreve fora dos 256
#define LR_STRIDE  (256u + 2u * LR_PAD)
#define LR_NOTIFY_EVERY 8u

static uint8_t *lr_ring;
static uint8_t *lr_dst[LR_SLOTS];
static volatile uint32_t lr_head;      // escrito somente pelo nucleo 0
static volatile uint32_t lr_tail;      // escrito somente pelo nucleo 1
static uint32_t lr_pending;            // slot adquirido e ainda nao publicado
static TaskHandle_t lr_task;
static volatile uint32_t lr_full_waits;
static bool lr_ready;

static void lr_worker(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, 1);
        for (;;) {
            const uint32_t head = lr_head;
            uint32_t tail = lr_tail;
            if (tail == head) break;
            __sync_synchronize();                     // dados do slot visiveis
            while (tail != head) {
                const uint32_t s = tail & (LR_SLOTS - 1u);
                memcpy(lr_dst[s], lr_ring + s * LR_STRIDE + LR_PAD, 256);
                ++tail;
            }
            __sync_synchronize();
            lr_tail = tail;
        }
    }
}

extern "C" int hal_lr_init(void)
{
    if (lr_ready) return 1;
    /* v1.08: a RAM interna e escassa (framebuffer FabGL + pilhas). Nao
     * consumir a reserva do cache MegaROM nem das pilhas das tarefas. */
    const size_t need = LR_SLOTS * LR_STRIDE;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < need + 120 * 1024) {
        hal_log("Line ring: RAM interna insuficiente (%uKiB livres), desligado\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
        return 0;
    }
    lr_ring = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lr_ring) {
        hal_log("Line ring: sem RAM interna, renderizacao direta na PSRAM\n");
        return 0;
    }
    lr_head = lr_tail = 0;
    lr_pending = 0;
    // Prioridade acima do worker VGA (4) e abaixo do audio (7), nucleo 1.
    if (xTaskCreatePinnedToCore(lr_worker, "msx_lines", 2048, NULL, 6,
                                &lr_task, 1) != pdPASS) {
        free(lr_ring);
        lr_ring = nullptr;
        return 0;
    }
    lr_ready = true;
    hal_log("Line ring: %u linhas em RAM interna, copia para PSRAM no core 1\n",
            (unsigned)LR_SLOTS);
    return 1;
}

extern "C" int hal_lr_enabled(void) { return lr_ready; }

static inline void lr_publish(void)
{
    if (!lr_pending) return;
    lr_pending = 0;
    __sync_synchronize();
    const uint32_t h = lr_head + 1u;
    lr_head = h;
    if (!(h & (LR_NOTIFY_EVERY - 1u))) xTaskNotifyGive(lr_task);
}

// Publica a linha anterior e devolve o slot (ja deslocado pelo padding)
// onde a proxima linha deve ser desenhada. dst = linha no framebuffer.
extern "C" uint8_t *hal_lr_acquire(uint8_t *dst)
{
    lr_publish();
    while (lr_head - lr_tail >= LR_SLOTS) {          // anel cheio: raro
        ++lr_full_waits;
        xTaskNotifyGive(lr_task);
        taskYIELD();
    }
    const uint32_t s = lr_head & (LR_SLOTS - 1u);
    lr_dst[s] = dst;
    lr_pending = 1;
    return lr_ring + s * LR_STRIDE + LR_PAD;
}

// Garante que todas as linhas chegaram ao framebuffer da PSRAM.
extern "C" void hal_lr_flush(void)
{
    if (!lr_ready) return;
    lr_publish();
    if (lr_tail == lr_head) return;
    xTaskNotifyGive(lr_task);
    while (lr_tail != lr_head) taskYIELD();
    __sync_synchronize();
}

extern "C" uint32_t hal_lr_full_waits(void) { return lr_full_waits; }
