// test_tonestack.cpp - Tone Stack Real: b1 coefficient against the published formula, mid notch, effect of the three controls, all versions, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_tonestack.cpp fx_tonestack.cpp -o test_tonestack
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void ToneStackBuildForTest (Ck::TCkt &c, int *h, const float *p);
void ToneStackPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float bass, float mid, float treble, float amp, float src)
{
	c = Ck::TCkt ();
	const float p[6] = {bass, mid, treble, amp, src, 0.0f};
	ToneStackBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	ToneStackPotsForTest (c, h, p);
	c.Start ();
}

static double Gain (Ck::TCkt &c, const int *h, double f, double amp)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * (f < 10 ? 3.0 : 0.4));
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		const double v = c.Run (h[0], amp * sin (th)); sn += v * sin (th); cs += v * cos (th);
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	// 1. b1 coefficient (low-frequency slope) against the published formula, Bassman, bass 0 (l = e^-3.4), mid and treble at various positions
	{
		const double R1 = 250e3, R2 = 1e6, R3 = 25e3, C1 = 250e-12, C2 = 20e-9, C3 = 20e-9;
		const float pos[3][3] = {{10, 5, 5}, {5, 0, 0}, {5, 10, 10}};
		for (int k = 0; k < 3; k++)
		{
			Setup (c, h, pos[k][0], pos[k][1], pos[k][2], 0, 0);
			c.SetR (h[10], 1.0e11); c.Start ();		// the published formula assumes an open-circuit output
			const double l = exp ((pos[k][0] * 0.1 - 1.0) * 3.4), m = pos[k][1] * 0.1, t = pos[k][2] * 0.1;
			const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
			const double f = 1.0, w = 2 * M_PI * f;
			const double g = Gain (c, h, f, 0.01);
			CHECK (fabs (g / (b1 * w) - 1.0) < 0.10, "b1 with bass %g mid %g treble %g: gain at 1 Hz %.3e, formula %.3e", pos[k][0], pos[k][1], pos[k][2], g, b1 * w);
		}
	}
	// 2. Bassman response with everything at 5 (levels relative to 1 kHz) and position of the notch
	Setup (c, h, 5, 5, 5, 0, 0);
	{
		printf ("Bassman 5-5-5, response in dB (absolute):\n");
		double best = 1e9, bf = 0;
		const double fr[] = {40, 60, 80, 120, 200, 300, 400, 500, 700, 1000, 1500, 2000, 3000, 5000, 8000, 12000};
		for (double f : fr)
		{
			const double g = Db (Gain (c, h, f, 0.01));
			printf ("  %6.0f Hz  %6.1f dB\n", f, g);
			if (f >= 200 && f <= 1500 && g < best) { best = g; bf = f; }
		}
		CHECK (bf >= 300 && bf <= 700, "mid notch at %.0f Hz (%.1f dB)", bf, best);
		CHECK (best > -30 && best < -8, "loss in the notch %.1f dB", best);
	}
	// 2b. position of the notch with mid 0 (bass and treble at 10) for the five versions
	for (int a = 0; a < 5; a++)
	{
		Setup (c, h, 10, 0, 10, (float) a, 0);
		double best = 1e9, bf = 0;
		for (double f = 100; f <= 3000; f *= 1.1) { const double g = Db (Gain (c, h, f, 0.01)); if (g < best) { best = g; bf = f; } }
		printf ("version %d, mid 0: notch at %.0f Hz, %.1f dB\n", a, bf, best);
	}
	// 3. effect of the controls
	{
		double lo[2], mi[2], hi[2];
		for (int k = 0; k < 2; k++)
		{
			const float v = k ? 10.0f : 0.0f;
			Setup (c, h, v, 5, 5, 0, 0); lo[k] = Db (Gain (c, h, 80, 0.01));
			Setup (c, h, 5, v, 5, 0, 0); mi[k] = Db (Gain (c, h, 500, 0.01));
			Setup (c, h, 5, 5, v, 0, 0); hi[k] = Db (Gain (c, h, 4000, 0.01));
		}
		CHECK (lo[1] > lo[0] + 6.0, "Bass: 80 Hz a 0/10 -> %.1f/%.1f dB", lo[0], lo[1]);
		CHECK (mi[1] > mi[0] + 3.0, "Mid: 500 Hz a 0/10 -> %.1f/%.1f dB", mi[0], mi[1]);
		CHECK (hi[1] > hi[0] + 6.0, "Treble: 4 kHz a 0/10 -> %.1f/%.1f dB", hi[0], hi[1]);
	}
	// 4. all versions and the 38k source: finite, no faults, higher source = more loss
	for (int a = 0; a < 5; a++)
	{
		Setup (c, h, 5, 5, 5, (float) a, 0);
		const double g0 = Db (Gain (c, h, 1000, 0.01));
		Setup (c, h, 5, 5, 5, (float) a, 1);
		const double g1 = Db (Gain (c, h, 1000, 0.01));
		CHECK (g0 > -60 && g0 < 0 && g1 < g0 + 0.01, "version %d: 1 kHz %.1f dB (ideal source), %.1f dB (38k)", a, g0, g1);
	}
	{
		Setup (c, h, 5, 5, 5, 0, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 1000.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
