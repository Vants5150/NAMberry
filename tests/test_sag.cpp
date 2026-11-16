// test_sag.cpp - tests of ADELSag (fx_sag.cpp) on the PC: transparency, steady-state depth, attack and recovery, class A, ripple, stereo sharing,
// independence from the block size, stability.
// Run with tests/run_host_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)
#define CHECKF(c, ...) do { char b_[240]; snprintf (b_, sizeof b_, __VA_ARGS__); CHECK (c, b_); } while (0)

static double Db (double v) { return 20 * log10 (v + 1e-12); }

// parameters in the order of fx_defs.h: supply, depth, attack, recovery, level, class, ripple, mains
struct Par
{
	float p[10];
	Par (float supply = 5, float depth = 10, float atk = 8, float rec = 40, float level = -9, float cls = 0, float rip = 0, float mains = 0)
	{ p[0] = supply; p[1] = depth; p[2] = atk; p[3] = rec; p[4] = level; p[5] = cls; p[6] = rip; p[7] = mains; p[8] = 0; p[9] = 0; }
};

struct Run
{
	CFx *fx; float fs; Par par; double ph; double gLast;
	Run (const Par &pa, float rate = 48000.0f) : fx (FxCreate (FxSag, rate)), fs (rate), par (pa), ph (0), gLast (1) {}
	~Run () { delete fx; }
	// processes n samples of a sine (peak dBFS, 1 kHz) and returns the gain measured as the ratio output/input over the last "tail" samples (rms)
	double Sine (unsigned n, double peakDb, double fHz = 1000.0, unsigned blk = 4, unsigned tail = 4800)
	{
		const double a = pow (10.0, peakDb / 20);
		double si = 0, so = 0; unsigned cnt = 0;
		std::vector<float> L (blk), R (blk), I (blk);
		for (unsigned b = 0; b < n; b += blk)
		{
			const unsigned m = blk < n - b ? blk : n - b;
			for (unsigned j = 0; j < m; j++) { I[j] = (float) (a * sin (ph)); ph += 2 * M_PI * fHz / fs; L[j] = R[j] = I[j]; }
			int mono = 0;
			fx->Process (L.data (), R.data (), m, &mono, par.p);
			if (b + m + tail > n) for (unsigned j = 0; j < m; j++) { si += (double) I[j] * I[j]; so += (double) L[j] * L[j]; cnt++; }
		}
		gLast = sqrt (so / (si + 1e-30));
		return gLast;
	}
	// instantaneous gain (rms over 1 ms) after running n samples of the same sine
	double OutRail (void) { float o[64]; fx->GetOutputs (o); return o[8]; }
	double OutGr (void) { float o[64]; fx->GetOutputs (o); return o[9]; }
};

// time (ms) for the gain to cover 63.2% of the way from g0 to g1, measuring 1-ms windows
static double T63 (Run &r, double peakDb, double g0, double g1, unsigned maxMs = 3000)
{
	const unsigned w = (unsigned) (r.fs / 1000);
	for (unsigned ms = 1; ms <= maxMs; ms++)
	{
		const double g = r.Sine (w, peakDb, 1000.0, 4, w);
		if (g1 < g0 ? g <= g0 + 0.632 * (g1 - g0) : g >= g0 + 0.632 * (g1 - g0)) return (double) ms;
	}
	return -1;
}

int main (void)
{
	// 1. identity with Depth 0 and Ripple 0 (Custom): the output is bit-exact
	{
		Run r (Par (5, 0, 8, 40, -9, 0, 0, 0));
		bool same = true;
		for (unsigned b = 0; b < 4000; b++)
		{
			float L[4], R[4], I[4];
			for (int j = 0; j < 4; j++) { I[j] = L[j] = R[j] = 0.8f * (float) sin (0.05 * (b * 4 + j)); }
			int mono = 0;
			r.fx->Process (L, R, 4, &mono, r.par.p);
			for (int j = 0; j < 4; j++) if (L[j] != I[j] || R[j] != I[j]) same = false;
		}
		CHECK (same, "Depth 0 and Ripple 0: the output is identical to the input");
	}
	// 2. a quiet signal passes through
	{
		Run r (Par (0));
		const double g = r.Sine (48000, -45.0);
		CHECKF (g > 0.995 && g < 1.0001, "quiet signal (-45 dB) with the 5E3 / 5Y3 preset: gain %.4f", g);
	}
	// 3. steady-state depth at full power for Custom: gain = 1 - depth
	for (float depth : {4.0f, 10.0f, 20.0f})
	{
		Run r (Par (5, depth, 8, 40, -9, 0, 0, 0));
		const double g = r.Sine (48000 * 3, -9.0);
		CHECKF (fabs (g - (1.0 - depth / 100.0)) < 0.004, "depth %.0f%% at full power: settled gain %.4f (expected %.4f)", depth, g, 1.0 - depth / 100.0);
	}
	// 4. presets: the depth of each real situation at full power (gain = 1 - D)
	{
		const float want[5] = {13.0f, 1.7f, 3.8f, 26.4f, 5.8f};
		double gs[5];
		bool ok = true;
		for (int s = 0; s < 5; s++) { Run r (Par ((float) s)); gs[s] = r.Sine (48000 * 6, -9.0); if (fabs (gs[s] - (1.0 - want[s] / 100.0)) > 0.006) ok = false; }
		CHECKF (ok, "preset depths at full power: 5E3/5Y3 %.3f, 5E3 diodes %.3f, diodes+100 ohm %.3f, Marshall %.3f, AC30 %.3f", gs[0], gs[1], gs[2], gs[3], gs[4]);
		CHECKF (gs[1] > gs[2] && gs[2] > gs[4] && gs[4] > gs[0] && gs[0] > gs[3], "order of sag: diodes < diodes+100 ohm < AC30 < 5E3/5Y3 < Marshall");
	}
	// 5. dynamics: the gain falls in tens of ms and comes back more slowly, and a longer Recovery is slower
	{
		Run r (Par (5, 16, 10, 70, -9, 0, 0, 0));
		const double g0 = r.Sine (48000 / 2, -45.0);
		const double ta = T63 (r, -9.0, g0, 0.84);
		r.Sine (48000 * 2, -9.0);
		const double gl = r.gLast;
		const double tr = T63 (r, -45.0, gl, g0);
		CHECKF (ta > 3 && ta < 120, "attack: 63%% of the fall in %.0f ms (Attack set 10 ms, detector 4 ms, screen stage follows)", ta);
		CHECKF (tr > ta, "recovery %.0f ms is slower than the attack %.0f ms", tr, ta);
		printf ("         measured: attack t63 = %.0f ms, recovery t63 = %.0f ms (Custom 16%%, Attack 10, Recovery 70)\n", ta, tr);
		Run r2 (Par (5, 16, 10, 200, -9, 0, 0, 0));
		r2.Sine (48000 / 2, -45.0); r2.Sine (48000 * 3, -9.0);
		const double tr2 = T63 (r2, -45.0, r2.gLast, g0);
		CHECKF (tr2 > tr * 1.5, "Recovery 200 ms is slower than 70 ms: %.0f ms against %.0f ms", tr2, tr);
	}
	// 6. class A hardly sags
	{
		Run a (Par (5, 16, 8, 40, -9, 0, 0, 0)), b (Par (5, 16, 8, 40, -9, 1, 0, 0));
		const double ga = a.Sine (48000 * 3, -9.0), gb = b.Sine (48000 * 3, -9.0);
		CHECKF (1.0 - gb < 0.2 * (1.0 - ga) && 1.0 - gb > 0.0, "class A: drop %.4f against %.4f of class AB", 1.0 - gb, 1.0 - ga);
	}
	// 7. level: a higher "Full power at" means the same playing level loads the supply less
	{
		Run a (Par (5, 10, 8, 40, -20, 0, 0, 0)), b (Par (5, 10, 8, 40, -3, 0, 0, 0));
		const double ga = a.Sine (48000 * 3, -12.0), gb = b.Sine (48000 * 3, -12.0);
		CHECKF (ga < gb, "same signal: Full power at -20 dB sags (%.4f) more than at -3 dB (%.4f)", ga, gb);
	}
	// 8. ripple: AM sidebands at +-100 Hz (50 Hz mains) and +-120 Hz (60 Hz mains), index = Ripple at full load
	for (int mains = 0; mains < 2; mains++)
	{
		Run r (Par (5, 0, 8, 40, -9, 0, 3.0f, (float) mains));
		const double fr = mains ? 120.0 : 100.0;
		const unsigned N = 48000;
		r.Sine (48000, -9.0);
		std::vector<float> L (N), R (N);
		for (unsigned i = 0; i < N; i++) { L[i] = R[i] = (float) (0.35481338 * sin (r.ph)); r.ph += 2 * M_PI * 1000.0 / 48000.0; }
		for (unsigned b = 0; b < N; b += 4) { int mono = 0; r.fx->Process (&L[b], &R[b], 4, &mono, r.par.p); }
		auto gz = [&](double f) { double re = 0, im = 0; for (unsigned i = 0; i < N; i++) { const double w = 0.5 - 0.5 * cos (2 * M_PI * i / N); re += w * L[i] * cos (2 * M_PI * f * i / 48000.0); im += w * L[i] * sin (2 * M_PI * f * i / 48000.0); } return sqrt (re * re + im * im); };
		const double c0 = gz (1000.0), sb = 0.5 * (gz (1000.0 - fr) + gz (1000.0 + fr));
		const double rel = Db (sb / c0), exp_ = Db (0.03 / 2);
		CHECKF (fabs (rel - exp_) < 1.5, "ripple 3%% at %.0f Hz: sidebands %.1f dB below the carrier (expected %.1f)", fr, -rel, -exp_);
	}
	// 9. stereo: one supply for the two channels (loud left, quiet right: same gain); a mono block copies the left channel to the right
	{
		Run r (Par (5, 16, 8, 40, -9, 0, 0, 0));
		double sl = 0, sr = 0, il = 0, ir = 0;
		float L[4], R[4];
		double ph = 0;
		for (unsigned b = 0; b < 48000 * 3 / 4; b++)
		{
			for (int j = 0; j < 4; j++) { const float s = (float) sin (ph); ph += 2 * M_PI * 1000.0 / 48000.0; L[j] = 0.35481338f * s; R[j] = 0.01f * s; }
			const float i0 = L[0], i1 = R[0];
			int mono = 0;
			r.fx->Process (L, R, 4, &mono, r.par.p);
			if (b > 48000 * 3 / 4 - 1000) { sl += (double) L[0] * L[0]; sr += (double) R[0] * R[0]; il += (double) i0 * i0; ir += (double) i1 * i1; }
		}
		(void) sl; (void) sr; (void) il; (void) ir;
		float o[64]; r.fx->GetOutputs (o);
		CHECKF (o[8] < 95.0f, "stereo: the loud left channel loads the shared supply (rail %.1f%%)", o[8]);
		float L2[4] = {0.3f, -0.2f, 0.1f, 0.5f}, R2[4] = {9, 9, 9, 9};
		int mono = 1;
		r.fx->Process (L2, R2, 4, &mono, r.par.p);
		CHECK (R2[0] == L2[0] && R2[1] == L2[1] && R2[2] == L2[2] && R2[3] == L2[3], "mono block: the right channel copies the left one");
	}
	// 10. the output does not depend on the block size
	{
		const float p[10] = {2, 0, 0, 0, -9, 0, 0.5f, 0, 0, 0};
		std::vector<float> in (48000), o1 (48000), o2 (48000);
		for (unsigned i = 0; i < 48000; i++) in[i] = (float) (0.4 * sin (0.07 * i) * (i % 9000 < 4500 ? 1.0 : 0.05));
		for (int pass = 0; pass < 2; pass++)
		{
			CFx *fx = FxCreate (FxSag, 48000.0f);
			std::vector<float> &o = pass ? o2 : o1;
			const unsigned blk = pass ? 64 : 4;
			o = in;
			std::vector<float> R (o);
			for (unsigned b = 0; b < 48000; b += blk) { int mono = 0; fx->Process (&o[b], &R[b], blk, &mono, p); }
			delete fx;
		}
		CHECK (memcmp (o1.data (), o2.data (), sizeof (float) * 48000) == 0, "same output with blocks of 4 and 64 samples");
	}
	// 11. outputs: Rail and Gain Reduction agree with the measured gain
	{
		Run r (Par (5, 16, 8, 40, -9, 0, 0, 0));
		const double g = r.Sine (48000 * 3, -9.0);
		CHECKF (fabs (-Db (g) - r.OutGr ()) < 0.1, "Gain Reduction output %.2f dB (measured %.2f dB)", r.OutGr (), -Db (g));
		CHECKF (r.OutRail () < 100.0 && r.OutRail () > 70.0, "Rail output %.1f%%", r.OutRail ());
	}
	// 12. other sample rates
	for (float fs : {44100.0f, 96000.0f})
	{
		Run r (Par (5, 10, 8, 40, -9, 0, 0, 0), fs);
		const double g = r.Sine ((unsigned) (fs * 3), -9.0);
		CHECKF (fabs (g - 0.9) < 0.004, "sample rate %.0f Hz: settled gain %.4f", fs, g);
	}
	// 13. stability: extreme parameters, NaN and infinity in the input, then it works again
	{
		unsigned seed = 7; bool finite = true;
		for (int t = 0; t < 40; t++)
		{
			Par pa;
			for (int i = 0; i < 8; i++) { seed = seed * 1664525u + 1013904223u; pa.p[i] = ((float) (seed >> 8) / 16777216.0f - 0.25f) * 600.0f; }
			CFx *fx = FxCreate (FxSag, 48000.0f);
			for (int b = 0; b < 2000; b++)
			{
				float L[4], R[4];
				for (int j = 0; j < 4; j++) { seed = seed * 1664525u + 1013904223u; L[j] = R[j] = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 3.0f; }
				if (b == 500) { L[1] = NAN; R[2] = INFINITY; L[3] = -INFINITY; }
				int mono = (b & 1);
				fx->Process (L, R, 4, &mono, pa.p);
				for (int j = 0; j < 4; j++) if (!std::isfinite (L[j]) && !(b == 500)) finite = false;
			}
			float o[64]; fx->GetOutputs (o);
			if (!std::isfinite (o[8]) || !std::isfinite (o[9])) finite = false;
			delete fx;
		}
		CHECK (finite, "40 random parameter sets with NaN/inf bursts: the state and the outputs stay finite");
		Run r (Par (5, 10, 8, 40, -9, 0, 0, 0));
		float L[4] = {NAN, 0.1f, 0.2f, 0.1f}, R[4] = {0, 0, 0, 0};
		int mono = 0;
		r.fx->Process (L, R, 4, &mono, r.par.p);
		const double g = r.Sine (48000 * 3, -9.0);
		CHECKF (fabs (g - 0.9) < 0.004, "after a NaN sample the block works as before (gain %.4f)", g);
	}
	// 14. definition in the table
	{
		const TFxDef *d = FxDefByType (FxSag);
		CHECK (d != 0 && d->nParams == 10, "ADELSag is in the table with 10 parameters (8 controls + 2 outputs)");
		bool ok = d != 0;
		for (unsigned i = 0; d && i < d->nParams; i++)
		{
			const TFxParam &q = d->pParams[i];
			if (!(q.Def >= q.Min && q.Def <= q.Max) || q.Min >= q.Max) ok = false;
		}
		CHECK (ok, "every default is inside its range");
	}
	printf ("\nRESULT: %s (%d failures)\n", fails ? "FAILED" : "all ok", fails);
	return fails ? 1 : 0;
}
