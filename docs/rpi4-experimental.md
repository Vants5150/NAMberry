# Raspberry Pi 4 (experimental)

Builds the firmware for a Raspberry Pi 4. Not tested on hardware.

## Changes

* Code that depends on the Pi 5's RP1 chip is guarded with `#if RASPPI == 5` (`src/main.cpp`, `src/kernel.cpp`): DMA reset at startup, DMA diagnostics, PWM fan.
* `src/main.cpp` provides `__getauxval` (needed by libgcc's outline atomics on bare metal).

## Build

As in the README (Getting started), with `RASPPI = 4` in Circle's `Config.mk`. Result: `kernel8-rpi4.img`.

## SD card

* In `circle/boot` run `make`; copy `start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`, `LICENCE.broadcom`, and `config64.txt` as `config.txt`.
* `kernel8-rpi4.img`.
* Wi-Fi firmware: in `circle/addon/wlan/firmware` run `make`; copy the files into a folder `firmware/`.
* `wpa_supplicant.conf` as in the README.

## Updates

The in-firmware update looks for `kernel_2712.img`: on a Pi 4 copy `kernel8-rpi4.img` to the card.
