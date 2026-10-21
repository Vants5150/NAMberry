//
// fxckt.h - common class of the REAL pedals simulated from the netlist (ckt.h): two channels, potentiometer smoothing, protection against non-convergence.
// Each pedal (fx_xxx.cpp) provides: Build (netlist and handles), Pots (values of the variable resistors from the parameters) and the input and output constants.
//
#ifndef _fxckt_h
#define _fxckt_h

#include "fx.h"
#include "ckt.h"
#include <circle/util.h>

// 0 dBFS = +1.5 dBu = 1.305 V peak (Pisound input calibration, see ADELnam)
#define CKT_VFS		1.305f
#define CKT_H		16		// handles per pedal

// transistor with internal resistances (base, emitter, collector) and values from a SPICE model (Gummel-Poon reduced to Ebers-Moll)
static inline void AddBjt (Ck::TCkt &c, int nc, int nb, int ne, double Is, double bf, double br, double rb, double re, double rc, bool pnp, bool simple)
{
	int c2 = nc, b2 = nb, e2 = ne;
	if (rc > 0.0) { c2 = c.Node (); c.R (nc, c2, rc); }
	if (rb > 0.0) { b2 = c.Node (); c.R (nb, b2, rb); }
	if (re > 0.0) { e2 = c.Node (); c.R (ne, e2, re); }
	c.Bjt (c2, b2, e2, Is, bf, br, 0.02585, pnp, simple);
}

struct TPedal
{
	unsigned type;
	// builds the circuit: h[0] = input generator, h[1] = output node, the rest at the pedal's choice; returns 0 if successful
	void (*Build) (Ck::TCkt &c, int *h, const float *p);
	// sets the potentiometers and the choices from the block's (smoothed) parameters
	void (*Pots) (Ck::TCkt &c, const int *h, const float *p);
	unsigned nParams;	// parameters read
	unsigned structMask;	// parameters (bits) that change the circuit's structure: when they change (rounded value) the circuit is rebuilt
	float fOutGain;		// gain between the circuit's output voltage (V) and the digital sample: 1 / CKT_VFS for the physical level
	unsigned iGainDb;	// 0 = none; otherwise 1 + index of the parameter (dB) that multiplies the output (recovery of the gain lost by the passive circuit)
	// periodic modulation (LFO outside the circuit): every modEvery samples Mod is called, which advances its own state st (8 numbers per channel, all zero at the start) by n samples and updates the
	// values of the circuit's resistances; the parameter value is the smoothed one
	unsigned modEvery;
	void (*Mod) (Ck::TCkt &c, const int *h, const float *p, double *st, unsigned n, double dt);
};

class CCktFx : public CFx
{
public:
	CCktFx (const TPedal *pDef, float fRate) : m_pDef (pDef), m_fRate (fRate), m_bInit (false)
	{
		for (unsigned i = 0; i < 16; i++) m_fSm[i] = m_fLast[i] = 0.0f;
		for (unsigned c = 0; c < 2; c++) { m_nMod[c] = 0; for (unsigned k = 0; k < 8; k++) m_st[c][k] = 0.0; }
		for (unsigned c = 0; c < 2; c++) Restart (c);
		m_pDef = pDef;
	}

	unsigned Type (void) const { return m_pDef->type; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		if (!m_bInit)
		{
			for (unsigned i = 0; i < m_pDef->nParams; i++) m_fSm[i] = pP[i];
			for (unsigned c = 0; c < 2; c++) Restart (c);		// the circuit is built only now that the structural parameters are known
			m_bInit = true;
			ApplyPots (true);
		}
		else
		{
			const float a = 1.0f - 0.0f;
			(void) a;
			const float k = 1.0f - Decay (n);
			bool bChg = false;
			for (unsigned i = 0; i < m_pDef->nParams; i++)
			{
				const float d = pP[i] - m_fSm[i];
				if (d != 0.0f) { m_fSm[i] += k * d; if (m_fSm[i] - pP[i] < 1e-4f && pP[i] - m_fSm[i] < 1e-4f) m_fSm[i] = pP[i]; }
				if (m_fSm[i] != m_fLast[i]) bChg = true;
			}
			if (bChg)
			{
				for (unsigned i = 0; i < m_pDef->nParams; i++)
				{
					if ((m_pDef->structMask >> i) & 1u)
					{
						const int a = (int) (m_fSm[i] + 0.5f), b = (int) (m_fLast[i] + 0.5f);
						if (a != b) { for (unsigned c = 0; c < 2; c++) { Restart (c); } break; }
					}
				}
				ApplyPots (false);
			}
		}
		const unsigned nCh = *pMono ? 1 : 2;
		const double vfs = (double) CKT_VFS;
		double og = (double) m_pDef->fOutGain;
		if (m_pDef->iGainDb) og *= (double) Ck::Exp (m_fSm[m_pDef->iGainDb - 1] * 0.11512925f);	// dB -> ratio
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			Ck::TCkt &K = m_Ck[c];
			for (unsigned j = 0; j < n; j++)
			{
				if (m_pDef->Mod && ++m_nMod[c] >= m_pDef->modEvery)
				{
					m_nMod[c] = 0;
					m_pDef->Mod (K, m_h, m_fSm, m_st[c], m_pDef->modEvery, 1.0 / (double) m_fRate);
				}
				double y = K.Run (m_h[0], (double) p[j] * vfs);
				if (!(y > -100.0 && y < 100.0))
				{
					Restart (c);
					y = 0.0;
				}
				p[j] = (float) (y * og);
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
		}
	}

private:
	// decay of the smoothing (time constant 10 ms) for a block of n samples
	float Decay (unsigned n) const
	{
		// exp(-n / (fs * 0.01)) with n / (fs * 0.01) small
		const float x = (float) n / (m_fRate * 0.010f);
		return 1.0f - x + 0.5f * x * x - x * x * x / 6.0f;
	}

	void Restart (unsigned c)
	{
		Ck::TCkt &K = m_Ck[c];
		K.Clear ();
		m_pDef->Build (K, m_h, m_fSm);
		K.SetStep (1.0 / (double) m_fRate);
		if (m_bInit) m_pDef->Pots (K, m_h, m_fSm);
		if (m_pDef->Mod && m_bInit) m_pDef->Mod (K, m_h, m_fSm, m_st[c], 0, 1.0 / (double) m_fRate);		// n = 0: only the resistances from the current state
		K.Start ();
	}

	void ApplyPots (bool bStart)
	{
		for (unsigned i = 0; i < 16; i++) m_fLast[i] = m_fSm[i];
		for (unsigned c = 0; c < 2; c++)
		{
			m_pDef->Pots (m_Ck[c], m_h, m_fSm);
			if (m_pDef->Mod) m_pDef->Mod (m_Ck[c], m_h, m_fSm, m_st[c], 0, 1.0 / (double) m_fRate);
		}
		if (bStart)
		{
			// the initial position of the potentiometers changes the bias: it recomputes the operating point
			for (unsigned c = 0; c < 2; c++) m_Ck[c].Start ();
		}
	}

	const TPedal *m_pDef;
	float m_fRate;
	bool m_bInit;
	float m_fSm[16], m_fLast[16];
	int m_h[CKT_H];
	Ck::TCkt m_Ck[2];
	double m_st[2][8];
	unsigned m_nMod[2];
};

#endif
