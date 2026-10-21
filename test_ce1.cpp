// test_ce1.cpp - Chorus CE-1 Real: gains against the schematic's legend, filters, LFO, delay, comb, stereo, stability.
// g++ -std=c++17 -O2 -Ihost_shim -I../../../../include -I. -Iadel test_ce1.cpp fx_ce1.cpp -o test_ce1
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include "fxckt.h"

void Ce1BuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &b, int *hb, Ck::TCkt &c, int *hc, bool bMono);
double Ce1LfoHzForTest (double k);

static int fails = 0;
#define CHECK(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)
static const double FS = 48000.0;
static double Db (double v) { return 20 * log10 (v + 1e-18); }

static Ck::TCkt A, B, C;
static int hA[6], hB[6], hC[6];

static void Setup (double levelT, bool bMono)
{
	A = Ck::TCkt (); B = Ck::TCkt (); C = Ck::TCkt ();
	Ce1BuildForTest (A, hA, B, hB, C, hC, bMono);
	A.SetStep (1.0 / FS); B.SetStep (1.0 / FS); C.SetStep (1.0 / FS);
	A.SetR (hA[2], (1.0 - levelT) * 50.0e3 + 1.0); A.SetR (hA[3], levelT * 50.0e3 + 1.0);
	A.Start (); B.Start (); C.Start ();
}

// amplitude (peak V) of node "what" of A (0 = IC-1, 1 = Q2 emitter, 2 = BBD input) for a sine of amplitude amp at f
static double GainA (double f, double amp, int what)
{
	const int cyc = (int) (f * 0.3 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.4);
	for (int i = 0; i < settle; i++) A.Run (hA[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		A.Run (hA[0], amp * sin (th));
		const double v = what == 0 ? A.Probe (hA[4]) : (what == 1 ? A.Probe (hA[5]) : A.Probe (hA[1]));
		sn += v * sin (th); cs += v * cos (th);
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M;
}

static double GainC (double f, double amp)
{
	const int cyc = (int) (f * 0.3 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.4);
	for (int i = 0; i < settle; i++) C.Run (hC[0], amp * sin (2 * M_PI * f * i / FS));
	double sn = 0, cs = 0;
	for (int i = 0; i < M; i++)
	{
		const double th = 2 * M_PI * f * (settle + i) / FS;
		const double v = C.Run (hC[0], amp * sin (th)); sn += v * sin (th); cs += v * cos (th);
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M;
}

// output network: gain from the dry (0) or wet (1) input to the output (0 = jack M, 1 = jack N)
static double GainB (double f, double amp, int src, int outJack)
{
	const int cyc = (int) (f * 0.3 + 1), M = (int) (cyc * FS / f + 0.5), settle = (int) (FS * 0.4);
	double sn = 0, cs = 0;
	for (int i = 0; i < settle + M; i++)
	{
		const double th = 2 * M_PI * f * i / FS;
		B.SetV (hB[1], src == 1 ? amp * sin (th) : 0.0);
		const double v0 = B.Run (hB[0], src == 0 ? amp * sin (th) : 0.0);
		const double v = outJack == 0 ? v0 : B.Probe (hB[3]);
		if (i >= settle) { sn += v * sin (th); cs += v * cos (th); }
	}
	return 2.0 * sqrt (sn * sn + cs * cs) / M;
}

int main (void)
{
	// 1. IC-1 e Level
	Setup (1.0, true);
	{
		const double g = GainA (1000.0, 0.001, 0) / 0.001;
		printf ("IC-1 at 1 kHz, Level at maximum: gain %.1f (1 + 470/10 = 48 ideal)\n", g);
		CHECK (g > 40 && g < 50, "input amplifier gain %.1f", g);
	}
	// 2. nominal legend levels at 440 Hz: IC-1 4 Vpp (C), Q2 4 Vpp (D)
	{
		Setup (1.0, true);
		const double vc = GainA (440.0, 0.04, 0) * 2.0, vd = GainA (440.0, 0.04, 1) * 2.0, vb = GainA (440.0, 0.04, 2) * 2.0;
		printf ("with 80 mVpp at the input and Level at maximum, 440 Hz: IC-1 %.2f Vpp, Q2 %.2f Vpp, BBD input %.2f Vpp (legend: C 4 Vpp, D 4 Vpp)\n", vc, vd, vb);
		CHECK (vc > 2.0 && vc < 6.0, "IC-1 with 80 mVpp: %.2f Vpp (legend C: 4 Vpp)", vc);
	}
	// 3. prefilter: shelf of R13 // C06 with R5, then low-pass; BBD high-pass (C14 with R19 and R21)
	{
		Setup (1.0, true);
		printf ("prefilter (IC-1 -> Q2) and BBD input, dB relative to 1 kHz:\n   Hz     Q2    BBD\n");
		const double fr[9] = {100, 200, 400, 1000, 2000, 4000, 8000, 12000, 16000};
		double g1k = GainA (1000.0, 0.001, 1), b1k = GainA (1000.0, 0.001, 2);
		double q[9], b[9];
		for (int i = 0; i < 9; i++) { q[i] = Db (GainA (fr[i], 0.001, 1) / g1k); b[i] = Db (GainA (fr[i], 0.001, 2) / b1k); printf ("  %5.0f %6.1f %6.1f\n", fr[i], q[i], b[i]); }
		CHECK (q[8] < q[3] - 6.0 && b[0] < b[3] - 5.0, "the prefilter cuts the treble and the BBD input cuts the bass (R19 6.2K with C14)");
	}
	// 4. BBD calibration: from D (4 Vpp) to F (750 mVpp) at 440 Hz
	double gBbd;
	{
		Setup (1.0, true);
		const double ad = GainA (440.0, 0.04, 1), ab = GainA (440.0, 0.04, 2);		// amplitudes at Q2 and at the BBD input
		const double tf = GainC (440.0, 0.1) / 0.1;						// post-filter input -> F
		gBbd = (0.75 / 4.0) / ((ab / ad) * tf);
		printf ("Q2 -> BBD input %.3f, post-filter %.3f: for D 4 Vpp -> F 750 mVpp the BBD gain must be %.3f (%.1f dB)\n", ab / ad, tf, gBbd, Db (gBbd));
		CHECK (gBbd > 0.05 && gBbd < 5.0, "BBD gain derived from the legend: %.3f", gBbd);
	}
	// 4b. the legend's waveforms are 440 Hz square waves: same calibration with a square wave (IC-1 at 4 Vpp, BBD of gain 1 and no delay), peak-to-peak at steady state
	{
		Setup (1.0, true);
		double mxC = -1e9, mnC = 1e9, mxD = -1e9, mnD = 1e9, mxF = -1e9, mnF = 1e9;
		const double amp = 0.0211;					// 21.1 mV peak: IC-1 at about 4 Vpp square wave
		for (int i = 0; i < 48000; i++)
		{
			const double sq = ((i * 440) % 48000) < 24000 ? amp : -amp;
			A.Run (hA[0], sq);
			const double vc = A.Probe (hA[4]), vd = A.Probe (hA[5]), vb = A.Probe (hA[1]);
			const double vf = C.Run (hC[0], vb);
			if (i > 24000) { mxC = fmax (mxC, vc); mnC = fmin (mnC, vc); mxD = fmax (mxD, vd); mnD = fmin (mnD, vd); mxF = fmax (mxF, vf); mnF = fmin (mnF, vf); }
		}
		const double pc = mxC - mnC, pd = mxD - mnD, pf = mxF - mnF;
		printf ("440 Hz square wave, BBD of gain 1: IC-1 %.2f Vpp (legend C 4), Q2 %.2f Vpp (legend D 4), F %.3f Vpp (legend 0.75 with the real BBD)\n", pc, pd, pf);
		CHECK (pd > 0.6 * pc && pd < 1.4 * pc, "with a square wave the peak-to-peak of Q2 is close to that of IC-1 as in the legend (%.2f against %.2f)", pd, pc);
		gBbd = (0.75 * pc / 4.0) / pf;
		printf ("BBD gain from the legend with a square wave: %.3f (%.1f dB)\n", gBbd, Db (gBbd));
	}
	// 5. output network: dry from IC-1 and wet from Q10 (legend: M 240 mVpp for C 4 Vpp, N 350 mVpp for F 750 mVpp)
	{
		Setup (1.0, false);
		const double gd = GainB (440.0, 0.1, 0, 0), gw = GainB (440.0, 0.1, 1, 1);
		printf ("output network at 440 Hz, separate jacks: dry %.3f (4 Vpp -> %.0f mVpp, legend 240), wet %.3f (750 mVpp -> %.0f mVpp, legend 350)\n", gd / 0.1, gd / 0.1 * 4000.0, gw / 0.1, gw / 0.1 * 750.0);
		CHECK (gd / 0.1 * 4000.0 > 150 && gd / 0.1 * 4000.0 < 400 && gw / 0.1 * 750.0 > 200 && gw / 0.1 * 750.0 < 600, "dry and wet output levels close to the legend");
	}
	// 6. LFO
	{
		const double f0 = Ce1LfoHzForTest (0.0), f5 = Ce1LfoHzForTest (0.5), f1 = Ce1LfoHzForTest (1.0);
		printf ("LFO (Intensity 0 / 5 / 10): %.2f / %.2f / %.2f Hz\n", f0, f5, f1);
		CHECK (f0 > 0.2 && f0 < 1.5 && f1 > 2.0 && f1 < 10.0 && f5 > f0 && f1 > f5, "LFO frequency between %.2f and %.2f Hz", f0, f1);
		printf ("delay: clock 100 kHz -> %.2f ms, 31.25 kHz -> %.2f ms (512 stages)\n", 256.0 / 100.0e3 * 1e3, 256.0 / 31.25e3 * 1e3);
	}
	// 7. complete effect
	{
		CFx *fx = NewCe1 ((float) FS);
		const float p[5] = {1.0f, 9.0f, 0.0f, 0.0f, 1.8f};
		double mx = 0; int mono = 0;
		for (int blk = 0; blk < 400; blk++)
		{
			std::vector<float> l (120), r (120);
			for (int i = 0; i < 120; i++) { const double t = (blk * 120 + i) / FS; l[i] = r[i] = (float) (0.05 * sin (2 * M_PI * 330.0 * t)); }
			fx->Process (l.data (), r.data (), 120, &mono, p);
			for (int i = 0; i < 120; i++) { if (fabs (l[i]) > mx) mx = fabs (l[i]); }
		}
		CHECK (mx > 0.01 && mx < 4.0, "complete effect, mono: peak %.2f", mx);
		delete fx;
		// stereo: dry on the left, effect on the right
		fx = NewCe1 ((float) FS);
		const float q[5] = {5.0f, 9.0f, 0.0f, 1.0f, 1.8f};
		double eL = 0, eR = 0; mono = 0;
		for (int blk = 0; blk < 400; blk++)
		{
			std::vector<float> l (120), r (120);
			for (int i = 0; i < 120; i++) { const double t = (blk * 120 + i) / FS; l[i] = r[i] = (float) (0.05 * sin (2 * M_PI * 330.0 * t)); }
			fx->Process (l.data (), r.data (), 120, &mono, q);
			if (blk > 200) for (int i = 0; i < 120; i++) { eL += l[i] * l[i]; eR += r[i] * r[i]; }
		}
		printf ("stereo: left (dry) energy %.4g, right (effect) %.4g\n", eL, eR);
		CHECK (eL > 0 && eR > 0 && fabs (Db (eL / eR)) > 0.1, "the two outputs are different");
		delete fx;
		// stability at strong levels
		for (int s = 0; s < 2; s++) for (int lv = 0; lv <= 10; lv += 5)
		{
			fx = NewCe1 ((float) FS);
			const float z[5] = {(float) lv, (float) lv, (float) s, 0.0f, 5.63f}; double m2 = 0; int mo = 0;
			for (int blk = 0; blk < 300; blk++)
			{
				std::vector<float> l (120), r (120);
				for (int i = 0; i < 120; i++) { const double t = (blk * 120 + i) / FS; l[i] = r[i] = (float) (0.9 * sin (2 * M_PI * 220.0 * t)); }
				fx->Process (l.data (), r.data (), 120, &mo, z);
				for (int i = 0; i < 120; i++) if (fabs (l[i]) > m2) m2 = fabs (l[i]);
			}
			CHECK (m2 < 20.0, "sensitivity %d, Intensity/Level %d: peak %.2f", s, lv, m2);
			delete fx;
		}
		fx = NewCe1 ((float) FS);
		const float z[5] = {5.0f, 5.0f, 0.0f, 0.0f, 1.8f}; int mo = 0;
		std::vector<float> src (4800), l (4800), r (4800);
		for (int i = 0; i < 4800; i++) src[i] = (float) (0.05 * sin (2 * M_PI * 440.0 * i / FS));
		const auto t0 = std::chrono::steady_clock::now ();
		for (int b = 0; b < 300; b++) { l = src; r = src; fx->Process (l.data (), r.data (), 4800, &mo, z); }
		printf ("time: %.3f us per sample (frame)\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / (300 * 4800.0));
		delete fx;
	}
	// 8. pitch deviation of the wet branch (effect jack in stereo): with the default Sweep it does not exceed the CE-2 at maximum (51.5 cent measured with the same method: Rate 10, Depth 10), with the full excursion it is the previous one (142 cent)
	{
		double cents[2];
		const float sw[2] = {1.8f, 5.63f};
		for (int k = 0; k < 2; k++)
		{
			CFx *fx = NewCe1 ((float) FS);
			const float pp[5] = {10.0f, 9.0f, 0.0f, 1.0f, sw[k]}; int mono = 0;
			const int N = (int) (4.0 * FS);
			std::vector<float> w (N);
			for (int b = 0; b < N; b += 120)
			{
				std::vector<float> l (120), r (120);
				for (int i = 0; i < 120; i++) l[i] = r[i] = (float) (0.05 * sin (2 * M_PI * 1000.0 * (b + i) / FS));
				fx->Process (l.data (), r.data (), 120, &mono, pp);
				for (int i = 0; i < 120 && b + i < N; i++) w[b + i] = r[i];
			}
			delete fx;
			// instantaneous frequency from the phase of the correlation with 1 kHz over 20 ms windows shifted by 5 ms (after 1 s)
			std::vector<double> fr;
			const int W = (int) (0.020 * FS), H = (int) (0.005 * FS);
			double prev = 0; bool have = false;
			for (int a = (int) FS; a + W < N; a += H)
			{
				double sn = 0, cs = 0;
				for (int i = a; i < a + W; i++) { const double th = 2 * M_PI * 1000.0 * i / FS; sn += w[i] * sin (th); cs += w[i] * cos (th); }
				const double ph = atan2 (sn, cs);
				if (have) { double d = ph - prev; while (d > M_PI) d -= 2 * M_PI; while (d < -M_PI) d += 2 * M_PI; fr.push_back (1000.0 - d / (2 * M_PI) / (H / FS)); }
				prev = ph; have = true;
			}
			double mx = 0; for (double f : fr) { const double c = fabs (1200.0 * log2 (f / 1000.0)); if (c > mx) mx = c; }			cents[k] = mx;
			printf ("Intensity 10, Sweep %.2f ms: maximum deviation of the wet branch %.1f cent (%zu windows)\n", (double) sw[k], mx, fr.size ());
		}
		CHECK (cents[0] < 52.0, "Default Sweep: deviation %.1f cent, below that of the CE-2 at maximum (51.5)", cents[0]);
		CHECK (cents[1] > 2.0 * cents[0], "the full excursion is much stronger (%.1f cent)", cents[1]);
	}	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
