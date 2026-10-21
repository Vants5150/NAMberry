// test_univibe.cpp - Uni-Vibe Real: each phase stage against the equations of Darabundit et al. (DAFx-19), levels of the Chorus and Vibrato outputs, LDR ranges of Table 1, lamp modulation, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_univibe.cpp fx_univibe.cpp -o test_univibe
#include <cstdio>
#include <cmath>
#include <complex>
#include <chrono>
#include <vector>
#include <cstring>
#include "fx.h"

void UniVibeStageForTest (unsigned nStage, double dRldr, double dFs, const float *pIn, float *pOut, unsigned n);
void UniVibeResForTest (CFx *pFx, double *pR);

static int fails = 0;
#define CHECK(c, ...) do { char b_[300]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// Table 1 and constants of the paper
struct TS { double cp, alpha, beta, avg, rmin, rmax; };
static const TS T[4] = {{15e-9, 1.01, 1.11, 0.405e6, 12.7e3, 2.79e6}, {220e-9, 0.98, 1.09, 0.233e6, 6.86e3, 2.59e6}, {470e-12, 0.97, 1.10, 0.29e6, 7.69e3, 3.32e6}, {4.7e-9, 0.95, 1.09, 0.240e6, 6.22e3, 4.16e6}};
static const double R6 = 4.7e3, CDC = 1e-6;

// analytic H(j w) of a stage: (alpha ke wo - beta (kc wo + s)) / (wo + s)
static std::complex<double> Hs (int s, double rLdr, double f)
{
	const double ke = CDC / (T[s].cp + CDC), kc = T[s].cp / (T[s].cp + CDC);
	const double wo = (1.0 / (rLdr + R6)) * (CDC + T[s].cp) / (T[s].cp * CDC);
	const std::complex<double> S (0.0, 2 * M_PI * f);
	return (T[s].alpha * ke * wo - T[s].beta * (kc * wo + S)) / (wo + S);
}

static void Meas (const float *in, const float *out, unsigned from, unsigned n, double f, double amp, double *mag, double *ph)
{
	double sn = 0, cs = 0, sn2 = 0, cs2 = 0;
	for (unsigned i = from; i < n; i++)
	{
		const double th = 2 * M_PI * f * i / FS;
		sn += out[i] * sin (th); cs += out[i] * cos (th);
	}
	const double M = n - from;
	*mag = 2.0 * sqrt (sn * sn + cs * cs) / M / amp;
	*ph = atan2 (cs, sn);			// out = mag sin (th + ph)
	(void) in; (void) sn2; (void) cs2;
}

// runs the instance on a sine; returns the gain of the last part
static double RunFx (CFx *fx, double f, double amp, double seconds, const float *p, double *ph = 0, std::vector<float> *outv = 0)
{
	const unsigned N = (unsigned) (seconds * FS) / 4 * 4;
	std::vector<float> o (N);
	for (unsigned b = 0; b < N / 4; b++)
	{
		float L[4], R[4];
		for (int j = 0; j < 4; j++) { const unsigned i = b * 4 + j; L[j] = R[j] = (float) (amp * sin (2 * M_PI * f * i / FS)); }
		int m = 1;
		fx->Process (L, R, 4, &m, p);
		for (int j = 0; j < 4; j++) o[b * 4 + j] = L[j];
	}
	double mag, phase;
	const unsigned from = N / 2 / 4 * 4;
	// whole cycles in the measured part
	unsigned cyc = (unsigned) ((N - from) * f / FS);
	const unsigned M = cyc > 0 ? (unsigned) (cyc * FS / f + 0.5) : N - from;
	Meas (0, o.data (), N - M, N, f, amp, &mag, &phase);
	if (ph) *ph = phase;
	if (outv) *outv = o;
	return mag;
}

int main (void)
{
	// 1. each stage alone, fixed LDR (minimum, average and maximum of Table 1), against the analytic response
	{
		double worstM = 0, worstP = 0;
		for (int s = 0; s < 4; s++)
		{
			const double Rs[3] = {T[s].rmin, T[s].avg, T[s].rmax};
			for (int k = 0; k < 3; k++)
			{
				const double wo = (1.0 / (Rs[k] + R6)) * (CDC + T[s].cp) / (T[s].cp * CDC), f0 = wo / (2 * M_PI);
				const double fr[3] = {0.5 * f0, f0, 2.0 * f0};
				for (int q = 0; q < 3; q++)
				{
					const double f = fr[q];
					if (f < 40.0 || f > 5000.0) continue;
					const unsigned N = (unsigned) (FS * (f < 100 ? 3.0 : 1.0)) / 4 * 4;
					std::vector<float> in (N), out (N);
					for (unsigned i = 0; i < N; i++) in[i] = (float) (0.1 * sin (2 * M_PI * f * i / FS));
					UniVibeStageForTest (s, Rs[k], FS, in.data (), out.data (), N);
					unsigned cyc = (unsigned) ((N / 2) * f / FS);
					const unsigned M = cyc > 0 ? (unsigned) (cyc * FS / f + 0.5) : N / 2;
					double mag, ph;
					Meas (in.data (), out.data (), N - M, N, f, 0.1, &mag, &ph);
					const std::complex<double> h = Hs (s, Rs[k], f);
					// analytic phase relative to the input: arg (h); measured: ph
					double dp = (ph - std::arg (h)) * 180 / M_PI;
					while (dp > 180) dp -= 360;
					while (dp < -180) dp += 360;
					const double dm = Db (mag) - Db (std::abs (h));
					if (fabs (dm) > worstM) worstM = fabs (dm);
					if (fabs (dp) > worstP) worstP = fabs (dp);
				}
			}
		}
		printf ("stage against the analytic response (f from 40 Hz to 5 kHz): worst magnitude error %.3f dB, worst phase error %.2f degrees\n", worstM, worstP);
		CHECK (worstM < 0.5, "stage magnitude within 0.5 dB of the paper's equations (%.3f dB)", worstM);
		CHECK (worstP < 4.0, "stage phase within 4 degrees of the paper's equations (%.2f degrees)", worstP);
	}

	// 2. Table 1: with Intensity 0 the LDRs stay at their average resistance
	float P[4] = {2.0f, 0.0f, 0.0f, 10.0f};
	{
		CFx *fx = FxCreate (FxUniVibe, (float) FS);
		RunFx (fx, 440.0, 0.01, 1.0, P);
		double R[4];
		UniVibeResForTest (fx, R);
		printf ("Intensity 0: LDR %.0f %.0f %.0f %.0f kohm (Table 1 averages: 405 233 290 240)\n", R[0] / 1e3, R[1] / 1e3, R[2] / 1e3, R[3] / 1e3);
		for (int s = 0; s < 4; s++) CHECK (fabs (R[s] / T[s].avg - 1.0) < 0.01, "stage %d: static resistance equals the average of Table 1 (%.0f kohm)", s + 1, R[s] / 1e3);
		delete fx;
	}

	// 3. levels: small signal at 1 kHz, Intensity 0, against the analytic chain (preamp, four stages, output mixer)
	for (int vib = 0; vib < 2; vib++)
	{
		float Q[4] = {2.0f, 0.0f, (float) vib, 10.0f};
		CFx *fx = FxCreate (FxUniVibe, (float) FS);
		const double amp = 1.0e-4;
		double ph;
		const double g = RunFx (fx, 1000.0, amp, 2.0, Q, &ph);
		std::complex<double> w (1.0, 0.0);
		for (int s = 0; s < 4; s++) w *= Hs (s, T[s].avg, 1000.0);
		const double pre = 0.6812 * 4.25;
		const std::complex<double> o = vib ? 0.594 * pre * w : pre * (1.0 + w) / 3.0;
		const double ref = std::abs (o);
		printf ("%s at 1 kHz: model %.3f (%.2f dB), analytic %.3f (%.2f dB)\n", vib ? "Vibrato" : "Chorus", g, Db (g), ref, Db (ref));
		CHECK (fabs (Db (g) - Db (ref)) < 0.4, "%s level within 0.4 dB of the analytic chain", vib ? "Vibrato" : "Chorus");
		delete fx;
	}

	// 4. lamp modulation: Intensity 100, Speed 2 Hz: the LDRs swing over a wide range with period 0.5 s
	{
		float Q[4] = {2.0f, 100.0f, 0.0f, 10.0f};
		CFx *fx = FxCreate (FxUniVibe, (float) FS);
		std::vector<double> r1;
		double rmin = 1e12, rmax = 0;
		for (unsigned b = 0; b < (unsigned) (FS * 6.0 / 4); b++)
		{
			float L[4] = {0, 0, 0, 0}, R[4] = {0, 0, 0, 0};
			int m = 1;
			fx->Process (L, R, 4, &m, Q);
			if ((b & 15) == 0 && b > (unsigned) (FS * 1.0 / 4))
			{
				double RR[4];
				UniVibeResForTest (fx, RR);
				r1.push_back (log (RR[0]));
				if (RR[0] < rmin) rmin = RR[0];
				if (RR[0] > rmax) rmax = RR[0];
			}
		}
		printf ("Intensity 100, Speed 2 Hz: LDR1 from %.1f kohm to %.0f kohm (Table 1: 12.7 kohm - 2.79 Mohm)\n", rmin / 1e3, rmax / 1e3);
		CHECK (rmax / rmin > 20.0, "LDR1 swings over more than a factor 20 (%.0f)", rmax / rmin);
		CHECK (rmin >= 12.0e3 && rmax <= 2.9e6, "LDR1 stays inside the measured range");
		// period: autocorrelation of the log resistance, step = 16 blocks of 4 samples = 64 samples
		const double dt = 64.0 / FS;
		double mean = 0;
		for (double v : r1) mean += v;
		mean /= r1.size ();
		double best = 0; int bl = 0;
		const int lagMin = (int) (0.2 / dt), lagMax = (int) (1.0 / dt);
		double c0 = 0;
		for (size_t i = 0; i < r1.size (); i++) c0 += (r1[i] - mean) * (r1[i] - mean);
		for (int lag = lagMin; lag < lagMax; lag++)
		{
			double c = 0;
			for (size_t i = 0; i + lag < r1.size (); i++) c += (r1[i] - mean) * (r1[i + lag] - mean);
			c /= c0;
			if (c > best) { best = c; bl = lag; }
		}
		printf ("period of the LDR modulation %.3f s (expected 0.5 s), correlation %.2f\n", bl * dt, best);
		CHECK (fabs (bl * dt - 0.5) < 0.03, "the LDR modulation has the period of the LFO (%.3f s)", bl * dt);
		delete fx;
	}

	// 5. Intensity: more intensity, wider sweep; Speed also changes the LFO amplitude (Geofex)
	{
		double span[3];
		const float it[3] = {20.0f, 60.0f, 100.0f};
		for (int k = 0; k < 3; k++)
		{
			float Q[4] = {1.0f, it[k], 0.0f, 10.0f};
			CFx *fx = FxCreate (FxUniVibe, (float) FS);
			double lo = 1e12, hi = 0;
			for (unsigned b = 0; b < (unsigned) (FS * 5.0 / 4); b++)
			{
				float L[4] = {0, 0, 0, 0}, R[4] = {0, 0, 0, 0};
				int m = 1;
				fx->Process (L, R, 4, &m, Q);
				if (b > (unsigned) (FS * 1.5 / 4) && (b & 15) == 0) { double RR[4]; UniVibeResForTest (fx, RR); if (RR[2] < lo) lo = RR[2]; if (RR[2] > hi) hi = RR[2]; }
			}
			span[k] = hi / lo;
			delete fx;
		}
		printf ("LDR3 span at Intensity 20/60/100: %.1f / %.1f / %.1f\n", span[0], span[1], span[2]);
		CHECK (span[0] < span[1] && span[1] < span[2], "the sweep widens with the Intensity");
	}

	// 6. volume, mono/stereo, stability
	{
		CFx *fx = FxCreate (FxUniVibe, (float) FS);
		float Q[4] = {2.0f, 70.0f, 0.0f, 10.0f}, Q5[4] = {2.0f, 70.0f, 0.0f, 5.0f};
		std::vector<float> a, b;
		RunFx (fx, 1000.0, 1e-3, 1.0, Q);
		const double g10 = RunFx (fx, 1000.0, 1e-3, 1.0, Q);
		RunFx (fx, 1000.0, 1e-3, 1.0, Q5);
		const double g5 = RunFx (fx, 1000.0, 1e-3, 1.0, Q5);
		printf ("Volume 10 %.3f, Volume 5 %.3f (ratio %.1f dB, expected about -12 dB)\n", g10, g5, Db (g5 / g10));
		CHECK (fabs (Db (g5 / g10) + 12.0) < 3.0, "Volume 5 is about 12 dB below Volume 10 (audio taper)");
		delete fx;
		// stereo with identical channels: identical outputs; stereo flag stays
		fx = FxCreate (FxUniVibe, (float) FS);
		bool same = true, fin = true;
		float pk = 0;
		for (unsigned bl = 0; bl < 200000 / 4; bl++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++) { L[j] = R[j] = (float) (0.9 * sin (2 * M_PI * 196.0 * (bl * 4 + j) / FS) + 0.1 * sin (bl * 0.37 + j)); }
			int m = 0;
			fx->Process (L, R, 4, &m, Q);
			for (int j = 0; j < 4; j++) { if (L[j] != R[j]) same = false; if (!(fabsf (L[j]) < 3.0f)) fin = false; if (fabsf (L[j]) > pk) pk = fabsf (L[j]); }
		}
		CHECK (same, "stereo input with equal channels gives equal outputs (the lamp is shared)");
		CHECK (fin, "stable with a 0 dBFS input and Intensity 70 (peak %.2f)", pk);
		delete fx;
		// extremes: every combination of Speed / Intensity / Mode / Volume
		bool ok = true;
		for (int sp = 0; sp < 2; sp++) for (int it = 0; it < 2; it++) for (int md = 0; md < 2; md++)
		{
			float E[4] = {sp ? 7.6f : 0.1f, it ? 100.0f : 0.0f, (float) md, 10.0f};
			fx = FxCreate (FxUniVibe, (float) FS);
			unsigned seed = 1;
			for (unsigned bl = 0; bl < 100000 / 4; bl++)
			{
				float L[4], R[4];
				for (int j = 0; j < 4; j++) { seed = seed * 1664525u + 1013904223u; L[j] = R[j] = ((int) (seed >> 8) / 8388608.0f - 1.0f) * 0.9f; }
				int m = 1;
				fx->Process (L, R, 4, &m, E);
				for (int j = 0; j < 4; j++) if (!(fabsf (L[j]) < 4.0f)) ok = false;
			}
			delete fx;
		}
		CHECK (ok, "stable with full-scale noise at the extremes of Speed, Intensity and Mode");
	}

	// 7. clipping: a hot signal is asymmetric (second harmonic), no DC at the output
	{
		float Q[4] = {2.0f, 0.0f, 1.0f, 10.0f};
		CFx *fx = FxCreate (FxUniVibe, (float) FS);
		std::vector<float> o;
		RunFx (fx, 500.0, 0.8, 2.0, Q, 0, &o);
		double h1 = 0, h2 = 0, dc = 0;
		const unsigned from = (unsigned) (FS * 1.0), N = (unsigned) o.size (), M = (unsigned) (500 * ((N - from) / FS)) * (unsigned) (FS / 500);
		double s1 = 0, c1 = 0, s2 = 0, c2 = 0;
		for (unsigned i = N - M; i < N; i++) { const double th = 2 * M_PI * 500.0 * i / FS; s1 += o[i] * sin (th); c1 += o[i] * cos (th); s2 += o[i] * sin (2 * th); c2 += o[i] * cos (2 * th); dc += o[i]; }
		h1 = 2 * sqrt (s1 * s1 + c1 * c1) / M; h2 = 2 * sqrt (s2 * s2 + c2 * c2) / M; dc /= M;
		printf ("0.8 FS at 500 Hz, Vibrato: fundamental %.3f, second harmonic %.4f (%.1f dB), DC %.5f\n", h1, h2, Db (h2 / h1), dc);
		CHECK (h2 / h1 > 0.003 && h2 / h1 < 0.3, "asymmetric clipping adds a second harmonic (%.1f dB)", Db (h2 / h1));
		CHECK (fabs (dc) < 0.01, "no DC at the output");
		delete fx;
	}

	// 8. cost
	{
		float Q[4] = {2.0f, 70.0f, 0.0f, 10.0f};
		CFx *fx = FxCreate (FxUniVibe, (float) FS);
		const auto c0 = std::chrono::steady_clock::now ();
		for (unsigned b = 0; b < 200000; b++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++) L[j] = R[j] = 0.3f * sinf ((float) (b * 4 + j) * 0.0575f);
			int m = 0;
			fx->Process (L, R, 4, &m, Q);
		}
		const double us = std::chrono::duration<double> (std::chrono::steady_clock::now () - c0).count () * 1e6 / 200000.0;
		printf ("time: %.3f us per block of 4 stereo frames\n", us);
		delete fx;
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
