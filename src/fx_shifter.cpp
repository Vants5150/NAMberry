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

// ---- ADELshifter Manual: the same engine with the voices always in Chromatic mode and a per-note memory of the three voices' semitones (12 entries, see Params::noteMap) ----
class CShifterManual : public CFx
{
public:
	enum { P_DRY, P_LEVEL, P_GLIDE, P_TRACK, P_GATE, P_LOWHZ, P_V1 = 6, P_MAP = 18, P_PITCH = 30, P_NOTE = 31, P_VOICED = 32 };
	CShifterManual (float fRate)
	{
		m_Eng.init ((double) fRate);
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			m_fOut[i] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxShifterManual; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		for (unsigned j = 0; j < n; j++)
		{
			m_fIn[j] = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
		}
		adelshifter::Params q;
		q.dryDb = pP[P_DRY];
		q.levelDb = pP[P_LEVEL];
		q.glideMs = pP[P_GLIDE];
		q.tracking = int (pP[P_TRACK] + 0.5f);
		q.gateDb = pP[P_GATE];
		q.lowHz = pP[P_LOWHZ];
		q.driftCents = 0.0f;
		for (int i = 0; i < 3; ++i)
		{
			const float *v = pP + P_V1 + 4 * i;
			adelshifter::VoiceParams &vp = q.v[i];
			vp.on = v[0] > 0.5f;
			vp.mode = 1;
			vp.semi = v[1];
			vp.levelDb = v[2];
			vp.pan = v[3] * 0.01f;
		}
		q.noteMap = pP + P_MAP;
		m_Eng.process (m_fIn, pL, pR, n, q);
		m_fOut[P_PITCH] = m_Eng.pitchHz ();
		m_fOut[P_NOTE] = m_Eng.noteMidi ();
		m_fOut[P_VOICED] = m_Eng.voiced ();
		*pMono = 0;
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			pOut[i] = m_fOut[i];
		}
	}

private:
	adelshifter::Harmonizer m_Eng;
	float m_fIn[PL_MAX_BLOCK];
	float m_fOut[FX_MAX_P];
};

CFx *NewShifterManual (float fRate)
{
	return new CShifterManual (fRate);
}