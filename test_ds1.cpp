// test_ds1.cpp - DS-1 Real: bias, stage-by-stage gains against ElectroSmash, tone/level, clipping, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_ds1.cpp fx_ds1.cpp -o test_ds1
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void Ds1BuildForTest (Ck::TCkt &c, int *h, const float *p);
void Ds1PotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float d, float t, float l)
{
	c = Ck::TCkt ();
	const float p[3] = {d, t, l};
	Ds1BuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	Ds1PotsForTest (c, h, p);
	c.Start ();
}

// amplitude at frequency f at the nodes: c2 (booster), u2o (op-amp), tw (Tone wiper), output
static void Gains (Ck::TCkt &c, const int *h, double f, double amp, double *g)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn[5] = {0}, cs[5] = {0};
	const int nodes[5] = {h[8], h[9], h[11], h[1], h[10]};
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
	Setup (c, h, 10, 5, 5);
	printf ("bias (V): rail %.2f (9 minus D1), virtual 4.5 V %.2f, Q2 collector %.2f (ElectroSmash: 5.8), op-amp output %.2f\n", c.Probe (3) + 0.0, c.Probe (h[12]), c.Probe (h[8]), c.Probe (h[9]));
	CHECK (c.Probe (h[8]) > 4.0 && c.Probe (h[8]) < 7.5, "Q2 collector close to 5.8 V (%.2f V)", c.Probe (h[8]));
	printf ("small signal (Dist 10, Tone 5, Level 5, input 0.2 mV), dB relative to the input: booster, op-amp output, Tone wiper, output\n  Hz     booster   op-amp   tone-w   output\n");
	double g[5], gb = 0, go = 0;
	const double fr[] = {30, 72, 200, 500, 1000, 2000, 4000, 7200, 12000};
	for (double f : fr)
	{
		Gains (c, h, f, 0.0002, g);
		printf ("  %6.0f %8.1f %8.1f %8.1f %8.1f\n", f, g[0], g[1] - g[0], g[2] - g[4], g[3]);
		if (f == 2000) { gb = g[0]; go = g[1] - g[0]; }
	}
	CHECK (gb > 25 && gb < 45, "booster a 2 kHz: %.1f dB (ElectroSmash: 35 dB)", gb);
	CHECK (go > 20 && go < 29, "op-amp stage at Dist 10 at 2 kHz: %.1f dB (ElectroSmash: up to 26.5 dB)", go);
	// Dist: the op-amp's gain grows
	double gd[3];
	const float dv[3] = {0, 5, 10};
	for (int i = 0; i < 3; i++) { Setup (c, h, dv[i], 5, 5); Gains (c, h, 2000.0, 0.0002, g); gd[i] = g[1] - g[0]; }
	CHECK (gd[0] < 3.0 && gd[2] > gd[1] + 5.0 && gd[1] > gd[0] + 5.0, "Dist 0/5/10 -> %.1f/%.1f/%.1f dB of gain in the op-amp (from 0 to 26.5)", gd[0], gd[1], gd[2]);
	// tone: scoop at about 500 Hz at half travel
	Setup (c, h, 0, 5, 10);
	double t200, t500, t5k, t30, t700, t10k;
	Gains (c, h, 30.0, 0.01, g); t30 = g[2] - g[4];
	Gains (c, h, 200.0, 0.01, g); t200 = g[2] - g[4];
	Gains (c, h, 500.0, 0.01, g); t500 = g[2] - g[4];
	Gains (c, h, 700.0, 0.01, g); t700 = g[2] - g[4];
	Gains (c, h, 5000.0, 0.01, g); t5k = g[2] - g[4];
	Gains (c, h, 10000.0, 0.01, g); t10k = g[2] - g[4];
	printf ("tone stage at half travel (Tone wiper relative to the input, ElectroSmash: -14 dB in the bass, -20.8 dB scoop at about 650 Hz, -14.5 dB in the treble with Level at maximum): 30 Hz %.1f, 200 Hz %.1f, 500 Hz %.1f, 700 Hz %.1f, 5 kHz %.1f, 10 kHz %.1f dB\n", t30, t200, t500, t700, t5k, t10k);
	CHECK (fmin (t500, t700) < t30 - 4.0 && fmin (t500, t700) < t10k - 4.0, "tone scoop between 500 and 700 Hz (%.1f against %.1f in the bass and %.1f in the treble)", fmin (t500, t700), t30, t10k);
	// clipping: input 100 mV at 440 Hz, Dist 10: after the diodes about +-0.7 V
	Setup (c, h, 10, 5, 10);
	double dmax = -1e9, dmin = 1e9, omax = -1e9, omin = 1e9;
	for (int i = 0; i < 48000; i++)
	{
		const double y = c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS));
		if (i > 24000) { const double v = c.Probe (h[10]) - c.Probe (h[12]); if (v > dmax) dmax = v; if (v < dmin) dmin = v; if (y > omax) omax = y; if (y < omin) omin = y; }
	}
	printf ("input 100 mV at 440 Hz, Dist 10: after the diodes from %.2f to %.2f V (ElectroSmash: +-0.7 V); output from %.2f to %.2f V\n", dmin, dmax, omin, omax);
	CHECK (dmax > 0.45 && dmax < 0.85 && dmin < -0.45 && dmin > -0.85, "symmetric clipping at +-0.6/0.7 V (%.2f ... %.2f)", dmin, dmax);
	for (int d = 0; d <= 10; d += 5) for (int t = 0; t <= 10; t += 10)
	{
		Setup (c, h, (float) d, (float) t, 10);
		double mx = 0; int itmax = 0;
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.8 * sin (2 * M_PI * 130.0 * i / FS) * sin (2 * M_PI * 1.1 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 8.0 && itmax < 40, "Dist %d Tone %d: peak %.2f V, iterations %d", d, t, mx, itmax);
	}
	{
		Setup (c, h, 10, 5, 5);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}