// test_dynacomp.cpp - Dyna Comp Real: bias against ElectroSmash, compression curve, Sensitivity, attack and release, mods, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_dynacomp.cpp fx_dynacomp.cpp -o test_dynacomp
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void DynaCompBuildForTest (Ck::TCkt &c, int *h, const float *p);
void DynaCompPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float sens, float level, float c5, float half, float c9, float r13)
{
	c = Ck::TCkt ();
	const float p[6] = {sens, level, c5, half, c9, r13};
	DynaCompBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	DynaCompPotsForTest (c, h, p);
	c.Start ();
}

// output amplitude (peak V) at steady state after "settle" seconds, measured over 0.1 s
static double Out (Ck::TCkt &c, const int *h, double f, double amp, double settle, double *pE = 0)
{
	const int ns = (int) (settle * FS), M = (int) (0.1 * FS);
	for (int i = 0; i < ns; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (ns + i) / FS;
		const double v = c.Run (h[0], amp * sin (th)); sn += v * sin (th); cs += v * cos (th);
	}
	if (pE) *pE = c.Probe (h[7]);
	return 2.0 * sqrt (sn * sn + cs * cs) / M;
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 7, 7, 0, 0, 0, 0);
	printf ("operating points (ElectroSmash in brackets): reference %.2f V (2.54), OTA input %.2f V (3.1-4), detector node %.2f V (high at rest), Q5 emitter %.2f V, pin 5 %.2f V, Q2 base %.2f V, Q2 emitter %.2f V\n",
		c.Probe (h[14]), c.Probe (h[8]), c.Probe (h[7]), c.Probe (h[11]), c.Probe (h[12]), c.Probe (h[10]), c.Probe (h[13]));
	CHECK (fabs (c.Probe (h[14]) - 2.54) < 0.15, "reference 2.54 V");
	CHECK (c.Probe (h[8]) > 2.5 && c.Probe (h[8]) < 4.5, "bias of the OTA inputs %.2f V", c.Probe (h[8]));
	CHECK (c.Probe (h[7]) > 6.0, "at rest the detector does not conduct (node %.2f V)", c.Probe (h[7]));
	// compression curve, Sensitivity 7, Output 7, 1 kHz
	{
		printf ("compression curve (Sens 7, Output 7, 1 kHz):\n   input(mV)  output(mV)  gain(dB)  node E(V)\n");
		const double amp[] = {0.0003, 0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 1.0};
		double o[8]; double ev[8];
		for (int i = 0; i < 8; i++)
		{
			o[i] = Out (c, h, 1000.0, amp[i], i ? 1.5 : 3.0, &ev[i]);
			printf ("   %10.2f %10.2f %10.1f %9.2f\n", amp[i] * 1e3, o[i] * 1e3, Db (o[i] / amp[i]), ev[i]);
		}
		const double slope = (Db (o[6]) - Db (o[3])) / (Db (amp[6]) - Db (amp[3]));
		CHECK (slope < 0.6 && slope > 0.0, "slope between 10 and 300 mV: %.2f dB/dB (1 = no compression)", slope);
		CHECK (Db (o[0] / amp[0]) > 3.0, "small-signal gain %.1f dB", Db (o[0] / amp[0]));
		CHECK (ev[7] < ev[0] - 2.0, "the detector falls with the signal (%.2f -> %.2f V)", ev[0], ev[7]);
	}
	// Sensitivity
	{
		double g[3];
		const float sv[3] = {1, 5, 10};
		for (int i = 0; i < 3; i++) { Setup (c, h, sv[i], 7, 0, 0, 0, 0); g[i] = Db (Out (c, h, 1000.0, 0.003, 3.0) / 0.003); }
		printf ("gain at 3 mV: Sens 1/5/10 -> %.1f/%.1f/%.1f dB\n", g[0], g[1], g[2]);
		CHECK (g[2] > g[0] + 6.0, "Sensitivity: more gain clockwise");
	}
	// attack and release: 0.3 s at 0.3 V, then 10 mV: time for the gain to climb back
	{
		Setup (c, h, 7, 7, 0, 0, 0, 0);
		for (int i = 0; i < (int) (2.0 * FS); i++) c.Run (h[0], 0.3 * sin (2 * M_PI * 1000.0 * i / FS));
		double t10 = 0, t90 = 0;
		const double gLow = Out (c, h, 1000.0, 0.01, 6.0) / 0.01;		// steady-state gain with 10 mV (after letting it recover)
		Setup (c, h, 7, 7, 0, 0, 0, 0);
		for (int i = 0; i < (int) (2.0 * FS); i++) c.Run (h[0], 0.3 * sin (2 * M_PI * 1000.0 * i / FS));
		double gAfter = 0; bool b10 = false, b90 = false;
		const int N = (int) (8.0 * FS);
		for (int i = 0; i < N; i += 480)
		{
			double sn = 0, cs = 0;
			for (int k = 0; k < 480; k++) { const double th = 2 * M_PI * 1000.0 * (i + k) / FS; const double v = c.Run (h[0], 0.01 * sin (th)); sn += v * sin (th); cs += v * cos (th); }
			gAfter = 2.0 * sqrt (sn * sn + cs * cs) / 480 / 0.01;
			const double t = (i + 480) / FS;
			if (!b10 && gAfter > gLow * 0.316) { b10 = true; t10 = t; }
			if (!b90 && gAfter > gLow * 0.891) { b90 = true; t90 = t; }
		}
		printf ("release after 0.3 V -> 10 mV: steady-state gain %.1f dB; -10 dB after %.2f s, -1 dB after %.2f s\n", Db (gLow), t10, t90);
		CHECK (b90 && t90 > 0.2 && t90 < 8.0, "full release in %.2f s", t90);
	}
	// mods: C5, single half-wave detector, C9, R13
	{
		double ghi[4];
		for (int i = 0; i < 4; i++) { Setup (c, h, 7, 7, (float) i, 0, 0, 0); ghi[i] = Db (Out (c, h, 8000.0, 0.003, 3.0) / 0.003); }
		printf ("C5 1n/470p/100p/none: gain at 8 kHz %.1f/%.1f/%.1f/%.1f dB\n", ghi[0], ghi[1], ghi[2], ghi[3]);
		CHECK (ghi[3] > ghi[0] + 3.0, "without C5 more treble");
		Setup (c, h, 7, 7, 0, 0, 1, 0);
		const double glo9 = Db (Out (c, h, 60.0, 0.003, 3.0) / 0.003);
		Setup (c, h, 7, 7, 0, 0, 0, 0);
		const double glo = Db (Out (c, h, 60.0, 0.003, 3.0) / 0.003);
		printf ("C9 50 nF / 1 uF: gain at 60 Hz %.1f / %.1f dB\n", glo, glo9);
		CHECK (glo9 > glo + 1.5, "C9 of 1 uF: more bass");
	}
	for (int half = 0; half < 2; half++) for (int r = 0; r < 2; r++) for (int s = 0; s <= 10; s += 5)
	{
		Setup (c, h, (float) s, 7, 0, (float) half, 0, (float) r);
		double mx = 0; int itmax = 0;
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.8 * sin (2 * M_PI * 700.0 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 12.0 && itmax < 40 && c.Fails () == 0, "half-wave %d R13 %d Sens %d: peak %.2f V, iterations %d, failures %d", half, r, s, mx, itmax, c.Fails ());
	}
	{
		Setup (c, h, 7, 7, 0, 0, 0, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 1000.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
