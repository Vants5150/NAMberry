// test_noise.cpp - tests of ADELNoise (fx_noise.cpp) on the PC: transparency when off, hiss level and normalisation, spectrum (white / pink, bandwidth, low cut),
// hum (harmonic series, mains, level), additivity, stereo, block size, instances, stability.
// Run with tests/run_host_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <complex>
#include <vector>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)
#define CHECKF(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); CHECK (c, b_); } while (0)

static double Db (double v) { return 20 * log10 (v + 1e-30); }

// parameters in the order of fx_defs.h: hiss, color, bandwidth, lowcut, hum, humtype, mains
struct Par
{
	float p[10];
	Par (float hiss = -130, float color = 50, float bw = 20, float lc = 20, float hum = -120, float type = 0, float mains = 0)
	{ p[0] = hiss; p[1] = color; p[2] = bw; p[3] = lc; p[4] = hum; p[5] = type; p[6] = mains; p[7] = p[8] = p[9] = 0; }
};

// output of the block for a silent input (the added noise itself), n samples, blocks of blk
static std::vector<float> Noise (const Par &pa, unsigned n, float fs = 48000.0f, unsigned blk = 4, int stereo = 0, std::vector<float> *outR = 0)
{
	CFx *fx = FxCreate (FxNoise, fs);
	std::vector<float> L (n, 0.0f), R (n, 0.0f);
	for (unsigned b = 0; b < n; b += blk)
	{
		const unsigned m = blk < n - b ? blk : n - b;
		int mono = stereo ? 0 : 1;
		fx->Process (&L[b], &R[b], m, &mono, pa.p);
	}
	delete fx;
	if (outR) *outR = R;
	return L;
}

static double Rms (const std::vector<float> &v, unsigned from = 0)
{
	double s = 0; for (size_t i = from; i < v.size (); i++) s += (double) v[i] * v[i];
	return sqrt (s / (v.size () - from));
}

// Welch power spectrum (Hann, 4096 points, 50% overlap), returns power per bin
static std::vector<double> Welch (const std::vector<float> &x)
{
	const unsigned N = 4096;
	std::vector<double> P (N / 2 + 1, 0.0);
	unsigned cnt = 0;
	for (size_t s = 0; s + N <= x.size (); s += N / 2)
	{
		std::vector<std::complex<double>> a (N);
		for (unsigned i = 0; i < N; i++) a[i] = x[s + i] * (0.5 - 0.5 * cos (2 * M_PI * i / N));
		for (unsigned i = 1, j = 0; i < N; i++)
		{
			unsigned bit = N >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j) std::swap (a[i], a[j]);
		}
		for (unsigned len = 2; len <= N; len <<= 1)
		{
			const std::complex<double> w (cos (-2 * M_PI / len), sin (-2 * M_PI / len));
			for (unsigned i = 0; i < N; i += len)
			{
				std::complex<double> wn (1, 0);
				for (unsigned k = 0; k < len / 2; k++)
				{
					const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * wn;
					a[i + k] = u + v; a[i + k + len / 2] = u - v; wn *= w;
				}
			}
		}
		for (unsigned k = 0; k <= N / 2; k++) P[k] += std::norm (a[k]);
		cnt++;
	}
	for (auto &p : P) p /= cnt;
	return P;
}

static double BandPower (const std::vector<double> &P, double f1, double f2, double fs)
{
	const double df = fs / 4096.0; double s = 0;
	for (unsigned k = 0; k < P.size (); k++) { const double f = k * df; if (f >= f1 && f < f2) s += P[k]; }
	return s;
}

// amplitude of the component at f (a whole number of Hz) over n samples (Goertzel-free DFT at a single frequency)
static double Amp (const std::vector<float> &x, double f, double fs)
{
	double re = 0, im = 0;
	for (size_t i = 0; i < x.size (); i++) { const double w = 2 * M_PI * f * i / fs; re += x[i] * cos (w); im += x[i] * sin (w); }
	return 2.0 * sqrt (re * re + im * im) / x.size ();
}

int main (void)
{
	// 1. both off: bit-exact transparency, mono block copies left to right
	{
		CFx *fx = FxCreate (FxNoise, 48000.0f);
		Par pa; bool same = true;
		for (unsigned b = 0; b < 2000; b++)
		{
			float L[4], R[4], I[4], J[4];
			for (int j = 0; j < 4; j++) { I[j] = L[j] = 0.7f * (float) sin (0.05 * (b * 4 + j)); J[j] = R[j] = 0.3f * (float) cos (0.03 * (b * 4 + j)); }
			int mono = 0;
			fx->Process (L, R, 4, &mono, pa.p);
			for (int j = 0; j < 4; j++) if (L[j] != I[j] || R[j] != J[j]) same = false;
		}
		CHECK (same, "Hiss and Hum at their minimum: the output is identical to the input");
		float L[4] = {0.3f, -0.2f, 0.1f, 0.5f}, R[4] = {9, 9, 9, 9};
		int mono = 1;
		fx->Process (L, R, 4, &mono, pa.p);
		CHECK (R[0] == L[0] && R[1] == L[1] && R[2] == L[2] && R[3] == L[3], "mono block: the right channel copies the left one");
		delete fx;
	}
	// 2. hiss level: close to the setting (the two filters take a little away at 20 kHz) and the output meter says the same
	for (float lvl : {-100.0f, -80.0f, -60.0f})
	{
		for (float color : {0.0f, 50.0f, 100.0f})
		{
			const std::vector<float> n = Noise (Par (lvl, color, 20, 20), 48000 * 20);
			const double r = Db (Rms (n, 4800));
			CHECKF (r < lvl + 0.3 && r > lvl - 2.2, "hiss %.0f dB, color %.0f%%: measured %.2f dB", lvl, color, r);
		}
	}
	{
		CFx *fx = FxCreate (FxNoise, 48000.0f);
		Par pa (-70, 50, 20, 20);
		std::vector<float> L (48000 * 4), R (48000 * 4);
		for (size_t b = 0; b < L.size (); b += 4) { int mono = 1; fx->Process (&L[b], &R[b], 4, &mono, pa.p); }
		float o[64]; fx->GetOutputs (o);
		const double r = Db (Rms (L, 48000));
		CHECKF (fabs (o[7] - r) < 0.4, "Noise level output %.2f dB against the measured %.2f dB", o[7], r);
		delete fx;
	}
	// 3. spectrum: white = +3 dB per octave in octave bands, pink = flat in octave bands
	{
		const double fs = 48000;
		const std::vector<double> Pw = Welch (Noise (Par (-60, 0, 20, 20), 48000 * 30)), Pp = Welch (Noise (Par (-60, 100, 20, 20), 48000 * 30));
		double sw = 0, sp = 0; int n = 0;
		double bw_[6], bp_[6];
		for (int i = 0; i < 6; i++) { const double f1 = 150.0 * pow (2.0, i); bw_[i] = 10 * log10 (BandPower (Pw, f1, 2 * f1, fs)); bp_[i] = 10 * log10 (BandPower (Pp, f1, 2 * f1, fs)); }
		for (int i = 1; i < 6; i++) { sw += bw_[i] - bw_[i - 1]; sp += bp_[i] - bp_[i - 1]; n++; }
		sw /= n; sp /= n;
		double worstP = 0;
		for (int i = 1; i < 6; i++) { const double d = fabs ((bp_[i] - bp_[i - 1])); if (d > worstP) worstP = d; }
		CHECKF (fabs (sw - 3.0) < 0.6, "white hiss: %.2f dB per octave in octave bands (expected +3)", sw);
		CHECKF (fabs (sp) < 0.6 && worstP < 1.5, "pink hiss: %.2f dB per octave on average, worst octave step %.2f dB (expected 0)", sp, worstP);
	}
	// 4. bandwidth and low cut
	{
		const double fs = 48000;
		const std::vector<double> Pa = Welch (Noise (Par (-60, 0, 20, 20), 48000 * 30)), Pb = Welch (Noise (Par (-60, 0, 2, 20), 48000 * 30)), Pc = Welch (Noise (Par (-60, 0, 20, 400), 48000 * 30));
		const double hiA = BandPower (Pa, 6000, 12000, fs), hiB = BandPower (Pb, 6000, 12000, fs), loA = BandPower (Pa, 20, 100, fs), loC = BandPower (Pc, 20, 100, fs);
		CHECKF (10 * log10 (hiA / hiB) > 15.0, "Hiss bandwidth 2 kHz: the 6-12 kHz band falls by %.1f dB", 10 * log10 (hiA / hiB));
		CHECKF (10 * log10 (loA / loC) > 10.0, "Hiss low cut 400 Hz: the 20-100 Hz band falls by %.1f dB", 10 * log10 (loA / loC));
	}
	// 5. hum: harmonic series and level, for the three types and both mains frequencies
	{
		const float amp0[8] = {1.0f, 0.5f, 0.3333f, 0.25f, 0.2f, 0.1667f, 0.1429f, 0.125f};
		const float amp1[6] = {1.0f, 0.4f, 0.25f, 0.12f, 0.08f, 0.05f};
		for (int type = 0; type < 3; type++)
		{
			for (int mains = 0; mains < 2; mains++)
			{
				const double fm = mains ? 60.0 : 50.0, f0 = fm * (type == 0 ? 2 : 1);
				const std::vector<float> h = Noise (Par (-130, 50, 20, 20, -70, (float) type, (float) mains), 48000 * 2);
				const double r = Db (Rms (h));
				const unsigned nh = type == 0 ? 8 : (type == 1 ? 6 : 20);
				bool ok = true; double worst = 0;
				const double a1 = Amp (h, f0, 48000.0);
				for (unsigned k = 1; k <= nh; k++)
				{
					const double want = type == 0 ? amp0[k - 1] : (type == 1 ? amp1[k - 1] : 1.0 / sqrt ((double) k));
					const double got = Amp (h, f0 * k, 48000.0) / a1;
					const double err = fabs (Db (got / want));
					if (err > worst) worst = err;
					if (err > 0.3) ok = false;
				}
				const double off = Amp (h, f0 * (nh + 1) + 7, 48000.0) / a1;
				CHECKF (fabs (r + 70.0) < 0.15 && ok && off < 0.01, "hum type %d, %.0f Hz mains: fundamental %.0f Hz, level %.2f dB, %u harmonics within %.2f dB of the design", type, fm, f0, r, nh, worst);
			}
		}
	}
	// 6. additive: with a signal the output is input + noise, the signal is not altered
	{
		Par pa (-130, 50, 20, 20, -60, 0, 0);
		const std::vector<float> hum = Noise (pa, 48000);
		CFx *fx = FxCreate (FxNoise, 48000.0f);
		std::vector<float> L (48000), R (48000), I (48000);
		for (unsigned i = 0; i < 48000; i++) I[i] = L[i] = R[i] = (float) (0.5 * sin (2 * M_PI * 1000.0 * i / 48000.0));
		for (unsigned b = 0; b < 48000; b += 4) { int mono = 0; fx->Process (&L[b], &R[b], 4, &mono, pa.p); }
		double worst = 0; bool eq = true;
		for (unsigned i = 0; i < 48000; i++) { worst = fmax (worst, fabs ((double) (L[i] - I[i]) - hum[i])); if (L[i] != R[i]) eq = false; }
		CHECKF (worst < 2e-7, "with a 1 kHz signal the output is input + hum (max difference %.1e)", worst);
		CHECK (eq, "the same noise is added to the two channels");
		delete fx;
	}
	// 7. the output does not depend on the block size (hum is deterministic; hiss level is the same)
	{
		const Par pa (-130, 50, 20, 20, -65, 2, 0);
		const std::vector<float> a = Noise (pa, 48000, 48000.0f, 4), b = Noise (pa, 48000, 48000.0f, 64);
		CHECK (memcmp (a.data (), b.data (), sizeof (float) * 48000) == 0, "hum identical with blocks of 4 and 64 samples");
		const double ra = Db (Rms (Noise (Par (-70, 50, 12, 20), 48000 * 10, 48000.0f, 4), 4800)), rb = Db (Rms (Noise (Par (-70, 50, 12, 20), 48000 * 10, 48000.0f, 64), 4800));
		CHECKF (fabs (ra - rb) < 0.15, "hiss level the same with blocks of 4 and 64 samples (%.2f and %.2f dB)", ra, rb);
	}
	// 8. two instances make different noise
	{
		const std::vector<float> a = Noise (Par (-60), 48000), b = Noise (Par (-60), 48000);
		double c = 0, aa = 0, bb = 0;
		for (unsigned i = 0; i < 48000; i++) { c += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
		CHECKF (fabs (c / sqrt (aa * bb)) < 0.03, "two blocks are not correlated (correlation %.4f)", c / sqrt (aa * bb));
	}
	// 9. sample rates: the hum frequencies stay right, harmonics above 0.45 fs are dropped
	for (float fs : {44100.0f, 96000.0f})
	{
		const std::vector<float> h = Noise (Par (-130, 50, 20, 20, -70, 2, 0), (unsigned) fs * 2, fs);
		const double a1 = Amp (h, 50.0, fs), a5 = Amp (h, 250.0, fs);
		CHECKF (fabs (Db (a5 / a1 * sqrt (5.0))) < 0.3 && fabs (Db (Rms (h) / pow (10.0, -70.0 / 20))) < 0.2, "sample rate %.0f Hz: hum at 50 Hz and 250 Hz right, level %.2f dB", fs, Db (Rms (h)));
	}
	// 10. stability: extreme and random parameters, NaN in the input
	{
		unsigned seed = 5; bool fin = true;
		for (int t = 0; t < 40; t++)
		{
			Par pa;
			for (int i = 0; i < 7; i++) { seed = seed * 1664525u + 1013904223u; pa.p[i] = ((float) (seed >> 8) / 16777216.0f - 0.25f) * 700.0f; }
			CFx *fx = FxCreate (FxNoise, t & 1 ? 44100.0f : 48000.0f);
			for (int b = 0; b < 3000; b++)
			{
				float L[4], R[4];
				for (int j = 0; j < 4; j++) { seed = seed * 1664525u + 1013904223u; L[j] = R[j] = ((float) (seed >> 8) / 8388608.0f - 1.0f); }
				int mono = b & 1;
				fx->Process (L, R, 4, &mono, pa.p);
				for (int j = 0; j < 4; j++) if (!std::isfinite (L[j]) || !std::isfinite (R[j])) fin = false;
			}
			float o[64]; fx->GetOutputs (o);
			if (!std::isfinite (o[7])) fin = false;
			delete fx;
		}
		CHECK (fin, "40 random parameter sets: the output and the meter stay finite");
		CFx *fx = FxCreate (FxNoise, 48000.0f);
		Par pa (-60, 50, 12, 20, -70, 0, 0);
		float L[4] = {NAN, 0.1f, INFINITY, 0.2f}, R[4] = {0, 0, 0, 0};
		int mono = 0;
		fx->Process (L, R, 4, &mono, pa.p);
		std::vector<float> a (4800), r (4800);
		for (size_t b = 0; b < a.size (); b += 4) { fx->Process (&a[b], &r[b], 4, &mono, pa.p); }
		CHECKF (std::isfinite (Rms (a)) && fabs (Db (Rms (a)) + 63.0) < 6.0, "after NaN and inf in the input the noise is unchanged (%.1f dB)", Db (Rms (a)));
		delete fx;
	}
	// 11. definition in the table
	{
		const TFxDef *d = FxDefByType (FxNoise);
		CHECK (d != 0 && d->nParams == 8, "ADELNoise is in the table with 8 parameters (7 controls + 1 output)");
		bool ok = d != 0;
		for (unsigned i = 0; d && i < d->nParams; i++)
		{
			const TFxParam &q = d->pParams[i];
			if (!(q.Def >= q.Min && q.Def <= q.Max) || q.Min >= q.Max) ok = false;
		}
		CHECK (ok, "every default is inside its range");
	}
	printf ("\nRESULT: %s (%d failures)\n", fails ? "FAILED" : "all ok", fails);
	return fails ? 1 : 0;
}
