//
// fx_shifter.cpp - ADELshifter (3-voice harmonizer for monophonic guitar): the PiPedal plugins' engine, unchanged (adel/adelshifter_dsp.hpp).
// Mono input (on a stereo path the engine receives (L+R)/2), stereo output; pitch, note and "Tracking OK" as measured outputs.
//
#include "fx.h"
#include "adelshifter_map.hpp"

class CAdelShifter : public CFx
{
public:
	CAdelShifter (float fRate)
	{
		m_Eng.init ((double) fRate);
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			m_fOut[i] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxAdelShifter; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		for (unsigned j = 0; j < n; j++)
		{
			m_fIn[j] = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
		}
		m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (pP));
		m_fOut[C_PITCH] = m_Eng.pitchHz ();
		m_fOut[C_NOTE] = m_Eng.noteMidi ();
		m_fOut[C_VOICED] = m_Eng.voiced ();
		*pMono = 0;
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < C_COUNT; i++)
		{
			pOut[i] = m_fOut[i];
		}
	}

private:
	adelshifter::Harmonizer m_Eng;
	float m_fIn[PL_MAX_BLOCK];
	float m_fOut[FX_MAX_P];
};

CFx *NewAdelShifter (float fRate)
{
	return new CAdelShifter (fRate);
}
