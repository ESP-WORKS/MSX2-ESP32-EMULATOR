# MSX2-ESP32-EMULATOR v1.07

Base: v1.06. Retorno do teste fisico: metade superior do logo animada
corretamente, metade inferior estatica; jogos pesados ainda oscilando com
Frame Skip AUTO desligado (Shift+F11 para 0%).

## 1. Metade inferior do logo MSX2+

Causa: a ROM de abertura le S#0 uma vez (limpa o bit de colisao) e depois
espera em laco a PROXIMA colisao para trocar R#26/R#27 no meio da tela.
No emulador a colisao da linha corrente era religada logo apos a leitura
que a limpava, e a linha 192 ainda religava o bit com a verificacao do
quadro inteiro. A segunda espera saia cedo demais e a troca de rolagem da
metade inferior caia no lugar errado (imagem estatica).

Correcao (somente durante a abertura):
- Colisao vira um evento por linha, disparado quando o feixe alcanca o
  primeiro pixel sobreposto (posicao calculada pelos ciclos do Z80 na
  fase HRefresh) ou no HBlank. Cada linha dispara no maximo uma vez.
- A verificacao de quadro inteiro da linha 192 fica suspensa na abertura.

## 2. Desempenho (meta: 60 EPS sem frame skip)

Causa principal: o renderizador do nucleo 0 gravava 58 KiB por quadro
direto no framebuffer da PSRAM. Isso atravessa o cache de memoria externa
do nucleo 0, expulsa bancos MegaROM, RAM e VRAM do Z80 e paga leitura e
escrita de cada linha de cache no barramento QSPI.

Correcoes:
- **Anel de scanlines (`hal_linering.cpp`):** cada linha e desenhada em
  RAM interna (32 slots); uma tarefa no nucleo 1 copia para a PSRAM.
  PutImage() espera o anel esvaziar, a imagem e identica. `msx_linering=0`
  volta ao caminho antigo.
- **RAM quente:** segmentos 0 e 1 do mapper (C000h e 8000h na configuracao
  da BIOS: pilha e variaveis) na RAM interna, quando sobra memoria sem
  reduzir o cache MegaROM abaixo de 64 KiB. `msx_hotram=0` desliga.
  Save states mantem o mesmo formato.
- Mantidos da v1.06: limitador em 59,92 Hz exatos e divida de 4 quadros.

## Novas chaves do bootl.rc

| Chave | Padrao | Efeito |
|---|---|---|
| `msx_linering` | 1 | 0 = renderiza direto na PSRAM (v1.06) |
| `msx_hotram` | 1 | 0 = toda a RAM do MSX na PSRAM (v1.06) |

## Testes (PC)

- `tests/test_v1_07_collision_x.c`: X da primeira sobreposicao - PASS
- `tests/test_v1_07_linering.cpp`: 63.600 linhas em 300 quadros, produtor
  e consumidor em threads separadas, imagem identica - PASS
- Testes v1.02, v1.03, v1.05, v1.06 - PASS; sintaxe C dos arquivos alterados - PASS

Compilacao Xtensa e teste fisico pendentes. No primeiro boot, conferir no
serial (115200): `Line ring: 32 linhas`, `RAM MSX: segmento ... RAM interna`
e `MegaROM LRU: N bancos`. Se o desempenho ainda oscilar, enviar o log com
`msx_profile=1`.
