# Raspberry Pi 4 (experimental, untested)

This branch (`experimental-rpi4`) only tries to **build** the firmware for a Raspberry Pi 4. It has **never been started on a Pi 4**: the author has no Pi 4. It may not boot, may give no audio, or may not reach the latency of the Pi 5 version. Please report what you find in an issue.

## What was changed

Three places depended on the Pi 5's RP1 chip and are now guarded with `#if RASPPI == 5` (`src/main.cpp`, `src/kernel.cpp`):

* the reset of the RP1 DMA controller at startup (skipped on the Pi 4);
* the DMA descriptor diagnostics (`LliSnapshot`, `LliCheck`, skipped);
* the PWM fan on GPIO 45 (the fan logic is skipped).

On a Pi 4 libgcc's outline atomics ask the OS for the CPU features (`__getauxval`); there is no OS here, so `src/main.cpp` provides a function that answers "none" (the Cortex-A72 then uses the load/store-exclusive forms).

## What is NOT known

* Whether the Pi 4's own I2S (BCM2711, not the RP1) works as a slave with the Pisound at 48 kHz and 4-frame blocks, and at what latency.
* Whether the patches to Circle (`circle-patches/circle-namberry.patch`: interrupts, exception handler, USB, RP1 DMA) behave the same on a Pi 4. The RP1 parts are not used on a Pi 4.
* Whether the CPU is enough. On a Pi 5 the firmware uses about 25 % with full-quality NAM A2 models; nothing was measured on a Pi 4, whose cores are slower.
* The in-firmware OTA update looks for `kernel_2712.img` (Pi 5): on a Pi 4 copy the new `kernel8-rpi4.img` to the SD card by hand.

## Building (checked from a clean checkout)

Everything below was run from scratch; the build ends with `kernel8-rpi4.img` (about 1.5 MB, 242,672 bytes of margin below the 0x280000 kernel limit).

```
git clone https://github.com/rsta2/circle.git
cd circle
git checkout 7a81e1b480fe7d19fc1fc695b89bff0a0848f44b
git apply /path/to/NAMberry/circle-patches/circle-namberry.patch
git submodule update --init addon/wlan/hostap     # the Wi-Fi stack's sources (not mentioned in the main README)
```

`Config.mk` in Circle's root:

```
PREFIX64 = aarch64-linux-gnu-
AARCH = 64
RASPPI = 4
```

Build Circle's libraries and the add-ons the Makefile uses:

```
for d in lib lib/usb lib/input lib/fs lib/sched lib/net lib/sound addon/SDCard addon/fatfs addon/tftpfileserver; do make -C $d -j4 || break; done
(cd addon/wlan && ./makeall --nosample)           # libwlan.a and libwpa_supplicant.a
mkdir -p addon/wlan/sample/pisound-wifi-test
cp -a /path/to/NAMberry/. addon/wlan/sample/pisound-wifi-test/
cd addon/wlan/sample/pisound-wifi-test
cp config_local.h.example config_local.h          # put the address of your PC (syslog and OTA)
make -j4
```

## What goes on the SD card

Nothing of this is in the repository (licenses, see [NOTICE.md](../NOTICE.md)); it comes from Circle:

* **Boot files**: in Circle's `boot/` run `make` (downloads `start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`, ... with wget). Use `boot/config64.txt` as `config.txt`. The author's own `config.txt` of the Pi 5 was not checked for extra settings.
* **The kernel**: `kernel8-rpi4.img`.
* **Wi-Fi firmware**: in Circle's `addon/wlan/firmware/` run `make`, copy the files to a `firmware/` folder on the card as Circle's Wi-Fi samples do (the Pi 4 uses the 43455 files).
* **Wi-Fi network**: `wpa_supplicant.conf` in the card's root, in the format of `addon/wlan/sample/hello_wlan/wpa_supplicant.conf` (`ssid`, `psk`, optional `country`).
* **Your NAM models, captures and IRs**, as for the Pi 5.

The Pi 4 header has the same Pisound pins, but the Pisound has never been tried with this firmware on a Pi 4.
