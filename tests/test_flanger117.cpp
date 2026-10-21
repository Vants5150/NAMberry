// test_flanger117.cpp - MXR 117 Flanger Real: filters against the nodal solution of the schematic, LFO range, clock, comb at the expected delay, regeneration, limiter, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_flanger117.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_flanger117
#include <cstdio>
#include <cmath>
#include <complex>
#include <chrono>
#include <vector>
#include "fx.h"

void Flanger117ForceForTest (CFx *pFx, double fclk);
void Flanger117FilterForTest (CFx *pFx, int which, const float *pIn, float *pOut, unsigned n);
double Flanger117LfoForTest (double speed, double *pLo, double *pHi);
double Flanger117ClockForTest (double manual, double wd, double tri);

typedef std::complex<double> Cx;
static int fails = 0;
#define CHECK(c, ...) do { char b_[300]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0, VFS = 1.305;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// U1A Rauch low-pass with C7 solved by nodal analysis (ideal op-amp: the inverting node is at 0 V)
static Cx RauchNodal (double f)
{
	const double R4 = 30e3, R6 = 30e3, R5 = 5.1e3, C2 = 6.8e-9, C22 = 150e-12, C7 = 0.1e-6;
	const Cx s (0.0, 2 * M_PI * f);
	const Cx Y1 = 1.0 / (R4 + 1.0 / (s * C7)), Y2 = 1.0 / R6, Y3 = 1.0 / R5;
	// unknowns N, Vo: (Vin - N) Y1 = N (s C2 + Y2 + Y3) - Vo Y2 ; N Y3 = -Vo s C22   (Vin = 1)
	// N = -Vo s C22 / Y3 -> Y1 = N (Y1 + s C2 + Y2 + Y3) - Vo Y2 = Vo (-(s C22 / Y3) (Y1 + s C2 + Y2 + Y3) - Y2)
	const Cx den = -(s * C22 / Y3) * (Y1 + s * C2 + Y2 + Y3) - Y2;
	return Y1 / den;
}

static Cx SkNodal (double f)
{
	const double R7 = 7.5e3, R3 = 2.4e3, C3 = 6.8e-9, C4 = 3.3e-9;
	const Cx s (0.0, 2 * M_PI * f);
	return 1.0 / (1.0 + s * C4 * (R7 + R3) + s * s * R7 * R3 * C3 * C4);
}

static Cx PreNodal (double f)
{
	const Cx s (0.0, 2 * M_PI * f);
	return 1.0 + 10e3 / (1e3 + 1.0 / (s * 0.033e-6));
}

static Cx MixNodal (double f, double rin)
{
	const Cx s (0.0, 2 * M_PI * f);
	const Cx zf = 1.0 / (1.0 / 100e3 + 1.0 / (10e3 + 1.0 / (s * 3.3e-9)));
	return -zf / rin;
}

static void FilterResponse (int which, double f, double *mag, double *ph)
{
	CFx *fx = FxCreate (FxFlanger, (float) FS);
	const unsigned N = (unsigned) (FS * (f < 100 ? 4.0 : 1.5)) / 4 * 4;
	std::vector<float> in (N), out (N);
	for (unsigned i = 0; i < N; i++) in[i] = (float) sin (2 * M_PI * f * i / FS);
	Flanger117FilterForTest (fx, which, in.data (), out.data (), N);
	const int cyc = (int) ((N / 2) * f / FS);
	const unsigned M = cyc > 0 ? (unsigned) (cyc * FS / f + 0.5) : N / 2;
	double sn = 0, cs = 0;
	for (unsigned i = N - M; i < N; i++) { const double th = 2 * M_PI * f * i / FS; sn += out[i] * sin (th); cs += out[i] * cos (th); }
	*mag = 2.0 * sqrt (sn * sn + cs * cs) / M;
	*ph = atan2 (cs, sn);
	delete fx;
}

static void RunBlocks (CFx *fx, const float *P, const std::vector<float> &in, std::vector<float> &out)
{
	out.assign (in.size (), 0.0f);
	for (size_t b = 0; b < in.size () / 4; b++)
	{
		float L[4], R[4];
		for (int j = 0; j < 4; j++) L[j] = R[j] = in[b * 4 + j];
		int m = 1;
		fx->Process (L, R, 4, &m, P);
		for (int j = 0; j < 4; j++) out[b * 4 + j] = L[j];
	}
}

static double Goertzel (const std::vector<float> &v, double f, unsigned from, unsigned to)
{
	double re = 0, im = 0;
	for (unsigned i = from; i < to; i++) { const double w = 2 * M_PI * f * i / FS; re += v[i] * cos (w); im += v[i] * sin (w); }
	return 2.0 * sqrt (re * re + im * im) / (to - from);
}

int main (void)
{
	// 1. filters against the nodal solution
	{
		const struct { int which; const char *name; } fl[5] = {{0, "U4A pre-emphasis"}, {1, "U1A Rauch low-pass (with C7)"}, {2, "U1B Sallen-Key"}, {3, "mixer, dry input"}, {4, "mixer, wet input"}};
		for (int k = 0; k < 5; k++)
		{
			double wm = 0, wp = 0;
			for (double f : {40.0, 150.0, 500.0, 1500.0, 4000.0})
			{
				double mag, ph;
				FilterResponse (fl[k].which, f, &mag, &ph);
				Cx h;
				if (k == 0) h = PreNodal (f); else if (k == 1) h = RauchNodal (f); else if (k == 2) h = SkNodal (f); else h = MixNodal (f, k == 3 ? 150e3 : 82e3);
				double dp = (ph - std::arg (h)) * 180 / M_PI;
				while (dp > 180) dp -= 360;
				while (dp < -180) dp += 360;
				const double dm = Db (mag) - Db (std::abs (h));
				if (fabs (dm) > fabs (wm) && std::abs (h) > 0.01) wm = dm;
				if (fabs (dp) > fabs (wp) && std::abs (h) > 0.01) wp = dp;
			}
			printf ("%s: worst difference %.3f dB, %.2f degrees\n", fl[k].name, wm, wp);
			CHECK (fabs (wm) < 0.4 && fabs (wp) < 6.0, "%s matches the nodal solution", fl[k].name);
		}
		double mag, ph;
		FilterResponse (1, 1000.0, &mag, &ph);
		CHECK (fabs (Db (mag) - 0.0) < 1.5, "Rauch low-pass: gain close to -1 in the midband (%.2f dB)", Db (mag));
	}

	// 2. LFO: range of the triangle and of the rate (reissue: 0.1 - 10 Hz)
	{
		double lo, hi;
		const double fmax = Flanger117LfoForTest (10.0, &lo, &hi);
		printf ("LFO at Speed 10: %.2f Hz, triangle from %.2f to %.2f V\n", fmax, lo, hi);
		CHECK (fmax > 8.0 && fmax < 11.0, "maximum LFO rate %.2f Hz (reissue spec 10 Hz)", fmax);
		CHECK (lo > 3.5 && lo < 4.1 && hi > 12.0 && hi < 12.8, "triangle between %.2f and %.2f V (Schmitt thresholds with R9 / R23)", lo, hi);
		const double fmin = Flanger117LfoForTest (0.0, &lo, &hi);
		printf ("LFO at Speed 0: %.3f Hz\n", fmin);
		CHECK (fmin > 0.07 && fmin < 0.12, "minimum LFO rate %.3f Hz (reissue spec 0.1 Hz)", fmin);
	}

	// 3. clock: 30 kHz and 960 kHz at the ends of MANUAL, delay = 512 / fclk
	{
		const double a = Flanger117ClockForTest (0.0, 0.0, 8.0), b = Flanger117ClockForTest (10.0, 0.0, 8.0);
		printf ("Manual 0: %.1f kHz (delay %.2f ms), Manual 10: %.1f kHz (delay %.2f ms)\n", a / 1e3, 512.0 / a * 1e3, b / 1e3, 512.0 / b * 1e3);
		CHECK (fabs (a - 30e3) < 500.0 && fabs (b - 960e3) < 15e3, "clock from 30 kHz to 960 kHz (measured on real units)");
		const double c = Flanger117ClockForTest (5.0, 1.0, 12.4), c0 = Flanger117ClockForTest (5.0, 1.0, 3.8), c5 = Flanger117ClockForTest (5.0, 0.0, 8.0);
		CHECK (c > c5 && c0 < c5, "Width moves the clock around the Manual value (%.0f / %.0f / %.0f kHz)", c0 / 1e3, c5 / 1e3, c / 1e3);
	}

	// 4. comb: Width 0, Manual 10 (clock forced to 960 kHz: delay 0.533 ms): notch near 1 / (2 d) = 938 Hz, peak near 1876 Hz
	{
		CFx *fx = FxCreate (FxFlanger, (float) FS);
		Flanger117ForceForTest (fx, 960.0e3);
		float P[4] = {0.0f, 0.0f, 10.0f, 0.0f};
		// white noise, response by averaging the spectrum
		std::vector<float> in (96000), out;
		unsigned seed = 5;
		for (auto &x : in) { seed = seed * 1664525u + 1013904223u; x = (float) (((int) (seed >> 8) / 8388608.0 - 1.0) * 0.01); }
		RunBlocks (fx, P, in, out);
		double bestF = 0, bestA = 1e9, peakF = 0, peakA = 0;
		for (double f = 300; f < 3000; f += 20)
		{
			const double a = Goertzel (out, f, 24000, 96000) / (Goertzel (in, f, 24000, 96000) + 1e-12);
			if (f < 1400 && a < bestA) { bestA = a; bestF = f; }
			if (f > 1400 && f < 2500 && a > peakA) { peakA = a; peakF = f; }
		}
		printf ("comb at 0.533 ms: notch near %.0f Hz (%.1f dB), peak near %.0f Hz (%.1f dB)\n", bestF, Db (bestA), peakF, Db (peakA));
		CHECK (bestF > 700 && bestF < 1100, "first notch at %.0f Hz (1 / (2 d) = 938 Hz, shifted by the filters' delay)", bestF);
		CHECK (peakF > 1500 && peakF < 2300 && peakA > 1.5 * bestA, "peak at %.0f Hz above the notch", peakF);
		delete fx;
	}

	// 5. regeneration: more Regen, higher peaks
	{
		double pk[2];
		for (int r = 0; r < 2; r++)
		{
			CFx *fx = FxCreate (FxFlanger, (float) FS);
			Flanger117ForceForTest (fx, 960.0e3);
			float P[4] = {0.0f, 0.0f, 10.0f, r ? 10.0f : 0.0f};
			std::vector<float> in (96000), out;
			unsigned seed = 5;
			for (auto &x : in) { seed = seed * 1664525u + 1013904223u; x = (float) (((int) (seed >> 8) / 8388608.0 - 1.0) * 0.01); }
			RunBlocks (fx, P, in, out);
			double m = 0;
			for (double f = 300; f < 6000; f += 20) { const double a = Goertzel (out, f, 24000, 96000) / (Goertzel (in, f, 24000, 96000) + 1e-12); if (a > m) m = a; }
			pk[r] = m;
			delete fx;
		}
		printf ("highest peak: Regen 0 %.2f dB, Regen 10 %.2f dB\n", Db (pk[0]), Db (pk[1]));
		CHECK (pk[1] > pk[0] * 1.05, "Regen raises the peaks (%.1f dB; modest because TRIM LEVEL is assumed to equalize wet and dry)", Db (pk[1] / pk[0]));
	}

	// 6. limiter: the gain falls for large signals (U2A with the diodes)
	{
		double g[2];
		const double lv[2] = {-40.0, -6.0};
		for (int k = 0; k < 2; k++)
		{
			CFx *fx = FxCreate (FxFlanger, (float) FS);
			Flanger117ForceForTest (fx, 960.0e3);
			float P[4] = {0.0f, 0.0f, 10.0f, 0.0f};
			std::vector<float> in (48000), out;
			const double a = pow (10.0, lv[k] / 20.0);
			for (size_t i = 0; i < in.size (); i++) in[i] = (float) (a * sin (2 * M_PI * 300.0 * i / FS));
			RunBlocks (fx, P, in, out);
			g[k] = Goertzel (out, 300.0, 24000, 48000) / a;
			delete fx;
		}
		printf ("gain at 300 Hz: -40 dBFS %.2f dB, -6 dBFS %.2f dB\n", Db (g[0]), Db (g[1]));
		CHECK (g[1] < g[0] * 0.9, "the limiter lowers the gain for large signals (%.1f dB)", Db (g[1] / g[0]));
	}

	// 7. stability and cost
	{
		bool ok = true;
		float pk = 0;
		for (int k = 0; k < 8; k++)
		{
			CFx *fx = FxCreate (FxFlanger, (float) FS);
			float P[4] = {(float) ((k & 1) ? 10 : 0), (float) ((k & 2) ? 10 : 0), (float) ((k & 4) ? 10 : 0), 10.0f};
			unsigned seed = 11 + k;
			for (unsigned b = 0; b < 144000 / 4; b++)
			{
				float L[4], R[4];
				for (int j = 0; j < 4; j++) { seed = seed * 1664525u + 1013904223u; L[j] = R[j] = (float) (((int) (seed >> 8) / 8388608.0 - 1.0) * 0.9); }
				int m = 1;
				fx->Process (L, R, 4, &m, P);
				for (int j = 0; j < 4; j++) { if (!(fabsf (L[j]) < 20.0f)) ok = false; if (fabsf (L[j]) > pk) pk = fabsf (L[j]); }
			}
			delete fx;
		}
		CHECK (ok, "stable with full-scale noise, Regen at maximum and every combination of Speed / Width / Manual (peak %.2f)", pk);
		CFx *fx = FxCreate (FxFlanger, (float) FS);
		float P[4] = {4.0f, 7.0f, 3.0f, 4.0f};
		const auto c0 = std::chrono::steady_clock::now ();
		for (unsigned b = 0; b < 100000; b++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++) L[j] = R[j] = 0.3f * sinf ((float) (b * 4 + j) * 0.0575f);
			int m = 0;
			fx->Process (L, R, 4, &m, P);
		}
		const double us = std::chrono::duration<double> (std::chrono::steady_clock::now () - c0).count () * 1e6 / 100000.0;
		printf ("time: %.3f us per block of 4 frames\n", us);
		delete fx;
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
