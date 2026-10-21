// test_guv.cpp - Guv'nor Real: stage-by-stage gains against ElectroSmash, tone stack, LED clipping, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_guv.cpp fx_guvnor.cpp -o test_guv
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void GuvBuildForTest (Ck::TCkt &c, int *h, const float *p);
void GuvPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float g, float b, float m, float t, float l, float d)
{
	c = Ck::TCkt ();
	const float p[6] = {g, b, m, t, l, d};
	GuvBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	GuvPotsForTest (c, h, p);
	c.Start ();
}

// amplitudes at frequency f: o1 (first stage), o2 (second), tin (after the LEDs), tw (tone output), output
static void Gains (Ck::TCkt &c, const int *h, double f, double amp, double *g)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn[5] = {0}, cs[5] = {0};
	const int nodes[5] = {h[11], h[12], h[13], h[15], h[1]};
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		c.Run (h[0], amp * sin (th));
		for (int k = 0; k < 5; k++) { const double v = c.Probe (nodes[k]); sn[k] += v * sin (th); cs[k] += v * cos (th); }
	}
	for (int k = 0; k < 5; k++) g[k] = Db (2.0 * sqrt (sn[k] * sn[k] + cs[k] * cs[k]) / M / amp);
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 10, 5, 5, 5, 10, 0);
	printf ("bias: virtual 4.5 V %.2f, first stage output %.2f, second %.2f V\n", c.Probe (h[14]), c.Probe (h[11]), c.Probe (h[12]));
	CHECK (fabs (c.Probe (h[14]) - 4.5) < 0.05 && fabs (c.Probe (h[11]) - 4.5) < 0.3 && fabs (c.Probe (h[12]) - 4.5) < 0.3, "op-amps biased at 4.5 V");
	printf ("small signal (Gain 10, tone at half, input 20 uV): dB\n   Hz    stage 1   stage 2    tone    output(Level 10)\n");
	double g[5], s1 = 0, s2 = 0;
	const double fr[] = {50, 100, 200, 500, 700, 1000, 2000, 4000, 8000};
	for (double f : fr)
	{
		Gains (c, h, f, 0.00002, g);
		printf ("  %6.0f %8.1f %10.1f %8.1f %8.1f\n", f, g[0], g[1] - g[0], g[3] - g[2], g[4]);
		if (f == 1000) { s1 = g[0]; s2 = g[1] - g[0]; }
	}
	CHECK (s1 > 28 && s1 < 36, "first stage at 1 kHz with Gain 10: %.1f dB (ElectroSmash: up to 33.3 dB)", s1);
	CHECK (s2 > 30 && s2 < 38, "second stage at 1 kHz: %.1f dB (ElectroSmash: 36 dB)", s2);
	// Gain: 0 -> 33 dB
	double gg[3];
	const float gv[3] = {0, 5, 10};
	for (int i = 0; i < 3; i++) { Setup (c, h, gv[i], 5, 5, 5, 10, 0); Gains (c, h, 1000.0, 0.00002, g); gg[i] = g[0]; }
	CHECK (gg[0] < 3.0 && gg[1] > gg[0] + 15.0 && gg[2] > gg[1] + 5.0, "Gain 0/5/10 -> %.1f/%.1f/%.1f dB in the first stage (from 0 to 33.3)", gg[0], gg[1], gg[2]);
	// LED clipping: input 20 mV at 440 Hz, Gain 10: after R6 the peak is around 1.7-2 V
	Setup (c, h, 10, 5, 5, 5, 10, 0);
	double pk = 0, pk2 = 0;
	for (int i = 0; i < 48000; i++) { c.Run (h[0], 0.02 * sin (2 * M_PI * 440.0 * i / FS)); if (i > 24000) { const double v = fabs (c.Probe (h[13])); if (v > pk) pk = v; const double w = fabs (c.Probe (h[12]) - c.Probe (h[14])); if (w > pk2) pk2 = w; } }
	printf ("input 20 mV, Gain 10: second stage %.2f V peak, after the LEDs %.2f V (ElectroSmash: clip at 1.8-2 V)\n", pk2, pk);
	CHECK (pk > 1.3 && pk < 2.3, "red LED clipping at %.2f V", pk);
	// different diodes: lower threshold
	double pkd[3];
	for (int d = 0; d < 3; d++)
	{
		Setup (c, h, 10, 5, 5, 5, 10, (float) d);
		pkd[d] = 0;
		for (int i = 0; i < 48000; i++) { c.Run (h[0], 0.02 * sin (2 * M_PI * 440.0 * i / FS)); if (i > 24000) { const double v = fabs (c.Probe (h[13])); if (v > pkd[d]) pkd[d] = v; } }
	}
	CHECK (pkd[0] > pkd[1] + 0.5 && pkd[1] > pkd[2] + 0.2, "threshold LED %.2f > 1N4148 %.2f > germanium %.2f V", pkd[0], pkd[1], pkd[2]);
	// tone stack: bass and treble
	double tb[2], tt[2];
	for (int k = 0; k < 2; k++)
	{
		Setup (c, h, 0, k ? 10.0f : 0.0f, 5, 5, 10, 0); Gains (c, h, 80.0, 0.01, g); tb[k] = g[3] - g[2];
		Setup (c, h, 0, 5, 5, k ? 10.0f : 0.0f, 10, 0); Gains (c, h, 5000.0, 0.01, g); tt[k] = g[3] - g[2];
	}
	CHECK (tb[1] > tb[0] + 6.0, "Bass 0 -> 10: a 80 Hz da %.1f a %.1f dB", tb[0], tb[1]);
	double tm[2];
	for (int k = 0; k < 2; k++) { Setup (c, h, 0, 5, k ? 10.0f : 0.0f, 5, 10, 0); Gains (c, h, 700.0, 0.01, g); tm[k] = g[3] - g[2]; }
	CHECK (tm[1] > tm[0] + 3.0, "Mids 0 -> 10: a 700 Hz da %.1f a %.1f dB", tm[0], tm[1]);
	CHECK (tt[1] > tt[0] + 6.0, "Treble 0 -> 10: a 5 kHz da %.1f a %.1f dB", tt[0], tt[1]);
	for (int gain = 0; gain <= 10; gain += 5) for (int d = 0; d < 3; d++)
	{
		Setup (c, h, (float) gain, 5, 5, 5, 10, (float) d);
		double mx = 0; int itmax = 0, f0 = c.Fails();
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 130.0 * i / FS) * sin (2 * M_PI * 1.1 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 10.0 && c.Fails () == f0, "Gain %d diodes %d: peak %.2f V, iterations %d, failures %d", gain, d, mx, itmax, c.Fails () - f0);
	}
	{
		Setup (c, h, 10, 5, 5, 5, 10, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}