// User_Setup_NB.h - TFT_eSPI settings of the two boards nb_display.ino runs on. The board is told by the compiler (ARDUINO_ESP32S3_DEV is defined when compiling for an ESP32-S3).
// Put this file in the TFT_eSPI library folder and, in TFT_eSPI's User_Setup_Select.h, replace the line  #include <User_Setup.h>  with  #include <User_Setup_NB.h>
// (every value was found on the real boards or taken from Freenove's setup file for the board: see docs/live-display.md).

#ifdef ARDUINO_ESP32S3_DEV
// ---- Freenove ESP32-S3 2.8" FNK0104B: ILI9341 240 x 320, capacitive touch (read over I2C by the sketch, not by TFT_eSPI)
#define USER_SETUP_INFO "NAMberry display S3"
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
#else
// ---- Freenove ESP32 CYD 4.0" FNK0114S: ST7796 320 x 480, resistive touch XPT2046 on the same SPI bus
#define USER_SETUP_INFO "NAMberry display CYD 4.0"
#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   27
#define TFT_BACKLIGHT_ON HIGH
#define TOUCH_CS 33			// chip select of the touch controller
#define USE_HSPI_PORT
#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
#endif

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
