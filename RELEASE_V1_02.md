# MSX2-ESP32-EMULATOR v1.02 — CUMULATIVO

## Base
`MSX2-ESP32-EMULATOR-1_1.zip`, versão anterior considerada 100% estável
pelo usuário. Não parte das v38.10/v38.11 descartadas.

## Correções solicitadas
1. Menu F10: opção renomeada exatamente para `LAYOUT KEYBOARD BR`,
   inicializada como ON. O item F10 continua alternando ON/OFF.
2. Auto Frame Skip: inicializado em AUTO. Shift+F11 mantém a alternância
   AUTO/0%; F11 mantém o OSD; F12 mantém o reset. O modo de início pode
   ser alterado via `/sd/bootl.rc`: `msx_autoskip=0` para OFF ou
   `msx_autoskip=1` para ON. A opção antiga `msx_frameskip=0` ainda desliga;
   `msx_autoskip`, quando informada, tem prioridade.
3. Tela de abertura real MSX2+: as BIOS `MSX2P.ROM` (32768 bytes) e
   `MSX2PEXT.ROM` (16384 bytes) bastam para chegar ao BASIC, mas o
   logotipo animado é executado pela ROM Kanji BASIC/Opening separada.
   Colocar `MSX2PKANJI.ROM` (32768 bytes) em `/MSX2/ROMS/` junto às BIOS.
   **Não confundir com `KANJI.ROM` (fonte Kanji, 131072 bytes).**
   Não incluímos ROMs proprietárias neste pacote.
4. Diagnóstico do MSX2+ no monitor serial a 115200 baud informa ROM
   ausente, tamanho incorreto, falha de carregamento ou arquivo aceito.
   A ausência da ROM opcional não impede o boot normal no BASIC.
5. Correção da seleção automática do registrador F4h: não presume a
   polaridade invertida apenas porque Kanji BASIC está presente.
   Procura assinaturas de Panasonic/National/Sanyo nas BIOS; caso contrário
   adota normal. Para BIOS com identificação diferente, `bootl.rc` permite
   `msx_f4_inverted=1` (Panasonic/Sanyo) ou `0` (normal).

## Passo a passo de verificação
1. Compile e grave pelo PlatformIO (ambiente `msx2`, Arduino-ESP32 2.x,
   FabGL 1.0.9) na TTGO VGA32/RobGo.
2. Inicie sem configurações `msx_autoskip=0` ou `msx_frameskip=0`
   no `/sd/bootl.rc`. Confira Shift+F11 e o indicador SKIP no OSD (F11).
3. Abra F10 e veja `LAYOUT KEYBOARD BR` marcado; digite acentos e
   teclas BR e confirme que a opção alterna OFF/ON.
4. No SD coloque as BIOS em `/MSX2/ROMS/`. Sem `MSX2PKANJI.ROM`,
   a BIOS deve continuar iniciando no BASIC, e o serial deve informar
   que falta a ROM de abertura.
5. Acrescente uma ROM de abertura de 32768 bytes compatível com as BIOS,
   nomeada `MSX2PKANJI.ROM`; desligue e religue fisicamente o ESP32
   (não use apenas F12). Confira o log serial de ROM carregada e se
   aparece a animação original antes do BASIC.
6. Se houver ROM carregada mas abertura pulada, teste primeiro a
   polaridade de F4h apropriada à BIOS em `bootl.rc`, sem substituir
   imagens de firmware por versões incompatíveis.
7. Teste F12, menus, cartuchos, disquetes, vídeo, som, MegaROM e FPS/EPS
   para detectar regressões na sua placa.

## Validação de desenvolvimento
- `python tests/test_v1_02_startup.py`: nove casos de Auto Frame Skip,
  cinco casos de polaridade F4h, verificações de fonte, sintaxe C via GCC.
- Z80, áudio, vídeo e hal_input.cpp não sofreram alterações de código.
- **A compilação completa com PlatformIO e os testes físicos com BIOS reais
  não foram realizados neste ambiente.** A exibição do logotipo depende do
  conjunto compatível de ROMs e da validação na placa real.
