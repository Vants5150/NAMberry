# NOTICE - third-party software and sources

NAMberry is Copyright (C) 2026 VanTs and is released under the **GNU General Public License v3.0** (see `LICENSE`).
The firmware is built on top of the Circle bare-metal environment, which is GPL-3.0. The source files of this repository are released under the GPL-3.0. The one component whose license is not settled is the Plan 9 derived part of Circle's Wi-Fi add-on, described in "Open licensing question" below.

## Software linked into the firmware

| Component | Used for | License | Where checked |
|---|---|---|---|
| [Circle](https://github.com/rsta2/circle) (R. Stange) | bare-metal environment, USB, network, sound, scheduler, file system glue | GPL-3.0 | `circle/LICENSE` |
| Circle Wi-Fi add-on (`addon/wlan`: `ether4330.c`, `bcm4343.cpp`, `p9*.cpp`, `emmc.c`) | Wi-Fi driver | `bcm4343.cpp` has R. Stange's GPL-3.0-or-later header. `ether4330.c` and `emmc.c` are ported from Plan 9 and have no license header; the add-on's `Plan9License` file applies (see "Open licensing question"). The `p9*.cpp` glue files have no header | `circle/addon/wlan/` (checked on commit `7a81e1b`) |
| [hostap / wpa_supplicant](https://w1.fi/wpa_supplicant/) (J. Malinen) | WPA2 supplicant and fallback access point | BSD license (the project dropped the GPL-2 alternative in 2012) | `circle/addon/wlan/hostap/COPYING` |
| [FatFs](http://elm-chan.org/fsw/ff/) (ChaN) | FAT file system on the SD card | FatFs license (1-clause BSD style) | `circle/addon/fatfs/LICENSE.txt` |

## Source files copied from Circle

* `oscillator.cpp`, `oscillator.h` and `synthmodule.h`: unchanged copies of the files in Circle's `sample/34-sounddevices` (MiniSynth Pi, Copyright (C) 2017 R. Stange, GPL-3.0 or later), used for the test tones.
* `circle-patches/circle-namberry.patch`: the modifications made to Circle (against commit `7a81e1b480fe7d19fc1fc695b89bff0a0848f44b`), under Circle's own license (GPL-3.0), with one exception: the part of the patch that changes `addon/wlan/ether4330.c` (a fallback access point with WPA2, about 40 changed lines) modifies a file derived from Plan 9 and is offered under that file's license, not under the GPL-3.0 (see "Open licensing question").

## Open licensing question

What was checked (2026-10-22, in Circle's tree at commit `7a81e1b`, and on the FSF license list):

* `addon/wlan/Plan9License` says that UC Berkeley was authorised by Alcatel-Lucent to release the Plan 9 software, previously under the Lucent Public License 1.02, under the **GNU GPL version 2**. The sentence that grants it says "version 2 of the License", with no "or later". The same file also says the Plan 9 software is provided under the Lucent Public License 1.02 with listed exceptions.
* `ether4330.c` and `emmc.c` contain no license or copyright text of their own, so the `Plan9License` file is the only statement that applies to them. Circle's `hello_wlan` README says only that the driver "has been ported from the Plan 9 operating system".
* The FSF license list says the Lucent Public License 1.02 is free but incompatible with the GNU GPL, and that GPLv2 is by itself not compatible with GPLv3 (most GPLv2 programs avoid the problem by allowing "any later version", which this text does not).

What follows from it, as a reading of those facts and not legal advice:

* This repository contains only source files written for this project (GPL-3.0) and a patch. It does **not** contain Circle or the Plan 9 files, and it does not distribute compiled images, so the combination that the licenses may not allow, a single binary that links GPL-3.0 code with the GPL-2.0-only Wi-Fi driver, is made by whoever builds the firmware, from sources they download themselves.
* **Do not publish a compiled firmware image** (for example as a GitHub release) until this is resolved: that would distribute the combined work.
* The `ether4330.c` part of `circle-patches/circle-namberry.patch` is a modification of a Plan 9 derived file, so it cannot simply be licensed under the GPL-3.0. It is offered under the license of the original file. Anyone who only wants the GPL-3.0 parts can apply the rest of the patch without that file.
* Circle's author distributes the same combination in Circle's own repository; how that is reconciled upstream has not been found in the files checked, and the question has not been put to the author.

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
