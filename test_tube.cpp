// test_tube.cpp - tests of the Tube Screamer (fx_tube.cpp) on the PC: frequency response compared with the EXACT transfer function of the circuit (computed here with complex numbers),
// behaviour of the controls and models, clipping, stability.
// g++ -std=c++17 -O1 -Ihost_shim -I../../../../include -I. -Iadel test_tube.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_tube
#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)

typedef std::complex<double> Cx;

// ---- circuit (same values as the file) ----
static const double R4 = 4.7e3, C4 = 51e-12, Rf = 51e3, PotD = 500e3, R9 = 1e3, C5 = 0.22e-6, Rb = 10e3, R10 = 1e3, R7 = 220, C6 = 0.22e-6, PotT = 20e3;
static const double C1 = 0.02e-6, Rin = 446e3, C2 = 1e-6, R5 = 10e3, C9 = 10e-6, Rload = 1e6;
static double Taper (double x, double a) { return (exp (a * x) - 1.0) / (exp (a) - 1.0); }
static double TonePos (double x) { double q = Taper (1.0 - x, 3.0); if (x > 0.8) q *= (1.0 - x) * 5.0; return 1.0 - q; }

// response (magnitude, dB) of the analog circuit at small signal: diodes off
static double Analog (double f, double drive10, double tone10, double level10, double C3, double RB, double RC)
{
	const Cx s (0.0, 2.0 * M_PI * f);
	const Cx Hbuf = (s * C1 * Rin) / (1.0 + s * C1 * Rin);
	const Cx Hcc = (s * C2 * R5) / (1.0 + s * C2 * R5);
	const double Rtot = Rf + PotD * Taper (drive10 / 10.0, 3.47);
	const Cx Zf = Rtot / (1.0 + s * Rtot * C4);
	const Cx Zg = R4 + 1.0 / (s * C3);
	const Cx Hclip = 1.0 + Zf / Zg;
	const double p = TonePos (tone10 / 10.0);
	const double Rxw = std::max (p * PotT, 1.0), Rwy = std::max ((1.0 - p) * PotT, 1.0);
	const double g = 1.0 / Rxw + 1.0 / Rwy;
	const double a0 = 1.0 + R9 / Rb;
	const Cx D = g + s * C6 * (1.0 + g * R7);
	const Cx Htone = (D + s * R10 * C6 / Rwy) / ((a0 + s * R9 * C5) * D + s * R9 * C6 / Rxw);
	const double RshL = RC * Rload / (RC + Rload);
	const Cx Hout = (s * C9 * RshL) / (1.0 + s * C9 * RshL) * (RshL / (RB + RshL));
	return 20.0 * log10 (std::abs (Hbuf * Hcc * Hclip * Htone * (level10 / 10.0) * Hout)) + 20.0 * log10 (0.40);	// 0.40 = the block's fixed output attenuation (kOutTrim)
}

// measured response of the block with a small sine (peak 1e-5 of full scale = 13 uV: the diodes do not conduct)
static double Measure (CFx *fx, const float *P, double f, double amp)
{
	const int N = 48000 * 2;
	double sumIn = 0, sumOut = 0;
	float L[4], R[4];
	int mono = 1;
	for (int b = 0; b < N / 4; b++)
	{
		for (int j = 0; j < 4; j++) { L[j] = R[j] = (float) (amp * sin (2.0 * M_PI * f * (double) (b * 4 + j) / 48000.0)); }
		float in[4] = {L[0], L[1], L[2], L[3]};
		mono = 1; fx->Process (L, R, 4, &mono, P);
		if (b * 4 > 48000)
			for (int j = 0; j < 4; j++) { sumIn += (double) in[j] * in[j]; sumOut += (double) L[j] * L[j]; }
	}
	return 10.0 * log10 (sumOut / sumIn);
}

int main ()
{
	// 1. small-signal frequency response against the exact circuit
	struct Set { int model; float drive, tone, level; const char *name; };
	const Set sets[] = {{1, 0, 5, 10, "TS9, Drive 0, Tone 5"}, {1, 10, 5, 10, "TS9, Drive 10, Tone 5"}, {1, 5, 0, 10, "TS9, Drive 5, Tone 0"}, {1, 5, 10, 10, "TS9, Drive 5, Tone 10"}, {0, 5, 5, 10, "TS808, Drive 5, Tone 5"}, {4, 5, 5, 10, "TS9DX +"}, {5, 5, 5, 10, "TS9DX Hot"}};
	const double C3s[7] = {0.047e-6, 0.047e-6, 0.047e-6, 0.047e-6, 0.22e-6, 1.0e-6, 2.2e-6};
	for (const Set &S : sets)
	{
		CFx *fx = FxCreate (FxTubeScreamer, 48000.0f);
		const float P[4] = {(float) S.model, S.drive, S.tone, S.level};
		double maxErr = 0, errAt = 0;
		for (double f : {60.0, 120.0, 250.0, 500.0, 720.0, 1000.0, 1500.0, 2200.0, 3300.0, 4500.0})
		{
			const double a = Analog (f, S.drive, S.tone, S.level, C3s[S.model], S.model == 0 ? 100.0 : 470.0, S.model == 0 ? 10e3 : 100e3);
			const double m = Measure (fx, P, f, 2e-5);
			if (fabs (a - m) > maxErr) { maxErr = fabs (a - m); errAt = f; }
		}
		char msg[160];
		snprintf (msg, sizeof msg, "%s: small-signal response within 1.2 dB of the exact circuit (maximum error %.2f dB at %.0f Hz)", S.name, maxErr, errAt);
		CHECK (maxErr < 1.2, msg);
		delete fx;
	}

	// 2. numbers from the sources: hump at 721 Hz, gain from about 12 (21.5 dB) to 118 (41.4 dB) at high frequencies, Tone: +14.9 dB
	{
		const double gMin = 1.0 + Rf / R4, gMax = 1.0 + (Rf + PotD) / R4;
		printf ("         clipping stage's high-frequency gain: Drive 0 = %.2f (%.1f dB), Drive 10 = %.1f (%.1f dB)\n", gMin, 20 * log10 (gMin), gMax, 20 * log10 (gMax));
		CHECK (fabs (gMin - 11.85) < 0.05 && fabs (20 * log10 (gMax) - 41.4) < 0.2, "minimum and maximum gain as in the sources (about 12 and 118)");
		CHECK (fabs (1.0 / (2.0 * M_PI * R4 * 0.047e-6) - 720.5) < 1.0, "clipping stage high-pass at 720 Hz");
		CHECK (fabs (20 * log10 (1.0 + R10 / R7) - 14.9) < 0.1, "tone: +14.9 dB (1 + 1k/220) at maximum");
	}

	// 3. clipping: with a large signal the output is limited by the diodes (with Drive at maximum) and the Turbo mode (no diodes) is not
	{
		auto peak = [] (int model, float drive, double amp) {
			CFx *fx = FxCreate (FxTubeScreamer, 48000.0f);
			const float P[4] = {(float) model, drive, 5.0f, 10.0f};
			float L[4], R[4]; int mono = 1; double pk = 0;
			for (int b = 0; b < 48000 / 4; b++)
			{
				for (int j = 0; j < 4; j++) L[j] = R[j] = (float) (amp * sin (2.0 * M_PI * 1000.0 * (double) (b * 4 + j) / 48000.0));
				mono = 1; fx->Process (L, R, 4, &mono, P);
				if (b * 4 > 24000) for (int j = 0; j < 4; j++) pk = fmax (pk, fabs (L[j]));
			}
			delete fx;
			return pk;
		};
		const double p1 = peak (1, 10.0f, 0.05), p2 = peak (1, 10.0f, 0.5), pT1 = peak (6, 10.0f, 0.05), pT2 = peak (6, 10.0f, 0.5);
		printf ("         TS9 Drive 10: peak with input 0.05 = %.3f, with 0.5 = %.3f | Turbo: %.3f, %.3f\n", p1, p2, pT1, pT2);
		CHECK (p2 / p1 < 4.0, "TS9 Drive 10: input 10 times larger, output much less than 10 times (diode clipping)");
		CHECK (pT1 > 3.0 * p1, "Turbo (no diodes) at the same input comes out much louder than the TS9, which the diodes limit");
	}

	// 4. the controls: Level linear, Tone and Drive change the timbre in the right direction
	{
		CFx *fx = FxCreate (FxTubeScreamer, 48000.0f);
		const float P0[4] = {1, 5, 5, 0}, P5[4] = {1, 5, 5, 5}, P10[4] = {1, 5, 5, 10};
		const double l5 = Measure (fx, P5, 800.0, 2e-5), l10 = Measure (fx, P10, 800.0, 2e-5);
		CHECK (fabs ((l10 - l5) - 6.02) < 0.3, "Level linear: from half to maximum +6 dB");
		const double l0 = Measure (fx, P0, 800.0, 2e-5);
		printf ("         Level 0: %.1f dB relative to the input\n", l0);
		CHECK (l0 < -50.0, "Level at zero: silence (only the output capacitor's discharge remains, as on the pedal)");
		const float Td[4] = {1, 5, 0, 10}, Tb[4] = {1, 5, 10, 10};
		const double dk = Measure (fx, Td, 6000.0, 2e-5), br = Measure (fx, Tb, 6000.0, 2e-5);
		CHECK (br - dk > 12.0, " Tone: at 6 kHz the difference between fully bright and fully dark is over 12 dB");
		const float Dlo[4] = {1, 0, 5, 10}, Dhi[4] = {1, 10, 5, 10};
		CHECK (Measure (fx, Dhi, 3000.0, 2e-5) - Measure (fx, Dlo, 3000.0, 2e-5) > 15.0, "Drive: from 0 to 10 the gain at 3 kHz rises by over 15 dB");
		delete fx;
	}

	// 5. models: TS808, TS9 and TS10 almost identical (as the sources say), the TS9DX modes different in the bass
	{
		CFx *fx = FxCreate (FxTubeScreamer, 48000.0f);
		const float P808[4] = {0, 5, 5, 8}, P9[4] = {1, 5, 5, 8}, P10[4] = {2, 5, 5, 8}, Pplus[4] = {4, 5, 5, 8}, Pturbo[4] = {6, 5, 5, 8};
		double md = 0;
		for (double f : {100.0, 300.0, 720.0, 2000.0, 5000.0})
			md = fmax (md, fmax (fabs (Measure (fx, P808, f, 2e-5) - Measure (fx, P9, f, 2e-5)), fabs (Measure (fx, P9, f, 2e-5) - Measure (fx, P10, f, 2e-5))));
		printf ("         maximum TS808/TS9/TS10 difference at small signal: %.3f dB\n", md);
		CHECK (md < 0.1, "TS808, TS9 and TS10: difference below 0.1 dB (output stage)");
		const double b9 = Measure (fx, P9, 150.0, 2e-5), bp = Measure (fx, Pplus, 150.0, 2e-5), bt = Measure (fx, Pturbo, 150.0, 2e-5);
		CHECK (bp > b9 + 6.0 && bt > bp + 2.0, "TS9DX: + and Turbo have far more bass than the TS9 mode (larger capacitor)");
		delete fx;
	}

	// 6. stability: signals of every kind, all models, knobs moved: no NaN and bounded output
	{
		bool ok = true; double pk = 0;
		unsigned seed = 5;
		for (int model = 0; model < 7 && ok; model++)
		{
			CFx *fx = FxCreate (FxTubeScreamer, 48000.0f);
			float L[4], R[4]; int mono = 1;
			for (int b = 0; b < 48000 * 3 / 4 && ok; b++)
			{
				float P[4] = {(float) model, (float) (5 + 5 * sin (b * 0.0007)), (float) (5 + 5 * sin (b * 0.0011)), 10.0f};
				for (int j = 0; j < 4; j++)
				{
					seed = seed * 1664525u + 1013904223u;
					const float noise = ((float) (int) (seed >> 8) / 8388608.0f - 1.0f);
					L[j] = R[j] = (b % 20000 < 10000 ? 0.9f * noise : 0.2f * (float) sin (b * 0.05 + j)) ;
				}
				mono = 1; fx->Process (L, R, 4, &mono, P);
				for (int j = 0; j < 4; j++) { if (!(fabs (L[j]) < 100.0f)) ok = false; pk = fmax (pk, fabs (L[j])); }
			}
			delete fx;
		}
		printf ("         maximum peak with strong noise and knobs in motion: %.3f\n", pk);
		CHECK (ok, "no NaN, output always bounded (7 models, input up to 0.9 of full scale)");
	}

	// 7. stereo: two independent channels
	{
		CFx *fx = FxCreate (FxTubeScreamer, 48000.0f);
		const float P[4] = {1, 5, 5, 8};
		float L[4] = {0.1f, 0.1f, 0.1f, 0.1f}, R[4] = {0, 0, 0, 0};
		int mono = 0;
		double sl = 0, sr = 0;
		for (int b = 0; b < 2000; b++)
		{
			for (int j = 0; j < 4; j++) { L[j] = 0.1f * (float) sin (2 * M_PI * 440.0 * (double) (b * 4 + j) / 48000.0); R[j] = 0.0f; }
			mono = 0; fx->Process (L, R, 4, &mono, P);
			for (int j = 0; j < 4; j++) { sl += fabs (L[j]); sr += fabs (R[j]); }
		}
		CHECK (sl > 1.0 && sr < 1e-6, "stereo: the silent right channel stays silent");
		delete fx;
	}

	printf ("RESULT: %s (%d failed)\n", fails == 0 ? "OK" : "FAILED", fails);
	return fails != 0;
}
