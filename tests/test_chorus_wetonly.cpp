// test_chorus_wetonly.cpp - ADELchorus: in Wet Only Mix sets the level of the chorus; the CE-1 vibrato (model 1) stays 100% wet and ignores Mix
#include <cmath>
#include <cstdio>
#include <vector>
#include "adel/adelchorus_dsp.hpp"

static double run (int model, float mix, bool wet)
{
	adelchorus::Chorus e; e.init (48000.0);
	adelchorus::Params p; p.model = model; p.wetOnly = wet; p.mix = mix;
	const unsigned N = 256; std::vector<float> in (N), l (N), r (N);
	double acc = 0;
	for (unsigned b = 0; b < 300; b++)
	{
		for (unsigned i = 0; i < N; i++) in[i] = 0.3f * std::sin (6.2831853f * 330.f * (b * N + i) / 48000.f);
		e.process (in.data (), l.data (), r.data (), N, p);
		if (b >= 100) for (unsigned i = 0; i < N; i++) acc += l[i] * l[i] + r[i] * r[i];
	}
	return acc;
}

int main ()
{
	int bad = 0;
	for (int model : {0, 2, 3})
	{
		const double dB = 10.0 * std::log10 (run (model, 0.05f, true) / run (model, 1.0f, true));
		const bool ok = dB < -20.0 && dB > -32.0;
		std::printf ("%s model %d: Mix 5%% is %.1f dB below Mix 100%% in Wet Only\n", ok ? "ok  " : "FAIL", model, dB);
		bad += !ok;
	}
	const double dv = 10.0 * std::log10 (run (1, 0.05f, true) / run (1, 1.0f, true));
	const bool okv = std::fabs (dv) < 0.5;
	std::printf ("%s vibrato (model 1): Mix 5%% vs 100%% = %.2f dB (must not change)\n", okv ? "ok  " : "FAIL", dv);
	bad += !okv;
	return bad;
}
