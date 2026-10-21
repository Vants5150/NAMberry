//
// fx_sweep.cpp - ADELSendSweep (probe): the PiPedal plugins' engine, unchanged (adel/adelsendsweep_dsp.hpp).
// It plays a sweep, guitar notes, a chord or pink noise with one button (Play), to measure without playing (for example the phase
// with ADELphase after an L/R Split). It goes BEFORE the Split. The engine is mono: on a stereo path two of them run, identical and
// synchronized (same seed, same commands), so the sample is the same on the two channels.
//
#include "fx.h"
#include "adel_probe_shm.hpp"
#include "adelsendsweep_map.hpp"

static volatile bool s_bProbePlaying = false;		// the probe is playing (read by the page)

// controls added for Circle at the end of the plugin's list: Repeat (input), Mute Output (input) and Playing (output)
static const unsigned C_LOOP_I = 5, C_MUTEOUT_I = 6, C_PLAYING_I = 7, C_ALL = 8;
static volatile bool s_bProbeMuteOut = false;		// the probe is playing and asks for the outputs to be muted (consumed by the audio callback)

class CAdelSweep : public CFx
{
public:
	CAdelSweep (float fRate)
	{
		m_EngL.init ((double) fRate);
		m_EngR.init ((double) fRate);
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			m_fOut[i] = 0.0f;
		}
	}

	~CAdelSweep (void)
	{
		s_bProbePlaying = false;
		s_bProbeMuteOut = false;
	}

	unsigned Type (void) const { return FxAdelSweep; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		adelsendsweep::Params Par = paramsFromControls (pP);
		// Repeat: with Play on, at the end of the sample the command is lowered for one block and raised again: the engine starts on the rising edge
		if (pP[C_LOOP_I] > 0.5f && Par.trigger)
		{
			if (m_bGap)
			{
				m_bGap = false;
			}
			else if (m_bEnded)
			{
				m_bEnded = false;
				m_bGap = true;
				Par.trigger = false;
			}
		}
		else
		{
			m_bGap = false;
			m_bEnded = false;
		}
		const boolean bBefore = m_EngL.playing ();
		m_EngL.process (pL, pL, n, Par);
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++)
			{
				pR[j] = pL[j];
			}
		}
		else
		{
			m_EngR.process (pR, pR, n, Par);
		}
		m_fOut[C_PROGRESS] = m_EngL.progressPct ();
		m_fOut[C_PLAYING_I] = m_EngL.playing () ? 1.0f : 0.0f;
		if (bBefore && !m_EngL.playing ())
		{
			m_bEnded = TRUE;		// the sample ended in this block: with Repeat it restarts on the next round
		}
		s_bProbePlaying = m_EngL.playing ();
		if (m_EngL.playing () && pP[C_MUTEOUT_I] > 0.5f)
		{
			s_bProbeMuteOut = true;
		}
		// like the plugin for PiPedal: tells ADELphase that the sample is playing
		adelprobe_shm::Shared *pSh = adelprobe_shm::get ();
		pSh->playing.store (m_EngL.playing () ? 1u : 0u, std::memory_order_relaxed);
		pSh->beat.fetch_add (1u, std::memory_order_relaxed);
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < C_ALL; i++)
		{
			pOut[i] = m_fOut[i];
		}
	}

private:
	adelsendsweep::Probe m_EngL, m_EngR;
	float m_fOut[FX_MAX_P];
	boolean m_bGap = FALSE, m_bEnded = FALSE;
};

unsigned FxProbeMuteOut (void)
{
	const bool b = s_bProbeMuteOut;
	s_bProbeMuteOut = false;
	return b ? 1 : 0;
}

unsigned FxProbePlaying (void)
{
	return s_bProbePlaying ? 1 : 0;
}

CFx *NewAdelSweep (float fRate)
{
	return new CAdelSweep (fRate);
}
