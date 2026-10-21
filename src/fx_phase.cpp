//
// fx_phase.cpp - ADELphase (phase): the PiPedal plugins' engine, unchanged (adel/adelphase_dsp.hpp).
// It uses the stereo "Align" version: polarity, delay, all-pass and trim on the R path; it measures correlation, delay and
// verdict between L and R by itself (useful after an L/R Split to align two microphones). On a mono path L and R are equal.
//
#include "fx.h"
#include "adelphase_map.hpp"

class CAdelPhase : public CFx
{
public:
	CAdelPhase (float fRate)
	{
		m_Eng.init ((double) fRate, 2);
		m_Eng.activate (0);
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			m_fOut[i] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxAdelPhase; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float *pIn[2] = {pL, pR};
		float *pOut[2] = {pL, pR};
		// with L and R identical and no correction the output stays mono; with delay/polarity/all-pass it does not
		m_Eng.process (pIn, pOut, n, paramsFromControls (pP));
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++)
			{
				if (pL[j] != pR[j])
				{
					*pMono = 0;
					break;
				}
			}
		}
		m_fOut[C_VERDICT] = m_Eng.verdict ();
		m_fOut[C_STALE] = m_Eng.verdict () > 7.5f ? 1.0f : 0.0f;
		m_fOut[C_LOSS] = m_Eng.lossDb ();
		m_fOut[C_CORRIN] = m_Eng.corrInPct ();
		m_fOut[C_CORROUT] = m_Eng.corrOutPct ();
		m_fOut[C_LAG] = m_Eng.lagSamples ();
		m_fOut[C_SUGDELAY] = m_Eng.suggestDelayMs ();
		m_fOut[C_SUGPOL] = m_Eng.suggestPolarity ();
		m_fOut[C_SUGSTAGES] = m_Eng.suggestStages ();
		m_fOut[C_CONF] = m_Eng.confidencePct ();
		m_fOut[C_LATENCY] = m_Eng.latencySamples ();
		// suggested frequency: index in the list 100..4000 Hz (24 values on a logarithmic scale); 0 = rotation off
		const float f = m_Eng.suggestFreq ();
		int nIdx = 0;
		if (m_Eng.suggestStages () > 0.5f && f > 0.0f)
		{
			nIdx = 1 + std::max (0, std::min (23, (int) std::lround (23.0 * std::log (f / 100.0) / std::log (40.0))));
		}
		m_fOut[C_SUGFREQ] = (float) nIdx;
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < C_COUNT; i++)
		{
			pOut[i] = m_fOut[i];
		}
	}

private:
	adelphase::Phase m_Eng;
	float m_fOut[FX_MAX_P];
};

CFx *NewAdelPhase (float fRate)
{
	return new CAdelPhase (fRate);
}
