//
// fx_octaver.cpp - Octaver Real: Boss OC-2 Octave (1982-2003), built from the factory schematic (Hobby-Hour copy of the Boss service schematic, read from the figure on 2026-10-21): input JFET follower
// and gain stage, tracking path with its low-pass filters, peak detectors and comparators that set and reset a latch (an adaptive Schmitt trigger on the fundamental), two flip-flops that divide
// by two (IC7b) and by four (IC6), JFET choppers that multiply the signal by the divided square waves (op-amp stages with gain -0.57 / +0.50), the low-pass Sallen-Key filters that smooth them
// (about 325 Hz and 152 Hz) and the three-level mixer.
//
// Values read from the schematic: Q1 2SK30A follower, R1 10k, C1 0.022 uF, R3 1M, R2 10k, C2 1 uF, R4 1M; IC1 (TL022) gain stage R6 2.7k + C3 10 uF, R7 10k (gain 1 + R7 / (R6 + 1 / s C3), 4.7 above 5.9 Hz);
// direct level R59 100k. Tracking path (from the source of Q1): R8 22k, C21 0.01 uF, R9 330k, C5 2.2 nF, R5 330k, C4 220 pF (Sallen-Key low-pass, about 690 Hz, Q 1.6), R10 33k, C20 330 pF, R32 33k,
// C18 0.01 uF + R31 68k to ground (node m2); R38 10k from the node before R32 (m1) to the comparators; peak detectors with IC2/IC3 (D6 D7, D8 D9, R33 10k, R34 1k and R37 1k, hold C22 and C23 1 uF);
// comparators IC2b/IC3b: m1 against the held positive peak resets the latch, m1 against the held negative peak sets it (IC7a with D and CK grounded); IC7b divides the latch by two (octave 1)
// and IC6 (BA634) by two again (octave 2). Choppers: IC5 with R44 47k (inverting, pin 6), R45 47k (non-inverting, pin 5), R43 27k feedback, R49 100k from the non-inverting input and the JFET Q8 (or Q7)
// that short-circuits it, driven by the flip-flop through R47 1M / R48 1M, Ge diode D10 (D11) clamping the input node; octave 1 filter: R42 22k + C28 0.022 uF, R41 330k, C26 4.7 nF, R40 330k, C27 470 pF (IC4);
// octave 2 chopper input from the octave 1 output through C29 1 uF; its filter: R56 22k + C32 0.047 uF, R57 330k, C31 0.01 uF, R58 330k, C30 1 nF; levels: three 100k B potentiometers (VR3 direct, VR1 octave 1,
// VR2 octave 2) from the stage outputs to ground, the wipers joined; output Q2 emitter follower. Spec (Hobby-Hour): minimum operating input level -60 dBm at 250 Hz, maximum input -5 dBm.
//
// ASSUMED (not readable in the figure or not documented): the value of the small resistor drawn inside the dashed box of each level potentiometer (22k here), the gain of Q1 and Q2 as followers (0.92 and 0.97),
// the output resistance of the op-amps (100 ohm, for the Ge diode clamps: 1SS... D10/D11 1S188FM modeled with Is 1 uA, n 1.3), the rails of the op-amps (+-3.5 V around the bias point), the droop of the peak
// detectors (50 nA of input bias current on 1 uF = 0.05 V/s), ideal comparators and JFET switches (Ron 300 ohm), which flip-flop output drives which chopper (IC7b octave 1, IC6 octave 2: from the wires of
// the figure), a toggle on the rising edge of the previous output. The circuit runs at the sample rate with no oversampling. Not verified against recordings of the real pedal.
//
#include "fx.h"
#include "dsp_util.h"
#include "iirdesign.h"
#include <circle/util.h>

namespace
{

const double kVfs = 1.305;		// 0 dBFS = 1.305 V peak (same calibration as the other REAL pedals)

inline double Exp (double x)
{
	return (double) DbToLin ((float) (x < -80.0 ? -80.0 : (x > 80.0 ? 80.0 : x)) * 8.685889638f);
}

inline double Tanh (double x)
{
	if (x > 20.0) return 1.0;
	if (x < -20.0) return -1.0;
	const double e = Exp (2.0 * x);
	return (e - 1.0) / (e + 1.0);
}

// soft limit to +-lim (op-amp output stage): linear in the middle
inline double Lim (double x, double lim)
{
	return lim * Tanh (x / lim);
}

// unity-gain Sallen-Key low-pass preceded by an RC (Ra, Ca to ground): nodes n1 (after Ra), A (after Rb, with Cfb to the output), B (after Rc, with Cg to ground), output = B.
// H = 1 / D(s), D = (1 + s Ra Ca + Ra/Rb) (1 + s Cg (Rc + Rb) + s^2 Cg Cfb Rc Rb) - (Ra/Rb) (1 + s Cg Rc)
void SkDesign (TIir &f, double Ra, double Ca, double Rb, double Cfb, double Rc, double Cg, double fs)
{
	const double rho = Ra / Rb, alpha = 1.0 + rho, tau = Ra * Ca, p1 = Cg * (Rc + Rb), p2 = Cg * Cfb * Rc * Rb, q = Cg * Rc;
	const double D[4] = {alpha - rho, alpha * p1 + tau - rho * q, alpha * p2 + tau * p1, tau * p2};
	const double N[4] = {1.0, 0.0, 0.0, 0.0};
	f.Design (3, N, D, fs);
}

// Ge diode (1S188FM) from the bias point to the node, source through a small output resistance: clamps the negative excursions near -0.3 V
inline double GeClamp (double u)
{
	if (u > -0.05) return u;
	const double is = 1.0e-6, nvt = 1.3 * 0.02585, rs = 100.0;
	double v = u < -0.4 ? -0.4 : u;
	for (int i = 0; i < 4; i++)
	{
		const double e = Exp (-v / nvt);
		const double f = v - u - rs * is * (e - 1.0), df = 1.0 + rs * is * e / nvt;
		v -= f / df;
	}
	return v;
}

struct TChan
{
	TIir hpIn, hp2, gain, trk, m1, m2, sk1, sk2, hpC29;
	double pPlus, pMinus;
	int q, t2, t4;
	bool qPrev, t2Prev;
	double g1, g2;
};

class COctaver : public CFx
{
public:
	COctaver (float fRate) : m_fRate (fRate)
	{
		Design ();
	}

	unsigned Type (void) const { return FxOctaver; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		// p: 0 = Octave 1 level (%), 1 = Direct level (%), 2 = Level (dB), 3 = Octave 2 level (%)
		double x1 = pP[0] * 0.01, xd = pP[1] * 0.01, x2 = pP[3] * 0.01;
		x1 = x1 < 0 ? 0 : (x1 > 1 ? 1 : x1); xd = xd < 0 ? 0 : (xd > 1 ? 1 : xd); x2 = x2 < 0 ? 0 : (x2 > 1 ? 1 : x2);
		const double og = (1.0 / kVfs) * Exp ((double) pP[2] * 0.11512925);
		// mixer: three 100k potentiometers from the stage outputs (top) to ground (bottom), each wiper through a small resistor to the common node, which loads 1M (C6 and Q3)
		const double Rp = 100.0e3, Rs = 22.0e3, Gl = 1.0e-6;
		const double xs[3] = {xd, x1, x2};
		double gt[3], gb[3], gsum = Gl;
		for (int i = 0; i < 3; i++)
		{
			const double rt = (1.0 - xs[i]) * Rp + 1.0, rb = xs[i] * Rp + 1.0;
			// the wiper node sees: top (to the source), bottom (to ground) and the series resistor to the common node: Thevenin of the pot as seen from the series resistor
			const double rth = rt * rb / (rt + rb), vf = rb / (rt + rb);
			gt[i] = vf / (rth + Rs);		// contribution of the source to the common node current
			gsum += 1.0 / (rth + Rs);
			gb[i] = 0.0;
		}
		TChan &C = m_C;
		for (unsigned j = 0; j < n; j++)
		{
			const double vin = 0.5 * ((double) pL[j] + (double) pR[j]) * kVfs;
			// input: R1 + C1 with R3 into the gate, Q1 follower, C2 + R4, IC1 gain stage
			const double yq = C.hpIn.Run (vin) * 0.92;
			const double u = C.hp2.Run (yq);
			const double d1 = Lim (C.gain.Run (u), 3.5);
			// tracking path from the source of Q1
			const double vsk = C.trk.Run (yq);
			const double m1 = C.m1.Run (vsk), m2 = C.m2.Run (vsk);
			// peak detectors on m2 (hold 1 uF charged through 1k, droop 0.05 V/s), comparators on m1
			const double kc = 1.0 - Exp (-1.0 / (0.001 * (double) m_fRate)), dr = 0.05 / (double) m_fRate;
			if (m2 > C.pPlus) C.pPlus += (m2 - C.pPlus) * kc; else { C.pPlus -= dr; if (C.pPlus < 0.0) C.pPlus = 0.0; }
			if (m2 < C.pMinus) C.pMinus += (m2 - C.pMinus) * kc; else { C.pMinus += dr; if (C.pMinus > 0.0) C.pMinus = 0.0; }
			if (m1 > C.pPlus) C.q = 0; else if (m1 < C.pMinus) C.q = 1;
			// flip-flops: IC7b toggles on the rising edge of the latch, IC6 on the rising edge of IC7b
			if (C.q && !C.qPrev) C.t2 ^= 1;
			C.qPrev = C.q != 0;
			if (C.t2 && !C.t2Prev) C.t4 ^= 1;
			C.t2Prev = C.t2 != 0;
			// chopper 1 (Q8 driven by IC7b) on the direct signal after C24, octave 1 filter
			const double ch1 = Lim (Chop (GeClamp (d1), C.t2), 3.5);
			const double o1 = C.sk1.Run (ch1);
			// chopper 2 (Q7 driven by IC6) on the octave 1 output through C29, octave 2 filter
			const double ch2 = Lim (Chop (GeClamp (C.hpC29.Run (o1)), C.t4), 3.5);
			const double o2 = C.sk2.Run (ch2);
			// mixer and output follower
			const double src[3] = {d1, o1, o2};
			double w = 0.0;
			for (int i = 0; i < 3; i++) w += gt[i] * src[i];
			w /= gsum;
			double y = Lim (w * 0.97, 3.5);
			pL[j] = pR[j] = (float) (y * og);
		}
		(void) pMono;
		*pMono = 1;
	}

	// filters for the tests: 0 = tracking Sallen-Key chain, 1 = m1, 2 = m2, 3 = octave 1 filter, 4 = octave 2 filter, 5 = IC1 gain stage, 6 = input high-pass
	double Filter (int which, double u) { TIir &f = which == 0 ? m_C.trk : (which == 1 ? m_C.m1 : (which == 2 ? m_C.m2 : (which == 3 ? m_C.sk1 : (which == 4 ? m_C.sk2 : (which == 5 ? m_C.gain : m_C.hpIn))))); return f.Run (u); }
	void ResetFilters (void) { Design (); }

private:
	// op-amp stage with the JFET switch: on (gate high) the non-inverting input is grounded through Ron (gain -R43/R44), off it receives R49 / (R45 + R49) of the signal
	static double Chop (double v, int on)
	{
		const double R43 = 27.0e3, R44 = 47.0e3, R45 = 47.0e3, R49 = 100.0e3, Ron = 300.0;
		const double gInv = -R43 / R44;
		const double fn = on ? Ron / (R45 + Ron) : R49 / (R45 + R49);
		return v * (gInv + (1.0 + R43 / R44) * fn);
	}

	void Design (void)
	{
		const double fs = (double) m_fRate;
		TChan &C = m_C;
		// input: high-pass (R1 + R3) C1
		{ const double tau = (10.0e3 + 1.0e6) * 0.022e-6; const double b[2] = {0.0, tau}, a[2] = {1.0, tau}; C.hpIn.Design (1, b, a, fs); }
		// C2 with R4 1M: high-pass
		{ const double tau = 1.0e6 * 1.0e-6; const double b[2] = {0.0, tau}, a[2] = {1.0, tau}; C.hp2.Design (1, b, a, fs); }
		// IC1: 1 + R7 s C3 / (1 + s R6 C3)
		{ const double t6 = 2.7e3 * 10.0e-6, c7 = 10.0e3 * 10.0e-6; const double b[2] = {1.0, t6 + c7}, a[2] = {1.0, t6}; C.gain.Design (1, b, a, fs); }
		// tracking Sallen-Key chain
		SkDesign (C.trk, 22.0e3, 0.01e-6, 330.0e3, 2.2e-9, 330.0e3, 220.0e-12, fs);
		// m1 and m2: after R10 (C20 to ground) and R32 with (R31 + C18) to ground
		{
			const double R10 = 33.0e3, R31 = 68.0e3, R32 = 33.0e3, C18 = 0.01e-6, C20 = 330.0e-12;
			const double t31 = R31 * C18, T = t31 + R32 * C18;
			const double d[3] = {1.0, T + R10 * (C20 + C18), R10 * C20 * T};
			const double n1[3] = {1.0, T, 0.0}, n2[3] = {1.0, t31, 0.0};
			C.m1.Design (2, n1, d, fs);
			C.m2.Design (2, n2, d, fs);
		}
		SkDesign (C.sk1, 22.0e3, 0.022e-6, 330.0e3, 4.7e-9, 330.0e3, 470.0e-12, fs);
		SkDesign (C.sk2, 22.0e3, 0.047e-6, 330.0e3, 10.0e-9, 330.0e3, 1.0e-9, fs);
		// C29 with the 47k input of the second chopper: high-pass
		{ const double tau = 47.0e3 * 1.0e-6; const double b[2] = {0.0, tau}, a[2] = {1.0, tau}; C.hpC29.Design (1, b, a, fs); }
		C.pPlus = C.pMinus = 0.0;
		C.q = C.t2 = C.t4 = 0;
		C.qPrev = C.t2Prev = false;
	}

	float m_fRate;
	TChan m_C;
};

}

CFx *NewOctaver (float fRate)
{
	return new COctaver (fRate);
}

// hooks for the tests
void OctaverFilterForTest (CFx *pFx, int which, const float *pIn, float *pOut, unsigned n)
{
	COctaver *p = static_cast<COctaver *> (pFx);
	p->ResetFilters ();
	for (unsigned i = 0; i < n; i++) pOut[i] = (float) p->Filter (which, (double) pIn[i]);
}
