# MSX2-ESP32-EMULATOR v1.08

Base: v1.07. Retorno do teste fisico: abertura MSX2+ e F12 perfeitos; pequena
regressao de desempenho no Metal Gear 2 (meta: 60 EPS com Frame Skip AUTO desligado).

## Como esta versao foi verificada

Foi montado um ambiente de compilacao completo (Xtensa GCC 8.4.0, Arduino-ESP32
2.0.11, FabGL 1.0.9, PlatformIO) e o projeto COMPILA e LINKA:
RAM 36,9% (120.912 bytes), Flash 24,0% (753.941 bytes), IRAM livre 1.486 bytes.
Isso verifica compilacao, link e tamanho. FPS so pode ser medido na placa.

## Causas provaveis da regressao (hipoteses, a confirmar na placa)

1. **RAM interna escassa.** O FabGL aloca o framebuffer VGA na RAM interna
   (76,8 KB, ou 153,6 KB com double buffer). Com os 121 KB estaticos sobra
   pouca RAM interna. O anel de linhas (9 KB) e a RAM quente (32 KB) da v1.07
   disputavam esse resto com o cache MegaROM e as pilhas. Agora ficam
   DESLIGADOS por padrao e so se ativam se houver folga.
2. **Prazo de 59,92 Hz (v1.06).** O OSD conta quadros por segundo de relogio e
   alternava 59/60 mesmo com folga de CPU. O limitador volta a 60,000 Hz exatos
   (50,000 Hz em PAL).

## Mudancas

- **Limitador exato** de 60,000 / 50,000 Hz (16.666.667 / 20.000.000 ns), com a
  divida de ate 4 quadros da v1.06 (picos isolados sao recuperados).
- **Servo de taxa do audio.** O relogio do MSX (59,92 Hz) e o limitador (60 Hz)
  diferem 0,13%. Cada quadro agora gera 400 amostras (24 kHz / 60) e a fila de
  audio e mantida perto de 70% por ajuste fino (maximo +-0,5%; +1,5% so na
  partida). O audio deixa de poder limitar o emulador abaixo de 60 EPS e a
  latencia cai de ~106 ms para ~75 ms.
- **Caminho quente na IRAM** (antes so o `RunZ80` estava la; o resto executava da
  flash, cujo cache e compartilhado com a PSRAM): `LoopZ80`, `OutZ80`, `InZ80`,
  `WrZ80`, `MapROM`/`MapROMCore`/`RomCacheSync` (troca de bancos do MegaROM),
  `ColorSprites`, `RefreshBorder` e `RefreshLine5` (SCREEN 5), so na instancia
  de 8 bits. Usa a IRAM livre; sobram 1.486 bytes de margem.
  `-DMSX_HOT_IRAM=0` no platformio.ini desliga, se outra versao de biblioteca
  estourar a IRAM (erro `iram0_0_seg overflowed`).
- Log serial `HEAP antes do fMSX: interna livre=... maior bloco=... PSRAM livre=...`.
- `msx_linering` e `msx_hotram` agora valem 0 por padrao (1 liga).

## Como testar

1. Frame Skip em 0% (Shift+F11), F11 ligado, Metal Gear 2.
2. Comparar a oscilacao com a v1.07. Se ainda oscilar, enviar o log serial
   (115200) com `msx_profile=1`, incluindo a linha `HEAP` e as linhas
   `MegaROM LRU`.

## Testes (PC)

- `tests/test_v1_08_pacing.py` - limitador a 60,000 Hz exato - PASS
- `tests/test_v1_08_audio_servo.py` - 20 min simulados, erro do DAC de -0,4% a
  +0,4%, NTSC e PAL: fila nunca cheia nem vazia - PASS
- testes v1.02, v1.03, v1.05, v1.06 (rolagem, 1.048.576 casos), v1.07 (colisao
  por posicao, anel de linhas com threads) - PASS
