// nb_link_test.ino - first link test between the ESP32-S3 touch display (Freenove FNK0104B) and the NAMberry firmware over USB serial (CDC).
// The display sends "ping N" once a second; the NAMberry answers "pong N <ms>" (and says "NB hello <ms>" once a second by itself).
// Everything received is listed on the screen. Needs TFT_eSPI with Freenove's FNK0104AB setup (see the project notes) and the board package esp32 3.x.
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#define TP_SDA 16
#define TP_SCL 15
#define TP_RST 18
#define RGB_LED 42

static TFT_eSPI tft;
static const int MAXL = 15;
static String lines[MAXL];
static int nLines = 0;
static String cur;
static unsigned long rxLines = 0, txPings = 0, pongs = 0, lastRx = 0, lastTx = 0;
static bool dirty = true;

static void pushLine (const String &s)
{
	if (nLines < MAXL) lines[nLines++] = s;
	else { for (int i = 1; i < MAXL; i++) lines[i - 1] = lines[i]; lines[MAXL - 1] = s; }
	dirty = true;
}

static uint8_t i2cRead (uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n)
{
	Wire.beginTransmission (addr);
	Wire.write (reg);
	if (Wire.endTransmission (false) != 0) return 0;
	return Wire.requestFrom ((int) addr, (int) n) == n ? (Wire.readBytes (out, n), n) : 0;
}

static void draw ()
{
	const bool linked = lastRx != 0 && millis () - lastRx < 3000;
	tft.fillRect (0, 0, 240, 56, TFT_NAVY);
	tft.setTextColor (TFT_WHITE, TFT_NAVY);
	tft.drawString ("NAMberry link test", 6, 4, 2);
	tft.setTextColor (linked ? TFT_GREEN : TFT_ORANGE, TFT_NAVY);
	tft.drawString (linked ? "link: ANSWERING" : "link: waiting for the NAMberry", 6, 22, 2);
	tft.setTextColor (TFT_WHITE, TFT_NAVY);
	tft.drawString ("rx " + String (rxLines) + "  tx " + String (txPings) + "  pong " + String (pongs), 6, 38, 2);
	tft.fillRect (0, 56, 240, 264, TFT_BLACK);
	tft.setTextColor (TFT_CYAN, TFT_BLACK);
	for (int i = 0; i < nLines; i++) tft.drawString (lines[i].substring (0, 28), 4, 60 + i * 17, 2);
	dirty = false;
}

void setup ()
{
	Serial.begin (115200);
	neopixelWrite (RGB_LED, 0, 0, 0);
	pinMode (TP_RST, OUTPUT); digitalWrite (TP_RST, LOW); delay (20); digitalWrite (TP_RST, HIGH); delay (100);
	Wire.begin (TP_SDA, TP_SCL, 400000);
	tft.init ();
	tft.setRotation (0);
	tft.fillScreen (TFT_BLACK);
	pushLine ("(waiting for lines from the NAMberry)");
	draw ();
}

void loop ()
{
	while (Serial.available ())
	{
		const char c = (char) Serial.read ();
		if (c == '\n' || c == '\r')
		{
			if (cur.length ())
			{
				rxLines++; lastRx = millis ();
				if (cur.startsWith ("pong")) pongs++;
				if (nLines == 1 && lines[0].startsWith ("(waiting")) nLines = 0;
				pushLine (cur);
				cur = "";
			}
		}
		else if (cur.length () < 70) cur += c;
	}
	if (millis () - lastTx >= 1000)
	{
		lastTx = millis ();
		Serial.printf ("ping %lu\n", ++txPings);
		dirty = true;
	}
	// touch: a small white dot where the finger is (also confirms the touch while the link runs)
	uint8_t t[5];
	if (i2cRead (0x38, 0x02, t, 5) == 5 && (t[0] & 0x0F) == 1)
	{
		const int x = ((t[1] & 0x0F) << 8) | t[2], y = ((t[3] & 0x0F) << 8) | t[4];
		tft.fillCircle (x, y, 4, TFT_WHITE);
	}
	static unsigned long lastDraw = 0;
	if (dirty && millis () - lastDraw > 250) { lastDraw = millis (); draw (); }
	delay (5);
}
