// hal.h - fronteira entre o emulador (C puro, msx_glue.c) e o hardware
// (C++ com FabGL/Arduino). Nada aqui depende de ESP32, então o lado C
// também compila no PC.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Geral ---------------------------------------------------------------
int64_t  hal_time_us(void);
void     hal_delay_ms(int ms);
void     hal_log(const char *fmt, ...);
void    *hal_alloc_big(size_t size);           // PSRAM se houver, senão heap normal
void    *hal_alloc_fast(size_t size);          // RAM interna (mais rápida) se sobrar folga, senão hal_alloc_big
void    *hal_alloc_hot(size_t size);
void     hal_set_fast_bios(int enabled);          // msx_fastbios=0 desliga a preferencia por DRAM
void    *hal_alloc_system_rom(size_t size); // BIOS MSX2+ preferencialmente RAM interna, sem sacrificar cartuchos
void    *hal_alloc_rom_cache(size_t size);   // banco LRU 64/128 KiB: apenas RAM interna
void    *hal_alloc_rom(size_t size);           // ROM pequena: tenta RAM interna antes da PSRAM           // memória crítica do emulador: prioriza RAM interna
void     hal_screen_message(const char *l1, const char *l2);   // texto de erro na tela
void     hal_restart(void);

// ---- SD ------------------------------------------------------------------
int      hal_sd_init(void);                    // 1 = montado em /sd
// Lê "chave=valor" de /sd/bootl.rc. Retorna 1 se achou (e copia em out).
int      hal_rc_get(const char *key, char *out, size_t out_size);

// ---- Vídeo ---------------------------------------------------------------
#ifndef MSX_NATIVE_RGB222
#define MSX_NATIVE_RGB222 1
#endif
// Output VGA permanece RGB222 (64 cores) em ambas as configuracoes.
// O modo nativo reduz o framebuffer INTERMEDIARIO de 16 para 8 bits/pixel.
void     hal_video_set_border_rgb222(uint8_t index);
void     hal_video_present_rgb222(const uint8_t *pixels, int w, int h, int stride);
uint8_t *hal_video_async_submit_rgb222(uint8_t *finished, int w, int h, int stride,
                                       int top, int active_h, uint8_t border);
void     hal_video_present_safe_rgb222(const uint8_t *pixels, int w, int h, int stride);

void     hal_video_init(void);
// pixels: RGB565 com bytes trocados (big-endian). Centraliza na tela de 320x240.
void     hal_video_set_row_cache(int enabled);
void     hal_video_row_cache_stats(uint32_t *hits, uint32_t *misses);
void     hal_video_present(const uint16_t *pixels, int w, int h, int stride);
// Pipeline opcional: duas reservas extras de framebuffer; o worker VGA roda no core 1.
// submit bloqueia apenas se os tres buffers estiverem ocupados; nunca sobrescreve
// um quadro ainda sendo apresentado. Em falha de alocacao, caller usa via sincrona.
int      hal_video_async_init(size_t frame_bytes);
uint16_t *hal_video_async_submit(uint16_t *finished, int w, int h, int stride,
                                  int top, int active_h, uint16_t border);
void     hal_video_present_safe(const uint16_t *pixels, int w, int h, int stride);
void     hal_video_async_stats(uint32_t *work_us, uint32_t *wait_us, uint32_t *presented);

// Área ativa da imagem MSX usada pelo stretch 4:3 (normalmente 256x192).
void     hal_video_set_active_area(int top, int height);
// Cor RGB565 com bytes invertidos do backdrop R#7 do VDP, inclusive margens VGA.
void     hal_video_set_border_rgb565(uint16_t color);
// Stretch 4:3 (preenche 320x240 sem deformar a área ativa) e contador de FPS.
void     hal_video_set_stretch(int on);
int      hal_video_get_stretch(void);
void     hal_video_toggle_stretch(void);
// ResetMSX define a cadencia pelo padrao selecionado no menu:
// Teto de execucao: NTSC = ate 60 EPS, PAL = ate 50 EPS.
// A temporizacao nativa do VDP continua em 60/50 Hz respectivamente.
// Saida fisica FabGL QVGA 320x240 @ 60 Hz: ate 60 imagens distintas/s.
void     hal_video_set_system_hz(unsigned hz);
void     hal_video_pace_emu_frame(void);
void     hal_video_toggle_fps(void);
// Atualiza o contador de quadros do emulador para o OSD FPS/EPS.
void     hal_video_set_emu_frames(uint32_t frames);
int      hal_video_fps_enabled(void);
// No OSD Shift+F12: FPS, EPS e porcentagem efetiva do frame skip AUTO.
// auto_mode=0 (0% fixo), auto_mode=1 (0..65% dinamico).
void     hal_video_set_frameskip_state(int auto_mode, int percent);
// Mostra temporariamente FRAME SKIP 0% ou FRAME SKIP AUTO (-1) se FPS estiver OFF.
void     hal_video_show_frameskip(int percent);
// Atalhos de vídeo (Shift+F11 reservado, Shift+F12 = OSD FPS).
int      hal_video_hotkey(int fkey, int shift);

// ---- Entrada (teclado/mouse PS/2) ---------------------------------------
enum {
    HAL_NAV_UP = 1, HAL_NAV_DOWN = 2, HAL_NAV_LEFT = 4, HAL_NAV_RIGHT = 8,
    HAL_NAV_A = 16,        // Enter / Espaço
    HAL_NAV_B = 32,        // Esc / Ctrl
    HAL_HOT_MENU = 64,     // F10 -> menu do fMSX
    HAL_HOT_RESET = 128,   // F12 -> reset (Shift+F12 sem acao)
    HAL_HOT_SKIP = 256,    // Shift+F11 -> alterna frame skip (F11 = FPS/EPS/SKIP)
};
void     hal_input_init(void);
uint32_t hal_input_nav(void);                  // máscara HAL_NAV_* / HAL_HOT_*
void     hal_input_keys(uint8_t keys[256]);    // códigos KBD_* do fMSX
/* -1: internacional, 0: ABNT2 MSX normal, 1: Expert, 2: Hotbit */
void     hal_input_set_keyboard_profile(int profile);
// BIOS carregada em RAM: extrai tabelas e permite patch reversivel Hotbit ABNT2.
void     hal_input_set_keyboard_bios(uint8_t *rom, size_t bytes);
uint32_t hal_input_mouse(void);                // x | y<<8 | botões<<16

// ---- Áudio (mono, 16 bits) ----------------------------------------------
// Taxa de mistura padrao reduzida para 24 kHz: ~25% menos amostras por
// segundo no PSG/SCC/FM mixer; 32 kHz pode ser restaurado no platformio.ini.
#ifndef MSX_PCM_RATE
#define MSX_PCM_RATE 24000
#endif
#define HAL_AUDIO_RATE MSX_PCM_RATE
#define HAL_AUDIO_STREAM_SAMPLES 2560          // ~80 ms
void     hal_audio_init(void);
unsigned hal_audio_free(void);                 // amostras que cabem sem bloquear
unsigned hal_audio_queued_bytes(void);
void     hal_audio_write(const int16_t *samples, unsigned count);  // bloqueia se cheio
// Click mecânico/eletrônico da tecla MSX (transição do bit SOUND da PPI).
void     hal_audio_keyclick(void);
void     hal_audio_keyclick_edge(int rising);

#ifdef __cplusplus
}
#endif
