//
// fx_ocd.cpp - OCD Real: Fulltone OCD (v2), simulated from the NETLIST (ckt.h) with the pedal's controls (Drive, Tone, Volume) and the switch of the output stage.
//
// Sources (2026-10-21):
//  - R. Giampiccolo, S. Polimeno, C. Macri, A. Lenoci, O. Massi, A. Bernardini, "Explicit Wave Digital Model of the Fulltone OCD Pedal Based on Canonical Piecewise-Linear Functions", DAFx26,
//    Cambridge MA 2026 (https://www.dafx.de/paper-archive/2026/papers/DAFx26_paper_24.pdf), Figure 2 and Tables 1-2 (schematic of the v2 from https://generalguitargadgets.com/pdf/ggg_ocdist_sc.pdf):
//    input network: R2 1M to ground, R1 10k, C1 22 nF, R3 470k to Vb (4.5 V) on the non-inverting input of the first op-amp; feedback: C2 220 pF in parallel with R4 18k + R5 500k (Drive),
//    inverting input to ground through C3 100 nF + R6 2.2k; output V1 -> R7 10k -> clipping node X; C4 10 nF from X to Vb; clipping network between Vb and X: a diode-connected 2N7000 (Q1, current
//    from Vb to X) and, in the opposite direction, a diode-connected 2N7000 (Q2) in series with a 1N34A germanium diode (D1); X -> R9 10k -> non-inverting input of the second op-amp, which is also
//    biased by R8 220k to Vb; second op-amp: R10 150k in parallel with C5 220 pF in feedback, R11 39k + C6 100 nF to ground; output V2 -> C7 10 uF -> R12 33k and R13 22k (with the switch) in parallel ->
//    node N1; N1: C8 47 nF in series with R14 10k (Tone) to ground, and R15 500k (Volume) to ground with the wiper as output. 2N7000: kn 0.025 A/V^2, Vth 2.1 V, lambda 0.02; 1N34A: Is 2.58 uA,
//    eta 1.61, Vt 25.8 mV (Table 2). The paper assumes the high-peak/low-peak switch permanently closed. The paper itself splits the circuit in the same three parts used here (linear input stage,
//    nonlinear clipping stage, linear output stage) because the op-amps decouple them.
//  - SoundOnSound review (clipping network tied to the circuit's voltage reference, so the threshold follows the bias) and MusicRadar (2N7000 as clipping devices after the first stage, germanium
//    diode in the v1.4+ / v2 versions): consistent with the above.
//
// Model: three small circuits per channel (A: input network and first op-amp, B: clipping stage driven by the voltage V1, C: second op-amp and output stage driven by the voltage Y), which cuts the
// Newton work; the op-amps are TL072-like macromodels (gain-bandwidth 3 MHz, rails at +-3 V around the 4.5 V, as in the Klon Real: the paper does not name the op-amp) with a low output resistance, so
// the load of the next stage on the output of the first one (R7, 10k against 50 ohm) is neglected, and the inputs draw no current. MOSFETs as one-port elements WITHOUT the body diode (as in the
// paper), linear potentiometers (the tapers are not in the sources: ASSUMED), Drive 0..100 = R5 0..500k, Tone 0..100 = R14 0..10k (brighter clockwise), Volume 0..10 = wiper of R15. The switch: "R13 in"
// (closed, as in the paper) or "R13 out". The circuit runs at the sample rate (no oversampling). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

// A: p: 0 = Drive (%); handles: 0 = input generator, 1 = output V1, 2 = R5
void BuildA (Ck::TCkt &c, int *h)
{
	const int vb = c.Node (), in = c.Node ();
	c.V (vb, 0, 4.5);
	h[0] = c.V (in, 0, 0.0);
	c.R (in, 0, 1.0e6);						// R2
	const int n1 = c.Node (), p1 = c.Node (), v1 = c.Node (), f1 = c.Node (), f1b = c.Node (), g6 = c.Node ();
	c.R (in, n1, 10.0e3);						// R1
	c.C (n1, p1, 22.0e-9);						// C1
	c.R (p1, vb, 470.0e3);						// R3
	c.Opamp (p1, f1, v1, vb, 3.0e6, 2.0e4, 4.0e7, 3.0, 50.0);
	c.C (f1, v1, 220.0e-12);					// C2
	c.R (v1, f1b, 18.0e3);						// R4
	h[2] = c.R (f1b, f1, 250.0e3);					// R5 (Drive), in series with R4
	c.C (f1, g6, 100.0e-9);						// C3
	c.R (g6, 0, 2.2e3);						// R6
	c.SetOut (v1);
	h[1] = v1;
}

// B: the clipping stage; the input generator is V1 (absolute volts, 4.5 V at rest); handles: 0 = input generator, 1 = output Y, 2 = node X
void BuildB (Ck::TCkt &c, int *h)
{
	const int vb = c.Node (), v1 = c.Node (), x = c.Node (), y = c.Node (), mid = c.Node ();
	c.V (vb, 0, 4.5);
	h[0] = c.V (v1, 0, 4.5);
	c.R (v1, x, 10.0e3);						// R7
	c.C (x, vb, 10.0e-9);						// C4
	c.MosDiode (vb, x, 0.025, 2.1, 0.02);				// Q1: from Vb to X
	c.Diode (x, mid, 2.58e-6, 1.61 * 0.0258);			// D1 (1N34A): from X to the source of Q2
	c.MosDiode (mid, vb, 0.025, 2.1, 0.02);				// Q2: in series with D1, from X to Vb
	c.R (mid, vb, 10.0e6);						// the middle node would be connected only to nonlinear elements (singular matrix): a 10 Mohm leak (0.2 uA at 2 V) fixes it
	c.R (x, y, 10.0e3);						// R9
	c.R (vb, y, 220.0e3);						// R8
	c.R (y, 0, 1.0e9);						// the input of the second op-amp draws no current
	c.SetOut (y);
	h[1] = y;
	h[2] = x;
}

// C: second op-amp and output stage; the input generator is Y (absolute volts); handles: 0 = input generator, 1 = output (wiper of the Volume), 2 = R14, 3 = R15 top, 4 = R15 bottom, 5 = R13
void BuildC (Ck::TCkt &c, int *h)
{
	const int vb = c.Node (), y = c.Node ();
	c.V (vb, 0, 4.5);
	h[0] = c.V (y, 0, 4.5);
	const int f2 = c.Node (), v2 = c.Node (), g11 = c.Node ();
	c.Opamp (y, f2, v2, vb, 3.0e6, 2.0e4, 4.0e7, 3.0, 50.0);
	c.R (v2, f2, 150.0e3);						// R10
	c.C (v2, f2, 220.0e-12);					// C5
	c.C (f2, g11, 100.0e-9);					// C6
	c.R (g11, 0, 39.0e3);						// R11
	const int n0 = c.Node (), n2 = c.Node (), tn = c.Node (), lw = c.Node ();
	c.C (v2, n0, 10.0e-6);						// C7
	c.R (n0, n2, 33.0e3);						// R12
	h[5] = c.R (n0, n2, 22.0e3);					// R13 with the switch
	c.C (n2, tn, 47.0e-9);						// C8
	h[2] = c.R (tn, 0, 5.0e3);					// R14 (Tone)
	h[3] = c.R (n2, lw, 250.0e3);					// R15 (Volume): from the top to the wiper
	h[4] = c.R (lw, 0, 250.0e3);					// from the wiper to ground
	c.R (lw, 0, 1.0e6);						// load
	c.SetOut (lw);
	h[1] = lw;
}

// p: 0 = Drive (%), 1 = Tone (%), 2 = Level (dB, output gain), 3 = switch (0 = R13 out, 1 = R13 in), 4 = Volume (0..10)
void PotsA (Ck::TCkt &c, const int *h, const float *p)
{
	double dr = p[0] * 0.01;
	dr = dr < 0 ? 0 : (dr > 1 ? 1 : dr);
	c.SetR (h[2], dr * 500.0e3 + 1.0);
}

void PotsC (Ck::TCkt &c, const int *h, const float *p)
{
	double tn = p[1] * 0.01, vo = p[4] * 0.1;
	tn = tn < 0 ? 0 : (tn > 1 ? 1 : tn); vo = vo < 0 ? 0 : (vo > 1 ? 1 : vo);
	c.SetR (h[2], tn * 10.0e3 + 1.0);
	c.SetR (h[3], (1.0 - vo) * 500.0e3 + 1.0);
	c.SetR (h[4], vo * 500.0e3 + 1.0);
	c.SetR (h[5], p[3] > 0.5f ? 22.0e3 : 1.0e9);
}

class COcd : public CFx
{
public:
	COcd (float fRate) : m_fRate (fRate), m_bInit (false), m_dX (0.0)
	{
		for (unsigned s = 0; s < 2; s++) for (unsigned k = 0; k < 3; k++) m_pCk[s][k] = 0;
		for (unsigned i = 0; i < 5; i++) m_fSm[i] = 0.0f;
	}

	~COcd (void)
	{
		for (unsigned s = 0; s < 2; s++) for (unsigned k = 0; k < 3; k++) delete m_pCk[s][k];
	}

	unsigned Type (void) const { return FxOcd; }

	// last voltage of the clipping node X relative to Vb (for the tests)
	double LastX (void) const { return m_dX; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const unsigned nCh = *pMono ? 1 : 2;
		if (!m_bInit)
		{
			for (unsigned i = 0; i < 5; i++) m_fSm[i] = pP[i];
			m_bInit = true;
		}
		else
		{
			const float x = (float) n / (m_fRate * 0.010f);		// smoothing of the controls, time constant 10 ms
			const float k = x - 0.5f * x * x + x * x * x / 6.0f;
			for (unsigned i = 0; i < 5; i++)
			{
				const float d = pP[i] - m_fSm[i];
				if (d != 0.0f) { m_fSm[i] += k * d; if (m_fSm[i] - pP[i] < 1e-4f && pP[i] - m_fSm[i] < 1e-4f) m_fSm[i] = pP[i]; }
			}
		}
		const double vfs = (double) CKT_VFS;
		const double og = (1.0 / vfs) * (double) Ck::Exp (m_fSm[2] * 0.11512925f);
		for (unsigned c = 0; c < nCh; c++)
		{
			if (!m_pCk[c][0]) Restart (c);
			Ck::TCkt &A = *m_pCk[c][0], &B = *m_pCk[c][1], &C = *m_pCk[c][2];
			PotsA (A, m_hA, m_fSm);
			PotsC (C, m_hC, m_fSm);
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				const double v1 = A.Run (m_hA[0], (double) p[j] * vfs);
				const double y = B.Run (m_hB[0], v1);
				const double o = C.Run (m_hC[0], y);
				if (!(v1 > -100.0 && v1 < 100.0 && y > -100.0 && y < 100.0 && o > -100.0 && o < 100.0))
				{
					Restart (c);
					p[j] = 0.0f;
					continue;
				}
				p[j] = (float) (o * og);
			}
			if (c == 0) m_dX = m_pCk[0][1]->Probe (m_hB[2]) - 4.5;
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
		}
	}

private:
	void Restart (unsigned c)
	{
		for (unsigned k = 0; k < 3; k++)
		{
			if (!m_pCk[c][k]) m_pCk[c][k] = new Ck::TCkt;
			m_pCk[c][k]->Clear ();
		}
		BuildA (*m_pCk[c][0], m_hA);
		BuildB (*m_pCk[c][1], m_hB);
		BuildC (*m_pCk[c][2], m_hC);
		for (unsigned k = 0; k < 3; k++) m_pCk[c][k]->SetStep (1.0 / (double) m_fRate);
		PotsA (*m_pCk[c][0], m_hA, m_fSm);
		PotsC (*m_pCk[c][2], m_hC, m_fSm);
		for (unsigned k = 0; k < 3; k++) m_pCk[c][k]->Start ();
	}

	float m_fRate;
	bool m_bInit;
	double m_dX;
	float m_fSm[5];
	int m_hA[CKT_H], m_hB[CKT_H], m_hC[CKT_H];
	Ck::TCkt *m_pCk[2][3];
};

}

CFx *NewOcd (float fRate)
{
	return new COcd (fRate);
}

double OcdLastXForTest (CFx *pFx)
{
	return static_cast<COcd *> (pFx)->LastX ();
}

void OcdBuildBForTest (Ck::TCkt &c, int *h) { BuildB (c, h); }
