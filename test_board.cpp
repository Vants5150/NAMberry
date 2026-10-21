// test_board.cpp - test on the build Pi (Linux) of the pedalboard document (ADELGrid) and of the graph engine, without hardware.
// g++ -std=c++17 -O1 -I<circle>/include -I. test_board.cpp board.cpp graph.cpp -o test_board
// The links, the Selector and the network's edge cases are in test_net.cpp.
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "params.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *r = (e); if (r) { printf("unexpected error line %d: %s\n", __LINE__, r); fails++; } } while (0)
#define ERR(e) do { const char *r = (e); if (!r) { printf("expected error line %d\n", __LINE__); fails++; } } while (0)

static TPlan P;
static adelnam::Instance *pI[BD_MAX_NAM] = {0};
static float inL[8], inR[8], oL[8], oR[8];

static void Run (const CBoard &B, boolean bBypass = FALSE)
{
	B.Compile (&P, pI, 1.0f, bBypass);
	GraphResetSmoothing ();
	GraphProcess (&P, inL, inR, oL, oR, 8);
}

static void Input (float l, float r)
{
	for (int j = 0; j < 8; j++)
	{
		inL[j] = l;
		inR[j] = r;
	}
}

int main ()
{
	static CBoard B;
	int a, b, c, d;

	// construction: IN L -> vol (-6 dB) -> OUT L
	OK (B.NetAdd ("vol", 2, 2, "", &a));
	OK (B.SetValue (a, "vol", -6.0f));
	OK (B.NetLink (BD_NET_IN, a, 1, 0, 0.0f));
	OK (B.NetLink (a, BD_NET_OUT, 0, 1, 0.0f));
	CHECK (B.Block (a).Used && B.Block (a).A == 2 && B.Block (a).B == 2);
	CHECK (B.LinkCount () == 2);

	// expected errors
	ERR (B.NetAdd ("boh", 0, 4, "", &d));		// unknown type
	ERR (B.NetAdd ("split", 4, 0, "", &d));		// the Split no longer exists
	ERR (B.NetAdd ("vol", 9, 4, "", &d));		// no such cell
	ERR (B.NetAdd ("vol", 2, 2, "", &d));		// cell occupied
	ERR (B.NetRemove (35));
	ERR (B.SetValue (a, "nonexistent", 1.0f));
	ERR (B.SetValue (35, "vol", 1.0f));

	// NAM: with the estimated cost of 21.5% the last one that fits in the 75% limit is the third
	static CBoard N;
	int nam[BD_MAX_NAM + 2], n = 0;
	for (; n < 3; n++)
	{
		OK (N.NetAdd ("nam", n, 1, "test.adn", &nam[n]));
	}
	ERR (N.NetAdd ("nam", 5, 5, "test.adn", &nam[n]));
	CHECK (N.CountNam () == 3);
	OK (N.NetRemove (nam[1]));
	CHECK (N.CountNam () == 2);
	OK (N.NetAdd ("nam", 5, 5, "other.adn", &nam[1]));		// reuses the freed slot
	CHECK (N.CountNam () == 3);

	// saving and re-reading
	static char Text[8192];
	unsigned len = B.Serialize (Text, sizeof Text);
	CHECK (len > 0 && Text[0] == 'm' && Text[2] == '2');
	static CBoard B2;
	CHECK (B2.Parse (Text));
	static char Text2[8192];
	B2.Serialize (Text2, sizeof Text2);
	CHECK (strcmp (Text, Text2) == 0);
	CHECK (B2.Block (a).Used && B2.LinkCount () == 2);
	CHECK (!B2.Parse ("m 2\nd 0 99 1 -1 0 1 -1 -1 0 -1 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 -\n"));		// invalid type
	CHECK (!B2.Parse ("d 0 2 1 -1 0 1 -1 -1 0 -1 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 -\n"));		// without an 'm' row (ADELFlow preset)
	CHECK (!B2.Parse ("m 1\n"));				// text of another mode
	CHECK (B2.Block (a).Used);				// refusals do not ruin the pedalboard
	// old formats: the 'l' row (ADELFlow lane) is ignored, the 4-value 'b' is read
	CHECK (B2.Parse ("m 2\nl 0 1 0 0 5\nb 0 2 1 -1 2 3 -1 0 -1 -6.00 0.00 0.00 0.00 -\n"));
	CHECK (B2.Block (0).Used && B2.Block (0).V[0] == -6.0f && B2.Block (0).A == 2 && B2.Block (0).B == 3);
	// the next, c, par and lane fields stay in the file but do not count
	B2.Serialize (Text2, sizeof Text2);
	CHECK (strstr (Text2, "d 0 2 1 -1 2 3 -1 -1 0 -1 ") != 0);

	// calculation: 0.1 * 10^(-6/20)
	Input (0.1f, 0.0f);
	Run (B);
	const float x = 0.1f * powf (10.0f, -6.0f / 20.0f);
	printf ("output L = %.5f (expected %.5f), output R = %.5f\n", oL[3], x, oR[3]);
	CHECK (fabsf (oL[3] - x) < 2e-3f);
	CHECK (fabsf (oR[3]) < 1e-6f);		// no link towards output R

	// two parallel paths on the same output add up
	OK (B.NetAdd ("vol", 3, 2, "", &b));
	OK (B.SetValue (b, "vol", 6.0f));
	OK (B.NetLink (BD_NET_IN, b, 1, 0, 0.0f));
	OK (B.NetLink (b, BD_NET_OUT, 0, 1, 0.0f));
	Run (B);
	CHECK (fabsf (oL[3] - 0.1f * (powf (10.0f, -6.0f / 20.0f) + powf (10.0f, 6.0f / 20.0f))) < 3e-3f);

	// bypass: L su L, R su R
	Input (0.1f, 0.2f);
	Run (B, TRUE);
	CHECK (fabsf (oL[3] - 0.1f) < 1e-6f && fabsf (oR[3] - 0.2f) < 1e-6f);

	// Pan: stereo path, L and R ports out of the block
	static CBoard Pn;
	int pn;
	OK (Pn.NetAdd ("pan", 2, 2, "", &pn));
	OK (Pn.NetLink (BD_NET_IN, pn, 1, 0, 0.0f));
	OK (Pn.NetLink (pn, BD_NET_OUT, 1, 1, 0.0f));
	OK (Pn.NetLink (pn, BD_NET_OUT, 2, 2, 0.0f));
	OK (Pn.SetValue (pn, "pan", 0.0f));
	Input (0.1f, 0.0f);
	Run (Pn);
	printf ("pan center (stereo): L %.5f R %.5f (expected %.5f)\n", oL[3], oR[3], 0.1f * 0.70710678f);
	CHECK (fabsf (oL[3] - 0.0707107f) < 1e-4f && fabsf (oR[3] - 0.0707107f) < 1e-4f);
	OK (Pn.SetValue (pn, "pan", -100.0f));
	Run (Pn);
	CHECK (fabsf (oL[3] - 0.1f) < 1e-4f && fabsf (oR[3]) < 1e-4f);
	OK (Pn.SetValue (pn, "pan", 100.0f));
	Run (Pn);
	CHECK (fabsf (oL[3]) < 1e-4f && fabsf (oR[3] - 0.1f) < 1e-4f);

	// true stereo input: L on L, R on R
	static CBoard St;
	OK (St.NetLink (BD_NET_IN, BD_NET_OUT, 1, 1, 0.0f));
	OK (St.NetLink (BD_NET_IN, BD_NET_OUT, 2, 2, 0.0f));
	Input (0.1f, -0.3f);
	Run (St);
	CHECK (fabsf (oL[3] - 0.1f) < 1e-6f && fabsf (oR[3] + 0.3f) < 1e-6f);

	// mode: the 'm' row is always there, and the text of another mode is rejected
	static CBoard M1, M2;
	M1.SetMode (1);
	M2.SetMode (2);
	int mn;
	OK (M1.NetAdd ("vol", 0, 0, "", &mn));
	M1.Serialize (Text, sizeof Text);
	CHECK (Text[0] == 'm' && Text[2] == '1');
	CHECK (M1.Parse (Text));
	CHECK (!M2.Parse (Text));				// ADELMatrix text in an ADELGrid pedalboard

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
