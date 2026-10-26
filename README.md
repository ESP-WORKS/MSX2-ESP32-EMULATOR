# MSX2 para VGA32 (RobGo-RG)

Emulador MSX2 (fMSX 6.0 de Marat Fayzullin) rodando direto no ESP32-WROVER da TTGO VGA32 / RobGo-RG.
Sem launcher, sem Retro-Go, sem ESP-IDF externo: um projeto PlatformIO normal (Arduino + FabGL).

## Compilar

Abra a pasta no VS Code (extensão PlatformIO) e use **Build** e **Upload**. O `platformio.ini` já tem tudo.

A validação dos pinos do `bootl.rc` tem teste que roda no PC, sem placa:

    pio test -e native

## Cartão SD

Formato FAT32. Coloque a BIOS na pasta `/MSX2` da raiz do cartão.

| Arquivo | Uso |
|---|---|
| `MSX2.ROM` (32 KB) | obrigatório |
| `MSX2EXT.ROM` (16 KB) | obrigatório |
| `DISK.ROM` | necessário para disquetes |
| `MSXDOS2.ROM` | opcional (MSX-DOS 2) |

A BIOS não vem no projeto. Os nomes precisam ser exatamente estes.
ROMs, disquetes (`.dsk`) e estados (`.sta`) são escolhidos pelo menu do fMSX (F10).

## Teclas (teclado PS/2)

| Tecla | Ação |
|---|---|
| F10 | menu do fMSX: abrir arquivo, salvar estado, modelo, discos, reset, reboot |
| botão em `magicb` | igual ao F10, se configurado no `bootl.rc` |
| F12 | reset do MSX |
| F11 | alterna frame skip (0 / 25 / 50 / 75 %) |
| F1–F5 | F1–F5 do MSX |
| Setas + Espaço/Enter + Ctrl/Esc | também funcionam como joystick 1 |
| Mouse PS/2 | mouse do MSX |

## /sd/bootl.rc (opcional)

Arquivo de texto UTF-8, uma linha `chave=valor`; `#` e `;` comentam.
Lido logo depois de o cartão montar. Há um modelo comentado em `bootl.rc.example`.

| Chave | Efeito |
|---|---|
| `kbddat=32`, `kbdclk=33` | GPIOs DATA e CLK do teclado PS/2 |
| `mousedat=27`, `mouseclk=26` | GPIOs DATA e CLK do mouse PS/2 |
| `magicb=36` | GPIO de um botão que abre o menu do fMSX (igual ao F10) |
| `msx_biosdir=/msx` | raiz da BIOS; os arquivos ficam em `/msx/MSX2/` |
| `msx_rom=/msx/jogo.rom` | cartucho carregado ao ligar |
| `msx_diska=/msx/disco.dsk`, `msx_diskb=...` | disquetes carregados ao ligar |
| `msx_frameskip=50` | quadros não desenhados: 0, 25, 50 ou 75 |
| `msx_autoskip=1` | ajuste automático do frame skip |
| `msx_fm=1` | síntese FM (YM2413) ligada/desligada |
| `msx_fps=1` | contador de FPS no monitor serial |

## Pinos

Os padrões estão em `src/board.h`: VGA em 22/21/19/18/5/4 com HSync 23 e VSync 15,
teclado PS/2 em 32/33, mouse PS/2 em 27/26, SD com CS 13, MOSI 12 e CLK 14,
áudio no DAC interno GPIO25 (mono).

O MISO do SD é tentado em GPIO35 e depois em GPIO2, cada um a 20, 10 e 4 MHz; o
primeiro que montar vale, e o serial mostra qual foi. Isso é fixo no firmware.

Teclado, mouse e botão de menu, ao contrário, saem do `bootl.rc` e não precisam de
recompilação — é assim que outras placas são atendidas. O PS/2 sobe antes do VGA:
o reset do teclado não completa com a interrupção de vídeo já ligada. No serial,
`teclado PS/2: respondeu ao reset` confirma que o cabo falou; `NAO respondeu` é
falha de fio, de pino ou de teclado desligado. O firmware recusa um GPIO que:

- não exista neste ESP32 (os pads 20, 24, 28–31, 37 e 38 não saem do WROVER);
- já pertença ao VGA, ao SD (incluindo o MISO que venceu o probe), ao áudio, à UART0,
  à flash ou à PSRAM;
- esteja repetido entre teclado e mouse;
- seja de entrada apenas (34–39) num papel de PS/2, que precisa escrever no pino;
- **não tenha função RTC**, num papel de PS/2. O driver PS/2 da FabGL roda no
  coprocessador ULP e chega aos pinos por `rtc_gpio_desc[]`, então só funciona em
  GPIO 0, 2, 4, 12–15, 25–27 e 32–39. Num pino sem RTC não há erro: o teclado
  simplesmente fica mudo.

Em qualquer recusa o motivo vai para o serial e valem os padrões, então um `bootl.rc`
errado não deixa a placa inutilizável.

No boot o firmware imprime o que foi decidido, com a origem de cada valor:

    ===== MSX2 VGA32 (build Oct  3 2026 00:31:12) =====
    SD ok: MISO=GPIO2, 20000 kHz
    ---- pinos ----
    bootl.rc: /sd/bootl.rc lido
    teclado PS/2: DATA=GPIO27 CLK=GPIO26 (bootl.rc)
    mouse PS/2:   DATA=GPIO32 CLK=GPIO33 (padrao, cedeu a porta ao teclado)
    botao de menu: GPIO36 (bootl.rc), ativo em LOW, precisa de pull-up externo
    cartao SD: CS=GPIO13 MOSI=GPIO12 CLK=GPIO14 MISO=GPIO2
    ---------------

## Estrutura

```
src/main.cpp        setup(): SD, vídeo, teclado, áudio; emulador no núcleo 0
src/msx_glue.c      cola em C entre o fMSX e o hardware (vídeo, teclas, áudio, frame skip)
src/hal.h           fronteira C <-> hardware
src/hal_pins.cpp    pinos de PS/2 e botão de menu vindos do bootl.rc, com validação
src/hal_*.cpp       SD/bootl.rc, VGA (FabGL), PS/2 (FabGL), áudio (I2S/DAC)
test/test_pins/     testes da validação de pinos (pio test -e native)
src/fmsx/           núcleo fMSX (EMULib, fMSX, Z80) + msxfix.c/h (cwd no ESP32)
```

O núcleo tem as alterações do fork robgo (mixer com SCC e FM, soft-clip, Z80 na IRAM, menu enxuto,
navegação de pastas), marcadas com `RG_TARGET_ROBGO_RG`.

## Se algo falhar

- **Linker reclama de `iram0_0_seg`**: em `platformio.ini`, troque `-DMSX_Z80_IN_IRAM=1` por `0`.
- **Tela azul com mensagem**: SD ausente ou BIOS não encontrada. O monitor serial (115200) mostra os detalhes.

## Licença do fMSX

O fMSX não pode ser distribuído comercialmente (ver cabeçalhos dos arquivos em `src/fmsx`).
