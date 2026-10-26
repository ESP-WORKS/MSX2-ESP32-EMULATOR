// hal_input.cpp - teclado e mouse PS/2 pela FabGL
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "fabgl.h"
#include "hal.h"
#include "board.h"
#include "msx_abnt2.h"
#include "msx_f12_debounce.h"

using namespace fabgl;

static Keyboard keyboard;
static Mouse mouse;
static KeyboardLayout physical_layout;
static KeyboardLayout br_physical_layout;
static bool input_ready;
static bool br_keyboard_trace = false; // msx_kbd_trace=1 (somente diagnostico)
static int active_keyboard_profile = -1;
static MSXABNT2Table br_bios_table;
/* BIOS carregada via LoadROM() reside em memoria alocada (nao e o SD).
 * Hotbit 1.2 tem tres slots vazios na tabela Graph+Shift. Ela nao
 * fornece { nem }. O apostrofo JA esta na tabela SHIFT em 1:6
 * (indice 14), sem necessidade de sintetizar GRAPH+SHIFT.
 * Enquanto o layout BR estiver ON, sobreponha somente os dois slots;
 * ao desligar, restaure os bytes originais.
 * Nao interceptamos nenhuma leitura de instrucao Z80, preservando FPS. */
static uint8_t *br_bios_copy = nullptr;
static size_t br_bios_bytes = 0;
static uint8_t br_saved_left = 0, br_saved_right = 0;
static bool br_hotbit_braces_patched = false;

/* A tecla de acento do perfil internacional usa o deadkey autentico da
 * BIOS (2:5). Nao editar os bytes de teclado na ROM em RAM; a antiga
 * sobreposicao de crase ASCII literal foi eliminada na v38.5. */
static bool br_hotbit_table_matches(void)
{
    return br_bios_copy && br_bios_bytes >= 0x0E35u + 24u &&
           br_bios_table.from_bios &&
           br_bios_table.normal[13] == 0xFF &&
           br_bios_table.normal[14] == 0xFF &&
           br_bios_table.normal[16] == 0xFF &&
           br_bios_table.normal[17] == '[' &&
           br_bios_table.shifted[17] == ']' &&
           br_bios_table.normal[23] == 'b' &&
           br_bios_table.graph[21] == '|';
}

static void br_update_hotbit_braces(bool enable)
{
    const unsigned offset = 0x0E35u;
    if (br_hotbit_braces_patched && !enable) {
        /* Uma ROM pode ter sido desalocada e realocada no mesmo endereco.
           Nao restaure bytes se a copia corrente nao tem nossa marca. */
        if (br_bios_copy[offset + 1] == '{' &&
            br_bios_copy[offset + 4] == '}') {
            br_bios_copy[offset + 1] = br_saved_left;
            br_bios_copy[offset + 4] = br_saved_right;
            br_bios_table.graph_shifted[1] = br_saved_left;
            br_bios_table.graph_shifted[4] = br_saved_right;
        }
        br_hotbit_braces_patched = false;
    }
    if (!enable || br_hotbit_braces_patched || !br_hotbit_table_matches())
        return;
    /* Evita substituir alguma funcionalidade de outra revisao de ROM. */
    if (br_bios_copy[offset + 1] != 0 || br_bios_copy[offset + 4] != 0)
        return;
    br_saved_left = br_bios_copy[offset + 1];
    br_saved_right = br_bios_copy[offset + 4];
    br_bios_copy[offset + 1] = '{';
    br_bios_copy[offset + 4] = '}';
    br_bios_table.graph_shifted[1] = '{';
    br_bios_table.graph_shifted[4] = '}';
    br_hotbit_braces_patched = true;
    hal_log("ABNT2 Hotbit: chaves { } habilitadas em RAM (apostrofo nativo)\n");
}

extern "C" void hal_input_set_keyboard_bios(uint8_t *rom, size_t bytes)
{
    /* Reset ou troca de BIOS: a copia antiga pode ter sido desalocada.
       Restauramos somente se o ponteiro ainda e o mesmo. */
    if (br_bios_copy == rom) {
        if (br_hotbit_braces_patched) br_update_hotbit_braces(false);
    }
    br_hotbit_braces_patched = false;
    br_bios_copy = rom;
    br_bios_bytes = bytes;
    msx_abnt2_init_table(&br_bios_table);
    if (msx_abnt2_load_bios(&br_bios_table, rom, bytes))
        hal_log("ABNT2: matriz extraida da BIOS ativa (0DA5h/0DD5h)\n");
    else
        hal_log("ABNT2: BIOS sem tabela reconhecida; matriz internacional padrao\n");
    br_update_hotbit_braces(active_keyboard_profile == 2);
}


/* FabGL usa set 2. ABNT2 tem duas teclas adicionais (0x61 e 0x51)
 * e posições de cedilha / acentos diferentes do teclado US.
 * Esta camada preserva as teclas F10/F11/F12 e os cursores. */
/* O decodificador real da FabGL (Keyboard::scancodeToVK) percorre a
 * tabela ATE encontrar scancode==0, sem checar comprimento. Ao acrescentar
 * as duas teclas fisicas extras ABNT2 (0x51/0x61) precisamos deixar SEMPRE
 * um marcador final. Se uma revisao FabGL entregar tabela cheia, liberar
 * primeiro ScrollLock ou KP* (nao usados pelo teclado MSX), sem perder
 * caracteres das linhas principais nem Shift/Alt/teclas de funcao. */
static bool br_release_unused_scan(KeyboardLayout *layout)
{
    static const uint8_t optional[] = {0x7e, 0x7c};
    const size_t count = sizeof(layout->scancodeToVK) / sizeof(layout->scancodeToVK[0]);
    for (uint8_t scan:optional)
        for (size_t i=0;i+1<count && layout->scancodeToVK[i].scancode;i++)
            if (layout->scancodeToVK[i].scancode == scan) {
                /* Mover inclusive o terminador NUL para evitar leitura
                   fora da tabela pelo decodificador FabGL. */
                for (size_t j=i;j+1<count;j++)
                    layout->scancodeToVK[j] = layout->scancodeToVK[j+1];
                layout->scancodeToVK[count-1].scancode=0;
                return true;
            }
    return false;
}

static bool override_scan(KeyboardLayout *layout, uint8_t scancode, VirtualKey key)
{
    const size_t count=sizeof(layout->scancodeToVK)/sizeof(layout->scancodeToVK[0]);
    size_t i=0;
    while(i<count && layout->scancodeToVK[i].scancode) {
        if(layout->scancodeToVK[i].scancode==scancode) {
            layout->scancodeToVK[i].virtualKey=key;
            return true;
        }
        ++i;
    }
    /* Precisa haver um slot para a tecla E outro para scancode=0. */
    if(i+1>=count) {
        if(!br_release_unused_scan(layout)) return false;
        i=0;
        while(i<count && layout->scancodeToVK[i].scancode) ++i;
        if(i+1>=count) return false;
    }
    layout->scancodeToVK[i].scancode=scancode;
    layout->scancodeToVK[i].virtualKey=key;
    layout->scancodeToVK[i+1].scancode=0;
    return true;
}

// Perfil de pinos vindo do /sd/bootl.rc (kbddat/kbdclk). Só aceita 32/33 ou 27/26.
static bool keyboard_on_port_b(void)
{
    char v[16];
    int data = PIN_PS2_A_DATA, clk = PIN_PS2_A_CLK;
    bool have_data = false, have_clk = false;
    if (hal_rc_get("kbddat", v, sizeof(v))) { data = atoi(v); have_data = true; }
    if (hal_rc_get("kbdclk", v, sizeof(v))) { clk = atoi(v); have_clk = true; }
    if (have_data && have_clk && data == PIN_PS2_B_DATA && clk == PIN_PS2_B_CLK)
        return true;
    if (have_data || have_clk) {
        if (!(have_data && have_clk && data == PIN_PS2_A_DATA && clk == PIN_PS2_A_CLK))
            hal_log("bootl.rc: perfil PS/2 invalido, usando DATA32 CLK33\n");
    }
    return false;
}

extern "C" void hal_input_init(void)
{
    if (input_ready) return;
    char trace_value[16];
    br_keyboard_trace = hal_rc_get("msx_kbd_trace", trace_value,
                                   sizeof(trace_value)) && atoi(trace_value) != 0;
    msx_abnt2_init_table(&br_bios_table);
    gpio_num_t kbdData, kbdClk, mouseData, mouseClk;
    if (keyboard_on_port_b()) {
        kbdData = (gpio_num_t)PIN_PS2_B_DATA;   kbdClk = (gpio_num_t)PIN_PS2_B_CLK;
        mouseData = (gpio_num_t)PIN_PS2_A_DATA; mouseClk = (gpio_num_t)PIN_PS2_A_CLK;
    } else {
        kbdData = (gpio_num_t)PIN_PS2_A_DATA;   kbdClk = (gpio_num_t)PIN_PS2_A_CLK;
        mouseData = (gpio_num_t)PIN_PS2_B_DATA; mouseClk = (gpio_num_t)PIN_PS2_B_CLK;
    }
    hal_log("PS/2: teclado DATA=%d CLK=%d, mouse DATA=%d CLK=%d\n",
            (int)kbdData, (int)kbdClk, (int)mouseData, (int)mouseClk);

    PS2Controller::begin(kbdClk, kbdData, mouseClk, mouseData);
    PS2Controller::setKeyboard(&keyboard);
    PS2Controller::setMouse(&mouse);
    keyboard.begin(true, true, 0);

    // O BIOS do MSX interpreta SHIFT/CTRL: evita que o host transforme SHIFT+2 em '@'.
    physical_layout = USLayout;
    memset(physical_layout.alternateVK, 0, sizeof(physical_layout.alternateVK));
    keyboard.setLayout(&physical_layout);
    /* A tabela scancodeToVK do USLayout ocupa todas as 86 entradas.
       Copia-la e tentar acrescentar 0x51 (/? ABNT2) e 0x61 (\\| ABNT2)
       falha silenciosamente, pois nao resta espaco para essas teclas.
       Use um layout derivado ESPARSO: as substituicoes ficam em nosso
       vetor (ate 9 posicoes) e todo o restante vem do physical_layout,
       que ja tem as conversoes de Shift/Alt desativadas. Assim nao ha
       perda das teclas US, do keypad nem do F10/F11/F12. */
    memset(&br_physical_layout, 0, sizeof(br_physical_layout));
    br_physical_layout.name = "BR-ABNT2";
    br_physical_layout.desc = "ABNT2 MSX (matriz fisica)";
    br_physical_layout.inherited = &physical_layout;
    /* Nao deixar a FabGL compor deadkeys no host.
       Todos os perfis enviam os contatos reais da matriz ao MSX,
       e a propria BIOS cuida da composicao dos acentos. */
    memset(br_physical_layout.deadKeysVK, 0, sizeof(br_physical_layout.deadKeysVK));
    memset(br_physical_layout.deadkeysToVK, 0, sizeof(br_physical_layout.deadkeysToVK));
    /* Scancodes de posição física PC ABNT2 (set 2). */
    static const struct { uint8_t scan; VirtualKey vk; } br_scan[] = {
        {0x0E, VK_QUOTE},          /* aspas/aspas duplas perto de 1 */
        {0x54, VK_ACUTEACCENT},    /* acento agudo/grave apos P */
        {0x5B, VK_LEFTBRACKET},   /* tecla [ */
        {0x5D, VK_RIGHTBRACKET},  /* tecla ] */
        {0x4C, VK_CEDILLA_c},     /* tecla Ç apos L */
        {0x52, VK_CARET},         /* circunflexo/til */
        {0x4A, VK_SEMICOLON},     /* ; : proximo de . */
        {0x51, VK_SLASH},         /* / ? ao lado do Shift direito */
        {0x61, VK_BACKSLASH},     /* \ | tecla extra ao lado do Shift esquerdo */
    };
    for (const auto &item : br_scan)
        if (!override_scan(&br_physical_layout, item.scan, item.vk))
            hal_log("ABNT2: sem slot para scancode %02x\n", item.scan);
    mouse.begin(1);
    input_ready = true;
}

static bool down(VirtualKey key) { return input_ready && keyboard.isVKDown(key); }

extern "C" void hal_input_set_keyboard_profile(int profile)
{
    if (profile < -1 || profile > 2) profile = -1;
    if (active_keyboard_profile == profile) return;
    /* Nao tocar na BIOS a cada frame: somente ao trocar o perfil. */
    br_update_hotbit_braces(profile == 2);
    active_keyboard_profile = profile;
    if (input_ready) {
        keyboard.setLayout(profile < 0 ? &physical_layout : &br_physical_layout);
        /* Perfil selecionado na leitura seguinte; nada altera ROM/RAM. */
        hal_log("Teclado PS/2: %s\n", profile < 0 ? "US original" :
                profile == 1 ? "ABNT2 Expert" :
                profile == 2 ? "ABNT2 Hotbit" : "ABNT2 MSX");
    }
}

// F11/F12 are handled from their current key state instead of relying on the
// FabGL virtual-key event queue. This makes the shortcuts deterministic even
// when the emulator polls input after the event has already been drained.
static MSXF12Debounce f11_debounce = {0, 0, 1};
static MSXF12Debounce f12_debounce = {0, 0, 1};

static void drain_events(void)
{
    // We use the current FabGL key state for shortcuts; still drain the event
    // queue so repeated F11/F12 packets cannot accumulate while the emulator
    // is busy.
    VirtualKeyItem item;
    for (int n = 0; n < 32 && keyboard.getNextVirtualKey(&item, 0); ++n) {
        /* Diagnostico opcional de teclados PS/2 fisicos: permite detectar
           revisoes cujo scancode ABNT2 difere de 0E, 51 ou 61. */
        if (br_keyboard_trace && active_keyboard_profile >= 0)
            hal_log("ABNT2 PS2: scan=%02X %02X vk=%d %s perfil=%d\n",
                    (unsigned)item.scancode[0], (unsigned)item.scancode[1],
                    (int)item.vk, item.down ? "DOWN" : "UP",
                    active_keyboard_profile);
    }
}

extern "C" uint32_t hal_input_nav(void)
{
    if (!input_ready) return 0;
    drain_events();
    uint32_t r = 0;

    const bool f11 = down(VK_F11);
    const bool f12 = down(VK_F12);
    const bool shift = down(VK_LSHIFT) || down(VK_RSHIFT);

    // Uma borda por pressionamento. F11 recebe o mesmo filtro contra
    // liberacoes falsas PS/2 usado anteriormente no Shift+F12 do OSD.
    const uint32_t now = millis();
    if (msx_f12_edge(&f11_debounce, f11, now)) {
        if (!hal_video_hotkey(11, shift)) r |= HAL_HOT_SKIP;
    }
    if (msx_f12_edge(&f12_debounce, f12, now)) {
        if (!hal_video_hotkey(12, shift)) r |= HAL_HOT_RESET;
    }

    if (down(VK_F10)) r |= HAL_HOT_MENU;
    if (down(VK_UP)    || down(VK_KP_UP))    r |= HAL_NAV_UP;
    if (down(VK_DOWN)  || down(VK_KP_DOWN))  r |= HAL_NAV_DOWN;
    if (down(VK_LEFT)  || down(VK_KP_LEFT))  r |= HAL_NAV_LEFT;
    if (down(VK_RIGHT) || down(VK_KP_RIGHT)) r |= HAL_NAV_RIGHT;
    // Botões do joystick 1: mantém os atalhos existentes e adiciona Enter/Esc.
    if (down(VK_LCTRL) || down(VK_SPACE) || down(VK_RETURN) || down(VK_KP_ENTER))
        r |= HAL_NAV_A;
    if (down(VK_LALT) || down(VK_ESCAPE)) r |= HAL_NAV_B;
    return r;
}

// Preenche keys[] com os códigos KBD_* do fMSX (MSX.h) das teclas apertadas.
extern "C" void hal_input_keys(uint8_t keys[256])
{
    memset(keys, 0, 256);
    if (!input_ready) return;
    drain_events();
    for (int i = 0; i < 26; ++i)
        keys['a' + i] = down(VirtualKey(VK_a + i)) || down(VirtualKey(VK_A + i));
    for (int i = 0; i < 10; ++i)
        keys['0' + i] = down(VirtualKey(VK_0 + i));
    static const uint8_t keypad_codes[] = {23, 24, 25, 26, 28, 29, 30, 31, 128, 129};
    for (int i = 0; i < 10; ++i)
        keys[keypad_codes[i]] = down(VirtualKey(VK_KP_0 + i));
    static const struct { VirtualKey vk; uint8_t code; } map[] = {
        {VK_LEFT, 1}, {VK_UP, 2}, {VK_RIGHT, 3}, {VK_DOWN, 4},
        {VK_LSHIFT, 5}, {VK_RSHIFT, 5}, {VK_LCTRL, 6}, {VK_RCTRL, 6},
        {VK_LALT, 7}, {VK_BACKSPACE, 8}, {VK_TAB, 9}, {VK_CAPSLOCK, 10},
        {VK_END, 11}, {VK_HOME, 12}, {VK_RETURN, 13}, {VK_KP_ENTER, 13},
        {VK_DELETE, 14}, {VK_INSERT, 15}, {VK_RALT, 16}, {VK_PAUSE, 17},
        {VK_F1, 18}, {VK_F2, 19}, {VK_F3, 20}, {VK_F4, 21}, {VK_F5, 22},
        {VK_ESCAPE, 27}, {VK_SPACE, 32}, {VK_MINUS, '-'}, {VK_EQUALS, '='},
        {VK_LEFTBRACKET, '['}, {VK_RIGHTBRACKET, ']'}, {VK_BACKSLASH, 92},
        {VK_SEMICOLON, ';'}, {VK_QUOTE, 39}, {VK_GRAVEACCENT, '`'},
        {VK_COMMA, ','}, {VK_PERIOD, '.'}, {VK_SLASH, '/'},
    };
    for (const auto &entry : map)
        keys[entry.code] |= down(entry.vk);

    if (active_keyboard_profile >= 0) {
        /* ABNT2 PC -> caractere pretendido -> tecla fisica da BIOS ativa.
         * Em particular, Shift+2 no Expert corresponde a outra posicao
         * matricial (Shift+2 no Expert gera aspas, nao arroba).
         * A conversao atua ANTES do KBD_SET: jogos e BASIC veem a matriz
         * verdadeira do modelo, sem injecao de ASCII ou edicao de ROM.
         */
        static const struct { VirtualKey key; unsigned physical; } positions[] = {
            {VK_QUOTE,BR_QUOTE},
            {VK_0,BR_0},{VK_1,BR_1},{VK_2,BR_2},{VK_3,BR_3},
            {VK_4,BR_4},{VK_5,BR_5},{VK_6,BR_6},{VK_7,BR_7},
            {VK_8,BR_8},{VK_9,BR_9},
            {VK_MINUS,BR_MINUS},{VK_EQUALS,BR_EQUALS},
            {VK_ACUTEACCENT,BR_ACUTE},{VK_LEFTBRACKET,BR_LEFTBRACKET},
            {VK_CEDILLA_c,BR_CEDILLA},{VK_CARET,BR_TILDE},
            {VK_RIGHTBRACKET,BR_RIGHTBRACKET},
            {VK_BACKSLASH,BR_BACKSLASH},{VK_COMMA,BR_COMMA},
            {VK_PERIOD,BR_PERIOD},{VK_SEMICOLON,BR_SEMICOLON},
            {VK_SLASH,BR_SLASH}
        };
        /* Correspondencia inversa da tabela Keys[] em MSX.c. A tecla
           virtual 130 e fundamental: ela e a linha 2/coluna 5 e nao
           pode ser ignorada (a versao antiga iterava somente ate 129). */
        static const uint8_t matrix_key[3][8] = {
            {41,33,50,35,36,37,54,38},
            {42,40,45,43,92,91,93,58},
            {34,96,44,46,47,130,65,66}
        };
        static const uint8_t punctuation[] = {
            '\'', '`', '[', ']', ';', '/', '\\', ',', '.', '-', '=', 130
        };
        const bool physical_shift=down(VK_LSHIFT)||down(VK_RSHIFT);
        bool translated=false;
        int target_shift=physical_shift;
        int target_graph=0;
        int target_code=0;
        for (int i=0;i<10;++i) keys['0'+i]=0;
        for (auto code:punctuation) keys[code]=0;
        for (const auto &pos:positions) {
            if (!down(pos.key)) continue;
            MSXABNT2Result selected=msx_abnt2_lookup(&br_bios_table,
                active_keyboard_profile,pos.physical,physical_shift);
            if (!selected.valid || selected.row>2 || selected.column>7) continue;
            keys[matrix_key[selected.row][selected.column]]=1;
            /* A matriz do MSX tem somente um Shift e um Graph globais.
               Em combinacoes com pontuacao, traduzir tambem o modificador
               para obter o caractere ABNT2 escolhido. */
            translated=true;
            target_shift=selected.shift;
            target_graph=selected.graph;
            target_code=selected.code;
        }
        if (translated) {
            keys[5]=!!target_shift;
            keys[7]=!!target_graph || down(VK_LALT);
            keys[16]=!!target_code || down(VK_RALT);
        }
    }
}

// Posição acumulada (0..255) nos 16 bits baixos; botões esquerdo/direito nos bits 16 e 17.
extern "C" uint32_t hal_input_mouse(void)
{
    static uint8_t x = 128, y = 128;
    static uint32_t buttons = 0;
    if (!input_ready) return 0;
    MouseDelta delta;
    while (mouse.getNextDelta(&delta, 0)) {
        x += delta.deltaX;
        y -= delta.deltaY;
        buttons = (delta.buttons.left ? 1u << 16 : 0) | (delta.buttons.right ? 1u << 17 : 0);
    }
    return x | (uint32_t(y) << 8) | buttons;
}
