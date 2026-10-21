//
// fx_verbm.cpp - ADELverb MONO: the same reverb engine with the engine's mono output (setMonoOut), for the mono lanes.
// Mono input, mono output (L and R identical); only the Width control is missing, which only makes sense in stereo.
//
#include "fx.h"
#include "adelverb_map.hpp"

class CAdelVerbM : public CFx
{
public:
	CAdelVerbM (float fRate)
	{
		m_Eng.init ((double) fRate);
		m_Eng.setMonoOut (true);
	}

	unsigned Type (void) const { return FxAdelVerbM; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		// paramsFromControls uses the port indices (P_MIX = 3); the excluded controls go back to the default values
		const unsigned nMono = sizeof g_adelverbm_Map / sizeof g_adelverbm_Map[0];
		float v[P_COUNT];
		for (int i = 0; i < P_MIX; i++)
		{
			v[i] = 0.0f;
		}
		for (int i = P_MIX; i < P_COUNT; i++)
		{
			v[i] = g_adelverb_Params[i - P_MIX].Def;
		}
		for (unsigned j = 0; j < nMono; j++)
		{
			v[P_MIX + g_adelverbm_Map[j]] = pP[j];
		}
		for (unsigned j = 0; j < n; j++)
		{
			m_fIn[j] = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
		}
		m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (v));
		for (unsigned j = 0; j < n; j++)
		{
			pR[j] = pL[j];
		}
		*pMono = 1;
	}

private:
	adelverb::Reverb m_Eng;
	float m_fIn[PL_MAX_BLOCK];
};

CFx *NewAdelVerbM (float fRate)
{
	return new CAdelVerbM (fRate);
}
