//
// fx_mflanger.cpp - Flanger Micro Real: MXR Micro Flanger (schematic 152-4001, revision 105, 512-stage Reticon SAD512D BBD): input with pre-emphasis and three-diode limiter, BBD input filter and output filter,
// mixer with de-emphasis and REGEN feedback simulated from the NETLIST (ckt.h); LFO, Q4/Q5 current mirror, clock oscillator and BBD line simulated numerically with the formulas of their circuit.
//
// Sources (2026-10-19, https://www.tonehome.de/mxr/micro-flanger/ : original MXR "Micro Flanger" schematic 152-4001, low-resolution scan; values read from the drawing):
//  - Input: R1 10K, C1 0.05 uF, U1A (1/2 TL062) non-inverting with R6 15K + C3 150 pF in feedback and R7 1.5K + C4 0.01 uF to ground (gain 1 up to about 10 kHz, 11 above: pre-emphasis); bias from the
//    divider R3 510K / R4 250K (trimmer) / R5 270K with R2 1M and C2 0.33 uF.
//  - Limiter: three 1N914 diodes in SERIES in the signal from U1A's output (CR) with R11 20K to ground: the quiescent current keeps them conducting and the signal that exceeds them in the negative direction is cut.
//  - BBD input filter: R12 15K, C5 820 pF, R13 47K, C6 820 pF (bootstrap on Q1's emitter), R14 47K, C7 82 pF, Q1 ZTX109C (follower, R15 47K), R16 200K towards pin 6 of the SAD512D.
//  - Output filter: R17 15K, C8 820 pF, R19 47K, C9 820 pF (bootstrap on Q2), R20 47K, C10 82 pF, Q2 ZTX109C (follower, R21 47K); R18 47K on the second output pin.
//  - Mixer U1B: dry from U1A via R10 150K, effect from Q2 via C11 0.06 uF and R22 150K; feedback R24 150K with C13 20 pF and with C12 1 nF + R23 15K (complementary de-emphasis); output C14 0.33 uF, R26 470; R25 47K.
//  - REGEN: from Q2's emitter via C16 5 nF, R9 500K (rheostat) and R8 47K to U1A's INVERTING input.
//  - LFO (U4, TL061): comparator with hysteresis R36 470K / R37 1M / R38 1M (thresholds on the + pin) and capacitor C18 15 uF charged from the output through R35 47K and the RATE rheostat R32 500K in parallel with R33 200K,
//    biased by R30 1M towards V+; the voltage on C18 (exponential ramps between the two thresholds) reaches R41 1M.
//  - Clock: mirror Q4 (diode-connected, R39 1M from V+) and Q5, with R40 22K and C20 3.3 uF on Q5's base and R41 1M from the LFO; Q5 discharges the capacitor C15 50 pF of the two-inverter 4069 oscillator (U2) that Q3 2N4126
//    (driven by R27 30K and R28 10K) recharges at every switching: frequency = Ic5 / (C15 (V+/2 + 0.6)), delay = 512 / (2 fclock).
//  - SAD1024 datasheet (same family): clock from 1.5 kHz to over 1.5 MHz (https://www.datasheetcafe.com/sad1024-pdf-datasheet-14598/): consistent with the 150 kHz - 1.5 MHz that come out of the circuit.
//
// Model and ASSUMPTIONS (NOT from the schematic): V+ = 8.6 V (9 V battery minus the 1N270 diode); TL061 output between 0.3 V and V+ - 1.5 V; ZTX109C transistors with beta 500 and thermal V 25.85 mV, Is equal for Q4 and Q5; CMOS threshold
// at V+/2 and clock dead time 100 ns (switching of the three inverters); ideal BBD with gain 1, interpolated delay (Lagrange), no clock noise; BBD POLARITY not documented (parameter);
// trimmer R4 at half travel; LFO ramp without the small effect of C17; R4 and potentiometers linear. With the schematic's values the delay comes out from 0.17 to 1.7 ms (clock from 150 kHz to 1.5 MHz) and the LFO cycle lasts from 1.6 s (RATE at maximum) to 7.3 s (at minimum).
// Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kVp = 8.6, kVt = 0.02585, kIs = 1.0e-14, kBeta = 500.0;
const double kBias = kVp * (270.0e3 + 125.0e3) / (510.0e3 + 250.0e3 + 270.0e3);		// audio bias (trimmer R4 at half)
const double kHi = kVp - 1.5, kLo = 0.3;						// TL061 output
const double kDead = 100.0e-9;								// clock dead time
const unsigned kBuf = 512;
const int kStep = 64;									// samples between two updates of the controls

void Op (Ck::TCkt &c, int inp, int inn, int out)
{
	c.Gc (0, out, inp, inn, 1.0);
	c.R (out, 0, 1.0e6);
}

// A: input -> U1A -> three-diode limiter -> input filter -> BBD input; h[0] input, h[1] BBD input, h[2] U1A output (dry), h[3] feedback generator, h[4] R9 (REGEN)
void BuildA (Ck::TCkt &c, int *h)
{
	const int ix = c.Node (), in = c.Node (), p = c.Node (), vb = c.Node (), m = c.Node (), o = c.Node (), k = c.Node ();
	const int rg = c.Node (), r1 = c.Node (), r2 = c.Node ();
	const int d1 = c.Node (), d2 = c.Node (), f1 = c.Node (), g1 = c.Node (), g2 = c.Node (), g3 = c.Node (), bi = c.Node ();
	h[0] = c.V (ix, 0, 0.0);
	c.V (vb, 0, kBias);
	c.R (ix, in, 10.0e3);						// R1
	c.C (in, p, 0.05e-6);						// C1
	c.R (p, vb, 1.0e6);						// R2
	Op (c, p, m, o);						// U1A
	c.R (m, o, 15.0e3); c.C (m, o, 150.0e-12);			// R6, C3
	c.R (m, k, 1.5e3); c.C (k, 0, 0.01e-6);			// R7, C4
	h[3] = c.V (rg, 0, 0.0);					// feedback from Q2's emitter
	c.C (rg, r1, 5.0e-9);						// C16
	h[4] = c.R (r1, r2, 250.0e3);					// R9 (REGEN)
	c.R (r2, m, 47.0e3);						// R8
	c.Diode (o, d1, 2.52e-9, 1.752 * kVt);				// CR (1N914 x 3 in series)
	c.Diode (d1, d2, 2.52e-9, 1.752 * kVt);
	c.Diode (d2, f1, 2.52e-9, 1.752 * kVt);
	c.R (f1, 0, 20.0e3);						// R11
	c.R (f1, g1, 15.0e3); c.C (g1, 0, 820.0e-12);			// R12, C5
	c.R (g1, g2, 47.0e3); c.C (g2, bi, 820.0e-12);			// R13, C6 (bootstrap)
	c.R (g2, g3, 47.0e3); c.C (g3, 0, 82.0e-12);			// R14, C7
	Op (c, g3, bi, bi);						// Q1
	c.R (bi, 0, 47.0e3);						// R15
	c.SetOut (bi);
	h[1] = bi; h[2] = o;
}

// C: BBD output -> filter -> Q2's emitter; h[0] generator, h[1] output
void BuildC (Ck::TCkt &c, int *h)
{
	const int s = c.Node (), n1 = c.Node (), n2 = c.Node (), n3 = c.Node (), e = c.Node ();
	h[0] = c.V (s, 0, 0.0);
	c.R (s, n1, 15.0e3); c.C (n1, 0, 820.0e-12);			// R17, C8
	c.R (n1, n2, 47.0e3); c.C (n2, e, 820.0e-12);			// R19, C9 (bootstrap)
	c.R (n2, n3, 47.0e3); c.C (n3, 0, 82.0e-12);			// R20, C10
	Op (c, n3, e, e);						// Q2
	c.R (e, 0, 47.0e3);						// R21
	c.SetOut (e);
	h[1] = e;
}

// D: mixer U1B; h[0] dry, h[1] effect, h[2] output
void BuildD (Ck::TCkt &c, int *h)
{
	const int vd = c.Node (), vw = c.Node (), w1 = c.Node (), p = c.Node (), o = c.Node (), k = c.Node (), q = c.Node (), out = c.Node ();
	h[0] = c.V (vd, 0, 0.0);
	h[1] = c.V (vw, 0, 0.0);
	c.R (vd, p, 150.0e3);						// R10
	c.C (vw, w1, 0.06e-6); c.R (w1, p, 150.0e3);			// C11, R22
	c.R (p, o, 150.0e3);						// R24
	c.C (p, o, 20.0e-12);						// C13
	c.C (p, k, 1.0e-9); c.R (k, o, 15.0e3);			// C12, R23
	Op (c, 0, p, o);						// U1B
	c.R (o, 0, 47.0e3);						// R25
	c.C (o, q, 0.33e-6); c.R (q, out, 470.0);			// C14, R26
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[2] = out;
}

// LFO and clock: state and step
struct TCtl
{
	double va, out, m, b4, ic5;

	void Init (void)
	{
		out = kHi; va = 0.5 * (Thr (kLo) + Thr (kHi)); m = 0.6; b4 = 0.55; ic5 = 1.0e-5;
		for (int i = 0; i < 700; i++) Mirror (0.005);		// mirror steady state
	}
	// hysteresis thresholds on U4's + pin (R36 470K, R37 1M, R38 1M)
	static double Thr (double vo) { return (vo / 470.0e3 + kVp / 1.0e6) / (1.0 / 470.0e3 + 2.0e-6); }
	void Mirror (double dt)
	{
		const double R39 = 1.0e6, R40 = 22.0e3, R41 = 1.0e6, C20 = 3.3e-6;
		for (int it = 0; it < 4; it++)
		{
			const double ic = kIs * Ck::Exp (b4 / kVt);
			const double f = (kVp - b4) / R39 + (m - b4) / R40 - ic * (1.0 + 1.0 / kBeta);
			const double df = -1.0 / R39 - 1.0 / R40 - ic / kVt * (1.0 + 1.0 / kBeta);
			b4 -= f / df;
		}
		ic5 = kIs * Ck::Exp (m / kVt);
		m += ((va - m) / R41 - ic5 / kBeta - (m - b4) / R40) / C20 * dt;
		if (m < 0.3) m = 0.3; else if (m > 0.8) m = 0.8;
	}
	// one step of dt seconds; rch = charging resistance of C18 (R35 + R32 // R33); returns the clock (Hz)
	CK_NOINLINE double Step (double dt, double rch)
	{
		const double g = 1.0 / rch + 1.0 / 1.0e6, vth = (out / rch + kVp / 1.0e6) / g;
		va += (vth - va) * (1.0 - Ck::Exp (-dt * g / 15.0e-6));
		if (out == kHi && va > Thr (kHi)) out = kLo;
		else if (out == kLo && va < Thr (kLo)) out = kHi;
		Mirror (dt);
		const double f0 = ic5 / (50.0e-12 * (kVp * 0.5 + 0.6));
		return 1.0 / (1.0 / f0 + kDead);
	}
};

double RateOhm (double x)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	const double r = 500.0e3 * (1.0 - x);					// RATE R32 500K (linear, assumed)
	return 47.0e3 + r * 200.0e3 / (r + 200.0e3);
}

class CMFlanger : public CFx
{
public:
	CMFlanger (float fRate) : m_fRate (fRate), m_bInit (false), m_nPol (0), m_nCnt (0), m_nW (0), m_dD (20.0), m_dDs (20.0), m_dForce (0.0), m_dDcDry (0.0), m_dDcB (0.0), m_bFirst (true), m_dWet (0.0)
	{
		for (unsigned i = 0; i < kBuf; i++) m_Buf[i] = 0.0f;
	}

	unsigned Type (void) const { return FxMicroFlanger; }

	void Force (double fclk) { m_dForce = fclk; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const int nPol = pP[2] > 0.5f ? 1 : 0;
		if (!m_bInit || nPol != m_nPol)
		{
			m_nPol = nPol;
			Restart ();
			m_bInit = true;
		}
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
			*pMono = 0;
		}
		double rg = pP[1] * 0.1;
		rg = rg < 0 ? 0 : (rg > 1 ? 1 : rg);
		m_A.SetR (m_hA[4], 500.0e3 * (1.0 - rg) + 1.0);				// REGEN R9 500K: at maximum the resistance goes to zero
		const double rch = RateOhm ((double) pP[0] * 0.1);
		const double dt = 1.0 / (double) m_fRate, vfs = (double) CKT_VFS, og = 1.0 / (double) CKT_VFS, sgn = m_nPol ? -1.0 : 1.0;
		const double kd = 1.0 - Ck::Exp (-1.0 / 40.0);
		const double kdc = 1.0 - Ck::Exp (-6.2831853 * 1.0 * dt);
		for (unsigned j = 0; j < n; j++)
		{
			if (m_nCnt == 0)
			{
				const double fc = m_Ctl.Step (dt * (double) kStep, rch);
				const double f = m_dForce > 0.0 ? m_dForce : fc;
				double d = 256.0 / f * (double) m_fRate;			// delay = 512 / (2 fclock)
				if (d < 3.0) d = 3.0;
				if (d > (double) kBuf - 8.0) d = (double) kBuf - 8.0;
				m_dD = d;
				m_nCnt = kStep;
			}
			m_nCnt--;
			m_dDs += (m_dD - m_dDs) * (m_dForce > 0.0 ? 1.0 : kd);
			const double x = 0.5 * ((double) pL[j] + (double) pR[j]) * vfs;
			m_A.SetV (m_hA[3], m_dWet);
			double vi = m_A.Run (m_hA[0], x), dry = m_A.Probe (m_hA[2]) - m_dDcDry;
			if (!(vi > -80.0 && vi < 80.0 && dry > -80.0 && dry < 80.0)) { Restart (); vi = 0.0; dry = 0.0; }
			if (m_bFirst) { m_dDcB = vi; m_bFirst = false; }
			m_dDcB += (vi - m_dDcB) * kdc;
			m_Buf[m_nW] = (float) (vi - m_dDcB);
			double rp = (double) m_nW - m_dDs;
			while (rp < 0.0) rp += (double) kBuf;
			const unsigned i1 = (unsigned) rp, i0 = (i1 + kBuf - 1) % kBuf, i2 = (i1 + 1) % kBuf, i3 = (i1 + 2) % kBuf;
			const double fr = rp - (double) i1;
			const double y0 = m_Buf[i0], y1 = m_Buf[i1], y2 = m_Buf[i2], y3 = m_Buf[i3];
			const double bb = y1 + 0.5 * fr * (y2 - y0 + fr * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + fr * (3.0 * (y1 - y2) + y3 - y0)));
			m_nW = (m_nW + 1) % kBuf;
			const double wet = m_C.Run (m_hC[0], sgn * bb);
			m_dWet = wet;
			m_D.SetV (m_hD[1], wet);
			double y = m_D.Run (m_hD[0], dry);
			if (!(y > -50.0 && y < 50.0 && wet > -80.0 && wet < 80.0)) { Restart (); y = 0.0; }
			pL[j] = pR[j] = (float) (y * og);
		}
	}

private:
	void Restart (void)
	{
		const double st = 1.0 / (double) m_fRate;
		m_A.Clear (); BuildA (m_A, m_hA); m_A.SetStep (st); m_A.Start ();
		m_C.Clear (); BuildC (m_C, m_hC); m_C.SetStep (st); m_C.Start ();
		m_D.Clear (); BuildD (m_D, m_hD); m_D.SetStep (st); m_D.Start ();
		for (int i = 0; i < 4; i++) m_A.Run (m_hA[0], 0.0);
		m_dDcDry = m_A.Probe (m_hA[2]);
		m_Ctl.Init ();
		m_dWet = 0.0; m_bFirst = true; m_nCnt = 0;
		for (unsigned i = 0; i < kBuf; i++) m_Buf[i] = 0.0f;
	}

	float m_fRate;
	bool m_bInit;
	int m_nPol, m_nCnt;
	unsigned m_nW;
	double m_dD, m_dDs, m_dForce, m_dDcDry, m_dDcB;
	bool m_bFirst;
	double m_dWet;
	TCtl m_Ctl;
	float m_Buf[kBuf];
	int m_hA[6], m_hC[6], m_hD[6];
	Ck::TCkt m_A, m_C, m_D;
};

}

CFx *NewMicroFlanger (float fRate)
{
	return new CMFlanger (fRate);
}

void MflangerBuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &c, int *hc, Ck::TCkt &d, int *hd) { BuildA (a, ha); BuildC (c, hc); BuildD (d, hd); }
void MflangerForceForTest (CFx *pFx, double fclk) { static_cast<CMFlanger *> (pFx)->Force (fclk); }

// LFO and clock alone: period, clock range and number of switchings in "seconds" seconds with RATE = x (0..1)
void MflangerClockForTest (double x, double seconds, double *fmin, double *fmax, int *nSw, double *dcM)
{
	TCtl t; t.Init ();
	const double rch = RateOhm (x), dt = 1.0e-3;
	double lo = 1e18, hi = 0.0; int sw = 0; double prev = t.out;
	for (double s = 0.0; s < seconds; s += dt)
	{
		const double f = t.Step (dt, rch);
		if (s > 40.0) { if (f < lo) lo = f; if (f > hi) hi = f; }
		if (t.out != prev) { sw++; prev = t.out; }
	}
	*fmin = lo; *fmax = hi; *nSw = sw; *dcM = t.m;
}
