// test_cabir.cpp - correctness (against direct double-precision convolution) and cost of the NEON convolution
// g++ -std=c++17 -O3 -mcpu=cortex-a76 test_cabir.cpp -o test_cabir
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <vector>
#include "cabir.h"

int main ()
{
	const unsigned T = 2048;
	std::vector<float> ir(T), rev(T), hist(2 * T);
	unsigned s = 5;
	for (unsigned i = 0; i < T; i++)
	{
		s = s * 1664525u + 1013904223u;
		ir[i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * expf (-(float) i / 300.0f);
	}
	for (unsigned i = 0; i < T; i++)
	{
		rev[i] = ir[T - 1 - i];
	}
	TCabChannel ch;
	ch.pHist = hist.data ();
	ch.Reset (T);
	const unsigned N = 20000;
	std::vector<float> x(N), y(N);
	for (unsigned i = 0; i < N; i++)
	{
		s = s * 1664525u + 1013904223u;
		x[i] = (float) (s >> 8) / 8388608.0f - 1.0f;
	}
	for (unsigned i = 0; i < N; i++)
	{
		y[i] = ch.Step (x[i], rev.data (), T);
	}
	double dmax = 0, ymax = 0;
	for (unsigned i = 0; i < N; i += 7)
	{
		double r = 0;
		for (unsigned k = 0; k < T && k <= i; k++)
		{
			r += (double) ir[k] * (double) x[i - k];
		}
		dmax = fmax (dmax, fabs (r - (double) y[i]));
		ymax = fmax (ymax, fabs (r));
	}
	printf ("NEON convolution (%u samples) against double precision: maximum error %.2e on a maximum of %.2f\n", T, dmax, ymax);
	int fails = dmax < 1e-3 * ymax ? 0 : 1;
	// cost per sample
	for (unsigned taps : {1024u, 2048u, 4096u, 8192u})
	{
		std::vector<float> r2(taps, 0.01f), h2(2 * taps);
		TCabChannel c2;
		c2.pHist = h2.data ();
		c2.Reset (taps);
		struct timespec t0, t1;
		const unsigned M = 200000;
		float acc = 0;
		clock_gettime (CLOCK_MONOTONIC, &t0);
		for (unsigned i = 0; i < M; i++)
		{
			acc += c2.Step (x[i % N], r2.data (), taps);
		}
		clock_gettime (CLOCK_MONOTONIC, &t1);
		const double ns = ((double) (t1.tv_sec - t0.tv_sec) * 1e9 + (double) (t1.tv_nsec - t0.tv_nsec)) / M;
		printf ("%5u samples: %.0f ns per sample = %.2f%% of a core per channel (at 48 kHz)  [%g]\n", taps, ns, ns * 48000 / 1e9 * 100, acc);
	}
	printf (fails ? "RESULT: FAILED\n" : "RESULT: all tests passed\n");
	return fails;
}
