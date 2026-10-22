# Contributing

NAMberry is a one-person project that was developed and tested on a single Raspberry Pi 5 with a Pisound. Help is welcome, and the most useful kind is described here.

## Reporting a problem

Open a [GitHub issue](https://github.com/Vants5150/NAMberry/issues) and include:

* the firmware build shown on the web page (for example `mcore98`) and the hardware (Pi 5 model, Pisound, power supply);
* what you did, what you expected and what happened;
* the preset or block settings involved (export them from the page) and, for audio problems, whether a NAM model or impulse response was used (do not attach models you do not have the right to share);
* for a crash or a reboot, the syslog lines around it if you have the log server set up.

## Ideas and effects

Effects that model a real circuit are only as good as their sources. If you propose or send one, say where the schematic, article or measurements come from, and what is measured and what is assumed (see how [EFFECTS.md](EFFECTS.md) and `docs/presets_fonti.txt` do it).

## Code changes

* Build and tests: see "Building" and "Tests" in the [README](README.md). `tests/run_host_tests.sh` and `tests/run_page_tests.sh` must pass.
* Code comments, log text and the web page are written in English.
* Anything that touches real hardware paths (USB, DIN MIDI, the fan) ships switched off until it has been tried on a board.
* Never commit passwords, Wi-Fi names, the OTA key, personal addresses or `config_local.h`.
* No NAM models, captures or impulse responses in the repository unless their licence allows redistribution.

By contributing you agree that your contribution is released under the GPL-3.0, like the rest of the project.
