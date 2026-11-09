# Emulador MSX2 ESP32 v1.03 — abertura MSX2+ (EXPERIMENTAL)

**Origem:** exclusivamente `MSX2-ESP32-EMULATOR-v1.02-CUMULATIVO.zip`, cuja base estável anterior é `MSX2-ESP32-EMULATOR-1_1.zip`. Nenhuma alteração da v38.10/v38.11 foi importada.

## Falha reproduzida por inspeção de código

Com `MSX2PKANJI.ROM`, a abertura MSX2+ começa a desenhar mas para antes de concluir. O BASIC aparece depois de alguns segundos. No dump do usuário (`MSX2PKANJI.ROM`, 32.768 bytes), a rotina em `0x7A66` contém `DB 99 DB 99 E6 20 CA 68 7A`: são **duas leituras consecutivas de S#0**, seguidas do teste do bit 5 (colisão de sprites), repetidas até ocorrer colisão.

No fMSX da v1.02, `CheckSprites()` calcula a colisão principalmente uma vez por quadro, na linha 192, não na linha de ocorrência. Esse atraso e a leitura que limpa o latch S#0 podem impedir a temporização necessária para animar o logo. Há relato técnico independente do mesmo impasse no fMSX, relacionado ao momento de detecção da colisão.

## Ajuste de baixo alcance

- Novo auxiliar `src/fmsx/fMSX/MSXBootSpriteCollision.h`: calcula colisão horizontal de sprites mode-2 **na linha visível atual**, considerando 8x8, 16x16, zoom, early-clock, scroll Y, até 8 sprites/linha, transparência `R#8.TP`, `CC` e `IC`.
- `src/fmsx/fMSX/MSX.c`: atualiza o bit 5 de S#0 no processamento de cada linha de vídeo e, durante a abertura, em leituras de S#0. Isso permite que a segunda leitura de S#0 possa observar uma colisão ainda em curso, como ocorre com a passagem do feixe no hardware.
- O caminho novo só é habilitado se há ROM Kanji de abertura mapeada, modelo MSX2+, SCREEN 6, nenhum cartucho inserido, sprites não desativados e até 900 quadros desde o reset. **Fora da abertura, não executa o algoritmo adicional de colisão.** O caminho tradicional fMSX continua em uso.
- Diagnóstico na inicialização pelo serial e desligamento independente por `/sd/bootl.rc`:

```ini
msx_boot_sprite_scanline=0
```

Padrão **ON** (chave ausente). Com `0`, volta ao comportamento da v1.02 sem reflashear.

## ROMs do usuário necessárias (não incluídas no ZIP)

```text
/MSX2/ROMS/
  MSX2P.ROM       (32.768 bytes)
  MSX2PEXT.ROM    (16.384 bytes)
  MSX2PKANJI.ROM  (32.768 bytes — a mesma enviada pelo usuário)
```

`KANJI.ROM` (fontes) é um arquivo diferente. Não se alterou nenhum byte das ROMs do usuário. Observe que a animação também depende de consistência entre as versões das três BIOS.

## Plano de testes na TTGO VGA32

1. Use a mesma BIOS e `MSX2PKANJI.ROM` utilizadas na v1.02. Grave a v1.03 e **desligue e religue o ESP32** (reset F12 pode pular a animação).
2. Verifique se a abertura agora se movimenta até o final e se o BASIC aparece. Observe cores, som e transições.
3. Teste `Shift+F11` (AUTO/0%), `F11` (OSD), layout BR no menu F10 e teclas acentuadas.
4. Rode um jogo MSX1 e um MSX2+/SCREEN 6, comparando FPS/EPS com a base v1.02 (não deve haver mudança perceptível fora do boot).
5. Se a abertura persistir congelada, configure `msx_boot_sprite_scanline=0`, desligue/ligue e compare. Se possível, envie gravação da tela e registro serial a 115200 baud.

## Validação local

- `gcc -std=gnu11 -fsyntax-only`: `MSX.c`, `msx_glue.c`, `V9938.c` — aprovado.
- `tests/test_v1_03_boot_sprite.c`: eventos de colisão, primeiro pixel na linha Y+1, sticky read, zoom, early clock, transparente/TP, IC/CC, limite de 8 sprites — aprovado.
- `tests/test_v1_03_boot_rom.py` contra **a ROM que o usuário enviou**: código `0x7A66` validado.
- `tests/test_v1_02_startup.py`: 9 combinações Auto Frame Skip, 5 casos F4, menu e sintaxe preservados — aprovado.
- **Limite:** ferramenta PlatformIO/Xtensa e acesso à TTGO VGA32 indisponíveis neste ambiente. A compilação final, o boot animado e FPS/EPS só poderão ser verificados no hardware.

## Política de versões

A v1.03 é **experimental até o teste físico**, não substituindo a referência estável que o usuário confirmou (`MSX2-ESP32-EMULATOR-1_1.zip`). Mantém as alterações v1.02 solicitadas: `LAYOUT KEYBOARD BR`, BR ON inicial, Auto Frame Skip AUTO inicial, F11 OSD, Shift+F11 Frame Skip.
