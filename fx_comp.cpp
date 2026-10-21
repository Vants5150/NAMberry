//
// fx_comp.cpp - ADELComp (compressor): the PiPedal plugins' engine, unchanged (adel/adelcomp_dsp.hpp)
//
#include "fx.h"
#include "adelcomp_map.hpp"

class CAdelComp : public CFx
{
public:
	CAdelComp (float fRate)
	{
		m_Eng.init ((double) fRate, 2);
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			m_fOut[i] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxAdelComp; }

	// the path is always stereo (two identical channels if the path is mono): the reduction is the same on both,
	// so a mono path stays mono
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		(void) pMono;
		const float *pIn[2] = {pL, pR};
		float *pOut[2] = {pL, pR};
		m_Eng.process (pIn, pOut, n, paramsFromControls (pP));
		m_fOut[C_GR] = m_Eng.grDb ();
		m_fOut[C_INLEVEL] = m_Eng.inDb ();
		m_fOut[C_OUTLEVEL] = m_Eng.outDb ();
		m_fOut[C_LATENCY] = m_Eng.latencySamples ();
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < C_COUNT; i++)
		{
			pOut[i] = m_fOut[i];
		}
	}

private:
	adelcomp::Comp m_Eng;
	float m_fOut[FX_MAX_P];
};

CFx *NewAdelComp (float fRate)
{
	return new CAdelComp (fRate);
}
