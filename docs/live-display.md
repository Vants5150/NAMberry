# Live Display

A small wired touch display for playing live: it shows the preset and its blocks, switches blocks and presets with a tap, edits parameters with knobs and a slider, changes the NAM model of a block, and builds a simple preset, with no phone, tablet or Wi-Fi in the signal of use. The display is an ESP32 board connected to a USB port of the Raspberry Pi 5 with one USB cable; the firmware talks to it over USB serial. Two boards are supported, one at a time on the cable: a **2.8" ESP32-S3** and a **4.0" ESP32 (classic)**.

![The Live Display link: the ESP32 touch display on a USB cable to the Raspberry Pi 5 that runs the firmware; the display's Wi-Fi is used only to update it](img/live_display_link.svg)

**Status: works on the author's board, one unit of each display.** What was measured and what was only read from code or data sheets is stated each time. The audio path (latency, noise) was **not** re-measured with a display connected, see "What is not measured".

| | Freenove ESP32-S3 2.8" (FNK0104B) | Freenove ESP32 CYD 4.0" (FNK0114S) |
|---|---|---|
| Screen | ILI9341, 240 x 320 | ST7796, 320 x 480 |
| Touch | capacitive (FT6336, I2C) | resistive (XPT2046, on the screen's SPI bus) |
| USB | native USB Serial/JTAG (CDC) | UART through a CH340 |
| Memory | 8 MB PSRAM | none |
| Tried on the cable | yes (with the Circle patch) | yes (CH341 driver of Circle, no patch) |

## What it does

| Screen | What you can do |
|---|---|
| **Stage** (home) | The preset name with `<` `>` to change it (tap the name for the list). The blocks as coloured tiles in the order of the signal (NAM green, dynamics and tone blue, modulation purple, delay and reverb amber) with a LED, the name and the main value. **Tap** a tile: the block goes on or off. **Press and hold** (about 0.6 s): its knobs. Swipe up or down for the next page of tiles. The two bars at the sides are the peak levels of the outputs. |
| **Knobs** | The parameters of one block as arc knobs; tap a knob to edit it; swipe sideways for the next block, up or down for more knobs. The ON / OFF pill in the header switches the block; for a NAM block, tapping its name opens the model list. |
| **Editor** | The value in large type, a slider you can touch or drag, `--` `-` `+` `++` (one step and ten steps), and the **starting value** (written and marked on the slider) with an **Undo** button that puts it back. Choices and switches become a single button. |
| **NAM model** | The library (282 models on the author's card), filtered by a text typed on the screen keyboard; the model in use is green. |
| **Presets** | The saved presets (tap to load). The **+** button creates a new simple preset. |
| **New preset** | One lane only: choose the **input** (L or R), up to **four blocks** (a NAM or any of the effects, with a filter) and the **output** (L, R or both). **Create** builds it on the board and saves it under the first free name (`New 1`, `New 2`, ...). |
| **Cfg** | Wi-Fi (scan, choose, type the password on the screen), forget the network, rotate the screen (portrait, landscape and both upside down), version and build time. |

The number of tiles and knobs per page follows the screen size (8 tiles per page on the 2.8" in landscape; 24 on the 4.0" as the layout computes it, not counted on the screen).

Not done from the display: drawing or removing arrows, moving blocks, deleting or renaming presets, **saving** the edited preset (parameter changes act on the sound at once but are stored only when you save from the web page; the preset name shows `*` while it holds unsaved changes), uploading models, MIDI settings, the service functions. A preset created with **+** is saved automatically, but only has one lane.

## Hardware

Freenove's own material for the two families: [Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display) (FNK0104) and [Freenove_ESP32_Display](https://github.com/Freenove/Freenove_ESP32_Display) (FNK0103, which holds the setup of the 4.0" board, folder `FNK0114S_4.0inch_ST7796`).

### ESP32-S3, 2.8" (FNK0104B)

| Part | Specification (found on the real board unless stated) |
|---|---|
| SoC | ESP32-S3 QFN56 rev v0.2 (read with esptool) |
| Flash and RAM | 16 MB flash; 8 MB PSRAM, octal (OPI) |
| USB | the built-in USB Serial/JTAG of the ESP32-S3 (VID 303a, PID 1001); it enumerates as a CDC serial device, so one USB-C cable carries the data and the power |
| Screen | ILI9341, 240 x 320, IPS, SPI on HSPI at 40 MHz, colour order BGR, inversion on; TFT_eSPI's `ILI9341_2_DRIVER` init is the one that works (the generic one left it black) |
| Screen pins | MISO 13, MOSI 11, SCLK 12, CS 10, DC 46, backlight 45 (high = on); RST shared with the board reset |
| Touch | FT6336 on I2C address 0x38, SDA 16, SCL 15, reset 18 (INT is on 17 in the design files, not used); one finger; it reports the position in the portrait frame (x 0..239, y 0..319) |
| Present but not used | ES8311 audio codec (answers on I2C 0x18), SD card slot, RGB LED on IO42, microphone |
| Orientation sensor | none found: no motion sensor is listed in the board's documentation and none answered on the addresses that were probed (the I2C bus was not scanned completely). The screen orientation is chosen in Cfg, not sensed |
| Firmware size | 1,146,848 bytes (36 % of the 3 MB application partition), 17 % of the RAM |

The pins were taken from the board's design files and then confirmed with a bare-metal test sketch (`display/bringup/display_probe`): the first init sequence I tried (Adafruit values on the FSPI port) gave a black screen, the TFT_eSPI one on HSPI works.

### ESP32 CYD, 4.0" (FNK0114S)

| Part | Specification |
|---|---|
| SoC | ESP32-D0WD-V3 rev v3.1, dual core 240 MHz, Wi-Fi and Bluetooth (read with esptool) |
| Flash and RAM | 4 MB flash; **no PSRAM** (none reported by the chip) |
| USB | a CH340 UART bridge (VID 1a86, PID 7523) on UART0: `/dev/ttyUSB*` on Linux; it is also the bootloader's port. The Circle CH341 driver recognizes it |
| Screen | ST7796, 320 x 480, SPI on HSPI at 80 MHz (Freenove's setup file; the generic colour order and no inversion work) |
| Screen pins | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, backlight 27 (high = on), RST shared with the board reset (from Freenove's setup file, the screen works with them) |
| Touch | XPT2046 resistive, chip select 33, on the same SPI bus; read through TFT_eSPI. Freenove's example calibration for rotation 1 (`{303, 3458, 350, 3304, 7}`) is used, and the touch was tried by hand on the author's unit |
| Firmware size | 1,166,768 bytes (59 % of the 1.9 MB application partition, `min_spiffs` scheme: two OTA slots), 17 % of the static RAM |

The chip's own factory flash was dumped before it was overwritten (4 MB, read back and checked).

## How it is connected

The cable goes from the display's USB connector to a USB-A port of the Pi 5. The Pi's USB host controller is brought up only in the "MIDI hardware" build (`MIDI_HW`); the firmware looks for the first USB serial device (`utty1`) once a second and serves the display's requests from the MIDI task, never from the audio callback. Only one display can be served at a time (the one found first). When it is found, the firmware sets 460,800 baud (the CDC device of the ESP32-S3 ignores it).

**S3: a change to Circle is needed.** The data interface of the ESP32-S3's USB Serial/JTAG has CDC subclass 2, not 0, and Circle's CDC driver rejected it (`uttycdc Invalid configuration descriptor`). `circle-patches/circle-namberry.patch` contains `CUSBFunction::SelectInterfaceByClassSub()` and its use in `lib/usb/usbserialcdc.cpp`. The complete patch was checked to apply on a clean checkout of Circle commit `7a81e1b`.

**4.0": no change.** Circle's CH341 driver binds the CH340 bridge and sets the speed. 460,800 baud was chosen because 921,600 gave read errors when esptool dumped the flash through the same bridge; whether 921,600 works for the link was not tried.

### The protocol

Text lines, the same JSON the web page uses (the display reuses the web handlers in `control.cpp`; the page's token is added by the board, so nothing secret travels on the cable):

```
display -> board   Q <id> G <path> [<query>]        e.g.  Q 7 G /status     Q 9 G /fx k=noisegate
display -> board   Q <id> P <path> <form>           e.g.  Q 8 P /g op=set&id=4&k=thr&v=-30
board   -> display A <id> <http status> <length>\n  followed by <length> bytes of body
board   -> display NB hello <ms>                    once a second (ignored)
```

**The effect definitions on demand.** `GET /fx` is 110 KB (all the effects, with parameters and descriptions), too much for a board without PSRAM. The firmware answers `GET /fx?k=<effect>` with that effect only and `GET /fx?l=1` with the list of keys and titles (a few KB); without parameters it is the same as before, so the web page is unchanged. The display asks for the definitions of the effects of the preset in use, a few at a time, and keeps them. A board with the old firmware ignores the filter and sends everything: the display notices and takes it all (the S3 can).

**Flow control.** The first version sent the reply at full speed and the display never got it all: the ESP32-S3's USB driver copies each 64-byte packet into a receive queue and **drops the bytes that do not fit**, and the display reads the queue only between redraws. The board sent the 109,806 bytes of `/fx` in 171 ms and the display saw the counter grow by about 34 bytes a second in the one measurement made. Now the board sends the body in blocks of 4096 bytes and waits for a `K` from the display before the next block (it gives up after 1.5 s). The source is `src/serialrpc.*` (line parser, with `tests/test_serialrpc.cpp`) and `SerialSendBody()` in `src/kernel.cpp`.

### Measured

| What | Result |
|---|---|
| Effect definitions, all at once (`GET /fx`, S3) | 109,806 bytes in 380 ms (board side, 430 writes of 256 bytes); 384 ms counted by the display with a simulated board |
| One effect definition (4.0", through the web interface with a simulated board) | 1.3 to 7.7 KB in 28 to 170 ms each |
| Status (`GET /status`, 3.6 KB), S3 | 5 ms on the board; 61 to 68 ms from request to complete reply on the display |
| Status, 4.0" through the CH340 at 460,800 baud | 3,667 bytes in 16 writes, 78 ms on the board side (about 47 KB/s, the line speed) |
| Polling | every 0.5 s on the Stage screen (for the level bars), every 2 s elsewhere; the screen is repainted only when something visible changed |
| Board CPU with a display attached and running | S3: average 22.7 %, peak 24.7 to 26.2 %; 4.0": average 20.7 %, peak 21.5 %; no overruns in either, one reading each (the board's CPU figure, not a separate measurement of the link) |

## Building

**On the board.** Add to `config_local.h` (copied from `config_local.h.example`):

```
#define MIDI_HW            // USB host controller (off in the normal image)
#define USB_SERIAL_TEST    // the link with the display (the flag keeps the name it had while it was only a test)
```

and build as in the main README, with the Circle patch applied. The link is off in the normal image.

**On the display.** Arduino CLI with the `esp32` core 3.3.x, libraries **TFT_eSPI** (2.5.43) and **ArduinoJson** (7.x), and the settings in `display/nb_display/User_Setup_NB.h` (it holds both boards; the compiler tells which; see the comment at its top for where it goes).

```
# 2.8" ESP32-S3
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi display/nb_display
# 4.0" ESP32
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs display/nb_display
```

* **First load, over USB.** S3: `esptool --no-stub write-flash 0x10000 nb_display.ino.bin` (`--no-stub` is needed with this chip's USB Serial/JTAG for reads). 4.0": `esptool --baud 460800 write-flash 0x0 nb_display.ino.merged.bin` (the whole image the first time, it sets the partition table). Later USB loads should write **only the application** (S3: 0x10000; 4.0": 0x10000 too), because writing from address 0 erases the area where the display keeps the Wi-Fi network, and it has to be typed again.
* **Wi-Fi.** Nothing is stored in the sources. On the display: Cfg, Scan networks, choose, type the password on the screen keyboard. It is kept in the ESP32's flash (NVS) in clear text.
* **Updates** afterwards go over Wi-Fi (ArduinoOTA, host `nb-display` for the S3 and `nb-display-cyd` for the 4.0") with `espota.py`, with the cable still on the board. Without a password anyone on the network can load firmware into the display: set `OTA_PASSWORD` in `wifi_config.h`.
* **Which firmware is running?** The screen shows `fw <number>` and the build time (Cfg and the waiting screen), and `http://<display address>/` answers in plain text with the version, build time, uptime, a few counters and the last presses of the touch. Check it after every update: an update that did not arrive looks the same as one that did. The page has no authentication.

`display/bringup/` holds the two sketches used to bring the S3 up (pins and init; a ping/pong test of the link). `display/tools/fake_namberry.py` plays the board's side of the protocol on a PC to test a display alone; `display/tools/bridge_namberry.py` plays it and forwards the requests to the real board over its web interface, so a display can be tried before the board has the link.

## What went wrong on the way

Written down because each cost time:

* The first Circle image did not bind the S3 (CDC data interface subclass 2). The Circle patch above fixes it.
* The link was "answering" with small ping/pong lines but the 109 KB reply never completed: the ESP32-S3's receive queue drops bytes (see Flow control).
* Opening the S3's serial port with RTS low (some tools do) made it send nothing.
* Wi-Fi updates failed about half of the time: ArduinoOTA aborts after 1 s without data and the Wi-Fi power saving added more than that. The update now runs in its own task, with a 15 s timeout and the modem sleep off.
* Every serial reply used to repaint the screen once (a progress counter meant for the first download), which showed up as a flicker whenever a value was sent. On the S3, whole-screen redraws are now composed off screen (PSRAM) and sent in one piece; single tiles, knobs, level bars and the slider are repainted alone. The 4.0" has no PSRAM and draws whole views straight to the screen, so a change of screen can still flash.
* The 4.0" touch, three causes, found by logging every press on the status page: (1) the touch was sampled once per turn of the main loop and a short tap during a 75 ms status read was lost; it is now sampled while waiting for the board too; (2) a resistive screen reads positions 20 to 30 px off while the pressure rises and falls, which made roughly one tap in five look like a swipe; a reading now counts only when the pressure is at least half of the strongest of the press; (3) a tap that fell 1 px above a tile hit nothing; tiles now have 3 px of margin.
* Without PSRAM the 110 KB description would not fit, hence the effects on demand.
* Resetting a display many times in a row made the Pi report USB over-current on all its ports (they are ganged).

## What is not measured

* **Audio latency and noise with a display connected.** The link is served by the MIDI task and the audio callback was not touched, and the board's counters showed no overruns, but the loop-back latency was not measured again and nothing was recorded to compare.
* **Changing from one display to the other while the board runs.** The firmware notices that the device is gone and looks for another every second, but this was not tried. The S3 was last tried on the cable with the previous firmware (without the `/fx` filter and the baud setting), which it should still work with.
* Long runs of days, and what happens to the board if the display stops reading in the middle of a reply (the write is synchronous; the 1.5 s timeout covers a display that stops answering `K`, not a hung USB transfer).
* The touch calibration of the 4.0" beyond trying it by hand; the landscape touch mapping of the S3 was derived from the display driver's register table and also tried by hand.
* The filter of `/fx` is not covered by a host test (the handler needs the web server); it was checked on the real board: without parameters 109,806 bytes as before, `l=1` 3,502 bytes, `k=noisegate` 838 bytes, `k=adelverbm` 7,115 bytes, an unknown effect `[]`.
* Other boards of the same families, other screen sizes, and the touch-less FNK0104A (no touch, so no use).
