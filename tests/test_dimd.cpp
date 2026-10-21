// test_dimd.cpp - tests of Dimension D Real (fx_dimd.cpp) on the PC: delay per mode (Roland table 8-12, 5-10, 6-9, 6-9 ms), LFO in antiphase, channel crossing, same-side share with button 4,
// frequency response of the wet path, stability, calculation time.
// g++ -std=c++17 -O1 -Ihost_shim -I../../../../include -I. -Iadel test_dimd.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_dimd
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <chrono>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)
#define CHECKF(c, ...) do { char b_[220]; snprintf (b_, sizeof b_, __VA_ARGS__); CHECK (c, b_); } while (0)

static const float FS = 48000.0f;
static unsigned s_seed = 777;
static float Noise (void) { s_seed = s_seed * 1664525u + 1013904223u; return ((float) (int) (s_seed >> 8) - 8388608.0f) / 8388608.0f; }

// runs the block on (inL, inR) and returns the outputs
static void RunFx (float mode, float input, const std::vector<float> &inL, const std::vector<float> &inR, std::vector<float> &oL, std::vector<float> &oR)
{
	CFx *fx = FxCreate (FxDimD, FS);
	float P[FX_MAX_P] = {mode, input, 0, 0, 0, 0, 0, 0};
	const size_t n = inL.size ();
	oL.assign (n, 0.0f); oR.assign (n, 0.0f);
	for (size_t i = 0; i < n; i += 4)
	{
		float l[4], r[4];
		for (int j = 0; j < 4; j++) { l[j] = inL[i + j]; r[j] = inR[i + j]; }
		int mono = 0;
		fx->Process (l, r, 4, &mono, P);
		for (int j = 0; j < 4; j++) { oL[i + j] = l[j]; oR[i + j] = r[j]; }
	}
	delete fx;
}

// delay (ms) per 200 ms window: maximum of the correlation between input (white noise) and output minus the dry part
static std::vector<double> Delays (const std::vector<float> &in, const std::vector<float> &wet, double *tmin, double *tmax)
{
	std::vector<double> d;
	const unsigned W = 9600, H = 4800;
	for (unsigned s = 48000 / 2; s + W + 800 < in.size (); s += H)
	{
		double best = -1e30; unsigned bl = 0; double c[800];
		for (unsigned lag = 100; lag < 800; lag++)
		{
			double a = 0;
			for (unsigned i = s; i < s + W; i++) a += (double) wet[i] * in[i - lag];
			c[lag] = a;
			if (a > best) { best = a; bl = lag; }
		}
		double frac = 0;
		if (bl > 100 && bl < 799)
		{
			const double y0 = c[bl - 1], y1 = c[bl], y2 = c[bl + 1];
			const double den = y0 - 2 * y1 + y2;
			if (den != 0) frac = 0.5 * (y0 - y2) / den;
		}
		d.push_back ((bl + frac) * 1000.0 / FS);
	}
	*tmin = 1e9; *tmax = -1e9;
	for (double v : d) { if (v < *tmin) *tmin = v; if (v > *tmax) *tmax = v; }
	return d;
}

int main (void)
{
	const unsigned N = (unsigned) (FS * 9.0f);		// 9 s: two periods of the 0.25 Hz LFO
	std::vector<float> nz (N), zero (N, 0.0f), oL, oR;
	for (unsigned i = 0; i < N; i++) nz[i] = 0.3f * Noise ();

	{	// the controls' definition must allow all six modes
		const TFxDef *d = FxDefByType (FxDimD);
		CHECKF (d && d->nParams == 2 && d->pParams[0].Max == 5.0f && d->pParams[1].Max == 1.0f && d->pParams[0].Def == 2.0f, "controls: Mode 0-%g (6 modes), Input 0-%g", d ? d->pParams[0].Max : -1.0f, d ? d->pParams[1].Max : -1.0f);
	}
	// delay per mode: input on the left only (Stereo), L's wet comes out on the right (half the amplitude, no dry on R)
	const double want[3][2] = {{8, 12}, {5, 10}, {6, 9}};
	const double kFilt = 0.25;		// group delay of the wet path's filters at low frequency (about 0.25 ms: Sallen-Key 0.09 + RC ladder 0.16)
	for (int m = 0; m < 3; m++)
	{
		RunFx ((float) (m * 2), 0.0f, nz, zero, oL, oR);
		double a, b;
		Delays (nz, oR, &a, &b);
		CHECKF (a > want[m][0] + kFilt - 0.5 && b < want[m][1] + kFilt + 0.5 && (b - a) > 0.7 * (want[m][1] - want[m][0]), "mode %d: measured delay %.2f-%.2f ms (expected %g-%g + about %.2f of filters)", m + 1, a, b, want[m][0], want[m][1], kFilt);
	}

	// LFO in antiphase: delay of the L->R path and of the R->L one (input on the right only): when one rises the other falls
	{
		std::vector<float> oL2, oR2;
		RunFx (0.0f, 0.0f, nz, zero, oL, oR);
		RunFx (0.0f, 0.0f, zero, nz, oL2, oR2);
		double a, b, c, d;
		std::vector<double> dl = Delays (nz, oR, &a, &b), dr = Delays (nz, oL2, &c, &d);
		double ma = 0, mb = 0, cov = 0, va = 0, vb = 0;
		for (size_t i = 0; i < dl.size (); i++) { ma += dl[i]; mb += dr[i]; }
		ma /= dl.size (); mb /= dl.size ();
		for (size_t i = 0; i < dl.size (); i++) { cov += (dl[i] - ma) * (dr[i] - mb); va += (dl[i] - ma) * (dl[i] - ma); vb += (dr[i] - mb) * (dr[i] - mb); }
		const double rho = cov / sqrt (va * vb);
		CHECKF (rho < -0.9, "the two channels' LFOs in antiphase (delay correlation %.3f)", rho);
	}

	// crossing: with input on the left only the main wet comes out on the right; on the left there is the dry plus the small same-side share
	{
		RunFx (4.0f, 0.0f, nz, zero, oL, oR);		// mode 3, low share
		std::vector<float> l3 (N), l34;
		double eR = 0, eLw = 0;
		for (unsigned i = N / 2; i < N; i++) { eR += (double) oR[i] * oR[i]; const double w = oL[i] - 0.5 * nz[i]; eLw += w * w; }
		const double dbSide = 10 * log10 (eLw / eR);
		RunFx (5.0f, 0.0f, nz, zero, oL, oR);		// mode 3+4, high share
		double eR4 = 0, eLw4 = 0;
		for (unsigned i = N / 2; i < N; i++) { eR4 += (double) oR[i] * oR[i]; const double w = oL[i] - 0.5 * nz[i]; eLw4 += w * w; }
		const double dbSide4 = 10 * log10 (eLw4 / eR4);
		CHECKF (dbSide < -10.0, "mode 3: same-side wet %.1f dB below the crossed one", -dbSide);
		CHECKF (dbSide4 > dbSide + 8.0, "mode 3+4: same-side share higher by %.1f dB (from %.1f to %.1f dB)", dbSide4 - dbSide, dbSide, dbSide4);
	}

	// frequency response of the wet path (input L, output R, amplitude / 0.5)
	{
		double prev = 0;
		printf ("wet response (mode 3):");
		double g1k = 0, g10k = 0;
		const float fr[] = {50, 100, 200, 500, 1000, 2000, 3000, 5000, 8000, 12000};
		for (float f : fr)
		{
			std::vector<float> s (N), sL;
			for (unsigned i = 0; i < N; i++) s[i] = 0.3f * sinf (6.2831853f * f * (float) i / FS);
			RunFx (4.0f, 0.0f, s, zero, oL, oR);
			double e = 0, e0 = 0;
			for (unsigned i = N / 2; i < N; i++) { e += (double) oR[i] * oR[i]; e0 += (double) s[i] * s[i]; }
			const double g = 20 * log10 (sqrt (e / e0) / 0.5);
			printf (" %g Hz %.1f dB;", f, g);
			if (f == 1000) g1k = g;
			if (f == 12000) g10k = g;
			prev = g;
		}
		(void) prev;
		printf ("\n");
		CHECKF (g10k < g1k - 20.0, "wet strongly damped at the top (1 kHz %.1f dB, 12 kHz %.1f dB)", g1k, g10k);
	}

	// stability and calculation time
	{
		RunFx (5.0f, 1.0f, nz, nz, oL, oR);
		bool ok = true; float mx = 0;
		for (unsigned i = 0; i < N; i++) { if (!std::isfinite (oL[i]) || !std::isfinite (oR[i])) ok = false; mx = std::max (mx, std::max (fabsf (oL[i]), fabsf (oR[i]))); }
		CHECKF (ok && mx < 1.0f, "stable with noise at 0.3: peak %.3f", mx);
		CFx *fx = FxCreate (FxDimD, FS);
		float P[FX_MAX_P] = {5, 0, 0, 0, 0, 0, 0, 0};
		float l[4] = {0.1f, 0.2f, 0.1f, -0.1f}, r[4] = {0.1f, -0.2f, 0.1f, 0.1f};
		int mono = 0;
		const auto t0 = std::chrono::steady_clock::now ();
		const unsigned B = 200000;
		for (unsigned b = 0; b < B; b++) { fx->Process (l, r, 4, &mono, P); l[0] = 0.1f; l[1] = 0.2f; l[2] = 0.1f; l[3] = -0.1f; }
		const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / B;
		printf ("time: %.3f us per 4-sample stereo block\n", us);
		delete fx;
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
