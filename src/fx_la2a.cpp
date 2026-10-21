//
// fx_la2a.cpp - Optical LA Real: LA-2A style optical compressor (Teletronix / UA), BEHAVIORAL model calibrated on recordings of a real LA-2A.
//
// Fonti (2026-10-18):
//  - UA manual of the Model LA-2A (https://media.uaudio.com/assetlibrary/l/a/la-2a_manual.pdf): gain 40 dB +-1, reduction up to 40 dB, response +-0.1 dB 30 Hz-15 kHz, distortion < 0.35% at +10 dBm; very fast
//    attack; release about 0.06 s for 50% and from 0.5 to 5 s for the full one, depending on the previous reduction (two-stage cadmium sulfide photocell with "memory"); FEEDBACK compressor
//    (the sidechain listens to the already reduced signal); Peak Reduction = sidechain gain (threshold and amount), Gain = output level, it does not affect the compression; Compress/Limit changes the
//    input/output curve (higher ratio in Limit); EL panel (HA-100X) driven by a 6AQ5 after a 12AX7 and an adjustable emphasis (R37; flat as from the factory).
//  - Data: SignalTrain LA2A Dataset v1.1 (Colburn and Hawley, https://zenodo.org/records/3824876, CC BY 4.0): recordings of an analog LA-2A as Peak Reduction varies. 3 complete, aligned pairs used
//    (900 s at 44.1 kHz each): Compress PR 65, Limit PR 65, Limit PR 80 (in the file name the first number is 0 for Compress and 1 for Limit: at equal level the reduction is greater with 1).
//
// Calibration (Nelder-Mead, 5 random restarts, gain reduction in 5 ms windows): on the first 300 s of music the error is 0.5-0.6 dB; on the synthetic noise not used in the calibration (600 s) the average reduction
// differs by 1.2-2.2 dB (the model compresses a bit more) and the short-term variation is about half that of the real unit. Thresholds: -21.6 dB (PR 65), -27.6 dB (PR 80), that is -0.40 dB per
// Peak Reduction point: between 65 and 80 it is measured, OUTSIDE that range it is a linear extrapolation. Ratios and the Compress/Limit distinction are poorly determined by the data alone (from 3.7 to 10 across the restarts).
// Not modelled: distortion of the tubes and of the transformer, noise, frequency response of the audio path, true feedback (here the sidechain is feed-forward with an equivalent curve).
//
// Structure: 3-band detector (<400 Hz, 400-2500 Hz, >2500 Hz) with calibrated weights (the treble weighs more: the EL panel is a capacitor), dB curve with soft knee, two stages
// (fast and slow) with attack/release, the slow stage releases more slowly the more it has been compressed (cell memory).
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

const float kW0 = 1.067f, kW2 = 1.709f;				// band weights in the sidechain (power): exp(0.0645), exp(0.5359)
const float kT65[2] = {-21.825f, -21.570f};			// threshold at PR 65 (dB, power per 1 ms of the detector), Compress and Limit
const float kTSlope = -0.4032f;					// dB per Peak Reduction point (from the thresholds at PR 65 and 80 in Limit)
const float kRatio[2] = {8.61f, 10.10f};
const float kKnee = 2.0f;
const float kWf = 0.763f;						// fast fraction
const float kTfa = 0.00485f, kTfd = 0.01814f;			// attack and release of the fast stage (s)
const float kTsa = 0.7725f, kTsd = 2.6815f, kMem = 2.1228f;	// slow stage: attack, base release, memory

// fast log2 (error < 3e-5): exponent from the bits, mantissa with a polynomial
inline float FastLog2 (float x)
{
	union { float f; u32 u; } v;
	v.f = x;
	const int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
	const float m = v.f - 1.0f;		// 0..1
	return (float) e + m * (1.4418258f + m * (-0.708682101f + m * (0.415421949f + m * (-0.194422672f + m * 0.0458855267f))));
}

inline float Ex (float x)
{
	x = x < -80.0f ? -80.0f : (x > 80.0f ? 80.0f : x);
	const float t = x * 1.44269504f;
	int n = (int) t;
	if ((float) n > t)
	{
		n--;
	}
	const float r = (t - (float) n) * 0.69314718f;
	const float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f + r * (0.041666668f + r * (0.0083333338f + r * (0.0013888889f + r * 0.0001984127f))))));
	union { float f; u32 u; } s;
	s.u = (u32) (n + 127) << 23;
	return p * s.f;
}

struct TBq { float b0, b1, b2, a1, a2; };
struct TBqState { float z1, z2; };

inline float Run (const TBq &c, TBqState &s, float x)
{
	const float y = c.b0 * x + s.z1;
	s.z1 = c.b1 * x - c.a1 * y + s.z2;
	s.z2 = c.b2 * x - c.a2 * y;
	return y;
}

void Design (TBq *c, bool bHigh, float fHz, float fQ, float fRate)
{
	const float w0 = 6.2831853f * fHz / fRate;
	const float cs = DspCos (w0), sn = DspSin (w0), al = sn / (2.0f * fQ), a0 = 1.0f + al;
	if (bHigh)
	{
		c->b0 = (1.0f + cs) * 0.5f / a0; c->b1 = -(1.0f + cs) / a0; c->b2 = c->b0;
	}
	else
	{
		c->b0 = (1.0f - cs) * 0.5f / a0; c->b1 = (1.0f - cs) / a0; c->b2 = c->b0;
	}
	c->a1 = -2.0f * cs / a0;
	c->a2 = (1.0f - al) / a0;
}

struct TChan
{
	TBqState lo[2], mhp, mlp, hi[2];
	float pdet;
};

// compression state of one channel (with Link on, the two channels receive the same level and stay equal)
struct TDyn
{
	float grF, grS, g, gPrev, grOut, pmax;
};

class CLa2a : public CFx
{
public:
	CLa2a (float fRate) : m_fRate (fRate)
	{
		Design (&m_Lo[0], false, 400.0f, 0.5411961f, fRate);
		Design (&m_Lo[1], false, 400.0f, 1.3065630f, fRate);
		Design (&m_Mhp, true, 400.0f, 0.7071068f, fRate);
		Design (&m_Mlp, false, 2500.0f, 0.7071068f, fRate);
		Design (&m_Hi[0], true, 2500.0f, 0.5411961f, fRate);
		Design (&m_Hi[1], true, 2500.0f, 1.3065630f, fRate);
		m_fADet = 1.0f - Ex (-1.0f / (fRate * 0.0007f));
		const float dt = (float) NC / fRate;
		m_fAFa = 1.0f - Ex (-dt / kTfa);
		m_fAFd = 1.0f - Ex (-dt / kTfd);
		m_fASa = 1.0f - Ex (-dt / kTsa);
		m_fDt = dt;
		for (unsigned c = 0; c < 2; c++)
		{
			memset (&m_Ch[c], 0, sizeof m_Ch[c]);
		}
		for (unsigned c = 0; c < 2; c++)
		{
			m_Dyn[c].grF = m_Dyn[c].grS = m_Dyn[c].grOut = m_Dyn[c].pmax = 0.0f;
			m_Dyn[c].g = m_Dyn[c].gPrev = 1.0f;
		}
		m_nPos = 0;
		m_bLink = true;
	}

	unsigned Type (void) const { return FxOpticalLA; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float fIn = DbToLin (pP[3]);
		const bool bLink = pP[4] > 0.5f;
		m_bLink = bLink;
		const unsigned nCh = *pMono ? 1 : 2;
		// with Link On the right channel uses the left one's controls, with Link Off it has its own (pr2, mode2, gain2)
		float fPr[2], fMakeup[2], fT[2], fSlope[2];
		for (unsigned c = 0; c < 2; c++)
		{
			const bool bOwn = c == 1 && !bLink && nCh > 1;
			const float pr = bOwn ? pP[6] : pP[0];
			const int nMode = (bOwn ? pP[7] : pP[1]) > 0.5f ? 1 : 0;
			fPr[c] = pr < 0.0f ? 0.0f : (pr > 100.0f ? 100.0f : pr);
			fMakeup[c] = DbToLin (bOwn ? pP[8] : pP[2]);
			fT[c] = kT65[nMode] + kTSlope * (fPr[c] - 65.0f);
			fSlope[c] = 1.0f - 1.0f / kRatio[nMode];
		}
		for (unsigned j = 0; j < n; j++)
		{
			float pw[2] = {0.0f, 0.0f};
			float x[2] = {pL[j] * fIn, pR[j] * fIn};
			for (unsigned c = 0; c < nCh; c++)
			{
				TChan &S = m_Ch[c];
				const float v = x[c];
				float lo = Run (m_Lo[0], S.lo[0], v);
				lo = Run (m_Lo[1], S.lo[1], lo);
				float md = Run (m_Mhp, S.mhp, v);
				md = Run (m_Mlp, S.mlp, md);
				float hi = Run (m_Hi[0], S.hi[0], v);
				hi = Run (m_Hi[1], S.hi[1], hi);
				const float p = kW0 * lo * lo + md * md + kW2 * hi * hi;
				S.pdet += m_fADet * (p - S.pdet);
				pw[c] = S.pdet;
			}
			for (unsigned c = 0; c < nCh; c++)
			{
				float p = pw[c];
				if (nCh > 1 && bLink && pw[1 - c] > p)
				{
					p = pw[1 - c];
				}
				if (p > m_Dyn[c].pmax)
				{
					m_Dyn[c].pmax = p;
				}
			}
			if (++m_nPos >= NC)
			{
				m_nPos = 0;
				for (unsigned c = 0; c < nCh; c++)
				{
					Control (m_Dyn[c], m_Dyn[c].pmax, fT[c], fSlope[c], fPr[c]);
					m_Dyn[c].pmax = 0.0f;
				}
			}
			// gain interpolated between one update and the next
			const float fr = (float) m_nPos / (float) NC;
			const float g0 = (m_Dyn[0].gPrev + (m_Dyn[0].g - m_Dyn[0].gPrev) * fr) * fMakeup[0];
			pL[j] = x[0] * g0;
			if (nCh > 1)
			{
				pR[j] = x[1] * (m_Dyn[1].gPrev + (m_Dyn[1].g - m_Dyn[1].gPrev) * fr) * fMakeup[1];
			}
			else
			{
				pR[j] = pL[j];
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
		}
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < FX_MAX_P; i++) pOut[i] = 0.0f;
		const float a = -m_Dyn[0].grOut, b = -m_Dyn[1].grOut;
		pOut[5] = m_bLink ? (a > b ? a : b) : a;		// reduction in dB, positive: with Link On the larger of the two channels, with Link Off that of the left one
		pOut[9] = b;		// right channel
	}

private:
	enum { NC = 16 };

	// one control step (every 16 samples): detector level -> curve -> two stages -> gain
	void Control (TDyn &D, float fPdet, float fT, float fSlope, float fPr)
	{
		const float fL = 3.0103f * FastLog2 (fPdet + 1e-12f);		// dB
		const float xk = (fL - fT) / kKnee;
		float sp = kKnee * (xk > 20.0f ? xk : 0.6931472f * FastLog2 (1.0f + Ex (xk)));	// softplus in dB (ln -> log2 * ln2)
		if (fPr < 0.25f)
		{
			sp = 0.0f;		// Peak Reduction at zero: no compression
		}
		const float gt = -fSlope * sp;
		const float gft = kWf * gt, gst = (1.0f - kWf) * gt;
		D.grF += (gft - D.grF) * (gft < D.grF ? m_fAFa : m_fAFd);
		float aS;
		if (gst < D.grS)
		{
			aS = m_fASa;
		}
		else
		{
			const float ts = kTsd * (1.0f + kMem * (-D.grS) / (-D.grS + 3.0f));
			aS = 1.0f - Ex (-m_fDt / ts);
		}
		D.grS += (gst - D.grS) * aS;
		D.grOut = D.grF + D.grS;
		D.gPrev = D.g;
		D.g = DbToLin (D.grOut);
	}

	float m_fRate, m_fADet, m_fAFa, m_fAFd, m_fASa, m_fDt;
	TBq m_Lo[2], m_Mhp, m_Mlp, m_Hi[2];
	TChan m_Ch[2];
	TDyn m_Dyn[2];
	unsigned m_nPos;
	bool m_bLink;
};

}

CFx *NewOpticalLA (float fRate)
{
	return new CLa2a (fRate);
}
