// test_ad9.cpp - Analog Delay AD9 Real: pre-emphasis, dry path, echo times against the service manual, compander, repeats, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_ad9.cpp fx_ad9.cpp -o test_ad9
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include "fxckt.h"

void Ad9BuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &b, int *hb, Ck::TCkt &c, int *hc, Ck::TCkt &d, int *hd, int ver, double r43);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// gain of the dry path: input -> A (pre-emphasis) -> D (mixer with de-emphasis), wet at zero, P3 at minimum
static double DryGain (double f, int ver, int what)
{
	static Ck::TCkt A, B, C, D; int hA[6], hB[6], hC[6], hD[6];
	A.Clear (); B.Clear (); C.Clear (); D.Clear ();
	Ad9BuildForTest (A, hA, B, hB, C, hC, D, hD, ver, 10.0e3);
	A.SetStep (1.0 / FS); D.SetStep (1.0 / FS);
	A.SetR (hA[3], 100.0e3); A.SetR (hA[4], 1.0);			// repeat at minimum
	D.SetR (hD[2], 100.0e3); D.SetR (hD[3], 1.0);			// delay level at minimum
	A.Start (); D.Start ();
	const double amp = 0.01;
	const int cyc = (int) (f * 0.4 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
	double sn = 0, cs = 0;
	for (int i = 0; i < settle + M; i++)
	{
		const double th = 2 * M_PI * f * i / FS;
		A.Run (hA[0], amp * sin (th));
		const double vo = A.Probe (hA[1]);
		const double y = D.Run (hD[0], vo);
		const double v = what == 0 ? vo : y;
		if (i >= settle) { sn += v * sin (th); cs += v * cos (th); }
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
}

// complete effect: 1 kHz burst, recorded output
static std::vector<float> Burst (const float *p, double amp, double durMs, double totSec, double freq = 1000.0)
{
	CFx *fx = NewAd9 ((float) FS);
	const int N = (int) (totSec * FS), nb = (int) (durMs * 1e-3 * FS);
	std::vector<float> out (N);
	int mono = 0;
	for (int b = 0; b < N; b += 120)
	{
		std::vector<float> l (120), r (120);
		for (int i = 0; i < 120; i++) { const int k = b + i; l[i] = r[i] = (k < nb) ? (float) (amp * sin (2 * M_PI * freq * k / FS)) : 0.0f; }
		fx->Process (l.data (), r.data (), 120, &mono, p);
		for (int i = 0; i < 120 && b + i < N; i++) out[b + i] = l[i];
	}
	delete fx;
	return out;
}

static double Rms (const std::vector<float> &v, int a, int b) { double s = 0; for (int i = a; i < b && i < (int) v.size (); i++) s += v[i] * v[i]; return sqrt (s / (b - a)); }

int main (void)
{
	for (int ver = 0; ver < 2; ver++)
	{
		const double g1 = DryGain (1000.0, ver, 0);
		printf ("version %d, pre-emphasis (U1.A output relative to the input):", ver);
		const double fr[5] = {100, 400, 1000, 3000, 10000};
		double g[5];
		for (int i = 0; i < 5; i++) { g[i] = Db (DryGain (fr[i], ver, 0)); printf (" %.0f Hz %.1f dB;", fr[i], g[i]); }
		printf ("\n");
		CHECK (g[4] > g[0] + 8.0, "version %d: the pre-emphasis raises the treble", ver);
		double mn = 1e9, mx = -1e9, gd[5];
		printf ("version %d, complete dry path (pre-emphasis + de-emphasis):", ver);
		for (int i = 0; i < 5; i++) { gd[i] = Db (DryGain (fr[i], ver, 1)); if (gd[i] < mn) mn = gd[i]; if (gd[i] > mx) mx = gd[i]; printf (" %.0f Hz %.1f dB;", fr[i], gd[i]); }
		printf ("\n");
		CHECK (mx - mn < 3.0, "version %d: the dry path stays flat within %.1f dB (the mixer's de-emphasis compensates)", ver, mx - mn);
		(void) g1;
	}
	// echo times: Delay Level 10, Repeat 0; Delay Time 0, 5, 10
	{
		const double expectMs[3] = {24.0, 85.0, 300.0};
		for (int k = 0; k < 3; k++)
		{
			const float p[5] = {k * 5.0f, 0.0f, 10.0f, 0.0f, 10.0f};
			const std::vector<float> o = Burst (p, 0.15, 6.0, 0.6);
			// echo search: 2 ms windows after the end of the dry burst (6 ms + tail), RMS peak
			double best = 0; int bi = 0;
			for (int a = (int) (0.012 * FS); a < (int) (0.58 * FS); a += 96) { const double r = Rms (o, a, a + 96); if (r > best) { best = r; bi = a; } }
			const double ms = bi / FS * 1000.0 + 1.0;
			printf ("Delay Time %d: echo at %.0f ms (expected about %.0f ms), RMS amplitude %.4f\n", k * 5, ms, expectMs[k], best);
			CHECK (ms > expectMs[k] * 0.7 && ms < expectMs[k] * 1.5 + 8.0, "Delay Time %d: the echo arrives at %.0f ms", k * 5, ms);
		}
	}
	// compander: transparency (echo level proportional to the input level)
	{
		double e[2];
		const double am[2] = {0.02, 0.2};
		for (int k = 0; k < 2; k++)
		{
			const float p[5] = {0.0f, 0.0f, 10.0f, 0.0f, 10.0f};
			const std::vector<float> o = Burst (p, am[k], 120.0, 0.6);
			double best = 0; for (int a = (int) (0.14 * FS); a < (int) (0.3 * FS); a += 96) { const double r = Rms (o, a, a + 96); if (r > best) best = r; }
			e[k] = best;
		}
		const double ratio = Db (e[1] / e[0]);
		printf ("compander: echo for 0.02 and 0.2 V -> %.4f and %.4f (ratio %.1f dB, expected 20 dB)\n", e[0], e[1], ratio);
		CHECK (ratio > 14.0 && ratio < 26.0, "the compander is transparent: the echo follows the level (%.1f dB for 20 dB)", ratio);
	}
	// repeats at Repeat 10 with three values of the trimmer: decay of the successive repeats, stability
	for (int tr = 0; tr < 3; tr++)
	{
		const double trims[3] = {5.0, 10.0, 20.0};
		const float p[5] = {0.0f, 10.0f, 5.0f, 0.0f, (float) trims[tr]};
		const std::vector<float> o = Burst (p, 0.15, 6.0, 1.0);
		double pk[6] = {0}; const int per = (int) (0.024 * FS);
		for (int n = 0; n < 6; n++) { double b = 0; for (int a = per * (n + 1) - 300; a < per * (n + 1) + 300; a += 24) { if (a < 0 || a + 24 > (int) o.size ()) continue; const double r = Rms (o, a, a + 24); if (r > b) b = r; } pk[n] = b; }
		double mx = 0; for (float v : o) if (fabs (v) > mx) mx = fabs (v);
		printf ("Repeat 10, VR105 %.0fk: repeats %.3f %.3f %.3f %.3f %.3f %.3f, peak %.2f\n", trims[tr], pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], mx);
		CHECK (mx < 20.0, "VR105 %.0fk: output limited (%.2f)", trims[tr], mx);
	}
	// Repeat 5: no sustained tone (tail in the last quarter second far below the first repeat); Repeat 8: at least 3 audible repeats (within 40 dB of the first)
	for (int ver = 0; ver < 2; ver++)
	{
		const float p5[5] = {0.0f, 5.0f, 5.0f, (float) ver, 10.0f}, p8[5] = {0.0f, 8.0f, 5.0f, (float) ver, 10.0f};
		const std::vector<float> o5 = Burst (p5, 0.1, 6.0, 1.0), o8 = Burst (p8, 0.1, 6.0, 1.0);
		const int per = (int) (0.0251 * FS);
		double e1 = 0, n = 0; for (int a = per - 300; a < per + 300; a += 24) { const double r = Rms (o5, a, a + 24); if (r > e1) e1 = r; }
		const double tail = Rms (o5, (int) (0.75 * FS), (int) FS);
		int count = 0; double f8 = 0;
		for (int k = 1; k <= 12; k++) { double b = 0; for (int a = per * k - 300; a < per * k + 300; a += 24) { const double r = Rms (o8, a, a + 24); if (r > b) b = r; } if (k == 1) f8 = b; if (b > f8 * 0.01) count++; }
		(void) n;
		printf ("version %d: Repeat 5 first repeat %.4f, tail %.6f; Repeat 8: %d repeats within 40 dB\n", ver, e1, tail, count);
		CHECK (tail < e1 * 0.01, "version %d: Repeat 5 dies out, no sustained tone", ver);
		CHECK (count >= (ver == 0 ? 3 : 2), "version %d: Repeat 8 gives enough repeats (%d; version B has R6 10k instead of 47k and a weaker loop)", ver, count);
	}
	{
		CFx *fx = NewAd9 ((float) FS);
		const float p[5] = {5.0f, 3.0f, 5.0f, 0.0f, 10.0f}; int mono = 0;
		std::vector<float> src (4800), l (4800), r (4800);
		for (int i = 0; i < 4800; i++) src[i] = (float) (0.1 * sin (2 * M_PI * 440.0 * i / FS));
		const auto t0 = std::chrono::steady_clock::now ();
		for (int b = 0; b < 300; b++) { l = src; r = src; fx->Process (l.data (), r.data (), 4800, &mono, p); }
		printf ("time: %.3f us per sample (frame)\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / (300 * 4800.0));
		delete fx;
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
