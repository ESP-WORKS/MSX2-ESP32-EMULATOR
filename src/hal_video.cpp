// hal_video.cpp - saída VGA 320x240 (64 cores) pela FabGL
// Recursos: stretch 4:3 (256x192 -> 320x240) e contador de FPS (toggle externo).
// Otimizações do caminho quente (hal_video_present):
//   - conversão RGB565 -> pixel bruto direto do valor com bytes trocados (sem bswap);
//   - conversão e escala horizontal numa passada só (sem linha intermediária);
//   - escrita de 4 pixels por vez em palavras de 32 bits;
//   - linhas de destino repetidas (stretch vertical) são copiadas com memcpy;
//   - funções quentes compiladas com -O3 (o Arduino usa -Os por padrão);
//   - caminho dedicado 256 -> 320 (o caso do MSX): cada pixel de origem é convertido uma
//     única vez e a razão 5:4 é desenrolada, sem tabela de colunas.
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fabgl.h"
#include "hal.h"
#include "board.h"

using namespace fabgl;

#define HOT __attribute__((optimize("O3")))

static VGAController video;
// VGA RGB222 mantida: dois buffers nativos FabGL, flip no VSync.
static bool vga_double_buffered = false;
static fabgl::Canvas *vga_swap_canvas = nullptr;
static uint8_t lut_r[32], lut_gb[64 * 32], lut222_raw[64], sync_bits, px_yellow, px_osd_bg;
/* v38.1 - corrigido estouro de 4432 bytes em dram0_0_seg.
 * A LUT 64x64xuint16 de v38 reservava 8192 bytes permanentes de DRAM.
 * Usar apenas a LUT RGB222 de 64 bytes, gerada conforme o pixel bruto da
 * FabGL. Dois pixels são montados diretamente em um registrador de 16 bits;
 * nenhum heap/PSRAM extra, nenhuma mudança nas cores ou bits de sync.
 * Mantem os demais caminhos rápidos v38 (SCREEN 1/2/4/7, SCC e OSD). */
static inline __attribute__((always_inline)) uint16_t rgb222_pair(uint8_t a, uint8_t b)
{
    return (uint16_t)lut222_raw[a & 63] |
           ((uint16_t)lut222_raw[b & 63] << 8);
}

static int vw, vh;
static bool video_ready;
// Compartilhado pelo worker VGA e pelos atalhos que alteram o OSD.
static SemaphoreHandle_t video_render_mutex;
static bool video_async_ready;
// A cor de borda e o RGB565 do VDP (mesmo byte swap do framebuffer).
// Nao depende da cor 0, que pode ser transparente nos TMS99x8.
static uint16_t border_rgb565_swapped = 0;
static uint8_t border_rgb222_index = 0;
static bool border_is_rgb222 = false;
static uint8_t border_raw = 0;
static bool border_dirty = true;
/* Cada backbuffer tem sua propria borda persistente. O endereco do scanline
 * zero distingue os dois buffers mesmo se o Canvas alternar a ordem.
 * Invalidar ao mudar cor, geometria ou overlay impede artefatos residuais. */
static uint8_t *border_last_buffer[2] = {nullptr, nullptr};
static uint8_t border_last_color[2] = {0, 0};
static unsigned border_last_epoch[2] = {0, 0};
static unsigned border_epoch = 1;
static void invalidate_border_buffers()
{
    border_dirty = true;
    ++border_epoch;
    if (!border_epoch) {
        border_epoch = 1;
        border_last_epoch[0] = border_last_epoch[1] = 0;
    }
}
extern "C" void hal_video_set_border_rgb565(uint16_t color)
{
    if (border_rgb565_swapped != color || border_is_rgb222) {
        border_rgb565_swapped = color;
        border_is_rgb222 = false;
        invalidate_border_buffers();
    }
}
extern "C" void hal_video_set_border_rgb222(uint8_t index)
{
    index &= 63;
    if (border_rgb222_index != index || !border_is_rgb222) {
        border_rgb222_index = index;
        border_is_rgb222 = true;
        invalidate_border_buffers();
    }
}


// ---------------------------------------------------------------------------
// Stretch
// ---------------------------------------------------------------------------
#define VMAX_W  320                       // largura máxima do viewport
#define VMAX_H  240                       // altura máxima do viewport

static volatile bool stretch = false;     // modo normal 1:1 por padrão
static uint16_t xmap[VMAX_W];             // coluna de destino -> coluna de origem
static uint16_t ymap[VMAX_H];             // linha de destino -> linha de origem (stretch)
static uint32_t converted_row[VMAX_W];
// Cache exato de linhas de origem por linha VGA: economiza conversão de cores
// em quadros estáticos. Snapshot em PSRAM; nenhuma suposição baseada em hash.
static uint16_t *row_snapshot = nullptr;
static uint8_t row_valid[VMAX_H];
static bool row_cache_enabled = false; // scroll: comparacao PSRAM custava mais que conversao
static uint32_t row_cache_hits = 0, row_cache_misses = 0;
extern "C" void hal_video_set_row_cache(int enabled) {
    row_cache_enabled = enabled != 0;
    memset(row_valid, 0, sizeof(row_valid));
}
extern "C" void hal_video_row_cache_stats(uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = row_cache_hits;
    if (misses) *misses = row_cache_misses;
}     // pixels FabGL já convertidos da linha fonte

// Parâmetros do mapeamento atual (recalculados quando w/h/stretch mudam).
static int map_w = -1, map_h = -1;
static bool map_stretch;
static int map_dw, map_dh, map_x0, map_y0, map_sy0, map_sx0;

// No MSX, o framebuffer tem 228 linhas, mas a tela normal é 256x192.
// O stretch 4:3 usa somente a área ativa; assim 256x192 -> 320x240
// tem exatamente o mesmo fator 1,25 nos dois eixos.
static int active_top = 18;
static int active_height = 192;

extern "C" void hal_video_set_active_area(int top, int height)
{
    if (height <= 0) return;
    if (top < 0) top = 0;
    if (active_top != top || active_height != height) {
        active_top = top;
        active_height = height;
        map_w = -1;
        map_h = -1;
    }
}


static void rebuild_maps(int w, int h)
{
    map_w = w; map_h = h; map_stretch = stretch;
    memset(row_valid, 0, sizeof(row_valid));
    if (stretch) {
        map_dw = vw; map_dh = vh;
        map_sx0 = 0;
        // Stretch 4:3: 256x192 -> 320x240.  Não estique as 18 linhas
        // de borda do framebuffer 256x228, pois elas não fazem parte da
        // área gráfica e são a causa da deformação vertical.
        const int src_top = active_top < h ? active_top : 0;
        const int available_h = h - src_top;
        const int src_h = active_height < available_h ? active_height : available_h;
        map_sy0 = src_top;
        for (int x = 0; x < map_dw; ++x)
            xmap[x] = (uint16_t)(((2 * x + 1) * w) / (2 * map_dw));
        for (int y = 0; y < map_dh; ++y)
            ymap[y] = (uint16_t)(src_top + (((2 * y + 1) * src_h) / (2 * map_dh)));
    } else {
        // Modo normal: pixels 1:1 e área ativa centralizada. O framebuffer fMSX
        // inclui bordas verticais (256x228); mostrá-las como se fossem área ativa
        // torna a imagem visualmente achatada. Exibe 256x192 (ou 256x212) sem
        // redimensionar, preservando a proporção original do MSX.
        map_dw = w < vw ? w : vw;
        const int src_top = (active_top >= 0 && active_top < h) ? active_top : 0;
        const int available_h = h - src_top;
        const int src_h = active_height < available_h ? active_height : available_h;
        map_dh = src_h < vh ? src_h : vh;
        map_sy0 = src_top + ((src_h - map_dh) / 2);
        map_sx0 = (w - map_dw) / 2;
        for (int x = 0; x < map_dw; ++x)
            xmap[x] = (uint16_t)(x + map_sx0);
    }
    map_x0 = (vw - map_dw) / 2;
    map_y0 = (vh - map_dh) / 2;

    // Mudanca de geometria invalida as duas bordas persistentes.
    invalidate_border_buffers();
}

extern "C" void hal_video_set_stretch(int on) { stretch = on != 0; }
extern "C" int  hal_video_get_stretch(void)   { return stretch; }
extern "C" void hal_video_toggle_stretch(void) { stretch = !stretch; }

// Frequencia da maquina selecionada no menu Hardware Model. A cadencia
// emulada NAO e o refresh fisico VGA: FabGL permanece em 320x240 @ 60Hz.
// Como PutImage() ocorre no maximo uma vez por quadro do fMSX,
// limitar EPS tambem limita FPS logicos do emulador. NTSC agora aceita
// ate 60 ciclos/s em NTSC, com PAL limitado a 50 ciclos/s.
/* Teto de processamento: NTSC 60, PAL 50. */
static unsigned emulation_hz = 60;
// O primeiro ajuste (inclusive NTSC=60) deve zerar a cadencia do boot.
static bool system_hz_initialized = false;
static int64_t pace_deadline_us = 0;
static unsigned pace_previous_rate = 0;
static void reset_fps_measurement(void);
extern "C" void msx_frameskip_on_timing_reset(void);

// Chamado por ResetMSX() depois de aprovar o novo modo PAL/NTSC, inclusive
// quando a opcao e alterada no menu. Reinicia os prazos e a janela do OSD;
// evita carregar o ultimo prazo de 60 Hz para um sistema agora em 50 Hz.
extern "C" void hal_video_set_system_hz(unsigned hz)
{
    if (hz != 50 && hz != 60) return;
    /* Reiniciar tambem se o Hz for o mesmo: trocar o modelo, a BIOS,
     * apertar F12 ou reselecionar NTSC/PAL invalida o prazo antigo.
     * Antes havia retorno antecipado que conservava a janela anterior. */
    system_hz_initialized = true;
    emulation_hz = (hz == 60 ? 60u : 50u);
    pace_deadline_us = 0;
    pace_previous_rate = 0;
    // Ao trocar a regiao/modelo, recalcular AUTO sem aproveitar o skip anterior.
    msx_frameskip_on_timing_reset();
    /* O OSD e produzido no nucleo VGA; proteger o reset dos contadores
     * quando o worker paralelo estiver operando. */
#ifdef ARDUINO
    if (video_async_ready && video_render_mutex)
        xSemaphoreTake(video_render_mutex, portMAX_DELAY);
#endif
    reset_fps_measurement();
#ifdef ARDUINO
    if (video_async_ready && video_render_mutex)
        xSemaphoreGive(video_render_mutex);
#endif
    hal_log("MSX %s: teto de EPS=%u (VDP %u Hz; VGA fisica 60 Hz)\n",
            hz == 50 ? "PAL" : "NTSC", emulation_hz, hz);
}

// Limita quadros EMULADOS (EPS), nao atualizacoes do sinal VGA. O fMSX chama
// Keyboard() uma vez por quadro, independentemente do frame skip. Se a CPU
// estiver lenta nao gera quadros em rajada para recuperar atrasos.
extern "C" void hal_video_pace_emu_frame(void)
{
    const unsigned max_eps = emulation_hz;
    const int64_t period_us = 1000000LL / (int64_t)max_eps;
    int64_t now = hal_time_us();

    if (pace_previous_rate != max_eps || pace_deadline_us == 0) {
        pace_previous_rate = max_eps;
        pace_deadline_us = now + period_us;
        return;
    }

    if (now < pace_deadline_us) {
        // Cede o núcleo durante a maior parte da espera e usa espera curta apenas
        // no final para evitar um overshoot de vários milissegundos.
        while (pace_deadline_us - now > 2000) {
            int ms = (int)((pace_deadline_us - now - 1000) / 1000);
            if (ms > 0) hal_delay_ms(ms);
            now = hal_time_us();
        }
        if (now < pace_deadline_us)
            delayMicroseconds((uint32_t)(pace_deadline_us - now));
        pace_deadline_us += period_us;
    } else if (now - pace_deadline_us > period_us) {
        // Um quadro já excedeu o orçamento: reancora a cadência, sem catch-up.
        pace_deadline_us = now + period_us;
    } else {
        pace_deadline_us += period_us;
    }
}

// ---------------------------------------------------------------------------
// Overlay de FPS
// ---------------------------------------------------------------------------
// OSD compacto 5x7: posicao (1,1), sem sobrepor as telas MSX
// centralizadas de ate 224 linhas (imagem ativa inicia em Y=8).
// A faixa tem 7 pixels; o sombreamento permanece dentro desse limite.
#define OV_X 1
#define OV_Y 1
#define OV_W 192
#define OV_H 7

static volatile bool fps_on = false;
static volatile bool ov_clear = false;    // pede restauracao da borda ao desligar
// Mascara 1-bit de 5x7, renderizada sobre UMA FAIXA OPACA em RGB222.
// v38.2: mesmo com sombra, um glifo transparente podia ser indistinguivel
// de pixels do VDP/borda que mudavam a cada quadro. A faixa escura compacta
// isola visualmente os indicadores em qualquer paleta e em ambos buffers.
// A faixa e repintada integralmente a cada apresentacao, depois do VDP.
// Ao desligar, update_border() e o renderer de linhas restauram o fundo.
static uint8_t ov[OV_H][OV_W];
static uint8_t ov_text_width = 0; // largura exata do texto (passo 6px, incluindo sombra)
static int64_t fps_t0_us;
static uint32_t fps_frames;
static volatile uint32_t emu_frames_total;
static uint32_t emu_frames_prev;
// Estado atomico de uma palavra: bit 8 = AUTO; bits 0..7 = descarte real.
// Escrito pela tarefa Z80, lido pela tarefa VGA. Nunca estimar % por FPS/EPS.
static volatile uint32_t frameskip_osd_state = 0;
static volatile bool fps_overlay_dirty = false;
static unsigned fps_last_value = 0, eps_last_value = 0;
static volatile uint32_t frameskip_until = 0;
static void render_overlay(unsigned fps, unsigned emu_fps);

static void reset_fps_measurement(void)
{
    fps_frames = 0;
    fps_t0_us = hal_time_us();
    emu_frames_prev = emu_frames_total;
    fps_last_value = eps_last_value = 0;
    /* Nao exibir ainda os FPS/EPS da regiao anterior apos trocar o F10. */
    if (fps_on) render_overlay(0, 0);
}

// Fonte 5x7 com passo de 6 pixels (altura logica 8): mesma escala
// compacta utilizada no indicador do MC-1000. Nao desenhar fundo preto.
static const uint8_t glyphs[24][7] = {
    {14,17,19,21,25,17,14}, // 0
    { 4,12, 4, 4, 4, 4,14}, // 1
    {14,17, 1, 2, 4, 8,31}, // 2
    {30, 1, 1,14, 1, 1,30}, // 3
    { 2, 6,10,18,31, 2, 2}, // 4
    {31,16,30, 1, 1,17,14}, // 5
    { 6, 8,16,30,17,17,14}, // 6
    {31, 1, 2, 4, 8, 8, 8}, // 7
    {14,17,17,14,17,17,14}, // 8
    {14,17,17,15, 1, 2,28}, // 9
    {31,16,16,30,16,16,16}, // F
    {30,17,17,30,16,16,16}, // P
    {15,16,16,14, 1, 1,30}, // S
    {30,17,17,30,20,18,17}, // R
    {14,17,17,31,17,17,17}, // A
    {17,27,21,21,17,17,17}, // M
    {31,16,16,30,16,16,31}, // E
    {17,18,20,24,20,18,17}, // K
    {31, 4, 4, 4, 4, 4,31}, // I
    {25,26, 2, 4, 8,11,19}, // % (dois pontos e diagonal)
    { 0, 0, 0, 0, 0, 0, 0}, // espaco
    {17,17,17,17,17,17,14}, // U
    {31, 4, 4, 4, 4, 4, 4}, // T
    {14,17,17,17,17,17,14}  // O
};

static int glyph_index(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    switch (c) {
        case 'F': return 10; case 'P': return 11; case 'S': return 12;
        case 'R': return 13; case 'A': return 14; case 'M': return 15;
        case 'E': return 16; case 'K': return 17; case 'I': return 18;
        case '%': return 19; case 'U': return 21;
        case 'T': return 22; case 'O': return 23;
        default: return 20;
    }
}

static void render_text_overlay(const char *txt)
{
    memset(ov, 0, sizeof(ov));
    int cx = 0;
    for (const char *p = txt; *p; ++p, cx += 6) {
        if (cx + 5 > OV_W) break;
        const uint8_t *g = glyphs[glyph_index(*p)];
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c)
                if (g[r] & (16 >> c)) ov[r][cx + c] = 1;
    }
    // Inclui o ultimo pixel de sombra (1px apos o glifo final), mas
    // nao pinta os 192 pixels se a mensagem for menor.
    ov_text_width = (uint8_t)(cx <= OV_W ? cx : OV_W);
}

static void render_overlay(unsigned fps, unsigned emu_fps)
{
    // FPS = quadros apresentados; EPS = quadros emulados. A porcentagem vem
    // diretamente do UPeriod do controlador AUTO, sem estimativa por FPS/EPS.
    const uint32_t state = frameskip_osd_state;
    const unsigned effective_skip = state & 0xFFu;
    const bool auto_mode = (state & 0x100u) != 0;
    char txt[40];
    snprintf(txt, sizeof(txt), "FPS %u EPS %u %s %u%%",
             fps > 999 ? 999 : fps, emu_fps > 999 ? 999 : emu_fps,
             auto_mode ? "AUTO" : "SKIP", effective_skip);
    render_text_overlay(txt);
    fps_overlay_dirty = false;
}

static void render_frameskip_overlay(int percent)
{
    char txt[24];
    if (percent < 0)
        snprintf(txt, sizeof(txt), "FRAME SKIP AUTO");
    else
        snprintf(txt, sizeof(txt), "FRAME SKIP %d%%", percent);
    render_text_overlay(txt);
}

static inline void overlay_row(int py)
{
    if (py < OV_Y || py >= OV_Y + OV_H || py >= vh) return;
    const unsigned width = ov_text_width;
    if (!width) return;
    uint8_t *dst = video.getScanline(py);
    const uint8_t *src = ov[py - OV_Y];
    const uint8_t *shadow = py > OV_Y ? ov[py - OV_Y - 1] : nullptr;

    // Ordem fisica da FabGL RGB222: [2,3,0,1] dentro de cada grupo de
    // 4 pixels. Em X=1, memset(dst+X, ...) corromperia pixels vizinhos.
    // Preencher pontas de forma enderecada; usar memset no trecho alinhado.
    const unsigned right = OV_X + width;
    unsigned x = OV_X;
    for (; x < right && (x & 3u); ++x) dst[x ^ 2u] = px_osd_bg;
    const unsigned aligned_end = right & ~3u;
    if (x < aligned_end) {
        memset(dst + x, px_osd_bg, aligned_end - x);
        x = aligned_end;
    }
    for (; x < right; ++x) dst[x ^ 2u] = px_osd_bg;
    for (unsigned i = 0; i < width; ++i) {
        if (shadow && i && shadow[i - 1] && !src[i])
            dst[(OV_X + i) ^ 2u] = sync_bits;
        if (src[i]) dst[(OV_X + i) ^ 2u] = px_yellow;
    }
}

static void update_fps(void)
{
    // Count completed video presents, not CPU/Keyboard calls. Using microseconds
    // avoids the integer drift that was visible around 50/60 Hz.
    ++fps_frames;
    const int64_t now = hal_time_us();
    if (!fps_t0_us) fps_t0_us = now;
    const int64_t dt = now - fps_t0_us;
    if (dt >= 1000000) {
        const unsigned fps = (unsigned)((fps_frames * 1000000ULL + (uint64_t)dt / 2) / (uint64_t)dt);
        const uint32_t emu_now = emu_frames_total;
        const uint32_t emu_delta = emu_now - emu_frames_prev;
        const unsigned emu_fps = (unsigned)((emu_delta * 1000000ULL + (uint64_t)dt / 2) / (uint64_t)dt);
        fps_last_value = fps;
        eps_last_value = emu_fps;
        render_overlay(fps, emu_fps);
        emu_frames_prev = emu_now;
        fps_frames = 0;
        fps_t0_us = now;
    }
}

// Preparar UMA unica vez por quadro, ANTES da restauracao das bordas.
// A versao anterior expirava o banner somente DEPOIS de update_border():
// um frame podia manter pixels antigos na VGA e nos backbuffers.
static void prepare_overlay_frame(void)
{
    const uint32_t until = frameskip_until;
    if (until && (int32_t)(millis() - until) >= 0) {
        frameskip_until = 0;
        ov_clear = true;
        invalidate_border_buffers();
    }
    if (fps_on) {
        update_fps();
        if (fps_overlay_dirty) render_overlay(fps_last_value, eps_last_value);
    }
}

extern "C" void hal_video_toggle_fps(void)
{
    if (video_async_ready) xSemaphoreTake(video_render_mutex, portMAX_DELAY);
    if (!fps_on) {
        fps_frames = 0;
        fps_t0_us = hal_time_us();
        emu_frames_prev = emu_frames_total;
        fps_last_value = eps_last_value = 0;
        render_overlay(0, 0);
        fps_on = true;
    } else {
        fps_on = false;
        ov_clear = true;
        invalidate_border_buffers();
    }
    if (video_async_ready) xSemaphoreGive(video_render_mutex);
}

extern "C" int hal_video_fps_enabled(void) { return fps_on; }

extern "C" void hal_video_set_emu_frames(uint32_t frames) { emu_frames_total = frames; }

// Chamado apenas na troca de modo ou quando o AUTO ajusta a porcentagem.
// Operacao sem mutex e sem trabalho grafico: nao bloquear o nucleo Z80.
extern "C" void hal_video_set_frameskip_state(int auto_mode, int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const uint32_t next = (auto_mode ? 0x100u : 0u) | (uint32_t)percent;
    if (frameskip_osd_state != next) {
        frameskip_osd_state = next;
        fps_overlay_dirty = true;
    }
}

extern "C" void hal_video_show_frameskip(int percent)
{
    // -1 indica AUTO; os demais casos mostram o modo fixo 0%.
    if (percent != -1 && percent != 0) percent = 0;
    if (video_async_ready) xSemaphoreTake(video_render_mutex, portMAX_DELAY);
    // Se o contador ativado por F11 estiver ativo, manter FPS/EPS/% visiveis.
    // O banner temporario so e mostrado quando o contador estiver desligado.
    frameskip_until = fps_on ? 0 : millis() + 5000;
    if (!fps_on) render_frameskip_overlay(percent);
    else fps_overlay_dirty = true;
    if (video_async_ready) xSemaphoreGive(video_render_mutex);
}

// Atalhos de vídeo: chame a cada tecla pressionada.
//   fkey  = número da tecla de função (11 ou 12)
//   shift = diferente de zero se Shift (esquerdo ou direito) está pressionado
// Retorna 1 se o atalho foi tratado (não repasse a tecla ao MSX), 0 caso contrário.
extern "C" int hal_video_hotkey(int fkey, int shift)
{
    // F11 agora liga/desliga FPS/EPS/SKIP. Shift+F11 deve seguir para
    // HAL_HOT_SKIP; Shift+F12 fica livre e nunca deve gerar reset.
    if (fkey == 11 && !shift) { hal_video_toggle_fps(); return 1; }
    if (fkey == 12 && shift) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// Inicialização
// ---------------------------------------------------------------------------
extern "C" void hal_video_init(void)
{
    video.begin((gpio_num_t)PIN_VGA_RED1, (gpio_num_t)PIN_VGA_RED0,
                (gpio_num_t)PIN_VGA_GREEN1, (gpio_num_t)PIN_VGA_GREEN0,
                (gpio_num_t)PIN_VGA_BLUE1, (gpio_num_t)PIN_VGA_BLUE0,
                (gpio_num_t)PIN_VGA_HSYNC, (gpio_num_t)PIN_VGA_VSYNC);
    // Linhas repetidas por DMA (double scan): 320x240 ocupam a tela toda sem cópia 2x2.
    video.setResolution(QVGA_320x240_60Hz, 320, 240, true);
    vga_double_buffered = video.isDoubleBuffered() &&
                          video.getViewPortWidth() == 320 &&
                          video.getViewPortHeight() == 240;
    if (!vga_double_buffered) {
        video.setResolution(QVGA_320x240_60Hz, 320, 240, false);
        hal_log("VGA RGB222: double buffer indisponivel; buffer unico\n");
    } else {
        vga_swap_canvas = new fabgl::Canvas(&video);
        hal_log("VGA RGB222: double buffer com flip VSync ativo\n");
    }
    vw = video.getViewPortWidth();
    vh = video.getViewPortHeight();
    if (vw > VMAX_W) vw = VMAX_W;
    if (vh > VMAX_H) vh = VMAX_H;
    if (vw != 320 || vh != 240) {
        hal_log("ERRO VGA: viewport truncado %dx%d\n", vw, vh);
        abort();
    }
    vw &= ~3;                                          // o caminho rápido escreve 4 pixels por vez
    hal_log("VGA: %dx%d\n", vw, vh);

    // Tabelas RGB565 -> pixel bruto da FabGL (2 bits por cor + bits de sincronismo).
    sync_bits = video.createRawPixel(RGB222(0, 0, 0));
    px_yellow = video.createRawPixel(RGB222(3, 3, 0));
    px_osd_bg = video.createRawPixel(RGB222(0, 0, 1)); // azul muito escuro, opaco
    // lut_r já inclui os bits de sincronismo, então o pixel final é só lut_r | lut_g | lut_b.
    for (unsigned i = 0; i < 64; ++i)
        lut222_raw[i] = video.createRawPixel(RGB222((i >> 4) & 3, (i >> 2) & 3, i & 3));
    for (unsigned r = 0; r < 32; ++r)
        lut_r[r] = video.createRawPixel(RGB222((r * 3 + 15) / 31, 0, 0));
    for (unsigned g = 0; g < 64; ++g) {
        const uint8_t green = video.createRawPixel(RGB222(0, (g * 3 + 31) / 63, 0)) ^ sync_bits;
        for (unsigned b = 0; b < 32; ++b) {
            const uint8_t blue = video.createRawPixel(RGB222(0, 0, (b * 3 + 15) / 31)) ^ sync_bits;
            lut_gb[(g << 5) | b] = green | blue;
        }
    }

    for (int y = 0; y < vh; ++y)
        memset(video.getScanline(y), sync_bits, vw);
    hal_log("VGA cache de linhas: desativado por padrao (use msx_rowcache=1)\n");
    video_ready = true;
}

// Pixel bruto a partir do RGB565 com bytes trocados, sem fazer bswap. Lido como uint16
// little-endian, s = (c >> 8) | (c << 8), onde c é o RGB565 de verdade:
//   vermelho = c >> 11          = (s >> 3) & 31
//   verde    = (c >> 5) & 63    = ((s & 7) << 3) | (s >> 13)
//   azul     = c & 31           = (s >> 8) & 31
static inline uint32_t pix(uint16_t s)
{
    const unsigned gb = ((((unsigned)(s & 7) << 3) | (s >> 13)) << 5) | ((s >> 8) & 31);
    return lut_r[(s >> 3) & 31] | lut_gb[gb];
}

// Converte uma linha de origem e a escreve (já escalada) na linha de destino.
// A FabGL guarda os pixels em palavras de 32 bits com a ordem de bytes 2,3,0,1,
// por isso os 4 pixels p0..p3 viram (p2 | p3<<8 | p0<<16 | p1<<24).
static HOT void render_row(uint8_t *dst, const uint16_t *src)
{
    int x = 0;
    if (!(map_x0 & 3)) {
        uint32_t *d32 = (uint32_t *)(dst + map_x0);
        const int n4 = map_dw >> 2;
        // Do not use a hand-unrolled 256->320 packing table here. The previous
        // special case duplicated/misordered source pixels inside FabGL's 4-byte
        // scanline layout, producing a completely corrupted stretched image.
        // The xmap path below is cheap at QVGA and is the single source of truth
        // for all stretch ratios.
        if (map_stretch && map_w == 256 && map_dw == 320 && map_sx0 == 0) {
            // Specialized integer-ratio 5:4 centered nearest-neighbor scaler.
            // For each four source pixels: 0,1,2,2,3; for each sixteen:
            //   0,1,2,2 | 3,4,5,6 | 6,7,8,9 | 10,10,11,12 | 13,14,14,15.
            // Five 32-bit stores per block instead of 20 xmap accesses, with
            // identical byte order to the FabGL packed generic path.
            for (int sx = 0, dx = 0; sx < 256; sx += 16, dx += 5) {
                uint8_t c[16];
                for (int k = 0; k < 16; ++k) c[k] = (uint8_t)pix(src[sx + k]);
                d32[dx + 0] = (uint32_t)c[2] | ((uint32_t)c[2] << 8) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 24);
                d32[dx + 1] = (uint32_t)c[5] | ((uint32_t)c[6] << 8) | ((uint32_t)c[3] << 16) | ((uint32_t)c[4] << 24);
                d32[dx + 2] = (uint32_t)c[8] | ((uint32_t)c[9] << 8) | ((uint32_t)c[6] << 16) | ((uint32_t)c[7] << 24);
                d32[dx + 3] = (uint32_t)c[11] | ((uint32_t)c[12] << 8) | ((uint32_t)c[10] << 16) | ((uint32_t)c[10] << 24);
                d32[dx + 4] = (uint32_t)c[14] | ((uint32_t)c[15] << 8) | ((uint32_t)c[13] << 16) | ((uint32_t)c[14] << 24);
            }
        } else if (map_stretch) {
            const int source_width = map_w < VMAX_W ? map_w : VMAX_W;
            for (int sx = 0; sx < source_width; ++sx)
                converted_row[sx] = pix(src[sx]);
            const uint16_t *xm = xmap;
            for (int i = 0; i < n4; ++i, xm += 4) {
                const uint32_t p0 = converted_row[xm[0]];
                const uint32_t p1 = converted_row[xm[1]];
                const uint32_t p2 = converted_row[xm[2]];
                const uint32_t p3 = converted_row[xm[3]];
                d32[i] = p2 | (p3 << 8) | (p0 << 16) | (p1 << 24);
            }
        } else {
            const uint16_t *s = src + map_sx0;
            for (int i = 0; i < n4; ++i, s += 4) {
                const uint32_t p0 = pix(s[0]), p1 = pix(s[1]);
                const uint32_t p2 = pix(s[2]), p3 = pix(s[3]);
                d32[i] = p2 | (p3 << 8) | (p0 << 16) | (p1 << 24);
            }
        }
        x = n4 << 2;
    }
    for (; x < map_dw; ++x)                            // resto (ou x0 desalinhado)
        dst[(map_x0 + x) ^ 2] = (uint8_t)pix(src[xmap[x]]);
}

// v30: o fMSX ja entregou os indices RGB222 de 6 bits; nao ha RGB565 para
// decodificar. Leitura PSRAM: 1 byte/pixel; conversao: LUT de 64 entradas.
static HOT void render_row_rgb222(uint8_t *dst, const uint8_t *src)
{
    if (!(map_x0 & 3)) {
        uint32_t *d = (uint32_t *)(dst + map_x0);
        const int n4 = map_dw >> 2;
        if (map_stretch) {
            const uint16_t *xm = xmap;
            for (int i = 0; i < n4; ++i, xm += 4) {
                /* Montar dois pares em registradores: sem LUT 8 KiB da v38.
                 * A FabGL usa a ordem [p2,p3,p0,p1]. */
                const uint16_t lo = rgb222_pair(src[xm[2]], src[xm[3]]);
                const uint16_t hi = rgb222_pair(src[xm[0]], src[xm[1]]);
                d[i] = (uint32_t)lo | ((uint32_t)hi << 16);
            }
        } else {
            const uint8_t *s = src + map_sx0;
            for (int i = 0; i < n4; ++i, s += 4) {
                const uint16_t lo = rgb222_pair(s[2], s[3]);
                const uint16_t hi = rgb222_pair(s[0], s[1]);
                d[i] = (uint32_t)lo | ((uint32_t)hi << 16);
            }
        }
        for (int x = n4 << 2; x < map_dw; ++x)
            dst[(map_x0 + x) ^ 2] = lut222_raw[src[xmap[x]] & 63];
    } else {
        for (int x = 0; x < map_dw; ++x)
            dst[(map_x0 + x) ^ 2] = lut222_raw[src[xmap[x]] & 63];
    }
}

// Buffer de quadro do emulador: RAM interna é bem mais rápida que a PSRAM (o VDP escreve nele
// e o hal_video_present lê dele a cada quadro). Só usa se sobrar folga para o resto do sistema;
// caso contrário cai na hal_alloc_big (PSRAM).
extern "C" void *hal_alloc_fast(size_t size)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t margin = 48 * 1024;                   // reserva para pilhas, SD, áudio, etc.
    if (heap_caps_get_free_size(caps) >= size + margin &&
        heap_caps_get_largest_free_block(caps) >= size) {
        void *p = heap_caps_malloc(size, caps);
        if (p) {
            hal_log("alloc_fast: %u bytes em RAM interna\n", (unsigned)size);
            return p;
        }
    }
    hal_log("alloc_fast: sem folga na RAM interna, usando hal_alloc_big\n");
    return hal_alloc_big(size);
}

// Preenche SOMENTE as margens em RGB222, preservando o retangulo ativo.
// Reexecuta quando a cor R#7 muda ou quando a geometria e reinicializada.
// VGA usa pixels em ordem 2,3,0,1 nos grupos de quatro, mas memset e valido
// porque os quatro bytes recebem o mesmo valor.
static HOT void update_border(void)
{
    const uint8_t color = border_is_rgb222 ? lut222_raw[border_rgb222_index]
                                           : (uint8_t)pix(border_rgb565_swapped);
    // v38.4: borda persistente por backbuffer. A pequena faixa do OSD
    // e repintada separadamente DEPOIS do VDP a cada apresentacao,
    // portanto nao deve invalidar o cache de toda a borda.
    int slot = -1;
    bool border_reusable = false;
    if (vga_double_buffered) {
        uint8_t *back = video.getScanline(0);
        if (border_last_buffer[0] == back) slot = 0;
        else if (border_last_buffer[1] == back) slot = 1;
        else if (!border_last_buffer[0]) slot = 0;
        else slot = 1;
        border_reusable = border_last_epoch[slot] == border_epoch &&
                          border_last_buffer[slot] == back &&
                          border_last_color[slot] == color;
        if (border_reusable) return;
        border_last_buffer[slot] = back;
    } else {
        border_reusable = !border_dirty;
        if (border_reusable) return;
    }
    border_raw = color;
    for (int py = 0; py < vh; ++py) {
        uint8_t *dst = video.getScanline(py);
        if (py < map_y0 || py >= map_y0 + map_dh) {
            memset(dst, border_raw, vw);
        } else {
            if (map_x0) memset(dst, border_raw, map_x0);
            const int right = map_x0 + map_dw;
            if (right < vw) memset(dst + right, border_raw, vw - right);
        }
    }
    border_dirty = false;
    if (slot >= 0) {
        border_last_color[slot] = color;
        border_last_epoch[slot] = border_epoch;
    }
}


// v38.4: faixa de OSD na borda VGA, recomposta mesmo quando o overlay
// esta desligado. Isso evita que um dos dois backbuffers conserve o banner
// antigo em quadros de transicao e elimina a dependencia do cache de borda.
// Nao apagar pixels de jogo quando algum modo ocupa a faixa superior:
// nesses modos o renderer ja atualiza a imagem e a sobreposicao (se ativada)
// ocorre por ultimo. Na configuracao normal MSX 192/212, a faixa inteira e
// borda (Y=0..7 vs map_y0 >= 8 nos modos de ate 224 linhas).
static inline void compose_overlay_header(bool visible)
{
    const int end = OV_Y + OV_H < vh ? OV_Y + OV_H : vh;
    if (OV_Y >= end) return;
    if (end <= map_y0) {
        for (int py = OV_Y; py < end; ++py)
            memset(video.getScanline(py), border_raw, vw);
    }
    if (visible)
        for (int py = OV_Y; py < end; ++py)
            overlay_row(py);
}

// pixels: RGB565 com os bytes trocados. O ^2 no índice é a ordem de bytes do DMA da FabGL.
extern "C" HOT void hal_video_present(const uint16_t *pixels, int w, int h, int stride)
{
    if (!video_ready || w <= 0 || h <= 0) return;
    if (row_cache_enabled && !vga_double_buffered && !row_snapshot) {
        row_snapshot = (uint16_t *)heap_caps_malloc(VMAX_H * VMAX_W * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!row_snapshot) {
            row_cache_enabled = false;
            hal_log("VGA cache indisponivel (sem PSRAM)\n");
        }
    }
    if (w != map_w || h != map_h || stretch != map_stretch)
        rebuild_maps(w, h);
    prepare_overlay_frame();
    // A faixa superior e restaurada por compose_overlay_header() todo quadro.
    // O sinal de limpeza continua invalida a borda quando o painel expira.
    const bool overlay_was_cleared = ov_clear;
    ov_clear = false;
    update_border();
    const bool show_fps = fps_on;
    const bool show_skip = !show_fps && frameskip_until != 0;

    // Trecho alinhado em 4 bytes que cobre a imagem (usado ao copiar linhas repetidas).
    const int span_lo = map_x0 & ~3;
    const int span_hi = (map_x0 + map_dw + 3) & ~3;

    int last_sy = -1;
    uint8_t *prev = nullptr;
    for (int y = 0; y < map_dh; ++y) {
        const int py = map_y0 + y;
        const int sy = map_stretch ? ymap[y] : y + map_sy0;
        uint8_t *dst = video.getScanline(py);
        // Linha de origem repetida (stretch vertical): copia a linha anterior já pronta.
        // Dentro/ao lado do overlay a linha anterior pode conter o overlay, então refaz.
        const bool ov_zone = (show_fps || show_skip) && py >= OV_Y && py < OV_Y + OV_H;
        if (sy == last_sy && !ov_zone) {
            memcpy(dst + span_lo, prev + span_lo, span_hi - span_lo);
        } else {
            const uint16_t *source = pixels + sy * stride;
            const size_t row_bytes = (size_t)map_w * sizeof(uint16_t);
            const bool force_overlay_redraw = overlay_was_cleared &&
                 py >= OV_Y && py < OV_Y + OV_H;
            const bool can_cache = !vga_double_buffered && row_cache_enabled && row_snapshot &&
                 map_w <= VMAX_W && y < VMAX_H;
            uint16_t *snapshot = can_cache ? row_snapshot + y * VMAX_W : nullptr;
            // Com OSD, essas linhas precisam ser refeitas mesmo que o jogo
            // nao tenha mudado: o cache conserva pixels anteriores do OSD.
            if (snapshot && row_valid[y] && !force_overlay_redraw && !ov_zone &&
                memcmp(snapshot, source, row_bytes) == 0) {
                ++row_cache_hits;
            } else {
                render_row(dst, source);
                if (snapshot) {
                    memcpy(snapshot, source, row_bytes);
                    row_valid[y] = 1;
                }
                ++row_cache_misses;
            }
        }
        last_sy = sy;
        prev = dst;
    }

    // v38.4: borda superior e OSD SEMPRE coerentes no backbuffer ativo;
    // o desenho e a ultima operacao antes do flip VGA.
    compose_overlay_header(show_fps || show_skip);
    if (vga_double_buffered && vga_swap_canvas) {
        vga_swap_canvas->swapBuffers();
        // O backbuffer seguinte pode conter um quadro antigo.
        memset(row_valid, 0, sizeof(row_valid));
    }
}

// Renderizacao RGB222 compacta: SEM RGB565, preservando os mesmos 64 níveis DAC.
extern "C" HOT void hal_video_present_rgb222(const uint8_t *pixels,int w,int h,int stride)
{
    if(!video_ready || w<=0 || h<=0) return;
    if(w!=map_w || h!=map_h || stretch!=map_stretch) rebuild_maps(w,h);
    prepare_overlay_frame();
    // VDP RGB222 tambem precisa repintar a borda NO MESMO frame do timeout.
    ov_clear = false;
    update_border();
    const bool show_fps=fps_on;
    const bool show_skip=!show_fps && frameskip_until!=0;
    const int span_lo=map_x0 & ~3;
    const int span_hi=(map_x0+map_dw+3)&~3;
    int last_sy=-1;
    uint8_t *prev=nullptr;
    for(int y=0;y<map_dh;++y) {
        const int py=map_y0+y;
        const int sy=map_stretch ? ymap[y] : y+map_sy0;
        uint8_t *dst=video.getScanline(py);
        const bool ov_zone=(show_fps||show_skip) && py>=OV_Y && py<OV_Y+OV_H;
        if(sy==last_sy && !ov_zone)
            memcpy(dst+span_lo,prev+span_lo,span_hi-span_lo);
        else
            render_row_rgb222(dst,pixels+sy*stride);
        last_sy=sy;
        prev=dst;
    }
    // Mesma composicao do RGB565; evita divergencia entre os pipelines.
    compose_overlay_header(show_fps || show_skip);
    if(vga_double_buffered && vga_swap_canvas) vga_swap_canvas->swapBuffers();
}

// Pipeline paralelo VGA permanente (v24+).
// O núcleo 0 continua emulando enquanto o núcleo 1 converte o quadro acabado.
// O produtor somente adquire buffers da free queue. Um buffer em fila ou em
// conversao nunca pode voltar a ser escrito pelo fMSX.
struct VideoFrame {
    void *data;
    int w, h, stride, top, active_h;
    uint16_t border;
    bool rgb222;
};
static QueueHandle_t video_ready_queue;
static QueueHandle_t video_free_queue;
static volatile uint32_t video_async_work_us;
static volatile uint32_t video_async_wait_us;
static volatile uint32_t video_async_frames;

static void video_worker(void *)
{
    VideoFrame frame;
    for (;;) {
        if (xQueueReceive(video_ready_queue, &frame, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(video_render_mutex, portMAX_DELAY);
            hal_video_set_active_area(frame.top, frame.active_h);
            if(frame.rgb222) hal_video_set_border_rgb222((uint8_t)frame.border);
            else hal_video_set_border_rgb565(frame.border);
            const int64_t render_start = hal_time_us();
            if(frame.rgb222) hal_video_present_rgb222((const uint8_t *)frame.data,frame.w,frame.h,frame.stride);
            else hal_video_present((const uint16_t *)frame.data,frame.w,frame.h,frame.stride);
            video_async_work_us += (uint32_t)(hal_time_us() - render_start);
            ++video_async_frames;
            xSemaphoreGive(video_render_mutex);
            // Liberar apenas depois de terminar toda a conversao.
            xQueueSend(video_free_queue, &frame.data, portMAX_DELAY);
        }
    }
}

extern "C" int hal_video_async_init(size_t frame_bytes)
{
    if (video_async_ready) return 1;
    if (!video_ready || !frame_bytes) return 0;
    void *b1 = heap_caps_malloc(frame_bytes,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *b2 = heap_caps_malloc(frame_bytes,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b1 || !b2) {
        if (b1) free(b1);
        if (b2) free(b2);
        hal_log("VGA async: sem memoria para buffers extras, modo sincrono\n");
        return 0;
    }
    video_ready_queue = xQueueCreate(2, sizeof(VideoFrame));
    video_free_queue = xQueueCreate(2, sizeof(void *));
    video_render_mutex = xSemaphoreCreateMutex();
    if (!video_ready_queue || !video_free_queue || !video_render_mutex) {
        if (video_ready_queue) vQueueDelete(video_ready_queue);
        if (video_free_queue) vQueueDelete(video_free_queue);
        if (video_render_mutex) vSemaphoreDelete(video_render_mutex);
        free(b1); free(b2);
        return 0;
    }
    // Permite zero-initialization e uso seguro caso a tela mude de 192 para 212.
    memset(b1, 0, frame_bytes);
    memset(b2, 0, frame_bytes);
    xQueueSend(video_free_queue, &b1, 0);
    xQueueSend(video_free_queue, &b2, 0);
    if (xTaskCreatePinnedToCore(video_worker, "msx_vga", 4096, NULL, 4,
                                NULL, 1) != pdPASS) {
        vQueueDelete(video_ready_queue);
        vQueueDelete(video_free_queue);
        vSemaphoreDelete(video_render_mutex);
        video_ready_queue = video_free_queue = nullptr;
        video_render_mutex = nullptr;
        free(b1); free(b2);
        return 0;
    }
    video_async_ready = true;
    hal_log("VGA async: 3 framebuffers PSRAM, worker no core 1\n");
    return 1;
}

static void *hal_video_async_submit_impl(void *finished,
                                         int w, int h, int stride,
                                         int top, int active_h, uint16_t border,
                                         bool rgb222)
{
    if (!video_async_ready) return nullptr;
    void *next = nullptr;
    // Nao tem fallback com renderizacao direta: esperar a liberação impede
    // que o worker leia o buffer que o emulador esta prestes a sobrescrever.
    const int64_t wait_start = hal_time_us();
    if (xQueueReceive(video_free_queue, &next, portMAX_DELAY) != pdTRUE)
        return nullptr;
    video_async_wait_us += (uint32_t)(hal_time_us() - wait_start);
    VideoFrame frame = {finished, w, h, stride, top, active_h, border, rgb222};
    xQueueSend(video_ready_queue, &frame, portMAX_DELAY);
    return next;
}

extern "C" uint16_t *hal_video_async_submit(uint16_t *finished,
                                            int w,int h,int stride,int top,int active_h,uint16_t border)
{
    return (uint16_t *)hal_video_async_submit_impl(finished,w,h,stride,top,active_h,border,false);
}
extern "C" uint8_t *hal_video_async_submit_rgb222(uint8_t *finished,
                                                   int w,int h,int stride,int top,int active_h,uint8_t border)
{
    return (uint8_t *)hal_video_async_submit_impl(finished,w,h,stride,top,active_h,border,true);
}

extern "C" void hal_video_async_stats(uint32_t *work_us, uint32_t *wait_us,
                                      uint32_t *presented)
{
    if (work_us) *work_us = video_async_work_us;
    if (wait_us) *wait_us = video_async_wait_us;
    if (presented) *presented = video_async_frames;
}

extern "C" void hal_video_present_safe(const uint16_t *pixels,
                                       int w, int h, int stride)
{
    if (video_async_ready) xSemaphoreTake(video_render_mutex, portMAX_DELAY);
    hal_video_present(pixels, w, h, stride);
    if (video_async_ready) xSemaphoreGive(video_render_mutex);
}

extern "C" void hal_video_present_safe_rgb222(const uint8_t *pixels,
                                                int w, int h, int stride)
{
    if(video_async_ready) xSemaphoreTake(video_render_mutex,portMAX_DELAY);
    hal_video_present_rgb222(pixels,w,h,stride);
    if(video_async_ready) xSemaphoreGive(video_render_mutex);
}

// Mensagem de erro na tela (só usada antes de o emulador começar).
static void draw_wrapped(fabgl::Canvas &cv, const char *text, int &y)
{
    const int cols = 38;                               // fonte 8x8 em 320 px, com margem
    char line[cols + 1];
    while (*text) {
        int n = (int)strlen(text);
        if (n > cols) {
            n = cols;
            while (n > 0 && text[n] != ' ') --n;
            if (n == 0) n = cols;
        }
        memcpy(line, text, n);
        line[n] = 0;
        cv.drawText(8, y, line);
        y += 12;
        text += n;
        while (*text == ' ') ++text;
    }
}

extern "C" void hal_screen_message(const char *l1, const char *l2)
{
    hal_log("%s\n%s\n", l1, l2 ? l2 : "");
    if (!video_ready) return;
    fabgl::Canvas cv(&video);
    cv.setBrushColor(fabgl::Color::Blue);
    cv.clear();
    cv.setPenColor(fabgl::Color::BrightWhite);
    cv.selectFont(&fabgl::FONT_8x8);
    int y = 96;
    draw_wrapped(cv, l1, y);
    y += 8;
    if (l2) draw_wrapped(cv, l2, y);
}
