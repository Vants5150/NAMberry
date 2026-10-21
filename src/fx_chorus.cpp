//
// fx_chorus.cpp - ADELchorus (BBD chorus / vibrato inspired by the Boss CE-1, CE-2, CH-1): engine in adel/adelchorus_dsp.hpp.
// Mono input, stereo output: on a stereo path the engine receives the sum (L+R)/2, like the NAM.
//
#include "fx.h"
#include "adelchorus_map.hpp"

class CAdelChorus : public CFx
{
public:
	CAdelChorus (float fRate)
	{
		m_Eng.init ((double) fRate);
		m_Eng.setMonoOut (false);
	}

	unsigned Type (void) const { return FxAdelChorus; }

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
	adelchorus::Chorus m_Eng;
	float m_fIn[PL_MAX_BLOCK];
};

CFx *NewAdelChorus (float fRate)
{
	return new CAdelChorus (fRate);
}
