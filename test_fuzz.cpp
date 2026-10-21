// test_fuzz.cpp - Fuzz Face Real: bias and gains against ElectroSmash, behaviour as the Fuzz varies, asymmetric clipping, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_fuzz.cpp fx_fuzzface.cpp -o test_fuzz
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void FuzzBuildForTest (Ck::TCkt &c, int *h, const float *p);
void FuzzPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// h: 0 in, 1 out(wv), 6 c1, 7 c2, 8 e2, 9 b1, 10 nout, 11 in
static void Setup (Ck::TCkt &c, int *h, float fuzz, float vol, float hot, float gtr)
{
	c = Ck::TCkt ();
	const float p[4] = {fuzz, vol, hot, gtr};
	FuzzBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	FuzzPotsForTest (c, h, p);
	c.Start ();
}

// amplitude at frequency f at the nodes: c1 (first stage output), c2 (Q2 collector), output
static void Gains (Ck::TCkt &c, const int *h, double f, double amp, double *g)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn[4] = {0}, cs[4] = {0};
	const int nodes[4] = {h[6], h[7], h[1], h[11]};
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		c.Run (h[0], amp * sin (th));
		for (int k = 0; k < 4; k++) { const double v = c.Probe (nodes[k]); sn[k] += v * sin (th); cs[k] += v * cos (th); }
	}
	for (int k = 0; k < 4; k++) g[k] = Db (2.0 * sqrt (sn[k] * sn[k] + cs[k] * cs[k]) / M / amp);
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 5, 10, 0, 0);
	printf ("bias, Fuzz at half (V; ElectroSmash in brackets): Q1 base %.2f (-0.2), Q1 coll %.2f (-0.7), Q2 emitter %.2f (-0.5), Q2 coll %.2f (-4.5), Q2 output %.2f (-8.7)\n",
		c.Probe (h[9]), c.Probe (h[6]), c.Probe (h[8]), c.Probe (h[7]), c.Probe (h[10]));
	CHECK (fabs (c.Probe (h[9]) + 0.2) < 0.12 && fabs (c.Probe (h[6]) + 0.7) < 0.35 && fabs (c.Probe (h[7]) + 4.5) < 1.5, "bias close to ElectroSmash's");
	printf ("small signal 1 kHz (0.3 mV): gains in dB; Fuzz  Q1coll  Q2coll  output(Volume 10)  Zin(kohm)\n");
	double g10[4] = {0}, g0[4] = {0}, g5[4] = {0};
	for (float fz = 0; fz <= 10.0f; fz += 2.5f)
	{
		Setup (c, h, fz, 10, 0, 0);
		double g[4];
		Gains (c, h, 1000.0, 0.0003, g);
		// input impedance: with a 1 ohm source the voltage at "in" is almost the generator's: it uses the load of an 8k source
		Setup (c, h, fz, 10, 0, 1);
		double gz[4];
		Gains (c, h, 1000.0, 0.0003, gz);
		const double ratio = pow (10.0, gz[3] / 20.0);		// V(in)/Vsource con Rs = 8k: Zin = 8k * ratio / (1 - ratio)
		const double zin = 8.0 * ratio / (1.0 - ratio);
		printf ("  %4.1f   %6.1f  %6.1f  %6.1f   %6.1f\n", fz, g[0], g[1], g[2], zin);
		if (fz == 0) for (int k = 0; k < 4; k++) g0[k] = g[k];
		if (fz == 5) for (int k = 0; k < 4; k++) g5[k] = g[k];
		if (fz == 10) for (int k = 0; k < 4; k++) g10[k] = g[k];
	}
	// ElectroSmash gives 18.6 dB for the first stage and -5.5 dB for the pedal: with their simulation (source not specified) it cannot be reproduced with any reasonable source; here the gain is gm x load (about 35 dB at Fuzz 10 with Q2 loading Q1's collector with about 9 kohm)
	CHECK (g10[0] > 25 && g10[0] < 50, "first stage at Fuzz 10: %.1f dB (gm x load expected about 35 dB; ElectroSmash states 18.6 dB)", g10[0]);
	CHECK (g10[1] > g0[1] + 2.0, "the Fuzz raises the gain (Q2: %.1f dB at 0, %.1f dB at 10)", g0[1], g10[1]);
	CHECK (g10[2] > g10[1] - 30.0 && g10[2] < g10[1] - 20.0, "output at Fuzz 10 and Volume 10: %.1f dB, i.e. %.1f dB below Q2's collector (R2/R3 divider: -25 dB)", g10[2], g10[1] - g10[2]);
	// clipping: input 100 mV at 220 Hz, Fuzz 10: asymmetry of the peaks at Q2's collector and at the output
	Setup (c, h, 10, 10, 0, 0);
	double pmax = -1e9, pmin = 1e9, omax = -1e9, omin = 1e9;
	for (int i = 0; i < 48000; i++)
	{
		const double y = c.Run (h[0], 0.1 * sin (2 * M_PI * 220.0 * i / FS));
		if (i > 24000) { const double v = c.Probe (h[7]); if (v > pmax) pmax = v; if (v < pmin) pmin = v; if (y > omax) omax = y; if (y < omin) omin = y; }
	}
	printf ("input 100 mV at 220 Hz, Fuzz 10: Q2 collector from %.2f to %.2f V; output from %.2f to %.2f V\n", pmin, pmax, omin, omax);
	CHECK (omax - omin > 0.05 && omax - omin < 8.0 && fabs (omax + omin) < 0.9 * (omax - omin), "output limited and asymmetric (%.2f ... %.2f V)", omin, omax);
	// all combinations: it converges and does not blow up
	for (int hot = 0; hot < 2; hot++) for (int g = 0; g < 2; g++) for (int fz = 0; fz <= 10; fz += 5)
	{
		Setup (c, h, (float) fz, 7, (float) hot, (float) g);
		double mx = 0; int itmax = 0;
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 110.0 * i / FS) * sin (2 * M_PI * 1.3 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 10.0 && itmax < 40, "hot %d source %d Fuzz %d: peak %.2f V, iterations %d", hot, g, fz, mx, itmax);
	}
	{
		Setup (c, h, 10, 7, 0, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.3 * sin (2 * M_PI * 440.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
