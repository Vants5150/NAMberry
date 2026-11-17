// test_pitch_shimmer.cpp - transposed repeats (ADELdelay stereo and mono) and shimmer (ADELverb): pitch of the repeats, tempo kept,
// dry untouched (no added latency), stability of the shimmer loop (long decay, freeze, extreme settings)
#include <cmath>
#include <cstdio>
#include <vector>
#include "adel/adeldelay_dsp.hpp"
#include "adel/adeldelaym_dsp.hpp"
#include "adel/adelverb_dsp.hpp"

static const double FS = 48000.0;
static int g_bad = 0;
#define CHECK(c, ...) do { if (c) std::printf ("ok   " __VA_ARGS__); else { std::printf ("FAIL " __VA_ARGS__); g_bad++; } std::printf ("\n"); } while (0)

// strongest frequency (Hz) between f0 and f1 in x[a, b) (Hann window, 1 Hz steps refined by the grid)
static double peakHz (const std::vector<float> &x, size_t a, size_t b, double f0, double f1, double *pPeak = 0)
{
	double best = 0, bf = 0;
	for (double f = f0; f <= f1; f += 2.0)
	{
		double re = 0, im = 0;
		for (size_t i = a; i < b; i++)
		{
			const double w = 0.5 - 0.5 * std::cos (6.283185307 * (double) (i - a) / (double) (b - a));
			const double ph = 6.283185307 * f * (double) i / FS;
			re += w * x[i] * std::cos (ph); im += w * x[i] * std::sin (ph);
		}
		const double e = re * re + im * im;
		if (e > best) { best = e; bf = f; }
	}
	if (pPeak) *pPeak = best;
	return bf;
}
static double bandE (const std::vector<float> &x, size_t a, size_t b, double fc, double rel = 0.03)
{
	double s = 0; int n = 0;
	for (double f = fc * (1 - rel); f <= fc * (1 + rel); f += fc * rel / 3, n++)
	{
		double re = 0, im = 0;
		for (size_t i = a; i < b; i++)
		{
			const double w = 0.5 - 0.5 * std::cos (6.283185307 * (double) (i - a) / (double) (b - a));
			const double ph = 6.283185307 * f * (double) i / FS;
			re += w * x[i] * std::cos (ph); im += w * x[i] * std::sin (ph);
		}
		s += re * re + im * im;
	}
	return s / n;
}
static size_t onset (const std::vector<float> &x, size_t from, float thr)
{
	for (size_t i = from; i < x.size (); i++) if (std::fabs (x[i]) > thr) return i;
	return x.size ();
}

template <class E, class P> static void runDelay (E &e, const P &p, std::vector<float> &out, double sec, bool stereo, bool burst = true)
{
	const unsigned N = 128; const size_t T = (size_t) (sec * FS);
	out.assign (T, 0.f);
	std::vector<float> in (N), l (N), r (N);
	for (size_t b = 0; b < T; b += N)
	{
		for (unsigned i = 0; i < N; i++) { const size_t t = b + i; in[i] = (burst && t < (size_t) (0.3 * FS)) ? 0.4f * std::sin (6.283185307 * 440.0 * (double) t / FS) : 0.f; }
		if (stereo) ((adeldelay::Delay &) e).process (in.data (), l.data (), r.data (), N, (const adeldelay::Params &) p);
		else ((adeldelaym::Delay &) e).process (in.data (), l.data (), N, (const adeldelaym::Params &) p);
		for (unsigned i = 0; i < N && b + i < T; i++) out[b + i] = l[i];
	}
}

template <class E, class P> static void testDelay (const char *name, bool stereo)
{
	P base; base.mix = 1.0f; base.wetOnly = true; base.timeMs = 400.f; base.feedback = 0.6f; base.glideMs = 10.f;
	// 1) pitch off vs on: dry untouched
	{
		P a = base, b = base; a.wetOnly = b.wetOnly = false; a.mix = b.mix = 0.5f; b.pitchSemi = 12.f; b.pitchMix = 1.f;
		E ea, eb; ea.init (FS); eb.init (FS);
		std::vector<float> oa, ob; runDelay (ea, a, oa, 0.37, stereo); runDelay (eb, b, ob, 0.37, stereo);
		double dmax = 0; for (size_t i = 0; i < oa.size (); i++) dmax = std::max (dmax, (double) std::fabs (oa[i] - ob[i]));
		CHECK (dmax < 1e-6, "%s: dry identical with the pitch on until the first repeat (max diff %.2g): no latency on the dry", name, dmax);
	}
	// 2) repeats: pitch and timing
	for (int semi : {12, -12, 7})
	{
		P p = base; p.pitchSemi = (float) semi; p.pitchMix = 1.f;
		E e; e.init (FS);
		std::vector<float> o; runDelay (e, p, o, 2.6, stereo);
		const double ratio = std::pow (2.0, semi / 12.0);
		// repeat 1: after 400 ms (the burst lasts 300 ms): window of the sustained part
		const size_t a = (size_t) (0.50 * FS), b = (size_t) (0.66 * FS);
		double pk; const double f1 = peakHz (o, a, b, 150, 2500, &pk);
		CHECK (std::fabs (f1 / (440.0 * ratio) - 1.0) < 0.04, "%s: %+d semitones: repeat 1 at %.0f Hz (expected %.0f)", name, semi, f1, 440.0 * ratio);
		// repeat 2: another step (only up to semitones whose result stays below 4 kHz)
		if (440.0 * ratio * ratio < 3000 && 440.0 * ratio * ratio > 120)
		{
			const size_t a2 = (size_t) (0.90 * FS), b2 = (size_t) (1.06 * FS);
			const double f2 = peakHz (o, a2, b2, 100, 4000, &pk);
			CHECK (std::fabs (f2 / (440.0 * ratio * ratio) - 1.0) < 0.05, "%s: %+d semitones: repeat 2 at %.0f Hz (expected %.0f, each repeat steps again)", name, semi, f2, 440.0 * ratio * ratio);
		}
	}
	// 3) tempo kept: the repeat spacing with and without pitch (unshifted copy, pitch 0 but the path on) stays at 400 ms
	{
		P p = base; p.pitchSemi = 0.f; p.pitchMix = 1.f; p.feedback = 0.5f;
		E e; e.init (FS);
		std::vector<float> o; runDelay (e, p, o, 1.6, stereo);
		P q = base; q.feedback = 0.5f; E e2; e2.init (FS); std::vector<float> o2; runDelay (e2, q, o2, 1.6, stereo);
		const size_t on1 = onset (o, (size_t) (0.1 * FS), 0.05f), on2 = onset (o2, (size_t) (0.1 * FS), 0.05f);
		// second repeat onset
		const size_t on1b = onset (o, (size_t) (0.75 * FS), 0.03f), on2b = onset (o2, (size_t) (0.75 * FS), 0.03f);
		const double d1 = (double) on1 / FS * 1000, d2 = (double) on2 / FS * 1000, d1b = (double) on1b / FS * 1000, d2b = (double) on2b / FS * 1000;
		CHECK (std::fabs (d1 - d2) < 15.0 && std::fabs (d1b - d2b) < 15.0,
		       "%s: tempo kept: repeat 1 at %.0f ms (off %.0f), repeat 2 at %.0f ms (off %.0f)", name, d1, d2, d1b, d2b);
	}
	// 4) output stays bounded with feedback 110% and pitch
	{
		P p = base; p.feedback = 1.1f; p.pitchSemi = 12.f; p.pitchMix = 1.f;
		E e; e.init (FS); std::vector<float> o; runDelay (e, p, o, 12.0, stereo);
		double mx = 0; bool nan = false; for (float v : o) { if (!(v == v)) nan = true; mx = std::max (mx, (double) std::fabs (v)); }
		CHECK (!nan && mx < 2.0, "%s: feedback 110%% with +12: bounded (peak %.2f, no NaN)", name, mx);
	}
}

static void runVerb (adelverb::Reverb &r, const adelverb::Params &p, std::vector<float> &out, double sec, double burstSec, bool noise, int freezeAtSec = -1)
{
	const unsigned N = 128; const size_t T = (size_t) (sec * FS);
	out.assign (T, 0.f);
	std::vector<float> in (N), l (N), rr (N);
	unsigned s = 7; adelverb::Params q = p;
	for (size_t b = 0; b < T; b += N)
	{
		if (freezeAtSec >= 0) q.freeze = (double) b / FS >= freezeAtSec ? 1.f : 0.f;
		for (unsigned i = 0; i < N; i++)
		{
			const size_t t = b + i; float v = 0;
			if ((double) t / FS < burstSec) { if (noise) { s = s * 1664525u + 1013904223u; v = 0.3f * ((float) (int) (s >> 8) / 8388608.f - 1.f) * 0.5f; } else v = 0.3f * std::sin (6.283185307 * 440.0 * (double) t / FS); }
			in[i] = v;
		}
		r.process (in.data (), l.data (), rr.data (), N, q);
		for (unsigned i = 0; i < N && b + i < T; i++) out[b + i] = l[i];
	}
}

static void testShimmer ()
{
	adelverb::Params p; p.wetOnly = true; p.mix = 1.f; p.decay = 6.f; p.er = 0.f; p.predelayMs = 0; p.lowCut = 40.f; p.highCut = 16000.f;
	std::vector<float> o0, o1;
	{ adelverb::Reverb r; r.init (FS); runVerb (r, p, o0, 4.0, 0.2, false); }
	{ adelverb::Reverb r; r.init (FS); adelverb::Params q = p; q.shimmer = 1.f; q.shimmerSemi = 12.f; runVerb (r, q, o1, 4.0, 0.2, false); }
	const size_t a = (size_t) (1.2 * FS), b = (size_t) (3.0 * FS);
	const double e440a = bandE (o0, a, b, 440), e880a = bandE (o0, a, b, 880), e440b = bandE (o1, a, b, 440), e880b = bandE (o1, a, b, 880);
	const double gainAt880 = 10 * std::log10 (e880b / std::max (e880a, 1e-30));
	std::printf ("     shimmer +12: tail energy at 880 Hz %.1f dB vs shimmer off; at 440 Hz %.1f dB\n", gainAt880, 10 * std::log10 (e440b / std::max (e440a, 1e-30)));
	CHECK (gainAt880 > 6.0, "ADELverb: shimmer +12 raises an octave-up halo in the tail (880 Hz: %+.1f dB)", gainAt880);
	// 7 semitones: fifth above 440 = 659 Hz
	std::vector<float> o7; { adelverb::Reverb r; r.init (FS); adelverb::Params q = p; q.shimmer = 1.f; q.shimmerSemi = 7.f; runVerb (r, q, o7, 4.0, 0.2, false); }
	const double e659a = bandE (o0, a, b, 659.3), e659b = bandE (o7, a, b, 659.3);
	CHECK (e659b > 4.0 * e659a, "ADELverb: shimmer +7 puts energy on the fifth (659 Hz: %+.1f dB)", 10 * std::log10 (e659b / std::max (e659a, 1e-30)));
	// no shimmer at 0: identical to the old tail (shimmer amount 0 does not touch the tail)
	{
		adelverb::Params q = p; q.shimmer = 0.f; q.shimmerSemi = -7.f; std::vector<float> o; adelverb::Reverb r; r.init (FS); runVerb (r, q, o, 2.0, 0.2, false);
		double d = 0; for (size_t i = 0; i < o.size (); i++) d = std::max (d, (double) std::fabs (o[i] - o0[i]));
		CHECK (d == 0.0, "ADELverb: Shimmer 0 leaves the tail identical whatever the pitch (max diff %.2g)", d);
	}
	// stability: long tail, shimmer 100%, two directions, then freeze
	for (int sc = 0; sc < 3; sc++)
	{
		adelverb::Params q = p; q.decay = 30.f; q.size = 2.f; q.shimmer = 1.f; q.shimmerSemi = sc == 1 ? -12.f : 12.f; q.mix = 1.f;
		std::vector<float> o; adelverb::Reverb r; r.init (FS);
		runVerb (r, q, o, 30.0, 1.0, true, sc == 2 ? 10 : -1);
		double mx = 0, e1 = 0, e3 = 0; bool nan = false;
		for (size_t i = 0; i < o.size (); i++) { const float v = o[i]; if (!(v == v)) nan = true; mx = std::max (mx, (double) std::fabs (v)); }
		for (size_t i = (size_t) (2 * FS); i < (size_t) (4 * FS); i++) e1 += o[i] * o[i];
		for (size_t i = (size_t) (26 * FS); i < (size_t) (28 * FS); i++) e3 += o[i] * o[i];
		const double dB = 10 * std::log10 (e3 / std::max (e1, 1e-30));
		if (sc == 2) CHECK (!nan && mx < 4.0, "ADELverb: shimmer +12 then Freeze at 10 s, 30 s run: bounded (peak %.2f, tail energy 26-28 s vs 2-4 s %+.1f dB)", mx, dB);
		else CHECK (!nan && mx < 4.0 && dB < 3.0, "ADELverb: shimmer %+d, decay 30 s, size 2: bounded and not growing (peak %.2f, 26-28 s vs 2-4 s %+.1f dB)", sc == 1 ? -12 : 12, mx, dB);
	}
}

int main ()
{
	testDelay<adeldelay::Delay, adeldelay::Params> ("ADELdelay", true);
	testDelay<adeldelaym::Delay, adeldelaym::Params> ("ADELdelay mono", false);
	testShimmer ();
	std::printf (g_bad ? "%d FAILED\n" : "all ok\n", g_bad);
	return g_bad != 0;
}
