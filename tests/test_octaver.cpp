// test_octaver.cpp - Octaver Real (Boss OC-2): every filter against the nodal solution of the schematic, octave tracking at several notes and levels, minimum operating level, direct path, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_octaver.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_octaver
#include <cstdio>
#include <cmath>
#include <complex>
#include <chrono>
#include <vector>
#include "fx.h"

void OctaverFilterForTest (CFx *pFx, int which, const float *pIn, float *pOut, unsigned n);

typedef std::complex<double> Cx;
static int fails = 0;
#define CHECK(c, ...) do { char b_[300]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0, VFS = 1.305;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

// Sallen-Key chain solved by nodal analysis: Vin -Ra- n1 (Ca to ground) -Rb- A (Cfb to the output B) -Rc- B (Cg to ground), unity-gain follower
static Cx SkNodal (double f, double Ra, double Ca, double Rb, double Cfb, double Rc, double Cg)
{
	const Cx s (0.0, 2 * M_PI * f);
	// unknowns n1, A, B: 3x3
	Cx M[3][4] = {
		{1.0 / Ra + s * Ca + 1.0 / Rb, -1.0 / Rb, 0.0, 1.0 / Ra},
		{-1.0 / Rb, 1.0 / Rb + s * Cfb + 1.0 / Rc, -s * Cfb - 1.0 / Rc, 0.0},
		{0.0, -1.0 / Rc, 1.0 / Rc + s * Cg, 0.0}};
	// the follower forces V(out) = B, so Cfb connects A to B: already in the matrix
	for (int c = 0; c < 3; c++)
	{
		int pr = c;
		for (int r = c + 1; r < 3; r++) if (std::abs (M[r][c]) > std::abs (M[pr][c])) pr = r;
		for (int q = 0; q < 4; q++) std::swap (M[c][q], M[pr][q]);
		for (int r = c + 1; r < 3; r++)
		{
			const Cx f2 = M[r][c] / M[c][c];
			for (int q = c; q < 4; q++) M[r][q] -= f2 * M[c][q];
		}
	}
	Cx x[3];
	for (int r = 2; r >= 0; r--)
	{
		Cx sum = M[r][3];
		for (int q = r + 1; q < 3; q++) sum -= M[r][q] * x[q];
		x[r] = sum / M[r][r];
	}
	return x[2];
}

// m1 and m2: Vsk -R10- m1 (C20 to ground) -R32- m2 (C18 + R31 to ground)
static void M12Nodal (double f, Cx *m1, Cx *m2)
{
	const double R10 = 33e3, R32 = 33e3, R31 = 68e3, C18 = 0.01e-6, C20 = 330e-12;
	const Cx s (0.0, 2 * M_PI * f);
	const Cx Ys = 1.0 / (R31 + 1.0 / (s * C18));
	// m1: (1 - m1) / R10 = s C20 m1 + (m1 - m2) / R32 ; m2: (m1 - m2) / R32 = m2 Ys
	const Cx a11 = 1.0 / R10 + s * C20 + 1.0 / R32, a12 = -1.0 / R32, b1 = 1.0 / R10;
	const Cx a21 = -1.0 / R32, a22 = 1.0 / R32 + Ys;
	const Cx det = a11 * a22 - a12 * a21;
	*m1 = (b1 * a22) / det;
	*m2 = (-a21 * b1) / det;
}

static void FilterResponse (int which, double f, double *mag, double *ph)
{
	CFx *fx = FxCreate (FxOctaver, (float) FS);
	const unsigned N = (unsigned) (FS * (f < 100 ? 4.0 : 1.5)) / 4 * 4;
	std::vector<float> in (N), out (N);
	for (unsigned i = 0; i < N; i++) in[i] = (float) sin (2 * M_PI * f * i / FS);
	OctaverFilterForTest (fx, which, in.data (), out.data (), N);
	const int cyc = (int) ((N / 2) * f / FS);
	const unsigned M = cyc > 0 ? (unsigned) (cyc * FS / f + 0.5) : N / 2;
	double sn = 0, cs = 0;
	for (unsigned i = N - M; i < N; i++) { const double th = 2 * M_PI * f * i / FS; sn += out[i] * sin (th); cs += out[i] * cos (th); }
	*mag = 2.0 * sqrt (sn * sn + cs * cs) / M;
	*ph = atan2 (cs, sn);
	delete fx;
}

static double Goertzel (const std::vector<float> &v, double f, unsigned from, unsigned to)
{
	double re = 0, im = 0;
	for (unsigned i = from; i < to; i++) { const double w = 2 * M_PI * f * i / FS; re += v[i] * cos (w); im += v[i] * sin (w); }
	return 2.0 * sqrt (re * re + im * im) / (to - from);
}

// runs the effect on a sine of level dbfs and frequency f for sec seconds
static void RunSine (CFx *fx, const float *P, double f, double dbfs, double sec, std::vector<float> &o)
{
	const unsigned N = (unsigned) (sec * FS) / 4 * 4;
	o.assign (N, 0.0f);
	const double a = pow (10.0, dbfs / 20.0);
	for (unsigned b = 0; b < N / 4; b++)
	{
		float L[4], R[4];
		for (int j = 0; j < 4; j++) L[j] = R[j] = (float) (a * sin (2 * M_PI * f * (b * 4 + j) / FS));
		int m = 1;
		fx->Process (L, R, 4, &m, P);
		for (int j = 0; j < 4; j++) o[b * 4 + j] = L[j];
	}
}

int main (void)
{
	// 1. filters against the nodal solution
	{
		const struct { int which; const char *name; double Ra, Ca, Rb, Cfb, Rc, Cg; } sk[3] = {
			{0, "tracking chain (R8 C21 R9 C5 R5 C4)", 22e3, 0.01e-6, 330e3, 2.2e-9, 330e3, 220e-12},
			{3, "octave 1 filter (R42 C28 R41 C26 R40 C27)", 22e3, 0.022e-6, 330e3, 4.7e-9, 330e3, 470e-12},
			{4, "octave 2 filter (R56 C32 R57 C31 R58 C30)", 22e3, 0.047e-6, 330e3, 10e-9, 330e3, 1e-9}};
		for (int k = 0; k < 3; k++)
		{
			double wm = 0, wp = 0;
			for (double f : {40.0, 100.0, 250.0, 600.0, 1500.0, 5000.0})
			{
				double mag, ph;
				FilterResponse (sk[k].which, f, &mag, &ph);
				const Cx h = SkNodal (f, sk[k].Ra, sk[k].Ca, sk[k].Rb, sk[k].Cfb, sk[k].Rc, sk[k].Cg);
				double dp = (ph - std::arg (h)) * 180 / M_PI;
				while (dp > 180) dp -= 360;
				while (dp < -180) dp += 360;
				const double dm = Db (mag) - Db (std::abs (h));
				if (fabs (dm) > fabs (wm) && std::abs (h) > 0.01) wm = dm;
				if (fabs (dp) > fabs (wp) && std::abs (h) > 1e-3) wp = dp;
			}
			printf ("%s: worst difference %.3f dB, %.2f degrees\n", sk[k].name, wm, wp);
			CHECK (fabs (wm) < 0.3 && fabs (wp) < 4.0, "%s matches the nodal solution", sk[k].name);
		}
		double wm1 = 0, wm2 = 0;
		for (double f : {40.0, 100.0, 250.0, 600.0, 1500.0, 5000.0})
		{
			double a1, p1, a2, p2;
			FilterResponse (1, f, &a1, &p1);
			FilterResponse (2, f, &a2, &p2);
			Cx h1, h2;
			M12Nodal (f, &h1, &h2);
			if (fabs (Db (a1) - Db (std::abs (h1))) > fabs (wm1)) wm1 = Db (a1) - Db (std::abs (h1));
			if (fabs (Db (a2) - Db (std::abs (h2))) > fabs (wm2)) wm2 = Db (a2) - Db (std::abs (h2));
		}
		printf ("comparator nodes m1 / m2: worst difference %.3f / %.3f dB\n", wm1, wm2);
		CHECK (fabs (wm1) < 0.3 && fabs (wm2) < 0.3, "m1 and m2 match the nodal solution");
		// IC1 gain stage: 1 + R7 / (R6 + 1 / (s C3))
		double wg = 0;
		for (double f : {5.0, 20.0, 100.0, 1000.0, 8000.0})
		{
			double mag, ph;
			FilterResponse (5, f, &mag, &ph);
			const Cx s (0.0, 2 * M_PI * f);
			const double a = std::abs (1.0 + 10e3 / (2.7e3 + 1.0 / (s * 10e-6)));
			if (fabs (Db (mag) - Db (a)) > fabs (wg)) wg = Db (mag) - Db (a);
		}
		CHECK (fabs (wg) < 0.2, "IC1 gain stage matches 1 + R7 / (R6 + 1 / s C3) (%.3f dB; 4.70 above 5.9 Hz)", wg);
	}

	// 2. octave tracking: only the octave 1 (and then only the octave 2) knob up; guitar notes at two levels
	const struct { const char *n; double f; } notes[4] = {{"E2", 82.41}, {"A2", 110.0}, {"G3", 196.0}, {"E4", 329.63}};
	for (int oc = 1; oc <= 2; oc++)
	{
		for (int lv = 0; lv < 2; lv++)
		{
			const double dbfs = lv ? -12.0 : -30.0;
			int good = 0;
			for (int k = 0; k < 4; k++)
			{
				CFx *fx = FxCreate (FxOctaver, (float) FS);
				float P[4] = {oc == 1 ? 100.0f : 0.0f, 0.0f, 0.0f, oc == 2 ? 100.0f : 0.0f};
				std::vector<float> o;
				RunSine (fx, P, notes[k].f, dbfs, 3.0, o);
				const unsigned from = o.size () / 2, to = o.size () / 4 * 4;
				const double fo = notes[k].f / (oc == 1 ? 2.0 : 4.0);
				const double a_oct = Goertzel (o, fo, from, to), a_f = Goertzel (o, notes[k].f, from, to), a_h = Goertzel (o, fo * 2.0, from, to);
				printf ("octave %d, %s %.1f Hz at %.0f dBFS: %.1f Hz %.4f, f %.4f, 2x %.4f (ratio %.1f dB)\n", oc, notes[k].n, notes[k].f, dbfs, fo, a_oct, a_f, a_h, Db (a_oct / (a_f + a_h + 1e-12)));
				if (a_oct > 2.0 * (a_f + a_h)) good++;
				delete fx;
			}
			CHECK (good >= 3, "octave %d tracks %d of 4 notes at %.0f dBFS", oc, good, dbfs);
		}
	}

	// 3. minimum operating level: -60 dBm at 250 Hz (0.775 mV rms, -61.5 dBFS here)
	{
		CFx *fx = FxCreate (FxOctaver, (float) FS);
		float P[4] = {100.0f, 0.0f, 0.0f, 0.0f};
		std::vector<float> o;
		RunSine (fx, P, 250.0, -61.5, 3.0, o);
		const unsigned from = o.size () / 2, to = o.size () / 4 * 4;
		const double a1 = Goertzel (o, 125.0, from, to), a0 = Goertzel (o, 250.0, from, to);
		printf ("-60 dBm at 250 Hz: octave %.5f, fundamental %.5f\n", a1, a0);
		CHECK (a1 > 2.0 * a0, "tracks the published minimum level (-60 dBm at 250 Hz)");
		delete fx;
	}

	// 4. direct path: gain at 1 kHz against the analytic chain (Q1, IC1 stage, mixer with the other two knobs at zero, output follower)
	{
		CFx *fx = FxCreate (FxOctaver, (float) FS);
		float P[4] = {0.0f, 100.0f, 0.0f, 0.0f};
		std::vector<float> o;
		RunSine (fx, P, 1000.0, -50.0, 1.0, o);
		const unsigned from = o.size () / 2, to = o.size () / 4 * 4;
		const double g = Goertzel (o, 1000.0, from, to) / pow (10.0, -50.0 / 20.0);
		const double Rs = 22e3, Gl = 1e-6;
		const double rth1 = 1.0, gsum = Gl + 1.0 / (rth1 + Rs) + 2.0 / (1.0 + Rs);
		const double mixf = (1.0 / (rth1 + Rs)) / gsum;
		const Cx s (0.0, 2 * M_PI * 1000.0);
		const double ref = 0.92 * std::abs (1.0 + 10e3 / (2.7e3 + 1.0 / (s * 10e-6))) * mixf * 0.97;
		printf ("direct at 1 kHz: model %.3f (%.2f dB), analytic %.3f (%.2f dB)\n", g, Db (g), ref, Db (ref));
		CHECK (fabs (Db (g) - Db (ref)) < 0.3, "direct path within 0.3 dB of the analytic chain");
		delete fx;
	}

	// 5. stability: noise and chords at full scale and at guitar level, all knobs at the extremes
	{
		bool ok = true;
		float pk = 0;
		for (int lv = 0; lv < 2; lv++) for (int k = 0; k < 4; k++)
		{
			CFx *fx = FxCreate (FxOctaver, (float) FS);
			float P[4] = {(float) ((k & 1) ? 100 : 0), (float) ((k & 2) ? 100 : 20), 12.0f, (float) ((k & 1) ? 100 : 50)};
			unsigned seed = 3;
			for (unsigned b = 0; b < 100000 / 4; b++)
			{
				float L[4], R[4];
				for (int j = 0; j < 4; j++)
				{
					seed = seed * 1664525u + 1013904223u;
					const double t = (b * 4 + j) / FS, nz = ((int) (seed >> 8) / 8388608.0 - 1.0);
					const double x = lv ? (0.9 * nz) : (0.2 * (sin (2 * M_PI * 82.4 * t) + sin (2 * M_PI * 123.5 * t) + sin (2 * M_PI * 164.8 * t)) + 0.01 * nz);
					L[j] = R[j] = (float) x;
				}
				int m = 1;
				fx->Process (L, R, 4, &m, P);
				for (int j = 0; j < 4; j++) { if (!(fabsf (L[j]) < 10.0f)) ok = false; if (fabsf (L[j]) > pk) pk = fabsf (L[j]); }
			}
			delete fx;
		}
		CHECK (ok, "stable with noise and chords, knobs at the extremes (peak %.2f of full scale)", pk);
	}

	// 6. cost
	{
		CFx *fx = FxCreate (FxOctaver, (float) FS);
		float P[4] = {70.0f, 80.0f, 0.0f, 40.0f};
		const auto c0 = std::chrono::steady_clock::now ();
		for (unsigned b = 0; b < 200000; b++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++) L[j] = R[j] = 0.3f * sinf ((float) (b * 4 + j) * 0.0575f);
			int m = 0;
			fx->Process (L, R, 4, &m, P);
		}
		const double us = std::chrono::duration<double> (std::chrono::steady_clock::now () - c0).count () * 1e6 / 200000.0;
		printf ("time: %.3f us per block of 4 frames\n", us);
		delete fx;
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
