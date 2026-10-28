// nb_display.ino - touch display for the NAMberry (Freenove ESP32-S3 2.8" FNK0104B): shows the preset in use, its blocks and their parameters.
// Link to the NAMberry: USB serial (CDC), the line protocol of serialrpc.h ("Q <id> G /status" -> "A <id> <status> <length>" + body): the same JSON the web page uses.
// Updates: ArduinoOTA over Wi-Fi. The network is chosen and its password typed ON THE DISPLAY (Cfg button: scan, select, on-screen keyboard); it is kept in the flash (NVS).
// Libraries: TFT_eSPI with the settings of User_Setup_NB.h (next to this file), ArduinoJson 7. Board: esp32 core 3.x, ESP32-S3, 16 MB flash, OPI PSRAM, USB-CDC on boot, hardware CDC.
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include "wifi_config.h"

// one line of the parameter list; editable rows carry what the editor needs (key for the board, range, step, kind 0 number / 1 choice / 2 on-off).
// Declared here, before any function: the Arduino preprocessor puts the function prototypes above the first function.
struct Row
{
	String label; String value; uint16_t color; bool header;
	bool edit = false;
	String key, unit;
	float v = 0, mn = 0, mx = 0, st = 1;
	int kind = 0;
	JsonArray e;
};
struct FieldSpec { const char *type, *key; float mn, mx, st; int kind; };

// raise the number at every change: the screen shows it with the build time, so the firmware that runs is always known
#define FW_NUM "11.1"
#define FW_NAME "nb_display v" FW_NUM
#define FW_BUILD __DATE__ " " __TIME__

// ---------------------------------------------------------------- pins (verified on the board, see the project notes)
#define TP_SDA 16
#define TP_SCL 15
#define TP_RST 18
#define RGB_LED 42

static TFT_eSPI tftHw;
static TFT_eSPI *gfx = &tftHw;			// where the drawing goes: the screen, or the off-screen frame while a whole view is composed
#define tft (*gfx)

// ---------------------------------------------------------------- PSRAM allocator for the big JSON documents
struct SpiRamAllocator : ArduinoJson::Allocator
{
	void *allocate (size_t n) override { return ps_malloc (n); }
	void deallocate (void *p) override { free (p); }
	void *reallocate (void *p, size_t n) override { return ps_realloc (p, n); }
};
static SpiRamAllocator g_psAlloc;

// ---------------------------------------------------------------- serial link
static char *g_rx = nullptr;				// reply body buffer (PSRAM)
static const size_t RX_CAP = 200 * 1024;
static uint32_t g_reqId = 0;
static uint32_t g_lastAnswer = 0;
static String g_msg;
static String g_rxInfo;					// result of the last reply received (bytes and time), shown on the waiting screen
static bool g_fxOk = false;				// the effect definitions have been read
static volatile unsigned g_nDraw = 0, g_nFull = 0, g_nKnob = 0, g_nTile = 0, g_nDyn = 0, g_nPoll = 0;	// how often each drawing step ran (read on the status page)
static char g_lastFullView[96];		// a plain array: the status page reads it from another task
static void draw ();
static WebServer g_web (80);

static bool readLine (char *out, size_t cap, uint32_t timeoutMs)
{
	size_t n = 0;
	const uint32_t t0 = millis ();
	while (millis () - t0 < timeoutMs)
	{
		while (Serial.available ())
		{
			const int c = Serial.read ();
			if (c == '\n') { out[n] = 0; return true; }
			if (c != '\r' && n + 1 < cap) out[n++] = (char) c;
		}
		delay (1);
	}
	return false;
}

// sends a request and reads the reply body into g_rx. Returns the HTTP status (200 = ok) or -1 on a timeout / bad frame.
static int rpc (bool post, const char *path, const char *form, size_t *pLen, uint32_t timeoutMs = 4000)
{
	while (Serial.available ()) Serial.read ();		// stale bytes (hello lines)
	const uint32_t id = ++g_reqId;
	Serial.printf ("Q %lu %c %s %s\n", (unsigned long) id, post ? 'P' : 'G', path, form ? form : "");
	const uint32_t t0 = millis ();
	char line[96];
	for (;;)
	{
		const uint32_t used = millis () - t0;
		if (used >= timeoutMs) return -1;
		if (!readLine (line, sizeof line, timeoutMs - used)) return -1;
		unsigned rid, st, len;
		if (sscanf (line, "A %u %u %u", &rid, &st, &len) == 3 && rid == id)
		{
			if (len + 1 > RX_CAP) return -1;
			size_t got = 0;
			uint32_t last = millis (), tShow = 0;
			const uint32_t tBody = last;
			// flow control: the board sends 4096 bytes and waits for a 'K' before the next block (the USB receive queue of this chip drops bytes when it is full)
			while (got < len)
			{
				const size_t blockEnd = got + (len - got > 4096 ? 4096 : len - got);
				while (got < blockEnd)
				{
					const size_t n = Serial.read (g_rx + got, blockEnd - got);
					if (n > 0) { got += n; last = millis (); }
					else if (millis () - last > 3000) { g_msg = "body stalled " + String ((unsigned) got) + "/" + String (len); return -1; }
					else delay (1);
				}
				if (got < len) Serial.write ('K');
				if (!g_fxOk && millis () - tShow > 500)		// the progress is shown only while the effects are read the first time: later replies must not repaint the screen
				{
					tShow = millis ();
					g_msg = "rx " + String ((unsigned) got) + "/" + String (len) + " st " + String (st);
					draw ();
				}
			}
			g_rxInfo = "last: " + String ((unsigned) got) + " B in " + String ((unsigned) (millis () - tBody)) + " ms";
			g_rx[len] = 0;
			*pLen = len;
			g_lastAnswer = millis ();
			return (int) st;
		}
		// anything else ("NB hello", "pong", ...) is skipped
	}
}

// ---------------------------------------------------------------- data
static JsonDocument g_fx (&g_psAlloc);			// the effect definitions (GET /fx), loaded once
static JsonDocument g_st;				// the last /status
static bool g_stOk = false;
static String g_fwVer;

static JsonObject fxDef (const char *key)
{
	for (JsonObject d : g_fx.as<JsonArray> ())
		if (!strcmp (d["fx"] | "", key)) return d;
	return JsonObject ();
}

static bool loadFx ()
{
	size_t len;
	if (rpc (false, "/fx", "", &len, 15000) != 200) return false;
	g_fx.clear ();
	const DeserializationError e = deserializeJson (g_fx, g_rx, len);
	g_fxOk = !e && g_fx.is<JsonArray> () && g_fx.size () > 0;
	return g_fxOk;
}

static bool loadStatus ()
{
	size_t len;
	if (rpc (false, "/status", "", &len, 4000) != 200) return false;
	g_st.clear ();
	g_stOk = !deserializeJson (g_st, g_rx, len);
	if (g_stOk) g_fwVer = String (g_st["ver"] | "?");
	return g_stOk;
}

// ---------------------------------------------------------------- UI model
struct BlockRef { int id; int r, c; JsonObject b; };
static std::vector<BlockRef> g_blocks;
static String g_preset, g_blocksKey;

static String blockName (JsonObject b)
{
	const char *t = b["t"] | "";
	if (!strcmp (t, "fx"))
	{
		JsonObject d = fxDef (b["fx"] | "");
		return String (d.isNull () ? (b["fx"] | "?") : (d["title"] | "?"));
	}
	if (!strcmp (t, "nam")) return String ("NAM ") + (const char *) (b["name"] | "");
	if (!strcmp (t, "vol")) return b["rt"].isNull () ? "Volume" : "Routing";
	if (!strcmp (t, "pan")) return "Pan";
	return String (t);
}

static void buildBlocks ()
{
	g_blocks.clear ();
	for (JsonObject cell : g_st["board"]["net"]["cells"].as<JsonArray> ())
	{
		JsonObject b = cell["b"][0];
		if (b.isNull ()) continue;
		g_blocks.push_back ({b["id"] | -1, cell["r"] | 0, cell["c"] | 0, b});
	}
	std::sort (g_blocks.begin (), g_blocks.end (), [] (const BlockRef &a, const BlockRef &b) { return a.c != b.c ? a.c < b.c : a.r < b.r; });
	g_preset = String (g_st["pc"] | "?");
	if ((int) (g_st["pd"] | 0)) g_preset += " *";
}

// ---------------------------------------------------------------- parameters of one block as rows
static std::vector<Row> g_rows;
static std::vector<int> g_kn;				// indexes in g_rows of the editable rows (the knobs of the parameter screen)

// ---------------------------------------------------------------- NAM files of the library (GET /lib), for the file picker
static JsonDocument g_libDoc (&g_psAlloc);
static std::vector<int> g_fileIdx;			// indexes in g_libDoc["lib"] of the files that match the filter
static String g_fileFilter;
static int g_kbMode = 0;				// what the keyboard edits: 0 the Wi-Fi password, 1 the file filter

static void buildFileIdx ()
{
	g_fileIdx.clear ();
	String f = g_fileFilter;
	f.toLowerCase ();
	JsonArray lib = g_libDoc["lib"];
	for (int i = 0; i < (int) lib.size (); i++)
	{
		String n = String ((const char *) (lib[i] | ""));
		n.toLowerCase ();
		if (f.length () == 0 || n.indexOf (f) >= 0) g_fileIdx.push_back (i);
	}
}

static bool loadLib ()
{
	size_t len;
	if (rpc (false, "/lib", "", &len, 6000) != 200) return false;
	g_libDoc.clear ();
	if (deserializeJson (g_libDoc, g_rx, len) || !g_libDoc["lib"].is<JsonArray> ()) return false;
	buildFileIdx ();
	return true;
}

static String fmtNum (const Row &r, float v)
{
	if (r.kind == 1)
	{
		const int i = (int) lroundf (v);
		if (i >= 0 && i < (int) r.e.size ()) return String ((const char *) r.e[i]);
	}
	if (r.kind == 2) return v > 0.5f ? "ON" : "OFF";
	const int dec = r.st >= 1.0f ? 0 : (r.st >= 0.1f ? 1 : 2);
	String s = String (v, dec);
	if (r.unit.length ()) { s += " "; s += r.unit; }
	return s;
}

// editable fields of the blocks that are not effects (the board's SetValue keys)
static const FieldSpec FIELDS[] = {
	{"nam", "in", -24, 12, 0.5f, 0}, {"nam", "out", -24, 12, 0.5f, 0}, {"nam", "bass", -12, 12, 0.5f, 0}, {"nam", "mid", -12, 12, 0.5f, 0},
	{"nam", "treb", -12, 12, 0.5f, 0}, {"nam", "noofs", 0, 1, 1, 2}, {"vol", "vol", -40, 12, 0.5f, 0}, {"pan", "pan", -100, 100, 5, 0},
};

static String fmtVal (JsonObject p, float v)
{
	const int kind = p["kind"] | 0;
	if (kind == 1)
	{
		JsonArray e = p["e"];
		const int i = (int) lroundf (v);
		if (i >= 0 && i < (int) e.size ()) return String ((const char *) e[i]);
	}
	if (kind == 2) return v > 0.5f ? "ON" : "OFF";
	const float step = p["step"] | 1.0f;
	const int dec = step >= 1.0f ? 0 : (step >= 0.1f ? 1 : 2);
	String s = String (v, dec);
	const char *u = p["u"] | "";
	if (u[0]) { s += " "; s += u; }
	return s;
}

static void buildRowsRaw (JsonObject b)
{
	g_rows.clear ();
	const char *t = b["t"] | "";
	if (!strcmp (t, "fx"))
	{
		JsonObject d = fxDef (b["fx"] | "");
		if (d.isNull ()) { g_rows.push_back ({"unknown effect", "", TFT_RED, false}); return; }
		JsonArray groups = d["g"];
		JsonArray vals = b["v"];
		int lastGr = -1, i = 0;
		for (JsonObject p : d["p"].as<JsonArray> ())
		{
			const int gr = p["gr"] | 0;
			const int kind = p["kind"] | 0;
			if (kind != 3)
			{
				if (gr != lastGr && groups.size () > 1)
				{
					g_rows.push_back ({String ((const char *) (groups[gr] | "")), "", TFT_ORANGE, true});
					lastGr = gr;
				}
				const float v = vals[i] | 0.0f;
				Row r = {String ((const char *) (p["l"] | "?")), fmtVal (p, v), TFT_WHITE, false};
				r.edit = true;
				r.key = String ((const char *) (p["k"] | ""));
				r.unit = String ((const char *) (p["u"] | ""));
				r.v = v;
				r.kind = kind;
				r.e = p["e"];
				r.st = p["step"] | 1.0f;
				r.mn = p["min"] | 0.0f;
				r.mx = p["max"] | 1.0f;
				if (kind == 1) { r.mn = 0; r.mx = r.e.size () > 0 ? (float) r.e.size () - 1 : 0; r.st = 1; }
				if (kind == 2) { r.mn = 0; r.mx = 1; r.st = 1; }
				g_rows.push_back (r);
			}
			i++;
		}
		return;
	}
	// other blocks: show their fields as they come
	for (JsonPair kv : b)
	{
		const char *k = kv.key ().c_str ();
		if (!strcmp (k, "id") || !strcmp (k, "t") || !strcmp (k, "on")) continue;
		const FieldSpec *fs = nullptr;
		for (const FieldSpec &f : FIELDS) if (!strcmp (f.type, t) && !strcmp (f.key, k)) fs = &f;
		if (fs)
		{
			Row r = {String (k), "", TFT_WHITE, false};
			r.edit = true; r.key = String (k); r.v = kv.value ().as<float> ();
			r.mn = fs->mn; r.mx = fs->mx; r.st = fs->st; r.kind = fs->kind;
			r.unit = (fs->kind == 2 || !strcmp (t, "pan")) ? "" : "dB";
			r.value = fmtNum (r, r.v);
			g_rows.push_back (r);
			continue;
		}
		String v;
		if (kv.value ().is<float> () || kv.value ().is<int> ()) v = String (kv.value ().as<float> (), 1);
		else if (kv.value ().is<bool> ()) v = kv.value ().as<bool> () ? "yes" : "no";
		else if (kv.value ().is<const char *> ()) v = String (kv.value ().as<const char *> ());
		else continue;
		g_rows.push_back ({String (k), v, TFT_LIGHTGREY, false});
	}
}

static void buildRows (JsonObject b)
{
	buildRowsRaw (b);
	g_kn.clear ();
	for (int i = 0; i < (int) g_rows.size (); i++)
		if (g_rows[i].edit) g_kn.push_back (i);
}

// ---------------------------------------------------------------- commands to the board (same requests as the web page: POST /g op=set, POST /pre op=load)
static uint32_t g_tStatus = 0;				// time of the last /status; set to 0 to read it again at once

static String urlEnc (const String &s)
{
	String o;
	for (size_t i = 0; i < s.length (); i++)
	{
		const char c = s[i];
		if (isalnum ((unsigned char) c) || c == '-' || c == '_' || c == '.') o += c;
		else { char b[4]; snprintf (b, sizeof b, "%%%02X", (unsigned char) c); o += b; }
	}
	return o;
}

// sends a POST and checks the {"ok":1,...} answer; on a refusal the board's message goes to g_msg
static bool boardPost (const char *path, const String &form)
{
	size_t len;
	const int st = rpc (true, path, form.c_str (), &len, 3000);
	if (st != 200) { g_msg = "no answer from the board"; return false; }
	if (strstr (g_rx, "\"ok\":1")) { g_msg = ""; g_tStatus = 0; return true; }
	const char *e = strstr (g_rx, "\"err\":\"");
	g_msg = e ? String (e + 7).substring (0, 40) : String ("refused");
	const int q = g_msg.indexOf ('"');
	if (q >= 0) g_msg = g_msg.substring (0, q);
	return false;
}

static bool boardSet (int id, const String &key, float v)
{
	return boardPost ("/g", "op=set&id=" + String (id) + "&k=" + urlEnc (key) + "&v=" + String (v, 3));
}

// ---------------------------------------------------------------- new simple preset: one lane, an input, up to four blocks, an output
struct NewBlock { String key, title; };
static NewBlock g_nb[4];				// the blocks chosen (empty key = slot not used)
static int g_npIn = 0, g_npOut = 0;			// input 0 = L, 1 = R; output 0 = L, 1 = R, 2 = L + R
static int g_npSlot = 0;				// the slot whose type is being chosen
static std::vector<NewBlock> g_types;			// the blocks that can be chosen: ("" = empty slot), "nam", then the effects
static std::vector<int> g_typeIdx;			// indexes in g_types of those matching the filter
static String g_typeFilter;

static void buildTypeIdx ()
{
	g_typeIdx.clear ();
	String f = g_typeFilter;
	f.toLowerCase ();
	for (int i = 0; i < (int) g_types.size (); i++)
	{
		String t = g_types[i].title;
		t.toLowerCase ();
		if (f.length () == 0 || i == 0 || t.indexOf (f) >= 0) g_typeIdx.push_back (i);
	}
}

static void buildTypes ()
{
	g_types.clear ();
	g_types.push_back ({"", "(empty slot)"});
	g_types.push_back ({"nam", "NAM model"});
	std::vector<NewBlock> fx;
	for (JsonObject d : g_fx.as<JsonArray> ())
	{
		const String key = String ((const char *) (d["fx"] | ""));
		if ((int) (d["hid"] | 0) || key == "selector" || key == "adelsendsweep" || key == "") continue;		// old versions, the routing selector and the probe are not for a simple preset
		fx.push_back ({key, String ((const char *) (d["title"] | key.c_str ()))});
	}
	std::sort (fx.begin (), fx.end (), [] (const NewBlock &a, const NewBlock &b) { String x = a.title, y = b.title; x.toLowerCase (); y.toLowerCase (); return x < y; });
	for (const NewBlock &n : fx) g_types.push_back (n);
	g_typeFilter = "";
	buildTypeIdx ();
}

// the first free name "New 1", "New 2"...
static String npName ()
{
	JsonArray pre = g_st["pre"];
	for (int n = 1; n < 100; n++)
	{
		const String c = "New " + String (n);
		bool used = false;
		for (JsonVariant p : pre) if (c == String ((const char *) (p | ""))) used = true;
		if (!used) return c;
	}
	return "New";
}

static int lastNewId ()
{
	const char *p = strstr (g_rx, "\"id\":");
	return p ? atoi (p + 5) : -1;
}

// Builds the preset on the board with the same requests the web page sends, then saves it. The board changes while this runs (a second at most).
// Returns false with the reason in g_msg.
static bool npRun ()
{
	// 1. the current blocks and the direct links input -> output go away
	for (const BlockRef &b : g_blocks)
		if (!boardPost ("/g", "op=del&id=" + String (b.id))) return false;
	for (JsonArray lk : g_st["board"]["net"]["links"].as<JsonArray> ())
		if ((int) lk[0] < 0 && (int) lk[1] < 0) boardPost ("/g", "op=ul&fr=" + String ((int) lk[0]) + "&to=" + String ((int) lk[1]) + "&pf=" + String ((int) lk[2]) + "&pt=" + String ((int) lk[3]));
	// 2. input and output cells: Input L (2,0), Input R (3,0), Output L (2,9), Output R (3,9); the blocks are in row 2, columns 2 to 5
	boardPost ("/g", "op=mvio&w=0&r=2&p=0");
	boardPost ("/g", "op=mvio&w=1&r=3&p=0");
	boardPost ("/g", "op=mvio&w=2&r=2&p=9");
	boardPost ("/g", "op=mvio&w=3&r=3&p=9");
	// 3. the blocks, then the links in a chain
	int prev = -1, pf = g_npIn == 0 ? 1 : 2, n = 0;
	for (int i = 0; i < 4; i++)
	{
		if (g_nb[i].key.length () == 0) continue;
		if (!boardPost ("/g", "op=add&k=" + g_nb[i].key + "&r=2&p=" + String (2 + n))) return false;
		const int id = lastNewId ();
		if (id < 0) { g_msg = "no block id"; return false; }
		if (!boardPost ("/g", "op=lk&fr=" + String (prev) + "&to=" + String (id) + "&pf=" + String (pf) + "&pt=0&v=0")) return false;
		prev = id; pf = 0; n++;
	}
	if (n == 0) { g_msg = "choose at least one block"; return false; }
	if (g_npOut != 1 && !boardPost ("/g", "op=lk&fr=" + String (prev) + "&to=-2&pf=0&pt=1&v=0")) return false;
	if (g_npOut != 0 && !boardPost ("/g", "op=lk&fr=" + String (prev) + "&to=-2&pf=0&pt=2&v=0")) return false;
	// 4. saved with the first free name (it waits if the board is still busy with another preset operation)
	const String name = npName ();
	for (int t = 0; t < 12; t++)
	{
		if (boardPost ("/pre", "op=save&n=" + urlEnc (name))) return true;
		if (g_msg.indexOf ("progress") < 0) return false;
		delay (150);
	}
	return false;
}

// ---------------------------------------------------------------- Wi-Fi + OTA
static Preferences g_prefs;
static String g_ssid, g_pass;			// the saved network (NVS, namespace "nbd")
static bool g_wifiWanted = false, g_otaStarted = false, g_otaBusy = false;
static String g_wifiMsg = "Wi-Fi: not set (Cfg)";

static void drawOtaProgress (unsigned pct)
{
	tft.fillRect (0, 120, tft.width (), 80, TFT_BLACK);
	tft.setTextColor (TFT_WHITE, TFT_BLACK);
	tft.drawString ("Updating the firmware", 20, 125, 2);
	tft.drawRect (20, 160, 200, 20, TFT_WHITE);
	tft.fillRect (22, 162, (196 * pct) / 100, 16, TFT_GREEN);
}

static void wifiConnect (const String &ssid, const String &pass)
{
	g_wifiWanted = true;
	WiFi.mode (WIFI_STA);
	WiFi.setSleep (false);				// no modem power saving: its latency made the firmware updates fail
	WiFi.setHostname (OTA_HOSTNAME);
	WiFi.begin (ssid.c_str (), pass.c_str ());
	g_wifiMsg = "Wi-Fi: connecting";
}

static void wifiLoadSaved ()
{
	g_prefs.begin ("nbd", true);
	g_ssid = g_prefs.getString ("ssid", "");
	g_pass = g_prefs.getString ("pass", "");
	g_prefs.end ();
	if (g_ssid.length ()) wifiConnect (g_ssid, g_pass);
}

static void wifiSave (const String &ssid, const String &pass)
{
	g_prefs.begin ("nbd", false);
	g_prefs.putString ("ssid", ssid);
	g_prefs.putString ("pass", pass);
	g_prefs.end ();
	g_ssid = ssid; g_pass = pass;
}

static void wifiForget ()
{
	g_prefs.begin ("nbd", false);
	g_prefs.clear ();
	g_prefs.end ();
	g_ssid = ""; g_pass = "";
	g_wifiWanted = false;
	if (g_otaStarted) { ArduinoOTA.end (); g_otaStarted = false; }
	WiFi.disconnect (true, true);
	g_wifiMsg = "Wi-Fi: not set (Cfg)";
}

static volatile unsigned g_otaPct = 0;

// The firmware update (ArduinoOTA) and the status page run in their own task, so they answer even when the main loop is busy on the serial link or the screen.
// The task never draws: it only sets g_otaBusy / g_otaPct, the main loop shows the progress.
static void netTask (void *)
{
	for (;;)
	{
		if (g_wifiWanted && WiFi.status () == WL_CONNECTED)
		{
			if (!g_otaStarted)
			{
				ArduinoOTA.setHostname (OTA_HOSTNAME);
#ifdef OTA_PASSWORD
				ArduinoOTA.setPassword (OTA_PASSWORD);
#endif
				ArduinoOTA.setTimeout (15000);		// default 1000 ms: an update was aborted by any Wi-Fi hiccup longer than one second
				ArduinoOTA.onStart ([] () { g_otaPct = 0; g_otaBusy = true; });
				ArduinoOTA.onProgress ([] (unsigned done, unsigned total) { g_otaPct = total ? (done * 100ULL) / total : 0; });
				ArduinoOTA.onEnd ([] () { g_otaPct = 100; });
				ArduinoOTA.onError ([] (ota_error_t) { g_otaBusy = false; });
				ArduinoOTA.begin ();
				g_web.on ("/", [] ()
				{
					// plain text status, read over Wi-Fi to know which firmware really runs and how the serial link is doing
					String s = String ("nb_display ") + FW_NUM + "\nbuild " + FW_BUILD + "\nup_ms " + String ((unsigned long) millis ()) + "\nfx_ok " + String (g_fxOk ? 1 : 0)
						+ "\nmsg " + g_msg + "\nrx " + g_rxInfo + "\nreq_id " + String ((unsigned long) g_reqId)
						+ "\ndraw " + String (g_nDraw) + " full " + String (g_nFull) + " knob " + String (g_nKnob) + " tile " + String (g_nTile) + " dyn " + String (g_nDyn) + " poll " + String (g_nPoll)
						+ "\nview " + g_lastFullView + "\n";
					g_web.send (200, "text/plain", s);
				});
				g_web.begin ();
				g_otaStarted = true;
			}
			ArduinoOTA.handle ();
			g_web.handleClient ();
		}
		vTaskDelay (pdMS_TO_TICKS (5));
	}
}

static void wifiLoop ()
{
	if (!g_wifiWanted) return;
	g_wifiMsg = WiFi.status () == WL_CONNECTED ? "Wi-Fi: " + WiFi.localIP ().toString () : String ("Wi-Fi: not connected");
}

// ---------------------------------------------------------------- touch
static int g_rot = 0;					// screen rotation 0..3 as TFT_eSPI setRotation (0/2 portrait, 1/3 landscape), saved in NVS namespace "nbui"

// the touch panel reports the position in the portrait frame (x 0..239, y 0..319); this gives the position in the frame of the current rotation
// (rotation 1 = MADCTL MV, 3 = MX|MY|MV, 2 = MY, as in TFT_eSPI's ILI9341 driver; the portrait one is verified, the landscape ones follow from the same table)
static void mapTouch (int rx, int ry, int *px, int *py)
{
	switch (g_rot)
	{
	case 1: *px = ry; *py = 239 - rx; break;
	case 2: *px = 239 - rx; *py = 319 - ry; break;
	case 3: *px = 319 - ry; *py = rx; break;
	default: *px = rx; *py = ry; break;
	}
}
static uint8_t i2cRead (uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n)
{
	Wire.beginTransmission (addr);
	Wire.write (reg);
	if (Wire.endTransmission (false) != 0) return 0;
	return Wire.requestFrom ((int) addr, (int) n) == n ? (Wire.readBytes (out, n), n) : 0;
}

// returns true once for every new touch (the first sample of a press)
static bool touchPress (int *px, int *py)
{
	static bool down = false;
	uint8_t t[5];
	if (i2cRead (0x38, 0x02, t, 5) == 5 && (t[0] & 0x0F) == 1)
	{
		mapTouch (((t[1] & 0x0F) << 8) | t[2], ((t[3] & 0x0F) << 8) | t[4], px, py);
		if (!down) { down = true; return true; }
		return false;
	}
	down = false;
	return false;
}

// true while a finger is on the screen (for the slider)
static bool touchNow (int *px, int *py)
{
	uint8_t t[5];
	if (i2cRead (0x38, 0x02, t, 5) == 5 && (t[0] & 0x0F) == 1)
	{
		mapTouch (((t[1] & 0x0F) << 8) | t[2], ((t[3] & 0x0F) << 8) | t[4], px, py);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- screens
enum Screen { SCR_PARAMS, SCR_CFG, SCR_SCAN, SCR_PASS, SCR_BUSY, SCR_EDIT, SCR_PRESETS, SCR_STAGE, SCR_FILES, SCR_NEWP, SCR_TYPES };
static Screen g_scr = SCR_STAGE;				// the stage screen is the home screen
static int g_edRow = -1;				// the row being edited (index in g_rows)
static float g_edStart = 0;				// its value when the editor was opened (for Undo)
static String g_sig;					// what the data screen shows: it is redrawn only when this changes (no flicker)
// layout: set by applyLayout () for the rotation (0/2 portrait 240x320, 1/3 landscape 320x240)
static int SW = 240, SH = 320, ROWS = 6, FOOT_Y = 284;
static int SL_X = 10, SL_W = 220, SL_Y = 100, SL_H = 40;		// slider of the editor
static int EVAL_Y = 52, EBTN_Y = 170, EBTN_H = 56, EMSG_Y = 250, EBOOL_Y = 110, EST_Y = 80;	// editor: value, - / + buttons, message, on/off button
static int KB_Y = 96, KEY_W = 24, KEY_H = 34, PW_Y = 44, PW_H = 30, PWMSG_Y = 78;	// password screen
static int BTN_X = 10, BTN_W = 220, CFG_Y0 = 154, CFG_DY = 36;			// Cfg screen buttons
static void applyLayout ()
{
	if (g_rot & 1)
	{
		SW = 320; SH = 240; ROWS = 4; FOOT_Y = 204;
		SL_X = 10; SL_W = 300; SL_Y = 78; SL_H = 30;
		EVAL_Y = 42; EBTN_Y = 146; EBTN_H = 40; EMSG_Y = 188; EBOOL_Y = 90; EST_Y = 122;
		KB_Y = 74; KEY_W = 32; KEY_H = 28; PW_Y = 40; PW_H = 22; PWMSG_Y = 64;
		BTN_X = 172; BTN_W = 140; CFG_Y0 = 44; CFG_DY = 38;
	}
	else
	{
		SW = 240; SH = 320; ROWS = 6; FOOT_Y = 284;
		SL_X = 10; SL_W = 220; SL_Y = 100; SL_H = 40;
		EVAL_Y = 52; EBTN_Y = 170; EBTN_H = 56; EMSG_Y = 250; EBOOL_Y = 110; EST_Y = 80;
		KB_Y = 96; KEY_W = 24; KEY_H = 34; PW_Y = 44; PW_H = 30; PWMSG_Y = 78;
		BTN_X = 10; BTN_W = 220; CFG_Y0 = 154; CFG_DY = 36;
	}
}
static int g_page = 0, g_sel = -1;
static const int ROW_H = 40, ROW_Y = 40;
static bool g_dirty = true;

// Wi-Fi scan results
struct Net { String ssid; int rssi; bool open; };
static std::vector<Net> g_nets;
static bool g_scanning = false;

// password entry
static String g_pwSsid, g_pw;
static bool g_pwOpen = false, g_pwShow = false;
static int g_kbLayer = 0;				// 0 lower case, 1 upper case, 2 symbols
static const char *KB[3][4] = {
	{"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"},
	{"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
	{"!@#$%^&*()", "-_=+[]{};:", "'\",.?/\\|~`", "<>"}
};

// connection in progress
static uint32_t g_connT0 = 0;
static String g_connSsid, g_connPass;

static void drawHeader (const String &title, const String &sub)
{
	tft.fillRect (0, 0, SW, 38, TFT_NAVY);
	tft.setTextColor (TFT_WHITE, TFT_NAVY);
	tft.drawString (title.substring (0, 26), 4, 3, 2);
	tft.setTextColor (TFT_CYAN, TFT_NAVY);
	tft.drawString (sub.substring (0, 34), 4, 21, 1);
}

// four buttons of 60 px at the bottom: zone 0 (left), 1 (middle), 2, 3
static void drawFooter (const String &b0, const String &b1, const String &b2, const String &b3)
{
	tft.fillRect (0, FOOT_Y, SW, 36, TFT_DARKGREY);
	tft.setTextColor (TFT_WHITE, TFT_DARKGREY);
	const String *b[4] = {&b0, &b1, &b2, &b3};
	for (int i = 0; i < 4; i++) tft.drawString (*b[i], 6 + i * (SW / 4), FOOT_Y + 11, 2);
}

// x / w default to the column of the Cfg buttons
static void drawButton (int y, const String &label, uint16_t color, int x = -1, int w = -1)
{
	if (x < 0) { x = BTN_X; w = BTN_W; }
	tft.fillRoundRect (x, y, w, 34, 6, color);
	tft.setTextColor (TFT_WHITE, color);
	tft.drawString (label, x + 10, y + 9, 2);
}

static void drawKey (int x, int y, int w, const String &s, uint16_t color = 0x2945)
{
	tft.fillRoundRect (x + 1, y + 1, w - 2, KEY_H - 2, 4, color);
	tft.setTextColor (TFT_WHITE, color);
	tft.drawString (s, x + (w - tft.textWidth (s, 2)) / 2, y + (KEY_H - 16) / 2, 2);
}

// x of the edges of the bottom row of the keyboard, given for the 240 px wide layout
static int kbEdge (int x240) { return x240 * SW / 240; }

static void drawKeyboard ()
{
	for (int r = 0; r < 4; r++)
	{
		const char *row = KB[g_kbLayer][r];
		const int n = (int) strlen (row);
		const int off = (SW - n * KEY_W) / 2;
		for (int i = 0; i < n; i++) drawKey (off + i * KEY_W, KB_Y + r * KEY_H, KEY_W, String (row[i]));
	}
	const int y = KB_Y + 4 * KEY_H;
	const int e1 = kbEdge (36), e2 = kbEdge (72), e3 = kbEdge (156), e4 = kbEdge (192);
	drawKey (0, y, e1, g_kbLayer == 1 ? "ABC" : "abc", g_kbLayer == 2 ? 0x2945 : TFT_DARKGREEN);
	drawKey (e1, y, e2 - e1, g_kbLayer == 2 ? "abc" : "123", TFT_DARKGREEN);
	drawKey (e2, y, e3 - e2, "space");
	drawKey (e3, y, e4 - e3, "<-", TFT_MAROON);
	drawKey (e4, y, SW - e4, "OK", TFT_DARKGREEN);
}

// ---------------------------------------------------------------- stage / chain / knobs
static int g_stagePg = 0;				// page of the four switches of the stage screen
static uint16_t rgb (int r, int g, int b) { return tft.color565 (r, g, b); }

// 0 NAM, 1 dynamics / tone, 2 modulation, 3 time (delay, reverb), by the key of the effect
static int catOfKey (const String &key)
{
	if (key == "nam") return 0;
	String f = key;
	f.toLowerCase ();
	if (f.indexOf ("delay") >= 0 || f.indexOf ("verb") >= 0 || f.indexOf ("echo") >= 0 || f.indexOf ("spring") >= 0 || f.indexOf ("dm2") >= 0 || f == "ad9") return 3;
	if (f.indexOf ("chorus") >= 0 || f.indexOf ("phase") >= 0 || f.indexOf ("flang") >= 0 || f.indexOf ("trem") >= 0 || f.indexOf ("vibe") >= 0
	    || f.indexOf ("secondhand") >= 0 || f.indexOf ("dimension") >= 0 || f.indexOf ("pitch") >= 0 || f.indexOf ("harm") >= 0 || f.indexOf ("shifter") >= 0
	    || f == "ce1" || f == "ce2" || f == "rotary") return 2;
	return 1;
}

static int blockCat (JsonObject b)
{
	if (!strcmp (b["t"] | "", "nam")) return 0;
	return catOfKey (String ((const char *) (b["fx"] | "")));
}

static void catColors (int cat, bool on, uint16_t *bg, uint16_t *fg, uint16_t *sub)
{
	static const int C[4][3][3] = {{{15, 110, 86}, {255, 255, 255}, {93, 202, 165}}, {{24, 95, 165}, {255, 255, 255}, {133, 183, 235}},
				       {{83, 74, 183}, {255, 255, 255}, {175, 169, 236}}, {{133, 79, 11}, {255, 255, 255}, {250, 199, 117}}};
	if (!on) { *bg = rgb (35, 39, 45); *fg = rgb (138, 144, 153); *sub = rgb (107, 112, 120); return; }
	*bg = rgb (C[cat][0][0], C[cat][0][1], C[cat][0][2]);
	*fg = rgb (C[cat][1][0], C[cat][1][1], C[cat][1][2]);
	*sub = rgb (C[cat][2][0], C[cat][2][1], C[cat][2][2]);
}

static String shortName (JsonObject b)
{
	if (!strcmp (b["t"] | "", "nam")) return String ((const char *) (b["name"] | "NAM"));
	return blockName (b);
}

// splits a name in two lines of at most w characters, at a space when there is one
static void splitName (const String &s, int w, String *l1, String *l2)
{
	if ((int) s.length () <= w) { *l1 = s; *l2 = ""; return; }
	int cut = s.lastIndexOf (' ', w);
	if (cut < 3) cut = w;
	*l1 = s.substring (0, cut);
	String rest = s.substring (cut);
	rest.trim ();
	*l2 = rest.substring (0, w);
}

// the main value of a block, shown on its tile
static String tileValue (JsonObject b)
{
	const char *t = b["t"] | "";
	if (!strcmp (t, "fx"))
	{
		JsonObject d = fxDef (b["fx"] | "");
		JsonArray vals = b["v"];
		int i = 0;
		for (JsonObject p : d["p"].as<JsonArray> ())
		{
			if ((p["kind"] | 0) != 3) return fmtVal (p, vals[i] | 0.0f);
			i++;
		}
		return "";
	}
	if (!strcmp (t, "nam")) return String ("out ") + String (b["out"] | 0.0f, 1) + " dB";
	if (!strcmp (t, "vol")) return String (b["vol"] | 0.0f, 1) + " dB";
	if (!strcmp (t, "pan")) return String (b["pan"] | 0.0f, 0);
	return "";
}

static void drawLed (int x, int y, bool on)
{
	tft.fillCircle (x, y, 5, on ? rgb (94, 224, 138) : rgb (85, 91, 99));
}

// one block as a coloured tile: LED, name on two lines, main value
static void drawTile (int x, int y, int w, int h, JsonObject b, bool withValue)
{
	g_nTile++;
	const bool on = (int) (b["on"] | 1) != 0;
	uint16_t bg, fg, sub;
	catColors (blockCat (b), on, &bg, &fg, &sub);
	tft.fillRoundRect (x, y, w, h, 7, bg);
	if (!on) tft.drawRoundRect (x, y, w, h, 7, rgb (74, 79, 87));
	drawLed (x + w - 9, y + 9, on);
	String l1, l2;
	splitName (shortName (b), w / 8 - 1, &l1, &l2);
	tft.setTextColor (fg, bg);
	tft.drawString (l1, x + 5, y + 5, 2);
	if (l2.length ()) tft.drawString (l2, x + 5, y + 21, 2);
	if (withValue)
	{
		tft.setTextColor (sub, bg);
		tft.drawString (tileValue (b).substring (0, w / 6), x + 5, y + h - 14, 1);
	}
}

// geometry of the tile grid of the chain screen
static int chainCols () { return (g_rot & 1) ? 4 : 3; }
static int chainRows () { return (g_rot & 1) ? 2 : 4; }
static void chainCell (int i, int *x, int *y, int *w, int *h)
{
	const int cols = chainCols (), rows = chainRows ();
	*w = (SW - 24 - (cols - 1) * 6) / cols;
	*h = (FOOT_Y - ROW_Y - 6 - (rows - 1) * 6) / rows;
	*x = 12 + (i % cols) * (*w + 6);
	*y = ROW_Y + 2 + (i / cols) * (*h + 6);
}

static int ppmToPx (int ppm, int h)
{
	if (ppm <= 0) return 0;
	float f = (20.0f * log10f ((float) ppm / 1e6f) + 60.0f) / 60.0f;		// -60 dBFS .. 0 dBFS
	f = f < 0 ? 0 : (f > 1 ? 1 : f);
	return (int) (f * h);
}

static int g_mpx[2] = {-1, -1};				// the height in pixels of the two level bars as drawn (-1 = not drawn yet)

// paints the part of a level bar between two heights with the colour of its zone (green, yellow from 75%, red from 92%)
static void meterZone (int x, int y1, int h, int from, int to)
{
	const int z1 = (int) (h * 0.75f), z2 = (int) (h * 0.92f);
	for (int a = from; a < to;)
	{
		const int zEnd = a < z1 ? z1 : (a < z2 ? z2 : h);
		const int b = to < zEnd ? to : zEnd;
		tft.fillRect (x, y1 - b, 6, b - a, a < z1 ? rgb (94, 224, 138) : (a < z2 ? (uint16_t) TFT_YELLOW : (uint16_t) TFT_RED));
		a = b;
	}
}

// the two level bars of the stage screen (peak of the outputs); only the part that changed is painted, so they do not flicker
static void drawMeters (bool force)
{
	if (!g_stOk) return;
	const int y0 = ROW_Y + 4, y1 = FOOT_Y - 6, h = y1 - y0;
	for (int side = 0; side < 2; side++)
	{
		const int x = side == 0 ? 2 : SW - 8;
		const int px = ppmToPx (g_st[side == 0 ? "outL" : "outR"] | 0, h);
		if (force || g_mpx[side] < 0)
		{
			tft.fillRect (x, y0, 6, h, rgb (35, 39, 45));
			meterZone (x, y1, h, 0, px);
		}
		else if (px > g_mpx[side]) meterZone (x, y1, h, g_mpx[side], px);
		else if (px < g_mpx[side]) tft.fillRect (x, y1 - g_mpx[side], 6, g_mpx[side] - px, rgb (35, 39, 45));
		g_mpx[side] = px;
	}
}

// geometry of the knobs of the parameter screen
static int knobCols () { return (g_rot & 1) ? 3 : 2; }
static int knobPerPage () { return 6; }
static void knobCell (int i, int *x, int *y, int *w, int *h)
{
	const int cols = knobCols (), rows = knobPerPage () / cols;
	*w = SW / cols;
	*h = (FOOT_Y - ROW_Y - 4) / rows;
	*x = (i % cols) * *w;
	*y = ROW_Y + (i / cols) * *h;
}

static String knobText (const Row &r)
{
	if (r.kind == 1 || r.kind == 2) return fmtNum (r, r.v).substring (0, 5);
	const int dec = r.st >= 1.0f ? 0 : (r.st >= 0.1f ? 1 : 2);
	return String (r.v, dec);
}

static void drawKnob (int x, int y, int w, int h, const Row &r)
{
	g_nKnob++;
	const int cx = x + w / 2, cy = y + 33;
	const float f0 = r.mx > r.mn ? (r.v - r.mn) / (r.mx - r.mn) : 0.0f;
	const float f = f0 < 0 ? 0 : (f0 > 1 ? 1 : f0);
	tft.drawSmoothArc (cx, cy, 28, 21, 40, 320, rgb (47, 53, 61), TFT_BLACK, false);
	if (f > 0.01f) tft.drawSmoothArc (cx, cy, 28, 21, 40, 40 + (int) (280 * f), rgb (93, 202, 165), TFT_BLACK, false);
	const String v = knobText (r);
	tft.fillRect (cx - 20, cy - 9, 40, 18, TFT_BLACK);		// the old value, which may be wider
	tft.setTextColor (TFT_WHITE, TFT_BLACK);
	tft.drawString (v, cx - tft.textWidth (v, 2) / 2, cy - 8, 2);
	String lbl = r.label;
	if (r.unit.length () && r.kind == 0) lbl += " (" + r.unit + ")";
	lbl = lbl.substring (0, w / 8);
	tft.fillRect (x, y + 64, w, 16, TFT_BLACK);
	tft.setTextColor (rgb (154, 163, 173), TFT_BLACK);
	tft.drawString (lbl, cx - tft.textWidth (lbl, 2) / 2, y + 64, 2);
}


// geometry of the new preset screen
static void npInBtn (int i, int *x, int *y, int *w, int *h) { *x = 90 + i * 76; *y = 44; *w = 70; *h = 34; }
static void npSlot (int i, int *x, int *y, int *w, int *h)
{
	if (g_rot & 1) { *w = (SW - 20 - 18) / 4; *h = 60; *x = 10 + i * (*w + 6); *y = 88; }
	else { *w = (SW - 26) / 2; *h = 50; *x = 10 + (i % 2) * (*w + 6); *y = 88 + (i / 2) * 56; }
}
static void npOutBtn (int i, int *x, int *y, int *w, int *h)
{
	static const int X[3] = {90, 156, 222}, W[3] = {60, 60, 88};
	static const int XP[3] = {90, 138, 186}, WP[3] = {44, 44, 50};
	const bool land = g_rot & 1;
	*x = land ? X[i] : XP[i]; *w = land ? W[i] : WP[i]; *h = 34; *y = land ? 160 : 206;
}
// off-screen frame (in PSRAM): every whole redraw is composed here and pushed to the screen at once, so a screen change does not flash
static TFT_eSprite fb (&tftHw);
static int g_fbW = 0, g_fbH = 0;
static bool fbBegin ()
{
	if (g_fbW != SW || g_fbH != SH)
	{
		fb.deleteSprite ();
		fb.setAttribute (PSRAM_ENABLE, true);
		if (!fb.createSprite (SW, SH)) { g_fbW = 0; return false; }
		g_fbW = SW; g_fbH = SH;
	}
	gfx = &fb;
	fb.fillSprite (TFT_BLACK);
	return true;
}
struct FbGuard
{
	bool on;
	~FbGuard () { if (on) { fb.pushSprite (0, 0); gfx = &tftHw; } }
};

// while the slider is dragged only the value and the fill are redrawn (a full redraw would flicker)
// The value text and the slider are composed in memory (sprites) and sent to the screen in one piece: no erase-then-draw step, so no flicker while dragging.
static void drawEditDynamic ()
{
	if (g_edRow < 0 || g_edRow >= (int) g_rows.size ()) return;
	g_nDyn++;
	const Row &r = g_rows[g_edRow];
	static TFT_eSprite vs (&tftHw), ss (&tftHw);
	static int vw = 0, sw = 0, sh = 0;
	const int w = SW - 40;
	if (vw != w) { vs.deleteSprite (); vs.createSprite (w, 28); vw = w; }
	vs.fillSprite (TFT_BLACK);
	vs.setTextColor (TFT_CYAN, TFT_BLACK);
	vs.setTextDatum (TC_DATUM);
	vs.drawString (fmtNum (r, r.v), w / 2, 0, 4);
	vs.pushSprite (20, EVAL_Y);
	if (r.kind == 2) return;
	if (sw != SL_W || sh != SL_H) { ss.deleteSprite (); ss.createSprite (SL_W, SL_H); sw = SL_W; sh = SL_H; }
	const float f0 = r.mx > r.mn ? (r.v - r.mn) / (r.mx - r.mn) : 0.0f;
	const int px = (int) ((SL_W - 4) * (f0 < 0 ? 0 : (f0 > 1 ? 1 : f0)));
	ss.fillSprite (TFT_BLACK);
	ss.drawRect (0, 0, SL_W, SL_H, TFT_WHITE);
	ss.fillRect (2, 2, px, SL_H - 4, 0x0310);
	const float fs0 = r.mx > r.mn ? (g_edStart - r.mn) / (r.mx - r.mn) : 0.0f;
	const int sx = 2 + (int) ((SL_W - 4) * (fs0 < 0 ? 0 : (fs0 > 1 ? 1 : fs0)));
	ss.drawFastVLine (sx, 0, SL_H, rgb (255, 170, 60));		// where the value started
	ss.pushSprite (SL_X, SL_Y);
}

static void draw ()
{
	if (g_otaBusy) return;
	// The stage and knob screens are repainted piece by piece when only a value changed (no flicker); everything else is repainted whole.
	// A different screen, page, block, preset or rotation is a new view: clear it and forget what was drawn.
	static String lastView;
	static String tsig[12], ksig[6];
	static String hdrSig;
	String view = String ((int) g_scr) + "|" + String (g_rot) + "|" + String (g_page) + "|" + String (g_stagePg) + "|" + String (g_sel) + "|" + g_preset;
	if (g_scr != SCR_STAGE && g_scr != SCR_PARAMS) view += "|" + String (millis ());		// always whole
	const bool full = view != lastView || !g_stOk || !g_fxOk;
	lastView = view;
	g_nDraw++;
	FbGuard guard { full && fbBegin () };		// a whole view is composed off screen and sent in one piece when draw () returns
	if (full)
	{
		g_nFull++;
		snprintf (g_lastFullView, sizeof g_lastFullView, "%s", view.c_str ());
		for (String &s : tsig) s = "";
		for (String &s : ksig) s = "";
		hdrSig = "";
		g_mpx[0] = g_mpx[1] = -1;
		tft.fillRect (0, 38, SW, SH - 38, TFT_BLACK);
	}
	if (g_scr == SCR_BUSY)
	{
		drawHeader ("Wi-Fi", g_connSsid);
		tft.setTextColor (TFT_ORANGE, TFT_BLACK);
		tft.drawString ("Connecting...", 10, 100, 2);
		tft.setTextColor (TFT_WHITE, TFT_BLACK);
		tft.drawString (g_msg, 10, 130, 2);
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_PASS)
	{
		drawHeader (g_kbMode == 0 ? "Password" : (g_kbMode == 1 ? "Filter files" : "Filter blocks"), g_kbMode == 0 ? g_pwSsid : String ("part of the name"));
		tft.drawRect (4, PW_Y, SW - 50, PW_H, TFT_WHITE);
		String shown = (g_pwShow || g_kbMode != 0) ? g_pw : String (std::string (g_pw.length (), '*').c_str ());
		if (shown.length () > 20) shown = shown.substring (shown.length () - 20);
		tft.setTextColor (TFT_WHITE, TFT_BLACK);
		tft.drawString (shown, 8, PW_Y + (PW_H - 16) / 2, 2);
		tft.fillRoundRect (SW - 42, PW_Y, 40, PW_H, 4, 0x2945);
		tft.setTextColor (TFT_WHITE, 0x2945);
		tft.drawString (g_pwShow ? "Hide" : "Show", SW - 38, PW_Y + (PW_H - 16) / 2, 2);
		tft.setTextColor (TFT_ORANGE, TFT_BLACK);
		tft.drawString (g_msg, 6, PWMSG_Y, 1);
		drawKeyboard ();
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_CFG)
	{
		drawHeader ("Config", String (FW_NAME));
		tft.setTextColor (TFT_WHITE, TFT_BLACK);
		// in landscape the text is the left column (clipped, the buttons are on the right)
		const int cl = (g_rot & 1) ? 19 : 32;
		tft.drawString (g_wifiMsg.substring (0, cl), 8, 44, 2);
		tft.setTextColor (TFT_CYAN, TFT_BLACK);
		tft.drawString ((g_ssid.length () ? "Saved: " + g_ssid : String ("Saved: none")).substring (0, cl), 8, 62, 2);
		tft.drawString ((g_otaStarted ? String ("Update: ") + OTA_HOSTNAME + ".local" : String ("Update: off")).substring (0, cl), 8, 80, 2);
		const bool link = g_lastAnswer != 0 && millis () - g_lastAnswer < 6000;
		tft.setTextColor (link ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
		tft.drawString (link ? "NAMberry link: ok" : "NAMberry link: none", 8, 98, 2);
		if (g_msg.length ()) { tft.setTextColor (TFT_YELLOW, TFT_BLACK); tft.drawString (g_msg.substring (0, cl), 8, 116, 2); }
		tft.setTextColor (TFT_CYAN, TFT_BLACK);
		tft.drawString (String ("fw ") + FW_NUM, 8, 134, 2);
		if (g_rot & 1)
		{
			tft.drawString (String (FW_BUILD).substring (0, 20), 8, 152, 1);
			tft.drawString (g_rot == 1 ? "landscape" : "landscape flipped", 8, 164, 1);
		}
		drawButton (CFG_Y0, "Scan networks", 0x0310);
		drawButton (CFG_Y0 + CFG_DY, "Forget Wi-Fi", TFT_MAROON);
		drawButton (CFG_Y0 + 2 * CFG_DY, "Rotate screen", 0x2945);
		drawFooter ("< Back", "", "", "");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_SCAN)
	{
		drawHeader ("Wi-Fi networks", g_scanning ? "scanning..." : String (g_nets.size ()) + " found");
		const int pages = ((int) g_nets.size () + ROWS - 1) / ROWS;
		if (g_page >= pages) g_page = pages > 0 ? pages - 1 : 0;
		for (int i = 0; i < ROWS; i++)
		{
			const int k = g_page * ROWS + i;
			if (k >= (int) g_nets.size ()) break;
			const int y = ROW_Y + i * ROW_H;
			tft.setTextColor (TFT_WHITE, TFT_BLACK);
			tft.drawString (g_nets[k].ssid.substring (0, 22), 8, y + 3, 2);
			tft.setTextColor (TFT_CYAN, TFT_BLACK);
			tft.drawString (String (g_nets[k].rssi) + " dBm" + (g_nets[k].open ? "  open" : "  locked"), 8, y + 21, 2);
			tft.drawFastHLine (0, y + ROW_H - 1, SW, 0x2104);
		}
		drawFooter ("< Back", "Scan", "Up", "Down");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_EDIT && g_stOk && g_fxOk && g_edRow >= 0 && g_edRow < (int) g_rows.size ())
	{
		const Row &r = g_rows[g_edRow];
		drawHeader (r.label, blockName (g_blocks[g_sel].b));
		tft.setTextColor (TFT_CYAN, TFT_BLACK);
		const String val = fmtNum (r, r.v);
		tft.drawString (val, SW / 2 - tft.textWidth (val, 4) / 2, EVAL_Y, 4);
		if (r.kind == 2)
		{
			drawButton (EBOOL_Y, r.v > 0.5f ? "ON  (tap to turn off)" : "OFF  (tap to turn on)", r.v > 0.5f ? TFT_DARKGREEN : TFT_MAROON, 10, SW - 20);
		}
		else
		{
			// slider: touch or drag; the fill shows the position in the range
			tft.drawRect (SL_X, SL_Y, SL_W, SL_H, TFT_WHITE);
			const float f = r.mx > r.mn ? (r.v - r.mn) / (r.mx - r.mn) : 0.0f;
			{ const int px0 = (int) ((SL_W - 4) * (f < 0 ? 0 : (f > 1 ? 1 : f))); tft.fillRect (SL_X + 2, SL_Y + 2, px0, SL_H - 4, 0x0310); }
			tft.setTextColor (TFT_LIGHTGREY, TFT_BLACK);
			tft.drawString (fmtNum (r, r.mn), SL_X, SL_Y + SL_H + 3, 1);
			const String mx = fmtNum (r, r.mx);
			tft.drawString (mx, SL_X + SL_W - tft.textWidth (mx, 1), SL_Y + SL_H + 3, 1);
			// - and + by one step, -- and ++ by ten steps
			static const char *LBL[4] = {"--", "-", "+", "++"};
			const int bw = (SW - 20 - 18) / 4;
			tft.setTextColor (TFT_WHITE, 0x2945);
			for (int i = 0; i < 4; i++)
			{
				const int bx = 10 + i * (bw + 6);
				tft.fillRoundRect (bx, EBTN_Y, bw, EBTN_H, 6, 0x2945);
				tft.drawString (LBL[i], bx + (bw - tft.textWidth (LBL[i], 4)) / 2, EBTN_Y + (EBTN_H - 26) / 2, 4);
			}
		}
		{
			// the starting value, to go back to it
			const String st = "start: " + fmtNum (r, g_edStart);
			tft.setTextColor (rgb (255, 170, 60), TFT_BLACK);
			tft.drawString (st, SW / 2 - tft.textWidth (st, 2) / 2, EST_Y, 2);
			if (r.kind != 2)
			{
				const float fs0 = r.mx > r.mn ? (g_edStart - r.mn) / (r.mx - r.mn) : 0.0f;
				tft.drawFastVLine (SL_X + 2 + (int) ((SL_W - 4) * (fs0 < 0 ? 0 : (fs0 > 1 ? 1 : fs0))), SL_Y, SL_H, rgb (255, 170, 60));
			}
		}
		if (g_msg.length ()) { tft.setTextColor (TFT_YELLOW, TFT_BLACK); tft.drawString (g_msg, 8, EMSG_Y, 2); }
		drawFooter ("< Back", "", "", "Undo");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_NEWP && g_stOk)
	{
		// new simple preset: input, up to four blocks in a row, output
		drawHeader ("New preset", g_msg.length () ? g_msg : "replaces the board; saved as " + npName ());
		tft.setTextColor (TFT_WHITE, TFT_BLACK);
		tft.drawString ("Input", 8, 53, 2);
		for (int i = 0; i < 2; i++)
		{
			int x, y, w, h;
			npInBtn (i, &x, &y, &w, &h);
			const uint16_t c = g_npIn == i ? 0x0310 : rgb (43, 48, 56);
			tft.fillRoundRect (x, y, w, h, 6, c);
			tft.setTextColor (TFT_WHITE, c);
			tft.drawString (i == 0 ? "L" : "R", x + w / 2 - 4, y + 9, 2);
		}
		for (int i = 0; i < 4; i++)
		{
			int x, y, w, h;
			npSlot (i, &x, &y, &w, &h);
			if (g_nb[i].key.length () == 0)
			{
				tft.fillRoundRect (x, y, w, h, 7, rgb (23, 26, 31));
				tft.drawRoundRect (x, y, w, h, 7, rgb (74, 79, 87));
				tft.setTextColor (rgb (138, 144, 153), rgb (23, 26, 31));
				tft.drawString ("+", x + w / 2 - tft.textWidth ("+", 4) / 2, y + h / 2 - 13, 4);
			}
			else
			{
				uint16_t bg, fg, sub;
				catColors (catOfKey (g_nb[i].key), true, &bg, &fg, &sub);
				tft.fillRoundRect (x, y, w, h, 7, bg);
				tft.setTextColor (fg, bg);
				String t = g_nb[i].title;
				const int p = t.indexOf (" (");
				if (p > 0) t = t.substring (0, p);
				String l1, l2;
				splitName (t, w / 8 - 1, &l1, &l2);
				tft.drawString (l1, x + 5, y + 6, 2);
				if (l2.length ()) tft.drawString (l2, x + 5, y + 22, 2);
			}
		}
		int oy = 0;
		{ int x, y, w, h; npOutBtn (0, &x, &y, &w, &h); oy = y; }
		tft.setTextColor (TFT_WHITE, TFT_BLACK);
		tft.drawString ("Output", 8, oy + 9, 2);
		static const char *OL[3] = {"L", "R", "L+R"};
		for (int i = 0; i < 3; i++)
		{
			int x, y, w, h;
			npOutBtn (i, &x, &y, &w, &h);
			const uint16_t c = g_npOut == i ? 0x0310 : rgb (43, 48, 56);
			tft.fillRoundRect (x, y, w, h, 6, c);
			tft.setTextColor (TFT_WHITE, c);
			tft.drawString (OL[i], x + w / 2 - tft.textWidth (OL[i], 2) / 2, y + 9, 2);
		}
		drawFooter ("< Back", "", "", "Create");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_TYPES && g_stOk)
	{
		// the list of blocks for a slot
		drawHeader ("Block " + String (g_npSlot + 1), g_typeFilter.length () ? "filter: " + g_typeFilter : String ((int) g_typeIdx.size ()) + " types");
		const int pages = ((int) g_typeIdx.size () + ROWS - 1) / ROWS;
		if (g_page >= pages) g_page = pages > 0 ? pages - 1 : 0;
		for (int i = 0; i < ROWS; i++)
		{
			const int k = g_page * ROWS + i;
			if (k >= (int) g_typeIdx.size ()) break;
			const int y = ROW_Y + i * ROW_H;
			const NewBlock &nb = g_types[g_typeIdx[k]];
			tft.setTextColor (nb.key == g_nb[g_npSlot].key ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
			tft.drawString (nb.title.substring (0, (SW - 12) / 8), 8, y + 10, 2);
			tft.drawFastHLine (0, y + ROW_H - 1, SW, 0x2104);
		}
		drawFooter ("< Back", "Filter", String (g_page + 1) + "/" + String (pages > 0 ? pages : 1), "");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_FILES && g_stOk && g_sel >= 0 && g_sel < (int) g_blocks.size ())
	{
		// NAM file picker: the library, filtered by a text typed on the keyboard
		JsonArray lib = g_libDoc["lib"];
		const String cur = String ((const char *) (g_blocks[g_sel].b["file"] | ""));
		drawHeader ("NAM file", g_fileFilter.length () ? "filter: " + g_fileFilter : String ((int) g_fileIdx.size ()) + " files");
		const int pages = ((int) g_fileIdx.size () + ROWS - 1) / ROWS;
		if (g_page >= pages) g_page = pages > 0 ? pages - 1 : 0;
		for (int i = 0; i < ROWS; i++)
		{
			const int k = g_page * ROWS + i;
			if (k >= (int) g_fileIdx.size ()) break;
			const int y = ROW_Y + i * ROW_H;
			String nm = String ((const char *) (lib[g_fileIdx[k]] | ""));
			const bool isCur = nm == cur;
			if (nm.endsWith (".adn")) nm = nm.substring (0, nm.length () - 4);
			tft.setTextColor (isCur ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
			tft.drawString (nm.substring (0, (SW - 12) / 8), 8, y + 10, 2);
			tft.drawFastHLine (0, y + ROW_H - 1, SW, 0x2104);
		}
		if (g_fileIdx.empty ()) { tft.setTextColor (TFT_LIGHTGREY, TFT_BLACK); tft.drawString ("no file matches", 10, 80, 2); }
		if (g_msg.length ()) { tft.setTextColor (TFT_YELLOW, TFT_BLACK); tft.drawString (g_msg, 8, FOOT_Y - 22, 2); }
		drawFooter ("< Back", "Filter", String (g_page + 1) + "/" + String (pages > 0 ? pages : 1), "");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_PRESETS && g_stOk)
	{
		JsonArray pre = g_st["pre"];
		drawHeader ("Presets", String (g_preset));
		tft.fillRoundRect (SW - 40, 4, 36, 30, 6, 0x0310);		// + : a new simple preset
		tft.setTextColor (TFT_WHITE, 0x0310);
		tft.drawString ("+", SW - 40 + 18 - tft.textWidth ("+", 4) / 2, 7, 4);
		const int pages = ((int) pre.size () + ROWS - 1) / ROWS;
		if (g_page >= pages) g_page = pages > 0 ? pages - 1 : 0;
		for (int i = 0; i < ROWS; i++)
		{
			const int k = g_page * ROWS + i;
			if (k >= (int) pre.size ()) break;
			const int y = ROW_Y + i * ROW_H;
			const char *nm = pre[k] | "";
			const bool cur = !strcmp (nm, g_st["pc"] | "");
			tft.setTextColor (cur ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
			tft.drawString (String (nm).substring (0, 24), 8, y + 10, 2);
			tft.drawFastHLine (0, y + ROW_H - 1, SW, 0x2104);
		}
		if (g_msg.length ()) { tft.setTextColor (TFT_YELLOW, TFT_BLACK); tft.drawString (g_msg, 8, FOOT_Y - 22, 2); }
		drawFooter ("< Back", String (g_page + 1) + "/" + String (pages > 0 ? pages : 1), "Up", "Down");
		g_dirty = false;
		return;
	}
	if (!g_stOk || !g_fxOk)
	{
		drawHeader (FW_NAME, g_wifiMsg);
		tft.setTextColor (TFT_ORANGE, TFT_BLACK);
		tft.drawString ("Waiting for the NAMberry", 10, 80, 2);
		tft.setTextColor (TFT_WHITE, TFT_BLACK);
		tft.drawString (g_msg, 10, 110, 2);
		tft.drawString (g_rxInfo, 10, 140, 2);
		tft.setTextColor (TFT_CYAN, TFT_BLACK);
		tft.drawString ("fw " FW_NUM, 10, FOOT_Y - 54, 2);
		tft.drawString (FW_BUILD, 10, FOOT_Y - 32, 2);
		drawFooter ("Cfg", "", "", "");
		g_dirty = false;
		return;
	}
	if (g_scr == SCR_STAGE)
	{
		// the home screen: preset name with the arrows to change it, the blocks as tiles in the order of the signal, the output levels at the sides
		const int per = chainCols () * chainRows ();
		const int pages = ((int) g_blocks.size () + per - 1) / per;
		if (g_stagePg >= pages) g_stagePg = pages > 0 ? pages - 1 : 0;
		if (full)
		{
			tft.fillRect (0, 0, SW, 38, TFT_NAVY);
			const uint16_t ab = rgb (43, 48, 56);
			tft.fillRoundRect (4, 4, 34, 30, 6, ab);
			tft.fillRoundRect (SW - 38, 4, 34, 30, 6, ab);
			tft.setTextColor (TFT_WHITE, ab);
			tft.drawString ("<", 4 + 17 - tft.textWidth ("<", 4) / 2, 7, 4);
			tft.drawString (">", SW - 38 + 17 - tft.textWidth (">", 4) / 2, 7, 4);
			const String nm = g_preset.substring (0, (SW - 90) / 14);
			tft.setTextColor (TFT_WHITE, TFT_NAVY);
			tft.drawString (nm, SW / 2 - tft.textWidth (nm, 4) / 2, 7, 4);
			drawFooter ("Presets", "Cfg", "", String (g_stagePg + 1) + "/" + String (pages > 0 ? pages : 1));
		}
		for (int i = 0; i < per; i++)
		{
			const int k = g_stagePg * per + i;
			if (k >= (int) g_blocks.size ()) break;
			JsonObject b = g_blocks[k].b;
			const String sg = shortName (b) + "|" + String ((int) (b["on"] | 1)) + "|" + tileValue (b);
			if (sg == tsig[i]) continue;		// this tile is already on the screen as it is
			tsig[i] = sg;
			int x, y, w, h;
			chainCell (i, &x, &y, &w, &h);
			drawTile (x, y, w, h, b, true);
		}
		drawMeters (full);
	}
	else
	{
		// B. parameters of one block as knobs
		JsonObject b = g_blocks[g_sel].b;
		const bool on = (int) (b["on"] | 1) != 0;
		const int per = knobPerPage ();
		const int pages = ((int) g_kn.size () + per - 1) / per;
		if (g_page >= pages) g_page = pages > 0 ? pages - 1 : 0;
		const bool isNam = !strcmp (b["t"] | "", "nam");
		const String hs = shortName (b) + (on ? "|1" : "|0");
		if (hs != hdrSig)
		{
			hdrSig = hs;
			tft.fillRect (0, 0, SW, 38, TFT_NAVY);
			tft.setTextColor (TFT_WHITE, TFT_NAVY);
			tft.drawString (shortName (b).substring (0, (SW - 70) / 8), 6, isNam ? 4 : 10, 2);
			if (isNam) { tft.setTextColor (rgb (127, 212, 255), TFT_NAVY); tft.drawString ("tap the name: change file", 6, 24, 1); }
			tft.fillRoundRect (SW - 62, 6, 56, 26, 13, on ? rgb (46, 125, 79) : rgb (85, 91, 99));
			tft.setTextColor (TFT_WHITE, on ? rgb (46, 125, 79) : rgb (85, 91, 99));
			tft.drawString (on ? "ON" : "OFF", SW - 62 + 28 - tft.textWidth (on ? "ON" : "OFF", 2) / 2, 11, 2);
		}
		for (int i = 0; i < per; i++)
		{
			const int k = g_page * per + i;
			if (k >= (int) g_kn.size ()) break;
			const Row &r = g_rows[g_kn[k]];
			const String sg = r.label + "|" + String (r.v, 3);
			if (sg == ksig[i]) continue;		// this knob is already on the screen as it is
			ksig[i] = sg;
			int x, y, w, h;
			knobCell (i, &x, &y, &w, &h);
			drawKnob (x, y, w, h, r);
		}
		if (full)
		{
			if (g_kn.empty ()) { tft.setTextColor (TFT_LIGHTGREY, TFT_BLACK); tft.drawString ("nothing to adjust here", 10, 80, 2); }
			drawFooter ("< Stage", "Prev", "Next", String (g_page + 1) + "/" + String (pages > 0 ? pages : 1));
		}
	}
	g_dirty = false;
}

static void startScan ()
{
	WiFi.mode (WIFI_STA);
	WiFi.scanDelete ();
	WiFi.scanNetworks (true);
	g_scanning = true;
	g_nets.clear ();
	g_page = 0;
}

static void scanLoop ()
{
	if (!g_scanning) return;
	const int n = WiFi.scanComplete ();
	if (n == WIFI_SCAN_RUNNING) return;
	g_scanning = false;
	if (n > 0)
	{
		for (int i = 0; i < n; i++)
		{
			const String s = WiFi.SSID (i);
			if (s.length () == 0) continue;
			bool seen = false;
			for (Net &x : g_nets) if (x.ssid == s) { seen = true; if (WiFi.RSSI (i) > x.rssi) x.rssi = WiFi.RSSI (i); }
			if (!seen) g_nets.push_back ({s, WiFi.RSSI (i), WiFi.encryptionType (i) == WIFI_AUTH_OPEN});
		}
		std::sort (g_nets.begin (), g_nets.end (), [] (const Net &a, const Net &b) { return a.rssi > b.rssi; });
	}
	WiFi.scanDelete ();
	g_dirty = true;
}

static void beginConnect (const String &ssid, const String &pass)
{
	g_connSsid = ssid; g_connPass = pass;
	g_connT0 = millis ();
	g_msg = "";
	g_scr = SCR_BUSY;
	wifiConnect (ssid, pass);
	g_dirty = true;
}

static void connectLoop ()
{
	if (g_scr != SCR_BUSY) return;
	if (WiFi.status () == WL_CONNECTED)
	{
		wifiSave (g_connSsid, g_connPass);
		g_msg = "Connected: " + WiFi.localIP ().toString ();
		g_scr = SCR_CFG;
		g_dirty = true;
	}
	else if (millis () - g_connT0 > 20000)
	{
		WiFi.disconnect (true, false);
		g_wifiWanted = false;
		g_msg = "Failed: wrong password?";
		g_scr = g_pwOpen ? SCR_CFG : SCR_PASS;
		g_dirty = true;
	}
}

static void onKeyboardTouch (int x, int y)
{
	if (y >= PW_Y && y < PW_Y + PW_H && x >= SW - 42) { g_pwShow = !g_pwShow; g_dirty = true; return; }
	if (y < KB_Y) return;
	const int r = (y - KB_Y) / KEY_H;
	if (r < 4)
	{
		const char *row = KB[g_kbLayer][r];
		const int n = (int) strlen (row);
		const int off = (SW - n * KEY_W) / 2;
		const int i = (x - off) / KEY_W;
		if (x >= off && i >= 0 && i < n && g_pw.length () < 63)
		{
			g_pw += row[i];
			if (g_kbLayer == 1) g_kbLayer = 0;		// one capital letter, then back to lower case
			g_msg = "";
			g_dirty = true;
		}
		return;
	}
	if (r == 4)
	{
		if (x < kbEdge (36)) { g_kbLayer = (g_kbLayer == 1) ? 0 : 1; }
		else if (x < kbEdge (72)) { g_kbLayer = (g_kbLayer == 2) ? 0 : 2; }
		else if (x < kbEdge (156)) { if (g_pw.length () < 63) g_pw += ' '; }
		else if (x < kbEdge (192)) { if (g_pw.length ()) g_pw.remove (g_pw.length () - 1); }
		else if (g_kbMode == 1) { g_fileFilter = g_pw; buildFileIdx (); g_scr = SCR_FILES; g_page = 0; g_msg = ""; }
		else if (g_kbMode == 2) { g_typeFilter = g_pw; buildTypeIdx (); g_scr = SCR_TYPES; g_page = 0; g_msg = ""; }
		else { if (g_pw.length () >= 8) beginConnect (g_pwSsid, g_pw); else g_msg = "A WPA password has at least 8 characters"; }
		g_dirty = true;
	}
}

// previous / next block in the parameter screen
static void stepBlock (int d)
{
	const int n = (int) g_blocks.size ();
	if (n == 0) return;
	g_sel = (g_sel + d + n) % n;
	buildRows (g_blocks[g_sel].b);
	g_page = 0;
}

// long press on a tile of the stage screen: open the knobs of that block
static void openBlockAt (int x, int y)
{
	const int per = chainCols () * chainRows ();
	for (int i = 0; i < per; i++)
	{
		const int k = g_stagePg * per + i;
		if (k >= (int) g_blocks.size ()) break;
		int bx, by, bw, bh;
		chainCell (i, &bx, &by, &bw, &bh);
		if (x >= bx && x < bx + bw && y >= by && y < by + bh)
		{
			g_sel = k;
			buildRows (g_blocks[k].b);
			g_scr = SCR_PARAMS;
			g_page = 0;
			g_dirty = true;
			return;
		}
	}
}

static void onTouch (int x, int y)
{
	if (g_scr == SCR_BUSY) return;
	if (g_scr == SCR_PASS)
	{
		onKeyboardTouch (x, y);
		return;
	}
	if (y >= FOOT_Y)
	{
		const int z = x / (SW / 4);
		if (g_scr == SCR_STAGE)
		{
			const int per = chainCols () * chainRows ();
			const int pages = ((int) g_blocks.size () + per - 1) / per;
			if (z == 0) { g_scr = SCR_PRESETS; g_page = 0; g_msg = ""; }
			else if (z == 1) { g_scr = SCR_CFG; g_msg = ""; g_page = 0; }
			else if (z == 3 && pages > 0) g_stagePg = (g_stagePg + 1) % pages;
		}
		else if (g_scr == SCR_PARAMS)
		{
			const int pages = ((int) g_kn.size () + knobPerPage () - 1) / knobPerPage ();
			if (z == 0) { g_scr = SCR_STAGE; g_page = 0; }
			else if (z == 1) stepBlock (-1);
			else if (z == 2) stepBlock (1);
			else if (z == 3 && pages > 0) g_page = (g_page + 1) % pages;
		}
		else if (g_scr == SCR_CFG)
		{
			if (z == 0) { g_scr = SCR_STAGE; g_page = 0; }
		}
		else if (g_scr == SCR_EDIT)
		{
			if (z == 0) { g_scr = SCR_PARAMS; g_msg = ""; }
			else if (z == 3 && g_edRow >= 0 && g_edRow < (int) g_rows.size () && g_sel >= 0 && g_sel < (int) g_blocks.size ())
			{
				// Undo: the value the parameter had when the editor was opened
				Row &r = g_rows[g_edRow];
				if (r.v != g_edStart && boardSet (g_blocks[g_sel].id, r.key, g_edStart)) { r.v = g_edStart; r.value = fmtNum (r, r.v); }
				if (r.kind == 2 || g_msg.length ()) g_dirty = true; else drawEditDynamic ();
				return;
			}
		}
		else if (g_scr == SCR_NEWP)
		{
			if (z == 0) { g_scr = SCR_PRESETS; g_page = 0; g_msg = ""; }
			else if (z == 3)
			{
				// build the preset on the board (a second) and go back to the stage screen
				g_msg = "creating the preset...";
				draw ();
				if (npRun ()) { g_scr = SCR_STAGE; g_page = 0; g_stagePg = 0; g_msg = ""; g_tStatus = 0; }
				g_dirty = true;
			}
		}
		else if (g_scr == SCR_TYPES)
		{
			if (z == 0) { g_scr = SCR_NEWP; g_msg = ""; }
			else if (z == 1) { g_kbMode = 2; g_pw = g_typeFilter; g_pwShow = true; g_kbLayer = 0; g_msg = ""; g_scr = SCR_PASS; }
		}
		else if (g_scr == SCR_FILES)
		{
			if (z == 0) { g_scr = SCR_PARAMS; g_page = 0; g_msg = ""; }
			else if (z == 1) { g_kbMode = 1; g_pw = g_fileFilter; g_pwShow = true; g_kbLayer = 0; g_msg = ""; g_scr = SCR_PASS; }
		}
		else if (g_scr == SCR_PRESETS)
		{
			if (z == 0) { g_scr = SCR_STAGE; g_page = 0; g_msg = ""; }
			else if (z == 2 && g_page > 0) g_page--;
			else if (z == 3) g_page++;
		}
		else if (g_scr == SCR_SCAN)
		{
			if (z == 0) { g_scr = SCR_CFG; g_page = 0; }
			else if (z == 1) startScan ();
			else if (z == 2 && g_page > 0) g_page--;
			else if (z == 3) g_page++;
		}
		g_dirty = true;
		return;
	}
	if (g_scr == SCR_CFG)
	{
		if (x < BTN_X) return;
		if (y >= CFG_Y0 && y < CFG_Y0 + CFG_DY) { g_scr = SCR_SCAN; startScan (); g_dirty = true; }
		else if (y >= CFG_Y0 + CFG_DY && y < CFG_Y0 + 2 * CFG_DY) { wifiForget (); g_msg = "Wi-Fi forgotten"; g_dirty = true; }
		else if (y >= CFG_Y0 + 2 * CFG_DY && y < CFG_Y0 + 3 * CFG_DY)
		{
			g_rot = (g_rot + 1) & 3;		// portrait, landscape, portrait flipped, landscape flipped
			g_prefs.begin ("nbui", false); g_prefs.putInt ("rot", g_rot); g_prefs.end ();
			tft.setRotation (g_rot);
			applyLayout ();
			g_page = 0;
			tft.fillScreen (TFT_BLACK);
			g_dirty = true;
		}
		return;
	}
	if (g_scr == SCR_EDIT)
	{
		if (g_edRow < 0 || g_edRow >= (int) g_rows.size () || g_sel < 0 || g_sel >= (int) g_blocks.size ()) return;
		Row &r = g_rows[g_edRow];
		float nv = r.v;
		if (r.kind == 2) { if (y >= EBOOL_Y && y < EBOOL_Y + 36) nv = r.v > 0.5f ? 0.0f : 1.0f; else return; }
		else if (y >= EBTN_Y && y < EBTN_Y + EBTN_H)
		{
			const float s = r.st > 0 ? r.st : 1.0f;
			int bi = (x - 10) / ((SW - 20 - 18) / 4 + 6);		// the four buttons: -10 steps, -1, +1, +10
			bi = bi < 0 ? 0 : (bi > 3 ? 3 : bi);
			nv = r.v + (bi == 0 ? -10 * s : (bi == 1 ? -s : (bi == 2 ? s : 10 * s)));
		}
		else return;		// the slider is handled while the finger is down (loop)
		if (nv < r.mn) nv = r.mn;
		if (nv > r.mx) nv = r.mx;
		if (nv != r.v && boardSet (g_blocks[g_sel].id, r.key, nv)) { r.v = nv; r.value = fmtNum (r, nv); }
		if (r.kind == 2) g_dirty = true;		// the on/off button changes colour: whole screen
		else { drawEditDynamic (); if (g_msg.length ()) g_dirty = true; }
		return;
	}
	if (g_scr == SCR_PRESETS && y < 38 && x >= SW - 44)
	{
		// + : start a new simple preset
		buildTypes ();
		for (NewBlock &n : g_nb) n = NewBlock ();
		g_npIn = 0; g_npOut = 0;
		g_msg = "";
		g_scr = SCR_NEWP;
		g_dirty = true;
		return;
	}
	if (g_scr == SCR_NEWP)
	{
		for (int i = 0; i < 2; i++)
		{
			int bx, by, bw, bh;
			npInBtn (i, &bx, &by, &bw, &bh);
			if (x >= bx && x < bx + bw && y >= by && y < by + bh) { g_npIn = i; g_dirty = true; return; }
		}
		for (int i = 0; i < 3; i++)
		{
			int bx, by, bw, bh;
			npOutBtn (i, &bx, &by, &bw, &bh);
			if (x >= bx && x < bx + bw && y >= by && y < by + bh) { g_npOut = i; g_dirty = true; return; }
		}
		for (int i = 0; i < 4; i++)
		{
			int bx, by, bw, bh;
			npSlot (i, &bx, &by, &bw, &bh);
			if (x >= bx && x < bx + bw && y >= by && y < by + bh) { g_npSlot = i; g_page = 0; g_scr = SCR_TYPES; g_dirty = true; return; }
		}
		return;
	}
	if (g_scr == SCR_STAGE)
	{
		JsonArray pre = g_st["pre"];
		if (y < 38)
		{
			// the arrows in the header: previous / next preset; the name opens the list
			if ((x < 44 || x >= SW - 44) && pre.size () > 0)
			{
				int pi = 0;
				for (int i = 0; i < (int) pre.size (); i++) if (!strcmp (pre[i] | "", g_st["pc"] | "")) pi = i;
				pi = (pi + (x < 44 ? (int) pre.size () - 1 : 1)) % (int) pre.size ();
				g_msg = "";
				boardPost ("/pre", "op=load&n=" + urlEnc (String ((const char *) (pre[pi] | ""))));
			}
			else { g_scr = SCR_PRESETS; g_page = 0; g_msg = ""; }
			g_dirty = true;
			return;
		}
		// a tap on a block switches it on or off (a long press opens its knobs, see loop)
		const int per = chainCols () * chainRows ();
		for (int i = 0; i < per; i++)
		{
			const int k = g_stagePg * per + i;
			if (k >= (int) g_blocks.size ()) break;
			int bx, by, bw, bh;
			chainCell (i, &bx, &by, &bw, &bh);
			if (x >= bx && x < bx + bw && y >= by && y < by + bh)
			{
				const bool on = (int) (g_blocks[k].b["on"] | 1) != 0;
				if (boardSet (g_blocks[k].id, "on", on ? 0.0f : 1.0f)) g_blocks[k].b["on"] = on ? 0 : 1;
				g_dirty = true;
				return;
			}
		}
		return;
	}
	if (g_scr == SCR_PARAMS && y < 38 && x < SW - 70 && g_sel >= 0 && g_sel < (int) g_blocks.size () && !strcmp (g_blocks[g_sel].b["t"] | "", "nam"))
	{
		// the name of a NAM block opens the picker of its file
		g_msg = "loading the library...";
		draw ();
		if (loadLib ()) { g_fileFilter = ""; buildFileIdx (); g_scr = SCR_FILES; g_page = 0; g_msg = ""; }
		else g_msg = "library not available";
		g_dirty = true;
		return;
	}
	if (g_scr == SCR_PARAMS && y < 38 && g_sel >= 0 && g_sel < (int) g_blocks.size ())
	{
		const bool on = (int) (g_blocks[g_sel].b["on"] | 1) != 0;
		if (boardSet (g_blocks[g_sel].id, "on", on ? 0.0f : 1.0f)) g_blocks[g_sel].b["on"] = on ? 0 : 1;
		g_dirty = true;
		return;
	}
	if (y < ROW_Y) return;
	const int i = (y - ROW_Y) / ROW_H;
	if (g_scr == SCR_SCAN)
	{
		const int k = g_page * ROWS + i;
		if (k < (int) g_nets.size ())
		{
			g_pwSsid = g_nets[k].ssid;
			g_pwOpen = g_nets[k].open;
			g_pw = "";
			g_pwShow = false;
			g_kbLayer = 0;
			g_msg = "";
			if (g_pwOpen) beginConnect (g_pwSsid, "");
			else { g_kbMode = 0; g_scr = SCR_PASS; }
			g_dirty = true;
		}
	}
	else if (g_scr == SCR_PARAMS)
	{
		// a knob opens the editor
		const int per = knobPerPage ();
		for (int i2 = 0; i2 < per; i2++)
		{
			const int k = g_page * per + i2;
			if (k >= (int) g_kn.size ()) break;
			int bx, by, bw, bh;
			knobCell (i2, &bx, &by, &bw, &bh);
			if (x >= bx && x < bx + bw && y >= by && y < by + bh)
			{
				g_edRow = g_kn[k];
				g_edStart = g_rows[g_edRow].v;
				g_msg = "";
				g_scr = SCR_EDIT;
				g_dirty = true;
				return;
			}
		}
	}
	else if (g_scr == SCR_TYPES)
	{
		// the chosen block goes into the slot
		const int k = g_page * ROWS + i;
		if (k < (int) g_typeIdx.size ())
		{
			g_nb[g_npSlot] = g_types[g_typeIdx[k]];
			g_scr = SCR_NEWP;
			g_msg = "";
			g_dirty = true;
		}
	}
	else if (g_scr == SCR_FILES)
	{
		// a file of the list is chosen for the block
		JsonArray lib = g_libDoc["lib"];
		const int k = g_page * ROWS + i;
		if (k < (int) g_fileIdx.size () && g_sel >= 0 && g_sel < (int) g_blocks.size ())
		{
			const String nm = String ((const char *) (lib[g_fileIdx[k]] | ""));
			g_msg = "loading the model...";
			draw ();
			if (boardPost ("/g", "op=file&id=" + String (g_blocks[g_sel].id) + "&s=" + urlEnc (nm))) { g_scr = SCR_PARAMS; g_page = 0; g_msg = ""; }
			g_dirty = true;
		}
	}
	else if (g_scr == SCR_PRESETS)
	{
		JsonArray pre = g_st["pre"];
		const int k = g_page * ROWS + i;
		if (k < (int) pre.size ())
		{
			const String nm = String ((const char *) (pre[k] | ""));
			g_msg = "loading...";
			if (boardPost ("/pre", "op=load&n=" + urlEnc (nm))) { g_scr = SCR_STAGE; g_page = 0; g_msg = ""; }
			g_dirty = true;
		}
	}
}

// ---------------------------------------------------------------- main
void setup ()
{
	Serial.setRxBufferSize (16384);
	Serial.begin (115200);
	neopixelWrite (RGB_LED, 0, 0, 0);
	pinMode (TP_RST, OUTPUT); digitalWrite (TP_RST, LOW); delay (20); digitalWrite (TP_RST, HIGH); delay (100);
	Wire.begin (TP_SDA, TP_SCL, 400000);
	tft.init ();
	g_prefs.begin ("nbui", true); g_rot = g_prefs.getInt ("rot", 0) & 3; g_prefs.end ();
	tft.setRotation (g_rot);
	applyLayout ();
	tft.fillScreen (TFT_BLACK);
	g_rx = (char *) ps_malloc (RX_CAP);
	if (!g_rx) { g_msg = "no PSRAM"; }
	wifiLoadSaved ();
	xTaskCreatePinnedToCore (netTask, "net", 8192, nullptr, 1, nullptr, 0);
	draw ();
}

void loop ()
{
	wifiLoop ();
	scanLoop ();
	connectLoop ();
	if (g_otaBusy)
	{
		static unsigned shown = 1000;
		if (shown == 1000) { tft.fillScreen (TFT_BLACK); shown = 1001; }
		if (g_otaPct != shown) { shown = g_otaPct; drawOtaProgress (shown); }
		delay (20);
		return;
	}

	// the data link runs only on the two data screens (the Wi-Fi screens must not be disturbed)
	const bool dataScreen = g_scr == SCR_STAGE || g_scr == SCR_PARAMS || g_scr == SCR_EDIT || g_scr == SCR_PRESETS;
	static uint32_t tFx = 0;
	int x, y;
	const bool finger = touchNow (&x, &y);
	if (dataScreen && g_rx && !g_fxOk && millis () - tFx > 3000)
	{
		tFx = millis ();
		g_msg = "reading the effects...";
		draw ();
		if (loadFx ()) g_msg = "";
		else if (g_msg.indexOf ("stalled") < 0) g_msg = "no answer on the USB link";
		g_dirty = true;
	}
	if (dataScreen && g_rx && g_fxOk && !finger && millis () - g_tStatus > (g_scr == SCR_STAGE ? 500 : 2000))
	{
		g_tStatus = millis ();
		g_nPoll++;
		const bool ok = loadStatus ();
		if (!ok) { if (g_msg != "no answer on the USB link") { g_msg = "no answer on the USB link"; g_dirty = true; } }
		else
		{
			// rebuild the lists when the preset or its blocks changed
			String key = String (g_st["pc"] | "") + String ((int) (g_st["pd"] | 0)) + String (g_st["board"]["s"] | 0);
			const bool changed = key != g_blocksKey;
			buildBlocks ();
			if (changed)
			{
				g_blocksKey = key;
				if (g_scr == SCR_PARAMS || g_scr == SCR_EDIT) { g_scr = SCR_STAGE; g_page = 0; }
			}
			else if (g_scr == SCR_PARAMS && g_sel >= 0 && g_sel < (int) g_blocks.size ()) buildRows (g_blocks[g_sel].b);
			// redraw only when what is on the screen really changed (the meters in the status change every time)
			String sig = String ((int) g_scr) + "|" + g_preset + "|" + String (g_page);
			if (g_scr == SCR_STAGE)
				{
					sig += "|" + String (g_stagePg);
					for (const BlockRef &b : g_blocks) sig += "|" + shortName (b.b) + String ((int) (b.b["on"] | 1)) + tileValue (b.b);
				}
			if (g_scr == SCR_PARAMS && g_sel >= 0 && g_sel < (int) g_blocks.size ())
			{
				sig += String ((int) (g_blocks[g_sel].b["on"] | 1));
				for (const Row &r : g_rows) sig += "|" + r.label + r.value;
			}
			if (g_scr == SCR_PRESETS) sig += String ((const char *) (g_st["pc"] | "")) + String ((int) g_st["pre"].size ());
			if (g_msg == "no answer on the USB link") { g_msg = ""; g_dirty = true; }
			if (sig != g_sig) { g_sig = sig; g_dirty = true; }
				else if (g_scr == SCR_STAGE && !g_dirty) drawMeters (false);		// only the levels changed
		}
	}
	const bool listScr = g_scr == SCR_STAGE || g_scr == SCR_PARAMS || g_scr == SCR_PRESETS || g_scr == SCR_SCAN || g_scr == SCR_FILES || g_scr == SCR_TYPES;
	if (!listScr)
	{
		if (touchPress (&x, &y)) onTouch (x, y);
	}
	else
	{
		// lists: a tap acts when the finger is lifted, a vertical swipe turns the page (swipe up = next page, down = previous);
		// on the stage screen a long press on a block opens its knobs
		static bool down = false, longDone = false;
		static uint32_t tDown = 0;
		static int sx = 0, sy = 0, lx = 0, ly = 0;
		if (finger && !down) { down = true; longDone = false; tDown = millis (); sx = lx = x; sy = ly = y; }
		else if (finger)
		{
			lx = x; ly = y;
			if (g_scr == SCR_STAGE && !longDone && millis () - tDown > 600 && abs (lx - sx) < 14 && abs (ly - sy) < 14)
			{
				longDone = true;
				openBlockAt (sx, sy);
			}
		}
		else if (down)
		{
			down = false;
			const int dx = lx - sx, dy = ly - sy;
			if (longDone) { }		// the long press has already acted
			else if (abs (dy) > 30 && abs (dy) > 2 * abs (dx) && sy >= ROW_Y && sy < FOOT_Y)
			{
				// up = next page, down = previous page
				if (g_scr == SCR_STAGE)
				{
					const int per = chainCols () * chainRows ();
					const int pages = ((int) g_blocks.size () + per - 1) / per;
					if (dy < 0 && g_stagePg + 1 < pages) g_stagePg++;
					else if (dy > 0 && g_stagePg > 0) g_stagePg--;
				}
				else if (dy < 0) g_page++;
				else if (g_page > 0) g_page--;
				g_dirty = true;
			}
			else if (abs (dx) > 45 && abs (dx) > 2 * abs (dy) && sy >= ROW_Y && sy < FOOT_Y && g_scr == SCR_PARAMS)
			{
				stepBlock (dx < 0 ? 1 : -1);		// sideways: next / previous block
				g_dirty = true;
			}
			else if (abs (dx) < 14 && abs (dy) < 14) onTouch (sx, sy);
		}
	}
	// slider of the editor: follows the finger, one command at most every 120 ms
	if (finger && g_scr == SCR_EDIT && g_edRow >= 0 && g_edRow < (int) g_rows.size () && g_sel >= 0 && g_sel < (int) g_blocks.size ())
	{
		Row &r = g_rows[g_edRow];
		static uint32_t tSent = 0;
		if (r.kind != 2 && y >= SL_Y - 8 && y < SL_Y + SL_H + 8 && x >= SL_X - 6 && x <= SL_X + SL_W + 6 && millis () - tSent > 120)
		{
			float f = (float) (x - SL_X) / (float) SL_W;
			f = f < 0 ? 0 : (f > 1 ? 1 : f);
			float nv = r.mn + f * (r.mx - r.mn);
			if (r.st > 0) nv = r.mn + roundf ((nv - r.mn) / r.st) * r.st;
			nv = nv < r.mn ? r.mn : (nv > r.mx ? r.mx : nv);
			if (nv != r.v)
			{
				tSent = millis ();
				if (boardSet (g_blocks[g_sel].id, r.key, nv)) { r.v = nv; r.value = fmtNum (r, nv); drawEditDynamic (); }
			}
		}
	}
	static uint32_t tDraw = 0;
	if (g_dirty && millis () - tDraw > 150) { tDraw = millis (); draw (); }
	delay (5);
}
