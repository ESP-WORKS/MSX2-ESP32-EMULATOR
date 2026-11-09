# MSX2-ESP32-EMULATOR v1.04 — Experimental: abertura V9958 / F10

## Problema descrito no teste real (v1.03)

O usuário confirmou que o logotipo MSX2+ já anima quase por completo e que o BASIC inicia, mas **duas barras verticais muito claras**, inicialmente presentes na abertura, continuam aparentes no fundo azul após a transição. A foto `Boot Screen.jpg` foi usada como referência visual, mas não faz parte do firmware.

## Diagnóstico e correção

A arquitetura já processa a colisão de sprites por linha na v1.03, permitindo completar a abertura. No entanto, o renderizador `RefreshLine6()` (SCREEN6) usa sempre a origem da VRAM sem aplicar os registradores de rolagem horizontal **R#25 (SP2, MSK), R#26 (deslocamento grosso) e R#27 (deslocamento fino)**. Esses registradores fazem parte do V9958 e são empregados na animação do logotipo original de MSX2+.

A v1.04 adiciona esse caminho ao renderizador SCREEN6 em 256 pixels VGA:

- R#26 desloca 8 pixels *do framebuffer reduzido* para a esquerda por unidade (16 pontos físicos SCREEN6).
- R#27 desloca um pixel *do framebuffer reduzido* para a direita por unidade (dois pontos físicos SCREEN6).
- SP2=0: rolagem circular de 256 amostras; SP2=1: campo de 512 amostras que atravessa duas páginas de 32KiB.
- MSK=1: usa a cor de borda nos 8 pixels da esquerda do framebuffer reduzido.
- Respeita o estado de sprites da v1.03 ao compor cada linha.
- **Sem rolagem e sem máscara:** a rotina original é usada sem mudanças nem custo adicional.
- **MSX1/MSX2:** comportamento original preservado.

Este ajuste corrige uma omissão real do renderizador. Não é um filtro de pixels fixo nem modifica ROMs. O resultado visual exato, incluindo o desaparecimento das barras, ainda precisa ser confirmado no hardware.

Para comparar com o renderizador anterior, adicione a `/sd/bootl.rc`:

```ini
msx_v9958_screen6_scroll=0
```

O padrão é 1 (ativado). `msx_boot_sprite_scanline` da v1.03 continua disponível e ativado.

## Menu F10

A entrada foi alterada para **`Layout Keyboard BR`**, exatamente com maiúsculas/minúsculas solicitadas. A funcionalidade ON/OFF do teclado permanece idêntica e BR continua ON por padrão.

## Preservação

A base é **v1.03** (derivada da v1.02 e da oficial MSX2-ESP32-EMULATOR-1_1.zip). Continuam válidos: F11=OSD, Shift+F11=Auto Frame Skip, Frame Skip AUTO padrão, teclado BR, colisão por scanline v1.03, áudio, mappers, vídeo VGA e ROMs do usuário. Nenhuma ROM proprietária está incluída no ZIP.

## Teste de validação recomendado

1. Com as mesmas três ROMs `MSX2P.ROM`, `MSX2PEXT.ROM` e `MSX2PKANJI.ROM`, faça um **power cycle completo**.
2. Observe a entrada das barras, a formação do logotipo, o momento em que as barras deveriam desaparecer e a transição ao BASIC.
3. Teste `F10` e confirme `Layout Keyboard BR`, BR ON; teste `F11` e `Shift+F11`.
4. Compare FPS/EPS em um jogo MSX2+ e num jogo MSX1 com a v1.03.
5. Se as barras permanecerem, teste `msx_v9958_screen6_scroll=0` (com reinicialização completa) para isolar o efeito dos registradores de rolagem; envie imagens das duas configurações e, se possível, logs da inicialização a 115200 baud.

## Verificações realizadas (host)

- 1.048.576 comparações do mapeamento de SCREEN6 para R#26/R#27, SP2 e páginas de VRAM: PASS.
- Equivalência de extração de pixels com o caminho anterior sem rolagem: PASS.
- Teste de colisão de sprites v1.03 (8x8, 16x16, zoom, EC, transparência, duas leituras): PASS.
- Teste sobre a ROM `MSX2PKANJI.ROM` do usuário, assinatura da rotina de sincronização: PASS.
- Testes anteriores de configuração do Frame Skip (9 cenários) e F4h (5 cenários): PASS.
- Sintaxe C (MSX.c, msx_glue.c, Menu.c, V9938.c): PASS.

**Limites:** Não há PlatformIO/compilador Xtensa ou placa TTGO VGA32 disponíveis no ambiente. A compilação final e a eficácia visual da correção exigem teste do usuário. A versão permanece EXPERIMENTAL.
