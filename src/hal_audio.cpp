// hal_audio.cpp - áudio mono 16 bits -> DAC interno do ESP32 (GPIO25) via I2S0
#include <Arduino.h>
#include "driver/i2s.h"
#include "freertos/stream_buffer.h"
#include "hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLOCK 64                                           // amostras por bloco (2 ms)
#define STREAM_BYTES (HAL_AUDIO_STREAM_SAMPLES * sizeof(int16_t))

static StreamBufferHandle_t pcm;
static int16_t pending[BLOCK];
static unsigned pending_count;

// MSX keyboard click state. The real machine does NOT generate a 1.8 kHz
// square-wave tone. PPI port C bit 7 is a 1-bit DAC: every transition produces
// a short, strongly attenuated transient. MAME models this as DAC_1BIT output.
// We emulate the audible result as a signed decaying impulse, with a stronger
// attack on 0->1 and a weaker opposite-polarity transient on 1->0.
static portMUX_TYPE click_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile unsigned click_left = 0;
static volatile int click_level = 0;
static volatile unsigned click_amp = 0;
static const unsigned CLICK_SAMPLES = (HAL_AUDIO_RATE * 8) / 1000; // ~8 ms
static const unsigned CLICK_ATTACK = 5200;
static const unsigned CLICK_RELEASE = 3000;

extern "C" void hal_audio_keyclick(void)
{
    // Kept for source compatibility: a normal keyboard transition is a rising
    // click in the common BIOS sequence. The falling edge uses the weaker path.
    portENTER_CRITICAL(&click_mux);
    click_left = CLICK_SAMPLES;
    click_level = 1;
    click_amp = CLICK_ATTACK;
    portEXIT_CRITICAL(&click_mux);
}

extern "C" void hal_audio_keyclick_edge(int rising)
{
    portENTER_CRITICAL(&click_mux);
    click_left = CLICK_SAMPLES;
    click_level = rising ? 1 : -1;
    click_amp = rising ? CLICK_ATTACK : CLICK_RELEASE;
    portEXIT_CRITICAL(&click_mux);
}

static void mix_keyclick(int *samples, unsigned count)
{
    portENTER_CRITICAL(&click_mux);
    unsigned left = click_left;
    unsigned amp = click_amp;
    int sign = click_level;

    for (unsigned i = 0; i < count && left; ++i) {
        samples[i] += sign * (int)amp;
        // Approximate the analog attenuation of the original 1-bit click.
        // A multiplicative decay preserves the broadband click instead of
        // imposing an artificial audible pitch.
        amp = (amp * 245u) >> 8;
        if (amp < 32) amp = 0;
        --left;
    }

    click_left = left;
    click_amp = amp;
    portEXIT_CRITICAL(&click_mux);
}


// Consumidor: entrega um bloco por vez ao DMA. O i2s_write bloqueia e dita o ritmo.
// Se faltar PCM, faz fade até o silêncio (sem estalo) e retoma com crossfade.
static void audio_task(void *)
{
    int16_t mono[BLOCK];
    int mix[BLOCK];
    uint16_t out[BLOCK * 2];                         // L,R intercalados, sem sinal
    int last = 0;
    bool recovering = false;
    for (;;) {
        size_t bytes = xStreamBufferReceive(pcm, mono, sizeof(mono), 0);
        size_t count = bytes / sizeof(int16_t);

        if (!count) {
            for (unsigned i = 0; i < BLOCK; ++i)
                mix[i] = last * (BLOCK - 1 - (int)i) / BLOCK;
            mix_keyclick(mix, BLOCK);
            for (unsigned i = 0; i < BLOCK; ++i)
                out[2 * i] = out[2 * i + 1] = (uint16_t)(mix[i] + 32768);
            last = 0;
            recovering = true;
            count = BLOCK;
        } else {
            for (unsigned i = 0; i < count; ++i) {
                int s = mono[i];
                if (recovering)
                    s = (last * (int)(count - 1 - i) + s * (int)(i + 1)) / (int)count;
                mix[i] = s;
            }
            mix_keyclick(mix, (unsigned)count);
            for (unsigned i = 0; i < count; ++i) {
                int s = mix[i];
                s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
                out[2 * i] = out[2 * i + 1] = (uint16_t)(s + 32768);
            }
            recovering = false;
            last = mono[count - 1];
        }
        size_t written;
        i2s_write(I2S_NUM_0, out, count * 2 * sizeof(uint16_t), &written, portMAX_DELAY);
    }
}

extern "C" void hal_audio_init(void)
{
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = HAL_AUDIO_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 128;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, NULL);                          // NULL = DAC interno
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);            // só GPIO25 (GPIO26 é pino do PS/2)
    i2s_zero_dma_buffer(I2S_NUM_0);

    pcm = xStreamBufferCreate(STREAM_BYTES, BLOCK * sizeof(int16_t));
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 7, NULL, 1);
}

extern "C" unsigned hal_audio_free(void)
{
    return xStreamBufferSpacesAvailable(pcm) / sizeof(int16_t) + (BLOCK - pending_count);
}

extern "C" unsigned hal_audio_queued_bytes(void)
{
    return xStreamBufferBytesAvailable(pcm);
}

// Acumula em blocos de 64 amostras; bloqueia quando o buffer enche (é o relógio do emulador).
extern "C" void hal_audio_write(const int16_t *samples, unsigned count)
{
    // Batch copies: same 64-sample frame boundaries and identical PCM payload,
    // without a per-sample branch/copy in the Z80 emulation task.
    while (count) {
        unsigned space = BLOCK - pending_count;
        unsigned n = count < space ? count : space;
        memcpy(pending + pending_count, samples, n * sizeof(samples[0]));
        pending_count += n;
        samples += n;
        count -= n;
        if (pending_count == BLOCK) {
            xStreamBufferSend(pcm, pending, sizeof(pending), portMAX_DELAY);
            pending_count = 0;
        }
    }
}
