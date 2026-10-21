// test_phase90.cpp - Phase 90 Real: notches against ElectroSmash (58.5 and 340.8 Hz with JFETs off), effect of R28, LFO (period, swing), notch excursion, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_phase90.cpp fx_phase90.cpp -o test_phase90
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void Phase90BuildForTest (Ck::TCkt &c, int *h, const float *p);
void Phase90ModForTest (Ck::TCkt &c, const int *h, const float *p, double *st, unsigned n, double dt);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, double *st, float speed, float bias, float fb, float vp, bool bFetOff)
{
	c = Ck::TCkt ();
	for (int k = 0; k < 8; k++) st[k] = 0.0;
	const float p[4] = {speed, bias, fb, vp};
	Phase90BuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	if (!bFetOff) Phase90ModForTest (c, h, p, st, 0, 1.0 / FS);
	c.Start ();
}

static double Gain (Ck::TCkt &c, const int *h, double f, double amp)
{
	const int cyc = (int) (f * 0.3 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.3);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		const double v = c.Run (h[0], amp * sin (th)); sn += v * sin (th); cs += v * cos (th);
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
}

// frequency and depth (dB) of the minimum in the interval
static void Notch (Ck::TCkt &c, const int *h, double f0, double f1, double *pf, double *pd)
{
	double best = 1e9, bf = f0;
	for (double f = f0; f <= f1; f *= 1.03) { const double g = Gain (c, h, f, 0.01); if (g < best) { best = g; bf = f; } }
	// refinement
	const double c0 = bf / 1.03, c1 = bf * 1.03;
	for (double f = c0; f <= c1; f *= 1.004) { const double g = Gain (c, h, f, 0.01); if (g < best) { best = g; bf = f; } }
	*pf = bf; *pd = Db (best);
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	double st[8];
	// 1. operating points and static response: JFETs off (rDS = 1 Gohm): notches expected at 58.5 and 340.8 Hz
	Setup (c, h, st, 5, 5, 0, 1, true);
	printf ("operating points: phase-shifter outputs %.2f %.2f %.2f %.2f V (Vref 5.1), buffer %.2f V, PNP collector %.2f V, base %.2f V\n", c.Probe (h[6]), c.Probe (h[7]), c.Probe (h[8]), c.Probe (h[9]), c.Probe (h[10]), c.Probe (h[11]), c.Probe (h[12]));
	CHECK (fabs (c.Probe (h[9]) - 5.1) < 0.2, "the op-amps sit at Vref at rest");
	{
		double f1, d1, f2, d2;
		Notch (c, h, 30, 150, &f1, &d1);
		Notch (c, h, 200, 800, &f2, &d2);
		printf ("notches with JFETs off: %.1f Hz (%.1f dB), %.1f Hz (%.1f dB); ElectroSmash: 58.5 and 340.8 Hz\n", f1, d1, f2, d2);
		CHECK (fabs (f1 / 58.5 - 1.0) < 0.06 && fabs (f2 / 340.8 - 1.0) < 0.06, "notches at %.1f and %.1f Hz", f1, f2);
		const double gHi = Db (Gain (c, h, 5000.0, 0.01)), gLo = Db (Gain (c, h, 15.0, 0.01));
		printf ("gain at 15 Hz %.1f dB, at 5 kHz %.1f dB (ElectroSmash: about +6 dB outside the notches, -3 dB at 1 Hz)\n", gLo, gHi);
	}
	// 2. R28 feedback
	{
		double g0[2];
		const double fr[2] = {120.0, 1000.0};
		Setup (c, h, st, 5, 5, 0, 1, true);
		for (int i = 0; i < 2; i++) g0[i] = Db (Gain (c, h, fr[i], 0.01));
		Setup (c, h, st, 5, 5, 2, 1, true);
		double g1[2];
		for (int i = 0; i < 2; i++) g1[i] = Db (Gain (c, h, fr[i], 0.01));
		printf ("R28 (stage 2): 120 Hz %.1f -> %.1f dB, 1 kHz %.1f -> %.1f dB\n", g0[0], g1[0], g0[1], g1[1]);
		CHECK (fabs (g1[0] - g0[0]) > 1.0 || fabs (g1[1] - g0[1]) > 1.0, "R28 changes the response (ElectroSmash: midrange hump and more gain)");
	}
	// 3. LFO: period and swing for three Speed positions
	{
		for (int s = 0; s <= 10; s += 5)
		{
			double stl[8] = {0, 0, 0, 0, 0, 0, 0, 0};
			Setup (c, h, stl, (float) s, 5, 0, 1, false);
			const float p[4] = {(float) s, 5, 0, 1};
			double vmin = 1e9, vmax = -1e9; int flips = 0; double tl = 0, per = 0; int np = 0; double last = stl[1];
			const double T = s == 0 ? 80.0 : 20.0;
			const int steps = (int) (T * FS / 64);
			for (int i = 0; i < steps; i++)
			{
				Phase90ModForTest (c, h, p, stl, 64, 1.0 / FS);
				const double t = (i + 1) * 64 / FS;
				if (t > 5.0) { if (stl[0] < vmin) vmin = stl[0]; if (stl[0] > vmax) vmax = stl[0]; }
				if (stl[1] != last) { last = stl[1]; if (stl[1] > 0.5) { if (tl > 0) { per += t - tl; np++; } tl = t; } flips++; }
			}
			printf ("Speed %d: triangle node from %.2f to %.2f V, period %.3f s (%.2f Hz)\n", s, vmin, vmax, np ? per / np : 0.0, np ? np / per : 0.0);
			CHECK (np >= 1 && vmax > vmin + 1.0, "LFO oscillates at Speed %d", s);
		}
	}
	// 4. notch excursion following the LFO at Speed 5, for the three JFET pinch-off voltages (0 = -1.3 V, 1 = -2.4 V, 2 = -3.5 V)
	for (int vpi = 0; vpi < 3; vpi++)
	{
		printf ("pinch-off voltage %d:\n", vpi);
		const float p[4] = {5, 5, 0, (float) vpi};
		double stl[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		Setup (c, h, stl, 5, 5, 0, (float) vpi, false);
		double fmn1 = 1e9, fmx1 = 0, fmn2 = 1e9, fmx2 = 0;
		for (int k = 0; k < 6; k++)
		{
			for (int i = 0; i < 340; i++) Phase90ModForTest (c, h, p, stl, 64, 1.0 / FS);
			double f1, d1, f2, d2;
			Notch (c, h, 30, 6000, &f1, &d1);
			Notch (c, h, f1 * 2.0, 20000, &f2, &d2);
			printf ("  LFO %.2f V: notches %.0f Hz (%.1f dB) and %.0f Hz (%.1f dB)\n", stl[0], f1, d1, f2, d2);
			if (f1 < fmn1) fmn1 = f1; if (f1 > fmx1) fmx1 = f1; if (f2 < fmn2) fmn2 = f2; if (f2 > fmx2) fmx2 = f2;
		}
		CHECK (fmx1 > 1.05 * fmn1 || vpi > 0, "the first notch moves: %.0f ... %.0f Hz (second %.0f ... %.0f Hz)", fmn1, fmx1, fmn2, fmx2);
		printf ("  first notch %.0f ... %.0f Hz, second %.0f ... %.0f Hz\n", fmn1, fmx1, fmn2, fmx2);
	}
	// 5. stability with all options, strong signal
	for (int fb = 0; fb <= 4; fb++) for (int v = 0; v < 3; v++)
	{
		double stl[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		const float p[4] = {8, 5, (float) fb, (float) v};
		Setup (c, h, stl, 8, 5, (float) fb, (float) v, false);
		double mx = 0; int itmax = 0;
		for (int i = 0; i < 96000; i++)
		{
			if (i % 64 == 0) Phase90ModForTest (c, h, p, stl, 64, 1.0 / FS);
			const double y = c.Run (h[0], 0.8 * sin (2 * M_PI * 300.0 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters ();
		}
		CHECK (mx < 12.0 && itmax < 40 && c.Fails () == 0, "R28 stage %d, cutoff %d: peak %.2f V, iterations %d, failures %d", fb, v, mx, itmax, c.Fails ());
	}
	{
		double stl[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		const float p[4] = {5, 5, 0, 1};
		Setup (c, h, stl, 5, 5, 0, 1, false);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) { if (i % 64 == 0) Phase90ModForTest (c, h, p, stl, 64, 1.0 / FS); acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 1000.0 * i / FS)); }
		printf ("time: %.3f us per sample (one channel, with the JFET update every 64 samples) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
