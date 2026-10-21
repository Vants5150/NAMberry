// test_net.cpp - test on the build Pi (Linux) of the network engine (ADELMatrix / ADELGrid): document, links, L/R entities, calculation
// g++ -std=c++17 -O1 -Ihost_shim -I<circle>/include -I. -Iadel test_net.cpp board.cpp graph.cpp fx*.cpp -o test_net
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "params.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *err_ = (e); if (err_) { printf("unexpected error line %d: %s\n", __LINE__, err_); fails++; } } while (0)
#define ERR(e) do { const char *err_ = (e); if (!err_) { printf("expected error line %d\n", __LINE__); fails++; } } while (0)
#define NEAR(a, b) do { if (fabsf ((a) - (b)) > 1e-4f) { printf ("FAILED line %d: %g != %g\n", __LINE__, (double) (a), (double) (b)); fails++; } } while (0)

static float Lin (float db) { return powf (10.0f, db / 20.0f); }

// computes an 8-frame block with constant input L = fl, R = fr and returns the outputs
static void Run (CBoard &B, float fl, float fr, float *pL, float *pR)
{
	static TPlan P;
	adelnam::Instance *pI[BD_MAX_NAM] = {0};
	B.Compile (&P, pI, 1.0f, FALSE);
	float inL[8], inR[8], oL[8], oR[8];
	for (int j = 0; j < 8; j++)
	{
		inL[j] = fl;
		inR[j] = fr;
	}
	GraphResetSmoothing ();		// in the tests the levels take the plan's values immediately (in operation they fade in about 3 ms)
	GraphProcess (&P, inL, inR, oL, oR, 8);
	*pL = oL[5];
	*pR = oR[5];
}

int main ()
{
	float oL, oR;
	int a, b, c;
	const int IN = BD_NET_IN, OUT = BD_NET_OUT;

	// ---- ADELGrid (6 rows): Input L (2,0), Input R (3,0), Output L (2,7), Output R (3,7). No automatic link from IN or towards OUT
	static CBoard G;
	G.SetMode (2);
	G.Reset ();
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.0f); NEAR (oR, 0.0f);				// without links nothing plays, not even the direct signal
	OK (G.NetAdd ("vol", 2, 1, "", &a));
	OK (G.SetValue (a, "vol", -6.0f));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.0f); NEAR (oR, 0.0f);				// the block has no links: it does not play
	OK (G.NetLink (IN, a, 1, 0, 0.0f));				// Input L -> A
	OK (G.NetLink (a, OUT, 0, 1, 0.0f));				// A -> Output L
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6)); NEAR (oR, 0.0f);
	ERR (G.NetAdd ("vol", 2, 1, "", &b));				// cell occupied
	ERR (G.NetAdd ("vol", 2, 0, "", &b));				// Input L is there
	ERR (G.NetAdd ("vol", 3, BD_GRID_COLS - 1, "", &b));		// Output R is there (last column)
	ERR (G.NetAdd ("vol", 6, 0, "", &b));				// outside the grid
	ERR (G.NetAdd ("split", 4, 0, "", &b));			// no Split

	// the inputs and outputs move to free cells (staying to the left/right of the linked blocks)
	ERR (G.NetMoveIo (2, 2, 1));					// cell occupied by a block
	OK (G.NetMoveIo (2, 2, 5));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6));
	OK (G.NetMoveIo (2, 4, 4));
	ERR (G.NetMoveIo (2, 4, 0));					// column 0: it is not to the right of A (column 1)
	OK (G.NetMoveIo (2, 2, 9));

	// second row: Input R -> B -> Output R, plus the diagonal A(2,1) -> B(3,3) at -6 dB
	OK (G.NetAdd ("vol", 3, 3, "", &b));
	OK (G.SetValue (b, "vol", 0.0f));
	OK (G.NetLink (IN, b, 2, 0, 0.0f));
	OK (G.NetLink (b, OUT, 0, 2, 0.0f));
	OK (G.NetLink (a, b, 0, 0, -6.0f));
	ERR (G.NetLink (b, a, 0, 0, 0.0f));				// towards the left: no
	ERR (G.NetLink (a, a, 0, 0, 0.0f));
	ERR (G.NetLink (a, 77, 0, 0, 0.0f));
	ERR (G.NetLink (a, b, 3, 0, 0.0f));
	ERR (G.NetLink (OUT, a, 0, 0, 0.0f));
	ERR (G.NetLink (a, OUT, 0, 0, 0.0f));				// the output has two channels: port 1 or 2 is needed
	ERR (G.NetLink (IN, a, 0, 0, 0.0f));				// and so does the input
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6));
	NEAR (oR, 0.25f + 0.5f * Lin (-6) * Lin (-6));			// R + A's row passed through the diagonal
	// Input L also enters B
	OK (G.NetLink (IN, b, 1, 0, -6.0f));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oR, 0.25f + 0.5f * Lin (-6) * Lin (-6) + 0.5f * Lin (-6));
	ERR (G.NetMoveIo (0, 2, 5));					// Input L cannot be to the right of B (column 3)
	OK (G.NetUnlink (IN, b, 1, 0));
	ERR (G.NetUnlink (IN, b, 1, 0));
	OK (G.NetUnlink (a, b, 0, 0));
	// from A also to Output R, at -3 dB
	OK (G.NetLink (a, OUT, 0, 2, -3.0f));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oR, 0.25f + 0.5f * Lin (-6) * Lin (-3));
	OK (G.NetUnlink (a, OUT, 0, 2));
	// the links made from IN and towards OUT are deleted like the others: without the input link A is silent
	OK (G.NetUnlink (IN, a, 1, 0));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.0f);
	OK (G.NetLink (IN, a, 1, 0, 0.0f));

	// no automatic link between neighbouring blocks in a row (A at (2,1) and D at (2,4))
	int d;
	OK (G.NetAdd ("vol", 2, 4, "", &d));
	OK (G.SetValue (d, "vol", -6.0f));
	OK (G.NetUnlink (a, OUT, 0, 1));
	OK (G.NetLink (d, OUT, 0, 1, 0.0f));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.0f);						// D does not receive from A: silent
	OK (G.NetLink (a, d, 0, 0, 0.0f));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6) * Lin (-6));			// IN L -> A -> D -> Output L
	OK (G.NetRemove (d));
	OK (G.NetLink (a, OUT, 0, 1, 0.0f));

	// type change: the block stays in the cell and keeps its links (Volume -> Pan: the Pan is stereo, center = no level change)
	OK (G.NetRetype (a, "pan", ""));
	CHECK (G.Block (a).Type == NodePan && G.Block (a).A == 2 && G.Block (a).B == 1);
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f * 0.70710678f);					// Pan at center: -3 dB on the channel
	ERR (G.NetRetype (a, "split", ""));
	ERR (G.NetRetype (a, "nonesiste", ""));
	ERR (G.NetRetype (77, "vol", ""));
	OK (G.NetRetype (a, "vol", ""));
	OK (G.SetValue (a, "vol", -6.0f));
	Run (G, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6));

	// vertical link: A (2,1) -> E (4,1) in the same column; the return would close a loop
	int e;
	OK (G.NetAdd ("vol", 4, 1, "", &e));
	OK (G.NetLink (a, e, 0, 0, 0.0f));
	ERR (G.NetLink (e, a, 0, 0, 0.0f));
	OK (G.NetUnlink (a, e, 0, 0));
	OK (G.NetLink (e, a, 0, 0, 0.0f));				// the other way (upwards) is fine if it does not close loops
	ERR (G.NetLink (a, e, 0, 0, 0.0f));
	OK (G.NetRemove (e));

	// Routing: a 0 dB Volume that routes (V1 = 1)
	int rt;
	OK (G.NetAdd ("rout", 4, 3, "", &rt));
	CHECK (G.Block (rt).Type == NodeVolume && G.Block (rt).V[1] > 0.5f && G.Block (rt).V[0] == 0.0f);
	OK (G.NetRetype (rt, "vol", ""));
	CHECK (G.Block (rt).Type == NodeVolume && G.Block (rt).V[1] < 0.5f);
	OK (G.NetRetype (rt, "rout", ""));
	CHECK (G.Block (rt).V[1] > 0.5f);
	OK (G.NetRemove (rt));

	// moving a cell: the column rules
	OK (G.NetLink (a, b, 0, 0, 0.0f));
	ERR (G.NetMove (b, 4, 0));					// column 0 is not to the right of A (column 1)
	OK (G.NetMove (b, 4, 5));
	CHECK (G.Block (b).A == 4 && G.Block (b).B == 5);
	OK (G.NetRemove (b));
	CHECK (G.LinkCount () == 2);					// IN -> A and A -> Output L remain: B's disappear with B

	// ---- saving and re-reading (Grid)
	OK (G.NetAdd ("pan", 5, 6, "", &c));
	OK (G.NetLink (a, c, 1, 2, -3.5f));
	OK (G.NetLink (IN, c, 2, 0, -1.0f));
	OK (G.NetLink (c, OUT, 0, 1, -9.0f));
	static char Text[16384], Text2[16384];
	const unsigned len = G.Serialize (Text, sizeof Text);
	CHECK (len > 0 && Text[0] == 'm' && Text[2] == '2');
	CHECK (strstr (Text, "o 2 0 3 0 2 9 3 11") != 0);		// Output L moved to column 9, Output R stays in the last (11)
	static CBoard G2;
	G2.SetMode (2);
	CHECK (G2.Parse (Text));
	G2.Serialize (Text2, sizeof Text2);
	CHECK (strcmp (Text, Text2) == 0);
	CHECK (G2.LinkCount () == G.LinkCount () && G2.LinkCount () == 5);
	static CBoard M0;
	M0.SetMode (1);
	CHECK (!M0.Parse (Text));					// e Matrix pure
	// a link towards the left in the text is rejected
	char Bad[16384];
	snprintf (Bad, sizeof Bad, "%sk %d %d 0 0 0.00\n", Text, c, a);
	CHECK (!G2.Parse (Bad));
	snprintf (Bad, sizeof Bad, "%sk -1 %d 0 0 0.00\n", Text, a);	// from the input without the L or R port
	CHECK (!G2.Parse (Bad));

	// ---- ADELMatrix: Input L and R rows, Output L and R columns
	static CBoard H;
	H.SetMode (1);
	H.Reset ();
	int v;
	OK (H.NetAdd ("vol", 0, 0, "", &v));
	OK (H.SetValue (v, "vol", 0.0f));
	OK (H.NetLink (IN, v, 1, 0, 0.0f));				// Input L -> block (on both ports)
	OK (H.NetLink (v, OUT, 0, 1, 0.0f));				// block -> Output L
	Run (H, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f); NEAR (oR, 0.0f);
	OK (H.NetLink (v, OUT, 0, 2, -6.0f));				// and at -6 dB also to Output R
	Run (H, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f); NEAR (oR, 0.5f * Lin (-6));
	OK (H.NetLink (IN, OUT, 2, 1, 0.0f));				// Input R -> Output L direct
	Run (H, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f + 0.25f);
	OK (H.NetUnlink (IN, OUT, 2, 1));
	// block ports: Input L only on the block's R port; the block's R port goes to Output L
	OK (H.NetUnlink (IN, v, 1, 0));
	OK (H.NetLink (IN, v, 1, 2, 0.0f));
	OK (H.NetUnlink (v, OUT, 0, 1));
	OK (H.NetUnlink (v, OUT, 0, 2));
	OK (H.NetLink (v, OUT, 2, 1, 0.0f));				// block R port -> Output L
	OK (H.NetLink (v, OUT, 1, 2, 0.0f));				// block L port -> Output R (empty)
	Run (H, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.5f); NEAR (oR, 0.0f);
	// stereo into a port: sum of the two channels
	OK (H.NetUnlink (IN, v, 1, 2));
	OK (H.NetUnlink (v, OUT, 2, 1));
	OK (H.NetUnlink (v, OUT, 1, 2));
	OK (H.NetLink (IN, v, 1, 1, 0.0f));				// L -> block L port
	OK (H.NetLink (IN, v, 2, 2, 0.0f));				// R -> block R port
	OK (H.NetLink (v, OUT, 0, 1, 0.0f));				// stereo block -> Output L: (L+R)/2
	Run (H, 0.5f, 0.25f, &oL, &oR);
	NEAR (oL, 0.375f); NEAR (oR, 0.0f);

	// ---- Matrix: loops rejected, calculation order, list order
	H.Reset ();
	int p, q, nr;
	OK (H.NetAdd ("vol", 0, -1, "", &p));
	OK (H.NetAdd ("vol", 0, -1, "", &q));
	OK (H.NetAdd ("vol", 0, 1, "", &nr));				// in second position
	CHECK (H.Block (p).B == 0 && H.Block (nr).B == 1 && H.Block (q).B == 2);
	OK (H.NetMove (q, 0, 0));
	CHECK (H.Block (q).B == 0 && H.Block (p).B == 1 && H.Block (nr).B == 2);
	OK (H.NetLink (IN, p, 1, 0, 0.0f));
	OK (H.NetLink (p, q, 0, 0, 0.0f));
	OK (H.NetLink (q, nr, 0, 0, 0.0f));
	ERR (H.NetLink (nr, p, 0, 0, 0.0f));				// would close a loop
	ERR (H.NetLink (nr, q, 0, 0, 0.0f));
	OK (H.NetLink (nr, OUT, 0, 1, 0.0f));
	OK (H.NetLink (nr, OUT, 0, 2, 0.0f));
	OK (H.SetValue (p, "vol", -6.0f));
	OK (H.SetValue (q, "vol", -6.0f));
	OK (H.SetValue (nr, "vol", 6.0f));
	Run (H, 0.5f, 0.0f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6)); NEAR (oR, 0.5f * Lin (-6));		// the gains multiply along the links, not in the order of the list
	// two parallel paths from the same source and different levels
	OK (H.NetLink (IN, nr, 1, 0, -20.0f));
	Run (H, 0.5f, 0.0f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-6) + 0.5f * Lin (-20) * Lin (6));
	// block off: the signal passes unchanged
	OK (H.SetValue (nr, "on", 0.0f));
	Run (H, 0.5f, 0.0f, &oL, &oR);
	NEAR (oL, 0.5f * Lin (-12) + 0.5f * Lin (-20));

	// ---- saving and re-reading (Matrix)
	const unsigned len2 = H.Serialize (Text, sizeof Text);
	CHECK (len2 > 0 && Text[0] == 'm' && Text[2] == '1');
	static CBoard H2;
	H2.SetMode (1);
	CHECK (H2.Parse (Text));
	H2.Serialize (Text2, sizeof Text2);
	CHECK (strcmp (Text, Text2) == 0);
	// loop in the text: rejected
	snprintf (Bad, sizeof Bad, "%sk %d %d 0 0 0.00\n", Text, nr, p);
	CHECK (!H2.Parse (Bad));
	CHECK (H2.LinkCount () == H.LinkCount ());			// the refusal does not ruin the pedalboard

	// ---- estimated load limit in ADELGrid (75%): each NAM costs 21.5% (processor at 1500 MHz; with CPU_BOOST it would be 13.5%); each reverb +9%
	static CBoard L;
	L.SetMode (2);
	L.Reset ();
	int ids[16], n = 0;
	for (int i = 0; i < 3; i++)
	{
		OK (L.NetAdd ("nam", i, 1, "x.adn", &ids[n++]));
	}
	CHECK (L.EstimateLoad10 () == 7 + 3 * 215);
	ERR (L.NetAdd ("nam", 3, 1, "x.adn", &ids[n]));				// a fourth NAM: 86.7 % > 75 %, rejected
	OK (L.NetAdd ("adelverb", 0, 3, "", &ids[n++]));			// 74,2 %
	ERR (L.NetAdd ("adelverb", 1, 3, "", &ids[n]));				// 83.2 % > 75 %: rejected
	OK (L.NetAdd ("adelequal", 3, 3, "", &ids[n++]));			// a small block (0.5 %) still fits
	ERR (L.NetRetype (ids[n - 1], "adelverb", ""));			// from equalizer to reverb: exceeds the limit
	OK (L.SetValue (ids[3], "on", 0.0f));					// turning the reverb off frees the load
	CHECK (L.EstimateLoad10 () == 7 + 3 * 215 + 5);
	OK (L.NetAdd ("adelverb", 4, 3, "", &ids[n++]));
	ERR (L.SetValue (ids[3], "on", 1.0f));					// turning the first back on would exceed the limit

	// ---- the blocks' cost scales with the processor frequency (measured at 1500 MHz)
	CHECK (CBoard::NodeCost10 (NodeNam) == 215);
	CBoard::SetCpuMhz (2400);
	CHECK (CBoard::NodeCost10 (NodeNam) == 135);		// 215 x 1500 / 2400 = 134.4 rounded up
	{
		static CBoard F;
		F.SetMode (2);
		F.Reset ();
		int fi[8], fk = 0;
		for (int i = 0; i < 5; i++)
		{
			OK (F.NetAdd ("nam", i, 1, "x.adn", &fi[fk++]));		// at 2400 MHz 5 fit (68.2 %)
		}
		CHECK (F.EstimateLoad10 () == 7 + 5 * 135);
	}
	CBoard::SetCpuMhz (1500);
	CHECK (CBoard::NodeCost10 (NodeNam) == 215);

	// ---- Selector (A/B/C): the outgoing links are the branches (from top to bottom); only the chosen branch passes; when off all pass
	{
		static CBoard S;
		S.SetMode (2);
		S.Reset ();
		int sl, va, vb, vc;
		OK (S.NetAdd ("selector", 2, 1, "", &sl));
		OK (S.NetAdd ("vol", 4, 3, "", &vc));				// C: the lowest row (added first: the creation order does not matter)
		OK (S.NetAdd ("vol", 1, 3, "", &va));				// A: the topmost row
		OK (S.NetAdd ("vol", 3, 3, "", &vb));				// B
		OK (S.SetValue (va, "vol", -6.0f)); OK (S.SetValue (vb, "vol", -12.0f)); OK (S.SetValue (vc, "vol", 0.0f));
		OK (S.NetLink (IN, sl, 1, 0, 0.0f));
		OK (S.NetLink (sl, vc, 0, 0, 0.0f)); OK (S.NetLink (sl, vb, 2, 0, 0.0f)); OK (S.NetLink (sl, va, 1, 0, 0.0f));		// Selector ports: A = 1, B = 2, C = 0
		OK (S.NetLink (va, OUT, 0, 1, 0.0f)); OK (S.NetLink (vb, OUT, 0, 1, 0.0f)); OK (S.NetLink (vc, OUT, 0, 1, 0.0f));
		const float want[3] = {0.5f * Lin (-6), 0.5f * Lin (-12), 0.5f};
		for (int sel = 0; sel < 3; sel++)
		{
			OK (S.SetValue (sl, "sel", (float) sel));
			Run (S, 0.5f, 0.0f, &oL, &oR);
			NEAR (oL, want[sel]);
		}
		OK (S.SetValue (sl, "sel", 7.0f));					// the parameter is limited to C
		CHECK (S.Block ((unsigned) sl).V[0] == 2.0f);
		OK (S.SetValue (sl, "on", 0.0f));					// off: all branches pass
		Run (S, 0.5f, 0.0f, &oL, &oR);
		NEAR (oL, want[0] + want[1] + want[2]);
		OK (S.SetValue (sl, "on", 1.0f));
		// input trim of the guitar profile: -6.02 dB halves the output, 0 dB restores it
			OK (S.SetValue (sl, "sel", 0.0f)); CBoard::SetInTrim (-6.0206f); Run (S, 0.5f, 0.0f, &oL, &oR); NEAR (oL, want[0] * 0.5f); CBoard::SetInTrim (0.0f); Run (S, 0.5f, 0.0f, &oL, &oR); NEAR (oL, want[0]);
			// Outputs = AB: choice C falls back to B
			OK (S.SetValue (sl, "outs", 0.0f)); OK (S.SetValue (sl, "sel", 2.0f)); Run (S, 0.5f, 0.0f, &oL, &oR); NEAR (oL, want[1]);
			OK (S.SetValue (sl, "outs", 1.0f));
			// port C with no link = silence
		OK (S.NetUnlink (sl, vc, 0, 0));
		OK (S.SetValue (sl, "sel", 2.0f));
		Run (S, 0.5f, 0.0f, &oL, &oR);
		NEAR (oL, 0.0f);
		// fade: changing branch without resetting the state, the level does not jump abruptly
		{
			static TPlan P;
			adelnam::Instance *pI[BD_MAX_NAM] = {0};
			OK (S.SetValue (sl, "sel", 0.0f));
			S.Compile (&P, pI, 1.0f, FALSE);
			float inL[8], inR[8], oL8[8], oR8[8];
			for (int j = 0; j < 8; j++) { inL[j] = 0.5f; inR[j] = 0.0f; }
			GraphResetSmoothing ();
			for (int rep = 0; rep < 400; rep++) GraphProcess (&P, inL, inR, oL8, oR8, 8);
			NEAR (oL8[7], want[0]);
			OK (S.SetValue (sl, "sel", 1.0f));
			S.Compile (&P, pI, 1.0f, FALSE);
			GraphProcess (&P, inL, inR, oL8, oR8, 8);
			CHECK (oL8[7] > want[1] + 0.01f && oL8[7] < want[0] - 0.003f);		// halfway: neither the old nor the new level
			for (int rep = 0; rep < 600; rep++) GraphProcess (&P, inL, inR, oL8, oR8, 8);
			NEAR (oL8[7], want[1]);
		}
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
