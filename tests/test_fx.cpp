// test_fx.cpp - tests of the effects and of their integration in the document and in the graph, on Linux without hardware.
// g++ -std=c++17 -O1 -I<circle>/include -I. test_fx.cpp board.cpp graph.cpp fx.cpp -o test_fx
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "fx.h"
#include "params.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *r = (e); if (r) { printf("unexpected error line %d: %s\n", __LINE__, r); fails++; } } while (0)
#define ERR(e) do { const char *r = (e); if (!r) { printf("expected error line %d\n", __LINE__); fails++; } } while (0)

static const unsigned N = 4;

// repeatable white noise
static float Rnd (unsigned &s)
{
	s = s * 1664525u + 1013904223u;
	return ((float) (s >> 8) / 8388608.0f) - 1.0f;
}

int main ()
{
	static CBoard B;
	static TPlan P;
	adelnam::Instance *pI[BD_MAX_NAM] = {0};
	CFx *pFx[BD_MAX_BLOCKS] = {0};
	int ph;

	OK (B.NetAdd ("phaser", 2, 2, "", &ph));
	OK (B.NetLink (BD_NET_IN, ph, 1, 0, 0.0f));		// Input L -> phaser -> Output L and R
	OK (B.NetLink (ph, BD_NET_OUT, 1, 1, 0.0f));
	OK (B.NetLink (ph, BD_NET_OUT, 2, 2, 0.0f));
	CHECK (B.Block (ph).Type == FxPhaser);
	CHECK (B.Block (ph).V[0] == 0.5f && B.Block (ph).V[4] == 50.0f);
	OK (B.SetValue (ph, "rate", 2.0f));
	CHECK (B.Block (ph).V[0] == 2.0f);
	OK (B.SetValue (ph, "rate", 99.0f));
	CHECK (B.Block (ph).V[0] == 5.0f);
	OK (B.SetValue (ph, "stages", 7.0f));
	CHECK (B.Block (ph).V[3] == 8.0f || B.Block (ph).V[3] == 6.0f);
	OK (B.SetValue (ph, "stages", 6.0f));
	CHECK (B.Block (ph).V[3] == 6.0f);
	ERR (B.SetValue (ph, "vol", 1.0f));
	ERR (B.SetValue (ph, "nonesiste", 1.0f));
	ERR (B.NetAdd ("chorus", 0, 4, "", &ph));

	// saving and reading
	static char Buf[8192];
	B.Serialize (Buf, sizeof Buf);
	static CBoard B2;
	CHECK (B2.Parse (Buf));
	CHECK (B2.Block (ph).Type == FxPhaser && B2.Block (ph).V[3] == 6.0f);

	// engine: the instance must be created from outside (as the kernel does)
	OK (B.SetValue (ph, "rate", 0.5f));
	OK (B.SetValue (ph, "depth", 70.0f));
	OK (B.SetValue (ph, "fb", 40.0f));
	pFx[ph] = FxCreate (FxPhaser, 48000.0f);
	CHECK (pFx[ph] != 0);

	// Mix 0 = signal unchanged and path still mono
	OK (B.SetValue (ph, "mix", 0.0f));
	B.Compile (&P, pI, 1.0f, FALSE, pFx);
	unsigned seed = 1;
	float inL[N], inR[N], oL[N], oR[N];
	double dMax = 0;
	for (unsigned b = 0; b < 2000; b++)
	{
		for (unsigned j = 0; j < N; j++)
		{
			inL[j] = 0.3f * Rnd (seed);
			inR[j] = inL[j];
		}
		GraphProcess (&P, inL, inR, oL, oR, N);
		for (unsigned j = 0; j < N; j++)
		{
			dMax = fmax (dMax, fabs (oL[j] - inL[j]));
		}
	}
	CHECK (dMax < 1e-6);

	// Mix 100 (effect only), no feedback: the all-pass preserves energy; the two channels differ (LFOs out of phase)
	OK (B.SetValue (ph, "mix", 100.0f));
	OK (B.SetValue (ph, "fb", 0.0f));
	B.Compile (&P, pI, 1.0f, FALSE, pFx);
	double eIn = 0, eOut = 0, dLR = 0;
	bool bad = false;
	for (unsigned b = 0; b < 20000; b++)
	{
		for (unsigned j = 0; j < N; j++)
		{
			inL[j] = 0.3f * Rnd (seed);
			inR[j] = inL[j];
		}
		GraphProcess (&P, inL, inR, oL, oR, N);
		for (unsigned j = 0; j < N; j++)
		{
			if (!std::isfinite (oL[j]) || !std::isfinite (oR[j]) || fabs (oL[j]) > 5.0f)
			{
				bad = true;
			}
			if (b > 2000)
			{
				eIn += inL[j] * inL[j];
				eOut += 0.5 * (oL[j] * oL[j] + oR[j] * oR[j]);
				dLR += fabs (oL[j] - oR[j]);
			}
		}
	}
	CHECK (!bad);
	printf ("output/input energy at Mix 100: %.3f, mean L-R difference %.4f\n", eOut / eIn, dLR / (18000.0 * N));
	CHECK (eOut / eIn > 0.9 && eOut / eIn < 1.1);
	CHECK (dLR / (18000.0 * N) > 0.01);

	// maximum feedback, mix 50, 8 stages: stays stable for 10 minutes of audio
	OK (B.SetValue (ph, "mix", 50.0f));
	OK (B.SetValue (ph, "fb", 90.0f));
	OK (B.SetValue (ph, "stages", 8.0f));
	OK (B.SetValue (ph, "rate", 5.0f));
	B.Compile (&P, pI, 1.0f, FALSE, pFx);
	double peak = 0;
	bad = false;
	for (unsigned b = 0; b < 48000 * 600 / N; b++)
	{
		for (unsigned j = 0; j < N; j++)
		{
			inL[j] = 0.5f * Rnd (seed);
			inR[j] = inL[j];
		}
		GraphProcess (&P, inL, inR, oL, oR, N);
		for (unsigned j = 0; j < N; j++)
		{
			peak = fmax (peak, fmax (fabs (oL[j]), fabs (oR[j])));
			if (!std::isfinite (oL[j]) || !std::isfinite (oR[j]))
			{
				bad = true;
			}
		}
	}
	printf ("peak with feedback 90%%, 8 stages: %.3f\n", peak);
	CHECK (!bad && peak < 8.0);

	// silence after the signal: no explosion and no lingering denormal numbers
	for (unsigned b = 0; b < 4000; b++)
	{
		memset (inL, 0, sizeof inL);
		memset (inR, 0, sizeof inR);
		GraphProcess (&P, inL, inR, oL, oR, N);
	}
	printf ("residue after silence: %g %g\n", oL[0], oR[0]);
	CHECK (fabs (oL[0]) < 1e-4 && fabs (oR[0]) < 1e-4);

	// block off = unchanged; effect not ready (instance missing) = unchanged
	OK (B.SetValue (ph, "on", 0.0f));
	B.Compile (&P, pI, 1.0f, FALSE, pFx);
	for (unsigned j = 0; j < N; j++)
	{
		inL[j] = inR[j] = 0.25f;
	}
	GraphProcess (&P, inL, inR, oL, oR, N);
	CHECK (oL[0] == 0.25f);
	OK (B.SetValue (ph, "on", 1.0f));
	B.Compile (&P, pI, 1.0f, FALSE, 0);
	GraphProcess (&P, inL, inR, oL, oR, N);
	CHECK (oL[0] == 0.25f);

	// ---- reverb (direct instance)
	{
		CFx *rv = FxCreate (FxReverb, 48000.0f);
		CHECK (rv != 0 && rv->Type () == FxReverb);
		const TFxDef *pd = FxDefByType (FxReverb);
		float Pr[8];
		for (unsigned k = 0; k < pd->nParams; k++)
		{
			Pr[k] = pd->pParams[k].Def;
		}
		Pr[4] = 100.0f;		// effect only
		Pr[2] = 0.0f;		// pre-delay zero
		// steady-state level with white noise: the wet output must have a level close to the input's
		float a[N], b[N];
		int mono = 1;
		double eI = 0, eO = 0;
		for (unsigned blk = 0; blk < 48000 * 6 / N; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.2f * Rnd (seed);
			}
			const float ref = a[0];
			(void) ref;
			double e0 = 0;
			for (unsigned j = 0; j < N; j++)
			{
				e0 += a[j] * a[j];
			}
			mono = 1;
			rv->Process (a, b, N, &mono, Pr);
			if (blk > 48000 * 3 / N)
			{
				eI += e0;
				for (unsigned j = 0; j < N; j++)
				{
					eO += 0.5 * (a[j] * a[j] + b[j] * b[j]);
				}
			}
		}
		printf ("reverb: wet/input level with white noise: %.2f (dB %.1f), stereo path: %d\n", sqrt (eO / eI), 20 * log10 (sqrt (eO / eI)), mono == 0);
		CHECK (sqrt (eO / eI) > 0.5 && sqrt (eO / eI) < 2.0);
		CHECK (mono == 0);

		// impulse response: decaying tail, duration depends on the room
		double t60[2];
		for (int big = 0; big < 2; big++)
		{
			CFx *r2 = FxCreate (FxReverb, 48000.0f);
			Pr[0] = big ? 90.0f : 20.0f;
			double peakEnv = 0;
			unsigned nT = 0;
			for (unsigned blk = 0; blk < 48000 * 20 / N; blk++)
			{
				for (unsigned j = 0; j < N; j++)
				{
					a[j] = b[j] = (blk == 0 && j == 0) ? 1.0f : 0.0f;
				}
				mono = 1;
				r2->Process (a, b, N, &mono, Pr);
				double e = 0;
				for (unsigned j = 0; j < N; j++)
				{
					e = fmax (e, fmax (fabs (a[j]), fabs (b[j])));
				}
				if (e > peakEnv)
				{
					peakEnv = e;
				}
				if (e > peakEnv * 0.001)
				{
					nT = blk * N;		// last instant above -60 dB from the peak
				}
			}
			t60[big] = nT / 48000.0;
		}
		printf ("reverb: tail at -60 dB: room 20%% = %.2f s, 90%% = %.2f s\n", t60[0], t60[1]);
		CHECK (t60[1] > t60[0] * 2.0 && t60[0] > 0.1 && t60[1] < 19.0);

		// Mix 0: unchanged and path still mono; pre-delay: the effect arrives later
		Pr[4] = 0.0f;
		mono = 1;
		for (unsigned j = 0; j < N; j++)
		{
			a[j] = b[j] = 0.25f;
		}
		rv->Process (a, b, N, &mono, Pr);
		CHECK (a[0] == 0.25f && b[3] == 0.25f && mono == 1);
		CFx *r3 = FxCreate (FxReverb, 48000.0f);
		Pr[4] = 100.0f;
		Pr[2] = 50.0f;
		Pr[0] = 50.0f;
		double eEarly = 0;
		for (unsigned blk = 0; blk < 48000 / N / 25; blk++)		// first 40 ms: still in the pre-delay
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = (blk == 0 && j == 0) ? 1.0f : 0.0f;
			}
			mono = 1;
			r3->Process (a, b, N, &mono, Pr);
			for (unsigned j = 0; j < N; j++)
			{
				eEarly += fabs (a[j]) + fabs (b[j]);
			}
		}
		CHECK (eEarly == 0.0);

		// width 0 = identical output on the two channels (mono)
		CFx *r4 = FxCreate (FxReverb, 48000.0f);
		Pr[2] = 0.0f;
		Pr[3] = 0.0f;
		double dLR2 = 0;
		for (unsigned blk = 0; blk < 4000; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.2f * Rnd (seed);
			}
			mono = 1;
			r4->Process (a, b, N, &mono, Pr);
			for (unsigned j = 0; j < N; j++)
			{
				dLR2 += fabs (a[j] - b[j]);
			}
		}
		CHECK (dLR2 < 1e-3);
	}

	// ---- delay (direct instance)
	{
		const TFxDef *pd = FxDefByType (FxDelay);
		CHECK (pd != 0);
		float Pd[8];
		for (unsigned k = 0; k < pd->nParams; k++)
		{
			Pd[k] = pd->pParams[k].Def;
		}
		Pd[0] = 100.0f;		// 4800 samples
		Pd[1] = 50.0f;
		Pd[2] = 100.0f;		// repeats almost unfiltered
		Pd[3] = 0.0f;
		Pd[4] = 50.0f;		// dry 1, effect 1
		static float outL[48000 * 2], outR[48000 * 2];
		float a[N], b[N];
		int mono = 1;
		CFx *d1 = FxCreate (FxDelay, 48000.0f);
		for (unsigned blk = 0; blk < 48000 * 2 / N; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = (blk == 0 && j == 0) ? 1.0f : 0.0f;
			}
			mono = 1;
			d1->Process (a, b, N, &mono, Pd);
			for (unsigned j = 0; j < N; j++)
			{
				outL[blk * N + j] = a[j];
				outR[blk * N + j] = b[j];
			}
		}
		CHECK (mono == 1);		// ping-pong 0 and mono path: stays mono
		CHECK (outL[0] == 1.0f);
		double e1 = 0, e2 = 0, e3 = 0;
		int nFirst = -1;
		for (unsigned i = 1; i < 48000 * 2; i++)
		{
			if (outL[i] > 0.01f && nFirst < 0)
			{
				nFirst = (int) i;
			}
			if (i > 3000 && i < 7000)
			{
				e1 += outL[i];
			}
			else if (i > 7800 && i < 11800)
			{
				e2 += outL[i];
			}
			else if (i > 12600 && i < 16600)
			{
				e3 += outL[i];
			}
		}
		printf ("delay: first echo at sample %d (expected 4800), echo area 1/2/3: %.3f %.3f %.3f (expected 1, 0.5, 0.25)\n", nFirst, e1, e2, e3);
		CHECK (nFirst >= 4797 && nFirst <= 4803);
		CHECK (fabs (e1 - 1.0) < 0.05 && fabs (e2 - 0.5) < 0.05 && fabs (e3 - 0.25) < 0.05);
		CHECK (fabs (outL[4800 * 2 + 5] - outR[4800 * 2 + 5]) < 1e-6);

		// ping-pong 100%: the first echo on the left, the second on the right
		Pd[3] = 100.0f;
		CFx *d2 = FxCreate (FxDelay, 48000.0f);
		double l1 = 0, r1 = 0, l2 = 0, r2 = 0;
		for (unsigned blk = 0; blk < 48000 / N; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = (blk == 0 && j == 0) ? 1.0f : 0.0f;
			}
			mono = 1;
			d2->Process (a, b, N, &mono, Pd);
			for (unsigned j = 0; j < N; j++)
			{
				const unsigned i = blk * N + j;
				if (i > 3000 && i < 7000)
				{
					l1 += fabs (a[j]);
					r1 += fabs (b[j]);
				}
				else if (i > 7800 && i < 11800)
				{
					l2 += fabs (a[j]);
					r2 += fabs (b[j]);
				}
			}
		}
		printf ("ping-pong: echo 1 L %.3f R %.3f, echo 2 L %.3f R %.3f, stereo path: %d\n", l1, r1, l2, r2, mono == 0);
		CHECK (l1 > 0.9 && r1 < 0.01 && r2 > 0.4 && l2 < 0.01 && mono == 0);

		// Mix 0 = unchanged; Mix 100 = no dry
		Pd[3] = 0.0f;
		Pd[4] = 0.0f;
		mono = 1;
		a[0] = a[1] = a[2] = a[3] = 0.3f;
		b[0] = b[1] = b[2] = b[3] = 0.3f;
		d1->Process (a, b, N, &mono, Pd);
		CHECK (a[0] == 0.3f && mono == 1);
		Pd[4] = 100.0f;
		CFx *d3 = FxCreate (FxDelay, 48000.0f);
		a[0] = a[1] = a[2] = a[3] = 0.3f;
		b[0] = b[1] = b[2] = b[3] = 0.3f;
		d3->Process (a, b, N, &mono, Pd);
		CHECK (a[0] == 0.0f);

		// stability: maximum feedback and time changing continuously for 10 minutes
		Pd[1] = 95.0f;
		Pd[4] = 50.0f;
		CFx *d4 = FxCreate (FxDelay, 48000.0f);
		double pk = 0;
		bool bad2 = false;
		for (unsigned blk = 0; blk < 48000 * 600 / N; blk++)
		{
			if (blk % 4000 == 0)
			{
				Pd[0] = 10.0f + (float) ((blk / 4000) % 150) * 10.0f;
				Pd[2] = (float) ((blk / 8000) % 101);
				Pd[3] = (blk / 12000) % 2 ? 100.0f : 0.0f;
			}
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.3f * Rnd (seed);
			}
			mono = 1;
			d4->Process (a, b, N, &mono, Pd);
			for (unsigned j = 0; j < N; j++)
			{
				pk = fmax (pk, fmax (fabs (a[j]), fabs (b[j])));
				if (!std::isfinite (a[j]) || !std::isfinite (b[j]))
				{
					bad2 = true;
				}
			}
		}
		printf ("delay feedback 95%%, parameters moving for 10 minutes: peak %.2f\n", pk);
		CHECK (!bad2 && pk < 12.0);
	}

	// ---- compressor (direct instance, continuous level: the steady-state values follow the formula)
	{
		const TFxDef *pd = FxDefByType (FxCompressor);
		CHECK (pd != 0);
		float Pc[8];
		for (unsigned k = 0; k < pd->nParams; k++)
		{
			Pc[k] = pd->pParams[k].Def;
		}
		Pc[0] = -20.0f;		// threshold
		Pc[1] = 4.0f;
		Pc[2] = 10.0f;
		Pc[3] = 100.0f;
		Pc[4] = 0.0f;		// hard knee
		Pc[5] = 0.0f;
		Pc[6] = 100.0f;
		float a[N], b[N];
		int mono = 1;
		CFx *c1 = FxCreate (FxCompressor, 48000.0f);
		// input -6.02 dB (0.5): expected reduction = (14 dB above) * (1 - 1/4) = 10.5 dB
		const double expect = 0.5 * pow (10.0, -10.5 / 20.0);
		double vOut = 0;
		double tAtk = -1;
		const double grSS = 10.5;
		for (unsigned blk = 0; blk < 48000 * 2 / N; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = blk < 1000 ? 0.01f : 0.5f;
			}
			mono = 1;
			c1->Process (a, b, N, &mono, Pc);
			if (blk >= 1000 && tAtk < 0)
			{
				// current reduction in dB
				const double gr = -20.0 * log10 (a[0] / 0.5);
				if (gr >= grSS * 0.632)
				{
					tAtk = ((blk - 1000) * N) / 48.0;		// ms
				}
			}
			vOut = a[0];
		}
		printf ("compressor: steady-state output %.4f (expected %.4f), time to 63%% of the reduction %.1f ms (attack 10 ms)\n", vOut, expect, tAtk);
		CHECK (fabs (vOut / expect - 1.0) < 0.02);
		CHECK (tAtk > 7.0 && tAtk < 14.0);
		CHECK (mono == 1 && a[0] == b[0]);

		// release: return to the low level, 63% in 100 ms
		double tRel = -1;
		for (unsigned blk = 0; blk < 48000 / N; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.01f;
			}
			c1->Process (a, b, N, &mono, Pc);
			const double gr = -20.0 * log10 (a[0] / 0.01);
			if (tRel < 0 && gr <= grSS * (1 - 0.632))
			{
				tRel = (blk * N) / 48.0;
			}
		}
		printf ("compressor: release time to 63%%: %.1f ms (expected 100)\n", tRel);
		CHECK (tRel > 80.0 && tRel < 125.0);

		// below the threshold: unchanged; ratio 1: unchanged; makeup +6 dB: doubles
		CFx *c2 = FxCreate (FxCompressor, 48000.0f);
		for (unsigned j = 0; j < N; j++)
		{
			a[j] = b[j] = 0.05f;
		}
		c2->Process (a, b, N, &mono, Pc);
		CHECK (fabs (a[N - 1] - 0.05f) < 1e-4);
		CFx *c3 = FxCreate (FxCompressor, 48000.0f);
		float Pr1[8];
		memcpy (Pr1, Pc, sizeof Pr1);
		Pr1[1] = 1.0f;
		Pr1[5] = 6.0f;
		for (unsigned blk = 0; blk < 4000; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.5f;
			}
			c3->Process (a, b, N, &mono, Pr1);
		}
		printf ("compressor: ratio 1, makeup +6 dB: %.4f (expected 0.9976)\n", a[0]);
		CHECK (fabs (a[0] - 0.5f * 1.99526f) < 0.01);

		// soft knee: at the threshold with a 12 dB knee the reduction is greater than 0 but smaller than the hard case
		CFx *c4 = FxCreate (FxCompressor, 48000.0f);
		float Pk[8];
		memcpy (Pk, Pc, sizeof Pk);
		Pk[4] = 12.0f;
		Pk[0] = -6.0206f;		// level 0.5 is exactly at the threshold
		for (unsigned blk = 0; blk < 4000; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.5f;
			}
			c4->Process (a, b, N, &mono, Pk);
		}
		const double grK = -20.0 * log10 (a[0] / 0.5);
		printf ("compressor: 12 dB knee, level at the threshold: reduction %.2f dB (expected 12*0.75/8 = %.2f)\n", grK, 12.0 * 0.75 / 8.0);
		CHECK (fabs (grK - 12.0 * 0.75 / 8.0) < 0.15);

		// Mix 0 unchanged; parallel 50%: average between dry and compressed
		CFx *c5 = FxCreate (FxCompressor, 48000.0f);
		Pc[6] = 50.0f;
		for (unsigned blk = 0; blk < 4000; blk++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				a[j] = b[j] = 0.5f;
			}
			c5->Process (a, b, N, &mono, Pc);
		}
		printf ("compressor: parallel 50%%: %.4f (expected %.4f)\n", a[0], 0.5 * (0.5 + expect / 0.5 * 0.5 / 1.0) );
		CHECK (fabs (a[0] - 0.5 * (0.5 + expect)) < 0.01);
	}

	// ---- saved setting linked to the block: saving and re-reading
	{
		static CBoard Q, Q2;
		int a, b;
		OK (Q.NetAdd ("phaser", 0, 1, "", &a));
		OK (Q.NetAdd ("vol", 1, 1, "", &b));
		OK (Q.SetPreset (a, "Dark crunch"));
		ERR (Q.SetPreset (a, "with\"quotes"));
		ERR (Q.SetPreset (30, "x"));
		CHECK (strcmp (Q.Block (a).Preset, "Dark crunch") == 0 && Q.Block (b).Preset[0] == 0);
		static char Bf[8192];
		Q.Serialize (Bf, sizeof Bf);
		CHECK (Q2.Parse (Bf));
		CHECK (strcmp (Q2.Block (a).Preset, "Dark crunch") == 0 && Q2.Block (b).Preset[0] == 0);
		OK (Q2.SetPreset (a, ""));
		CHECK (Q2.Block (a).Preset[0] == 0);
	}

	// ---- ADEL effects: many values per block, saving and re-reading ('e' rows)
	{
		static CBoard A, A2;
		int c1, v1;
		OK (A.NetAdd ("adelcomp", 0, 1, "", &c1));
		OK (A.NetAdd ("adelverb", 1, 1, "", &v1));
		const TFxDef *dc = FxDefByType (FxAdelComp), *dv = FxDefByType (FxAdelVerb);
		CHECK (dc != 0 && dv != 0 && dc->nParams == 28 && dv->nParams == 41);
		for (unsigned k = 0; k < dc->nParams; k++)
		{
			CHECK (A.Block (c1).V[k] == dc->pParams[k].Def);
		}
		OK (A.SetValue (c1, "type", 2.0f));
		OK (A.SetValue (c1, "ratio", 6.5f));
		OK (A.SetValue (c1, "automakeup", 1.0f));
		OK (A.SetValue (v1, "decay", 4.25f));
		OK (A.SetValue (v1, "shape", 3.0f));
		OK (A.SetValue (v1, "morphtarget", 4.0f));
		ERR (A.SetValue (c1, "gr", 3.0f));		// measured output: read-only
		OK (A.SetValue (c1, "threshold", -99.0f));
		CHECK (A.Block (c1).V[4] == -60.0f);		// out of range: brought to the minimum
		OK (A.SetValue (c1, "type", 7.0f));
		CHECK (A.Block (c1).V[3] == 3.0f);		// choice: maximum limit
		static char Bg[32768];
		const unsigned nWritten = A.Serialize (Bg, sizeof Bg);
		printf ("ADEL: pedalboard saved in %u bytes\n", nWritten);
		CHECK (A2.Parse (Bg));
		for (unsigned k = 0; k < dc->nParams; k++)
		{
			CHECK (fabsf (A2.Block (c1).V[k] - A.Block (c1).V[k]) < 1e-3f);
		}
		for (unsigned k = 0; k < dv->nParams; k++)
		{
			CHECK (fabsf (A2.Block (v1).V[k] - A.Block (v1).V[k]) < 1e-3f);
		}
		CHECK (A2.Block (v1).V[0] == A.Block (v1).V[0]);
		// compiled plan: the values reach the engine
		static TPlan Pl;
		A.Compile (&Pl, 0, 1.0f, FALSE, 0);
		CHECK (Pl.Node[c1].P[6] == A.Block (c1).V[6] && Pl.Node[v1].P[40] == A.Block (v1).V[40]);
	}

	printf (fails ? "RISULTATO: %d FALLITE\n" : "RESULT: all tests passed\n", fails);
	return fails != 0;
}


