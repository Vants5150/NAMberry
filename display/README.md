# display/ - the Live Display

The firmware of the ESP32 touch displays (Freenove ESP32-S3 2.8" FNK0104B and ESP32 CYD 4.0" FNK0114S, the compiler's board choice selects the code). Everything about them (what they do, the hardware, the protocol, how to build and load them, the limits) is in [../docs/live-display.md](../docs/live-display.md).

```
nb_display/nb_display.ino   the firmware: Stage, knobs, editor, presets, NAM model picker, new preset, Wi-Fi setup, firmware update, touch tracker
nb_display/wifi_config.h    update host name (one per board) and an optional update password (no Wi-Fi network or password here: they are typed on the display)
nb_display/User_Setup_NB.h  TFT_eSPI settings of both boards
bringup/display_probe/      pins and screen/touch initialization test of the S3, no libraries
bringup/nb_link_test/       ping/pong test of the USB link with the board (S3)
tools/fake_namberry.py      plays the board's side of the protocol on a PC (answers the display's first request with 109,806 bytes in 4096-byte blocks and times it)
tools/bridge_namberry.py    plays it and forwards the requests to the real board over its web interface (a display can be tried before the board has the link)
```

Change `FW_NUM` at the top of `nb_display.ino` with every change: the screen and `http://<display>/` show it with the build time, which is how to tell that an update really arrived.
