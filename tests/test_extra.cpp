// test_extra.cpp - tests on the build Pi (Linux) of the 20 ToTest effects (fx_extra.cpp)
// g++ -std=c++17 -O1 -Ihost_shim -I<circle>/include -I. -Iadel test_extra.cpp fx.cpp fx_*.cpp ... -o test_extra
#include <cstdio>
#include <cmath>
#include <cstring>
#include <ctime>
#include <vector>
#include "fx.h"
#include "fx_defs.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define CHECKM(c, ...) do { if (!(c)) { printf("FAILED line %d: %s -- ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const float FS = 48000.0f;
static float g_P[FX_MAX_P];

static void Defaults (unsigned type)
{
	const TFxDef *d = FxDefByType (type);
	memset (g_P, 0, sizeof g_P);
	for (unsigned i = 0; i < d->nParams; i++) g_P[i] = d->pParams[i].Def;
}

static void SetP (unsigned type, const char *key, float v)
{
	const TFxDef *d = FxDefByType (type);
	for (unsigned i = 0; i < d->nParams; i++) if (!strcmp (d->pParams[i].Key, key)) { g_P[i] = v; return; }
	printf ("parameter %s not found (type %u)\n", key, type);
	fails++;
}

// processes nBlocks blocks of 4 frames (like the board); input: sine of frequency f and amplitude a (a = 0: silence). Returns L and R in vectors
static void RunSine (CFx *fx, float f, float a, unsigned nSamples, std::vector<float> &oL, std::vector<float> &oR, int mono, float *pIn = 0)
{
	oL.assign (nSamples, 0.0f);
	oR.assign (nSamples, 0.0f);
	double ph = 0.0;
	for (unsigned b = 0; b < nSamples / 4; b++)
	{
		float L[4], R[4];
		for (int j = 0; j < 4; j++)
		{
			const float x = a * (float) sin (ph);
			ph += 6.283185307 * f / FS;
			L[j] = R[j] = x;
			if (pIn) pIn[b * 4 + j] = x;
		}
		int m = mono;
		fx->Process (L, R, 4, &m, g_P);
		for (int j = 0; j < 4; j++) { oL[b * 4 + j] = L[j]; oR[b * 4 + j] = R[j]; }
	}
}

static double Rms (const std::vector<float> &v, unsigned from, unsigned to)
{
	double s = 0;
	for (unsigned i = from; i < to; i++) s += (double) v[i] * v[i];
	return sqrt (s / (to - from));
}

// amplitude of the component at frequency f (Goertzel over a window)
static double Tone (const std::vector<float> &v, float f, unsigned from, unsigned to)
{
	double re = 0, im = 0;
	for (unsigned i = from; i < to; i++)
	{
		const double w = 6.283185307 * f * i / FS;
		re += v[i] * cos (w);
		im += v[i] * sin (w);
	}
	return 2.0 * sqrt (re * re + im * im) / (to - from);
}

static bool Finite (const std::vector<float> &v, float lim)
{
	for (float x : v) if (!(fabsf (x) <= lim)) return false;
	return true;
}

int main ()
{
	std::vector<float> L, R;

	// ---- 1. tables: consistent parameters, creatable types
	unsigned nNew = 0;
	for (unsigned t = 28; t <= 78; t++)
	{
		const TFxDef *d = FxDefByType (t);
		if (!d) continue;		// 42, 57, 66, 72, 73, 74 and 76 (Bit Crusher, Octavia, Q-Tron, Slapback, Dotted Delay, Reverse Delay, Whammy) have been removed
		CHECKM (d != 0, "type %u without definition", t);
		if (!d) continue;
		nNew++;
		CHECKM (strncmp (d->Title, "ToTest", 6) == 0 || strncmp (d->Title, "ADEL", 4) == 0, "title with neither ToTest nor ADEL: %s", d->Title);
		CHECK (d->nParams > 0 && d->nParams <= FX_MAX_P);
		for (unsigned i = 0; i < d->nParams; i++)
		{
			const TFxParam &p = d->pParams[i];
			CHECKM (p.Def >= p.Min && p.Def <= p.Max, "%s.%s: default out of limits", d->Name, p.Key);
			CHECKM (p.Max > p.Min, "%s.%s: limits", d->Name, p.Key);
			if (p.Kind == FxKindChoice) CHECKM (p.pEnum != 0 && p.nEnum == (u8) (p.Max + 1), "%s.%s: choices", d->Name, p.Key);
		}
		CFx *fx = FxCreate (t, FS);
		CHECKM (fx != 0 && fx->Type () == t, "FxCreate %u", t);
		delete fx;
	}
	CHECK (nNew == 41 && FxDefByType (36) == 0 && FxDefByType (42) == 0 && FxDefByType (57) == 0 && FxDefByType (66) == 0 && FxDefByType (69) == 0 && FxDefByType (31) == 0 && FxDefByType (72) == 0 && FxDefByType (73) == 0 && FxDefByType (74) == 0 && FxDefByType (76) == 0);
	printf ("types 28..78 defined: %u\n", nNew);

	// ---- 2. robustness: finite and bounded values with default parameters, at the minimums and at the maximums, mono and stereo; silence stays silence
	for (unsigned t = 28; t <= 78; t++)
	{
		const TFxDef *d = FxDefByType (t);
		if (!d) continue;		// 42, 57, 66, 72, 73, 74 and 76 (Bit Crusher, Octavia, Q-Tron, Slapback, Dotted Delay, Reverse Delay, Whammy) have been removed
		for (int variant = 0; variant < 3; variant++)
		{
			for (int mono = 0; mono < 2; mono++)
			{
				CFx *fx = FxCreate (t, FS);
				Defaults (t);
				for (unsigned i = 0; i < d->nParams; i++)
				{
					if (variant == 1) g_P[i] = d->pParams[i].Min;
					if (variant == 2) g_P[i] = d->pParams[i].Max;
				}
				RunSine (fx, 220.0f, 0.5f, 48000, L, R, mono);
				CHECKM (Finite (L, 40.0f) && Finite (R, 40.0f), "%s variant %d mono %d: non-finite or too large values", d->Name, variant, mono);
				RunSine (fx, 220.0f, 0.0f, 48000, L, R, mono);
				const double rs = Rms (L, 24000, 48000), rr = Rms (R, 24000, 48000);
				// silence after the tone must die out (no reverb tails here; the tape echo with 100% repeats has a long tail)
				const bool bTail = (t == FxTapeEcho || t == FxAnalog || t == FxDuck || t == FxCe1 || t == FxFactory || t == FxDs1 || t == FxCentaur || t == FxDynaComp);		// Dyna Comp: the DC shift stored in C2 (1 uF on R4 1 Mohm, 1 s) is amplified by the OTA with Sensitivity at maximum; repeats and tails that continue after the end of the tone; DS-1: the 4.5 V node (5 kohm with 47 uF, 0.23 s) moves with the clipping diodes' current and slowly returns, as in the real pedal; Klon: the germanium diodes' node is floating in DC (C9 and C10 of 1 uF with only the diodes' leakage) and discharges in about 1 s
				CHECKM (bTail || (rs < 0.01 && rr < 0.01), "%s variant %d mono %d: silence not silent (%g, %g)", d->Name, variant, mono, rs, rr);
				if (mono)
				{
					// with a mono input and no Haas, L and R stay equal (the effects that widen set *pMono = 0)
				}
				delete fx;
			}
		}
	}
	printf ("robustness: 51 effects x 3 variants x mono/stereo checked\n");

	// ---- 3. behaviours
	{	// Tremolo: at 100% depth the level swings down to almost zero; at 0% it does not change
		CFx *fx = FxCreate (FxTremolo, FS); Defaults (FxTremolo); SetP (FxTremolo, "depth", 100.0f); SetP (FxTremolo, "rate", 5.0f);
		RunSine (fx, 440.0f, 0.5f, 96000, L, R, 1);
		double mn = 1e9, mx = 0;
		for (unsigned i = 4800; i + 480 < 96000; i += 480) { const double r = Rms (L, i, i + 480); if (r < mn) mn = r; if (r > mx) mx = r; }
		CHECKM (mn < 0.05 * mx, "tremolo 100%%: minimum %g maximum %g", mn, mx);
		delete fx;
		fx = FxCreate (FxTremolo, FS); Defaults (FxTremolo); SetP (FxTremolo, "depth", 0.0f);
		RunSine (fx, 440.0f, 0.5f, 48000, L, R, 1);
		CHECKM (fabs (Rms (L, 4800, 48000) - 0.5 / sqrt (2.0)) < 0.01, "tremolo 0%%: level %g", Rms (L, 4800, 48000));
		delete fx;
	}
	{	// Noise gate: noise at -70 dB (with threshold -55) is greatly lowered; a tone at -20 dB passes
		CFx *fx = FxCreate (FxGate, FS); Defaults (FxGate);
		RunSine (fx, 440.0f, 0.00045f, 48000, L, R, 1);
		const double quiet = Rms (L, 24000, 48000);
		delete fx;
		fx = FxCreate (FxGate, FS); Defaults (FxGate);
		RunSine (fx, 440.0f, 0.1f, 48000, L, R, 1);
		const double loud = Rms (L, 24000, 48000);
		CHECKM (quiet < 0.00045 / sqrt (2.0) * 0.05, "gate: weak signal not attenuated (%g)", quiet);
		CHECKM (fabs (loud - 0.1 / sqrt (2.0)) < 0.004, "gate: strong tone altered (%g)", loud);
		delete fx;
	}
	{	// Tone Stack Real: bass, mid and treble at 10 against 0 raise 80 Hz, 500 Hz and 4 kHz respectively
		float freq[3] = {80.0f, 500.0f, 4000.0f};
		const char *key[3] = {"bass", "mid", "treble"};
		for (int k = 0; k < 3; k++)
		{
			double g[2];
			for (int v = 0; v < 2; v++)
			{
				CFx *fx = FxCreate (FxToneStack, FS); Defaults (FxToneStack); SetP (FxToneStack, key[k], v ? 10.0f : 0.0f);
				RunSine (fx, freq[k], 0.01f, 48000, L, R, 1);
				g[v] = 20.0 * log10 (Rms (L, 24000, 48000) / (0.01 / sqrt (2.0)));
				delete fx;
			}
			CHECKM (g[1] > g[0] + 3.0, "tonestack %s: 0 -> %g dB, 10 -> %g dB", key[k], g[0], g[1]);
		}
	}
	{	// Booster: +12 dB a 1 kHz con warmth 0, brightness 0, lowcut 20
		CFx *fx = FxCreate (FxBooster, FS); Defaults (FxBooster); SetP (FxBooster, "gain", 12.0f); SetP (FxBooster, "warm", 0.0f); SetP (FxBooster, "bright", 0.0f); SetP (FxBooster, "lowcut", 20.0f);
		RunSine (fx, 1000.0f, 0.05f, 48000, L, R, 1);
		const double g = 20.0 * log10 (Rms (L, 24000, 48000) / (0.05 / sqrt (2.0)));
		CHECKM (fabs (g - 12.0) < 0.5, "booster: %g dB", g);
		delete fx;
	}
	{	// Widener: width 0 -> L = R; width 100 -> unchanged; Haas with mono input -> R delayed and stereo output
		CFx *fx = FxCreate (FxWidener, FS); Defaults (FxWidener); SetP (FxWidener, "width", 0.0f);
		float l[4] = {0.5f, 0.2f, -0.3f, 0.1f}, r[4] = {0.1f, -0.2f, 0.3f, 0.4f}; int m = 0;
		fx->Process (l, r, 4, &m, g_P);
		CHECK (l[0] == r[0] && l[1] == r[1] && l[2] == r[2] && l[3] == r[3]);
		delete fx;
		fx = FxCreate (FxWidener, FS); Defaults (FxWidener); SetP (FxWidener, "width", 100.0f);
		float l2[4] = {0.5f, 0.2f, -0.3f, 0.1f}, r2[4] = {0.1f, -0.2f, 0.3f, 0.4f}; m = 0;
		fx->Process (l2, r2, 4, &m, g_P);
		CHECK (fabsf (l2[0] - 0.5f) < 1e-6f && fabsf (r2[3] - 0.4f) < 1e-6f);
		delete fx;
		fx = FxCreate (FxWidener, FS); Defaults (FxWidener); SetP (FxWidener, "width", 100.0f); SetP (FxWidener, "haas", 10.0f);
		std::vector<float> in (4800);
		RunSine (fx, 300.0f, 0.5f, 4800, L, R, 1, in.data ());
		// R(t) = L(t - 480)
		double err = 0;
		for (unsigned i = 1000; i < 4000; i++) err += fabs (R[i] - L[i - 480]);
		CHECKM (err / 3000 < 0.005, "widener Haas: mean error %g", err / 3000);
		delete fx;
	}
	{	// SecondHand (mode Clean | Hand): L is the input unchanged; with no looseness or drift R is the input delayed by offset + 1 ms guard; with looseness the delay changes at every attack
		const unsigned type = FxSecondHand;
		const unsigned N = 48000 * 4;
		std::vector<float> in (N, 0.0f);
		unsigned seed = 12345;
		for (int b = 0; b < 8; b++)		// eight notes, one every 0.45 s: decaying white noise (its autocorrelation has a single sharp peak, so the delay can be measured)
		{
			const unsigned st = 4800 + (unsigned) b * 21600;
			for (unsigned i = 0; i < 12000 && st + i < N; i++)
			{
				const double t = i / (double) FS;
				seed = seed * 1664525u + 1013904223u;
				const float nz = (float) ((int) (seed >> 8) - 8388608) / 8388608.0f;
				in[st + i] = 0.4f * nz * (float) exp (-t * 12.0);
			}
		}		auto Run = [&] (float *pOutL, float *pOutR)
		{
			CFx *fx = FxCreate (type, FS);
			for (unsigned b = 0; b < N / 4; b++)
			{
				float l[4], r[4]; int m = 1;
				for (int j = 0; j < 4; j++) l[j] = r[j] = in[b * 4 + j];
				fx->Process (l, r, 4, &m, g_P);
				CHECK (m == 0);
				for (int j = 0; j < 4; j++) { pOutL[b * 4 + j] = l[j]; pOutR[b * 4 + j] = r[j]; }
			}
			delete fx;
		};
		std::vector<float> oL (N), oR (N);
		Defaults (type); SetP (type, "off1", 10.0f); SetP (type, "loose1", 0.0f); SetP (type, "drift", 0.0f);
		SetP (type, "dyn", 0.0f); SetP (type, "tone", 0.0f); SetP (type, "tonevar", 0.0f); SetP (type, "pick", 0.0f);
		Run (oL.data (), oR.data ());
		double eL = 0; for (unsigned i = 0; i < N; i++) eL += fabs (oL[i] - in[i]);
		CHECKM (eL < 1e-3, "secondhand: L is not the clean input (%g)", eL);
		// R = input delayed by (10 + 1) ms = 528 samples
		double err = 0, sig = 0;
		for (unsigned i = 30000; i < 90000; i++) { err += fabs (oR[i] - in[i - 528]); sig += fabs (in[i - 528]); }
		CHECKM (err < 0.02 * sig, "secondhand: R is not the delayed input (error %g of %g)", err, sig);
		// looseness: the delay of each note differs and stays inside offset +- looseness
		Defaults (type); SetP (type, "off1", 14.0f); SetP (type, "loose1", 10.0f); SetP (type, "drift", 0.0f);
		SetP (type, "dyn", 0.0f); SetP (type, "tonevar", 0.0f); SetP (type, "pick", 0.0f);
		Run (oL.data (), oR.data ());
		double lagMin = 1e9, lagMax = -1e9;
		for (int b = 0; b < 8; b++)
		{
			const unsigned st = 4800 + (unsigned) b * 21600 + 3000;		// after the crossfade of the attack
			double best = -1e9; int bl = 0;
			for (int l = (int) (2.0 * FS / 1000.0); l <= (int) (40.0 * FS / 1000.0); l++)		// 2 .. 40 ms
			{
				double c = 0;
				for (unsigned i = 0; i < 6000; i++) c += (double) oR[st + i] * in[st + i - (unsigned) l];
				if (c > best) { best = c; bl = l; }
			}
			const double ms = bl * 1000.0 / FS - 1.0;		// minus the 1 ms guard
			if (ms < lagMin) lagMin = ms;
			if (ms > lagMax) lagMax = ms;
		}
		CHECKM (lagMin >= 14.0 - 10.0 - 0.7 && lagMax <= 14.0 + 10.0 + 0.7, "secondhand: delays out of range (%g .. %g ms)", lagMin, lagMax);
		CHECKM (lagMax - lagMin > 4.0, "secondhand: the delay does not change from note to note (%g .. %g ms)", lagMin, lagMax);
		// another take: other delays
		Defaults (type); SetP (type, "off1", 14.0f); SetP (type, "loose1", 10.0f); SetP (type, "drift", 0.0f); SetP (type, "take", 7.0f);
		SetP (type, "dyn", 0.0f); SetP (type, "tonevar", 0.0f); SetP (type, "pick", 0.0f);
		std::vector<float> o2L (N), o2R (N);
		Run (o2L.data (), o2R.data ());
		double diff = 0; for (unsigned i = 0; i < N; i++) diff += fabs (o2R[i] - oR[i]);
		CHECKM (diff > 1.0, "secondhand: a different take gives the same performance");
		// pitch drift 20 cents at 1 Hz: the frequency of a sustained 300 Hz tone wanders by a few Hz; with no drift it does not
		auto Wander = [&] (float drift)
		{
			Defaults (type); SetP (type, "off1", 10.0f); SetP (type, "loose1", 0.0f); SetP (type, "drift", drift); SetP (type, "driftrate", 1.0f);
			SetP (type, "dyn", 0.0f); SetP (type, "tonevar", 0.0f); SetP (type, "pick", 0.0f);
			CFx *fx = FxCreate (type, FS);
			std::vector<float> sL, sR;
			RunSine (fx, 300.0f, 0.3f, 192000, sL, sR, 1);
			delete fx;
			// instantaneous frequency: demodulate at 300 Hz, smooth over 20 ms, differentiate the phase
			const unsigned W = 960;
			double maxDf = 0, prevPh = 0; bool first = true;
			for (unsigned s = 48000; s + W < 192000; s += W / 2)
			{
				double re = 0, im = 0;
				for (unsigned i = s; i < s + W; i++) { const double w = 6.283185307 * 300.0 * i / FS; re += sR[i] * cos (w); im += -sR[i] * sin (w); }
				const double ph = atan2 (im, re);
				if (!first)
				{
					double dp = ph - prevPh; while (dp > 3.14159265) dp -= 6.2831853; while (dp < -3.14159265) dp += 6.2831853;
					const double df = fabs (dp) / 6.2831853 / ((W / 2) / (double) FS);
					if (df > maxDf) maxDf = df;
				}
				prevPh = ph; first = false;
			}
			return maxDf;
		};
		const double dfNone = Wander (0.0f), dfDrift = Wander (20.0f);
		CHECKM (dfNone < 0.5, "secondhand: pitch moves with no drift (%g Hz)", dfNone);
		CHECKM (dfDrift > 1.0 && dfDrift < 9.0, "secondhand: drift 20 cents: maximum frequency deviation %g Hz", dfDrift);
		// Hand | Hand: both outputs carry the signal and stay bounded
		Defaults (type); SetP (type, "mode", 1.0f);
		CFx *fx2 = FxCreate (type, FS);
		std::vector<float> sL, sR;
		RunSine (fx2, 300.0f, 0.3f, 96000, sL, sR, 1);
		CHECKM (Rms (sL, 48000, 96000) > 0.1 && Rms (sR, 48000, 96000) > 0.1, "secondhand Hand | Hand: a hand is silent");
		CHECK (Finite (sL, 2.0f) && Finite (sR, 2.0f));
		delete fx2;
	}	{	// ADELshifter Manual: a note with no saved setting uses the voice's semitones; with a saved setting for that pitch class the saved semitones are used
		const unsigned type = FxShifterManual;
		Defaults (type); SetP (type, "dry", -60.0f); SetP (type, "v1on", 1.0f); SetP (type, "v1semi", 12.0f); SetP (type, "v1level", 0.0f); SetP (type, "v1pan", 0.0f);
		SetP (type, "v2on", 0.0f); SetP (type, "v3on", 0.0f); SetP (type, "glide", 5.0f);
		CFx *fx = FxCreate (type, FS);
		std::vector<float> L, R;
		RunSine (fx, 110.0f, 0.3f, 96000, L, R, 1);
		const double u220 = Tone (L, 220.0f, 48000, 96000), u165 = Tone (L, 164.81f, 48000, 96000);
		CHECKM (u220 > 4.0 * u165, "shifter manual: unsaved note: %g at 220 Hz against %g at 164.8 Hz", u220, u165);
		delete fx;
		// the note A (pitch class 9): voice 1 +7 semitones (packed (7+24) + 49 (0+24) + 2401 (0+24))
		SetP (type, "n9", (float) ((7 + 24) + 49 * 24 + 2401 * 24));
		fx = FxCreate (type, FS);
		RunSine (fx, 110.0f, 0.3f, 96000, L, R, 1);
		const double s220 = Tone (L, 220.0f, 48000, 96000), s165 = Tone (L, 164.81f, 48000, 96000);
		CHECKM (s165 > 4.0 * s220, "shifter manual: saved note: %g at 164.8 Hz against %g at 220 Hz", s165, s220);
		delete fx;
		// a saved setting of ANOTHER note does not affect this one
		Defaults (type); SetP (type, "dry", -60.0f); SetP (type, "v1on", 1.0f); SetP (type, "v1semi", 12.0f); SetP (type, "v1level", 0.0f); SetP (type, "v1pan", 0.0f);
		SetP (type, "v2on", 0.0f); SetP (type, "v3on", 0.0f); SetP (type, "glide", 5.0f);
		SetP (type, "n4", (float) ((7 + 24) + 49 * 24 + 2401 * 24));
		fx = FxCreate (type, FS);
		RunSine (fx, 110.0f, 0.3f, 96000, L, R, 1);
		CHECKM (Tone (L, 220.0f, 48000, 96000) > 4.0 * Tone (L, 164.81f, 48000, 96000), "shifter manual: a saved note affects another one");
		delete fx;
	}	{	// Fuzz/overdrive: with a large input the output is bounded; the Fuzz Face is asymmetric (a non-zero mean after the DC filter is not guaranteed): the shape is checked
		unsigned types[3] = {FxTsDrive, FxMuff, FxFuzzFace};
		for (int k = 0; k < 3; k++)
		{
			CFx *fx = FxCreate (types[k], FS); Defaults (types[k]);
			RunSine (fx, 220.0f, 0.8f, 48000, L, R, 1);
			double mx = 0; for (unsigned i = 24000; i < 48000; i++) if (fabs (L[i]) > mx) mx = fabs (L[i]);
			CHECKM (mx > 0.05 && mx < 3.0, "type %u: peak %g", types[k], mx);
			// distortion: there are strong odd harmonics (660 Hz) relative to the fundamental
			const double h1 = Tone (L, 220.0f, 24000, 48000), h3 = Tone (L, 660.0f, 24000, 48000);
			CHECKM (h3 > 0.03 * h1, "type %u: little distortion (h3/h1 %g)", types[k], h3 / h1);
			delete fx;
		}
	}
	{	// Octaver: 440 Hz input, sub only (dry 0): the 220 Hz component prevails over 440 Hz
		CFx *fx = FxCreate (FxOctaver, FS); Defaults (FxOctaver); SetP (FxOctaver, "dry", 0.0f); SetP (FxOctaver, "sub", 100.0f);
		RunSine (fx, 440.0f, 0.5f, 48000, L, R, 1);
		const double a220 = Tone (L, 220.0f, 12000, 48000), a440 = Tone (L, 440.0f, 12000, 48000);
		CHECKM (a220 > 0.1 && a220 > 2.0 * a440, "octaver: 220 Hz %g, 440 Hz %g", a220, a440);
		delete fx;
	}
	{	// Flanger/Vibrato/Rotary/Uni-Vibe/Auto-Wah/Tape Echo: the output is not identical to the input and the level is reasonable
		unsigned types[6] = {FxFlanger, FxVibrato, FxRotary, FxUniVibe, FxAutoWah, FxTapeEcho};
		for (int k = 0; k < 6; k++)
		{
			CFx *fx = FxCreate (types[k], FS); Defaults (types[k]);
			std::vector<float> in (96000);
			RunSine (fx, 330.0f, 0.3f, 96000, L, R, 1, in.data ());
			double diff = 0; for (unsigned i = 48000; i < 96000; i++) diff += fabs (L[i] - in[i]);
			const double rs = Rms (L, 48000, 96000);
			CHECKM (diff / 48000 > 0.005, "type %u: output equal to the input", types[k]);
			CHECKM (rs > 0.02 && rs < 1.0, "type %u: level %g", types[k], rs);
			delete fx;
		}
	}
	{	// Tape echo: an impulse comes back after "time" ms
		CFx *fx = FxCreate (FxTapeEcho, FS); Defaults (FxTapeEcho); SetP (FxTapeEcho, "time", 100.0f); SetP (FxTapeEcho, "wow", 0.0f); SetP (FxTapeEcho, "mix", 100.0f); SetP (FxTapeEcho, "repeats", 0.0f); SetP (FxTapeEcho, "tone", 100.0f); SetP (FxTapeEcho, "sat", 0.0f);
		std::vector<float> out (24000);
		for (unsigned b = 0; b < 24000 / 4; b++)
		{
			float l[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0}; int m = 1;
			if (b == 0) l[0] = 1.0f;
			for (int j = 0; j < 4; j++) r[j] = l[j];
			fx->Process (l, r, 4, &m, g_P);
			for (int j = 0; j < 4; j++) out[b * 4 + j] = l[j];
		}
		unsigned peak = 0; float pv = 0;
		for (unsigned i = 100; i < 24000; i++) if (fabsf (out[i]) > pv) { pv = fabsf (out[i]); peak = i; }
		CHECKM (peak > 4700 && peak < 4950 && pv > 0.1f, "tape echo: echo at %u samples (expected about 4800), amplitude %g", peak, pv);
		delete fx;
	}
	{	// Volume swell: after silence, the level rises slowly (600 ms attack)
		CFx *fx = FxCreate (FxSwell, FS); Defaults (FxSwell);
		RunSine (fx, 440.0f, 0.3f, 48000, L, R, 1);
		const double early = Rms (L, 480, 960), late = Rms (L, 40000, 48000);
		CHECKM (early < 0.3 * late, "swell: start %g end %g", early, late);
		delete fx;
	}
	{	// Harmonic tremolo and Uni-Vibe: with depth 0 the amplitude stays that of the input (uni-vibe in chorus with 0 intensity = an all-pass added to the direct)
		CFx *fx = FxCreate (FxHarmTrem, FS); Defaults (FxHarmTrem); SetP (FxHarmTrem, "depth", 0.0f);
		RunSine (fx, 440.0f, 0.4f, 48000, L, R, 1);
		CHECKM (fabs (Rms (L, 4800, 48000) - 0.4 / sqrt (2.0)) < 0.005, "harmtrem 0%%: %g", Rms (L, 4800, 48000));
		delete fx;
	}

	// ======== tests of effects 48..77 ========
	{	// Limiter: the peak does not exceed the ceiling; a signal below the threshold passes almost unchanged
		CFx *fx = FxCreate (FxLimiter, FS); Defaults (FxLimiter); SetP (FxLimiter, "thr", -12.0f); SetP (FxLimiter, "ceiling", -6.0f);
		RunSine (fx, 220.0f, 0.9f, 48000, L, R, 1);
		double mx = 0; for (unsigned i = 0; i < 48000; i++) if (fabs (L[i]) > mx) mx = fabs (L[i]);
		CHECKM (mx <= 0.5012 && mx > 0.2, "limiter: peak %g (ceiling 0.501)", mx);
		delete fx;
		fx = FxCreate (FxLimiter, FS); Defaults (FxLimiter); SetP (FxLimiter, "thr", -12.0f); SetP (FxLimiter, "ceiling", -0.5f);
		RunSine (fx, 220.0f, 0.1f, 48000, L, R, 1);
		CHECKM (fabs (Rms (L, 4800, 48000) - 0.1 / sqrt (2.0)) < 0.002, "limiter: signal below threshold altered (%g)", Rms (L, 4800, 48000));
		delete fx;
	}
	{	// Compressors: the difference between a strong and a weak tone shrinks; Sustainer: a decaying tone stays high
		unsigned types[1] = {FxDynaComp};
		for (int k = 0; k < 1; k++)
		{
			CFx *fx = FxCreate (types[k], FS); Defaults (types[k]);
			RunSine (fx, 330.0f, 0.5f, 48000, L, R, 1);
			const double loud = Rms (L, 24000, 48000);
			delete fx;
			fx = FxCreate (types[k], FS); Defaults (types[k]);
			RunSine (fx, 330.0f, 0.02f, 48000, L, R, 1);
			const double soft = Rms (L, 24000, 48000);
			delete fx;
			const double inRatio = 0.5 / 0.02, outRatio = loud / soft;
			CHECKM (outRatio < 0.7 * inRatio && outRatio > 1.0, "compressor type %u: ratio in %g, out %g", types[k], inRatio, outRatio);
		}
		CFx *fx = FxCreate (FxSustain, FS); Defaults (FxSustain); SetP (FxSustain, "sustain", 100.0f); SetP (FxSustain, "level", 0.0f);
		RunSine (fx, 330.0f, 0.01f, 48000, L, R, 1);
		const double r = Rms (L, 24000, 48000);
		CHECKM (r > 0.01 / sqrt (2.0) * 3.0 && r < 1.2, "sustainer: weak input 0.01 -> output rms %g", r);
		delete fx;
	}
	{	// Delay: an impulse comes back where it should (Analog, Slapback, Dotted on L at 3/4 and on R at 1/1, Ducking)
		struct { unsigned type; const char *tk; float t; } cs[2] = {{FxAnalog, "time", 100.0f}, {FxDuck, "time", 100.0f}};
		for (int k = 0; k < 2; k++)
		{
			CFx *fx = FxCreate (cs[k].type, FS); Defaults (cs[k].type); SetP (cs[k].type, cs[k].tk, cs[k].t); SetP (cs[k].type, "repeats", 0.0f); SetP (cs[k].type, "mix", 100.0f);
			if (cs[k].type == FxAnalog) SetP (cs[k].type, "mod", 0.0f);
			std::vector<float> out (24000);
			for (unsigned b = 0; b < 24000 / 4; b++)
			{
				float l[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0}; int m = 1;
				if (b == 0) l[0] = 1.0f;
				for (int j = 0; j < 4; j++) r[j] = l[j];
				fx->Process (l, r, 4, &m, g_P);
				for (int j = 0; j < 4; j++) out[b * 4 + j] = l[j];
			}
			unsigned peak = 0; float pv = 0;
			for (unsigned i = 200; i < 24000; i++) if (fabsf (out[i]) > pv) { pv = fabsf (out[i]); peak = i; }
			CHECKM (peak > 4700 && peak < 4950 && pv > 0.05f, "%s: echo at %u samples (expected about 4800), amplitude %g", FxDefByType (cs[k].type)->Name, peak, pv);
			delete fx;
		}
		// ducking: with a strong continuous tone the effect (repeats only) drops a lot compared to duck 0
		CFx *fx = FxCreate (FxDuck, FS); Defaults (FxDuck); SetP (FxDuck, "duck", 0.0f); SetP (FxDuck, "mix", 100.0f);
		std::vector<float> in (96000);
		RunSine (fx, 330.0f, 0.5f, 96000, L, R, 1, in.data ());
		double w0 = 0; for (unsigned i = 48000; i < 96000; i++) w0 += fabs (L[i] - in[i]);
		delete fx;
		fx = FxCreate (FxDuck, FS); Defaults (FxDuck); SetP (FxDuck, "duck", 100.0f); SetP (FxDuck, "mix", 100.0f);
		RunSine (fx, 330.0f, 0.5f, 96000, L, R, 1, in.data ());
		double w1 = 0; for (unsigned i = 48000; i < 96000; i++) w1 += fabs (L[i] - in[i]);
		CHECKM (w1 < 0.35 * w0, "ducking: repeats with duck 100 %g, with duck 0 %g", w1, w0);
		delete fx;
	}
	{	// Auto-Pan: stereo output, L and R swing in opposition; Slicer (offbeat): the level drops to zero in steps
		CFx *fx = FxCreate (FxAutoPan, FS); Defaults (FxAutoPan); SetP (FxAutoPan, "rate", 2.0f); SetP (FxAutoPan, "depth", 100.0f); SetP (FxAutoPan, "wave", 0.0f);
		float l[4] = {0.3f, 0.3f, 0.3f, 0.3f}, r[4] = {0.3f, 0.3f, 0.3f, 0.3f}; int m = 1;
		fx->Process (l, r, 4, &m, g_P);
		CHECKM (m == 0, "autopan: the output must be stereo");
		RunSine (fx, 440.0f, 0.4f, 96000, L, R, 1);
		double dmax = 0;
		for (unsigned i = 4800; i + 960 < 96000; i += 960) { const double a = Rms (L, i, i + 960), b = Rms (R, i, i + 960); dmax = fmax (dmax, fabs (a - b)); }
		CHECKM (dmax > 0.15, "autopan: maximum difference between L and R %g", dmax);
		delete fx;
		fx = FxCreate (FxSlicer, FS); Defaults (FxSlicer); SetP (FxSlicer, "pattern", 2.0f); SetP (FxSlicer, "rate", 2.0f); SetP (FxSlicer, "depth", 100.0f);
		RunSine (fx, 440.0f, 0.4f, 96000, L, R, 1);
		double mn = 1e9, mx = 0;
		for (unsigned i = 4800; i + 480 < 96000; i += 480) { const double a = Rms (L, i, i + 480); if (a < mn) mn = a; if (a > mx) mx = a; }
		CHECKM (mn < 0.1 * mx && mx > 0.2, "slicer: minimum %g maximum %g", mn, mx);
		delete fx;
	}
	{	// Graphic EQ: +12 dB at 800 Hz; flat = unchanged. Woman Tone: low-pass. Cry Baby: the peak follows the position
		CFx *fx = FxCreate (FxGeq, FS); Defaults (FxGeq); SetP (FxGeq, "g800", 12.0f);
		RunSine (fx, 800.0f, 0.1f, 48000, L, R, 1);
		double g = 20.0 * log10 (Rms (L, 24000, 48000) / (0.1 / sqrt (2.0)));
		CHECKM (g > 10.0 && g < 13.0, "geq6 +12 dB a 800 Hz: %g dB", g);
		delete fx;
		fx = FxCreate (FxGeq, FS); Defaults (FxGeq);
		RunSine (fx, 1000.0f, 0.1f, 48000, L, R, 1);
		CHECK (fabs (Rms (L, 24000, 48000) - 0.1 / sqrt (2.0)) < 1e-4);
		delete fx;
		fx = FxCreate (FxWoman, FS); Defaults (FxWoman); SetP (FxWoman, "level", 0.0f); SetP (FxWoman, "body", 0.0f);
		RunSine (fx, 200.0f, 0.2f, 48000, L, R, 1);
		const double lo = Rms (L, 24000, 48000);
		delete fx;
		fx = FxCreate (FxWoman, FS); Defaults (FxWoman); SetP (FxWoman, "level", 0.0f); SetP (FxWoman, "body", 0.0f);
		RunSine (fx, 5000.0f, 0.2f, 48000, L, R, 1);
		const double hi = Rms (L, 24000, 48000);
		CHECKM (lo > 4.0 * hi, "womantone: 200 Hz %g, 5 kHz %g", lo, hi);
		delete fx;
		double a350[2], a2000[2];
		for (int k = 0; k < 2; k++)
		{
			fx = FxCreate (FxCry, FS); Defaults (FxCry); SetP (FxCry, "pos", k ? 10.0f : 0.0f);
			RunSine (fx, 350.0f, 0.02f, 96000, L, R, 1); a350[k] = Rms (L, 60000, 96000);
			delete fx;
			fx = FxCreate (FxCry, FS); Defaults (FxCry); SetP (FxCry, "pos", k ? 10.0f : 0.0f);
			RunSine (fx, 2000.0f, 0.02f, 96000, L, R, 1); a2000[k] = Rms (L, 60000, 96000);
			delete fx;
		}
		CHECKM (a350[0] > 2.0 * a2000[0] && a2000[1] > 2.0 * a350[1], "crybaby: pos 0 -> 350 Hz %g / 2 kHz %g; pos 100 -> 350 Hz %g / 2 kHz %g", a350[0], a2000[0], a350[1], a2000[1]);
	}
	{	// New distortions: the peak is bounded and there are harmonics
		unsigned types[8] = {FxCentaur, FxBluesDrv, FxDs1, FxOcd, FxGuv, FxRange, FxFz1, FxBender};
		for (int k = 0; k < 8; k++)
		{
			CFx *fx = FxCreate (types[k], FS); Defaults (types[k]);
			const TFxDef *d = FxDefByType (types[k]);
			SetP (types[k], d->pParams[0].Key, 85.0f);
			RunSine (fx, 220.0f, 0.5f, 48000, L, R, 1);
			double mx = 0; for (unsigned i = 24000; i < 48000; i++) if (fabs (L[i]) > mx) mx = fabs (L[i]);
			CHECKM (mx > 0.03 && mx < 3.0, "%s: peak %g", d->Name, mx);
			const double h1 = Tone (L, 220.0f, 24000, 48000), h2 = Tone (L, 440.0f, 24000, 48000), h3 = Tone (L, 660.0f, 24000, 48000);
			CHECKM (h2 + h3 > 0.02 * h1, "%s: little distortion (h2 %g h3 %g h1 %g)", d->Name, h2, h3, h1);
			delete fx;
		}
	}
	{	// New modulations: the output is not identical to the input and the level is reasonable; Dimension and Auto-Pan make the output stereo from a mono input
		unsigned types[5] = {FxPhase90, FxStone, FxCe1, FxDimD, FxSustain};
		for (int k = 0; k < 5; k++)
		{
			CFx *fx = FxCreate (types[k], FS); Defaults (types[k]);
			std::vector<float> in (96000);
			RunSine (fx, 330.0f, 0.3f, 96000, L, R, 1, in.data ());
			double diff = 0; for (unsigned i = 48000; i < 96000; i++) diff += fabs (L[i] - in[i]);
			const double rs = Rms (L, 48000, 96000);
			CHECKM (diff / 48000 > 0.003, "%s: output equal to the input", FxDefByType (types[k])->Name);
			CHECKM (rs > 0.01 && rs < 2.0, "%s: level %g", FxDefByType (types[k])->Name, rs);
			delete fx;
		}
		CFx *fx = FxCreate (FxDimD, FS); Defaults (FxDimD);
		float l[4] = {0.3f, 0.1f, -0.2f, 0.3f}, r[4] = {0.3f, 0.1f, -0.2f, 0.3f}; int m = 1;
		fx->Process (l, r, 4, &m, g_P);
		CHECKM (m == 0, "dimension: the output must be stereo");
		delete fx;
	}

	{	// Sync to MIDI tempo: at 120 bpm the 1/4 repeat comes back after 500 ms even with time = 100 ms; with Sync Off or with no known tempo the manual time applies
		struct { unsigned type; float sel; unsigned want; } cs[3] = {{FxTapeEcho, 1.0f, 24000}, {FxAnalog, 1.0f, 24000}, {FxDuck, 1.0f, 24000}};
		for (int variant = 0; variant < 3; variant++)
		{
			for (int k = 0; k < 3; k++)
			{
				g_fFxTempoBpm = variant == 2 ? 0.0f : 120.0f;
				CFx *fx = FxCreate (cs[k].type, FS); Defaults (cs[k].type);
				SetP (cs[k].type, "time", 100.0f); SetP (cs[k].type, "repeats", 0.0f); SetP (cs[k].type, "mix", 100.0f); SetP (cs[k].type, "sync", variant == 1 ? 0.0f : cs[k].sel);
				if (cs[k].type == FxAnalog) SetP (cs[k].type, "mod", 0.0f);
				if (cs[k].type == FxTapeEcho) { SetP (cs[k].type, "wow", 0.0f); SetP (cs[k].type, "sat", 0.0f); SetP (cs[k].type, "tone", 100.0f); }
				std::vector<float> out (36000);
				for (unsigned b = 0; b < 36000 / 4; b++)
				{
					float l[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0}; int m = 1;
					if (b == 0) l[0] = 1.0f;
					for (int j = 0; j < 4; j++) r[j] = l[j];
					fx->Process (l, r, 4, &m, g_P);
					for (int j = 0; j < 4; j++) out[b * 4 + j] = l[j];
				}
				unsigned peak = 0; float pv = 0;
				for (unsigned i = 200; i < 36000; i++) if (fabsf (out[i]) > pv) { pv = fabsf (out[i]); peak = i; }
				const unsigned want = variant == 0 ? cs[k].want : 4800;
				CHECKM (peak + 200 > want && peak < want + 200 && pv > 0.03f, "%s sync variant %d: echo at %u samples (expected about %u), amplitude %g", FxDefByType (cs[k].type)->Name, variant, peak, want, pv);
				delete fx;
			}
		}
		g_fFxTempoBpm = 0.0f;
		// sync without tempo: FxSyncMs returns the manual value; with the tempo it respects the limits
		CHECK (FxSyncMs (123.0f, 1.0f, 20.0f, 1000.0f) == 123.0f);
		g_fFxTempoBpm = 60.0f;
		CHECK (FxSyncMs (123.0f, 1.0f, 20.0f, 600.0f) == 600.0f && FxSyncMs (123.0f, 5.0f, 20.0f, 600.0f) == 250.0f && FxSyncMs (123.0f, 0.0f, 20.0f, 600.0f) == 123.0f);
		g_fFxTempoBpm = 0.0f;
	}

	// ---- 4. relative cost on the PC (only for comparison between effects; the board's CPU must be measured on the board)
	for (unsigned t = 28; t <= 78; t++)
	{
		if (!FxDefByType (t)) continue;
		CFx *fx = FxCreate (t, FS); Defaults (t);
		float l[4], r[4];
		clock_t c0 = clock ();
		for (unsigned b = 0; b < 200000; b++)
		{
			for (int j = 0; j < 4; j++) l[j] = r[j] = 0.3f * sinf ((float) (b * 4 + j) * 0.0575f);
			int m = 0;
			fx->Process (l, r, 4, &m, g_P);
		}
		const double us = (double) (clock () - c0) / CLOCKS_PER_SEC * 1e6 / 200000.0;
		printf ("  %-12s %.2f us per block of 4 stereo frames (period 83 us)\n", FxDefByType (t)->Name, us);
		delete fx;
	}

	// ---- ADELSendSweep "Mute Output": the flag is raised only while the sample plays, only with the switch on, and is consumed by the callback
	{
		CFx *fx = FxCreate (FxAdelSweep, FS); Defaults (FxAdelSweep);
		CHECKM (FxProbeMuteOut () == 0, "sweep: mute flag down before playing");
		SetP (FxAdelSweep, "play", 1.0f);
		std::vector<float> a, b;
		RunSine (fx, 440.0f, 0.1f, 400, a, b, 0);
		CHECKM (FxProbePlaying () == 1, "sweep: the probe is playing");
		CHECKM (FxProbeMuteOut () == 1, "sweep: Mute Output on (default) raises the flag while playing");
		CHECKM (FxProbeMuteOut () == 0, "sweep: the flag is consumed (a bypassed block cannot leave the outputs muted)");
		SetP (FxAdelSweep, "muteout", 0.0f);
		RunSine (fx, 440.0f, 0.1f, 400, a, b, 0);
		CHECKM (FxProbeMuteOut () == 0, "sweep: Mute Output off leaves the outputs alone");
		delete fx;
		CHECKM (FxProbeMuteOut () == 0 && FxProbePlaying () == 0, "sweep: flags down after the block is deleted");
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
