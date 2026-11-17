// test_bypass.cpp - bypass behaviours (Thru, Mute FX In, Mute FX Out, Mute In, Mute Out) of an effect block
// Run with tests/run_host_tests.sh
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "fx.h"
#include "params.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *err_ = (e); if (err_) { printf("unexpected error line %d: %s\n", __LINE__, err_); fails++; } } while (0)

// a block is processed with constant input; returns the output of the last frame of the last block
static float Blocks (CBoard &B, CFx *pFx, int a, float fIn, int nBlocks, float *pFirst = 0)
{
	static TPlan P;
	CFx *ppFx[PL_MAX_NODES] = {0};
	ppFx[a] = pFx;
	adelnam::Instance *pI[BD_MAX_NAM] = {0};
	float fLast = 0.0f;
	for (int k = 0; k < nBlocks; k++)
	{
		B.Compile (&P, pI, 1.0f, FALSE, ppFx);
		float inL[64], inR[64], oL[64], oR[64];
		for (int j = 0; j < 64; j++)
		{
			inL[j] = fIn;
			inR[j] = 0.0f;
		}
		GraphResetSmoothing ();
		GraphProcess (&P, inL, inR, oL, oR, 64);
		if (pFirst != 0)
		{
			for (int j = 0; j < 64; j++) { if (fabsf (oL[j]) > *pFirst) *pFirst = fabsf (oL[j]); }
		}
		fLast = oL[63];
	}
	return fLast;
}

// runs the scenario for one mode: the block works, then it is bypassed
static void Scenario (int nMode, float *pTailFirst, float *pDryLate)
{
	static CBoard G;
	G.SetMode (2);
	G.Reset ();
	int a;
	OK (G.NetAdd ("adeldelay", 2, 1, "", &a));
	OK (G.NetLink (BD_NET_IN, a, 1, 0, 0.0f));
	OK (G.NetLink (a, BD_NET_OUT, 0, 1, 0.0f));
	OK (G.SetValue (a, "mix", 100.0f));
	OK (G.SetValue (a, "time", 10.0f));
	OK (G.SetValue (a, "feedback", 30.0f));
	CFx *pFx = FxCreate (G.Block ((unsigned) a).Type, 48000.0f);
	CHECK (pFx != 0);
	Blocks (G, pFx, a, 0.5f, 60);					// the effect works: the repeats are building up
	OK (G.SetValue (a, "byp", (float) nMode));
	OK (G.SetValue (a, "on", 0.0f));
	*pTailFirst = 0.0f;
	Blocks (G, pFx, a, 0.0f, 16, pTailFirst);			// the 16 blocks after the bypass, silent input: peak of the tails
	*pDryLate = Blocks (G, pFx, a, 0.5f, 300);			// much later, constant input: the dry and no tails left
	delete pFx;
}

int main ()
{
	float tail[BYP_COUNT], late[BYP_COUNT];
	for (int m = 0; m < BYP_COUNT; m++)
	{
		Scenario (m, &tail[m], &late[m]);
		printf ("mode %d: peak of the tails after = %.4f, output 300 blocks later = %.4f\n", m, (double) tail[m], (double) late[m]);
	}
	CHECK (fabsf (tail[BYP_THRU]) < 1e-6f && fabsf (late[BYP_THRU] - 0.5f) < 1e-4f);			// Thru: nothing but the dry, the tails stop at once
	CHECK (fabsf (tail[BYP_MUTE_OUT]) < 1e-6f && fabsf (late[BYP_MUTE_OUT]) < 1e-6f);		// Mute Out: silence
	CHECK (fabsf (tail[BYP_MUTE_FX_OUT]) < 1e-6f && fabsf (late[BYP_MUTE_FX_OUT] - 0.5f) < 1e-4f);	// Mute FX Out: only the dry
	CHECK (tail[BYP_MUTE_FX_IN] > 0.01f && fabsf (late[BYP_MUTE_FX_IN] - 0.5f) < 1e-3f);		// Mute FX In: the tails ring out, then only the dry
	CHECK (tail[BYP_MUTE_IN] > 0.01f && fabsf (late[BYP_MUTE_IN]) < 1e-3f);				// Mute In: the tails ring out, no dry
	CBoard G2;
	G2.SetMode (2);
	G2.Reset ();
	int v;
	OK (G2.NetAdd ("vol", 2, 1, "", &v));
	CHECK (G2.SetValue (v, "byp", 9.0f) != 0);							// out of range
	if (fails == 0)
	{
		printf ("test_bypass: OK\n");
	}
	return fails != 0;
}
