// test_reverb_wetonly.cpp - in Wet Only the dry is removed and Mix still sets the level of the reverb (ADELverb and ADELspring)
#include <cmath>
#include <cstdio>
#include <vector>
#include "adel/adelverb_dsp.hpp"
#include "adel/adelspring_dsp.hpp"

template <class E, class P> static double run (float mix, bool wet)
{
	E e; e.init (48000.0);
	P p; p.wetOnly = wet; p.mix = mix;
	const unsigned N = 256; std::vector<float> in (N), l (N), r (N);
	double acc = 0;
	for (unsigned b = 0; b < 400; b++)
	{
		for (unsigned i = 0; i < N; i++) in[i] = 0.3f * std::sin (6.2831853f * 330.f * (b * N + i) / 48000.f);
		e.process (in.data (), l.data (), r.data (), N, p);
		if (b >= 200) for (unsigned i = 0; i < N; i++) acc += l[i] * l[i] + r[i] * r[i];
	}
	return acc;
}

template <class E, class P> static int check (const char *name)
{
	const double full = run<E, P> (1.0f, true), low = run<E, P> (0.05f, true);
	const double dB = 10.0 * std::log10 (low / full);
	if (dB > -20.0 || dB < -32.0) { std::printf ("FAIL %s: Mix 5%% vs 100%% in Wet Only = %.1f dB (expected about -26)\n", name, dB); return 1; }
	std::printf ("ok   %s: Mix 5%% is %.1f dB below Mix 100%% in Wet Only\n", name, dB);
	return 0;
}

int main ()
{
	return check<adelverb::Reverb, adelverb::Params> ("ADELverb") + check<adelspring::Tank, adelspring::Params> ("ADELspring");
}
