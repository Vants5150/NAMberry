//
// fx_ce2.cpp - Chorus CE-2 Real: Roland/Boss CE-2 (1979, input buffer, pre-emphasis and de-emphasis filters, third-order anti-aliasing and reconstruction filters, MN3007 BBD driven by the MN3101 clock,
// triangular LFO with two op-amps), simulated from the NETLIST (ckt.h) for the audio parts, from a numerical delay line for the BBD and from a numerical LFO with the formulas of its circuit.
//
// Sources (2026-10-19): ElectroSmash, Boss CE-2 analysis (archived copy, https://electrosmash.com/boss-ce-2-analysis), schematics and values: input C1 47 nF, R1 1k, R2 470k at 4.5 V, follower (2SC732), R3 10k,
// C2 0.47 uF; pre-emphasis (4558): R4 47k, R5 10k + C3 6.8 nF in parallel with R4, R6 47k and C4 100 pF in feedback (gain 0 dB below 410 Hz, 15 dB above 2.34 kHz); anti-aliasing: C5 33 nF and R8 100k at 4.5 V
// (48 Hz), then R9 R10 R11 10k, C6 3.3 nF, C7 8.2 nF towards the follower's output, C8 470 pF (poles at 6.6 and 6.9 kHz), R12 10k; reconstruction: the same but with C10 33 nF and R15 330k (14.6 Hz); de-emphasis
// (4558): R21 47k (dry), R22 47k (wet), R24 47k, R23 10k + C15 6.8 nF and C16 100 pF in feedback (same poles as the pre-emphasis, inverse ratio: 50% mix), R25 470, C17 1 uF, R26 100k; 1024-stage MN3007 BBD
// (delay N / (2 fclock), from 5.12 to 51.2 ms) with MN3101 clock: C22 47 pF charges from R38 150k and discharges through the transistor, the starting voltage is (Vlfo - Vbe) / 2 - Vf(D1), with Vlfo taken from the emitter
// of Q4 (R35 220k and C21 10 nF at 72 Hz); LFO (TL022): Schmitt trigger (R30 47k, R29 33k) and integrator (R32 1M, C19 0.1 uF), frequency R30 / (4 C19 R29 R32) = 3.5 Hz with Rate at maximum (VR1 100k and R31 10k
// reduce the amplitude of the square wave towards the integrator: frequency from about 0.3 to 3.5 Hz), VR2 Depth 100k at the triangle's output.
//
// Model and ASSUMPTIONS (not in the source): linear op-amps with finite gain; the transistor followers (Q1, Q2, Q3) are ideal followers; BBD = delay line with Lagrange interpolation, gain 1,
// without compression, distortion or sampling aliasing; JFET switch (Q9) always closed; non-inverting BBD polarity; output amplitude of the TL022s +-3 V (hence a 4.2 V peak-to-peak triangle);
// CMOS inverter threshold of the MN3101 = half the supply (9 V; the datasheet does not give it), linear potentiometers. MN3101 datasheet (Panasonic, copy downloaded by the user on 2026-10-19): two-inverter oscillator
// with external R and C, "the clock frequency is half the oscillation frequency" (CONFIRMED: fclock = 1 / (2 T), T = charge period of C22), outputs CP1 and CP2 in antiphase with 1/2 duty; examples:
// C1 = 33 pF, R2 5k-1M: fosc 15-1500 kHz, fclock 7.5-750 kHz (fosc about 1 / (2 R2 C1) for large R2). With these assumptions the delay goes from about 2.8 to 4.8 ms with Depth at maximum (center 3.85 ms, clock of about
// 133 kHz: above the 100 kHz of the MN3007's 5.12-51.2 ms range, which is a nameplate minimum delay). The real CE-2 delay was not measured here: a measurement of the pedal's clock is needed. Not verified against recordings.
//
#include "fxckt.h"

namespace
{

const double kVb = 4.5, kVcc = 9.0;
const double kFs = 48000.0;

// linear op-amp: current g (V+ - V-) into the output node, 1 Mohm load to ground
void Op (Ck::TCkt &c, int inp, int inn, int out)
{
	c.Gc (0, out, inp, inn, 1.0);
	c.R (out, 0, 1.0e6);
}

// input circuit: h[0] = generator, h[1] = dry output (pre-emphasis output), h[2] = anti-aliasing output (towards the BBD)
void BuildA (Ck::TCkt &c, int *h)
{
	const int vb = c.Node (), ix = c.Node (), in = c.Node (), n1 = c.Node (), b = c.Node (), e = c.Node (), pe = c.Node (), m = c.Node (), o = c.Node (), k = c.Node ();
	const int a1 = c.Node (), a2 = c.Node (), a3 = c.Node (), a4 = c.Node (), ao = c.Node ();
	c.V (vb, 0, kVb);
	h[0] = c.V (ix, 0, 0.0);
	c.R (ix, in, 1.0);
	c.C (in, n1, 47.0e-9); c.R (n1, b, 1.0e3); c.R (b, vb, 470.0e3);		// C1, R1, R2
	Op (c, b, e, e);									// Q1: follower
	c.C (e, pe, 0.47e-6);									// C2
	c.R (pe, m, 47.0e3);									// R4
	c.C (pe, k, 6.8e-9); c.R (k, m, 10.0e3);						// C3, R5
	c.R (m, o, 47.0e3); c.C (m, o, 100.0e-12);						// R6, C4
	Op (c, vb, m, o);
	c.C (o, a1, 33.0e-9); c.R (a1, vb, 100.0e3);						// C5, R8
	c.R (a1, a2, 10.0e3); c.C (a2, 0, 3.3e-9);						// R9, C6
	c.R (a2, a3, 10.0e3); c.C (a3, ao, 8.2e-9);						// R10, C7
	c.R (a3, a4, 10.0e3); c.C (a4, 0, 470.0e-12);						// R11, C8
	Op (c, a4, ao, ao);									// Q2: follower
	c.R (ao, 0, 10.0e3);									// R12
	c.SetOut (o);
	h[1] = o; h[2] = ao;
}

// output circuit: h[0] = dry generator (4.5 V + signal), h[1] = wet generator (pure signal), h[2] = output
void BuildB (Ck::TCkt &c, int *h)
{
	const int vb = c.Node (), vd = c.Node (), vw = c.Node (), r1 = c.Node (), r2 = c.Node (), r3 = c.Node (), r4 = c.Node (), ro = c.Node (), m = c.Node (), k = c.Node (), o2 = c.Node (), x = c.Node (), out = c.Node ();
	c.V (vb, 0, kVb);
	h[0] = c.V (vd, 0, kVb);
	h[1] = c.V (vw, 0, 0.0);
	c.C (vw, r1, 33.0e-9); c.R (r1, vb, 330.0e3);						// C10, R15
	c.R (r1, r2, 10.0e3); c.C (r2, 0, 3.3e-9);						// R16, C11
	c.R (r2, r3, 10.0e3); c.C (r3, ro, 8.2e-9);						// R17, C12
	c.R (r3, r4, 10.0e3); c.C (r4, 0, 470.0e-12);						// R18, C13
	Op (c, r4, ro, ro);									// Q3
	c.R (ro, 0, 10.0e3);									// R19
	c.R (vd, m, 47.0e3);									// R21 (dry)
	c.R (ro, m, 47.0e3);									// R22 (wet)
	c.R (m, o2, 47.0e3); c.C (m, o2, 100.0e-12);						// R24, C16
	c.C (m, k, 6.8e-9); c.R (k, o2, 10.0e3);						// C15, R23
	Op (c, vb, m, o2);
	c.R (o2, x, 470.0); c.C (x, out, 1.0e-6); c.R (out, 0, 100.0e3);			// R25, C17, R26
	c.R (out, 0, 1.0e6);									// amplifier load
	c.SetOut (out);
	h[2] = out;
}

// natural logarithm with the atanh series, exact to within 1e-4 for x in (0.5; 2.5)
double Ln (double x)
{
	const double y = (x - 1.0) / (x + 1.0), y2 = y * y;
	return 2.0 * y * (1.0 + y2 * (1.0 / 3.0 + y2 * (1.0 / 5.0 + y2 * (1.0 / 7.0 + y2 * (1.0 / 9.0)))));
}

// BBD delay in seconds from the LFO voltage (absolute, centered on 4.5 V)
double DelaySec (double vlfo)
{
	const double v0 = (vlfo - 0.65) * 0.5 - 0.6;				// starting voltage of C22: (Vlfo - Vbe) / 2 - Vf
	const double T = 150.0e3 * 47.0e-12 * Ln ((kVcc - v0) / (kVcc - 0.5 * kVcc));
	return 1024.0 * T;							// 1024 stages, fclock = 1 / (2 T): N / (2 fclock) = N T
}

// LFO frequency for Rate 0..10: the voltage of the square wave to the integrator is the fraction (w VR1 + R31) / (VR1 + R31)
double LfoHz (double rate)
{
	double w = rate * 0.1;
	w = w < 0 ? 0 : (w > 1 ? 1 : w);
	const double s = (w * 100.0e3 + 10.0e3) / 110.0e3;
	return s * 47.0e3 / (4.0 * 0.1e-6 * 33.0e3 * 1.0e6);
}

const unsigned kBuf = 1024;

class CCe2 : public CFx
{
public:
	CCe2 (float fRate) : m_fRate (fRate), m_bInit (false), m_fPh (0.0), m_fDepth (5.0f)
	{
		for (unsigned c = 0; c < 2; c++)
		{
			m_nW[c] = 0;
			for (unsigned i = 0; i < kBuf; i++) m_Buf[c][i] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxCe2; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		if (!m_bInit)
		{
			for (unsigned c = 0; c < 2; c++) Restart (c);
			m_fDepth = pP[1];
			m_bInit = true;
		}
		const unsigned nCh = *pMono ? 1 : 2;
		const double dt = 1.0 / (double) m_fRate, vfs = (double) CKT_VFS, og = 1.0 / (double) CKT_VFS;
		const double hz = LfoHz ((double) pP[0]);
		const float kD = 1.0f - 0.9995f;
		for (unsigned j = 0; j < n; j++)
		{
			m_fDepth += kD * (pP[1] - m_fDepth);
			m_fPh += hz * dt;
			if (m_fPh >= 1.0) m_fPh -= 1.0;
			const double tri = m_fPh < 0.5 ? -1.0 + 4.0 * m_fPh : 3.0 - 4.0 * m_fPh;			// triangle -1..1
			double dep = (double) m_fDepth * 0.1;
			dep = dep < 0 ? 0 : (dep > 1 ? 1 : dep);
			const double vlfo = kVb + 2.1 * dep * tri;							// 2.1 V = half of the 4.2 V peak-to-peak (+-3 V output of the TL022s through R29 / R30)
			double d = DelaySec (vlfo) * (double) m_fRate;
			if (d < 4.0) d = 4.0;
			if (d > (double) kBuf - 8.0) d = (double) kBuf - 8.0;
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				double dry = m_A[c].Run (m_hA[c][0], (double) p[j] * vfs);
				double aa = m_A[c].Probe (m_hA[c][2]) - kVb;
				if (!(dry > -50.0 && dry < 50.0 && aa > -50.0 && aa < 50.0)) { Restart (c); dry = kVb; aa = 0.0; }
				m_Buf[c][m_nW[c]] = (float) aa;
				// fractional read with 4-point Lagrange interpolation
				double rp = (double) m_nW[c] - d;
				while (rp < 0.0) rp += (double) kBuf;
				const unsigned i1 = (unsigned) rp, i0 = (i1 + kBuf - 1) % kBuf, i2 = (i1 + 1) % kBuf, i3 = (i1 + 2) % kBuf;
				const double f = rp - (double) i1;
				const double y0 = m_Buf[c][i0], y1 = m_Buf[c][i1], y2 = m_Buf[c][i2], y3 = m_Buf[c][i3];
				const double wet = y1 + 0.5 * f * (y2 - y0 + f * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + f * (3.0 * (y1 - y2) + y3 - y0)));
				m_nW[c] = (m_nW[c] + 1) % kBuf;
				m_B[c].SetV (m_hB[c][1], wet);
				double y = m_B[c].Run (m_hB[c][0], dry);
				if (!(y > -50.0 && y < 50.0)) { Restart (c); y = 0.0; }
				p[j] = (float) (y * og);
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
		}
	}

private:
	void Restart (unsigned c)
	{
		m_A[c].Clear (); BuildA (m_A[c], m_hA[c]); m_A[c].SetStep (1.0 / (double) m_fRate); m_A[c].Start ();
		m_B[c].Clear (); BuildB (m_B[c], m_hB[c]); m_B[c].SetStep (1.0 / (double) m_fRate); m_B[c].Start ();
	}

	float m_fRate;
	bool m_bInit;
	double m_fPh;
	float m_fDepth;
	unsigned m_nW[2];
	float m_Buf[2][kBuf];
	int m_hA[2][4], m_hB[2][4];
	Ck::TCkt m_A[2], m_B[2];
};

}

CFx *NewCe2 (float fRate)
{
	return new CCe2 (fRate);
}

void Ce2BuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &b, int *hb) { BuildA (a, ha); BuildB (b, hb); }
double Ce2DelayForTest (double vlfo) { return DelaySec (vlfo); }
double Ce2LfoHzForTest (double rate) { return LfoHz (rate); }
