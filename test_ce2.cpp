// test_ce2.cpp - Chorus CE-2 Real: pre-emphasis, filters, flat dry path, LFO, delay, comb, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_ce2.cpp fx_ce2.cpp -o test_ce2
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include "fxckt.h"

void Ce2BuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &b, int *hb);
double Ce2DelayForTest (double vlfo);
double Ce2LfoHzForTest (double rate);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// gain of the circuits for a sine: node 0 = pre-emphasis dry output, 1 = anti-aliasing output, 2 = complete dry path (wet at zero), 3 = like 2 but from the anti-aliasing instead of the dry
static double Gain (double f, int what)
{
	static Ck::TCkt a, b;
	int ha[4], hb[4];
	a = Ck::TCkt (); b = Ck::TCkt ();
	Ce2BuildForTest (a, ha, b, hb);
	a.SetStep (1.0 / FS); b.SetStep (1.0 / FS); a.Start (); b.Start ();
	const double amp = 0.01;
	const int cyc = (int) (f * 0.5 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 1.0);
	double sn = 0, cs = 0;
	for (int i = 0; i < settle + M; i++)
	{
		const double th = 2 * M_PI * f * i / FS;
		const double dry = a.Run (ha[0], amp * sin (th));
		const double aa = a.Probe (ha[2]) - 4.5;
		double v = 0;
		if (what == 0) v = dry - 4.5;
		else if (what == 1) v = aa;
		else { b.SetV (hb[1], 0.0); v = b.Run (hb[0], dry) ; }
		if (i >= settle) { sn += v * sin (th); cs += v * cos (th); }
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
}

int main (void)
{
	// 1. pre-emphasis: 0 dB below 410 Hz, 15 dB above 2.34 kHz (ElectroSmash)
	{
		const double g100 = Db (Gain (100.0, 0)), g1k = Db (Gain (1000.0, 0)), g5k = Db (Gain (5000.0, 0)), g10k = Db (Gain (10000.0, 0));
		printf ("pre-emphasis: 100 Hz %.1f dB, 1 kHz %.1f dB, 5 kHz %.1f dB, 10 kHz %.1f dB (ElectroSmash: 0 dB below 410 Hz, 15 dB above 2.3 kHz)\n", g100, g1k, g5k, g10k);
		CHECK (fabs (g100) < 1.0 && fabs (g5k - 15.0) < 1.5, "pre-emphasis as in ElectroSmash");
	}
	// 2. anti-aliasing: 48 Hz high-pass and low-pass with poles at 6.6 and 6.9 kHz
	{
		double g[7]; const double fr[7] = {20, 48, 200, 1000, 6600, 10000, 20000};
		for (int i = 0; i < 7; i++) g[i] = Db (Gain (fr[i], 1));
		printf ("anti-aliasing from the input (includes the pre-emphasis): 20 Hz %.1f, 48 Hz %.1f, 200 Hz %.1f, 1 kHz %.1f, 6.6 kHz %.1f, 10 kHz %.1f, 20 kHz %.1f dB\n", g[0], g[1], g[2], g[3], g[4], g[5], g[6]);
		CHECK (g[0] < g[2] - 6.0 && g[6] < g[3] - 10.0, "passband: it cuts below 48 Hz and above 6-7 kHz");
	}
	// 3. flat dry path: pre-emphasis and de-emphasis compensate each other
	{
		const double fr[6] = {80, 200, 800, 2000, 5000, 10000};
		double g[6], mn = 1e9, mx = -1e9;
		for (int i = 0; i < 6; i++) { g[i] = Db (Gain (fr[i], 2)); if (g[i] < mn) mn = g[i]; if (g[i] > mx) mx = g[i]; }
		printf ("dry path (wet at zero): 80 Hz %.1f, 200 %.1f, 800 %.1f, 2 kHz %.1f, 5 kHz %.1f, 10 kHz %.1f dB\n", g[0], g[1], g[2], g[3], g[4], g[5]);
		CHECK (mx - mn < 1.5, "the dry path stays flat within %.1f dB", mx - mn);
	}
	// 4. LFO and delay
	{
		const double fMax = Ce2LfoHzForTest (10.0), fMin = Ce2LfoHzForTest (0.0);
		printf ("LFO: Rate 10 -> %.2f Hz (ElectroSmash 3,5), Rate 0 -> %.2f Hz\n", fMax, fMin);
		CHECK (fabs (fMax - 3.5) < 0.15 && fMin < fMax / 8.0, "LFO frequency as in the ElectroSmash formula");
		const double dC = Ce2DelayForTest (4.5) * 1e3, dLo = Ce2DelayForTest (4.5 + 2.1) * 1e3, dHi = Ce2DelayForTest (4.5 - 2.1) * 1e3;
		printf ("delay (with the assumptions about the MN3101): center %.2f ms, from %.2f to %.2f ms with Depth 10\n", dC, dLo, dHi);
		CHECK (dC > 1.0 && dHi < 12.0 && dLo < dC && dHi > dC, "plausible and monotonic delay");
	}
	// 5. complete effect: comb with fixed delay (Depth 0) and stability
	{
		CFx *fx = NewCe2 ((float) FS);
		std::vector<float> L (48000), R (48000);
		const float p[2] = {5.0f, 0.0f};
		auto gain = [&](double f) {
			double sn = 0, cs = 0; int mono = 1; const int N = 48000;
			std::vector<float> l (N), r (N);
			for (int i = 0; i < N; i++) { l[i] = r[i] = (float) (0.01 * sin (2 * M_PI * f * i / FS)); }
			fx->Process (l.data (), r.data (), N, &mono, p);
			const int s0 = 24000, M = (int) (round ((N - s0) * f / FS) * FS / f);
			for (int i = 0; i < M; i++) { const double th = 2 * M_PI * f * (s0 + i) / FS; sn += l[s0 + i] * sin (th); cs += l[s0 + i] * cos (th); }
			return 2.0 * sqrt (sn * sn + cs * cs) / M / 0.01;
		};
		const double dMs = Ce2DelayForTest (4.5) * 1e3;
		const double fNotch = 500.0 / dMs, fPeak = 1000.0 / dMs;		// with delay d: 1 / (2 d) cancels, 1 / d reinforces
		const double gn = Db (gain (fNotch)), gp = Db (gain (fPeak));
		printf ("comb with delay %.2f ms: %.0f Hz %.1f dB (notch expected), %.0f Hz %.1f dB (peak expected)\n", dMs, fNotch, gn, fPeak, gp);
		CHECK (gp > gn + 6.0, "the comb exists: crest %.1f dB, notch %.1f dB", gp, gn);
		delete fx;
		for (int rate = 0; rate <= 10; rate += 5) for (int depth = 0; depth <= 10; depth += 5)
		{
			fx = NewCe2 ((float) FS);
			const float q[2] = {(float) rate, (float) depth}; double mx = 0; int mono = 0;
			for (int blk = 0; blk < 300; blk++)
			{
				std::vector<float> l (160), r (160);
				for (int i = 0; i < 160; i++) { const double t = (blk * 160 + i) / FS; l[i] = (float) (0.5 * sin (2 * M_PI * 330.0 * t)); r[i] = (float) (0.5 * sin (2 * M_PI * 110.0 * t)); }
				fx->Process (l.data (), r.data (), 160, &mono, q);
				for (int i = 0; i < 160; i++) { if (fabs (l[i]) > mx) mx = fabs (l[i]); if (fabs (r[i]) > mx) mx = fabs (r[i]); }
			}
			CHECK (mx < 4.0 && mx > 0.01, "Rate %d Depth %d: peak %.2f", rate, depth, mx);
			delete fx;
		}
	}
	{
		CFx *fx = NewCe2 ((float) FS);
		const float p[2] = {5.0f, 5.0f}; int mono = 0;
		std::vector<float> l (4800), r (4800);
		for (int i = 0; i < 4800; i++) { l[i] = r[i] = (float) (0.1 * sin (2 * M_PI * 440.0 * i / FS)); }
		std::vector<float> src (l);
		const auto t0 = std::chrono::steady_clock::now ();
		const int NB = 400;
		for (int b = 0; b < NB; b++)
		{
			l = src; r = src;		// the previous block's output must not come back in: the mix gain (dry + wet) would amplify it at every round
			fx->Process (l.data (), r.data (), 4800, &mono, p);
		}
		const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / (NB * 4800.0);
		printf ("time: %.3f us per sample (two channels)\n", us);
		delete fx;
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
