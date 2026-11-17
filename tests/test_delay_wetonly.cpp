// test_delay_wetonly.cpp - in Wet Only the dry is removed and Mix still sets the level of the repeats (ADELdelay stereo and mono)
#include <cmath>
#include <cstdio>
#include <vector>
#include "adel/adeldelay_dsp.hpp"
#include "adel/adeldelaym_dsp.hpp"

static void doProc (adeldelay::Delay &e, const float *i, float *l, float *r, unsigned n, const adeldelay::Params &p) { e.process (i, l, r, n, p); }
static void doProc (adeldelaym::Delay &e, const float *i, float *l, float *, unsigned n, const adeldelaym::Params &p) { e.process (i, l, n, p); }

template <class E, class P> static float run (bool wet, float mix, float &dryPart)
{
	E e; e.init (48000.0);
	P p; p.wetOnly = wet; p.mix = mix; p.timeMs = 300.f; p.feedback = 0.3f; p.levelDb = 0.f;
	const unsigned N = 256; std::vector<float> in (N), l (N), r (N);
	double early = 0, late = 0;
	for (unsigned b = 0; b < 200; b++)
	{
		for (unsigned i = 0; i < N; i++) in[i] = b < 20 ? 0.5f * std::sin (6.2831853f * 220.f * (b * N + i) / 48000.f) : 0.f;
		doProc (e, in.data (), l.data (), r.data (), N, p);
		for (unsigned i = 0; i < N; i++) { if (b < 20 && b > 2 && b * N + i < 0.28 * 48000) early += l[i] * l[i]; else if (b >= 60) late += l[i] * l[i]; }
	}
	dryPart = (float) early;
	return (float) late;
}

template <class E, class P> static int check (const char *name)
{
	float d1, d2, d3;
	const float full = run<E, P> (true, 1.0f, d1), low = run<E, P> (true, 0.05f, d2);
	run<E, P> (true, 0.5f, d3);
	const float ratioDb = 10.f * std::log10 (low / full);
	int bad = 0;
	if (ratioDb > -20.f || ratioDb < -32.f) { std::printf ("FAIL %s: Mix 5%% vs 100%% in Wet Only = %.1f dB (expected about -26)\n", name, ratioDb); bad++; }
	if (d2 > 1e-6f * (d1 + 1e-9f) + 1e-12f) { std::printf ("FAIL %s: dry heard in Wet Only (%g)\n", name, d2); bad++; }
	if (!bad) std::printf ("ok   %s: Mix 5%% is %.1f dB below Mix 100%%, no dry before the first repeat\n", name, ratioDb);
	return bad;
}

int main ()
{
	return check<adeldelay::Delay, adeldelay::Params> ("ADELdelay") + check<adeldelaym::Delay, adeldelaym::Params> ("ADELdelay mono");
}
