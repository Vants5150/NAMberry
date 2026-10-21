// test_midi_board.cpp - MIDI in the document: block classes, first block of a class, normalized parameters, saving in the presets
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "midi.h"
#include "fx_defs.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *err_ = (e); if (err_) { printf ("unexpected error line %d: %s\n", __LINE__, err_); fails++; } } while (0)
#define NEAR(a, b, t) do { if (fabs ((double) (a) - (double) (b)) > (t)) { printf ("FAILED line %d: %g != %g\n", __LINE__, (double) (a), (double) (b)); fails++; } } while (0)

static int Add (CBoard &B, const char *type, int r, int c)
{
	int id = -1;
	const char *e = B.NetAdd (type, r, c, "x.adn", &id);
	if (e) { printf ("NetAdd %s (%d,%d): %s\n", type, r, c, e); fails++; }
	return id;
}

int main ()
{
	static CBoard B;
	B.SetMode (2);
	B.Reset ();
	const int ts = Add (B, "tsdrive", 2, 1), fl1 = Add (B, "flanger", 1, 2), nam = Add (B, "nam", 2, 4), fl2 = Add (B, "flanger", 1, 6);
	const int rv1 = Add (B, "adelverb", 3, 7), rv2 = Add (B, "adelverbm", 1, 7), dl = Add (B, "tapeecho", 4, 7), eq = Add (B, "adelequal", 0, 5);

	CHECK (CBoard::MidiClassOfType (NodeNam) == McAmp && CBoard::MidiClassOfType (FxCabIR) == McIr && CBoard::MidiClassOfType (NodeVolume) == 0);
	CHECK (B.MidiFindClass (McDrive) == ts);
	CHECK (B.MidiFindClass (McModPre) == fl1);			// to the left of the NAM (column 4)
	CHECK (B.MidiFindClass (McModPost) == fl2);			// to the right of the NAM
	CHECK (B.MidiFindClass (McAmp) == nam);
	CHECK (B.MidiFindClass (McReverb) == rv2);			// same column 7: the topmost (row 1)
	CHECK (B.MidiFindClass (McDelay) == dl);
	CHECK (B.MidiFindClass (McEq) == eq);
	CHECK (B.MidiFindClass (McTuner) == -1 && B.MidiFindClass (McCompressor) == -1 && B.MidiFindClass (0) == -1 && B.MidiFindClass (17) == -1);
	unsigned mask = B.MidiClassMask ();
	CHECK ((mask & (1u << (McAmp - 1))) && (mask & (1u << (McReverb - 1))) && !(mask & (1u << (McTuner - 1))));
	OK (B.SetValue (rv2, "on", 0.0f));
	CHECK (B.MidiClassMask () != mask && !(B.MidiClassMask () & (1u << (McReverb - 1))));		// the first reverb is off: the class turns out off
	OK (B.SetValue (rv2, "on", 1.0f));
	// the first of a class can change: I move reverb rv1 to the left
	OK (B.NetMove (rv1, 3, 6));
	CHECK (B.MidiFindClass (McReverb) == rv1);

	// normalized parameters
	OK (B.SetParamNorm (nam, 0, 0.5f));
	CHECK (fabsf (B.Block (nam).V[0] - (-6.0f)) < 1e-4f);
	OK (B.SetParamNorm (nam, 1, 1.0f));
	CHECK (B.Block (nam).V[1] == 12.0f);
	CHECK (B.SetParamNorm (nam, 5, 0.5f) != 0);		// the NAM has 5 parameters: input, output, bass, mid, treble
	const int vol = Add (B, "vol", 5, 2), pn = Add (B, "pan", 5, 3);
	OK (B.SetParamNorm (vol, 0, 0.0f)); CHECK (B.Block (vol).V[0] == -40.0f);
	OK (B.SetParamNorm (pn, 0, 0.5f)); CHECK (fabsf (B.Block (pn).V[0]) < 1e-4f);
	// tape echo: parameter 0 = time 20..1000 ms, logarithmic scale
	OK (B.SetParamNorm (dl, 0, 0.5f)); NEAR (B.Block (dl).V[0], 20.0 * sqrt (50.0), 1.0);
	OK (B.SetParamNorm (dl, 0, 0.0f)); NEAR (B.Block (dl).V[0], 20.0, 0.1);
	OK (B.SetParamNorm (dl, 0, 1.0f)); NEAR (B.Block (dl).V[0], 1000.0, 1.0);
	// linear: tape echo parameter 1 = repeats 0..100
	OK (B.SetParamNorm (dl, 1, 0.25f)); NEAR (B.Block (dl).V[1], 25.0, 0.6);
	// choice: tremolo shape 0..2
	const int tr = Add (B, "tremolo", 5, 5);
	OK (B.SetParamNorm (tr, 2, 1.0f)); CHECK (B.Block (tr).V[2] == 2.0f);
	OK (B.SetParamNorm (tr, 2, 0.5f)); CHECK (B.Block (tr).V[2] == 1.0f);
	CHECK (B.SetParamNorm (999, 0, 0.5f) != 0 && B.SetParamNorm (tr, 40, 0.5f) != 0);
	OK (B.SetParamNorm (tr, 0, -5.0f)); NEAR (B.Block (tr).V[0], 0.1, 0.01);		// out of limit: truncated
	OK (B.SetParamNorm (tr, 0, 7.0f)); NEAR (B.Block (tr).V[0], 20.0, 0.01);

	// map in the preset: CC per block, controllers with links
	CHECK (B.Midi ().SetBlockCc (50, ts) && B.Midi ().SetBlockCc (51, dl));
	B.Midi ().Ctl (0).Cc = 40;
	B.Midi ().Ctl (0).Link[0].Block = dl; B.Midi ().Ctl (0).Link[0].Param = 1; B.Midi ().Ctl (0).Link[0].MinPct = 10.0f; B.Midi ().Ctl (0).Link[0].MaxPct = 90.0f;
	B.Midi ().Ctl (2).Cc = 99;
	B.Midi ().Ctl (2).Link[1].Block = nam; B.Midi ().Ctl (2).Link[1].Param = 0; B.Midi ().Ctl (2).Link[1].MinPct = 0.0f; B.Midi ().Ctl (2).Link[1].MaxPct = 100.0f;
	static char Text[32768];
	const unsigned n = B.Serialize (Text, sizeof Text);
	CHECK (n > 0 && strstr (Text, "\nx 50 ") != 0 && strstr (Text, "\ny 0 40\n") != 0 && strstr (Text, "\nz 0 0 ") != 0 && strstr (Text, "\ny 2 ") == 0 && strstr (Text, "\nz 2 1 ") != 0);		// global controller 3: the source is not in the preset, the links are
	static CBoard C;
	C.SetMode (2);
	CHECK (C.Parse (Text));
	CHECK (C.Midi ().BlockCcCount () == 2 && C.Midi ().Ctl (0).Cc == 40 && C.Midi ().Ctl (2).Cc == 0);
	CHECK (C.Midi ().Ctl (0).Link[0].Block == dl && C.Midi ().Ctl (0).Link[0].Param == 1);
	NEAR (C.Midi ().Ctl (0).Link[0].MinPct, 10.0, 0.01); NEAR (C.Midi ().Ctl (0).Link[0].MaxPct, 90.0, 0.01);
	CHECK (C.Midi ().Ctl (2).Link[1].Block == nam && C.Midi ().Ctl (2).Link[0].Block == -1);
	// stable serialization
	static char Text2[32768];
	CHECK (C.Serialize (Text2, sizeof Text2) == n && strcmp (Text, Text2) == 0);
	// the MIDI commands drive the document
	TMidiAct act[8];
	TMidiMsg m = {MidiControl, 0, 40, 127};
	const unsigned na = C.Midi ().Handle (m, act, 8);
	CHECK (na == 1 && act[0].Type == ActParam && act[0].A == dl && act[0].B == 1);
	OK (C.SetParamNorm (act[0].A, (unsigned) act[0].B, act[0].F)); NEAR (C.Block (dl).V[1], 90.0, 0.6);
	// deleting a block removes its CCs and its links
	OK (C.NetRemove (dl));
	CHECK (C.Midi ().BlockCcCount () == 1 && C.Midi ().Ctl (0).Link[0].Block == -1);
	// a preset with no MIDI rows is read and has an empty map
	static CBoard D;
	D.SetMode (2);
	D.Reset ();
	Add (D, "vol", 1, 1);
	static char Text3[32768];
	D.Serialize (Text3, sizeof Text3);
	CHECK (strstr (Text3, "\nx ") == 0 && strstr (Text3, "\ny ") == 0 && strstr (Text3, "\nz ") == 0);
	CHECK (C.Parse (Text3) && C.Midi ().BlockCcCount () == 0 && C.Midi ().Ctl (0).Cc == 0);
	// MIDI rows for blocks that do not exist: ignored without rejecting the preset
	char Bad[40000];
	strcpy (Bad, Text3);
	strcat (Bad, "x 60 39\ny 1 70\nz 1 0 39 0 0.0 100.0\nz 9 9 9 9 0 0\nx 20 0\ny 1 20\n");
	CHECK (C.Parse (Bad) && C.Midi ().BlockCcCount () == 0 && C.Midi ().Ctl (1).Cc == 70 && C.Midi ().Ctl (1).Link[0].Block == -1);
	// ADELMatrix (tests only): no first block of a class (ADELGrid only)
	static CBoard F;
	F.SetMode (1);
	F.Reset ();
	CHECK (F.MidiFindClass (McAmp) == -1 && F.MidiClassMask () == 0);

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
