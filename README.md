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

## Measured figures

Measured on 2026-10-21 with firmware build `mcore92` on a Raspberry Pi 5 (48 kHz, 4-frame blocks). What was measured and what was only calculated is stated for every number.

### Quality

| What | Result | How |
|---|---|---|
| ADELnam engine against the NeuralAudio reference (A2 WaveNet, full quality) | relative error -128 to -118 dB (float32 rounding level) | `AdnCompare` on two models and three test signals (glissando, plucked notes, loud noise), run on a Pi 5 under Linux |
| Tube Screamer circuit model against real TS9 captures | 0.42 dB rms, 40 Hz - 10 kHz, small signal | nine TONE3000 captures; the model has four free parameters |
| RAT circuit model against real RAT captures | 1.49 dB rms over 294 points | fitted to 14 TONE3000 captures (so this is a fit, not an independent check) |
| LA-2A style optical compressor | 0.5 - 0.6 dB gain-reduction error on music | fitted to SignalTrain recordings; about 1.2 - 2.2 dB off on noise not used in the fit |
| All the other circuit models | not compared with recordings of the real pedals | see [EFFECTS.md](EFFECTS.md) for sources and assumptions |
| Automated tests | 35 host test steps and 13 web-page tests, all passing | run as part of the `mcore92` build |

### Latency

| Block size | Block period | Guitar-to-amp latency (calculated) |
|---|---|---|
| 2 frames | 41.7 us | 1.000 ms |
| **4 frames (default)** | **83.3 us** | **1.083 ms** |
| 8 frames | 166.7 us | 1.250 ms |
| 16 frames | 333.3 us | 1.583 ms |

The latency is calculated, not measured with a loop-back cable: two blocks plus 44 frames of converter filter delay, the same figure the web page shows. A measured round-trip with a physical loop is still to do.

### CPU

Share of the audio core's time per block (Pi 5, 4-frame blocks, benchmark under Linux, mean of a 3 s run):

| NAM models running (A2, 8 channels) | ADELnam engine | NeuralAudio reference |
|---|---|---|
| 1 | 5.5 % | 12.9 - 13.7 % |
| 2 | 11.2 % | 27.4 - 28.9 % |
| 3 | 17.2 % | - |

On the running board with a test preset (one NAM, eleven other blocks, four cores in use): average CPU 24.5 %, peak 30.0 %, no overruns, SoC temperature 57 C (one reading). Worker-core statistics since boot: 3,788,670 blocks, 0 late, 0 faults.

### Space

| What | Size |
|---|---|
| Firmware image (`kernel_2712.img`) | 1,551,088 bytes |
| Room left below the kernel limit (end of the image and its zeroed data must stay under 0x280000) | 266,784 bytes |
| One NAM A2 model as `.adn` (8 channels, 23 layers, 12,146 weights, receptive field 6,347 samples) | 48,880 bytes |
| A library of 282 models | about 13.8 MB (calculated: 282 x 48,880 bytes) |
| Web page as served (gzip) | 63,364 bytes (source `control_page.h` is 190 KB) |
| DI recorder buffer | up to about 15.7 MB for 40 s of 24-bit stereo, allocated only while a recording exists |
| Free heap reported by the firmware after boot | about 4.1 million KB |
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
