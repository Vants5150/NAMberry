// test_bender.cpp - Tone Bender Real: bias against the voltages measured on original units, gain, controls, clipping, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_bender.cpp fx_bender.cpp -o test_bender
#include <cstdio>
#include <cmath>
#include <chrono>
#include <initializer_list>
#include "fxckt.h"

void BenderBuildForTest (Ck::TCkt &c, int *h, const float *p);
void BenderPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float att, float lev, float ver, float rc, float src)
{
	c = Ck::TCkt ();
	const float p[5] = {att, lev, ver, rc, src};
	BenderBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	BenderPotsForTest (c, h, p);
	c.Start ();
}

static double Gain (Ck::TCkt &c, const int *h, double f, double amp, int node)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
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
	// h: 10 c1, 11 c2, 12 c3, 13 b1, 14 b2, 15 e3
	const double ref[2][7] = {{-8.51, -0.06, -0.13, -0.068, -8.72, -0.13, -0.07}, {-8.50, -0.038, -0.13, -0.076, -8.29, -0.13, -0.08}};
	for (int v = 0; v < 2; v++)
	{
		Setup (c, h, 5, 5, (float) v, 8.2f, 0);
		const double m[7] = {c.Probe (h[10]), c.Probe (h[13]), c.Probe (h[11]), c.Probe (h[14]), c.Probe (h[12]), c.Probe (h[11]), c.Probe (h[15])};
		printf ("version %d: Q1 c %.2f (%.2f) b %.3f (%.3f); Q2 c %.2f (%.2f) b %.3f (%.3f); Q3 c %.2f (%.2f) b %.2f (%.2f) e %.3f (%.3f)   [measured on originals in brackets]\n", v,
			m[0], ref[v][0], m[1], ref[v][1], m[2], ref[v][2], m[3], ref[v][3], m[4], ref[v][4], m[5], ref[v][5], m[6], ref[v][6]);
		CHECK (fabs (m[0] - ref[v][0]) < 1.0 && fabs (m[2] - ref[v][2]) < 1.0 && fabs (m[4] - ref[v][4]) < 1.5, "version %d: collectors close to those of the originals", v);
	}
	Setup (c, h, 10, 10, 0, 8.2f, 0);
	printf ("response (Attack 10, Level 10, input 0.3 mV):");
	double g1k = 0, g100 = 0, g8k = 0;
	for (double f : {100.0, 300.0, 1000.0, 3000.0, 8000.0}) { const double g = Gain (c, h, f, 0.0003, h[1]); printf (" %g Hz %.1f dB;", f, g); if (f == 1000) g1k = g; if (f == 100) g100 = g; if (f == 8000) g8k = g; }
	printf ("\n");
	CHECK (g1k > 20, "gain at 1 kHz with Attack 10: %.1f dB", g1k);
	double ga[3];
	const float av[3] = {0, 5, 10};
	for (int i = 0; i < 3; i++) { Setup (c, h, av[i], 10, 0, 8.2f, 0); ga[i] = Gain (c, h, 1000.0, 0.0003, h[1]); }
	CHECK (ga[2] > ga[0] + 3.0, "Attack 0/5/10 -> %.1f/%.1f/%.1f dB", ga[0], ga[1], ga[2]);
	double gl[3];
	for (int i = 0; i < 3; i++) { Setup (c, h, 10, av[i], 0, 8.2f, 0); gl[i] = Gain (c, h, 1000.0, 0.0003, h[1]); }
	CHECK (gl[2] > gl[1] + 3.0 && gl[1] > gl[0] + 3.0, "Level 0/5/10 -> %.1f/%.1f/%.1f dB", gl[0], gl[1], gl[2]);
	Setup (c, h, 10, 10, 0, 8.2f, 0);
	double mx = -1e9, mn = 1e9;
	for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.1 * sin (2 * M_PI * 220.0 * i / FS)); if (i > 24000) { if (y > mx) mx = y; if (y < mn) mn = y; } }
	printf ("input 100 mV at 220 Hz, Attack 10, Level 10: output from %.2f to %.2f V\n", mn, mx);
	CHECK (mx - mn > 0.1 && mx - mn < 10.0, "output limited (%.2f ... %.2f V)", mn, mx);
	for (int ver = 0; ver < 2; ver++) for (int src = 0; src < 2; src++) for (int a = 0; a <= 10; a += 5)
	{
		Setup (c, h, (float) a, 8, (float) ver, 8.2f, (float) src);
		double pk = 0; int itmax = 0, f0 = c.Fails();
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 110.0 * i / FS) * sin (2 * M_PI * 1.3 * i / FS)); if (fabs (y) > pk) pk = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (pk < 12.0 && c.Fails () == f0, "version %d source %d Attack %d: peak %.2f V, iterations %d, failures %d", ver, src, a, pk, itmax, c.Fails () - f0);
	}
	{
		Setup (c, h, 10, 8, 0, 8.2f, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}