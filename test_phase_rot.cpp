// test_phase_rot.cpp - stereo ADELphase: suggestion and correction of the phase rotation (per-band analysis on the corrected outputs)
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Iadel test_phase_rot.cpp -o test_phase_rot
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "adelphase_dsp.hpp"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf ("FAILED line %d: %s -- ", __LINE__, #c); printf (__VA_ARGS__); printf ("\n"); fails++; } } while (0)

static const float FS = 48000.0f;

static std::vector<float> Noise (unsigned n, unsigned seed)
{
	std::vector<float> x (n);
	unsigned s = seed;
	float lp = 0.0f;
	for (unsigned i = 0; i < n; i++)
	{
		s = s * 1664525u + 1013904223u;
		const float w = ((float) (int) (s >> 8) - 8388608.0f) / 8388608.0f;
		lp += 0.35f * (w - lp);				// noise with more energy in the bass and mids, like a distorted guitar
		x[i] = 0.5f * (w + 2.0f * lp) / 3.0f;
	}
	return x;
}

static std::vector<float> AllPass (const std::vector<float> &x, int stages, float f0)
{
	std::vector<float> y = x;
	const float tn = tanf ((float) M_PI * f0 / FS), a = (tn - 1.0f) / (tn + 1.0f);
	for (int s = 0; s < stages; s++)
	{
		adelphase::AllPass1 ap;
		for (float &v : y) v = ap.process (v, a);
	}
	return y;
}

static std::vector<float> Delay (const std::vector<float> &x, unsigned d)
{
	std::vector<float> y (x.size (), 0.0f);
	for (unsigned i = d; i < x.size (); i++) y[i] = x[i - d];
	return y;
}

struct Res { float corrIn, corrOut, verdict, stages, freq, sugDelay, sugPol; bool finite; };

// processes in blocks of 4 frames like the board and returns the measurements at the end
static Res Run (adelphase::Phase &P, const adelphase::Params &q, const std::vector<float> &L, const std::vector<float> &R, bool probe = true)
{
	Res r = {0, 0, 0, 0, 0, 0, 0, true};
	std::vector<float> oL (L.size ()), oR (L.size ());
	adelprobe_shm::Shared *sh = adelprobe_shm::get ();
	sh->playing.store (probe ? 1 : 0);
	for (unsigned b = 0; b + 4 <= L.size (); b += 4)
	{
		if (probe) sh->beat.store (sh->beat.load () + 1);		// the probe "plays": the per-band analysis only works this way
		float l[4], rr[4];
		for (int j = 0; j < 4; j++) { l[j] = L[b + j]; rr[j] = R[b + j]; }
		const float *in[2] = {l, rr};
		float *out[2] = {l, rr};
		P.process (in, out, 4, q);
		for (int j = 0; j < 4; j++)
		{
			if (!(fabsf (l[j]) < 100.0f) || !(fabsf (rr[j]) < 100.0f)) r.finite = false;
			oL[b + j] = l[j]; oR[b + j] = rr[j];
		}
	}
	if (probe)
	{
		// end of the sweep: a last block without a playing probe starts the final calculation
		sh->playing.store (0);
		float l[4] = {0, 0, 0, 0}, rr[4] = {0, 0, 0, 0};
		const float *in[2] = {l, rr};
		float *out[2] = {l, rr};
		P.process (in, out, 4, q);
	}
	r.corrIn = P.corrInPct (); r.corrOut = P.corrOutPct (); r.verdict = P.verdict (); r.stages = P.suggestStages (); r.freq = P.suggestFreq ();
	r.sugDelay = P.suggestDelayMs (); r.sugPol = P.suggestPolarity ();
	return r;
}

// normalized correlation between two signals after the first 2 s (outside the transient)
static float Corr (const std::vector<float> &a, const std::vector<float> &b, unsigned from)
{
	double ab = 0, aa = 1e-12, bb = 1e-12;
	for (unsigned i = from; i < a.size (); i++) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
	return (float) (ab / sqrt (aa * bb));
}

int main ()
{
	const unsigned N = (unsigned) (FS * 14);
	const std::vector<float> x = Noise (N, 12345);

	// 0. without a playing probe the per-band analysis does not work: no rotation suggestion, even with a rotated path
	{
		const std::vector<float> L = AllPass (x, 2, 400.0f);
		adelphase::Phase P; P.init (FS, 2); P.activate (0);
		adelphase::Params q;
		Res r = Run (P, q, L, x, false);
		CHECK (r.stages == 0.0f && r.verdict != 7.0f, "without a probe no rotation suggested (%.0f stages, verdict %.0f)", r.stages, r.verdict);
	}

	// 1. two identical paths: nothing to correct, no rotation suggested
	{
		adelphase::Phase P; P.init (FS, 2); P.activate (0);
		adelphase::Params q;
		Res r = Run (P, q, x, x);
		printf ("identical: corrIn %.0f%% corrOut %.0f%% verdict %.0f stages %.0f freq %.0f\n", r.corrIn, r.corrOut, r.verdict, r.stages, r.freq);
		CHECK (r.finite, "finite outputs");
		CHECK (r.corrOut > 95.0f, "output correlation %.1f", r.corrOut);
		CHECK (r.verdict == 1.0f, "verdict OK (1), found %.0f", r.verdict);
		CHECK (r.stages == 0.0f, "no rotation suggested (%.0f stages)", r.stages);
	}

	// 2. L rotated by 2 stages at 400 Hz, R equal to the original: the two paths are out of phase per band and a delay is not enough; it can be corrected by rotating R
	{
		const std::vector<float> L = AllPass (x, 2, 400.0f);
		adelphase::Phase P; P.init (FS, 2); P.activate (0);
		adelphase::Params q;
		Res r0 = Run (P, q, L, x);
		printf ("rotation 2 stages at 400 Hz: corrIn %.0f%% corrOut %.0f%% verdict %.0f -> recommended %.0f stages at %.0f Hz\n", r0.corrIn, r0.corrOut, r0.verdict, r0.stages, r0.freq);
		CHECK (r0.finite, "finite outputs");
		CHECK (r0.stages >= 1.0f && r0.freq >= 100.0f && r0.freq <= 4000.0f, "the engine suggests a rotation (%.0f stages at %.0f Hz)", r0.stages, r0.freq);
		// the recommended values are applied as the page will do and it measures again with a new instance
		adelphase::Phase P2; P2.init (FS, 2); P2.activate (0);
		adelphase::Params q2 = q;
		q2.apStages = (int) r0.stages;
		q2.apFreq = r0.freq;
		Res r1 = Run (P2, q2, L, x);
		printf ("   after the recommended rotation: corrOut %.0f%% (before %.0f%%) verdict %.0f, new advice %.0f stages at %.0f Hz\n", r1.corrOut, r0.corrOut, r1.verdict, r1.stages, r1.freq);
		CHECK (r1.corrOut > r0.corrOut + 15.0f, "the recommended rotation improves the output correlation (%.1f -> %.1f)", r0.corrOut, r1.corrOut);
		CHECK (r1.corrOut > 85.0f, "and takes it beyond 85%% (%.1f)", r1.corrOut);
		CHECK (r1.finite, "finite outputs after the correction");
		// once corrected, it must not keep asking for more (no oscillation between nearby values)
		CHECK (fabsf (r1.stages - r0.stages) <= 1.0f, "after the correction the advice does not change much (%.0f -> %.0f stages)", r0.stages, r1.stages);
	}

	// 3. pure delay of 20 samples on R: Auto Align corrects it and no rotations are needed
	{
		const std::vector<float> R = Delay (x, 20);
		adelphase::Phase P; P.init (FS, 2); P.activate (0);
		adelphase::Params q; q.autoAlign = true;
		Res r = Run (P, q, x, R);
		printf ("delay 20 samples, Auto Align: corrIn %.0f%% corrOut %.0f%% verdict %.0f stages %.0f\n", r.corrIn, r.corrOut, r.verdict, r.stages);
		CHECK (r.corrOut > 90.0f, "with Auto Align the output correlation is high (%.1f)", r.corrOut);
		CHECK (r.stages == 0.0f, "a pure delay does not ask for rotations (%.0f stages)", r.stages);
	}

	// 4. R inverted (polarity): Auto Align inverts it
	{
		std::vector<float> R = x;
		for (float &v : R) v = -v;
		adelphase::Phase P; P.init (FS, 2); P.activate (0);
		adelphase::Params q; q.autoAlign = true;
		Res r = Run (P, q, x, R);
		printf ("polarity inverted, Auto Align: corrOut %.0f%% stages %.0f\n", r.corrOut, r.stages);
		CHECK (r.corrOut > 90.0f, "polarity corrected (%.1f)", r.corrOut);
	}

	// 5. delay and rotation together: Auto Align + recommended rotation bring the correlation much higher than the delay alone
	{
		const std::vector<float> L = AllPass (x, 2, 400.0f);
		const std::vector<float> R = Delay (x, 12);
		adelphase::Phase P; P.init (FS, 2); P.activate (0);
		adelphase::Params q; q.autoAlign = true;
		Res r0 = Run (P, q, L, R);
		adelphase::Phase P2; P2.init (FS, 2); P2.activate (0);
		adelphase::Params q2 = q;
		q2.apStages = (int) r0.stages;
		q2.apFreq = r0.freq > 0 ? r0.freq : 300.0f;
		Res r1 = Run (P2, q2, L, R);
		printf ("delay + rotation: Auto Align only corrOut %.0f%% (recommended %.0f stages at %.0f Hz) -> with the rotation %.0f%%\n", r0.corrOut, r0.stages, r0.freq, r1.corrOut);
		CHECK (r0.finite && r1.finite, "finite outputs");
		CHECK (r1.corrOut >= r0.corrOut - 1.0f, "the rotation does not make it worse (%.1f -> %.1f)", r0.corrOut, r1.corrOut);
	}

	// 6. robustness: extreme parameters and signals, values always finite
	{
		srand (7);
		for (int k = 0; k < 40; k++)
		{
			adelphase::Phase P; P.init (FS, 2); P.activate (0);
			adelphase::Params q;
			q.autoAlign = rand () & 1;
			q.invert = rand () & 1;
			q.delayMs = (float) (rand () % 4001 - 2000) * 0.001f;
			q.apStages = rand () % 5;
			q.apFreq = 20.0f + (float) (rand () % 5000);
			q.trimDb = (float) (rand () % 25 - 12);
			q.mode = rand () % 3;
			std::vector<float> a (N / 4), b (N / 4);
			for (unsigned i = 0; i < a.size (); i++)
			{
				a[i] = ((rand () & 7) == 0) ? 0.0f : (float) (rand () % 20001 - 10000) * 0.0001f * (k % 5 == 0 ? 20.0f : 1.0f);
				b[i] = (k & 1) ? a[i] : (float) (rand () % 20001 - 10000) * 0.0001f;
			}
			Res r = Run (P, q, a, b);
			CHECK (r.finite && r.stages >= 0.0f && r.stages <= 4.0f && r.freq >= 0.0f && r.freq <= 4001.0f, "case %d out of limits (finite %d stages %.1f freq %.1f)", k, (int) r.finite, r.stages, r.freq);
		}
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
