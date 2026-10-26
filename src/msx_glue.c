// msx_glue.c - liga o núcleo fMSX (Marat Fayzullin) ao hardware através do hal.h.
// Aqui ficam as funções que o fMSX espera que a plataforma forneça:
// vídeo, teclado/joystick, mouse, áudio e o ritmo de execução (frame skip).
#include "msxfix.h"
#include "hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "msx_frameskip_auto.h"
#include "msx_abnt2_glyph.h"

// Tela do MSX: 256 x 228 (192 ou 212 linhas + bordas).
// RGB222 nativo mantém todas as 64 cores da saida VGA (NAO e VGA16).
#if MSX_NATIVE_RGB222
#define BPP8
#else
#define BPP16
#endif
#define WIDTH 256
#define HEIGHT 228

// O framebuffer é RGB565 com os bytes trocados (big-endian): é o formato que o Menu.c do
// fork usa nos seus PIXEL(). O hal_video_present desfaz a troca ao converter para VGA.
#define C_RGB(R, G, B) ((((R) & 0xF8) << 8) | (((G) & 0xFC) << 3) | (((B) & 0xFF) >> 3))
#define SWAP16(C) ((uint16_t)((((C) >> 8) | ((C) << 8)) & 0xFFFF))

#if MSX_NATIVE_RGB222
static uint8_t BPal[256];
static uint8_t XPal[80];
static uint8_t XPal0;
static uint8_t *XBuf;
#else
static uint16_t BPal[256];
static uint16_t XPal[80];
static uint16_t XPal0;
static uint16_t *XBuf;
#endif

void PutImage(void);   // usado pelo RefreshScreen() do Common.h

#include "fmsx.h"

const char *Title = "fMSX 6.0";
const char *Disks[2][MAXDISKS + 1];

#if MSX_NATIVE_RGB222
static uint8_t *FrameBuf;
#else
static uint16_t *FrameBuf;
#endif
static Image NormScreen;

static int JoyState, LastKey, InMenu;
static uint32_t PrevNav;
/* Um evento de joystick e o ciclo de teclado usam a mesma matriz fisica.
 * Evitar duas leituras PS/2/FabGL seguidas no MESMO scanline: a amostragem
 * completa permanece obrigatoria no proximo quadro. */
static int64_t InputLastPollUs;
static void ProcessEventsIfStale(int min_age_us);

/* O glifo ASCII 27h original de varias BIOS MSX e um acento inclinado.
 * Corrigir o bitmap JA copiado da BIOS para a VRAM, sem tocar no SD ou
 * na imagem de ROM em RAM. Atualizar APENAS esse
 * glifo no SCREEN 0/1 e somente se ainda coincidir com o bitmap original.
 * Jogos/fontes personalizadas sao preservados; custo por EPS: 8 bytes. */
static void msx_br_update_apostrophe_vram(int br_on)
{
    if (ScrMode > 1 || !VRAM || !ChrGen) return;
    uint8_t *glyph=ChrGen+(0x27u<<3);
    /* A tabela de padroes deve estar dentro da VRAM corrente. */
    if (glyph < VRAM || (size_t)(glyph-VRAM)+8u > (size_t)VRAMPages*0x4000u)
        return;
    msx_abnt2_update_quote_glyph(glyph,br_on);
}

// ---------------------------------------------------------------------------
// Shift+F11: 0% (desenha tudo) ou AUTO (ajusta a taxa a 50/60 EPS).
// O quadro descartado continua executando Z80/VDP/som; apenas nao desenha.
// ---------------------------------------------------------------------------
static int FrameSkip = 0;           // % efetiva descartada; 0 no modo manual
static int FrameSkipAuto = 0;       // modo selecionado por Shift+F11/bootl.rc
static MSXAutoFrameSkip AutoSkipState;
static int VideoAsync = 1;        // pipeline VGA permanente
static int VideoAsyncReady = 0;
static int FbInternal = 0;           // prioriza a RAM do Z80; framebuffer na PSRAM por padrão
static int BenchmarkTurbo = 0;       // opcional: sem limite EPS, desliga PCM (diagnóstico)
static int ProfileOn = 0;            // opcional: mede tempo em VGA/audio/pacing
static volatile uint32_t stat_vga_us, stat_sound_us, stat_pacing_us;
// Contadores lidos pela tarefa de diagnóstico e pelo OSD de FPS (desde o boot).
static volatile unsigned stat_emu_frames, stat_drawn_frames;
static volatile const char *stat_where = "boot";

static void set_runtime_frame_skip_auto(int auto_enabled)
{
    FrameSkipAuto = auto_enabled != 0;
    FrameSkip = 0;
    UPeriod = 100;
    msx_auto_fs_reset(&AutoSkipState);
    hal_video_set_frameskip_state(FrameSkipAuto, FrameSkip);
    hal_video_show_frameskip(FrameSkipAuto ? -1 : 0);
    hal_log("frame skip=%s; descarte atual=%d%%\n",
            FrameSkipAuto ? "AUTO" : "0%", FrameSkip);
}

/* Chamado no reset e ao mudar PAL/NTSC, inclusive entre modelos com o
 * mesmo Hz. O controlador nao pode reutilizar medidas da regiao anterior. */
void msx_frameskip_on_timing_reset(void)
{
    msx_auto_fs_reset(&AutoSkipState);
    if (FrameSkipAuto) {
        FrameSkip = 0;
        UPeriod = 100;
    }
    hal_video_set_frameskip_state(FrameSkipAuto, FrameSkip);
}

static void update_auto_frame_skip(uint32_t pacing_us)
{
    if (!FrameSkipAuto || BenchmarkTurbo) return;
    const int new_skip = msx_auto_fs_tick(&AutoSkipState, hal_time_us(),
                                         VIDEO(MSX_PAL) ? 50u : 60u, pacing_us);
    if (new_skip < 0) return;
    FrameSkip = new_skip;
    UPeriod = (byte)(100 - FrameSkip);
    hal_video_set_frameskip_state(1, FrameSkip);
    if (ProfileOn)
        hal_log("FRAME SKIP AUTO: %d%% (teto EPS %u)\n",
                FrameSkip, VIDEO(MSX_PAL) ? 50u : 60u);
}

extern void MSXBootKeyClickOnFirstInput(void);

// ---------------------------------------------------------------------------
// Entrada
// ---------------------------------------------------------------------------
#define NAV_MASK (HAL_NAV_UP | HAL_NAV_DOWN | HAL_NAV_LEFT | HAL_NAV_RIGHT | HAL_NAV_A | HAL_NAV_B)

static void wait_nav_release(int max_ms)
{
    for (int waited = 0; (hal_input_nav() & NAV_MASK) && waited < max_ms; waited += 10)
        hal_delay_ms(10);
}

int ProcessEvents(int Wait)
{
    (void)Wait;
    // Matriz do teclado MSX: 0xFF = nenhuma tecla apertada.
    for (int i = 0; i < 16; ++i)
        KeyState[i] = 0xFF;
    JoyState = 0;

    /* A variante MSX1 usa a sua própria BIOS de teclado. Este perfil se
     * atualiza também após trocar MSX1/Expert/Hotbit no F10. */
    int keyboard_profile = !MSXKeyboardBR ? -1 :
        MODEL(MSX_MSX1) && MSX1BiosVariant == MSX1_BIOS_EXPERT ? 1 :
        MODEL(MSX_MSX1) && MSX1BiosVariant == MSX1_BIOS_HOTBIT ? 2 : 0;
    hal_input_set_keyboard_profile(keyboard_profile);
    /* v38.2: o usuario prefere o desenho original da BIOS para ASCII 27h.
       Reverter inclusive o glifo previamente modificado em VRAM. */
    msx_br_update_apostrophe_vram(0);
    uint8_t keys[256];
    hal_input_keys(keys);
    /* Ativar CLIKSW somente na primeira tecla digitada no BASIC.
     * A BIOS do MSX2/2+ pode limpar a variavel durante o boot: ativar
     * em um frame arbitrario (v35) nao garantia que ela permanecesse 1.
     * Menu F10, joystick e jogos nao devem disparar essa configuracao. */
    if (!InMenu) {
        int text_key_pressed = 0;
        for (int k = '0'; k <= '9'; ++k) text_key_pressed |= !!keys[k];
        for (int k = 'a'; k <= 'z'; ++k) text_key_pressed |= !!keys[k];
        text_key_pressed |= !!keys[32] || !!keys[13] || !!keys[8];
        if (text_key_pressed) MSXBootKeyClickOnFirstInput();
    }
    for (int k = 1; k < 131; ++k)
        if (keys[k]) { KBD_SET(k); }

    uint32_t nav = hal_input_nav();
    uint32_t rising = nav & ~PrevNav;
    PrevNav = nav;

    if (!InMenu) {
        if (rising & HAL_HOT_RESET) {
            ResetMSX(Mode, RAMPages, VRAMPages);
            return 0;
        }
        if (rising & HAL_HOT_SKIP) {
            // Shift+F11: 0% -> AUTO -> 0%.
            set_runtime_frame_skip_auto(!FrameSkipAuto);
        }
        if (rising & HAL_HOT_MENU)
            InMenu = 2;
    }

    if (InMenu == 2) {
        // Menu do próprio fMSX: carrega cartucho/disco, salva estado, opções...
        InMenu = 1;
        stat_where = "menu";
        MenuMSX();
        wait_nav_release(500);
        PrevNav = hal_input_nav();
        InMenu = 0;
        // Nao interpretar o tempo parado no F10 como queda real de EPS.
        // Conserva a escolha AUTO e recomeca a busca em 0% ao sair.
        msx_frameskip_on_timing_reset();
        return 0;
    }

    if (InMenu) {
        // Dentro do menu, as setas/Enter/Esc viram teclas do console.
        if (nav & HAL_NAV_LEFT)  LastKey = CON_LEFT;
        if (nav & HAL_NAV_RIGHT) LastKey = CON_RIGHT;
        if (nav & HAL_NAV_UP)    LastKey = CON_UP;
        if (nav & HAL_NAV_DOWN)  LastKey = CON_DOWN;
        if (nav & HAL_NAV_A)     LastKey = CON_OK;
        if (nav & HAL_NAV_B)     LastKey = CON_EXIT;
    } else {
        // Fora do menu, as mesmas teclas também funcionam como joystick 1.
        if (nav & HAL_NAV_LEFT)  JoyState |= JST_LEFT;
        if (nav & HAL_NAV_RIGHT) JoyState |= JST_RIGHT;
        if (nav & HAL_NAV_UP)    JoyState |= JST_UP;
        if (nav & HAL_NAV_DOWN)  JoyState |= JST_DOWN;
        if (nav & HAL_NAV_A)     JoyState |= JST_FIREA;
        if (nav & HAL_NAV_B)     JoyState |= JST_FIREB;
    }
    InputLastPollUs = hal_time_us();
    return 0;
}

static void ProcessEventsIfStale(int min_age_us)
{
    const int64_t now = hal_time_us();
    if (!InputLastPollUs || now < InputLastPollUs ||
        now - InputLastPollUs >= min_age_us)
        ProcessEvents(0);
}

unsigned int Joystick(void)
{
    // Varios leitores do joystick no mesmo quadro reutilizam a matriz recente.
    ProcessEventsIfStale(2000);
    return JoyState;
}

void Keyboard(void)
{
    // A chamada imediatamente posterior a Joystick() aproveita a mesma
    // matriz; continua fazendo ao menos uma leitura por frame da maquina.
    stat_where = "keyboard";
    ProcessEventsIfStale(750);
    ++stat_emu_frames;
    hal_video_set_emu_frames(stat_emu_frames);
    // ResetMSX sincroniza PAL=50 ou NTSC=60 no HAL. A trava continua
    // por EPS (quadros emulados), independente do frame skip, e por isso
    // o numero de imagens apresentadas por segundo nunca supera o de EPS.
    if (!BenchmarkTurbo) {
        const int64_t t = (ProfileOn || FrameSkipAuto) ? hal_time_us() : 0;
        hal_video_pace_emu_frame();
        if (ProfileOn || FrameSkipAuto) {
            const uint32_t elapsed = (uint32_t)(hal_time_us() - t);
            if (ProfileOn) stat_pacing_us += elapsed;
            update_auto_frame_skip(elapsed);
        }
    }
    stat_where = "run";
}

unsigned int Mouse(byte N)
{
    return N == 0 ? hal_input_mouse() : 0;
}

unsigned int GetJoystick(void)
{
    ProcessEvents(0);
    return 0;
}

unsigned int GetMouse(void)
{
    return 0;
}

unsigned int GetKey(void)
{
    unsigned int J;
    ProcessEvents(0);
    J = LastKey;
    LastKey = 0;
    return J;
}

unsigned int WaitKey(void)
{
    LastKey = 0;
    wait_nav_release(1000000);
    while (!(hal_input_nav() & NAV_MASK))
        hal_delay_ms(10);
    return GetKey();
}

unsigned int WaitKeyOrMouse(void)
{
    LastKey = WaitKey();
    return 0;
}

// ---------------------------------------------------------------------------
// Vídeo
// ---------------------------------------------------------------------------
int InitMachine(void)
{
    if (!FrameBuf) {
        const size_t bytes = WIDTH * HEIGHT * sizeof(*FrameBuf);
        FrameBuf = (void *)(FbInternal ? hal_alloc_fast(bytes) : hal_alloc_big(bytes));
    }
    if (!FrameBuf) return 0;
    memset(FrameBuf, 0, WIDTH * HEIGHT * sizeof(*FrameBuf));

    XBuf = FrameBuf;
    NormScreen = (Image){
        .Data = FrameBuf,
        .W = WIDTH,
        .H = HEIGHT,
        .L = WIDTH,
        .D = MSX_NATIVE_RGB222 ? 8 : 16,
    };
    SetScreenDepth(NormScreen.D);
    SetVideo(&NormScreen, 0, 0, WIDTH, HEIGHT);
    // Reserva dois framebuffers de PSRAM e inicia o conversor VGA no core 1.
    // Renderizador fMSX continua usando XBuf; nada no VDP e alterado.
    if (VideoAsync && !VideoAsyncReady)
        VideoAsyncReady = hal_video_async_init(WIDTH * HEIGHT * sizeof(*FrameBuf));

    for (int J = 0; J < 80; J++)
        SetColor(J, 0, 0, 0);

    // Paleta fixa da SCREEN 8 (GRB 3-3-2).
    for (int J = 0; J < 256; J++) {
        const byte r = ((J >> 2) & 7) * 255 / 7;
        const byte g = ((J >> 5) & 7) * 255 / 7;
        const byte b = (J & 3) * 255 / 3;
#if MSX_NATIVE_RGB222
        // Reproduz bit-a-bit o arredondamento da antiga conversao RGB565.
        const unsigned r5 = r >> 3, g6 = g >> 2, b5 = b >> 3;
        BPal[J] = (((r5 * 3 + 15) / 31) << 4) |
                  (((g6 * 3 + 31) / 63) << 2) | ((b5 * 3 + 15) / 31);
#else
        BPal[J] = SWAP16(C_RGB(r, g, b));
#endif
    }

    InitSound(HAL_AUDIO_RATE, 150);
    SetChannels(96, 0xFFFFFFFF);

    RPLInit(SaveState, LoadState, MAX_STASIZE);
    RPLRecord(RPL_RESET);
    return 1;
}

void TrashMachine(void)
{
    RPLTrash();
    TrashSound();
}

void SetColor(byte N, byte R, byte G, byte B)
{
#if MSX_NATIVE_RGB222
    const unsigned r5 = R >> 3, g6 = G >> 2, b5 = B >> 3;
    const uint8_t color = (((r5 * 3 + 15) / 31) << 4) |
                          (((g6 * 3 + 31) / 63) << 2) | ((b5 * 3 + 15) / 31);
#else
    uint16_t color = SWAP16(C_RGB(R, G, B));
#endif
    if (N)
        XPal[N] = color;
    else
        XPal0 = color;
}

void PutImage(void)
{
    const int top = (ScanLines212 ? 8 : 18) + VAdjust;
    const int active_h = ScanLines212 ? 212 : 192;
    const unsigned border = (ScrMode == 8 || ScrMode == 10 ||
             ScrMode == 11 || ScrMode == 12) ? BPal[VDP[7]] :
             (ScrMode == 6 ? XPal[BGColor & 3] : XPal[BGColor]);

    ++stat_drawn_frames;
    stat_where = "video";
    int64_t t = ProfileOn ? hal_time_us() : 0;
    if (VideoAsyncReady) {
        // A VGA le exclusivamente o quadro CONCLUIDO. O emulador recebe outro
        // framebuffer antes de continuar o proximo frame. Em caso de fila
        // cheia, aguarda uma buffer liberado: nao existe tearing por reuse.
#if MSX_NATIVE_RGB222
        uint8_t *next = hal_video_async_submit_rgb222(FrameBuf, WIDTH, HEIGHT,
                                                      WIDTH, top, active_h, (uint8_t)border);
#else
        uint16_t *next = hal_video_async_submit(FrameBuf, WIDTH, HEIGHT,
                                                WIDTH, top, active_h, (uint16_t)border);
#endif
        if (next) {
            FrameBuf = next;
            XBuf = next;
            NormScreen.Data = next;
        }
    } else {
        hal_video_set_active_area(top, active_h);
#if MSX_NATIVE_RGB222
        hal_video_set_border_rgb222((uint8_t)border);
        hal_video_present_rgb222(FrameBuf, WIDTH, HEIGHT, WIDTH);
#else
        hal_video_set_border_rgb565((uint16_t)border);
        hal_video_present(FrameBuf, WIDTH, HEIGHT, WIDTH);
#endif
    }
    if (ProfileOn) stat_vga_us += (uint32_t)(hal_time_us() - t);
    stat_where = "run";
}

int ShowVideo(void)
{
#if MSX_NATIVE_RGB222
    hal_video_present_safe_rgb222(FrameBuf, WIDTH, HEIGHT, WIDTH);
#else
    hal_video_present_safe(FrameBuf, WIDTH, HEIGHT, WIDTH);
#endif
    return 1;
}

// ---------------------------------------------------------------------------
// Áudio (o EMULib gera PCM mono; o HAL leva até o DAC)
// ---------------------------------------------------------------------------
unsigned int InitAudio(unsigned int Rate, unsigned int Latency)
{
    (void)Rate; (void)Latency;
    return HAL_AUDIO_RATE;
}

void TrashAudio(void)
{
}

unsigned int GetFreeAudio(void)
{
    // WriteAudio bloqueia se a fila encher, então não devolva menos que um bloco curto.
    unsigned room = hal_audio_free();
    return room > 32 ? room : 32;
}

void PlayAllSound(int uSec)
{
    // Turbo replicates ESPectrum's benchmark concept: disable sound synthesis
    // and frame pacing together, never mix turbo with normal audio timing.
    if (BenchmarkTurbo) return;
    int64_t t = ProfileOn ? hal_time_us() : 0;
    // Preserva a fração de amostra entre chamadas curtas (uma por grupo de scanlines).
#if HAL_AUDIO_RATE == 24000
    /* O quociente audio/time e exato: 24000/1000000 = 3/125.
     * Evita duas divisoes por 1.000.000 em inteiros de 64 bits a cada
     * bloco de 8 scanlines. Conserva EXATAMENTE as amostras e o backlog.
     * Nos casos comuns a divisao por 125 usa apenas aritmetica de 32 bits. */
    static uint64_t fraction125;
    fraction125 += (uint64_t)(unsigned int)uSec * 3u;
    unsigned int samples;
    if (fraction125 <= UINT32_MAX) {
        const uint32_t accum = (uint32_t)fraction125;
        samples = accum / 125u;
        fraction125 = accum % 125u;
    } else {
        samples = (unsigned int)(fraction125 / 125u);
        fraction125 %= 125u;
    }
    unsigned int played = RenderAndPlayAudio(samples);
    if (played < samples)
        fraction125 += (uint64_t)(samples - played) * 125u;
#else
    /* Outras taxas personalizadas preservam o calculo original. */
    static uint64_t fraction;
    fraction += (uint64_t)uSec * HAL_AUDIO_RATE;
    unsigned int samples = (unsigned int)(fraction / 1000000u);
    fraction %= 1000000u;
    unsigned int played = RenderAndPlayAudio(samples);
    if (played < samples)
        fraction += (uint64_t)(samples - played) * 1000000u;
#endif
    if (ProfileOn) stat_sound_us += (uint32_t)(hal_time_us() - t);
}

unsigned int WriteAudio(sample *Data, unsigned int Length)
{
    stat_where = "audio-write";     // se o emulador parar aqui, o áudio não está drenando
    hal_audio_write((const int16_t *)Data, Length);
    stat_where = "run";
    return Length;
}

// ---------------------------------------------------------------------------
// Partida: escolhe a pasta da BIOS, lê o /sd/bootl.rc e chama o fMSX
// ---------------------------------------------------------------------------
static int file_exists(const char *dir, const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

// MSX2+ requer as ROMs principal e de extensao, ambas na pasta ROMS.
static int bios_folder_ok(const char *dir)
{
    return file_exists(dir, "MSX2P.ROM") && file_exists(dir, "MSX2PEXT.ROM");
}

static char BiosDir[192];
static char RomArg[192], DiskAArg[192], DiskBArg[192];
static char SkipArg[8];

static void absolute_sd_path(char *dst, size_t n, const char *value)
{
    if (strncmp(value, "/sd/", 4) == 0 || strcmp(value, "/sd") == 0)
        snprintf(dst, n, "%s", value);
    else
        snprintf(dst, n, "/sd/%s", value[0] == '/' ? value + 1 : value);
}

static int select_bios_folder(void)
{
    // Nova estrutura padrao do cartao FAT32: /MSX 2+ FAST/ROMS/
    static const char *candidates[] = {
        "/sd/MSX2/ROMS", "/sd/MSX2/roms",
    };
    char value[160];
    if (hal_rc_get("msx_biosdir", value, sizeof(value)) && value[0]) {
        absolute_sd_path(BiosDir, sizeof(BiosDir), value);
        return bios_folder_ok(BiosDir);
    }
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (bios_folder_ok(candidates[i])) {
            snprintf(BiosDir, sizeof(BiosDir), "%s", candidates[i]);
            return 1;
        }
    }
    snprintf(BiosDir, sizeof(BiosDir), "/sd/MSX2/ROMS");
    return 0;
}

void msx_run(void)
{
    char value[160];

    if (hal_rc_get("msx_frameskip", value, sizeof(value))) {
        const int v = atoi(value);
        if (!strcmp(value, "auto") || !strcmp(value, "AUTO"))
            FrameSkipAuto = 1;
        else if (v == 15 || v == 25 || v == 35 || v == 50 || v == 75) {
            FrameSkipAuto = 1;
            hal_log("msx_frameskip=%d legado; substituido por AUTO\n", v);
        } else FrameSkipAuto = 0;
    }
    if (hal_rc_get("msx_fbram", value, sizeof(value)))
        FbInternal = atoi(value) != 0;
    if (hal_rc_get("msx_turbo", value, sizeof(value)))
        BenchmarkTurbo = atoi(value) != 0;
    // Cache de linhas VGA (padrao DESLIGADO): em scroll faz duas leituras
    // de PSRAM por pixel sem obter acertos. Ative em telas estaticas.
    if (hal_rc_get("msx_rowcache", value, sizeof(value)))
        hal_video_set_row_cache(atoi(value) != 0);
    // Permite comparar o custo de busca da BIOS em PSRAM x DRAM sem
    // alterar a ROM do jogo nem os bancos de RAM do computador emulado.
    if (hal_rc_get("msx_fastbios", value, sizeof(value)))
        hal_set_fast_bios(atoi(value) != 0);
    /* Cache compartilhado de 8KiB/pagina: 128KiB padrao, 64 opcional,
       0 desliga sem afetar o acesso direto da MegaROM na PSRAM. */
    int romcache_kb=128;
    if (hal_rc_get("msx_romcache_kb", value, sizeof(value)))
        romcache_kb=atoi(value);
    if (romcache_kb!=0 && romcache_kb!=64 && romcache_kb!=128) romcache_kb=128;
    MSXSetRomCacheKiB(romcache_kb);
    hal_log("MegaROM cache configurado: %dKiB (sujeito a RAM interna livre)\n", romcache_kb);
    // TMS9918: 0 = RGB oficial MAME (padrao); 1 = analogica experimental.
    if (hal_rc_get("msx_tms_palette", value, sizeof(value)))
        TMSPaletteVariant = atoi(value) != 0;
    if (hal_rc_get("msx_profile", value, sizeof(value)))
        ProfileOn = atoi(value) != 0;
    hal_log("Perf v35: async VGA=%s, framebuffer=%s, turbo=%s, profile=%s\n",
            VideoAsync ? "ON" : "OFF",
            FbInternal ? "RAM interna (tentativa)" : "PSRAM",
            BenchmarkTurbo ? "ON/SEM PCM" : "OFF",
            ProfileOn ? "ON" : "OFF");
    /* SCC/SCC+ audio: OFF por padrao, opcionalmente ON no F10 ou no rc.
       Nao interfere com mapeamento de bancos de cartuchos Konami. */
    if (hal_rc_get("msx_scc", value, sizeof(value)))
        SCCEnabled = atoi(value) != 0;
    hal_log("SCC/SCC+ audio inicial=%s (menu F10)\n", SCCEnabled ? "ON" : "OFF");
    hal_log("MSX video interno: %s; VGA: RGB222 64 cores\n",
            MSX_NATIVE_RGB222 ? "RGB222 8bpp (v30)" : "RGB565 16bpp (v29)");
    if (hal_rc_get("msx_fm", value, sizeof(value)))
        SetFMEnabled(atoi(value) != 0);
    // A porcentagem efetiva do modo AUTO sera calculada em execucao.
    FrameSkip = 0;
    msx_auto_fs_reset(&AutoSkipState);
    UPeriod = 100;
    hal_video_set_frameskip_state(FrameSkipAuto, FrameSkip);
    snprintf(SkipArg, sizeof(SkipArg), "0");

    if (!select_bios_folder()) {
        hal_screen_message("MSX2+ BIOS not found",
                           "Copy MSX2P.ROM and MSX2PEXT.ROM to /MSXX2/ROMS/");
        hal_log("BIOS MSX2+: MSX2P.ROM or MSX2PEXT.ROM not found in %s\n", BiosDir);
        return;
    }
    hal_log("BIOS: %s\n", BiosDir);

    const char *argv[24];
    int argc = 0;
    argv[argc++] = "fmsx";
    // A opcao -ntsc precisa estar explicita no primeiro boot. O valor
    // MSX_NTSC do Mode sozinho nao configura todo o caminho de partida.
    argv[argc++] = "-ntsc";
    argv[argc++] = "-msx2+";
    argv[argc++] = "-ram";   argv[argc++] = "32";    // MSX2+: 512 KiB de RAM (32 x 16 KiB)
    argv[argc++] = "-vram";  argv[argc++] = "8";     // V9958: 128 KiB de VRAM (8 x 16 KiB)
    argv[argc++] = "-skip";  argv[argc++] = SkipArg;
    argv[argc++] = "-home";  argv[argc++] = BiosDir;
    argv[argc++] = "-joy";   argv[argc++] = "1";

    if (hal_rc_get("msx_diska", value, sizeof(value)) && value[0]) {
        absolute_sd_path(DiskAArg, sizeof(DiskAArg), value);
        argv[argc++] = "-diska"; argv[argc++] = DiskAArg;
    }
    if (hal_rc_get("msx_diskb", value, sizeof(value)) && value[0]) {
        absolute_sd_path(DiskBArg, sizeof(DiskBArg), value);
        argv[argc++] = "-diskb"; argv[argc++] = DiskBArg;
    }
    if (hal_rc_get("msx_rom", value, sizeof(value)) && value[0]) {
        absolute_sd_path(RomArg, sizeof(RomArg), value);
        argv[argc++] = RomArg;
    }
    argv[argc] = NULL;

    // Sincroniza o limitador antes mesmo de o primeiro frame do Z80 rodar.
    // O menu F10 continua podendo selecionar PAL mais tarde.
    hal_video_set_system_hz(60u);
    hal_log("fMSX start: NTSC 60 Hz inicial\n");
    stat_where = "start";
    int rc = fmsx_main(argc, (char **)argv);
    hal_log("fMSX ended (%d)\n", rc);
    if (rc) {
        hal_screen_message("fMSX terminou com erro", "Veja o monitor serial (115200)");
        return;
    }
    hal_restart();   // "Reboot" no menu do fMSX
}

// Diagnóstico (usado pela tarefa de FPS do main.cpp).
void msx_stats(unsigned *emu_frames, unsigned *drawn_frames_total, const char **where)
{
    *emu_frames = stat_emu_frames;
    *drawn_frames_total = stat_drawn_frames;
    *where = (const char *)stat_where;
}
// Contadores cumulativos para o log serial (sem executar conversao com serial no caminho quente).
// Leitura informativa: nao modifica o estado de nenhum chip.
unsigned int msx_audio_active_mask(void)
{
    return ActiveSoundMask();
}

int msx_scc_audio_enabled(void)
{
    return SCCEnabled;
}

void msx_profile_stats(uint32_t *video_us, uint32_t *sound_us, uint32_t *pacing_us)
{
    *video_us = stat_vga_us;
    *sound_us = stat_sound_us;
    *pacing_us = stat_pacing_us;
}
