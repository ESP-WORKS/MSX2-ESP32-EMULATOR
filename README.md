# MSX2 for VGA32 (RobGo-RG)

MSX2 emulator (fMSX 6.0 by Marat Fayzullin) running directly on the ESP32-WROVER in the TTGO VGA32 / RobGo-RG board.

No launcher, no Retro-Go, no external ESP-IDF: just a regular PlatformIO project (Arduino + FabGL).

## Build

Open the folder in VS Code (with the PlatformIO extension) and use **Build** and **Upload**. The `platformio.ini` file already contains everything needed.

The `bootl.rc` pin validation includes a test that runs on the PC without requiring the board:

    pio test -e native

## SD Card

FAT32 format. Place the BIOS files in the **`/MSX2/ROMS/`** directory at the root of the card (default for this build).

| File | Purpose |
|---|---|
| `MSX2P.ROM` (32 KB) | Required for the default MSX2+ model |
| `MSX2PEXT.ROM` (16 KB) | Required for the default MSX2+ model |
| `MSX2PKANJI.ROM` (32 KB) | Required for the **authentic animated MSX2+ boot screen**, optional to reach BASIC |
| `KANJI.ROM` (128 KB) | Optional Kanji font only; **does not replace** `MSX2PKANJI.ROM` |
| `DISK.ROM` | Optional for floppy disk support |
| `MSXDOS2.ROM` | Optional (MSX-DOS 2) |

The BIOS files are not included in the project. Filenames must match exactly.

ROMs, floppy disk images (`.dsk`), and save states (`.sta`) are selected through the fMSX menu (F10).

## Keyboard Controls (PS/2 Keyboard)

| Key | Action |
|---|---|
| F10 | fMSX menu: open file, save state, select model, manage disks, reset, reboot |
| `magicb` button | Same as F10, if configured in `bootl.rc` |
| F12 | Reset the MSX (MSX2+: replays the opening animation) |
| F11 | Toggle FPS/EPS/SKIP on-screen overlay |
| Shift+F11 | Toggle Auto Frame Skip (AUTO / 0%); starts enabled |
| F1–F5 | MSX F1–F5 keys |
| Arrow keys + Space/Enter + Ctrl/Esc | Also work as joystick 1 |
| PS/2 mouse | MSX mouse |

## Optional `/sd/bootl.rc`

UTF-8 text file with one `key=value` entry per line. `#` and `;` are used for comments.

The file is read immediately after the SD card is mounted. A commented example is provided in `bootl.rc.example`.

| Key | Effect |
|---|---|
| `kbddat=32`, `kbdclk=33` | PS/2 keyboard DATA and CLK GPIOs |
| `mousedat=27`, `mouseclk=26` | PS/2 mouse DATA and CLK GPIOs |
| `magicb=36` | GPIO for a button that opens the fMSX menu (same as F10) |
| `msx_biosdir=/MSX2/ROMS` | BIOS directory (default `/MSX2/ROMS/`) |
| `msx_rom=/msx/game.rom` | Cartridge ROM loaded at startup |
| `msx_diska=/msx/disk.dsk`, `msx_diskb=...` | Floppy disks loaded at startup |
| `msx_frameskip=0` | Legacy control: explicitly disable Auto Frame Skip; AUTO is the default |
| `msx_autoskip=0` | Disable Auto Frame Skip at boot (1 = enable); overrides `msx_frameskip` |
| `msx_fm=1` | Enable/disable FM synthesis (YM2413) |
| `msx_fps=1` | Display the FPS counter on the serial monitor |
| `msx_f4_inverted=1` | MSX2+ port F4h: force Panasonic (inverted) logic; `0` forces normal logic. Default: auto-detect |
| `msx_f12_logo=0` | F12 goes straight to BASIC (v1.06 default 1: F12 replays the MSX2+ opening) |
| `msx_v9958_screen6_scroll=0` | Disable V9958 SCREEN 6 horizontal scroll (v1.06 default 1) |
| `msx_linering=1` | Experimental internal-RAM line ring copied by core 1 (default 0 since v1.08) |
| `msx_hotram=1` | Experimental: mapper segments 0/1 in internal RAM when memory allows (default 0 since v1.08) |

## GPIO Pins

Default pin assignments are defined in `src/board.h`: VGA on GPIOs 22/21/19/18/5/4, with HSync on GPIO23 and VSync on GPIO15; PS/2 keyboard on GPIOs 32/33; PS/2 mouse on GPIOs 27/26; SD card with CS on GPIO13, MOSI on GPIO12, and CLK on GPIO14; audio through the internal DAC on GPIO25 (mono).

The SD card MISO pin is tested first on GPIO35 and then on GPIO2, at 20, 10, and 4 MHz. The first configuration that successfully mounts the card is selected, and the serial monitor reports which one was chosen. This behavior is fixed in the firmware.

Unlike the SD configuration, the keyboard, mouse, and menu button pins are configured through `bootl.rc` and do not require recompilation. This is how support for other boards is implemented.

PS/2 is initialized before VGA because the keyboard reset handshake cannot complete once the video interrupt is running.

On the serial monitor, `teclado PS/2: respondeu ao reset` (“PS/2 keyboard: responded to reset”) confirms that the keyboard communicated successfully. `NAO respondeu` (“NO response”) indicates a wiring issue, an incorrect pin assignment, or a disconnected keyboard.

The firmware rejects any GPIO that:

- Does not physically exist on this ESP32 (pads 20, 24, 28–31, 37, and 38 are not exposed on the WROVER).
- Is already assigned to VGA, the SD card (including the MISO pin selected by the probe), audio, UART0, flash, or PSRAM.
- Is duplicated between the keyboard and mouse.
- Is input-only (GPIOs 34–39) when used for PS/2, which requires driving the pin.
- **Does not support RTC functions** when used for PS/2. The FabGL PS/2 driver runs on the ULP coprocessor and accesses pins through `rtc_gpio_desc[]`, so it only works on GPIOs 0, 2, 4, 12–15, 25–27, and 32–39. Using a pin without RTC support does not produce an error; the keyboard simply remains unresponsive.

Whenever a configuration is rejected, the reason is printed to the serial monitor and the default settings are used. This ensures that an invalid `bootl.rc` file will not render the board unusable.

At startup, the firmware prints the selected configuration and the source of each setting:

    ===== MSX2 VGA32 (build Oct  3 2026 00:31:12) =====
    SD ok: MISO=GPIO2, 20000 kHz
    ---- pins ----
    bootl.rc: /sd/bootl.rc loaded
    PS/2 keyboard: DATA=GPIO27 CLK=GPIO26 (bootl.rc)
    PS/2 mouse:   DATA=GPIO32 CLK=GPIO33 (default, reassigned to the keyboard)
    menu button: GPIO36 (bootl.rc), active LOW, requires an external pull-up
    SD card: CS=GPIO13 MOSI=GPIO12 CLK=GPIO14 MISO=GPIO2
    ---------------

## Project Structure

```text
src/main.cpp        setup(): SD, video, keyboard, audio; emulator runs on core 0
src/msx_glue.c      C glue layer between fMSX and the hardware (video, keyboard, audio, frame skipping)
src/hal.h           C <-> hardware interface
src/hal_pins.cpp    PS/2 and menu button pins from bootl.rc, with validation
src/hal_*.cpp       SD/bootl.rc, VGA (FabGL), PS/2 (FabGL), audio (I2S/DAC)
test/test_pins/     Pin validation tests (pio test -e native)
src/fmsx/           fMSX core (EMULib, fMSX, Z80) + msxfix.c/h (ESP32 working directory support)
```

The core includes modifications from the RobGo fork (SCC and FM mixer, soft clipping, Z80 code in IRAM, streamlined menu, and directory navigation), marked with `RG_TARGET_ROBGO_RG`.

## Troubleshooting

- **Linker error mentioning `iram0_0_seg`:** In `platformio.ini`, change `-DMSX_Z80_IN_IRAM=1` to `0`.
- **Blue screen with an error message:** The SD card is missing or the BIOS could not be found. Check the serial monitor at 115200 baud for details.

## fMSX License

fMSX cannot be distributed commercially (see the file headers in `src/fmsx`).


## Thanks to
People from Whatsapp ROBGO_RG Group - André Chaves, Evandro, fg1998, Cacá Couto, Roberto Guerra, Miguel Roberto


## v39.0 changes

- **MSX2+ reset-status F4h implemented.** The authentic opening animation also requires a compatible **MSX2PKANJI.ROM (32768 bytes)** placed alongside the BIOS. Port F4h (MSX2+ reset-status register) is always emulated in MSX2+ mode. Before, it only existed when `MSX2PKANJI.ROM` was present; otherwise it read as FFh, which normal-logic BIOSes interpret as a warm reset and skip the opening screen. Logic (normal or Panasonic inverted) is auto-detected and can be forced with `msx_f4_inverted` in `bootl.rc`. As on real hardware, the logo shows at power-on and is skipped on F12 reset.
- **Jump tables re-enabled in the fMSX core.** `scripts/fmsx_fast_flags.py` removes `-fno-jump-tables`, `-fno-tree-switch-conversion` and `-fstack-protector` only for files under `src/fmsx/`. The Z80 opcode switches and I/O port switches no longer compile into comparison trees. FabGL, HAL and ISRs keep the framework flags. IRAM usage drops by about 4.5 KB.
- **MegaROM cache without copies during bank switching.** A bank miss now reads directly from PSRAM instead of copying 8 KiB in the middle of Z80 execution. Banks that stay mapped between frames are promoted to internal RAM once per frame (at most one 8 KiB copy per frame). Targets large MegaROMs such as Metal Gear 2 and games that switch banks inside the interrupt handler.


## v1.02 (derived only from MSX2-ESP32-EMULATOR-1_1.zip)

- F10 menu: **LAYOUT KEYBOARD BR**; ABNT2 starts **ON**, remains toggleable via F10.
- **Auto Frame Skip starts ON** and Shift+F11 continues to toggle 0%/AUTO.
  In `/sd/bootl.rc`, set `msx_autoskip=0` to start OFF. Legacy
  `msx_frameskip=0` still works; the explicit `msx_autoskip` setting wins.
- **Authentic MSX2+ boot animation:** BIOS `MSX2P.ROM` (32768 bytes) and
  `MSX2PEXT.ROM` (16384 bytes) boot to BASIC but are **not sufficient**
  for the animated screen. Supply the appropriate **`MSX2PKANJI.ROM`
  (32768 bytes)** in `/MSX2/ROMS/` on the SD card; `KANJI.ROM` (font) is
  a separate file. Source ROMs are **not included**. This optional file
  is detected at MSX2+ cold boot and mapped to slot 3-1. Absent/invalid
  ROMs are logged on the serial monitor (115200 baud); the original BASIC
  boot is kept and no fabricated animation is displayed.
- MSX2+ `F4h` auto-polarity no longer assumes Panasonic just because a Kanji
  BASIC ROM is present; it checks manufacturer strings for Panasonic/National
  or Sanyo. BIOS images without such a marker default to normal polarity.
  If needed force `msx_f4_inverted=1` for Panasonic/Sanyo or `0` for normal.
- F12/reset remains a warm boot relative to F4h (no repeated logo);
  ESP32 power cycle performs a fresh cold start.
- MSX1/MSX2 and existing VGA, audio, MegaROM and keyboard hotkeys untouched.

**Note:** The native ESP32/PlatformIO build and physical boot-animation test
need to be completed on TTGO VGA32/RobGo hardware.

## v1.03 EXPERIMENTAL — Boot Screen MSX2+

Foi acrescentado o tratamento de colisão de sprites por scanline e por leitura do
status do V9958 **apenas durante a abertura original em SCREEN 6**. O objetivo é
corrigir o congelamento da animação de `MSX2PKANJI.ROM` que, na v1.02,
inicia parcialmente e depois passa ao BASIC. Requer BIOS compatíveis, inclusive
`MSX2PKANJI.ROM` (32 KB) em `/MSX2/ROMS/`; os arquivos de BIOS não acompanham
este projeto. Para restaurar o comportamento da v1.02, defina
`msx_boot_sprite_scanline=0` no `/sd/bootl.rc` e reinicie o ESP32.

Consulte `RELEASE_V1_03_BOOT_SPRITES.md`. A versão está pendente de teste físico.

## v1.04 EXPERIMENTAL — V9958 SCREEN6 horizontal scroll / menu F10

- Menu F10: `Layout Keyboard BR` (caixa exatamente como solicitada).
- Corrigida a leitura dos registradores V9958 `R#25` (SP2/mascara), `R#26`
  (rolagem de 8 amostras por unidade) e `R#27` (rolagem fina) no
  renderizador SCREEN6 de 256 pixels de largura VGA. A v1.03 ignorava
  essa rolagem, utilizada pelo logotipo original do MSX2+.
- Preservado o caminho de desenho original sem rolagem, inclusive no MSX1 e
  MSX2. A cor da mascara esquerda respeita o fundo selecionado no VDP.
- Fallback diagnostico no `/sd/bootl.rc`: `msx_v9958_screen6_scroll=0`.
- Colisao de sprites por scanline da v1.03, teclado BR, Frame Skip AUTO,
  F11 e Shift+F11 preservados. BIOS e ROMs nao incluidas.
- Provisorio: compilar e testar visualmente na TTGO VGA32 para validar
  a sequencia completa; a base anterior continua como referencia estavel.

## v1.05 EXPERIMENTAL — barras de abertura / regressao visual

- A tela de abertura MSX2+ usa sprites-guia junto as bordas para gerar colisoes
  e controlar a animacao; a ROM de 32KB fornecida pelo usuario indica os
  atributos dos sprites em 7AB2h e a transicao final de R#25 para zero e R#2
  para 1Fh. A v1.05 deixa de compor visualmente esses sprites apos a sequencia
  final, mas preserva VRAM, registradores e calculo de colisoes.
- A rolagem experimental SCREEN6 da v1.04 fica DESLIGADA por padrao, pois
  provocou glitches nos testes reais. O caminho de video da v1.03 e retomado.
- Comparacao A/B no `/sd/bootl.rc`: `msx_boot_guides_cleanup=0` deixa
  os sprites-guia visiveis; `msx_v9958_screen6_scroll=1` religa a rotina v1.04.
- F10: `Layout Keyboard BR`, sem modificar texto nem funcionamento.
- Teclado BR, AUTO frame skip, OSD F11, Shift+F11, audio e carts preservados.
- Nao inclui ROMs, BIOS, conteudo licenciado ou dados grafios proprietarios.
- **A eficacia visual do ajuste e uma hipotese testavel**: compilacao completa
  PlatformIO e teste fisico requeridos antes de promover a versao a estavel.

## v1.06

- **MSX2+ opening animated again:** V9958 SCREEN 6 horizontal scroll rewritten
  (SP2 even/odd pages, R#26 5/6 bits, R#27 backdrop) and enabled by default.
  SCREEN 6 sprite colours now use palette (colour>>2), as on the V9938/V9958,
  which removes the white guide bars.
- **F12 replays the opening** (cold reset of port F4h). `msx_f12_logo=0` restores
  the warm reset.
- **Steadier 60 FPS in heavy games:** EPS limiter uses the real MSX frame period
  (59.92/50.16 Hz), audio-queue waiting counts as idle time for Auto Frame Skip,
  and isolated slow frames are recovered (up to 4 frames of debt).
- Details: `RELEASE_V1_06.md`.

## v1.07

- **MSX2+ opening, lower half:** sprite collision is now an event raised when the
  beam reaches the first overlapping pixel, once per line; the whole-frame check at
  line 192 is suspended during the opening. Fixes the static lower half.
- **Performance:** scanlines rendered into an internal-RAM ring and copied to the
  PSRAM framebuffer by core 1 (core 0 cache no longer flooded by 58 KiB per frame);
  mapper segments 0/1 in internal RAM when memory allows.
- Details: `RELEASE_V1_07.md`.

## v1.08

- **Exact 60.000 Hz limiter** (50.000 Hz PAL) and an **audio rate servo** keep the
  audio queue near 70% so audio can never throttle emulation below 60 EPS.
- **Hot path in IRAM:** `LoopZ80`, `OutZ80`, `InZ80`, `WrZ80`, MegaROM bank switching,
  `ColorSprites`, `RefreshBorder`, `RefreshLine5` (build verified, 1486 bytes IRAM spare;
  `-DMSX_HOT_IRAM=0` disables).
- Line ring and hot RAM are now opt-in (internal RAM is scarce because FabGL keeps the
  VGA framebuffer there). New serial line `HEAP antes do fMSX`.
- Details: `RELEASE_V1_08.md`.
