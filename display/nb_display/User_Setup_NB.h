// User_Setup_NB.h - TFT_eSPI settings for the Freenove ESP32-S3 2.8" display (FNK0104B, ILI9341 240x320, capacitive touch), as used by nb_display.ino.
// Put this file in the TFT_eSPI library folder and, in TFT_eSPI's User_Setup_Select.h, replace the line  #include <User_Setup.h>  with  #include <User_Setup_NB.h>
// (every value below was found on the real board: see docs/live-display.md).
#define USER_SETUP_INFO "NAMberry display"

#define ILI9341_2_DRIVER		// the alternative ILI9341 driver: the generic one left this panel black
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1			// shared with the board reset
#define TFT_BL   45			// backlight, HIGH = on
#define TFT_BACKLIGHT_ON HIGH

#define USE_HSPI_PORT			// the LCD works on HSPI, not on FSPI
#define SPI_FREQUENCY 40000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
