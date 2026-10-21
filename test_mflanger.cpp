// test_mflanger.cpp - Micro Flanger Real: pre-emphasis and dry path, diode limiter, LFO and clock from the circuit, comb at the expected delay, feedback, stability.
#include <cstdio>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <vector>
#include "fxckt.h"

void MflangerBuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &c, int *hc, Ck::TCkt &d, int *hd);
void MflangerForceForTest (CFx *pFx, double fclk);
void MflangerClockForTest (double x, double seconds, double *fmin, double *fmax, int *nSw, double *dcM);

static int fails = 0;
#define CHECK(c, ...) do { char b_[300]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// response of A (input -> U1A output) and of U1A + mixer (dry): amplitude in dB at f
static double Gain (double f, int what, double amp)
{
	static Ck::TCkt A, C, D; int hA[6], hC[6], hD[6];
	A.Clear (); C.Clear (); D.Clear ();
	MflangerBuildForTest (A, hA, C, hC, D, hD);
	A.SetStep (1.0 / FS); D.SetStep (1.0 / FS);
	A.SetR (hA[4], 500.0e3);
	A.Start (); D.Start ();
	for (int i = 0; i < 4; i++) A.Run (hA[0], 0.0);
	const double dc = A.Probe (hA[2]);
	const int cyc = (int) (f * 0.3 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
	double sn = 0, cs = 0;
	for (int i = 0; i < settle + M; i++)
	{
		const double th = 2 * M_PI * f * i / FS;
		const double bi = A.Run (hA[0], amp * sin (th));
		const double o = A.Probe (hA[2]) - dc;
		double v = o;
		if (what == 1) v = D.Run (hD[0], o);
		if (what == 2) v = bi;
		if (i >= settle) { sn += v * sin (th); cs += v * cos (th); }
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
}

static double Rms (const std::vector<float> &v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += (double) v[i] * v[i]; return sqrt (s / (b - a)); }

// sine f, amplitude amp, seconds; parameters rate/regen/pol; clock forced if fclk > 0; returns the rms of the last third; mx = peak
static double Run (double f, double amp, double sec, float rate, float regen, float pol, double fclk, double *mx = nullptr, bool noise = false)
{
	CFx *fx = NewMicroFlanger ((float) FS);
	if (fclk > 0) MflangerForceForTest (fx, fclk);
	const float p[3] = {rate, regen, pol}; int mono = 0;
	const int N = (int) (sec * FS);
	std::vector<float> o (N);
	srand (7);
	for (int b = 0; b < N; b += 120)
	{
		std::vector<float> l (120), r (120);
		for (int i = 0; i < 120; i++)
		{
			const int k = b + i;
			l[i] = r[i] = noise ? (float) (amp * ((rand () / (double) RAND_MAX) * 2.0 - 1.0)) : (float) (amp * sin (2 * M_PI * f * k / FS));
		}
		fx->Process (l.data (), r.data (), 120, &mono, p);
		for (int i = 0; i < 120 && b + i < N; i++) o[b + i] = l[i];
	}
	delete fx;
	double pk = 0; for (float v : o) if (fabs (v) > pk) pk = fabs (v);
	if (mx) *mx = pk;
	return Rms (o, N * 2 / 3, N);
}

int main (void)
{
	// input: U1A pre-emphasis and flat dry path
	{
		const double fr[5] = {100, 1000, 3000, 10000, 15000};
		double g[5], gd[5], mn = 1e9, mx = -1e9;
		printf ("pre-emphasis (U1A output / input):");
		for (int i = 0; i < 5; i++) { g[i] = Db (Gain (fr[i], 0, 0.01)); printf (" %.0f Hz %.1f dB;", fr[i], g[i]); }
		printf ("\ndry path (U1A + mixer):");
		for (int i = 0; i < 5; i++) { gd[i] = Db (Gain (fr[i], 1, 0.01)); if (gd[i] < mn) mn = gd[i]; if (gd[i] > mx) mx = gd[i]; printf (" %.0f Hz %.1f dB;", fr[i], gd[i]); }
		printf ("\n");
		CHECK (g[4] > g[0] + 8.0, "U1A's pre-emphasis raises the treble (%.1f dB at 15 kHz)", g[4] - g[0]);
		CHECK (mx - mn < 3.0, "the dry path stays flat within %.1f dB (the mixer's de-emphasis compensates)", mx - mn);
	}
	// three-diode limiter: the BBD input (g[2]) compresses with large signals
	{
		const double a1 = Gain (1000, 2, 0.05), a2 = Gain (1000, 2, 3.0);
		printf ("BBD input: gain %.3f with 0.05 V and %.3f with 3 V (%.1f dB of compression)\n", a1, a2, Db (a2 / a1));
		CHECK (a2 < a1 * 0.95, "the three diodes cut the large peaks (%.1f dB)", Db (a2 / a1));
		CHECK (a2 > a1 * 0.05, "the limiter does not shut the signal off (%.1f dB)", Db (a2 / a1));
	}
	// LFO and clock from the circuit
	{
		const double xs[3] = {0.0, 0.5, 1.0};
		for (int k = 0; k < 3; k++)
		{
			double fmin, fmax, dcm; int sw;
			MflangerClockForTest (xs[k], 400.0, &fmin, &fmax, &sw, &dcm);
			const double per = 400.0 / (sw / 2.0);
			printf ("Rate %.1f: LFO cycle %.1f s, clock %.0f - %.0f kHz, delay %.2f - %.2f ms (ratio %.1f)\n", xs[k] * 10, per, fmin / 1e3, fmax / 1e3, 256.0 / fmax * 1e3, 256.0 / fmin * 1e3, fmax / fmin);
			CHECK (per > 1.0 && per < 15.0, "Rate %.1f: LFO cycle of %.1f s", xs[k] * 10, per);
			CHECK (fmax < 2.0e6 && fmin > 1.5e3, "Rate %.1f: clock %.0f - %.0f kHz within the SAD1024's range (1.5 kHz - over 1.5 MHz)", xs[k] * 10, fmin / 1e3, fmax / 1e3);
			CHECK (fmax / fmin > 4.0 && fmax / fmin < 40.0, "Rate %.1f: the clock travel is %.1f times", xs[k] * 10, fmax / fmin);
		}
		double a, b, c; int s; MflangerClockForTest (0.0, 400.0, &a, &b, &s, &c);
		double a2, b2; MflangerClockForTest (1.0, 400.0, &a2, &b2, &s, &c);
		CHECK (true, "(Rate 0 and 10: delay %.2f-%.2f and %.2f-%.2f ms)", 256.0 / b * 1e3, 256.0 / a * 1e3, 256.0 / b2 * 1e3, 256.0 / a2 * 1e3);
	}
	// comb: clock forced to 400 kHz (0.64 ms delay): in the dry + effect mixer (Normal polarity) zero at 1/(2d) = 781 Hz and peak at 1/d = 1562 Hz
	{
		const double fc = 400.0e3, d = 256.0 / fc;
		const double fn = 0.5 / d, fp = 1.0 / d;
		const double rn = Run (fn, 0.1, 1.5, 4, 0, 0, fc), rp = Run (fp, 0.1, 1.5, 4, 0, 0, fc);
		const double rn2 = Run (fn, 0.1, 1.5, 4, 0, 1, fc), rp2 = Run (fp, 0.1, 1.5, 4, 0, 1, fc);
		printf ("clock %.0f kHz (delay %.2f ms): Normal %.0f Hz %.4f, %.0f Hz %.4f (%.1f dB); Inverted %.0f Hz %.4f, %.0f Hz %.4f\n", fc / 1e3, d * 1e3, fn, rn, fp, rp, Db (rp / rn), fn, rn2, fp, rp2);
		CHECK (Db (rp / rn) > 15.0, "Normal polarity: zero at %.0f Hz and peak at %.0f Hz (%.1f dB)", fn, fp, Db (rp / rn));
		CHECK (Db (rn2 / rp2) > 10.0, "Inverted polarity: the comb is inverted (%.1f dB)", Db (rn2 / rp2));
	}
	// feedback: at maximum Regen the delayed signal changes the response, and it stays stable
	{
		const double fc = 400.0e3;
		const double r0 = Run (1562.0, 0.1, 1.5, 4, 0, 0, fc), r1 = Run (1562.0, 0.1, 1.5, 4, 10, 0, fc);
		const double r2 = Run (1562.0, 0.1, 1.5, 4, 0, 1, fc), r3 = Run (1562.0, 0.1, 1.5, 4, 10, 1, fc);
		printf ("Regen 0 against 10 at 1562 Hz: Normal %.4f -> %.4f (%.1f dB); Inverted %.4f -> %.4f (%.1f dB)\n", r0, r1, Db (r1 / r0), r2, r3, Db (r3 / r2));
		CHECK (fabs (Db (r1 / r0)) > 0.5 || fabs (Db (r3 / r2)) > 0.5, "the feedback changes the response (%.1f and %.1f dB)", Db (r1 / r0), Db (r3 / r2));
		const double fcs[4] = {160.0e3, 400.0e3, 900.0e3, 1.5e6};
		const double fs[4] = {300.0, 1200.0, 2500.0, 5000.0};
		double worst = 0;
		for (int a = 0; a < 4; a++)
			for (int pol = 0; pol < 2; pol++)
			{
				double mx;
				Run (fs[a], 0.2, 1.0, 4, 10, (float) pol, fcs[a], &mx);
				if (mx > worst) worst = mx;
				Run (0, 0.1, 1.0, 4, 10, (float) pol, fcs[3 - a], &mx, true);
				if (mx > worst) worst = mx;
			}
		CHECK (worst < 5.0 && worst == worst, "Regen 10, two polarities, four clocks, sines and noise: peak %.2f (limit 5)", worst);
	}
	// LFO running: 40 s of noise at Rate 10 (shortest cycle), no reset, bounded output and level close to the input's
	{
		double mx; const double r = Run (0, 0.1, 40.0, 10, 5, 0, 0, &mx, true);
		const double rin = 0.1 / sqrt (3.0);
		printf ("40 s of noise, Rate 10: output rms %.4f (input %.4f, %.1f dB), peak %.2f\n", r, rin, Db (r / rin), mx);
		CHECK (mx < 3.0 && r > rin * 0.25 && r < rin * 4.0, "LFO running: bounded output with a sensible level (%.1f dB)", Db (r / rin));
	}
	{
		CFx *fx = NewMicroFlanger ((float) FS);
		const float p[3] = {4.0f, 3.0f, 0.0f}; int mono = 0;
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
