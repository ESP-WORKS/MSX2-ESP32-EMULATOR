# MSX2 for VGA32 (RobGo-RG)

MSX2 emulator (fMSX 6.0 by Marat Fayzullin) running directly on the ESP32-WROVER in the TTGO VGA32 / RobGo-RG board.

No launcher, no Retro-Go, no external ESP-IDF: just a regular PlatformIO project (Arduino + FabGL).

## Build

Open the folder in VS Code (with the PlatformIO extension) and use **Build** and **Upload**. The `platformio.ini` file already contains everything needed.

The `bootl.rc` pin validation includes a test that runs on the PC without requiring the board:

    pio test -e native

## SD Card

FAT32 format. Place the BIOS files in the `/MSX2` directory at the root of the card.

| File | Purpose |
|---|---|
| `MSX2.ROM` (32 KB) | Required |
| `MSX2EXT.ROM` (16 KB) | Required |
| `DISK.ROM` | Required for floppy disk support |
| `MSXDOS2.ROM` | Optional (MSX-DOS 2) |

The BIOS files are not included in the project. Filenames must match exactly.

ROMs, floppy disk images (`.dsk`), and save states (`.sta`) are selected through the fMSX menu (F10).

## Keyboard Controls (PS/2 Keyboard)

| Key | Action |
|---|---|
| F10 | fMSX menu: open file, save state, select model, manage disks, reset, reboot |
| `magicb` button | Same as F10, if configured in `bootl.rc` |
| F12 | Reset the MSX |
| F11 | Toggle frame skipping (0 / 25 / 50 / 75%) |
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
| `msx_biosdir=/msx` | BIOS root directory; files are stored in `/msx/MSX2/` |
| `msx_rom=/msx/game.rom` | Cartridge ROM loaded at startup |
| `msx_diska=/msx/disk.dsk`, `msx_diskb=...` | Floppy disks loaded at startup |
| `msx_frameskip=50` | Frames skipped: 0, 25, 50, or 75 |
| `msx_autoskip=1` | Enable automatic frame skip adjustment |
| `msx_fm=1` | Enable/disable FM synthesis (YM2413) |
| `msx_fps=1` | Display the FPS counter on the serial monitor |

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
