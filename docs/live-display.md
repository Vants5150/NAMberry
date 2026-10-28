# Live Display

A small wired touch display for playing live: it shows the preset and its blocks, switches blocks and presets with a tap, edits parameters with knobs and a slider, changes the NAM model of a block, and builds a simple preset, with no phone, tablet or Wi-Fi in the signal of use. The display is an ESP32-S3 board connected to a USB port of the Raspberry Pi 5 with one USB-C cable; the firmware talks to it over USB serial.

![The Live Display link: the ESP32-S3 touch display on a USB cable to the Raspberry Pi 5 that runs the firmware; the display's Wi-Fi is used only to update it](img/live_display_link.svg)

**Status: works on the author's board, one unit.** Everything below was done and checked with a Freenove FNK0104B and one Raspberry Pi 5; what was measured and what was only read from code or data sheets is stated each time. The audio path (latency, noise) was **not** re-measured with the display connected, see "What is not measured".

## What it does

| Screen | What you can do |
|---|---|
| **Stage** (home) | The preset name with `<` `>` to change it (tap the name for the list). The blocks as coloured tiles in the order of the signal (NAM green, dynamics and tone blue, modulation purple, delay and reverb amber) with a LED, the name and the main value. **Tap** a tile: the block goes on or off. **Press and hold** (about 0.6 s): its knobs. Swipe up or down for the next page of tiles. The two bars at the sides are the peak levels of the outputs. |
| **Knobs** | The parameters of one block as arc knobs, six per page. Tap a knob to edit it; swipe sideways for the next block, up or down for more knobs. The ON / OFF pill in the header switches the block; for a NAM block, tapping its name opens the model list. |
| **Editor** | The value in large type, a slider you can touch or drag, `--` `-` `+` `++` (one step and ten steps), and the **starting value** (written and marked on the slider) with an **Undo** button that puts it back. Choices and switches become a single button. |
| **NAM model** | The library (282 models on the author's card), filtered by a text typed on the screen keyboard; the model in use is green. |
| **Presets** | The saved presets (tap to load). The **+** button creates a new simple preset. |
| **New preset** | One lane only: choose the **input** (L or R), up to **four blocks** (a NAM or any of the effects, with a filter) and the **output** (L, R or both). **Create** builds it on the board and saves it under the first free name (`New 1`, `New 2`, ...). |
| **Cfg** | Wi-Fi (scan, choose, type the password on the screen), forget the network, rotate the screen (portrait, landscape and both upside down), version and build time. |

Not done from the display: drawing or removing arrows, moving blocks, deleting or renaming presets, **saving** the edited preset (parameter changes act on the sound at once but are stored only when you save from the web page; the preset name shows `*` while it holds unsaved changes), uploading models, MIDI settings, the service functions. A preset created with **+** is saved automatically, but only has one lane.

## Hardware

Tested with the **Freenove ESP32-S3 Display, 2.8 inch, capacitive touch (FNK0104B)**. Freenove's own material for the family is at [github.com/Freenove/Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display).

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
| Firmware size | 1,142,719 bytes (36 % of the 3 MB application partition), 16 % of the RAM |

The pins were taken from the board's design files and then confirmed with a bare-metal test sketch (`display/bringup/display_probe`): the first init sequence I tried (Adafruit values on the FSPI port) gave a black screen, the TFT_eSPI one on HSPI works.

## How it is connected

The cable goes from the display's USB-C connector to a USB-A port of the Pi 5. The Pi's USB host controller is brought up only in the "MIDI hardware" build (`MIDI_HW`); the firmware looks for the serial device `utty1` once a second and serves the display's requests from the MIDI task, never from the audio callback.

**A change to Circle is needed.** The data interface of the ESP32-S3's USB Serial/JTAG has CDC subclass 2, not 0, and Circle's CDC driver rejected it (`uttycdc Invalid configuration descriptor`). `circle-patches/circle-namberry.patch` now also contains `CUSBFunction::SelectInterfaceByClassSub()` and its use in `lib/usb/usbserialcdc.cpp`. The complete patch was checked to apply on a clean checkout of Circle commit `7a81e1b`.

### The protocol

Text lines, the same JSON the web page uses (the display reuses the web handlers in `control.cpp`; the page's token is added by the board, so nothing secret travels on the cable):

```
display -> board   Q <id> G <path>                  e.g.  Q 7 G /status
display -> board   Q <id> P <path> <form>           e.g.  Q 8 P /g op=set&id=4&k=thr&v=-30
board   -> display A <id> <http status> <length>\n  followed by <length> bytes of body
board   -> display NB hello <ms>                    once a second (ignored)
```

**Flow control.** The first version sent the reply at full speed and the display never got it all: the ESP32 USB driver copies each 64-byte packet into a receive queue and **drops the bytes that do not fit**, and the display reads the queue only between redraws. The board sent the 109,806 bytes of `/fx` in 171 ms and the display saw the counter grow by about 34 bytes a second in the one measurement made. Now the board sends the body in blocks of 4096 bytes and waits for a `K` from the display before the next block (it gives up after 1.5 s). The source is `src/serialrpc.*` (line parser, with `tests/test_serialrpc.cpp`) and `SerialSendBody()` in `src/kernel.cpp`.

### Measured

| What | Result |
|---|---|
| Effect definitions (`GET /fx`, once after the display starts) | 109,806 bytes in 380 ms (board side, 430 writes of 256 bytes); 384 ms counted by the display with a simulated board |
| Status (`GET /status`, 3.6 KB) | 5 ms on the board; 61 to 68 ms from request to complete reply on the display |
| Polling | every 0.5 s on the Stage screen (for the level bars), every 2 s elsewhere; the screen is repainted only when something visible changed |
| Board CPU with the display attached and running | average 22.7 %, peak 24.7 to 26.2 %, no overruns, in one reading after about an hour (the board's CPU figure, not a separate measurement of the link) |

## Building

**On the board.** Add to `config_local.h` (copied from `config_local.h.example`):

```
#define MIDI_HW            // USB host controller (off in the normal image)
#define USB_SERIAL_TEST    // the link with the display (the flag keeps the name it had while it was only a test)
```

and build as in the main README, with the Circle patch applied. The link is off in the normal image.

**On the display.** Arduino CLI with the `esp32` core 3.3.x, libraries **TFT_eSPI** (2.5.43) and **ArduinoJson** (7.x), and the settings in `display/nb_display/User_Setup_NB.h` (see the comment at its top for where it goes).

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi display/nb_display
```

* **First load, over USB** (the display on any computer): `esptool --no-stub write-flash 0x10000 nb_display.ino.bin`. Write **only the application** (0x10000): writing the whole image from address 0 also erases the area where the display keeps the Wi-Fi network, and it has to be typed again. `--no-stub` is needed with this chip's USB Serial/JTAG for reads.
* **Wi-Fi.** Nothing is stored in the sources. On the display: Cfg, Scan networks, choose, type the password on the screen keyboard. It is kept in the ESP32's flash (NVS) in clear text.
* **Updates** afterwards go over Wi-Fi (ArduinoOTA, host `nb-display`) with `espota.py`, with the cable still on the board. Without a password anyone on the network can load firmware into the display: set `OTA_PASSWORD` in `wifi_config.h`.
* **Which firmware is running?** The screen shows `fw <number>` and the build time (Cfg and the waiting screen), and `http://<display address>/` answers in plain text with the version, build time, uptime and a few counters. Check it after every update: an update that did not arrive looks the same as one that did. The page has no authentication.

`display/bringup/` holds the two sketches used to bring the hardware up (pins and init; a ping/pong test of the link), and `display/tools/fake_namberry.py` plays the board's side of the protocol on a PC to test the display alone.

## What went wrong on the way

Written down because each cost time:

* The first Circle image did not bind the display (CDC data interface subclass 2). The Circle patch above fixes it.
* The link was "answering" with small ping/pong lines but the 109 KB reply never completed: the ESP32 receive queue drops bytes (see Flow control).
* Opening the display's serial port with RTS low (some tools do) made it send nothing.
* Wi-Fi updates failed about half of the time: ArduinoOTA aborts after 1 s without data and the Wi-Fi power saving added more than that. The update now runs in its own task, with a 15 s timeout and the modem sleep off.
* Every serial reply used to repaint the screen once (a progress counter meant for the first download), which showed up as a flicker whenever a value was sent. Whole-screen redraws are now composed off screen and sent in one piece; single tiles, knobs, level bars and the slider are repainted alone.
* Resetting the display many times in a row made the Pi report USB over-current on all its ports (they are ganged).

## What is not measured

* **Audio latency and noise with the display connected.** The link is served by the MIDI task and the audio callback was not touched, and the board's counters showed no overruns, but the loop-back latency was not measured again and nothing was recorded to compare.
* Long runs of days, unplugging and plugging the display while the board runs, and what happens to the board if the display stops reading in the middle of a reply (the write is synchronous; the 1.5 s timeout covers a display that stops answering `K`, not a hung USB transfer).
* The landscape touch mapping was derived from the display driver's register table and then tried by hand on the author's unit; it was not measured.
* Other boards of the same family, other screen sizes, and the touch-less FNK0104A (no touch, so no use).
