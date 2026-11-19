//
// fx_delay.cpp - ADELdelay: the PiPedal plugins' engine, unchanged (adel/adeldelay_dsp.hpp).
// Mono input, stereo output: on a stereo path the engine receives the sum (L+R)/2, like the NAM.
//
#include "fx.h"
#include "adeldelay_map.hpp"

class CAdelDelay : public CFx
{
public:
	CAdelDelay (float fRate)
	{
		m_Eng.init ((double) fRate);
	}

	unsigned Type (void) const { return FxAdelDelay; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		for (unsigned j = 0; j < n; j++)
		{
			m_fIn[j] = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
		}
		// Follow tap tempo: the base time is the quarter note of the master tempo (Rhythm applies to it, Glide smooths the change); the stored Time stays as set
		const float fMs = FxFollowMs (pP[kFollow]);
		if (fMs > 0.0f)
		{
			for (unsigned i = 0; i < kFollow; i++)
			{
				m_fV[i] = pP[i];
			}
			m_fV[C_TIME] = fMs < 20.0f ? 20.0f : (fMs > 2000.0f ? 2000.0f : fMs);
			m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (m_fV));
		}
		else
		{
			m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (pP));
		}
		*pMono = 0;
	}

private:
	static const unsigned kFollow = C_COUNT;		// the Follow tap tempo switch is the control after the last one of the engine
	static_assert (sizeof g_adeldelay_Params / sizeof g_adeldelay_Params[0] == C_COUNT + 1, "the delay controls are the engine's plus Follow tap tempo");
	adeldelay::Delay m_Eng;
	float m_fIn[PL_MAX_BLOCK];
	float m_fV[FX_MAX_P];
};

CFx *NewAdelDelay (float fRate)
{
	return new CAdelDelay (fRate);
}
