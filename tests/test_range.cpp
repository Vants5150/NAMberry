// test_range.cpp - Rangemaster Real: bias and gain against ElectroSmash, frequency response, controls, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_range.cpp fx_rangemaster.cpp -o test_range
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void RangeBuildForTest (Ck::TCkt &c, int *h, const float *p);
void RangePotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// h: 0 in, 1 out, 7 col, 8 e, 9 b
static void Setup (Ck::TCkt &c, int *h, float range, float bias, float cap, float hot, float gtr)
{
	c = Ck::TCkt ();
	const float p[5] = {range, bias, cap, hot, gtr};
	RangeBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	RangePotsForTest (c, h, p);
	c.Start ();
}

static double Gain (Ck::TCkt &c, const int *h, double f, double amp, int node)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.5);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		c.Run (h[0], amp * sin (th));
		const double v = c.Probe (node); sn += v * sin (th); cs += v * cos (th);
	}
	return Db (2.0 * sqrt (sn * sn + cs * cs) / M / amp);
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 10, 68, 0, 0, 0);
	// values relative to the positive rail (signal ground = +9 V): ElectroSmash refers to the negative rail's ground: base +8 V, emitter +8.2 V, collector +2 V
	const double vb = 9.0 + c.Probe (h[9]), ve = 9.0 + c.Probe (h[8]), vc = 9.0 + c.Probe (h[7]);
	printf ("bias (V relative to the negative rail; ElectroSmash in brackets): base %.2f (8), emitter %.2f (8.2), collector %.2f (2)\n", vb, ve, vc);
	CHECK (fabs (vb - 8.0) < 0.5 && fabs (ve - 8.2) < 0.5 && fabs (vc - 2.0) < 1.0, "bias close to ElectroSmash's");
	printf ("pedal gain (Range 10, input 0.5 mV, ideal source), dB:\n");
	double g1k = 0, g100 = 0, g5k = 0;
	const double fr[] = {50, 100, 200, 500, 1000, 2000, 2600, 4000, 8000};
	for (double f : fr)
	{
		const double g = Gain (c, h, f, 0.0005, h[1]);
		printf ("  %6.0f Hz  %6.1f dB\n", f, g);
		if (f == 100) g100 = g;
		if (f == 1000) g1k = g;
		if (f == 8000) g5k = g;
	}
	CHECK (g5k > g1k + 3.0 && g1k > g100 + 3.0, "high-pass: gain rising towards the treble (100 Hz %.1f, 1 kHz %.1f, 8 kHz %.1f dB)", g100, g1k, g5k);
	CHECK (g5k > 30.0 && g5k < 42.0, "treble gain %.1f dB (ElectroSmash: 38 dB = gm Rc)", g5k);
	// Range
	double gr[3];
	const float rg[3] = {2, 6, 10};
	for (int i = 0; i < 3; i++) { Setup (c, h, rg[i], 68, 0, 0, 0); gr[i] = Gain (c, h, 4000.0, 0.0005, h[1]); }
	CHECK (gr[2] > gr[1] + 3.0 && gr[1] > gr[0] + 3.0, "Range: Range 2/6/10 -> %.1f/%.1f/%.1f dB", gr[0], gr[1], gr[2]);
	// input capacitor: the low cutoff drops with 47 and 100 nF
	double gc[3];
	const float cp[3] = {0, 3, 4};
	for (int i = 0; i < 3; i++) { Setup (c, h, 10, 68, cp[i], 0, 0); gc[i] = Gain (c, h, 200.0, 0.0005, h[1]); }
	CHECK (gc[2] > gc[1] + 3.0 && gc[1] > gc[0] + 10.0, "input capacitor: at 200 Hz 5/47/100 nF -> %.1f/%.1f/%.1f dB", gc[0], gc[1], gc[2]);
	// asymmetric clipping
	Setup (c, h, 10, 68, 0, 0, 0);
	double pmax = -1e9, pmin = 1e9;
	for (int i = 0; i < 48000; i++)
	{
		const double y = c.Run (h[0], 0.15 * sin (2 * M_PI * 2000.0 * i / FS));
		if (i > 24000) { if (y > pmax) pmax = y; if (y < pmin) pmin = y; }
	}
	printf ("input 150 mV at 2 kHz, Range 10: output from %.2f to %.2f V\n", pmin, pmax);
	CHECK (pmax - pmin > 0.3 && fabs (pmax + pmin) > 0.02 * (pmax - pmin), "asymmetric output (%.2f ... %.2f V)", pmin, pmax);
	for (int hot = 0; hot < 2; hot++) for (int g = 0; g < 2; g++) for (int r = 0; r <= 10; r += 5)
	{
		Setup (c, h, (float) r, 68, 0, (float) hot, (float) g);
		double mx = 0; int itmax = 0;
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.8 * sin (2 * M_PI * 1500.0 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 12.0 && itmax < 40, "hot %d source %d Range %d: peak %.2f V, iterations %d", hot, g, r, mx, itmax);
	}
	{
		Setup (c, h, 10, 68, 0, 0, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 1000.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
