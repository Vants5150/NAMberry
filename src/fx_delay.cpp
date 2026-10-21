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
		m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (pP));
		*pMono = 0;
	}

private:
	adeldelay::Delay m_Eng;
	float m_fIn[PL_MAX_BLOCK];
};

CFx *NewAdelDelay (float fRate)
{
	return new CAdelDelay (fRate);
}
