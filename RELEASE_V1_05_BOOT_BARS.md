# v1.05 EXPERIMENTAL — residuos de barras na abertura MSX2+

## Base
v1.04 experimental, tendo como referencia estavel MSX2-ESP32-EMULATOR-1_1.zip.
A correcao de colisao da v1.03 permanece e o texto F10 da v1.04 e preservado.

## Analise da ROM e da foto
A ROM MSX2PKANJI.ROM do usuario tem 32768 bytes e a rotina de abertura em
7A00h. Em 7A68h a ROM espera colisao S#0, e o array de atributos em 7AB2h
coloca sprites de sincronismo em ambas as extremidades do campo de video.
A rotina de animacao habilita rolagem em duas paginas R#25=03h e depois
faz R#25=00h, R#26=00h, R#27=00h e R#2=1Fh na transicao final.
As barras observadas sao compatíveis com residuos visuais dos sprites-guia.

## Correcao restrita ao boot
- Observa a ordem de escritas no VDP, sem analisar bytes ou o nome da ROM.
- So atua enquanto a ROM de Kanji BASIC esta montada, MSX2+, SCREEN6,
  sem cartuchos e na janela inicial de 900 quadros.
- Depois dos marcadores da propria ROM, compoe apenas o fundo do VDP
  (nao desenha os sprites-guia), **sem remover os sprites da VRAM** e **sem
  adulterar o status de colisao**. Sem custo permanente para jogos.
- A alteracao eh uma correcao visual especifica para esta sequencia de boot,
  nao uma revisao geral de compatibilidade do hardware V9958. Seu resultado
  requer teste fisico.

## Glitches v1.04
A rolagem experimental SCREEN6 da v1.04 foi desativada por padrao.
O renderer utilizado volta ao caminho da v1.03, que o usuario informou nao
produzir os glitches adicionais. Pode ser reativada para comparacao.

## Configuracao para A/B (/sd/bootl.rc)
```ini
# Padrao na v1.05:
# msx_boot_guides_cleanup=1
# msx_v9958_screen6_scroll=0

# Para desativar apenas a limpeza das barras:
# msx_boot_guides_cleanup=0

# Para religar apenas a rolagem experimental v1.04:
# msx_v9958_screen6_scroll=1
```

## Teste fisico
1. Usar as MESMAS ROMs sem modificacao: MSX2P.ROM, MSX2PEXT.ROM,
   MSX2PKANJI.ROM, em `/MSX2/ROMS/`.
2. Ligar o equipamento a frio em MSX2+; comparar barras ao final do logo.
3. Verificar se os glitches da v1.04 desapareceram.
4. Verificar transicao natural para BASIC, F10, F11, Shift+F11, teclado BR,
   desempenho e alguns jogos.
5. Se as barras persistirem, testar `msx_boot_guides_cleanup=0` para aferir
   se o residuo esta nos sprites ou na VRAM de fundo. Opcionalmente enviar foto
   do instante de transicao, alem do monitor serial de boot.

## Limites
Teste no PC nao substitui compilacao ESP32 nem validacao visual na TTGO VGA32.
Se o residuo estiver na VRAM de fundo em vez dos sprites, a limpeza visual
nao sera suficiente: sera preciso investigar o caminho de comandos do VDP.
