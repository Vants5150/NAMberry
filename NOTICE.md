# NOTICE - third-party software and sources

NAMberry is Copyright (C) 2026 VanTs and is released under the **GNU General Public License v3.0** (see `LICENSE`).
The firmware is built on top of the Circle bare-metal environment, which is GPL-3.0, so the whole firmware is distributed under the same license.

## Software linked into the firmware

| Component | Used for | License | Where checked |
|---|---|---|---|
| [Circle](https://github.com/rsta2/circle) (R. Stange) | bare-metal environment, USB, network, sound, scheduler, file system glue | GPL-3.0 | `circle/LICENSE` |
| Circle Wi-Fi add-on (`addon/wlan`: `ether4330.c`, `bcm4343.cpp`, `p9*.cpp`, `emmc.c`) | Wi-Fi driver | GPL-3.0 headers in the files checked (`ether4330.c`); the add-on directory also carries a `Plan9License` file (Plan 9 software released by UC Berkeley under GPL-2.0) | `circle/addon/wlan/` |
| [hostap / wpa_supplicant](https://w1.fi/wpa_supplicant/) (J. Malinen) | WPA2 supplicant and fallback access point | BSD license (the project dropped the GPL-2 alternative in 2012) | `circle/addon/wlan/hostap/COPYING` |
| [FatFs](http://elm-chan.org/fsw/ff/) (ChaN) | FAT file system on the SD card | FatFs license (1-clause BSD style) | `circle/addon/fatfs/LICENSE.txt` |

## Source files copied from Circle

* `oscillator.cpp` / `oscillator.h`: unchanged copies of the files in Circle's `sample/34-sounddevices` (MiniSynth Pi, Copyright (C) 2017 R. Stange, GPL-3.0 or later), used for the test tones.
* `circle-patches/circle-namberry.patch`: the modifications made to Circle (against commit `7a81e1b480fe7d19fc1fc695b89bff0a0848f44b`), under Circle's own license (GPL-3.0; the patched add-on files keep their original headers).

## Not distributed in this repository

* **Wi-Fi firmware blobs** (`brcmfmac*` files from Circle's `boot/` and `addon/wlan/firmware`): Broadcom/Raspberry Pi license (`LICENCE.broadcom`), redistribution only in binary form and only for use with Raspberry Pi devices. They must be taken from Circle's own distribution when preparing the SD card.
* **NAM models, captures and cabinet impulse responses**: not included. Models you add to the SD card keep the license chosen by their authors (the web page can store an author/license note next to every model).

## Used only for testing, not part of the firmware

* [NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) (M. Oliphant, MIT) with [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (S. Atkinson, MIT), [RTNeural](https://github.com/jatinchowdhury18/RTNeural) and math_approx (J. Chowdhury, BSD-3-Clause), Eigen and nlohmann/json: used by the helper tools to compare the output of the ADELnam engine against the reference implementation. No source of these projects is compiled into the firmware (the Makefile does not reference them). The ADELnam engine (`adelnam_engine.hpp`) is written for this project; it reads the compact `.adn` format produced by `nam2adn.py` from standard `.nam` files.

## Sources used to model the circuits

The `Real` effects are written from published schematics, service notes, datasheets and papers. Only facts (component values, topologies, equations, measured figures) were taken; no code or text was copied. The sources of each effect, and what was assumed where they were silent, are listed in the header comment of its source file and summarised in `EFFECTS.md`. Main references:

* ElectroSmash circuit analyses (Fuzz Face, Rangemaster, Dyna Comp, Phase 90, DS-1, Guv'nor, Klon Centaur, Tube Screamer, RAT, Wah, Big Muff Pi, CE-2).
* Papers: Darabundit et al., *Digital Grey Box Model of the Uni-Vibe Effects Pedal* (DAFx-19); Eichas et al., *Physical Modeling of the MXR Phase 90* (DAFx-14); Giampiccolo et al., *Explicit Wave Digital Model of the Fulltone OCD Pedal* (DAFx26); Yeh and Smith, *Discretization of the '59 Fender Bassman Tone Stack* (DAFx-06); Colburn and Hawley, *SignalTrain LA2A Dataset* (CC BY 4.0), used to fit the optical compressor.
* Manufacturer service schematics and manuals (Boss CE-1, CE-2, OC-2; MXR 117 and Micro Flanger; Roland SDD-320; Ibanez AD9), The Big Muff Pi Page, Guitar PCB, Geofex, forum measurements (cited in the file headers).
* Component values of the tone stacks come from the Faust `tonestacks.lib` table (LGPL-2.1) and the Guitarix project; they are published figures, no code is reused.

All product names (Boss, MXR, Ibanez, Electro-Harmonix, Fulltone, Marshall, Fender, Roland, Dunlop, Vox, Klon, ProCo, Teletronix, Zvex, Shin-ei, Pisound, Raspberry Pi and others) are trademarks of their owners and are used only to say which circuit an effect models. This project is not affiliated with or endorsed by any of them.
