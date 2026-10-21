// test_adel_fid.cpp - fidelity and cost of the ADEL engines with Circle's compatibility layer.
// It is compiled twice: (1) normal, with Linux's libstdc++ and libm (reference); (2) with -nostdinc++ -Ifxshim and fxmath.cpp (as on Circle).
// Same program, same signals, same parameters: it writes the outputs to a file; cmpbin compares them.
//   usage: test_adel_fid output.bin [bench]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fx.h"

static unsigned s_nSeed = 12345;
static float Rnd (void)
{
	s_nSeed = s_nSeed * 1664525u + 1013904223u;
	return (float) (s_nSeed >> 8) / 8388608.0f - 1.0f;
}

static unsigned Lcg (void)
{
	s_nSeed = s_nSeed * 1664525u + 1013904223u;
	return s_nSeed >> 8;
}

int main (int argc, char **argv)
{
	FILE *fo = fopen (argv[1], "wb");
	const bool bBench = argc > 2;
	const unsigned nTypes[7] = {FxAdelComp, FxAdelDelay, FxAdelVerb, FxAdelPhase, FxAdelSweep, FxAdelSpring, FxAdelChorus};
	double dMaxNs[7] = {0, 0, 0, 0, 0, 0, 0}, dSumNs[7] = {0, 0, 0, 0, 0, 0, 0};
	unsigned long nBlocks[7] = {0, 0, 0, 0, 0, 0, 0};

	for (unsigned e = 0; e < 7; e++)
	{
		const TFxDef *pD = FxDefByType (nTypes[e]);
		const unsigned nSets = argc > 3 ? 1 : 7;
		for (unsigned set = 0; set < nSets; set++)
		{
			CFx *pFx = FxCreate (nTypes[e], 48000.0f);
			float P[FX_MAX_P];
			for (unsigned k = 0; k < FX_MAX_P; k++)
			{
				P[k] = 0.0f;
			}
			for (unsigned k = 0; k < pD->nParams; k++)
			{
				const TFxParam &Pm = pD->pParams[k];
				float v = Pm.Def;
				if (set == 1 || set == 2)
				{
					// extreme values: all at minimum / all at maximum (the choices stay valid)
					v = set == 1 ? Pm.Min : Pm.Max;
				}
				else if (set >= 3)
				{
					const float t = (float) (Lcg () % 10001) / 10000.0f;
					v = Pm.Min + (Pm.Max - Pm.Min) * t;
					if (Pm.Kind == FxKindChoice || Pm.Kind == FxKindToggle)
					{
						v = (float) (int) (v + 0.5f);
					}
				}
				if (Pm.Kind == FxKindOutput)
				{
					v = 0.0f;
				}
				P[k] = v;
			}
			// signal: fake guitar (sum of sine waves with a repeating envelope) + noise, different L and R; last 2 s of silence
			const unsigned nBlk = getenv ("BLK") ? (unsigned) atoi (getenv ("BLK")) : 4, nTotal = 48000 * 6 / nBlk;
			float L[64], R[64];
			double phA = 0, phB = 0;
			const bool bMono = set == 5;
			for (unsigned b = 0; b < nTotal; b++)
			{
				const unsigned nSamp = b * nBlk;
				const float env = nSamp < 48000 * 4 ? ((nSamp % 24000) < 1200 ? 1.0f : 0.15f + 0.85f * (float) (24000 - nSamp % 24000) / 24000.0f) : 0.0f;
				for (unsigned j = 0; j < nBlk; j++)
				{
					phA += 6.2831853 * 196.0 / 48000.0;
					phB += 6.2831853 * 330.0 / 48000.0;
					const float a = (float) __builtin_sin (phA), c = (float) __builtin_sin (phB);
					L[j] = env * (0.30f * a + 0.10f * c + 0.02f * Rnd ());
					R[j] = bMono ? L[j] : env * (0.25f * c + 0.12f * a + 0.02f * Rnd ());
				}
				int nMono = bMono ? 1 : 0;
				struct timespec t0, t1;
				if (bBench)
				{
					clock_gettime (CLOCK_MONOTONIC, &t0);
				}
				pFx->Process (L, R, nBlk, &nMono, P);
				if (bBench)
				{
					clock_gettime (CLOCK_MONOTONIC, &t1);
					const double ns = (double) (t1.tv_sec - t0.tv_sec) * 1e9 + (double) (t1.tv_nsec - t0.tv_nsec);
					if (b > 100)
					{
						dSumNs[e] += ns;
						nBlocks[e]++;
						if (ns > dMaxNs[e])
						{
							dMaxNs[e] = ns;
						}
					}
				}
				fwrite (L, 4, nBlk, fo);
				fwrite (R, 4, nBlk, fo);
			}
		}
		printf ("%s: %u tests\n", pD->Name, nSets);
	}
	fclose (fo);
	if (bBench)
	{
		for (unsigned e = 0; e < 7; e++)
		{
			const TFxDef *pD = FxDefByType (nTypes[e]);
			printf ("COST %-10s average %.0f ns/block of %u samples = %.2f%% of the available time, maximum %.0f ns\n", pD->Name,
				dSumNs[e] / (double) nBlocks[e], getenv ("BLK") ? (unsigned) atoi (getenv ("BLK")) : 4u,
				100.0 * dSumNs[e] / (double) nBlocks[e] / ((getenv ("BLK") ? (unsigned) atoi (getenv ("BLK")) : 4u) * 20833.0), dMaxNs[e]);
		}
	}
	return 0;
}
