// test_wah.cpp - Wah Real: bias against ElectroSmash, resonance peak as the pedal varies (450 Hz - 1.6 kHz, up to 18 dB), options, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_wah.cpp fx_wah.cpp -o test_wah
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void WahBuildForTest (Ck::TCkt &c, int *h, const float *p);
void WahPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float pos, float q, float rng, float buf)
{
	c = Ck::TCkt ();
	const float p[4] = {pos, q, rng, buf};
	WahBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	WahPotsForTest (c, h, p);
	c.Start ();
}

static double Gain (Ck::TCkt &c, const int *h, double f, double amp)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.5);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		const double y = c.Run (h[0], amp * sin (th));
		sn += y * sin (th); cs += y * cos (th);
	}
	return Db (2.0 * sqrt (sn * sn + cs * cs) / M / amp);
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 5, 0, 0, 1);
	printf ("bias (V; ElectroSmash in brackets): Q0 collector %.2f (8.6), Q0 emitter %.2f (3), Q1 collector %.2f (4.9), Q1 emitter %.3f (0.06), node L1 %.2f (0.6), Q2 base %.2f (4.3)\n",
		c.Probe (h[11]), c.Probe (h[10]), c.Probe (h[5]), c.Probe (h[9]), c.Probe (h[6]), c.Probe (h[7]));
	CHECK (fabs (c.Probe (h[5]) - 4.9) < 1.0 && fabs (c.Probe (h[6]) - 0.6) < 0.3 && fabs (c.Probe (h[10]) - 3.0) < 1.0, "bias close to ElectroSmash's");
	printf ("response as the pedal varies (input 3 mV):\n   pedal   peak(Hz)  gain at peak(dB)\n");
	double pk[3] = {0}, pg[3] = {0};
	const float pv[3] = {0, 5, 10};
	for (int k = 0; k < 3; k++)
	{
		Setup (c, h, pv[k], 0, 0, 1);
		double best = -1e9, bf = 0;
		for (double f = 250; f <= 3000; f *= 1.06) { const double g = Gain (c, h, f, 0.003); if (g > best) { best = g; bf = f; } }
		pk[k] = bf; pg[k] = best;
		printf ("  %5.1f   %8.0f   %6.1f\n", pv[k], bf, best);
	}
	CHECK (pk[0] < pk[1] && pk[1] < pk[2], "the peak rises with the pedal toe (%.0f / %.0f / %.0f Hz)", pk[0], pk[1], pk[2]);
	CHECK (pk[0] > 300 && pk[0] < 700 && pk[2] > 1100 && pk[2] < 2400, "range %.0f - %.0f Hz (ElectroSmash: 450 Hz - 1.6 kHz, 750 Hz at half)", pk[0], pk[2]);
	CHECK (pg[1] > 15.0 && pg[1] < 25.0 && fabs (pg[2] - pg[0]) < 6.0, "gain at the peak %.1f / %.1f / %.1f dB along the travel (ElectroSmash: 18-22 dB)", pg[0], pg[1], pg[2]);
	CHECK (pk[1] > 600 && pk[1] < 900, "peak at half travel at %.0f Hz (ElectroSmash: 750 Hz)", pk[1]);
	// Q: larger R7 = narrower and higher peak
	double gq[2];
	for (int k = 0; k < 2; k++) { Setup (c, h, 5, k ? 3.0f : 0.0f, 0, 1); double best = -1e9; for (double f = 400; f <= 1400; f *= 1.05) { const double g = Gain (c, h, f, 0.003); if (g > best) best = g; } gq[k] = best; }
	CHECK (gq[1] > gq[0] + 1.0, "Q: R7 100k against 33k, peak %.1f against %.1f dB", gq[1], gq[0]);
	// range: C2 0.1 uF lowers the sweep
	double pr[2];
	for (int k = 0; k < 2; k++) { Setup (c, h, 5, 0, k ? 1.0f : 0.0f, 1); double best = -1e9, bf = 0; for (double f = 80; f <= 3000; f *= 1.06) { const double g = Gain (c, h, f, 0.003); if (g > best) { best = g; bf = f; } } pr[k] = bf; }
	CHECK (pr[1] < pr[0] * 0.7, "range: with C2 0.1 uF the peak drops from %.0f to %.0f Hz", pr[0], pr[1]);
	for (int buf = 0; buf < 2; buf++) for (int p = 0; p <= 10; p += 5)
	{
		Setup (c, h, (float) p, 0, 0, (float) buf);
		double mx = 0; int itmax = 0, f0 = c.Fails();
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 700.0 * i / FS) * sin (2 * M_PI * 1.3 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 12.0 && c.Fails () == f0, "buffer %d pedal %d: peak %.2f V, iterations %d, failures %d", buf, p, mx, itmax, c.Fails () - f0);
	}
	{
		Setup (c, h, 5, 0, 0, 1);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.05 * sin (2 * M_PI * 700.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}