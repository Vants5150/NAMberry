// display_probe.ino (v2) - checks the pins of the Freenove ESP32-S3 2.8" display (ILI9341V + FT6336G + ES8311) without any library.
// The pin numbers come from a community board package for the same design: this sketch tells whether they are right.
// v2: full ILI9341 init sequence, read-back of the controller registers (proves that the SPI writes arrive), and a loop that cycles
// backlight level and fill color every 4 s (the RGB LED shows the phase) so that a dark screen can be told from a missing image.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <stdarg.h>
#include <string.h>

#define LCD_CS   10
#define LCD_DC   46
#define LCD_SCK  12
#define LCD_MOSI 11
#define LCD_MISO 13
#define LCD_BL   45
#define TP_SDA   16
#define TP_SCL   15
#define TP_RST   18
#define TP_INT   17
#define RGB_LED  42

static SPIClass lcd (HSPI);		// Freenove's own TFT_eSPI setup defines USE_HSPI_PORT
static const int W = 240, H = 320;
static char g_rep[3500];

static void rp (const char *fmt, ...)
{
	char b[300];
	va_list ap; va_start (ap, fmt); vsnprintf (b, sizeof b, fmt, ap); va_end (ap);
	size_t n = strlen (g_rep);
	if (n + strlen (b) < sizeof g_rep - 1) strcat (g_rep, b);
	Serial.print (b);
}

static void lcdCmd (uint8_t c)
{
	digitalWrite (LCD_DC, LOW);
	digitalWrite (LCD_CS, LOW);
	lcd.transfer (c);
	digitalWrite (LCD_CS, HIGH);
}

static void lcdData (const uint8_t *d, size_t n)
{
	digitalWrite (LCD_DC, HIGH);
	digitalWrite (LCD_CS, LOW);
	lcd.writeBytes (d, n);
	digitalWrite (LCD_CS, HIGH);
}

static void lcdCmdData (uint8_t c, const uint8_t *d, size_t n)
{
	lcdCmd (c);
	if (n) lcdData (d, n);
}
#define CD(c, ...) do { const uint8_t _d[] = {__VA_ARGS__}; lcdCmdData (c, _d, sizeof _d); } while (0)

// reads n bytes after a command (first byte is a dummy); slow clock
static void lcdRead (uint8_t c, uint8_t *out, size_t n)
{
	lcd.endTransaction ();
	lcd.beginTransaction (SPISettings (4000000, SPI_MSBFIRST, SPI_MODE0));
	digitalWrite (LCD_DC, LOW);
	digitalWrite (LCD_CS, LOW);
	lcd.transfer (c);
	digitalWrite (LCD_DC, HIGH);
	for (size_t i = 0; i < n; i++) out[i] = lcd.transfer (0);
	digitalWrite (LCD_CS, HIGH);
	lcd.endTransaction ();
	lcd.beginTransaction (SPISettings (20000000, SPI_MSBFIRST, SPI_MODE0));
}

static void window (int x0, int y0, int x1, int y1)
{
	CD (0x2A, (uint8_t) (x0 >> 8), (uint8_t) x0, (uint8_t) (x1 >> 8), (uint8_t) x1);
	CD (0x2B, (uint8_t) (y0 >> 8), (uint8_t) y0, (uint8_t) (y1 >> 8), (uint8_t) y1);
	lcdCmd (0x2C);
}

static void fillRect (int x0, int y0, int x1, int y1, uint16_t color)
{
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
	if (x1 < x0 || y1 < y0) return;
	window (x0, y0, x1, y1);
	static uint8_t line[W * 2];
	for (int i = 0; i < W; i++) { line[2 * i] = color >> 8; line[2 * i + 1] = color & 0xFF; }
	digitalWrite (LCD_DC, HIGH);
	digitalWrite (LCD_CS, LOW);
	for (int y = y0; y <= y1; y++) lcd.writeBytes (line, (x1 - x0 + 1) * 2);
	digitalWrite (LCD_CS, HIGH);
}

static uint16_t rgb (uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

static uint8_t i2cRead (uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n)
{
	Wire.beginTransmission (addr);
	Wire.write (reg);
	if (Wire.endTransmission (false) != 0) return 0;
	return Wire.requestFrom ((int) addr, (int) n) == n ? (Wire.readBytes (out, n), n) : 0;
}

static void readback (const char *when)
{
	uint8_t r[6];
	rp ("-- LCD registers %s:\n", when);
	lcdRead (0x09, r, 5); rp ("   status 0x09        : %02X %02X %02X %02X %02X\n", r[0], r[1], r[2], r[3], r[4]);
	lcdRead (0x0A, r, 2); rp ("   power mode 0x0A    : %02X (second byte is the value)\n", r[1]);
	lcdRead (0x0B, r, 2); rp ("   MADCTL 0x0B        : %02X (written 08)\n", r[1]);
	lcdRead (0x0C, r, 2); rp ("   pixel format 0x0C  : %02X (written 55 -> reads 05)\n", r[1]);
	lcdRead (0x0D, r, 2); rp ("   image mode 0x0D    : %02X\n", r[1]);
}

static void lcdInit ()
{
	// the values of TFT_eSPI's "ILI9341_2_DRIVER" init (the one Freenove's setup file for this board selects), written from memory: the read-back below tells whether they were accepted
	lcdCmd (0x01); delay (150);				// software reset
	CD (0xCF, 0x00, 0xC1, 0x30);
	CD (0xED, 0x64, 0x03, 0x12, 0x81);
	CD (0xE8, 0x85, 0x00, 0x78);
	CD (0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);
	CD (0xF7, 0x20);
	CD (0xEA, 0x00, 0x00);
	CD (0xC0, 0x10);					// power control 1
	CD (0xC1, 0x00);					// power control 2
	CD (0xC5, 0x30, 0x30);				// VCOM 1
	CD (0xC7, 0xB7);					// VCOM 2
	CD (0x3A, 0x55);					// 16 bit pixels
	CD (0x36, 0x08);					// MADCTL: BGR, portrait
	CD (0xB1, 0x00, 0x1A);				// frame rate
	CD (0xB6, 0x0A, 0xA2);				// display function control
	CD (0xF2, 0x00);					// 3 gamma off
	CD (0x26, 0x01);					// gamma curve
	CD (0xE0, 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00);
	CD (0xE1, 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F);
	lcdCmd (0x11); delay (130);				// sleep out
	lcdCmd (0x21);					// TFT_INVERSION_ON in Freenove's setup
	lcdCmd (0x29); delay (30);				// display on
}

void setup ()
{
	Serial.begin (115200);
	delay (1500);
	rp ("%s", "\n=== display_probe v2: Freenove ESP32-S3 2.8 inch ===\n");
	pinMode (LCD_BL, OUTPUT); digitalWrite (LCD_BL, LOW);
	pinMode (LCD_CS, OUTPUT); digitalWrite (LCD_CS, HIGH);
	pinMode (LCD_DC, OUTPUT);
	neopixelWrite (RGB_LED, 0, 24, 0);

	// ---- touch and codec on the I2C bus
	pinMode (TP_RST, OUTPUT); digitalWrite (TP_RST, LOW); delay (20); digitalWrite (TP_RST, HIGH); delay (200);
	Wire.begin (TP_SDA, TP_SCL, 400000);
	rp ("I2C devices found at:");
	int found = 0;
	for (uint8_t a = 1; a < 127; a++)
	{
		Wire.beginTransmission (a);
		if (Wire.endTransmission () == 0) { rp (" 0x%02X", a); found++; }
	}
	rp ("  (%d)\n", found);
	uint8_t id[4] = {0};
	if (i2cRead (0x38, 0xA3, id, 1)) rp ("FT6336 chip id (0xA3) = 0x%02X (expected 0x64)\n", id[0]); else rp ("%s", "no answer at 0x38\n");

	// ---- LCD
	lcd.begin (LCD_SCK, LCD_MISO, LCD_MOSI, -1);
	lcd.beginTransaction (SPISettings (20000000, SPI_MSBFIRST, SPI_MODE0));
	readback ("before init");
	lcdInit ();
	readback ("after init");
	rp ("%s", "Loop phases (4 s each; RGB LED shows the phase): 1 red LED = BL high + red screen, 2 green LED = BL high + green, 3 blue LED = BL LOW + blue, 4 white LED = BL high + white\n");
}

void loop ()
{
	static uint32_t t0 = 0, last = 0, tp = 0;
	static int phase = -1;
	static bool wasDown = false;
	if (phase < 0 || millis () - tp > 4000)
	{
		tp = millis (); phase = (phase + 1) & 3;
		const uint16_t col[4] = {rgb (255, 0, 0), rgb (0, 255, 0), rgb (0, 0, 255), rgb (255, 255, 255)};
		const bool bl[4] = {true, true, false, true};
		digitalWrite (LCD_BL, bl[phase] ? HIGH : LOW);
		fillRect (0, 0, W - 1, H - 1, col[phase]);
		neopixelWrite (RGB_LED, phase == 0 || phase == 3 ? 40 : 0, phase == 1 || phase == 3 ? 40 : 0, phase == 2 || phase == 3 ? 40 : 0);
		Serial.printf ("phase %d: BL %s\n", phase + 1, bl[phase] ? "HIGH" : "LOW");
	}
	uint8_t t[7];
	if (i2cRead (0x38, 0x02, t, 5) == 5)
	{
		const int n = t[0] & 0x0F;
		if (n > 0 && n < 3)
		{
			const int x = ((t[1] & 0x0F) << 8) | t[2], y = ((t[3] & 0x0F) << 8) | t[4];
			if (!wasDown || millis () - last > 100)
			{
				Serial.printf ("touch x=%d y=%d (points %d)\n", x, y, n);
				fillRect (x - 5, y - 5, x + 5, y + 5, rgb (0, 0, 0));
				last = millis ();
			}
			wasDown = true;
		}
		else wasDown = false;
	}
	if (millis () - t0 > 8000) { t0 = millis (); Serial.println ("--- start-up report ---"); Serial.print (g_rep); Serial.println ("--- end ---"); }
	delay (10);
}
