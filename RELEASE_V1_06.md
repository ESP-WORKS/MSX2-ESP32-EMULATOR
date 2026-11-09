# MSX2-ESP32-EMULATOR v1.06

Base: v1.05 (100% funcional segundo o usuario). Tres ajustes pontuais.

## 1. Abertura MSX2+ "sendo construida"

Causa: a animacao do logotipo e feita por rolagem horizontal do V9958
(R#25 SP2/MSK, R#26, R#27) em duas paginas de VRAM, com troca de rolagem
por faixa de tela sincronizada por colisao de sprites. O logo ja esta
pronto na VRAM desde o inicio; o que "constroi" a imagem e a rolagem.
A v1.05 desligava a rolagem (glitches da v1.04), por isso aparecia o logo
completo de uma vez.

Correcoes:
- Rolagem SCREEN 6 reescrita com a semantica do openMSX: com SP2 a pagina
  par e a de R#2 com A15 zerado e a impar e a de R#2 (a v1.04 somava 32 KiB
  e, com R#2=3Fh, lia a pagina 2); sem SP2 so os 5 bits baixos de R#26
  valem; R#27 desloca a imagem e deixa a cor de fundo a esquerda (a v1.04
  repetia pixels da direita). Ligada por padrao.
- Cor de sprites em SCREEN 6: cada ponto de sprite usa a paleta (cor>>2)
  no ponto esquerdo e (cor&3) no direito. O fMSX usava a paleta 0..15
  inteira, o que transformava os sprites-guia da abertura em barras brancas.
- Sincronismo por colisao e limpeza dos sprites-guia tambem funcionam com
  cartucho inserido.

## 2. F12 mostra a abertura novamente

F12 agora restaura a porta F4h ao valor de power-on antes do reset, e a
BIOS executa a abertura completa. Para voltar ao comportamento anterior
(BASIC direto): `msx_f12_logo=0` no `/sd/bootl.rc`.

## 3. Oscilacao 56-60 FPS em jogos pesados (Metal Gear 2)

Causas identificadas no codigo:
- O limitador de EPS usava 1000000/60 us por quadro, mas cada quadro
  emulado gera 16,688 ms de audio. O Z80 produzia 0,13% mais audio do que
  o I2S consome; a fila de 80 ms enchia e o audio passava a ditar o ritmo.
- Nessa condicao a espera acontecia dentro de WriteAudio() e nao era
  contada como folga pelo Frame Skip AUTO. Depois de um trecho pesado o
  AUTO subia o descarte e nao conseguia mais descer (exige 8% de folga):
  FPS preso abaixo de 60 mesmo com CPU sobrando.
- Um unico quadro acima de 33 ms reancorava o limitador e o tempo era
  perdido definitivamente.

Correcoes:
- Periodo do limitador = periodo real do quadro MSX (NTSC 59,92 Hz,
  PAL 50,16 Hz), em nanossegundos. O OSD continua mostrando 60/50.
- Espera na fila de audio contada como folga do AUTO.
- Divida de ate 4 quadros: picos isolados sao recuperados nos quadros
  seguintes, sem perder EPS; atrasos longos (menu, carga) reancoram.

Limite: se um trecho exigir continuamente mais CPU do que o ESP32 tem,
o AUTO ainda descarta quadros nesse trecho. Para diagnostico, use
`msx_profile=1` no `bootl.rc` e envie o log serial (115200).

## Novas chaves do bootl.rc

| Chave | Padrao | Efeito |
|---|---|---|
| `msx_f12_logo` | 1 | 0 = F12 vai direto ao BASIC |
| `msx_v9958_screen6_scroll` | 1 | 0 = desliga a rolagem SCREEN 6 (A/B) |

## Testes (PC)

- `gcc -O2 tests/test_v1_06_screen6_scroll.c` : 1.048.576 casos contra
  referencia independente no modelo openMSX - PASS
- `python3 tests/test_v1_06_pacing.py` : periodo 59,92 Hz, pico recuperado,
  reancoragem - PASS
- `python3 tests/test_v1_02_startup.py`, testes v1.03 e v1.05 - PASS
- Sintaxe C de MSX.c, msx_glue.c, Menu.c, V9938.c (todas as profundidades
  de Common.h) - PASS

Compilacao Xtensa/PlatformIO e teste fisico nao foram possiveis neste
ambiente: validar na TTGO VGA32 / RobGo (power-on, F12, Metal Gear 2 com F11).
