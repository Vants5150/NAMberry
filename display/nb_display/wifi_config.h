// wifi_config.h - settings of the firmware update over Wi-Fi (ArduinoOTA). The Wi-Fi network itself and its password are NOT here:
// they are chosen and typed on the display (Cfg button) and kept in its flash.
#pragma once

// optional: a password for the firmware update itself (anyone on your network could flash the display without it). Leave the line commented to update without a password.
// #define OTA_PASSWORD "choose-one"

// the name on the network: each board has its own, so that two displays can be on the same network
#ifdef ARDUINO_ESP32S3_DEV
#define OTA_HOSTNAME "nb-display"
#else
#define OTA_HOSTNAME "nb-display-cyd"
#endif
