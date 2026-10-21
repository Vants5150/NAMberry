// test_factory.cpp - Fuzz Factory Real: bias, gain, effect of the controls, distortion, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_factory.cpp fx_factory.cpp -o test_factory
#include <cstdio>
#include <cmath>
#include <chrono>
#include "fxckt.h"

void FactoryBuildForTest (Ck::TCkt &c, int *h, const float *p);
void FactoryPotsForTest (Ck::TCkt &c, const int *h, const float *p);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static void Setup (Ck::TCkt &c, int *h, float gate, float comp, float drive, float stab, float vol, float hot, float src)
{
	c = Ck::TCkt ();
	const float p[7] = {gate, comp, drive, stab, vol, hot, src};
	FactoryBuildForTest (c, h, p);
	c.SetStep (1.0 / FS);
	FactoryPotsForTest (c, h, p);
	c.Start ();
}

// output amplitude and fundamental component after "settle" seconds
static void Out (Ck::TCkt &c, const int *h, double f, double amp, double settle, double *pPeak, double *pFund)
{
	const int ns = (int) (settle * FS), cyc = (int) (f * 0.3 + 1), M = (int) (cyc * FS / f + 0.5);
	for (int i = 0; i < ns; i++) c.Run (h[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0, pk = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (ns + i) / FS;
		const double v = c.Run (h[0], amp * sin (th)); sn += v * sin (th); cs += v * cos (th); if (fabs (v) > pk) pk = fabs (v);
	}
	*pFund = 2.0 * sqrt (sn * sn + cs * cs) / M; *pPeak = pk;
}

int main (void)
{
	static Ck::TCkt c;
	int h[CKT_H];
	Setup (c, h, 0, 5, 5, 10, 5, 0, 0);
	printf ("operating points (default: Gate 0, Comp 5, Drive 5, Stab 10): Vs %.2f, Q1 collector %.2f, Q2 base %.2f, Q2 collector %.2f, node D %.2f, Q3 collector %.2f V\n",
		c.Probe (h[10]), c.Probe (h[11]), c.Probe (h[12]), c.Probe (h[13]), c.Probe (h[14]), c.Probe (h[15]));
	CHECK (c.Probe (h[10]) > 8.5 && c.Probe (h[11]) > 0.5 && c.Probe (h[11]) < 8.5, "supply and Q1 in the active region");
	// gain and distortion
	{
		printf ("output (fundamental / peak) with a 440 Hz sine, Volume 5:\n   input(mV)  fund(mV)  peak(mV)  gain(dB)\n");
		const double amp[] = {0.003, 0.01, 0.03, 0.1, 0.3, 1.0};
		double gF[6];
		for (int i = 0; i < 6; i++)
		{
			double pk, fu; Out (c, h, 440.0, amp[i], 1.0, &pk, &fu); gF[i] = fu / amp[i];
			printf ("   %10.1f %9.1f %9.1f %10.1f\n", amp[i] * 1e3, fu * 1e3, pk * 1e3, Db (gF[i]));
		}
		CHECK (gF[0] > 1.0, "small-signal gain %.1f dB (positive)", Db (gF[0]));
		CHECK (Db (gF[5]) < Db (gF[0]) - 3.0, "compression: the gain falls with level (%.1f -> %.1f dB)", Db (gF[0]), Db (gF[5]));
	}
	// effect of the controls on the gain at 30 mV (each control from 0 to 10 with the others at default)
	{
		const char *nm[5] = {"Gate", "Comp", "Drive", "Stab", "Volume"};
		for (int k = 0; k < 5; k++)
		{
			double g[2];
			for (int v = 0; v < 2; v++)
			{
				float p[5] = {0, 5, 5, 10, 5}; p[k] = v ? 10.0f : 0.0f;
				Setup (c, h, p[0], p[1], p[2], p[3], p[4], 0, 0);
				double pk, fu; Out (c, h, 440.0, 0.03, 1.0, &pk, &fu); g[v] = Db (fu / 0.03);
			}
			printf ("%s 0 -> 10: gain at 30 mV %.1f -> %.1f dB\n", nm[k], g[0], g[1]);
		}
	}
	// stability: all extreme combinations, 0.5 V signal, check of the iterations and failures
	for (int mask = 0; mask < 32; mask++)
	{
		const float gate = (mask & 1) ? 10.0f : 0.0f, comp = (mask & 2) ? 10.0f : 0.0f, drive = (mask & 4) ? 10.0f : 0.0f, stab = (mask & 8) ? 10.0f : 0.0f, vol = (mask & 16) ? 10.0f : 3.0f;
		Setup (c, h, gate, comp, drive, stab, vol, 0, 0);
		double mx = 0; int itmax = 0;
		for (int i = 0; i < 24000; i++) { const double y = c.Run (h[0], 0.5 * sin (2 * M_PI * 220.0 * i / FS)); if (fabs (y) > mx) mx = fabs (y); if (c.Iters () > itmax) itmax = c.Iters (); }
		// with Gate at maximum and Stab at minimum the circuit is almost a relaxation oscillator (it is the "gate" that spits): Newton skips a few samples and the protection holds the last output; the only
		// requirement is that the output stays bounded and that the skipped samples are fewer than 2%
		const bool ok = mx < 12.0 && c.Fails () < 480;
		if (!ok || mask % 8 == 0) CHECK (ok, "G%g C%g D%g S%g V%g: peak %.2f V, iterations at most %d, samples skipped %d of 24000", gate, comp, drive, stab, vol, mx, itmax, c.Fails ());
	}
	for (int hot = 0; hot < 2; hot++) for (int src = 0; src < 2; src++)
	{
		Setup (c, h, 3, 5, 5, 10, 5, (float) hot, (float) src);
		double pk, fu; Out (c, h, 440.0, 0.1, 1.0, &pk, &fu);
		CHECK (pk < 12.0 && fu > 0.0, "transistor %d source %d: peak %.2f V", hot, src, pk);
	}
	{
		Setup (c, h, 0, 5, 5, 10, 5, 0, 0);
		const auto t0 = std::chrono::steady_clock::now ();
		const int N = 480000; double acc = 0;
		for (int i = 0; i < N; i++) acc += c.Run (h[0], 0.1 * sin (2 * M_PI * 440.0 * i / FS));
		printf ("time: %.3f us per sample (one channel) %g\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N, acc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
