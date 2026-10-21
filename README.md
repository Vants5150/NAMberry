# NAMberry

A bare-metal guitar multi-effects processor for the **Raspberry Pi 5** with a **Blokas Pisound** audio interface. It runs on [Circle](https://github.com/rsta2/circle) (no Linux), is controlled from a web page over Wi-Fi, and can run Neural Amp Modeler (NAM) captures next to a set of effects, many of them modelled from the schematics of real pedals.

> **Status: experimental, one-person project.** It was developed and tested on a single board. Read the "Honest limits" in [EFFECTS.md](EFFECTS.md) before trusting any effect to sound like the pedal it is named after.

## What it does

* **Pedalboard grid** (6 rows x 12 columns): place blocks, drag them, draw links between them (series, parallel, splits, mixes), left/right routing, two inputs and two outputs.
* **Blocks**: about 50 effects (see [EFFECTS.md](EFFECTS.md)), a NAM block, cabinet impulse responses, EQ, delays, reverbs, modulation, dynamics.
* **NAM models** in A2 WaveNet form, converted to a compact `.adn` file by `tools/nam2adn.py` and run by the ADELnam engine (`adelnam_engine.hpp`), which is written for this project (no third-party code in the firmware).
* **Web page** served by the board: presets, per-block controls and meters, model/IR library upload, MIDI settings, service functions, temperature and fan control.
* **Seamless preset change** (the old preset's delay and reverb tails keep ringing while the new one starts).
* **MIDI** over USB (DIN input is present in the code but disabled by default because it is untested on hardware).
* **Updates over Wi-Fi** (signed with an HMAC key stored on the SD card, `ota.key`), a **fallback WPA2 access point** when the home network is missing, and a **concert mode** that boots without any network.
* **DI recorder**: records the raw input (24-bit stereo) into memory and downloads it as a WAV file from the page.

## Screenshots

![The pedalboard grid in the web page](docs/img/pedalboard.png)

*The web page served by the board: a test preset with a NAM capture, compressor, noise gate, flanger, two choruses, delays, reverb, spring and tone stack, linked with series, parallel and stereo routes.*

## Hardware

* Raspberry Pi 5
* Blokas Pisound (I2S audio, MIDI)
* microSD card with Circle's boot files and Wi-Fi firmware (see "Not included")
* A fan on the Pi 5 header is supported (PWM)

## Building

You need an `aarch64-linux-gnu` cross toolchain (or build on a Pi 5) and Circle.

1. Clone Circle at the commit this project was built against and apply the patch:
   ```
   git clone https://github.com/rsta2/circle.git
   cd circle
   git checkout 7a81e1b480fe7d19fc1fc695b89bff0a0848f44b
   git apply /path/to/NAMberry/circle-patches/circle-namberry.patch
   ```
   The patch contains the changes made to Circle (RP1 I2S/DMA buffer alignment and cache handling, Wi-Fi access point support, exception-handler diagnostics, `ARM_ALLOW_MULTI_CORE` enabled and `MAX_TASKS` raised to 48).
2. In Circle's `Config.mk` set `PREFIX64 = aarch64-linux-gnu-`, `AARCH = 64`, `RASPPI = 5`, then build Circle and the add-ons used by the Makefile (`addon/wlan`, `addon/fatfs`, `addon/SDCard`, `addon/tftpfileserver`, `lib`).
3. Copy this directory to `circle/addon/wlan/sample/pisound-wifi-test`.
4. Copy `config_local.h.example` to `config_local.h` and put the address of your PC (syslog and OTA).
5. Run `make`. The result is `kernel_2712.img`.

## Tests

Host tests (plain `g++`, no board needed) cover the DSP engines, the circuit solver and the control logic: files `test_*.cpp`. The page's JavaScript is tested with Node (`test_*.js`, `jscheck.sh`).

## Not included

* **Wi-Fi firmware blobs and Circle boot files**: take them from Circle's distribution (Broadcom/Raspberry Pi license, see [NOTICE.md](NOTICE.md)).
* **NAM models, captures and impulse responses**: bring your own and respect their authors' licenses.

## License

Copyright (C) 2026 VanTs. Released under the GPL-3.0, see [LICENSE](LICENSE). Third-party components and the sources used for the circuit models are listed in [NOTICE.md](NOTICE.md).

Product names are trademarks of their owners and appear only to say which circuit an effect models. The project is not affiliated with or endorsed by them.
