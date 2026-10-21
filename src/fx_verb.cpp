//
// fx_verb.cpp - ADELverb (reverb): the PiPedal plugins' engine, unchanged (adel/adelverb_dsp.hpp).
// Mono input, stereo output: on a stereo path the engine receives the sum (L+R)/2.
//
#include "fx.h"
#include "adelverb_map.hpp"

class CAdelVerb : public CFx
{
public:
	CAdelVerb (float fRate)
	{
		m_Eng.init ((double) fRate);
		m_Eng.setMonoOut (false);
	}

	unsigned Type (void) const { return FxAdelVerb; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		// paramsFromControls uses the port indices (P_MIX = 3: first there are the three audio ports); the block's values start from 0
		float v[P_COUNT];
		for (int i = 0; i < P_MIX; i++)
		{
			v[i] = 0.0f;
		}
		for (int i = P_MIX; i < P_COUNT; i++)
		{
			v[i] = pP[i - P_MIX];
		}
		for (unsigned j = 0; j < n; j++)
		{
			m_fIn[j] = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
		}
		m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (v));
		*pMono = 0;
	}

private:
	adelverb::Reverb m_Eng;
	float m_fIn[PL_MAX_BLOCK];
};

CFx *NewAdelVerb (float fRate)
{
	return new CAdelVerb (fRate);
}
