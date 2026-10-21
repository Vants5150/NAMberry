// test_extra_fuzz.cpp - fuzzer with AddressSanitizer and UBSan for the 20 ToTest effects: random parameters (even out of range), blocks of random
// length, random inputs (noise, sines, impulses, silence, large values), mono and stereo, parameter changes at every block
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ihost_shim -I<circle>/include -I. -Iadel test_extra_fuzz.cpp fx*.cpp ... -o test_extra_fuzz
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include "fx.h"
#include "fx_defs.h"

static unsigned long long g_s = 88172645463325252ULL;
static unsigned long long Rnd (void) { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }
static float Uni (void) { return (float) (Rnd () >> 40) / 16777216.0f; }	// 0..1

int main (int argc, char **argv)
{
	const unsigned long nIter = argc > 1 ? strtoul (argv[1], 0, 10) : 2000;
	if (argc > 2) g_s ^= strtoull (argv[2], 0, 10) * 0x9E3779B97F4A7C15ULL;
	int bad = 0;
	static const unsigned sizes[] = {1, 2, 3, 4, 5, 7, 8, 16, 32, 64, 128};
	for (unsigned t = 28; t <= 47; t++)
	{
		const TFxDef *d = FxDefByType (t);
		const float rate = (Rnd () & 1) ? 48000.0f : 44100.0f;
		CFx *fx = FxCreate (t, rate);
		float P[FX_MAX_P];
		memset (P, 0, sizeof P);
		for (unsigned long it = 0; it < nIter; it++)
		{
			// parameters: usually within the limits, now and then outside (the page and the API limit them but the engine must not break)
			for (unsigned i = 0; i < d->nParams; i++)
			{
				const float lo = d->pParams[i].Min, hi = d->pParams[i].Max;
				const unsigned r = (unsigned) (Rnd () % 20);
				if (r == 0) P[i] = lo - (hi - lo) * Uni ();
				else if (r == 1) P[i] = hi + (hi - lo) * Uni ();
				else if (r == 2) P[i] = lo;
				else if (r == 3) P[i] = hi;
				else if (r < 6) { /* left as it was*/ }
				else P[i] = lo + (hi - lo) * Uni ();
			}
			const unsigned n = sizes[Rnd () % (sizeof sizes / sizeof sizes[0])];
			std::vector<float> L (n), R (n);
			const unsigned kind = (unsigned) (Rnd () % 6);
			for (unsigned j = 0; j < n; j++)
			{
				float x = 0.0f;
				switch (kind)
				{
				case 0: x = 0.0f; break;
				case 1: x = (Uni () - 0.5f) * 2.0f; break;
				case 2: x = 0.5f * sinf ((float) (it * 131 + j) * 0.05f); break;
				case 3: x = (j == 0 && (it % 7) == 0) ? 1.0f : 0.0f; break;
				case 4: x = (Uni () - 0.5f) * 40.0f; break;
				default: x = 0.001f * (Uni () - 0.5f); break;
				}
				L[j] = x;
				R[j] = (Rnd () & 3) == 0 ? (Uni () - 0.5f) : x;
			}
			int mono = (int) (Rnd () & 1);
			if (mono) R = L;
			fx->Process (L.data (), R.data (), n, &mono, P);
			for (unsigned j = 0; j < n; j++)
			{
				if (!(fabsf (L[j]) < 1e4f) || !(fabsf (R[j]) < 1e4f))
				{
					printf ("invalid value: %s iter %lu n %u kind %u L %g R %g\n", d->Name, it, n, kind, L[j], R[j]);
					bad++;
					break;
				}
			}
			if (bad > 10) return 1;
		}
		delete fx;
	}
	if (bad) { printf ("RESULT: %d errors\n", bad); return 1; }
	printf ("RESULT: no sanitizer errors, no invalid values (20 effects x %lu blocks)\n", nIter);
	return 0;
}
