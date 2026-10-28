# display/ - the Live Display

The firmware of the ESP32-S3 touch display. Everything about it (what it does, the hardware, the protocol, how to build and load it, the limits) is in [../docs/live-display.md](../docs/live-display.md).

```
nb_display/nb_display.ino   the firmware: Stage, knobs, editor, presets, NAM model picker, new preset, Wi-Fi setup, firmware update
nb_display/wifi_config.h    update host name and an optional update password (no Wi-Fi network or password here: they are typed on the display)
nb_display/User_Setup_NB.h  TFT_eSPI settings of the Freenove FNK0104B
bringup/display_probe/      pins and screen/touch initialization test, no libraries
bringup/nb_link_test/       ping/pong test of the USB link with the board
tools/fake_namberry.py      plays the board's side of the protocol on a PC (answers the display's first request with 109,806 bytes in 4096-byte blocks and times it)
```

Change `FW_NUM` at the top of `nb_display.ino` with every change: the screen and `http://<display>/` show it with the build time, which is how to tell that an update really arrived.
