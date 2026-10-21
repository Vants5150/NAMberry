//
// fx_flanger117.cpp - Flanger Real: MXR M-117 Flanger (schematic 117-4001, revision 106, Reticon SAD1024 BBD with 1024 stages): input buffer with pre-emphasis, limiter with REGEN feedback (simulated from the
// NETLIST with ckt.h), 1024-stage BBD as an interpolated delay, output filters, mixer with de-emphasis; LFO, MANUAL and WIDTH controls and clock control from the formulas of their circuit.
//
// Sources (2026-10-21): the MXR service schematic 117-4001 rev. 106 (PDF supplied by the user, scan of the factory drawing; all values read from the drawing) and, for the clock, forum measurements on two
// units (DIYstompboxes, technician: clock about 29-32 kHz at the low end and 960-965 kHz at the high end; the two 512-stage halves of the SAD1024 are wired in SERIES: 1024 stages). Values read:
//  - Input: J2 -> C16 0.1 uF -> R32 1K -> U4A (CA1458) non-inverting, bias R24 470K to Vb (7.5 V from R12 30K / R8 30K), C25 51 pF; gain 1 + R34 10K / (R33 1K + 1 / s C14 0.033 uF): pre-emphasis (1 below 480 Hz, 11 above 4.8 kHz).
//  - U2A (CA1458) inverting from U4A's output through R35 50K; feedback R16 100K, C23 100 pF and R15 27K in series with the antiparallel diodes CR3 / CR4 (1N914): the gain falls from 2 to 0.42 for large signals;
//    REGEN: wet node -> C18 0.1 uF -> R53 50K (potentiometer, one end to ground) -> wiper -> C17 0.015 uF + R31 120K -> U2A's inverting input. Output: R30 1K and C12 0.01 uF (anti-aliasing, 16 kHz).
//  - BBD: C1 0.1 uF into IN A (bias R1 100K), outputs of section A (pins 5 and 6) loaded by R10 2.4K + R11 10K, from the junction through C6 0.1 uF into IN B (bias R2 100K), outputs of B (pins 11 and 12) on the
//    TRIM LEVEL potentiometer R48 20K, wiper -> C7 0.1 uF -> R4 30K. Two-phase clock from U3 (CD4013).
//  - Output filter: U1A multiple-feedback low-pass (R4 30K, R6 30K, R5 5.1K, C2 6.8 nF, C22 150 pF: 12.7 kHz, Q 2.1, gain -1) followed by U1B unity Sallen-Key (R7 7.5K, R3 2.4K, C3 6.8 nF, C4 3.3 nF: 7.9 kHz).
//  - Mixer U4B: dry from U4A through R37 150K, wet from U1B through R41 82K, feedback R39 100K in parallel with R40 10K + C15 3.3 nF (de-emphasis), C19 2.2 uF, R36 1K, R38 10K.
//  - LFO: U5A Schmitt trigger (R9 75K from the triangle and R23 100K from its own output on the + input, reference R22 15K / R21 16K = 7.74 V) and U5B integrator (R19 4.7K + SPEED R52 500K, C10 and C11 15 uF back to back
//    = 7.5 uF): triangle between 3.8 and 12.4 V, 0.09 Hz (SPEED at minimum) to 9.4 Hz (maximum), the reissue's published rate range is 0.1 - 10 Hz. WIDTH R51 50K mixes between the triangle and the buffered MANUAL level (U2:
//    R17 470K / R18 470K and R25 100K from the MANUAL potentiometer R50 50K, 2.2 to 12.8 V), through R29 1M into the base of Q3 (ZTX109C) that sets the clock oscillator (Q1, Q2, C21 33-62 pF) with Q4 and R46 22K.
//
// ASSUMED (not in the sources): the clock frequency is an exponential function of the control voltage that goes from 30 kHz (MANUAL at minimum) to 960 kHz (at maximum): the measured extremes, not a simulation of
// the relaxation oscillator; delay = 512 / fclk (0.53 to 17 ms); BBD ideal (gain 1 per section, interpolated delay, no clock noise or sampling at the clock rate); TRIM LEVEL set so that the wet and dry levels are
// equal (deepest notches); CA1458 with gain-bandwidth 1 MHz, slew 0.5 V/us and output swing +-5.5 V around Vb; the Schmitt trigger's output levels 1.5 V and 13 V; the pots linear; the Q3/Q4 exponential law of the
// clock replaced by the calibrated one. The effect is mono, inverting like the original. Not verified against recordings of the real pedal.
//
#include "fxckt.h"
#include "iirdesign.h"

namespace
{

const double kVb = 7.5, kVt = 0.02585;
// LFO: Schmitt trigger and integrator
const double kVref2 = 15.0 * 16.0 / 31.0, kVH = 13.0, kVL = 1.5, kRho = 75.0 / 100.0, kCint = 7.5e-6, kR19 = 4.7e3;
const double kThLo = kVref2 * (1.0 + kRho) - kRho * kVH, kThHi = kVref2 * (1.0 + kRho) - kRho * kVL;
const double kFLo = 30.0e3, kFHi = 960.0e3;		// clock at the extremes of MANUAL (measured)
const double kVmanLo = 2.2380, kVmanHi = 12.7596;
const double kBbdGain = 0.806 * 0.339;			// R10 / R11 divider between the sections and TRIM LEVEL (assumed for equal wet and dry)
const unsigned kBuf = 1024;
const int kStep = 64;

// A: U2A stage: h[0] = generator of U4A's output (absolute volts), h[1] = regen generator (wet node, absolute volts), h[2] = BBD input node (after R30 / C12), h[3] / h[4] = REGEN pot, h[5] = U2A output
void BuildA (Ck::TCkt &c, int *h)
{
	const int vb = c.Node (), va = c.Node (), s = c.Node (), o = c.Node (), mid = c.Node (), rg = c.Node (), p = c.Node (), w1 = c.Node (), q = c.Node (), bb = c.Node ();
	c.V (vb, 0, kVb);
	h[0] = c.V (va, 0, kVb);
	c.R (va, s, 50.0e3);								// R35
	c.Opamp (vb, s, o, vb, 1.0e6, 2.0e5, 5.0e5, 5.5, 100.0);			// U2A
	c.R (s, o, 100.0e3);								// R16
	c.C (s, o, 100.0e-12);								// C23
	c.R (s, mid, 27.0e3);								// R15
	c.DiodePair (mid, o, 2.52e-9, 1.752 * kVt);					// CR3 / CR4 (1N914, antiparallel)
	h[1] = c.V (rg, 0, kVb);
	c.C (rg, p, 0.1e-6);								// C18
	h[3] = c.R (p, w1, 25.0e3);							// R53: from the purple wire to the wiper
	h[4] = c.R (w1, 0, 25.0e3);							// R53: from the wiper to ground (black wire)
	c.C (w1, q, 0.015e-6);								// C17
	c.R (q, s, 120.0e3);								// R31
	c.R (o, bb, 1.0e3);								// R30
	c.C (bb, 0, 0.01e-6);								// C12
	c.SetOut (bb);
	h[2] = bb; h[5] = o;
}

// Rauch (multiple-feedback) low-pass U1A with the input coupling C7: H = -Y1 / (Y2 + s C' R3 (Y1 + s C + Y2 + Y3)), Y1 = s C7 / (1 + s R4 C7)
void DesignRauch (TIir &f, double fs)
{
	const double R4 = 30.0e3, R6 = 30.0e3, R5 = 5.1e3, C2 = 6.8e-9, C22 = 150.0e-12, C7 = 0.1e-6;
	const double tau = R4 * C7, y2 = 1.0 / R6, y3 = 1.0 / R5, cp = C22, r3 = R5;
	const double D[4] = {y2, tau * y2 + cp * r3 * (y2 + y3), cp * r3 * (C2 + tau * (y2 + y3) + C7), cp * r3 * C2 * tau};
	const double N[4] = {0.0, -C7, 0.0, 0.0};
	f.Design (3, N, D, fs);
}

// U1B Sallen-Key: R7 into node A (C3 to the output), R3 into B (C4 to ground)
void DesignSk (TIir &f, double fs)
{
	const double R7 = 7.5e3, R3 = 2.4e3, C3 = 6.8e-9, C4 = 3.3e-9;
	const double D[4] = {1.0, C4 * (R7 + R3), R7 * R3 * C3 * C4, 0.0}, N[4] = {1.0, 0.0, 0.0, 0.0};
	f.Design (2, N, D, fs);
}

// mixer input: -(R39 / Rin) (1 + s R40 C15) / (1 + s (R40 C15 + R39 C15))
void DesignMix (TIir &f, double rin, double fs)
{
	const double R39 = 100.0e3, R40 = 10.0e3, C15 = 3.3e-9, k = R39 / rin, t40 = R40 * C15;
	const double N[4] = {-k, -k * t40, 0.0, 0.0}, D[4] = {1.0, t40 + R39 * C15, 0.0, 0.0};
	f.Design (1, N, D, fs);
}

void DesignHp (TIir &f, double tau, double fs)
{
	const double N[4] = {0.0, tau, 0.0, 0.0}, D[4] = {1.0, tau, 0.0, 0.0};
	f.Design (1, N, D, fs);
}

// LFO state: triangle node of the integrator and output of the Schmitt trigger
struct TLfo
{
	double tri;
	bool hi;

	void Init (void) { tri = 0.5 * (kThLo + kThHi); hi = true; }

	// dt seconds; rint = R19 + SPEED
	void Step (double dt, double rint)
	{
		const double vsq = hi ? kVH : kVL;
		tri += -(vsq - kVref2) / (rint * kCint) * dt;
		if (hi && tri <= kThLo) { tri = kThLo; hi = false; }
		else if (!hi && tri >= kThHi) { tri = kThHi; hi = true; }
	}
};

double SpeedOhm (double knob)
{
	knob = knob < 0 ? 0 : (knob > 10 ? 10 : knob);
	return kR19 + 500.0e3 * (1.0 - knob * 0.1);
}

// MANUAL: U2's + input from the pot R50 (Thevenin 50K m (1 - m)) through R25 100K, R17 470K / R18 470K
double ManualVolts (double knob)
{
	double m = knob * 0.1;
	m = m < 0 ? 0 : (m > 1 ? 1 : m);
	const double rth = 50.0e3 * m * (1.0 - m), g = 1.0 / (100.0e3 + rth);
	return (15.0 / 470.0e3 + 15.0 * m * g) / (2.0 / 470.0e3 + g);
}

// clock (Hz) for the control voltage at the WIDTH wiper
double ClockHz (double vw)
{
	double u = (vw - kVmanLo) / (kVmanHi - kVmanLo);
	u = u < 0 ? 0 : (u > 1 ? 1 : u);
	return kFLo * Ck::Exp (u * 3.4657359);		// ln 32
}

class CFlanger117 : public CFx
{
public:
	CFlanger117 (float fRate) : m_fRate (fRate), m_bInit (false), m_nCnt (0), m_nW (0), m_dDs (200.0), m_dD (200.0), m_dForce (0.0), m_dWet (0.0), m_dDc (0.0)
	{
		m_pA = 0;
		for (unsigned i = 0; i < kBuf; i++) m_Buf[i] = 0.0f;
		Design ();
	}

	~CFlanger117 (void) { delete m_pA; }

	unsigned Type (void) const { return FxFlanger; }

	void Force (double fclk) { m_dForce = fclk; }

	// p: 0 = Speed (0..10), 1 = Width (0..10), 2 = Manual (0..10), 3 = Regen (0..10)
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		if (!m_bInit) { Restart (); m_bInit = true; }
		double rg = pP[3] * 0.1;
		rg = rg < 0 ? 0 : (rg > 1 ? 1 : rg);
		m_pA->SetR (m_hA[3], (1.0 - rg) * 50.0e3 + 1.0);
		m_pA->SetR (m_hA[4], rg * 50.0e3 + 1.0);
		double wd = pP[1] * 0.1;
		wd = wd < 0 ? 0 : (wd > 1 ? 1 : wd);
		const double rint = SpeedOhm ((double) pP[0]), vman = ManualVolts ((double) pP[2]);
		const double dt = 1.0 / (double) m_fRate, vfs = (double) CKT_VFS, og = 1.0 / (double) CKT_VFS;
		const double kd = 1.0 - Ck::Exp (-1.0 / 40.0);
		for (unsigned j = 0; j < n; j++)
		{
			if (m_nCnt == 0)
			{
				m_Lfo.Step (dt * (double) kStep, rint);
				const double vw = vman + wd * (m_Lfo.tri - vman);
				const double f = m_dForce > 0.0 ? m_dForce : ClockHz (vw);
				double d = 512.0 / f * (double) m_fRate;			// delay = 1024 / (2 fclock)
				if (d < 3.0) d = 3.0;
				if (d > (double) kBuf - 8.0) d = (double) kBuf - 8.0;
				m_dD = d;
				m_nCnt = kStep;
			}
			m_nCnt--;
			m_dDs += (m_dD - m_dDs) * (m_dForce > 0.0 ? 1.0 : kd);
			const double x = 0.5 * ((double) pL[j] + (double) pR[j]) * vfs;
			// U4A: input high-pass and pre-emphasis, limited by its supply
			double va = m_Pre.Run (m_HpIn.Run (x));
			va = 5.5 * Ck::Tanh (va / 5.5);
			m_pA->SetV (m_hA[1], kVb + m_dWet);
			double bb = m_pA->Run (m_hA[0], kVb + va);
			if (!(bb > -80.0 && bb < 80.0)) { Restart (); bb = kVb; va = 0.0; }
			// BBD: input coupling, 1024 stages as a delay, the two sections' coupling
			m_Buf[m_nW] = (float) m_HpB1.Run (bb - m_dDc);
			double rp = (double) m_nW - m_dDs;
			while (rp < 0.0) rp += (double) kBuf;
			const unsigned i1 = (unsigned) rp, i0 = (i1 + kBuf - 1) % kBuf, i2 = (i1 + 1) % kBuf, i3 = (i1 + 2) % kBuf;
			const double fr = rp - (double) i1;
			const double y0 = m_Buf[i0], y1 = m_Buf[i1], y2 = m_Buf[i2], y3 = m_Buf[i3];
			const double bd = y1 + 0.5 * fr * (y2 - y0 + fr * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + fr * (3.0 * (y1 - y2) + y3 - y0)));
			m_nW = (m_nW + 1) % kBuf;
			const double wetIn = m_HpB2.Run (bd * kBbdGain);
			// output filters
			const double w = m_Sk.Run (m_Rauch.Run (wetIn));
			m_dWet = w;
			// mixer U4B and output
			const double o = 0.909 * (m_Dry.Run (va) + m_Wet.Run (w));
			pL[j] = pR[j] = (float) (o * og);
		}
		(void) pMono;
		*pMono = 1;
	}

	// filters for the tests: 0 = pre-emphasis, 1 = Rauch, 2 = Sallen-Key, 3 = dry mixer input, 4 = wet mixer input
	double Filter (int which, double u) { TIir &f = which == 0 ? m_Pre : (which == 1 ? m_Rauch : (which == 2 ? m_Sk : (which == 3 ? m_Dry : m_Wet))); return f.Run (u); }
	void ResetFilters (void) { Design (); }

private:
	void Design (void)
	{
		const double fs = (double) m_fRate;
		DesignHp (m_HpIn, (470.0e3 + 1.0e3) * 0.1e-6, fs);					// C16 with R24
		{ const double t33 = 1.0e3 * 0.033e-6, tg = 10.0e3 * 0.033e-6; const double N[4] = {1.0, t33 + tg, 0.0, 0.0}, D[4] = {1.0, t33, 0.0, 0.0}; m_Pre.Design (1, N, D, fs); }
		DesignHp (m_HpB1, 100.0e3 * 0.1e-6, fs);						// C1 with R1
		DesignHp (m_HpB2, 100.0e3 * 0.1e-6, fs);						// C6 with R2
		DesignRauch (m_Rauch, fs);
		DesignSk (m_Sk, fs);
		DesignMix (m_Dry, 150.0e3, fs);
		DesignMix (m_Wet, 82.0e3, fs);
	}

	void Restart (void)
	{
		if (!m_pA) m_pA = new Ck::TCkt;
		m_pA->Clear ();
		BuildA (*m_pA, m_hA);
		m_pA->SetStep (1.0 / (double) m_fRate);
		m_pA->SetR (m_hA[3], 25.0e3);
		m_pA->SetR (m_hA[4], 25.0e3);
		m_pA->Start ();
		for (int i = 0; i < 4; i++) m_pA->Run (m_hA[0], kVb);
		m_dDc = m_pA->Probe (m_hA[2]);
		m_Lfo.Init ();
		m_dWet = 0.0; m_nCnt = 0;
		for (unsigned i = 0; i < kBuf; i++) m_Buf[i] = 0.0f;
		Design ();
	}

	float m_fRate;
	bool m_bInit;
	int m_nCnt;
	unsigned m_nW;
	double m_dDs, m_dD, m_dForce, m_dWet, m_dDc;
	TLfo m_Lfo;
	TIir m_HpIn, m_Pre, m_HpB1, m_HpB2, m_Rauch, m_Sk, m_Dry, m_Wet;
	float m_Buf[kBuf];
	int m_hA[6];
	Ck::TCkt *m_pA;
};

}

CFx *NewFlanger117 (float fRate)
{
	return new CFlanger117 (fRate);
}

// hooks for the tests
void Flanger117ForceForTest (CFx *pFx, double fclk) { static_cast<CFlanger117 *> (pFx)->Force (fclk); }

void Flanger117FilterForTest (CFx *pFx, int which, const float *pIn, float *pOut, unsigned n)
{
	CFlanger117 *p = static_cast<CFlanger117 *> (pFx);
	p->ResetFilters ();
	for (unsigned i = 0; i < n; i++) pOut[i] = (float) p->Filter (which, (double) pIn[i]);
}

// LFO alone: frequency (Hz) of the triangle and its minimum and maximum, for a SPEED knob value; clock (Hz) for a MANUAL knob value and a control voltage
double Flanger117LfoForTest (double speed, double *pLo, double *pHi)
{
	TLfo l;
	l.Init ();
	const double rint = SpeedOhm (speed), dt = 1.0e-4;
	double lo = 1e9, hi = -1e9, tprev = 0.0, tt = 0.0, per = 0.0;
	int ncross = 0;
	bool prevHi = l.hi;
	const double tmax = 40.0 / (0.05 + (speed > 5 ? 3.0 : 0.15));
	for (double t = 0.0; t < tmax && ncross < 6; t += dt)
	{
		l.Step (dt, rint);
		if (l.tri < lo) lo = l.tri;
		if (l.tri > hi) hi = l.tri;
		if (l.hi && !prevHi) { ncross++; if (ncross == 2) tprev = t; if (ncross == 5) { per = (t - tprev) / 3.0; } }
		prevHi = l.hi;
		tt = t;
	}
	(void) tt;
	*pLo = lo; *pHi = hi;
	return per > 0.0 ? 1.0 / per : 0.0;
}

double Flanger117ClockForTest (double manual, double wd, double tri)
{
	const double vman = ManualVolts (manual);
	return ClockHz (vman + wd * (tri - vman));
}
