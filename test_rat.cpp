// test_rat.cpp - tests of the RAT (fx_rat.cpp) on the PC: stability, controls, variants and comparison with anchors derived from the real captures (RAT2 by outmodedelectronics, TONE3000, 2026-10-18).
// g++ -std=c++17 -O1 -Ihost_shim -I../../../../include -I. -Iadel test_rat.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_rat
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)
#define CHECKF(c, ...) do { char b_[200]; snprintf (b_, sizeof b_, __VA_ARGS__); CHECK (c, b_); } while (0)

// gain of the fundamental (dB) with a sine of peak volts
static double Gain (int model, double dist, double filt, double vol, double f, double a)
{
	CFx *fx = FxCreate (FxRatTS, 48000.0f);
	const float P[5] = {(float) model, (float) dist, (float) filt, (float) vol, getenv ("RAT_OS") ? (float) atof (getenv ("RAT_OS")) : 1.0f};
	const int N = 48000;
	double sr = 0, si = 0, wsum = 0;
	for (int b = 0; b < N / 4; b++)
	{
		float L[4], R[4];
		for (int j = 0; j < 4; j++) L[j] = R[j] = (float) (a * sin (2 * M_PI * f * (b * 4 + j) / 48000.0) / 1.305);
		int mono = 1;
		fx->Process (L, R, 4, &mono, P);
		for (int j = 0; j < 4; j++)
		{
			const int n = b * 4 + j;
			if (n >= N / 2)
			{
				const double w = 0.5 - 0.5 * cos (2 * M_PI * (n - N / 2) / (N / 2 - 1)), ph = 2 * M_PI * f * n / 48000.0;
				sr += w * L[j] * 1.305 * cos (ph); si += w * L[j] * 1.305 * sin (ph); wsum += w;
			}
		}
	}
	delete fx;
	return 20 * log10 (2.0 * sqrt (sr * sr + si * si) / wsum / a + 1e-12);
}

int main ()
{
	// 1. anchors derived from the real captures: Distortion 12:00 (5), Filter 12:00 (5), Volume 3:00 (8.33); the block has 4.8 dB less than the pure circuit (1.8 from the captures + 3 fixed)
	{
		const double trim = 20 * log10 (0.575 / 0.813);		// relative to the level adapted to the captures
		struct A { double f, amp, cap; } anc[] = {{100, 0.01, 23.5}, {200, 0.01, 26.5}, {100, 0.05, 17.1}, {800, 0.05, 18.3}, {100, 0.2, 6.3}, {800, 0.2, 6.2}, {1600, 0.2, 5.6}};
		for (const A &a : anc)
		{
			const double g = Gain (0, 5.0, 5.0, 8.33, a.f, a.amp);
			CHECKF (fabs (g - (a.cap + trim)) < 2.5, "anchor capture D 12:00 F 12:00 at %.0f Hz %.0f mV: %.1f dB (capture %.1f, expected %.1f +-2.5)", a.f, a.amp * 1000, g, a.cap, a.cap + trim);
		}
		// Distortion 9:00 (1.67), Filter 12:00: capture 14.2 dB at 100 Hz, 10 mV
		const double g9 = Gain (0, 1.667, 5.0, 8.33, 100, 0.01);
		CHECKF (fabs (g9 - (14.2 + trim)) < 2.5, "anchor capture D 9:00 F 12:00 at 100 Hz 10 mV: %.1f dB (capture 14.2, expected %.1f)", g9, 14.2 + trim);
	}
	// 2. controls: more Distortion = more gain at low level; Filter to the right = darker; Volume at zero = silence and increasing
	{
		const double d0 = Gain (0, 0, 5, 8.33, 400, 0.001), d3 = Gain (0, 3, 5, 8.33, 400, 0.001), d6 = Gain (0, 6, 5, 8.33, 400, 0.001);		// 1 mV: still linear
		CHECKF (d3 > d0 + 6 && d6 > d3 + 6, "Distortion raises the gain at 1 mV: %.1f, %.1f, %.1f dB (positions 0, 3, 6)", d0, d3, d6);
		const double fb = Gain (0, 5, 0, 8.33, 5000, 0.05), fd = Gain (0, 5, 10, 8.33, 5000, 0.05);
		CHECKF (fb > fd + 15, "Filter: at 5 kHz fully left %.1f dB, fully right %.1f dB (at least 15 dB of difference)", fb, fd);
		const double v0 = Gain (0, 5, 5, 0, 400, 0.05), v5 = Gain (0, 5, 5, 5, 400, 0.05), v10 = Gain (0, 5, 5, 10, 400, 0.05);
		CHECKF (v0 < -60 && v5 > v0 + 20 && v10 > v5 + 3, "Volume: %.1f, %.1f, %.1f dB (0, 5, 10)", v0, v5, v10);
	}
	// 3. variants: the OP07 has less bandwidth (audible with high Distortion and treble), the LEDs compress less (they clip at a higher voltage)
	{
		const double l308 = Gain (0, 8, 0, 8.33, 3000, 0.01), op07 = Gain (1, 8, 0, 8.33, 3000, 0.01), led = Gain (2, 8, 0, 8.33, 1000, 0.2), dio = Gain (0, 8, 0, 8.33, 1000, 0.2);
		CHECKF (fabs (l308 - op07) > 1.0, "LM308 and OP07 differ at 3 kHz: %.1f and %.1f dB", l308, op07);
		CHECKF (led > dio + 1.5, "LED: at saturation they come out louder than the 1N914s: %.1f and %.1f dB", led, dio);
	}
	// 4. stability: strong noise with all knobs at maximum and at minimum, no NaN, bounded output
	{
		bool ok = true; double pk = 0;
		for (int m = 0; m < 3; m++)
		{
			for (int k = 0; k < 4; k++)
			{
				const float P[5] = {(float) m, (k & 1) ? 10.0f : 0.0f, (k & 2) ? 10.0f : 0.0f, 10.0f, (float) (m & 1)};
				CFx *fx = FxCreate (FxRatTS, 48000.0f);
				unsigned s = 7;
				for (int b = 0; b < 12000; b++)
				{
					float L[4], R[4];
					for (int j = 0; j < 4; j++) { s = s * 1664525u + 1013904223u; L[j] = R[j] = ((float) (int) (s >> 16) - 32768.0f) / 32768.0f; }
					int mono = 1;
					fx->Process (L, R, 4, &mono, P);
					for (int j = 0; j < 4; j++) { if (!std::isfinite (L[j])) ok = false; if (fabs (L[j]) > pk) pk = fabs (L[j]); }
				}
				delete fx;
			}
		}
		CHECKF (ok && pk < 2.0, "stability with full-scale noise, all knobs and variants: peak %.3f, no NaN", pk);
	}
	// 5. stereo: the two channels are independent (a silent right channel stays silent)
	{
		CFx *fx = FxCreate (FxRatTS, 48000.0f);
		const float P[5] = {0, 7, 5, 8, 1};
		float maxR = 0;
		for (int b = 0; b < 4000; b++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++) { L[j] = (float) (0.1 * sin (2 * M_PI * 220 * (b * 4 + j) / 48000.0)); R[j] = 0; }
			int mono = 0;
			fx->Process (L, R, 4, &mono, P);
			for (int j = 0; j < 4; j++) if (fabs (R[j]) > maxR) maxR = fabs (R[j]);
		}
		delete fx;
		CHECKF (maxR < 1e-6, "stereo: the silent right channel stays silent (%.2g)", maxR);
	}
	printf ("\nRESULT: %s (%d failed)\n", fails ? "FAILED" : "OK", fails);
	return fails ? 1 : 0;
}
