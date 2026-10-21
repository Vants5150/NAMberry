// test_la2a.cpp - tests of Optical LA Real (fx_la2a.cpp) on the PC: gain below threshold, compression, order of the controls, attack and release, stereo link, stability.
// g++ -std=c++17 -O1 -Ihost_shim -I../../../../include -I. -Iadel test_la2a.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_la2a
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)
#define CHECKF(c, ...) do { char b_[220]; snprintf (b_, sizeof b_, __VA_ARGS__); CHECK (c, b_); } while (0)

static const float FS = 48000.0f;
static double Db (double v) { return 20 * log10 (v + 1e-12); }
static unsigned s_seed = 12345;
static float Noise (void) { s_seed = s_seed * 1664525u + 1013904223u; return ((float) (int) (s_seed >> 8) - 8388608.0f) / 8388608.0f; }

struct Run
{
	CFx *fx; float P[10];
	Run (float pr = 65, float mode = 1, float gain = 0, float in = 0, float link = 1) { fx = FxCreate (FxOpticalLA, FS); P[0] = pr; P[1] = mode; P[2] = gain; P[3] = in; P[4] = link; P[5] = 0; P[6] = pr; P[7] = mode; P[8] = gain; P[9] = 0; }
	~Run () { delete fx; }
	// processes n samples of white noise at rmsDb (equal stereo or different L/R); returns the output rms (dB) on L and R over the last second
	void Feed (unsigned n, double rmsLdb, double rmsRdb, double *outL, double *outR, bool bSine = false, float fHz = 1000.0f)
	{
		const float aL = (float) pow (10.0, rmsLdb / 20), aR = (float) pow (10.0, rmsRdb / 20);
		double sl = 0, sr = 0; unsigned cnt = 0; float ph = 0;
		for (unsigned b = 0; b < n / 4; b++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++)
			{
				if (bSine) { ph += fHz / FS; if (ph >= 1.0f) ph -= 1.0f; const float s = (float) sin (6.2831853 * ph) * 1.41421356f; L[j] = aL * s; R[j] = aR * s; }
				else { const float nz = Noise () * 1.7320508f; L[j] = aL * nz; R[j] = aR * Noise () * 1.7320508f; (void) nz; }
			}
			int mono = 0;
			fx->Process (L, R, 4, &mono, P);
			if (b * 4 >= n - (unsigned) FS) { for (int j = 0; j < 4; j++) { sl += L[j] * L[j]; sr += R[j] * R[j]; cnt++; } }
		}
		*outL = Db (sqrt (sl / cnt)); *outR = Db (sqrt (sr / cnt));
	}
	float Gr (void) { float o[FX_MAX_P]; fx->GetOutputs (o); return o[5]; }
};

int main ()
{
	double l, r;
	// 1. below threshold: no reduction
	{ Run a (50); a.Feed (96000, -45, -45, &l, &r); CHECKF (fabs (l + 45) < 0.2 && a.Gr () < 0.2, "below threshold (-45 dBFS, PR 50): output %.2f dB (expected -45), reduction %.2f dB", l, a.Gr ()); }
	// 2. Peak Reduction at zero: no compression even when strong
	{ Run a (0); a.Feed (96000, -8, -8, &l, &r); CHECKF (fabs (l + 8) < 0.2 && a.Gr () < 0.2, "PR 0: no compression at -8 dBFS: output %.2f dB, reduction %.2f dB", l, a.Gr ()); }
	// 3. compression and order of the controls (white noise at -11 dBFS rms, 8 s)
	double g40, g65, g80, g65c;
	{ Run a (40); a.Feed (8 * 48000, -11, -11, &l, &r); g40 = -11 - l; }
	{ Run a (65); a.Feed (8 * 48000, -11, -11, &l, &r); g65 = -11 - l; }
	{ Run a (80); a.Feed (8 * 48000, -11, -11, &l, &r); g80 = -11 - l; }
	{ Run a (65, 0); a.Feed (8 * 48000, -11, -11, &l, &r); g65c = -11 - l; }
	CHECKF (g40 > 0.5 && g65 > g40 + 2.0 && g80 > g65 + 2.0, "reduction at -11 dBFS: PR 40 %.1f, PR 65 %.1f, PR 80 %.1f dB (increasing)", g40, g65, g80);
	CHECKF (g65 > 4.0 && g65 < 16.0, "PR 65 Limit at -11 dBFS rms (white noise): reduction %.1f dB (in the real recordings with noise less rich in treble about 8: white weighs more on the treble band)", g65);
	CHECKF (fabs (g65 - g65c) < 1.5, "Compress and Limit at PR 65 and -11 dBFS differ little (as in the recordings, where at this level they were equal within 0.1 dB): %.2f and %.2f dB", g65, g65c);
	// 4. attack: after a low tone, a step at -10 dBFS: after 20 ms the reduction is at least 40% of that at 2 s
	{
		Run a (65); a.Feed (48000 * 2, -50, -50, &l, &r, true);
		float gr20 = 0, gr2s = 0; float ph = 0; const float amp = (float) pow (10.0, -10.0 / 20) * 1.41421356f;
		for (unsigned b = 0; b < 2 * 48000 / 4; b++)
		{
			float L[4], R[4];
			for (int j = 0; j < 4; j++) { ph += 1000.0f / FS; if (ph >= 1.0f) ph -= 1.0f; L[j] = R[j] = amp * (float) sin (6.2831853 * ph); }
			int mono = 0; a.fx->Process (L, R, 4, &mono, a.P);
			if (b * 4 == 960) gr20 = a.Gr ();
		}
		gr2s = a.Gr ();
		CHECKF (gr2s > 3.0 && gr20 > 0.4 * gr2s, "attack: at 20 ms %.2f dB, at 2 s %.2f dB (>= 40%%)", gr20, gr2s);
	}
	// 5. two-stage release: after 10 s compressed and silence, within 100 ms it recovers more than 40% and after 1 s a tail still remains
	{
		Run a (65); a.Feed (10 * 48000, -11, -11, &l, &r);
		const float g0 = a.Gr (); float g100 = 0, g1s = 0;
		for (unsigned b = 0; b < 48000 / 4 + 1; b++)
		{
			float L[4] = {0, 0, 0, 0}, R[4] = {0, 0, 0, 0}; int mono = 0; a.fx->Process (L, R, 4, &mono, a.P);
			if (b * 4 == 4800) g100 = a.Gr ();
		}
		g1s = a.Gr ();
		CHECKF (g0 > 4.0 && g100 < 0.6 * g0 && g1s > 0.05 && g1s < 0.4 * g0, "release: reduction %.2f dB -> %.2f dB at 100 ms -> %.2f dB at 1 s (slow tail)", g0, g100, g1s);
	}
	// 6. stereo link: strong L, weak R
	{
		Run on (65, 1, 0, 0, 1), off (65, 1, 0, 0, 0);
		double lo, ro, lf, rf;
		on.Feed (6 * 48000, -8, -45, &lo, &ro); off.Feed (6 * 48000, -8, -45, &lf, &rf);
		CHECKF (fabs ((ro + 45) - (lo + 8)) < 1.0 && fabs (rf + 45) < 0.3, "Link: On reduces R like L (%.1f against %.1f dB), Off leaves R at %.2f dB (expected -45)", ro + 45, lo + 8, rf);
	}
	// 6b. Link Off: the right channel has its own controls (pr2, mode2, gain2); with Link On it ignores them
	{
		Run off (65, 1, 0, 0, 0), on (65, 1, 0, 0, 1);
		off.P[6] = 0; off.P[8] = 6; on.P[6] = 0; on.P[8] = 6;
		double lo, ro, ln, rn;
		off.Feed (6 * 48000, -8, -8, &lo, &ro); on.Feed (6 * 48000, -8, -8, &ln, &rn);
		float gl[FX_MAX_P]; off.fx->GetOutputs (gl);
		CHECKF (fabs ((ro + 8) - 6.0) < 0.3 && lo < -8 - 3.0, "Link Off: R with Peak Reduction 0 and Gain +6 comes out at %+.2f dB (expected +6), L compressed to %.1f dB", ro + 8, lo + 8);
		CHECKF (fabs (rn - ln) < 0.3, "Link On: the right channel's controls are ignored (L %.2f, R %.2f dB)", ln, rn);
		CHECKF (gl[5] > 3.0 && gl[9] < 0.1, "meters: left %.1f dB, right %.1f dB with Link Off", gl[5], gl[9]);
	}
	// 7. stability: full-scale noise, all controls at the limits
	{
		bool ok = true; double pk = 0;
		for (int k = 0; k < 8; k++)
		{
			Run a ((k & 1) ? 100.0f : 0.0f, (float) ((k >> 1) & 1), (k & 4) ? 24.0f : -12.0f, (k & 2) ? 24.0f : -24.0f, 1);
			for (unsigned b = 0; b < 48000 * 3 / 4; b++)
			{
				float L[4], R[4]; for (int j = 0; j < 4; j++) { L[j] = Noise (); R[j] = Noise (); }
				int mono = 0; a.fx->Process (L, R, 4, &mono, a.P);
				for (int j = 0; j < 4; j++) { if (!std::isfinite (L[j]) || !std::isfinite (R[j])) ok = false; if (fabs (L[j]) > pk) pk = fabs (L[j]); }
			}
			if (!(a.Gr () >= -0.001f && a.Gr () <= 60.0f)) ok = false;
		}
		CHECKF (ok && pk < 300.0, "stability with full-scale noise and controls at the limits (up to +48 dB of gain): peak %.2f, no NaN", pk);
	}
	printf ("\nRESULT: %s (%d failed)\n", fails ? "FAILED" : "OK", fails);
	return fails ? 1 : 0;
}
