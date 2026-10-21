// test_klon.cpp - Klon Real: bias, gain stage (40 dB at 1 kHz), Treble shelving (+18 / -8 dB), germanium clipping, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_klon.cpp fx_klon.cpp -o test_klon
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void KlonBuildForTest (Ck::TCkt &c, int *h, const float *p);
void KlonPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float g, float t, float o, float d)
{
	c = Ck::TCkt ();
	const float p[4] = {g, t, o, d};
	KlonBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	KlonPotsForTest (c, h, p);
	c.Start ();
}

// h: 0 in, 1 out, 11 o2 (gain stage), 12 d (diodes), 13 o3 (summer), 14 o4 (tone), 15 vb
static void Gains (Ck::TCkt &c, const int *h, double f, double amp, double *g)
{
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.8);
	for (int i = 0; i < settle; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn[5] = {0}, cs[5] = {0};
	const int nodes[5] = {h[11], h[12], h[13], h[14], h[1]};
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
	Setup (c, h, 10, 5, 5, 0);
	printf ("bias: virtual 4.5 V %.2f, gain stage output %.2f, summer %.2f, tone %.2f V\n", c.Probe (h[15]), c.Probe (h[11]), c.Probe (h[13]), c.Probe (h[14]));
	CHECK (fabs (c.Probe (h[15]) - 4.5) < 0.5 && fabs (c.Probe (h[11]) - c.Probe (h[15])) < 0.1 && fabs (c.Probe (h[13]) - c.Probe (h[15])) < 0.1 && fabs (c.Probe (h[14]) - c.Probe (h[15])) < 0.1, "op-amps biased at the virtual node (%.2f V; 4.5 minus the 17 uA current of the output dry path)", c.Probe (h[15]));
	printf ("small signal (Gain 10, Treble 5, input 50 uV), dB relative to the input:\n   Hz    gain stage  summer   tone   output\n");
	double g[5], g1k = 0;
	const double fr[] = {50, 100, 200, 400, 700, 1000, 2000, 4000, 8000};
	for (double f : fr)
	{
		Gains (c, h, f, 0.00005, g);
		printf ("  %6.0f %9.1f %10.1f %8.1f %8.1f\n", f, g[0], g[2], g[3], g[4]);
		if (f == 1000) g1k = g[0];
	}
	CHECK (g1k > 30 && g1k < 50, "gain stage at 1 kHz with Gain 10: %.1f dB (ElectroSmash: about 40 dB at maximum)", g1k);
	// Gain
	double gg[3];
	const float gv[3] = {0, 5, 10};
	for (int i = 0; i < 3; i++) { Setup (c, h, gv[i], 5, 5, 0); Gains (c, h, 1000.0, 0.00005, g); gg[i] = g[0]; }
	CHECK (gg[2] > gg[1] + 3.0 && gg[1] > gg[0] + 3.0, "Gain 0/5/10 -> %.1f/%.1f/%.1f dB al gain stage", gg[0], gg[1], gg[2]);
	// Treble: shelving above 400 Hz: +18 dB at maximum and -8 dB at minimum relative to the center, ratio between tone and summer
	double sh[3], hf[3];
	const float tv[3] = {0, 5, 10};
	for (int i = 0; i < 3; i++) { Setup (c, h, 5, tv[i], 5, 0); Gains (c, h, 6000.0, 0.0001, g); hf[i] = g[3] - g[2]; Gains (c, h, 100.0, 0.0001, g); sh[i] = g[3] - g[2]; }
	printf ("tone (relative to the summer): Treble 0/5/10 at 100 Hz %.1f/%.1f/%.1f dB, at 6 kHz %.1f/%.1f/%.1f dB (ElectroSmash: -8 / 0 / +18 dB above 408 Hz)\n", sh[0], sh[1], sh[2], hf[0], hf[1], hf[2]);
	CHECK (hf[2] > hf[1] + 8.0 && hf[1] > hf[0] + 4.0 && fabs (sh[2] - sh[0]) < 2.0, "shelving: treble from %.1f to %.1f dB, bass unchanged (%.1f / %.1f)", hf[0], hf[2], sh[0], sh[2]);
	// germanium clipping: after R13 the peak is about 0.35 V
	Setup (c, h, 10, 5, 5, 0);
	double pk = 0;
	for (int i = 0; i < 48000; i++) { c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS)); if (i > 24000) { const double v = fabs (c.Probe (h[12])); if (v > pk) pk = v; } }
	printf ("input 100 mV, Gain 10: peak at the diodes %.2f V (1N34A: Vf 0.35 V)\n", pk);
	CHECK (pk > 0.2 && pk < 0.55, "germanium clipping at %.2f V", pk);
	for (int gain = 0; gain <= 10; gain += 5) for (int d = 0; d < 3; d++)
	{
		Setup (c, h, (float) gain, 5, 5, (float) d);
		double mx = 0; int itmax = 0, f0 = c.Fails();
		for (int i = 0; i < 48000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 130.0 * i / FS) * sin (2 * M_PI * 1.1 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		CHECK (mx < 12.0 && c.Fails () == f0, "Gain %d diodes %d: peak %.2f V, iterations %d, failures %d", gain, d, mx, itmax, c.Fails () - f0);
	}
	{
		Setup (c, h, 10, 5, 5, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}