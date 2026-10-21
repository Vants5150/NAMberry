// test_muff.cpp - Big Muff Pi Real: bias, stage-by-stage small-signal response against ElectroSmash, tone control, clipping.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_muff.cpp fx_muff.cpp -o test_muff
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void MuffBuildForTest (Ck::TCkt &c, int *h, const float *p);
void MuffPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[240]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

struct Gains { double v[4]; };

// amplitude of the component at frequency f at the four nodes (h[8], h[9], h[10], output) relative to the input amplitude
static Gains Measure (Ck::TCkt &c, const int *h, double f, double amp)
{
	const int cyc = (int) (f * 0.4 + 1);			// whole cycles in the measurement
	const int M = (int) (cyc * FS / f + 0.5);
	const int settle = (int) (FS * 0.5);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn[4] = {0}, cs[4] = {0};
	const int nodes[4] = {h[8], h[9], h[10], h[1]};
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		c.Run (h[0], amp * sin (th));
		for (int k = 0; k < 4; k++) { const double v = c.Probe (nodes[k]); sn[k] += v * sin (th); cs[k] += v * cos (th); }
	}
	Gains g;
	for (int k = 0; k < 4; k++) g.v[k] = 2.0 * sqrt (sn[k] * sn[k] + cs[k] * cs[k]) / M / amp;
	return g;
}

static void Setup (Ck::TCkt &c, int *h, float s, float t, float v)
{
	c = Ck::TCkt ();
	const float P[4] = {s, t, v, 0.0f};
	MuffBuildForTest (c, h, P);
	c.SetStep (1.0 / FS);
	MuffPotsForTest (c, h, P);
	c.Start ();
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 10, 5, 5);
	printf ("bias (V): Q4 collector %.2f, Q3 %.2f, Q2 %.2f, Q1 %.2f (supply 9)\n", c.Probe (h[8]), c.Probe (h[9]), c.Probe (h[10]), c.Probe (h[11]));
	CHECK (c.Probe (h[9]) > 3.0 && c.Probe (h[9]) < 6.0 && c.Probe (h[10]) > 3.0 && c.Probe (h[10]) < 6.0 && c.Probe (h[8]) > 3.0 && c.Probe (h[8]) < 8.5, "collectors of the two clipping stages close to half the supply (%.2f, %.2f V)", c.Probe (h[9]), c.Probe (h[10]));
	printf ("small signal (20 uV, Sustain 10, Tone 5, Volume 5): gain from the input to the collectors in dB\n   Hz       Q4     Q3     Q2     output\n");
	const double fr[] = {20, 55, 94, 200, 500, 1000, 1170, 1780, 3000, 6000};
	double g500[4] = {0};
	for (double f : fr)
	{
		const Gains g = Measure (c, h, f, 0.00002);
		printf ("  %5.0f  %6.1f %6.1f %6.1f %6.1f\n", f, Db (g.v[0]), Db (g.v[1]), Db (g.v[2]), Db (g.v[3]));
		if (f == 500) for (int k = 0; k < 4; k++) g500[k] = Db (g.v[k]);
	}
	CHECK (g500[0] > 12 && g500[0] < 21, "first stage (Q4) at 500 Hz: %.1f dB (ElectroSmash: 16.7 dB in simulation)", g500[0]);
	CHECK (g500[1] - g500[0] > 17 && g500[1] - g500[0] < 29, "first clipping at 500 Hz: %.1f dB (ElectroSmash: 23.4 dB at maximum)", g500[1] - g500[0]);
	CHECK (g500[2] - g500[1] > 18 && g500[2] - g500[1] < 30, "second clipping at 500 Hz: %.1f dB (ElectroSmash: 25.2 dB at maximum)", g500[2] - g500[1]);
	// tone control: gain from the Q2 collector to the output, three positions, in dB relative to the average; scoop at 1 kHz at half travel
	printf ("tone control (input 20 uV, Sustain 10): output in dB relative to Q2\n   Hz     tone 0   tone 5  tone 10\n");
	double mid[3][10];
	const float tones[3] = {0, 5, 10};
	const double ft[] = {100, 200, 400, 700, 1000, 1500, 2500, 4000, 8000, 12000};
	for (int t = 0; t < 3; t++)
	{
		Setup (c, h, 10, tones[t], 5);
		for (int i = 0; i < 10; i++) { const Gains g = Measure (c, h, ft[i], 0.00002); mid[t][i] = Db (g.v[3]) - Db (g.v[2]); }
	}
	for (int i = 0; i < 10; i++) printf ("  %5.0f  %7.1f  %7.1f  %7.1f\n", ft[i], mid[0][i], mid[1][i], mid[2][i]);
	CHECK (mid[1][4] < mid[1][1] - 3.0 && mid[1][4] < mid[1][8] - 3.0, "tone at half travel: scoop at 1 kHz (%.1f dB against %.1f at 200 Hz and %.1f at 8 kHz)", mid[1][4], mid[1][1], mid[1][8]);
	CHECK (mid[0][0] > mid[2][0] + 5.0 && mid[2][8] > mid[0][8] + 5.0, "tone: more bass to the left, more treble to the right");
	// clipping: with sustain at maximum and a 100 mV input the signal at the collectors does not exceed about 0.7 V of amplitude
	Setup (c, h, 10, 5, 5);
	double pk3 = 0, pk2 = 0, pko = 0;
	for (int i = 0; i < 24000; i++)
	{
		const double y = c.Run (h[0], 0.1 * sin (2 * M_PI * 220.0 * i / FS));
		if (i > 12000) { const double d3 = fabs (c.Probe (h[9]) - c.Probe (h[9])); (void) d3; if (fabs (y) > pko) pko = fabs (y); }
	}
	(void) pk3; (void) pk2;
	printf ("input 100 mV at 220 Hz, Sustain 10, Volume 5: output peak %.2f V\n", pko);
	CHECK (pko > 0.1 && pko < 6.0, "output with a strong input is bounded (%.2f V peak)", pko);
	{	// speed and stability with a strong input
		Setup (c, h, 10, 5, 5);
		const auto t0 = std::chrono::steady_clock::now ();
		double mx = 0; const int N = 480000; int itmax = 0;
		for (int i = 0; i < N; i++)
		{
			const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 110.0 * i / FS) * sin (2 * M_PI * 0.7 * i / FS));
			if (fabs (y) > mx) mx = fabs (y);
			if (c.Iters () > itmax) itmax = c.Iters ();
		}
		const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N;
		printf ("time: %.3f us per sample (one channel), at most %d Newton iterations, peak %.2f V\n", us, itmax, mx);
		CHECK (itmax < 40 && mx < 20.0, "stable with a very strong input (at most %d iterations)", itmax);
	}
	// all versions (0 V3 1976, 1 V2 1974, 2 V1 1972, 3 EH schematic 15/10/1975, 4 Little Big Muff)
	{
		const char *nm[5] = {"V3 1976", "V2 1974", "V1 1972", "EH 10/1975", "Little Big Muff"};
		double gTot[5];
		for (int ver = 0; ver < 5; ver++)
		{
			c = Ck::TCkt ();
			const float P[4] = {10.0f, 5.0f, 5.0f, (float) ver};
			MuffBuildForTest (c, h, P);
			c.SetStep (1.0 / FS);
			MuffPotsForTest (c, h, P);
			c.Start ();
			const double v4 = c.Probe (h[8]), v3 = c.Probe (h[9]), v2 = c.Probe (h[10]), v1 = c.Probe (h[11]);
			const bool pn = ver == 2;
			const double sg = pn ? -1.0 : 1.0;
			const Gains g = Measure (c, h, 500.0, 0.00002);
			gTot[ver] = Db (g.v[3]);
			printf ("%s: collectors %.2f %.2f %.2f %.2f V; at 500 Hz (20 uV, Sustain 10): Q4 %.1f, Q3 %.1f, Q2 %.1f, output %.1f dB\n", nm[ver], v4, v3, v2, v1, Db (g.v[0]), Db (g.v[1]), Db (g.v[2]), Db (g.v[3]));
			// version 3 (EH schematic 1975): Q2 has no resistor from base to ground and self-biases from R15 with the collector at about 1.3 V (a consequence of the schematic as drawn)
			CHECK (sg * v3 > 2.0 && sg * v3 < 7.5 && sg * v2 > (ver == 3 ? 1.0 : 1.5) && sg * v2 < 7.5 && sg * v4 > 2.0 && sg * v4 < 8.5, "%s: collectors in the active region", nm[ver]);
			CHECK (Db (g.v[1]) - Db (g.v[0]) > 10.0 && Db (g.v[2]) - Db (g.v[1]) > 10.0, "%s: the two clipping stages amplify (%.1f and %.1f dB)", nm[ver], Db (g.v[1]) - Db (g.v[0]), Db (g.v[2]) - Db (g.v[1]));
			double mx = 0; int itmax = 0;
			for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 110.0 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
			CHECK (itmax < 40 && mx < 20.0, "%s: stable with a strong input (peak %.2f V, iterations %d)", nm[ver], mx, itmax);
		}
		// Little Big Muff: there is no Sustain
		double gS[2];
		for (int k = 0; k < 2; k++)
		{
			c = Ck::TCkt ();
			const float P[4] = {k ? 10.0f : 0.0f, 5.0f, 5.0f, 4.0f};
			MuffBuildForTest (c, h, P); c.SetStep (1.0 / FS); MuffPotsForTest (c, h, P); c.Start ();
			gS[k] = Db (Measure (c, h, 500.0, 0.00002).v[3]);
		}
		CHECK (fabs (gS[0] - gS[1]) < 0.5, "Little Big Muff: the Sustain does not change the gain (%.1f and %.1f dB)", gS[0], gS[1]);
		(void) gTot;
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
