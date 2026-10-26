// Testes da validação de pinos do bootl.rc, rodando no PC: pio test -e native
//
// O hal_pins.cpp é incluído como fonte para os testes alcançarem os estáticos do
// arquivo (pins, loaded) e poderem reiniciar o estado entre casos. Os três símbolos
// que ele usa do resto do firmware (bootl.rc, log e MISO do SD) são simulados aqui.
#include <unity.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"

struct RcEntry { const char *key; const char *value; };

static const RcEntry *rc_table;
static int rc_count;
static int sd_miso_stub = 2;          // LilyGO TTGO VGA32
static int rc_present_stub = 1;

extern "C" {

int hal_rc_present(void) { return rc_present_stub; }

int hal_rc_get(const char *key, char *out, size_t out_size)
{
    for (int i = 0; i < rc_count; ++i)
        if (strcmp(rc_table[i].key, key) == 0) {
            snprintf(out, out_size, "%s", rc_table[i].value);
            return 1;
        }
    return 0;
}

void hal_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

int hal_sd_miso_pin(void) { return sd_miso_stub; }

}  // extern "C"

#include "hal_pins.cpp"

template <int N>
static const hal_pins_t *load(const RcEntry (&entries)[N])
{
    rc_table = entries;
    rc_count = N;
    pins = hal_pins_t{ PIN_PS2_A_DATA, PIN_PS2_A_CLK, PIN_PS2_B_DATA, PIN_PS2_B_CLK,
                       PIN_MAGIC_BUTTON };
    kbd_origin = mouse_origin = "padrao";
    magic_origin = "sem a chave magicb";
    loaded = false;
    return hal_pins();
}

static void expect(const hal_pins_t *p, int kd, int kc, int md, int mc, int magic)
{
    TEST_ASSERT_EQUAL_INT(kd, p->kbd_data);
    TEST_ASSERT_EQUAL_INT(kc, p->kbd_clk);
    TEST_ASSERT_EQUAL_INT(md, p->mouse_data);
    TEST_ASSERT_EQUAL_INT(mc, p->mouse_clk);
    TEST_ASSERT_EQUAL_INT(magic, p->magic_button);
}

// Sem nenhuma chave de pino no arquivo, nada muda.
static void test_sem_chaves_usa_padrao(void)
{
    const RcEntry none[] = { { "msx_fm", "1" } };
    expect(load(none), 32, 33, 27, 26, HAL_NO_PIN);
}

// Sem o arquivo, o bootl.rc nem é consultado.
static void test_sem_arquivo_usa_padrao(void)
{
    const RcEntry rc[] = { { "kbddat", "27" }, { "kbdclk", "26" }, { "magicb", "36" } };
    rc_present_stub = 0;
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
    rc_present_stub = 1;
}

// Os quatro pinos declarados, trocando teclado e mouse de porta.
static void test_quatro_pinos_explicitos(void)
{
    const RcEntry rc[] = {
        { "kbddat", "27" }, { "kbdclk", "26" },
        { "mousedat", "32" }, { "mouseclk", "33" },
    };
    expect(load(rc), 27, 26, 32, 33, HAL_NO_PIN);
}

// bootl.rc antigo: só o teclado muda, e o mouse sai da frente sozinho.
static void test_compatibilidade_so_teclado(void)
{
    const RcEntry rc[] = { { "kbddat", "27" }, { "kbdclk", "26" } };
    expect(load(rc), 27, 26, 32, 33, HAL_NO_PIN);
}

// GPIO 34-39 só leem: não servem para PS/2.
static void test_ps2_recusa_pino_de_entrada(void)
{
    const RcEntry rc[] = { { "kbddat", "34" }, { "kbdclk", "33" } };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

static void test_ps2_recusa_pino_do_vga(void)
{
    const RcEntry rc[] = { { "kbddat", "22" }, { "kbdclk", "21" } };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

// O PS/2 da FabGL roda no ULP: GPIO sem função RTC não serve, mesmo estando livre.
// O 5 e o 18 só aparecem livres se outra placa não usar o VGA neles.
static void test_ps2_recusa_pino_sem_rtc(void)
{
    const RcEntry rc[] = { { "kbddat", "5" }, { "kbdclk", "18" } };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

// Aceita os que têm RTC e estão livres. Numa RobGo-RG (MISO 35) sobram 0 e 2.
static void test_ps2_aceita_pino_com_rtc(void)
{
    const RcEntry rc[] = {
        { "kbddat", "0" }, { "kbdclk", "2" },
        { "mousedat", "26" }, { "mouseclk", "27" },
    };
    sd_miso_stub = 35;
    expect(load(rc), 0, 2, 26, 27, HAL_NO_PIN);
    sd_miso_stub = 2;
}

// O MISO que montou o cartão também é intocável, e ele varia por placa.
static void test_ps2_recusa_miso_do_sd(void)
{
    const RcEntry rc[] = { { "kbddat", "2" }, { "kbdclk", "0" } };
    sd_miso_stub = 2;
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);

    sd_miso_stub = 35;                 // RobGo-RG: aí o GPIO2 fica livre
    expect(load(rc), 2, 0, 27, 26, HAL_NO_PIN);
    sd_miso_stub = 2;
}

static void test_ps2_recusa_pino_repetido(void)
{
    const RcEntry rc[] = {
        { "kbddat", "32" }, { "kbdclk", "33" },
        { "mousedat", "32" }, { "mouseclk", "26" },
    };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

// DATA sem CLK é configuração pela metade: volta tudo ao padrão.
static void test_ps2_exige_par_data_clk(void)
{
    const RcEntry rc[] = { { "kbddat", "27" } };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

static void test_ps2_recusa_valor_nao_numerico(void)
{
    const RcEntry rc[] = { { "kbddat", "GPIO27" }, { "kbdclk", "26" } };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

// O botão aceita pino de entrada apenas, que é o caso do GPIO36 do exemplo.
static void test_magicb_aceita_pino_de_entrada(void)
{
    const RcEntry rc[] = { { "magicb", "36" } };
    expect(load(rc), 32, 33, 27, 26, 36);
}

static void test_magicb_recusa_pino_ocupado(void)
{
    const RcEntry vga[] = { { "magicb", "22" } };
    expect(load(vga), 32, 33, 27, 26, HAL_NO_PIN);

    const RcEntry inexistente[] = { { "magicb", "99" } };
    expect(load(inexistente), 32, 33, 27, 26, HAL_NO_PIN);

    const RcEntry do_ps2[] = { { "magicb", "32" } };
    expect(load(do_ps2), 32, 33, 27, 26, HAL_NO_PIN);
}

static void test_magicb_menos_um_desliga(void)
{
    const RcEntry rc[] = { { "magicb", "-1" } };
    expect(load(rc), 32, 33, 27, 26, HAL_NO_PIN);
}

// Teclado movido para o 36 e botão no 32 liberado: as duas regras convivem.
static void test_magicb_livre_apos_mover_o_ps2(void)
{
    const RcEntry rc[] = {
        { "kbddat", "27" }, { "kbdclk", "26" },
        { "mousedat", "0" }, { "mouseclk", "2" },
        { "magicb", "32" },
    };
    sd_miso_stub = 35;
    expect(load(rc), 27, 26, 0, 2, 32);
    sd_miso_stub = 2;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sem_chaves_usa_padrao);
    RUN_TEST(test_sem_arquivo_usa_padrao);
    RUN_TEST(test_quatro_pinos_explicitos);
    RUN_TEST(test_compatibilidade_so_teclado);
    RUN_TEST(test_ps2_recusa_pino_de_entrada);
    RUN_TEST(test_ps2_recusa_pino_do_vga);
    RUN_TEST(test_ps2_recusa_pino_sem_rtc);
    RUN_TEST(test_ps2_aceita_pino_com_rtc);
    RUN_TEST(test_ps2_recusa_miso_do_sd);
    RUN_TEST(test_ps2_recusa_pino_repetido);
    RUN_TEST(test_ps2_exige_par_data_clk);
    RUN_TEST(test_ps2_recusa_valor_nao_numerico);
    RUN_TEST(test_magicb_aceita_pino_de_entrada);
    RUN_TEST(test_magicb_recusa_pino_ocupado);
    RUN_TEST(test_magicb_menos_um_desliga);
    RUN_TEST(test_magicb_livre_apos_mover_o_ps2);
    return UNITY_END();
}
