// test_namtone.cpp - Bass/Mid/Treble of the NAM block: frequency response of the filter, state, and integration in the document (values, limits, MIDI, saving, plan)
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Ihost_shim -I<circle>/include -I. test_namtone.cpp board.cpp ... (see hosttests.sh)
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "namtone.h"
#include "fx_defs.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *err_ = (e); if (err_) { printf ("unexpected error line %d: %s\n", __LINE__, err_); fails++; } } while (0)
#define NEAR(a, b, t) do { if (fabs ((double) (a) - (double) (b)) > (t)) { printf ("FAILED line %d: %g != %g\n", __LINE__, (double) (a), (double) (b)); fails++; } } while (0)

// gain in dB of the filter at a frequency: a 0.5 sine passed through the filter, steady-state amplitude
static double GainDb (const float *pC, u8 nMask, double f)
{
	float z[NT_STAGES][2] = {{0, 0}, {0, 0}, {0, 0}};
	const unsigned N = 48000;
	static float buf[48000];
	for (unsigned i = 0; i < N; i++) buf[i] = (float) (0.5 * sin (6.283185307 * f * i / 48000.0));
	// in blocks of 4 frames as on the board
	for (unsigned i = 0; i < N; i += 4) NamToneRun (pC, nMask, z, buf + i, 4);
	double pk = 0;
	for (unsigned i = N - 9600; i < N; i++) pk = fmax (pk, fabs (buf[i]));
	return 20.0 * log10 (pk / 0.5);
}

static int Add (CBoard &B, const char *type, int r, int c)
{
	int id = -1;
	const char *e = B.NetAdd (type, r, c, "x.adn", &id);
	if (e) { printf ("NetAdd %s: %s\n", type, e); fails++; }
	return id;
}

int main ()
{
	setvbuf (stdout, 0, _IONBF, 0);
	float C[NT_COEF];
	// ---- filter: flat, one control at a time, cuts
	CHECK (NamToneSetup (C, 0, 0, 0, 48000.0f) == 0);
	CHECK (NamToneSetup (C, 0.04f, -0.03f, 0.0f, 48000.0f) == 0);		// below the threshold: off
	u8 m = NamToneSetup (C, 12.0f, 0, 0, 48000.0f);
	CHECK (m == 1);
	NEAR (GainDb (C, m, 30.0), 12.0, 1.0);			// very low bass: almost all the gain
	NEAR (GainDb (C, m, 110.0), 6.0, 1.0);			// at half the shelf: half the gain
	NEAR (GainDb (C, m, 3000.0), 0.0, 0.5);
	m = NamToneSetup (C, 0, 12.0f, 0, 48000.0f);
	CHECK (m == 2);
	NEAR (GainDb (C, m, 650.0), 12.0, 0.3);
	NEAR (GainDb (C, m, 60.0), 0.0, 1.5);
	NEAR (GainDb (C, m, 8000.0), 0.0, 1.5);
	m = NamToneSetup (C, 0, 0, 12.0f, 48000.0f);
	CHECK (m == 4);
	NEAR (GainDb (C, m, 12000.0), 12.0, 1.2);
	NEAR (GainDb (C, m, 3200.0), 6.0, 1.0);
	NEAR (GainDb (C, m, 200.0), 0.0, 0.5);
	m = NamToneSetup (C, -12.0f, -12.0f, -12.0f, 48000.0f);
	CHECK (m == 7);
	CHECK (GainDb (C, m, 40.0) < -10.0 && GainDb (C, m, 650.0) < -10.0 && GainDb (C, m, 12000.0) < -10.0);
	// out of range: truncated to +-12 dB
	m = NamToneSetup (C, 99.0f, 0, 0, 48000.0f);
	NEAR (GainDb (C, m, 30.0), 12.0, 1.0);
	// mask 0: signal unchanged bit for bit
	{
		float z[NT_STAGES][2] = {{1, 2}, {3, 4}, {5, 6}};
		float x[4] = {0.1f, -0.2f, 0.3f, -0.4f}, y[4];
		memcpy (y, x, sizeof x);
		NamToneRun (C, 0, z, y, 4);
		CHECK (memcmp (x, y, sizeof x) == 0 && z[0][0] == 0.0f && z[1][1] == 0.0f && z[2][0] == 0.0f);		// the state is also reset
	}
	// stability: white noise at +12 on everything for one second, without blowing up or NaN
	{
		m = NamToneSetup (C, 12, 12, 12, 48000.0f);
		float z[NT_STAGES][2] = {{0, 0}, {0, 0}, {0, 0}};
		unsigned seed = 1;
		double mx = 0;
		for (unsigned b = 0; b < 12000; b++)
		{
			float x[4];
			for (int k = 0; k < 4; k++) { seed = seed * 1664525u + 1013904223u; x[k] = ((int) (seed >> 8) / 8388608.0f - 1.0f) * 0.5f; }
			NamToneRun (C, m, z, x, 4);
			for (int k = 0; k < 4; k++) { if (!std::isfinite (x[k])) { CHECK (false); b = 12000; break; } mx = fmax (mx, fabs (x[k])); }
		}
		CHECK (mx < 20.0);
	}

	// ---- document: values, limits, MIDI, saving, plan
	static CBoard B;
	B.SetMode (2);
	B.Reset ();
	const int nam = Add (B, "nam", 2, 4);
	CHECK (B.Block (nam).V[2] == 0.0f && B.Block (nam).V[3] == 0.0f && B.Block (nam).V[4] == 0.0f);	// new block: flat
	OK (B.SetValue (nam, "bass", 5.5f));
	OK (B.SetValue (nam, "mid", -3.0f));
	OK (B.SetValue (nam, "treb", 40.0f));		// truncated to +12
	CHECK (B.Block (nam).V[2] == 5.5f && B.Block (nam).V[3] == -3.0f && B.Block (nam).V[4] == 12.0f);
	OK (B.SetValue (nam, "treb", -99.0f));
	CHECK (B.Block (nam).V[4] == -12.0f);
	CHECK (B.SetValue (nam, "tone", 1.0f) != 0);		// unknown key
	const int vol = Add (B, "vol", 4, 4);
	CHECK (B.SetValue (vol, "bass", 1.0f) != 0);		// only the NAM has tone controls
	OK (B.SetParamNorm (nam, 2, 0.0f)); CHECK (B.Block (nam).V[2] == -12.0f);
	OK (B.SetParamNorm (nam, 3, 1.0f)); CHECK (B.Block (nam).V[3] == 12.0f);
	OK (B.SetParamNorm (nam, 4, 0.5f)); NEAR (B.Block (nam).V[4], 0.0, 1e-4);
	CHECK (B.SetParamNorm (nam, 5, 0.5f) != 0);
	// the old input/output does not change
	OK (B.SetParamNorm (nam, 0, 0.5f)); NEAR (B.Block (nam).V[0], -6.0, 1e-4);

	// calibration offset per block (V5 = 1: the block does NOT use the offset; 0 = uses it, as before)
	CHECK (B.Block (nam).V[5] == 0.0f);		// new block: uses the offset
	OK (B.SetValue (nam, "noofs", 1.0f)); CHECK (B.Block (nam).V[5] == 1.0f);
	CHECK (B.SetValue (vol, "noofs", 1.0f) != 0);		// only the NAM
	OK (B.SetValue (nam, "noofs", 0.0f)); CHECK (B.Block (nam).V[5] == 0.0f);
	OK (B.SetValue (nam, "noofs", 1.0f));

	// saving and reloading: the three values stay
	OK (B.SetValue (nam, "bass", 4.0f)); OK (B.SetValue (nam, "mid", -2.5f)); OK (B.SetValue (nam, "treb", 7.0f));
	static char Text[8192], Text2[8192];
	const unsigned n = B.Serialize (Text, sizeof Text);
	static CBoard Cb;
	Cb.SetMode (2);
	CHECK (Cb.Parse (Text));
	const int cn = Cb.MidiFindClass (McAmp);
	CHECK (cn >= 0);
	NEAR (Cb.Block (cn).V[2], 4.0, 0.01); NEAR (Cb.Block (cn).V[3], -2.5, 0.01); NEAR (Cb.Block (cn).V[4], 7.0, 0.01);
	CHECK (Cb.Block (cn).V[5] == 1.0f);		// the offset exclusion is saved and re-read
	CHECK (Cb.Serialize (Text2, sizeof Text2) == n && strcmp (Text, Text2) == 0);

	// an old preset (without the tone values: V2..V4 at zero) is read flat
	{
		static CBoard D;
		D.SetMode (2);
		D.Reset ();
		const int dn = Add (D, "nam", 1, 1);
		static char T3[8192];
		D.Serialize (T3, sizeof T3);
		static CBoard E;
		E.SetMode (2);
		CHECK (E.Parse (T3));
		const int en = E.MidiFindClass (McAmp);
		CHECK (E.Block (en).V[2] == 0.0f && E.Block (en).V[3] == 0.0f && E.Block (en).V[4] == 0.0f && dn >= 0);
	}

	// plan for the callback: the coefficients and the mask reach the node
	{
		static TPlan P;
		adelnam::Instance *pI[BD_MAX_NAM] = {0};
		B.Compile (&P, pI, 1.0f, FALSE);
		const TPNode &N = P.Node[nam];
		CHECK (N.Kind == NodeNam && N.Sel == 7);
		float Cx[NT_COEF];
		NamToneSetup (Cx, 4.0f, -2.5f, 7.0f, 48000.0f);
		for (unsigned k = 0; k < NT_COEF; k++) NEAR (N.P[k], Cx[k], 1e-6);
		OK (B.SetValue (nam, "bass", 0.0f)); OK (B.SetValue (nam, "mid", 0.0f)); OK (B.SetValue (nam, "treb", 0.0f));
		B.Compile (&P, pI, 1.0f, FALSE);
		CHECK (P.Node[nam].Sel == 0);		// all at zero: no filter in the callback

		// input calibration: max input 1.5 dBu, model send 15 dBu, offset +6 -> -7.5 dB; with the offset excluded -> -13.5 dB; without a send level -> 0 dB
		B.Block (nam).HasModelIn = TRUE;
		B.Block (nam).ModelInDbu = 15.0f;
		B.Block (nam).V[0] = 0.0f;
		CBoard::SetCal (1.5f, 6.0f);
		OK (B.SetValue (nam, "noofs", 0.0f));
		B.Compile (&P, pI, 1.0f, FALSE);
		NEAR (20.0 * log10 (P.Node[nam].In), -7.5, 1e-3);
		OK (B.SetValue (nam, "noofs", 1.0f));
		B.Compile (&P, pI, 1.0f, FALSE);
		NEAR (20.0 * log10 (P.Node[nam].In), -13.5, 1e-3);
		CBoard::SetCal (1.5f, 0.0f);
		OK (B.SetValue (nam, "noofs", 0.0f));
		B.Compile (&P, pI, 1.0f, FALSE);
		NEAR (20.0 * log10 (P.Node[nam].In), -13.5, 1e-3);		// global offset 0: same result
		CBoard::SetCal (1.5f, 6.0f);
		B.Block (nam).HasModelIn = FALSE;
		B.Compile (&P, pI, 1.0f, FALSE);
		NEAR (P.Node[nam].In, 1.0, 1e-6);
	}

	// ---- atomic reorder (NetRelayout): positions only, never links or parameters
	{
		static CBoard R;
		R.SetMode (2);
		R.Reset ();
		const int a = Add (R, "nam", 1, 2), b = Add (R, "vol", 3, 4), c = Add (R, "nam", 5, 6);
		OK (R.NetLink (BD_NET_IN, a, 1, 0, 0.0f));
		OK (R.NetLink (a, b, 0, 0, 0.0f));
		OK (R.NetLink (b, c, 0, 0, 0.0f));
		OK (R.NetLink (c, BD_NET_OUT, 0, 1, 0.0f));
		const int Id[3] = {a, b, c}, Row[3] = {0, 1, 2}, Col[3] = {1, 3, 5};
		const int IoR[4] = {2, 3, 2, 3}, IoC[4] = {0, 0, 9, 9};
		OK (R.NetRelayout (Id, Row, Col, 3, IoR, IoC));
		CHECK (R.Block (a).A == 0 && R.Block (a).B == 1 && R.Block (c).A == 2 && R.Block (c).B == 5);
		CHECK (R.LinkCount () == 4);		// the links do not change
		// cell used twice: rejected and everything as it was
		const int RowD[3] = {0, 0, 2}, ColD[3] = {1, 1, 5};
		CHECK (R.NetRelayout (Id, RowD, ColD, 3, IoR, IoC) != 0);
		CHECK (R.Block (a).A == 0 && R.Block (b).A == 1 && R.Block (b).B == 3);
		// a link would go to the left: rejected and everything as it was
		const int ColL[3] = {1, 3, 0};
		CHECK (R.NetRelayout (Id, Row, ColL, 3, IoR, IoC) != 0);
		CHECK (R.Block (c).A == 2 && R.Block (c).B == 5);
		// blocks missing, or outside the grid
		CHECK (R.NetRelayout (Id, Row, Col, 2, IoR, IoC) != 0);
		const int RowO[3] = {0, 1, 9};
		CHECK (R.NetRelayout (Id, RowO, Col, 3, IoR, IoC) != 0);
		// the MIDI numbering must not change: the first NAM (column, then row) stays the same block
		const int first = R.MidiFindClass (3);
		CHECK (first == a);
		const int ColS[3] = {5, 6, 7};		// a is still upstream (column 5 against 7): it stays the first
		OK (R.NetRelayout (Id, Row, ColS, 3, IoR, IoC));
		CHECK (R.MidiFindClass (3) == a);
		// branches of a Selector = ports A/B/C: swapping the destinations' rows is allowed and does not change the branches
		static CBoard S2;
		S2.SetMode (2);
		S2.Reset ();
		const int sel = Add (S2, "selector", 2, 1), x = Add (S2, "vol", 1, 3), y = Add (S2, "vol", 4, 3);
		OK (S2.NetLink (BD_NET_IN, sel, 1, 0, 0.0f));
		OK (S2.NetLink (sel, x, 1, 0, 0.0f));
		OK (S2.NetLink (sel, y, 2, 0, 0.0f));
		OK (S2.NetLink (x, BD_NET_OUT, 0, 1, 0.0f));
		OK (S2.NetLink (y, BD_NET_OUT, 0, 2, 0.0f));
		const int Id2[3] = {sel, x, y}, Col2[3] = {1, 3, 3};
		const int RowOk[3] = {2, 0, 5};		// x stays above y: same order of the branches
		OK (S2.NetRelayout (Id2, RowOk, Col2, 3, IoR, IoC));
		const int RowSw[3] = {2, 5, 0};		// x below y: the branches stay those of the ports
		OK (S2.NetRelayout (Id2, RowSw, Col2, 3, IoR, IoC));
		CHECK (S2.Block (x).A == 5 && S2.Block (y).A == 0);
	}

	printf ("\nRISULTATO: %s\n", fails ? "tests failed" : "all tests passed");
	return fails ? 1 : 0;
}
