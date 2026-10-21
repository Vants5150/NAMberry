# NAMberry

A bare-metal guitar multi-effects processor for the **Raspberry Pi 5** with a **Blokas Pisound** audio interface. It runs on [Circle](https://github.com/rsta2/circle) (no Linux), is controlled from a web page over Wi-Fi, and can run Neural Amp Modeler (NAM) captures next to a set of effects, many of them modelled from the schematics of real pedals.

> **Status: experimental, one-person project.** It was developed and tested on a single board. Read the "Honest limits" in [EFFECTS.md](EFFECTS.md) before trusting any effect to sound like the pedal it is named after.

## What it does

* **Pedalboard grid** (6 rows x 12 columns): place blocks, drag them, draw links between them (series, parallel, splits, mixes), left/right routing, two inputs and two outputs.
* **Blocks**: about 60 effects, listed in alphabetical order in the add menu (see [EFFECTS.md](EFFECTS.md)), a NAM block, cabinet impulse responses, EQ, delays, reverbs, modulation, dynamics.
* **NAM models** in A2 WaveNet form, converted to a compact `.adn` file by `tools/nam2adn.py` and run by the ADELnam engine (`adelnam_engine.hpp`), which is written for this project (no third-party code in the firmware).
* **Web page** served by the board: presets, per-block controls and meters, model/IR library upload, MIDI settings, service functions, temperature and fan control.
* **Seamless preset change** (the old preset's delay and reverb tails keep ringing while the new one starts).
* **MIDI** over USB (DIN input is present in the code but disabled by default because it is untested on hardware).
* **Updates over Wi-Fi** (signed with an HMAC key stored on the SD card, `ota.key`), a **fallback WPA2 access point** when the home network is missing, and a **concert mode** that boots without any network.
* **DI recorder**: records the raw input (24-bit stereo) into memory and downloads it as a WAV file from the page.

## Screenshots

![The pedalboard grid in the web page](docs/img/pedalboard.png)

*The web page served by the board, showing a test preset with two NAM captures, compressor, noise gate, MXR 117 flanger, Boss CE-1 and CE-2 chorus models, ducking delay, delay, reverb, spring reverb and tone stack, linked with series, parallel and stereo routes.*

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
| DI recorder buffer | up to about 236 MB for 614 s of 24-bit stereo (stored as 32-bit words), allocated only while a recording exists |
| Free heap reported by the firmware after boot | about 4.1 million KB |
## Features in detail

### Pedalboard and blocks

* A 6 x 12 grid. Drag from a block's output port to another block's input port to draw an arrow; the level on every arrow can be changed (in dB) and arrows can run diagonally. Series, parallel branches, splits and mixes are all just arrows. There are two inputs and two outputs.
* Press and hold a block for about 0.6 s and a trash button appears over it; tap it to remove the block.
* Every block has its own panel with the controls grouped in sections and a "Saved settings" menu for block presets (the author's board holds 239, installed with `tools/block_presets.py`: four for every effect, about 45 of them with values from manuals, articles and forum posts (ZVEX Fuzz Factory manual, Tube Screamer and Big Muff settings of famous players, The Edge's dotted-eighth delay, slapback times, reverb decay guides and others); the rest are marked `(typical)` and are starting points built on how the controls are described, not documented settings of a player. Every source and every assumption is in `presets_fonti.txt`).

![A delay block panel with all its sections open](docs/img/delay_panel.png)

### Preset analysis

The magnifier button next to the preset name opens an analysis of the preset. **It does not listen to the sound**: it reads the structure of the pedalboard and tells you, block by block, what looks wrong, why, and what to do about it, with buttons that select the blocks involved and an "Ignore" button for advice you disagree with (remembered per preset). It checks:

* **Routing:** no input or no output connected, blocks that receive no signal or whose output goes nowhere (a NAM or Cab IR in that state is not computed, so it does not use CPU), blocks that are switched off, a stereo block that ends on a single output, one output channel left silent.
* **Levels:** the estimated gain from input to output, based on the arrows' dB and the Volume and Pan controls; a warning when the signal is very low. For every NAM block it compares the input level you set with the level the capture was made at and warns when it is far off.
* **Phase between parallel branches:** when two branches that contain different amp or cabinet models start from the same signal and are summed again, they can cancel each other (comb filtering). The analysis explains where the first notch falls for a given delay and offers the measurement below.
* **Order and tone advice** for a guitar chain: reverb or delay placed before a NAM, no cabinet or equalizer after a NAM when the output goes to headphones or a mixer, a high cut left open after a distorted sound, a boost in the 200-500 Hz "mud" zone, a treble shelf after distortion, high gain without a noise gate. The "After the output there is" selector tells it whether a power amp and cabinet follow (then it does not suggest cabinet or EQ). Every piece of advice cites the forum posts, articles or manuals it comes from; these are indications from players' experience, not measurements.
* **Measure the phase:** a button that does by itself what used to be done by hand. It adds a probe block (`ADELSendSweep`) and `ADELphase` to the preset, plays a 10-second sweep with the output muted, reads the verdict (phase OK, invert the polarity of one branch, delay branch A or B, the paths are not correlated, phase rotation) and, if you press "Apply the correction", writes the delay and polarity into `ADELphase`. It then measures again to confirm and puts the preset back as it was.

![The preset analysis panel](docs/img/preset_analysis.png)

### Harmonizer

* **Manual section:** with Manual on, each of the three voices has down and up arrows that move it by one semitone from the note you are playing now (hold to repeat). The voice is put in Chromatic mode, and the page shows the note being played and the note each voice makes, so a harmony can be built by ear. "Save settings..." stores the three voices as a named setting.
* **Pitch tracker:** an attack detector limits the analysis window to the new note, the note that is sounding keeps its pitch while its period still fits, and an octave leap without a new attack needs many agreeing readings. On the synthetic test bench (`tools/tracker`) the notes reported shorter than 50 ms fell from 65 to 13, the note changes from 184 to 133 for 160 real notes, and the latency of picked notes from 27 to 18 ms; chords are still not followed as well as single notes.
* Five presets named "Track 1..5" are made to try the tracker (octave up, octave down, fifth, fifth + octave, third and fifth in E-flat major).
### NAM models

A2 WaveNet captures run in the ADELnam engine. Each model's input level is set from the metadata of the capture (`input_level_dbu`) and the interface's measured maximum level; bass, mid and treble controls and a per-block level offset are available. Models are uploaded from the page into the library, with an author/license note stored next to each one.

### Playing live and keeping it running

* **Seamless preset change**: the tails of the old preset's delays and reverbs keep ringing while the new preset starts, and the NAM states, model files and parsed presets are cached so a change takes a few milliseconds.
* **Concert mode**: boots without Wi-Fi or page, for the safest condition on stage. It can be entered and left with the Pisound button (hold for 3 seconds).
* **Fallback access point** and **updates over Wi-Fi**, signed with a key on the SD card.
* **Fan and temperature control**, and a safe mode when the boot is not clean.

### Tools

* **DI recorder**: records the raw guitar input (24-bit stereo) in memory, up to about 10 minutes, and downloads it as a WAV file; several takes can be added one after the other (a small .csv file says where each starts). It is meant to build test material, for example for a note tracker.
* **Measurement tools** as blocks: `ADELSendSweep` (sweep, guitar notes, chord or pink noise at the press of a button) and `ADELphase` (polarity, delay, all-pass and phase measurement between two channels).
* **Service functions** that can be switched on and off from the page: hum, rumble, limiter, mono, air, saturation, click, RTA, tuner and meter.
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
