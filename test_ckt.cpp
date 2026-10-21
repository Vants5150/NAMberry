// test_ckt.cpp - tests of the circuit engine (ckt.h) on the PC: RC filter, diode limiter against the exact solution, transistor amplifier (bias), op-amp (gain, output limit).
// g++ -std=c++17 -O1 test_ckt.cpp -o test_ckt
#include <cstdio>
#include <cmath>
#include <chrono>
#include "ckt.h"

static int fails = 0;
#define CHECK(c, ...) do { char b_[240]; snprintf (b_, sizeof b_, __VA_ARGS__); if (!(c)) { printf ("FAILED: %s\n", b_); fails++; } else printf ("ok       %s\n", b_); } while (0)

static const double FS = 48000.0;

int main (void)
{
	// 1. RC low-pass: R = 1 k, C = 159.155 nF (1 kHz)
	{
		Ck::TCkt c;
		const int in = c.Node (), out = c.Node ();
		const int vs = c.V (in, 0, 0.0);
		c.R (in, out, 1000.0);
		c.C (out, 0, 159.155e-9);
		c.SetOut (out); c.SetStep (1.0 / FS);
		c.Start ();
		double mx = 0; const int N = 48000;
		for (int i = 0; i < N; i++)
		{
			const double y = c.Run (vs, sin (2 * M_PI * 1000.0 * i / FS));
			if (i > N / 2 && fabs (y) > mx) mx = fabs (y);
		}
		CHECK (fabs (mx - 0.7071) < 0.012, "RC at 1 kHz: amplitude %.4f (expected 0.7071 with the trapezoidal rule's frequency warping)", mx);
	}
	// 2. diode limiter: 1 k in series, two 1N914s in antiparallel to ground; input 1 V peak at 100 Hz
	{
		const double Is = 2.52e-9, nVt = 1.752 * 0.02585;
		Ck::TCkt c;
		const int in = c.Node (), out = c.Node ();
		const int vs = c.V (in, 0, 0.0);
		c.R (in, out, 1000.0);
		c.Diode (out, 0, Is, nVt); c.Diode (0, out, Is, nVt);
		c.SetOut (out); c.SetStep (1.0 / FS);
		c.Start ();
		double pk = 0;
		for (int i = 0; i < 48000; i++) { const double y = c.Run (vs, sin (2 * M_PI * 100.0 * i / FS)); if (y > pk) pk = y; }
		// exact: (1 - V)/1000 = Is (e^(V/nVt) - 1)
		double lo = 0, hi = 1;
		for (int k = 0; k < 100; k++) { const double v = 0.5 * (lo + hi); if ((1 - v) / 1000.0 > Is * (exp (v / nVt) - 1)) lo = v; else hi = v; }
		CHECK (fabs (pk - lo) < 0.004, "diode limiter: peak %.4f V, exact %.4f V", pk, lo);
	}
	// 3. common-emitter stage: 9 V, Rb1 100 k, Rb2 22 k, Rc 4.7 k, Re 1 k, 2N5088-like (Is 6.7 fA? we use 1e-14, beta 300)
	{
		Ck::TCkt c;
		const int vcc = c.Node (), in = c.Node (), base = c.Node (), col = c.Node (), em = c.Node (), out = c.Node ();
		c.V (vcc, 0, 9.0);
		const int vs = c.V (in, 0, 0.0);
		c.C (in, base, 1e-6);
		c.R (vcc, base, 100e3); c.R (base, 0, 22e3);
		c.R (vcc, col, 4.7e3); c.R (em, 0, 1e3);
		c.Bjt (col, base, em, 1e-14, 300.0, 5.0, 0.02585, false);
		c.C (col, out, 10e-6); c.R (out, 0, 100e3);
		c.C (em, 0, 100e-6);
		c.SetOut (out); c.SetStep (1.0 / FS);
		c.Start ();
		const double vb = c.Probe (base), vc = c.Probe (col), ve = c.Probe (em);
		const double ic = (9.0 - vc) / 4.7e3, ie = ve / 1e3;
		CHECK (fabs (ic / ie - 300.0 / 301.0) < 0.01 && vc > 3.0 && vc < 8.5, "bias: Vb %.3f Vc %.3f Ve %.3f V, Ic/Ie %.4f", vb, vc, ve, ic / ie);
		// gain at 1 kHz (decoupled emitter): about gm Rc with gm = Ic/Vt, reduced by the load and the input divider
		double mx = 0; const int N = 48000;
		for (int i = 0; i < N; i++) { const double y = c.Run (vs, 0.002 * sin (2 * M_PI * 1000.0 * i / FS)); if (i > N / 2 && fabs (y) > mx) mx = fabs (y); }
		const double gm = ic / 0.02585, av = gm * (4.7e3 * 100e3 / (4.7e3 + 100e3));
		CHECK (mx / 0.002 > 0.4 * av && mx / 0.002 < 1.05 * av, "gain at 1 kHz %.1f (gm Rc = %.1f)", mx / 0.002, av);
	}
	// 4. non-inverting op-amp, gain 10: 1 k to ground and 9 k feedback, 9 V supply with 4.5 V reference
	{
		Ck::TCkt c;
		const int vcc = c.Node (), vref = c.Node (), in = c.Node (), inn = c.Node (), out = c.Node ();
		c.V (vcc, 0, 9.0);
		c.R (vcc, vref, 10e3); c.R (vref, 0, 10e3);
		const int vs = c.V (in, vref, 0.0);
		c.R (out, inn, 9e3); c.R (inn, vref, 1e3);
		c.Opamp (in, inn, out, vref, 1.0e6, 1.0e5, 13e6, 4.0, 50.0);
		c.SetOut (out); c.SetStep (1.0 / FS);
		c.Start ();
		double mx = 0; const int N = 48000;
		for (int i = 0; i < N; i++) { const double y = c.Run (vs, 0.05 * sin (2 * M_PI * 5000.0 * i / FS)); const double d = y - c.Probe (vref); if (i > N / 2 && fabs (d) > mx) mx = fabs (d); }
		CHECK (fabs (mx / 0.05 - 10.0 / sqrt (1 + 0.0025)) < 0.25, "op-amp: gain at 5 kHz %.3f (expected 9.99)", mx / 0.05);
		double pk = 0;
		for (int i = 0; i < N; i++) { const double y = c.Run (vs, 1.0 * sin (2 * M_PI * 200.0 * i / FS)); const double d = y - c.Probe (vref); if (fabs (d) > pk) pk = fabs (d); }
		CHECK (pk > 3.5 && pk < 4.2, "op-amp: swing limited to %.2f V (rail 4)", pk);
	}
	// 5. calculation speed: transistor stage with 2 diodes like the Big Muff (nonlinear ports: 4)
	{
		Ck::TCkt c;
		const int vcc = c.Node (), in = c.Node (), base = c.Node (), col = c.Node (), em = c.Node (), out = c.Node ();
		c.V (vcc, 0, 9.0);
		const int vs = c.V (in, 0, 0.0);
		c.R (in, base, 10e3); c.R (col, base, 470e3);
		c.R (vcc, col, 10e3); c.R (em, 0, 150);
		c.Bjt (col, base, em, 1e-14, 300.0, 5.0, 0.02585, false);
		const int dm = c.Node ();
		c.C (base, dm, 1e-6);
		c.Diode (dm, col, 2.52e-9, 1.752 * 0.02585); c.Diode (col, dm, 2.52e-9, 1.752 * 0.02585);
		c.C (col, out, 1e-6); c.R (out, 0, 100e3);
		c.SetOut (out); c.SetStep (1.0 / FS);
		c.Start ();
		const auto t0 = std::chrono::steady_clock::now ();
		double acc = 0; const int N = 480000; int itmax = 0;
		for (int i = 0; i < N; i++) { acc += c.Run (vs, 0.05 * sin (2 * M_PI * 440.0 * i / FS)); if (c.Iters () > itmax) itmax = c.Iters (); }
		const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now () - t0).count () / N;
		printf ("time: %.3f us per sample, at most %d Newton iterations (%g)\n", us, itmax, acc);
		CHECK (itmax < 40, "Newton converges (at most %d iterations)", itmax);
	}
	// 6. inductor: R 1k in series and L 100 mH to ground: high-pass with cutoff at R/(2 pi L) = 1.59 kHz; in DC the output is at 0 V
	{
		Ck::TCkt c;
		const int in = c.Node (), out = c.Node ();
		const int vs = c.V (in, 0, 0.0);
		c.R (in, out, 1000.0);
		c.Ind (out, 0, 0.1);
		c.SetOut (out); c.SetStep (1.0 / FS);
		c.Start ();
		double mx = 0; const int N = 48000;
		for (int i = 0; i < N; i++) { const double y = c.Run (vs, sin (2 * M_PI * 1591.55 * i / FS)); if (i > N / 2 && fabs (y) > mx) mx = fabs (y); }
		CHECK (fabs (mx - 0.7071) < 0.03, "RL high-pass at 1.59 kHz: amplitude %.4f (expected 0.7071)", mx);
		double dc = 0; for (int i = 0; i < 4800; i++) dc = c.Run (vs, 0.0);
		CHECK (fabs (dc) < 1e-6, "inductor: output at rest %.3g V", dc);
	}
	printf (fails ? "%d TESTS FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
