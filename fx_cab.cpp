//
// fx_cab.cpp - cabinet IR: direct NEON convolution with the impulse response loaded from file (.air on the board).
// The IR is prepared (reversed, padded to multiples of 16) in one of the two spaces and activated with a single store: the audio callback
// always uses a complete space. Mono path: a single convolution channel. Stereo IR: one channel per side.
//
#include "fx.h"
#include "cabir.h"
#include "dsp_util.h"
#include <circle/util.h>

class CCabIR : public CFx
{
public:
	CCabIR (float fRate)
	:	m_fRate (fRate), m_nActive (0), m_bFilt (FALSE)
	{
		for (unsigned s = 0; s < 2; s++)
		{
			m_nTaps[s] = 0;
			m_nCh[s] = 1;
			for (unsigned c = 0; c < 2; c++)
			{
				m_pRev[s][c] = new float[CABIR_MAX_TAPS];
				m_pHist[s][c] = new float[2 * CABIR_MAX_TAPS];
			}
		}
		for (unsigned i = 0; i < 4; i++)
		{
			m_Bq[i].z1 = m_Bq[i].z2 = 0.0f;
		}
		m_fKeyLo = m_fKeyHi = -1.0f;
	}

	~CCabIR (void)
	{
		for (unsigned s = 0; s < 2; s++)
		{
			for (unsigned c = 0; c < 2; c++)
			{
				delete[] m_pRev[s][c];
				delete[] m_pHist[s][c];
			}
		}
	}

	unsigned Type (void) const { return FxCabIR; }

	// pData: nCh channels of nTaps floats (one after the other). Returns FALSE if the values are not usable.
	boolean SetImpulse (const float *pData, unsigned nTaps, unsigned nCh)
	{
		if (nTaps == 0)
		{
			// no IR: the signal passes
			const unsigned s0 = m_nActive ^ 1;
			m_nTaps[s0] = 0;
			asm volatile ("dmb ish" ::: "memory");
			m_nActive = s0;
			return TRUE;
		}
		if (nCh < 1 || nCh > 2)
		{
			return FALSE;
		}
		if (nTaps > CABIR_MAX_TAPS)
		{
			nTaps = CABIR_MAX_TAPS;
		}
		const unsigned nPad = (16 - (nTaps & 15)) & 15;
		const unsigned T = nTaps + nPad;
		const unsigned s = m_nActive ^ 1;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *pR = m_pRev[s][c];
			for (unsigned i = 0; i < nPad; i++)
			{
				pR[i] = 0.0f;
			}
			for (unsigned k = 0; k < nTaps; k++)
			{
				pR[nPad + k] = pData[c * nTaps + (nTaps - 1 - k)];
			}
		}
		for (unsigned c = 0; c < 2; c++)
		{
			for (unsigned i = 0; i < 2 * T; i++)
			{
				m_pHist[s][c][i] = 0.0f;
			}
			m_Ch[s][c].pHist = m_pHist[s][c];
			m_Ch[s][c].nWrite = 0;
		}
		m_nCh[s] = nCh;
		m_nTaps[s] = T;
		asm volatile ("dmb ish" ::: "memory");
		m_nActive = s;
		return TRUE;
	}

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const unsigned s = m_nActive;
		const unsigned T = m_nTaps[s];
		if (T == 0)
		{
			return;		// no IR: the signal passes
		}
		const float fLevel = DbToLin (pP[0] < -24.0f ? -24.0f : (pP[0] > 24.0f ? 24.0f : pP[0]));
		const float fMix = (pP[1] < 0.0f ? 0.0f : (pP[1] > 100.0f ? 100.0f : pP[1])) * 0.01f;
		Setup (pP[2], pP[3]);
		const boolean bMono = *pMono != 0;
		const boolean bStereoIr = m_nCh[s] == 2;
		for (unsigned c = 0; c < (bMono && !bStereoIr ? 1u : 2u); c++)
		{
			float *p = c ? pR : pL;
			const float *pRev = m_pRev[s][bStereoIr ? c : 0];
			TCabChannel &Ch = m_Ch[s][c];
			TBq &Hp = m_Bq[c], &Lp = m_Bq[2 + c];
			for (unsigned j = 0; j < n; j++)
			{
				const float x = p[j];
				float y = Ch.Step (x, pRev, T) * fLevel;
				y = Biq (Lp, Biq (Hp, y));
				p[j] = x + (y - x) * fMix;
			}
		}
		if (bMono && !bStereoIr)
		{
			for (unsigned j = 0; j < n; j++)
			{
				pR[j] = pL[j];
			}
		}
		else if (bMono && bStereoIr)
		{
			*pMono = 0;		// stereo IR: L and R different
		}
	}

private:
	struct TBq
	{
		float b0, b1, b2, a1, a2, z1, z2;
	};

	static float Biq (TBq &q, float x)
	{
		const float y = q.b0 * x + q.z1;
		q.z1 = q.b1 * x - q.a1 * y + q.z2;
		q.z2 = q.b2 * x - q.a2 * y;
		return y;
	}

	// second-order high-pass and low-pass (Butterworth); coefficients recomputed only if the frequencies change
	void Setup (float fLo, float fHi)
	{
		if (fLo == m_fKeyLo && fHi == m_fKeyHi)
		{
			return;
		}
		m_fKeyLo = fLo;
		m_fKeyHi = fHi;
		const float fL = fLo < 20.0f ? 20.0f : (fLo > 1000.0f ? 1000.0f : fLo);
		const float fH = fHi < 1000.0f ? 1000.0f : (fHi > 0.45f * m_fRate ? 0.45f * m_fRate : fHi);
		for (unsigned c = 0; c < 2; c++)
		{
			{
				const float w0 = 6.2831853f * fL / m_fRate;
				const float cw = DspCos (w0), sw = DspSin (w0), al = sw / (2.0f * 0.70710678f), a0 = 1.0f + al;
				TBq &q = m_Bq[c];
				q.b0 = (1.0f + cw) * 0.5f / a0; q.b1 = -(1.0f + cw) / a0; q.b2 = q.b0;
				q.a1 = -2.0f * cw / a0; q.a2 = (1.0f - al) / a0;
			}
			{
				const float w0 = 6.2831853f * fH / m_fRate;
				const float cw = DspCos (w0), sw = DspSin (w0), al = sw / (2.0f * 0.70710678f), a0 = 1.0f + al;
				TBq &q = m_Bq[2 + c];
				q.b0 = (1.0f - cw) * 0.5f / a0; q.b1 = (1.0f - cw) / a0; q.b2 = q.b0;
				q.a1 = -2.0f * cw / a0; q.a2 = (1.0f - al) / a0;
			}
		}
	}

	float m_fRate;
	float *m_pRev[2][2];
	float *m_pHist[2][2];
	TCabChannel m_Ch[2][2];
	unsigned m_nTaps[2], m_nCh[2];
	volatile unsigned m_nActive;
	TBq m_Bq[4];
	float m_fKeyLo, m_fKeyHi;
	boolean m_bFilt;
};

CFx *NewCabIR (float fRate)
{
	return new CCabIR (fRate);
}
