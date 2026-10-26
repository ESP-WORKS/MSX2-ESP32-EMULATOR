// main.cpp - inicialização, tarefa do emulador e contador de FPS no serial
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "hal.h"

extern "C" void msx_run(void);
extern "C" void msx_stats(unsigned *emu_frames, unsigned *drawn_frames, const char **where);
extern "C" void msx_profile_stats(uint32_t *video_us, uint32_t *sound_us, uint32_t *pacing_us);
extern "C" unsigned msx_mapper_write_count(void);
extern "C" void msx_mapper_profile_enable(int enabled);
extern "C" void MSXRomCacheStats(unsigned *hits,unsigned *misses,unsigned *copies,
                                  unsigned *evictions,unsigned *pages);
extern "C" unsigned int msx_audio_active_mask(void);
extern "C" int msx_scc_audio_enabled(void);

static bool emu_started;
static bool show_fps = false;       // msx_fps=1 no bootl.rc liga o log de FPS no serial
static bool show_profile = false;   // msx_profile=1 detalha % do quadro em cada subsistema

// O emulador roda no núcleo 0; vídeo (ISR da FabGL) e áudio ficam no núcleo 1.
static void emu_task(void *)
{
    msx_run();                       // só retorna em caso de erro (a mensagem já está na tela)
    for (;;) vTaskDelay(portMAX_DELAY);
}

void setup()
{
    setCpuFrequencyMhz(240);         // garante 240 MHz (sem efeito se já estiver)
    disableCore0WDT();               // o emulador ocupa o núcleo 0 quase o tempo todo
    disableLoopWDT();
    Serial.begin(115200);
    hal_log("\nMSX2+ FAST VGA32 v38.5 | 512 KiB RAM | NTSC 60 Hz | MegaROM LRU\n");

       // Apaga a otadata para o bootloader voltar a partir da primeira partição de app.
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }

    const int sd_ok = hal_sd_init();  // antes de tudo: o bootl.rc define os pinos do PS/2
    hal_video_init();
    if (!sd_ok) {
        hal_screen_message("Cartao SD nao encontrado",
                           "Use FAT32 com MSX2P.ROM e MSX2PEXT.ROM em /MSX 2+ FAST/ROMS/");
        return;
    }
    char v[8];
    if (hal_rc_get("msx_fps", v, sizeof(v))) show_fps = atoi(v) != 0;
    if (hal_rc_get("msx_profile", v, sizeof(v))) show_profile = atoi(v) != 0;
    // Atualiza o contador do mapper apenas no modo diagnostico.
    msx_mapper_profile_enable(show_profile ? 1 : 0);
    // msx_vpar foi removido: a saída VGA atual usa a implementação FabGL padrão.
    hal_input_init();
    hal_audio_init();
    emu_started = true;
    xTaskCreatePinnedToCore(emu_task, "msx", 16384, NULL, 5, NULL, 0);
}

// Roda no núcleo 1, separado do emulador: continua imprimindo mesmo se o emulador travar.
void loop()
{
    static unsigned last_emu, last_drawn;
    static int64_t last_t;
    static uint32_t last_vga, last_sound, last_pacing;
    static uint32_t last_vga_worker, last_vga_wait, last_vga_presented;
    static uint32_t last_mapper_writes;

    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!emu_started || (!show_fps && !show_profile)) return;

    unsigned emu, drawn;
    const char *where;
    msx_stats(&emu, &drawn, &where);
    const int64_t now = hal_time_us();
    const int64_t dt = last_t ? now - last_t : 1000000;
    last_t = now;

    const unsigned emu10 = (unsigned)((emu - last_emu) * 10000000ULL / dt);
    const unsigned draw10 = (unsigned)((drawn - last_drawn) * 10000000ULL / dt);
    const bool stalled = emu == last_emu;
    last_emu = emu;
    last_drawn = drawn;

    if (show_fps) {
        hal_log("FPS emu %u.%u  tela %u.%u  audio %u%%  onde=%s  ram=%uK psram=%uK%s\n",
                emu10 / 10, emu10 % 10, draw10 / 10, draw10 % 10,
                hal_audio_queued_bytes() * 100 / (HAL_AUDIO_STREAM_SAMPLES * 2), where,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                stalled ? "  <-- PARADO" : "");
    }
    if (show_profile) {
        uint32_t vga, sound, pacing;
        msx_profile_stats(&vga, &sound, &pacing);
        // Differential durations (milliseconds per second; sums may overlap other cores).
        hal_log("PROFILE ms/s VGA_MAIN=%u AUDIO=%u PACE=%u (intervalo=%u ms)\n",
                (unsigned)((vga - last_vga) / 1000),
                (unsigned)((sound - last_sound) / 1000),
                (unsigned)((pacing - last_pacing) / 1000),
                (unsigned)(dt / 1000));
        uint32_t worker_us, wait_us, presented;
        hal_video_async_stats(&worker_us, &wait_us, &presented);
        hal_log("PROFILE VGA async: render=%u ms/s, aguardo=%u ms/s, "
                "presentados=%u/s (0 significa sincrono)\n",
                (unsigned)((worker_us - last_vga_worker) / 1000),
                (unsigned)((wait_us - last_vga_wait) / 1000),
                (unsigned)(presented - last_vga_presented));
        last_vga_worker = worker_us;
        last_vga_wait = wait_us;
        last_vga_presented = presented;
        uint32_t hit, miss;
        hal_video_row_cache_stats(&hit, &miss);
        static uint32_t last_hit = 0, last_miss = 0;
        const uint32_t dh = hit - last_hit, dm = miss - last_miss;
        hal_log("PROFILE VGA cache: %u%% reaproveitadas (%u/%u linhas)\n",
                (unsigned)(dh + dm ? (100u * dh) / (dh + dm) : 0u),
                (unsigned)dh, (unsigned)(dh + dm));
        last_hit = hit;
        last_miss = miss;
        const uint32_t map_writes = msx_mapper_write_count();
        hal_log("PROFILE MegaROM: escritas em espaco mapper=%u/s (candidatas, nao bancos reais)\n",
                (unsigned)(map_writes - last_mapper_writes));
        last_mapper_writes = map_writes;
        static unsigned old_ch=0,old_cm=0,old_cc=0,old_ce=0;
        unsigned ch,cm,cc,ce,cp;
        MSXRomCacheStats(&ch,&cm,&cc,&ce,&cp);
        const unsigned cache_hits_delta=ch-old_ch, cache_misses_delta=cm-old_cm;
        hal_log("PROFILE MegaROM LRU: %u KiB, hits=%u misses=%u acertos=%u%%, "
                "copias8K=%u/s, evictions=%u/s\n",
                cp*8u,cache_hits_delta,cache_misses_delta,
                (cache_hits_delta+cache_misses_delta)?
                (100u*cache_hits_delta/(cache_hits_delta+cache_misses_delta)):0u,
                cc-old_cc,ce-old_ce);
        old_ch=ch;old_cm=cm;old_cc=cc;old_ce=ce;
        const uint32_t active_voices = msx_audio_active_mask();
        hal_log("PROFILE AUDIO vozes PSG=%u SCC=%u FM=%u; SCC audio=%s\n",
                (unsigned)__builtin_popcount(active_voices & 0x3Fu),
                (unsigned)__builtin_popcount((active_voices >> 6) & 0x1Fu),
                (unsigned)__builtin_popcount((active_voices >> 11) & 0x1FFu),
                msx_scc_audio_enabled() ? "ON" : "OFF");
        /* Tempo residual no nucleo da emulacao. Inclui Z80, VDP, IO,
           input e overhead; nao separa os subsistemas individualmente. */
        const int64_t used = (int64_t)(vga-last_vga) +
                             (int64_t)(sound-last_sound) +
                             (int64_t)(pacing-last_pacing);
        const int64_t cpu_vdp_io = dt > used ? dt - used : 0;
        hal_log("PROFILE CORE0 residual CPU+VDP+IO~= %u ms/s\n",
                (unsigned)(cpu_vdp_io / 1000));
        last_vga = vga;
        last_sound = sound;
        last_pacing = pacing;
    }
}