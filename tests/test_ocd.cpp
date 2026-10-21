// test_ocd.cpp - OCD Real: small-signal response against the analytic circuit (both op-amp stages, clipping network off), clipping thresholds, controls, switch, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_ocd.cpp fx_ocd.cpp -o test_ocd
#include <cstdio>
#include <cmath>
#include <complex>
#include <chrono>
#include <vector>
#include "fxckt.h"
#include "fx.h"

double OcdLastXForTest (CFx *pFx);

typedef std::complex<double> Cx;
static int fails = 0;
#define CHECK(c, ...) do { char b_[300]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// the effect with its controls: drive %, tone %, level dB (0), switch, volume
struct TOcd
{
	CFx *fx;
	float p[5];
	TOcd (float drive, float tone, float sw, float vol) { p[0] = drive; p[1] = tone; p[2] = 0; p[3] = sw; p[4] = vol; fx = FxCreate (FxOcd, (float) FS); }
	~TOcd () { delete fx; }
};

// gain (amplitude) at the output for a sine of amplitude amp (V) at frequency f; also the extreme values of the clipping node X (relative to Vb) over the measured part
static double Gain (TOcd &o, double f, double amp, double *ph = 0, double *xmin = 0, double *xmax = 0)
{
	const int cyc = (int) (f * 0.5 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.6);
	const double vfs = 1.305;
	double sn = 0, cs = 0, lo = 1e9, hi = -1e9;
	for (int i = 0; i < settle + M; i++)
	{
		const double th = 2 * M_PI * f * i / FS;
		float l[1] = {(float) (amp * sin (th) / vfs)}, r[1] = {l[0]};
		int m = 1;
		o.fx->Process (l, r, 1, &m, o.p);
		if (i >= settle)
		{
			const double y = (double) l[0] * vfs;
			sn += y * sin (th); cs += y * cos (th);
			const double x = OcdLastXForTest (o.fx);
			if (x < lo) lo = x; if (x > hi) hi = x;
		}
	}
	if (xmin) *xmin = lo;
	if (xmax) *xmax = hi;
	if (ph) *ph = atan2 (cs, sn);
	return 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
}
// analytic small-signal response of the whole chain (clipping network off), with algebraic op-amps of gain 2e4
static Cx Hchain (double f, double drive, double tone, int sw, double vol)
{
	const Cx s (0.0, 2 * M_PI * f);
	const double R1 = 10e3, R2 = 1e6, R3 = 470e3, R4 = 18e3, R5 = drive * 500e3, R6 = 2.2e3, R7 = 10e3, R8 = 220e3, R9 = 10e3, R10 = 150e3, R11 = 39e3, R12 = 33e3, R13 = 22e3, R14 = tone * 10e3, R15 = 500e3;
	const double C1 = 22e-9, C2 = 220e-12, C3 = 100e-9, C4 = 10e-9, C5 = 220e-12, C6 = 100e-9, C7 = 10e-6, C8 = 47e-9;
	const double A0 = 2e4, wp = 2 * M_PI * 3e6 / A0;
	const Cx A = A0 / (1.0 + s / wp);
	// input: source ideal, R2 across the source (no effect); R1 + C1 into R3 to Vb (AC ground)
	const Cx Hin = R3 / (R1 + 1.0 / (s * C1) + R3);
	// stage 1: closed loop of a non-inverting amplifier with feedback Zf = (R4 + R5) // C2 and Zg = R6 + 1 / (s C3), op-amp output resistance 50 ohm neglected
	const Cx Zfa = (R4 + R5), Zfc = 1.0 / (s * C2);
	const Cx Zf = Zfa * Zfc / (Zfa + Zfc), Zg = R6 + 1.0 / (s * C3);
	const Cx beta1 = Zg / (Zg + Zf);
	const Cx G1 = A / (1.0 + A * beta1);
	// clipping stage (off): R7 into C4 // (R9 + R8 to Vb), then the divider R8 / (R9 + R8)
	const Cx Zc4 = 1.0 / (s * C4), Zr = R9 + R8;
	const Cx Zp = Zc4 * Zr / (Zc4 + Zr);
	const Cx Hx = Zp / (R7 + Zp) * (R8 / Zr);
	// stage 2: Zf = R10 // C5, Zg = R11 + 1 / (s C6)
	const Cx Zf2c = 1.0 / (s * C5);
	const Cx Zf2 = R10 * Zf2c / (R10 + Zf2c), Zg2 = R11 + 1.0 / (s * C6);
	const Cx beta2 = Zg2 / (Zg2 + Zf2);
	const Cx G2 = A / (1.0 + A * beta2);
	// output: C7, series R12 (// R13 with the switch), then N1 loaded by the Tone branch (C8 + R14), R15 with its wiper and the 1M load
	const double Rs = sw ? R12 * R13 / (R12 + R13) : R12;
	const double Rtop = (1.0 - vol) * R15 + 1.0, Rbot = vol * R15 + 1.0;
	const double Rlw = Rbot * 1e6 / (Rbot + 1e6);
	const Cx Zv = Rtop + Rlw;
	const Cx Zt = R14 + 1.0 / (s * C8);
	const Cx ZN1 = Zv * Zt / (Zv + Zt);
	const Cx Hn1 = ZN1 / (1.0 / (s * C7) + Rs + ZN1);
	const Cx Hvol = Rlw / Zv;
	return Hin * G1 * Hx * G2 * Hn1 * Hvol;
}

int main (void)
{
	// 1. small signal (1 mV at the input keeps the clipping network off) against the analytic chain, Drive at the minimum, middle and maximum
	{
		double worst = 0;
		const float drives[3] = {0.0f, 50.0f, 100.0f};
		for (int k = 0; k < 3; k++)
		{
			TOcd o (drives[k], 50.0f, 1.0f, 10.0f);
			const double amp = drives[k] > 60 ? 2.0e-4 : 1.0e-3;
			double wk = 0;
			for (double f : {60.0, 150.0, 400.0, 1000.0, 2500.0})
			{
				const double g = Gain (o, f, amp);
				const double a = std::abs (Hchain (f, drives[k] * 0.01, 0.5, 1, 1.0));
				const double e = Db (g) - Db (a);
				printf ("   Drive %.0f %6.0f Hz: model %.2f dB analytic %.2f dB\n", drives[k], f, Db (g), Db (a));
				if (fabs (e) > fabs (wk)) wk = e;
			}
			printf ("Drive %.0f: worst difference against the analytic chain %.2f dB\n", drives[k], wk);
			if (fabs (wk) > fabs (worst)) worst = wk;
		}
		CHECK (fabs (worst) < 0.5, "small-signal response within 0.5 dB of the analytic circuit (%.2f dB)", worst);
	}

	// 2. gain range of the Drive at 1 kHz (published analysis: first stage from 1 + 18k / 2.2k = 9.2 to 1 + 518k / 2.2k = 236)
	{
		TOcd lo (0.0f, 50.0f, 1.0f, 10.0f), hi (100.0f, 50.0f, 1.0f, 10.0f);
		const double gl = Gain (lo, 1000.0, 1.0e-3), gh = Gain (hi, 1000.0, 2.0e-4);
		printf ("gain at 1 kHz: Drive 0 %.1f dB, Drive 100 %.1f dB\n", Db (gl), Db (gh));
		CHECK (Db (gh) - Db (gl) > 20.0 && Db (gh) - Db (gl) < 30.0, "the Drive changes the small-signal gain by %.1f dB (first stage 19 -> 47 dB, attenuated by C2 and the second stage)", Db (gh) - Db (gl));
	}

	// 3. clipping thresholds of the network at the clipping node (relative to Vb): positive side through Q2 + 1N34A, negative side through Q1
	{
		TOcd o (100.0f, 50.0f, 1.0f, 10.0f);
		double ph, xmin, xmax;
		Gain (o, 440.0, 0.3, &ph, &xmin, &xmax);
		printf ("clipping node, Drive 100, 0.3 V input: from %.2f to %.2f V around Vb (MOSFET threshold 2.1 V, germanium 0.2-0.3 V)\n", xmin, xmax);
		CHECK (xmax > 2.2 && xmax < 3.4, "positive side clips at %.2f V (Q2 + 1N34A)", xmax);
		CHECK (xmin < -2.0 && xmin > -3.1, "negative side clips at %.2f V (Q1)", xmin);
		CHECK (xmax > -xmin + 0.1, "the germanium diode makes the clipping asymmetric (%.2f against %.2f V)", xmax, -xmin);
		// the clipping node tracks Vb: no signal, no offset
		TOcd q (100.0f, 50.0f, 1.0f, 10.0f);
		Gain (q, 440.0, 1e-9, &ph, &xmin, &xmax);
		CHECK (fabs (xmin) < 0.01 && fabs (xmax) < 0.01, "without signal the clipping node sits at Vb (%.4f / %.4f V)", xmin, xmax);
	}

	// 4. Tone: at 5 kHz brighter towards 100; Volume; switch
	{
		TOcd t0 (30.0f, 0.0f, 1.0f, 10.0f), t1 (30.0f, 100.0f, 1.0f, 10.0f);
		const double a0 = Gain (t0, 5000.0, 1e-3), a1 = Gain (t1, 5000.0, 1e-3);
		printf ("5 kHz: Tone 0 %.1f dB, Tone 100 %.1f dB (difference %.1f dB)\n", Db (a0), Db (a1), Db (a1) - Db (a0));
		CHECK (Db (a1) - Db (a0) > 3.0, "Tone brighter towards 100 (%.1f dB at 5 kHz)", Db (a1) - Db (a0));
		TOcd v10 (30.0f, 50.0f, 1.0f, 10.0f), v5 (30.0f, 50.0f, 1.0f, 5.0f), v0 (30.0f, 50.0f, 1.0f, 0.0f);
		const double g10 = Gain (v10, 1000.0, 1e-3), g5 = Gain (v5, 1000.0, 1e-3), g0 = Gain (v0, 1000.0, 1e-3);
		printf ("Volume 10 %.1f dB, 5 %.1f dB, 0 %.1f dB\n", Db (g10), Db (g5), Db (g0));
		CHECK (g5 < g10 * 0.8 && g0 < g10 * 0.001, "the Volume lowers the level (5: %.1f dB, 0: %.1f dB below 10)", Db (g5 / g10), Db (g0 / g10));
		TOcd s1 (30.0f, 50.0f, 1.0f, 10.0f), s0 (30.0f, 50.0f, 0.0f, 10.0f);
		const double b1 = Gain (s1, 1000.0, 1e-3), b0 = Gain (s0, 1000.0, 1e-3);
		printf ("switch: R13 in %.1f dB, R13 out %.1f dB (difference %.2f dB)\n", Db (b1), Db (b0), Db (b1) - Db (b0));
		CHECK (b1 > b0, "R13 in parallel with R12 lowers the series resistance: more level (%.2f dB)", Db (b1) - Db (b0));
	}

	// 5. stability: every combination with strong signals (a guitar level and the full scale), finite and bounded
	{
		bool ok = true;
		double pkv = 0;
		for (int amp = 0; amp < 2; amp++) for (int d = 0; d < 2; d++) for (int t = 0; t < 2; t++) for (int s = 0; s < 2; s++)
		{
			TOcd o (d ? 100.0f : 0.0f, t ? 100.0f : 0.0f, (float) s, 10.0f);
			unsigned seed = 7;
			for (int i = 0; i < 24000; i += 4)
			{
				float l[4], r[4];
				for (int j = 0; j < 4; j++) { seed = seed * 1664525u + 1013904223u; l[j] = r[j] = (float) ((((int) (seed >> 8) / 8388608.0) - 1.0) * (amp ? 1.0 : 0.25)); }
				int m = 1;
				o.fx->Process (l, r, 4, &m, o.p);
				for (int j = 0; j < 4; j++) { if (!(fabsf (l[j]) < 40.0f)) ok = false; if (fabsf (l[j]) > pkv) pkv = fabsf (l[j]); }
			}
		}
		CHECK (ok, "stable with noise at guitar level and at full scale at the extremes (peak %.2f of full scale)", pkv);
	}
	// 6. speed: Drive 100 and a loud guitar-like sine
	{
		TOcd o (100.0f, 50.0f, 1.0f, 10.0f);
		const auto c0 = std::chrono::steady_clock::now ();
		for (int i = 0; i < 96000; i += 4)
		{
			float l[4], r[4];
			for (int j = 0; j < 4; j++) l[j] = r[j] = (float) (0.4 * sin (2 * M_PI * 196.0 * (i + j) / FS));
			int m = 1;
			o.fx->Process (l, r, 4, &m, o.p);
		}
		const double us = std::chrono::duration<double> (std::chrono::steady_clock::now () - c0).count () * 1e6 / 96000.0;
		printf ("time: %.3f us per sample (one channel)\n", us);
		CHECK (us < 8.0, "cost below 8 us per sample (%.2f)", us);
	}
	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
