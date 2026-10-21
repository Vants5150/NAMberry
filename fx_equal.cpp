//
// fx_equal.cpp - ADELequal: equalizer for guitar. Low cut and high cut (2nd-order Butterworth), bass and treble shelves and three
// parametric bands (frequency, gain, Q); RBJ biquads. The coefficients are recomputed only when a parameter changes.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

#define EQ_NP		16
#define EQ_STAGES	7	// 0 low cut, 1 bass shelf, 2..4 bands, 5 treble shelf, 6 high cut

class CAdelEqual : public CFx
{
public:
	CAdelEqual (float fRate)
	:	m_fRate (fRate), m_bValid (FALSE)
	{
		for (unsigned c = 0; c < 2; c++)
		{
			for (unsigned s = 0; s < EQ_STAGES; s++)
			{
				m_z1[c][s] = m_z2[c][s] = 0.0f;
			}
		}
		for (unsigned s = 0; s < EQ_STAGES; s++)
		{
			m_On[s] = FALSE;
		}
		for (unsigned k = 0; k < EQ_NP; k++)
		{
			m_fKey[k] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxAdelEqual; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		Setup (pP);
		const float fOut = m_fOut;
		const unsigned nCh = *pMono != 0 ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned s = 0; s < EQ_STAGES; s++)
			{
				if (!m_On[s])
				{
					continue;
				}
				const float b0 = m_b0[s], b1 = m_b1[s], b2 = m_b2[s], a1 = m_a1[s], a2 = m_a2[s];
				float z1 = m_z1[c][s], z2 = m_z2[c][s];
				for (unsigned j = 0; j < n; j++)
				{
					const float x = p[j];
					const float y = b0 * x + z1;
					z1 = b1 * x - a1 * y + z2;
					z2 = b2 * x - a2 * y;
					p[j] = y;
				}
				m_z1[c][s] = z1;
				m_z2[c][s] = z2;
			}
			for (unsigned j = 0; j < n; j++)
			{
				p[j] *= fOut;
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++)
			{
				pR[j] = pL[j];
			}
		}
	}

private:
	static float Clamp (float v, float lo, float hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	void Set (unsigned s, float b0, float b1, float b2, float a0, float a1, float a2)
	{
		const float r = 1.0f / a0;
		m_b0[s] = b0 * r;
		m_b1[s] = b1 * r;
		m_b2[s] = b2 * r;
		m_a1[s] = a1 * r;
		m_a2[s] = a2 * r;
		m_On[s] = TRUE;
	}

	void Peak (unsigned s, float fFreq, float fGain, float fQ)
	{
		if (fGain > -0.05f && fGain < 0.05f)
		{
			m_On[s] = FALSE;
			return;
		}
		const float A = DbToLin (fGain * 0.5f);		// 10^(dB/40)
		const float w0 = 6.2831853f * Clamp (fFreq, 20.0f, 0.45f * m_fRate) / m_fRate;
		const float cw = DspCos (w0), al = DspSin (w0) / (2.0f * Clamp (fQ, 0.1f, 20.0f));
		Set (s, 1.0f + al * A, -2.0f * cw, 1.0f - al * A, 1.0f + al / A, -2.0f * cw, 1.0f - al / A);
	}

	void Shelf (unsigned s, boolean bHigh, float fFreq, float fGain)
	{
		if (fGain > -0.05f && fGain < 0.05f)
		{
			m_On[s] = FALSE;
			return;
		}
		const float A = DbToLin (fGain * 0.5f);
		const float sA = DbToLin (fGain * 0.25f);		// 10^(dB/80)
		const float w0 = 6.2831853f * Clamp (fFreq, 20.0f, 0.45f * m_fRate) / m_fRate;
		const float cw = DspCos (w0), al2 = DspSin (w0) * 0.70710678f * 2.0f * sA;	// 2 sqrt(A) alpha, slope S = 1
		if (!bHigh)
		{
			Set (s, A * ((A + 1.0f) - (A - 1.0f) * cw + al2), 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw), A * ((A + 1.0f) - (A - 1.0f) * cw - al2),
			     (A + 1.0f) + (A - 1.0f) * cw + al2, -2.0f * ((A - 1.0f) + (A + 1.0f) * cw), (A + 1.0f) + (A - 1.0f) * cw - al2);
		}
		else
		{
			Set (s, A * ((A + 1.0f) + (A - 1.0f) * cw + al2), -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw), A * ((A + 1.0f) + (A - 1.0f) * cw - al2),
			     (A + 1.0f) - (A - 1.0f) * cw + al2, 2.0f * ((A - 1.0f) - (A + 1.0f) * cw), (A + 1.0f) - (A - 1.0f) * cw - al2);
		}
	}

	void Cut (unsigned s, boolean bHigh, float fFreq)
	{
		const float w0 = 6.2831853f * Clamp (fFreq, 10.0f, 0.45f * m_fRate) / m_fRate;
		const float cw = DspCos (w0), al = DspSin (w0) / (2.0f * 0.70710678f);
		if (!bHigh)		// high-pass
		{
			Set (s, (1.0f + cw) * 0.5f, -(1.0f + cw), (1.0f + cw) * 0.5f, 1.0f + al, -2.0f * cw, 1.0f - al);
		}
		else			// low-pass
		{
			Set (s, (1.0f - cw) * 0.5f, 1.0f - cw, (1.0f - cw) * 0.5f, 1.0f + al, -2.0f * cw, 1.0f - al);
		}
	}

	// indices: 0 level, 1 lowcut, 2 highcut, 3 lsfreq, 4 lsgain, 5..7 band 1 (freq, gain, Q), 8..10 band 2, 11..13 band 3, 14 hsfreq, 15 hsgain
	void Setup (const float *pP)
	{
		boolean bSame = m_bValid;
		for (unsigned k = 0; k < EQ_NP && bSame; k++)
		{
			bSame = pP[k] == m_fKey[k];
		}
		if (bSame)
		{
			return;
		}
		for (unsigned k = 0; k < EQ_NP; k++)
		{
			m_fKey[k] = pP[k];
		}
		m_bValid = TRUE;
		m_fOut = DbToLin (Clamp (pP[0], -24.0f, 24.0f));
		if (pP[1] > 20.5f)
		{
			Cut (0, FALSE, pP[1]);
		}
		else
		{
			m_On[0] = FALSE;
		}
		Shelf (1, FALSE, pP[3], pP[4]);
		Peak (2, pP[5], pP[6], pP[7]);
		Peak (3, pP[8], pP[9], pP[10]);
		Peak (4, pP[11], pP[12], pP[13]);
		Shelf (5, TRUE, pP[14], pP[15]);
		if (pP[2] < 19999.0f)
		{
			Cut (6, TRUE, pP[2]);
		}
		else
		{
			m_On[6] = FALSE;
		}
	}

	float m_fRate;
	boolean m_bValid;
	float m_fKey[EQ_NP];
	float m_fOut;
	boolean m_On[EQ_STAGES];
	float m_b0[EQ_STAGES], m_b1[EQ_STAGES], m_b2[EQ_STAGES], m_a1[EQ_STAGES], m_a2[EQ_STAGES];
	float m_z1[2][EQ_STAGES], m_z2[2][EQ_STAGES];
};

CFx *NewAdelEqual (float fRate)
{
	return new CAdelEqual (fRate);
}
