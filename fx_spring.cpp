//
// fx_spring.cpp - ADELspring (spring reverb): the PiPedal plugins' engine, unchanged (adel/adelspring_dsp.hpp).
// Mono input, stereo output (the springs are distributed between left and right): on a stereo path the engine receives (L+R)/2.
//
#include "fx.h"
// DIAGNOSIS of the spring's bursts (2026-10-18): snapshot of the internal state at the first anomalous sample, read by the page with /m?b=<id> (indices 20..);
// plus, freezing of the state from 0.2-0.3 s earlier and of the input that follows it, sent in slices by the same request (see GetOutputs) to replay it on the PC.
// Off in the normal firmware; it is turned on by defining ADELSPRING_WANT_DEBUG (the cause of the bursts was the memory of the exp/pow results in fxmath.cpp, fixed on 2026-10-18)
#ifdef ADELSPRING_WANT_DEBUG
#define ADELSPRING_DEBUG 1
#endif
#include "adelspring_map.hpp"

#ifdef ADELSPRING_DEBUG
#define SPR_HIST		24576		// input samples kept (512 ms)
#define SPR_SNAP_EVERY		4800		// one saved state every 100 ms, 3 are kept
#define SPR_AFTER		2048		// samples to record after the burst before freezing
#endif

class CAdelSpring : public CFx
{
public:
	CAdelSpring (float fRate)
	{
		m_Eng.init ((double) fRate);
		m_Eng.setMonoOut (false);
#ifdef ADELSPRING_DEBUG
		for (unsigned i = 0; i < 3; i++)
		{
			m_pSnap[i] = new float[adelspring::Tank::stateFloats ()];
			m_nSnapAbs[i] = 0;
			m_bSnapOk[i] = false;
		}
		m_pHist = new float[SPR_HIST];
		m_pEnd = new float[adelspring::Tank::stateFloats ()];
		m_pDbgEnd = new float[5 * 4096];
		for (unsigned i = 0; i < SPR_HIST; i++)
		{
			m_pHist[i] = 0.0f;
		}
#endif
	}

	unsigned Type (void) const { return FxAdelSpring; }

#ifdef ADELSPRING_DEBUG
	~CAdelSpring (void)
	{
		for (unsigned i = 0; i < 3; i++)
		{
			delete[] m_pSnap[i];
		}
		delete[] m_pHist;
		delete[] m_pEnd;
		delete[] m_pDbgEnd;
	}

	// diagnosis outputs (the block's controls are 14: from 20 onwards). Small values multiplied by 1000 (the page shows them with 2 decimals).
	// After the freeze ([62] = 4242) every call returns a slice of 60 integer values (16-bit halves of the floats) of the frozen state and input:
	// [60] = slice number, [61] = total slices. The slices advance at every call and start over.
	// Stream: 8 header values (4242, state floats, history samples, state position lo16/hi16, final position lo16/hi16, 0),
	// then the state (two halves per float, low first), then the input history from the moment of the state (two halves per float).
	void GetOutputs (float *pOut) const
	{
		if (m_bFrozen)
		{
			const unsigned nStateHalves = 2 * (unsigned) adelspring::Tank::stateFloats ();
			const unsigned nDbgHalves = 2 * 5 * 4096;
			const unsigned nTotal = 8 + nStateHalves + 2 * SPR_HIST + nStateHalves + nDbgHalves;
			const unsigned nSlices = (nTotal + 59) / 60;
			const unsigned nSlice = m_nSlice % nSlices;
			m_nSlice = nSlice + 1;
			for (unsigned k = 0; k < 60; k++)
			{
				const unsigned v = nSlice * 60 + k;
				float f = 0.0f;
				if (v < 8)
				{
					const float H[8] = {4242.0f, (float) adelspring::Tank::stateFloats (), (float) SPR_HIST,
							    (float) (m_nFrozenSnapAbs & 0xFFFFu), (float) ((m_nFrozenSnapAbs >> 16) & 0xFFFFu),
							    (float) (m_nFrozenNow & 0xFFFFu), (float) ((m_nFrozenNow >> 16) & 0xFFFFu), 0.0f};
					f = H[v];
				}
				else if (v < 8 + nStateHalves)
				{
					const unsigned q = v - 8;
					union { float f; unsigned u; } c;
					c.f = m_pFrozenSnap[q >> 1];
					f = (float) ((q & 1) ? (c.u >> 16) : (c.u & 0xFFFFu));
				}
				else if (v >= 8 + nStateHalves + 2 * SPR_HIST + nStateHalves && v < nTotal)
				{
					const unsigned q = v - 8 - nStateHalves - 2 * SPR_HIST - nStateHalves;
					union { float f; unsigned u; } c;
					c.f = m_pDbgEnd[q >> 1];
					f = (float) ((q & 1) ? (c.u >> 16) : (c.u & 0xFFFFu));
				}
				else if (v < 8 + nStateHalves + 2 * SPR_HIST + nStateHalves && v >= 8 + nStateHalves + 2 * SPR_HIST)
				{
					const unsigned q = v - 8 - nStateHalves - 2 * SPR_HIST;
					union { float f; unsigned u; } c;
					c.f = m_pEnd[q >> 1];
					f = (float) ((q & 1) ? (c.u >> 16) : (c.u & 0xFFFFu));
				}
				else if (v < 8 + nStateHalves + 2 * SPR_HIST)
				{
					const unsigned q = v - 8 - nStateHalves;
					union { float f; unsigned u; } c;
					c.f = m_pHist[(m_nFrozenHistStart + (q >> 1)) % SPR_HIST];
					f = (float) ((q & 1) ? (c.u >> 16) : (c.u & 0xFFFFu));
				}
				pOut[k] = f;
			}
			pOut[60] = (float) nSlice;
			pOut[61] = (float) nSlices;
			pOut[62] = 4242.0f;
			return;
		}
		const adelspring::Tank::Dbg &D = m_Eng.dbg;
		pOut[20] = D.events; pOut[21] = D.calls; pOut[22] = D.spr; pOut[23] = D.fpcrBits;
		pOut[24] = D.x * 1000.0f; pOut[25] = D.xd * 1000.0f; pOut[26] = D.hp * 1000.0f; pOut[27] = D.v; pOut[28] = D.a; pOut[29] = D.wob * 1000.0f;
		pOut[30] = D.d; pOut[31] = D.yo; pOut[32] = D.g;
		pOut[33] = D.firstStage; pOut[34] = D.firstXn; pOut[35] = D.firstYn;
		pOut[36] = D.maxXh; pOut[37] = D.maxXhAt; pOut[38] = D.maxYh; pOut[39] = D.maxYhAt;
		pOut[40] = D.bufMax; pOut[41] = D.bufMaxAt; pOut[42] = D.n; pOut[43] = D.w;
		pOut[44] = D.maxIn * 1000.0f; pOut[45] = D.maxOutV;
		pOut[57] = m_fShakeEdges; pOut[58] = m_fShakeChanges; pOut[59] = m_fResetChanges; pOut[60] = m_fShakeMax; pOut[61] = m_fLastShake;
		pOut[46] = D.ev2; pOut[47] = D.yo2; pOut[48] = D.d2; pOut[49] = D.w2; pOut[50] = D.bufMax2; pOut[51] = D.bufMaxAt2; pOut[52] = D.readIdx2; pOut[53] = D.spr2; pOut[54] = D.v2; pOut[55] = D.x2 * 1000.0f; pOut[56] = D.bufAround;
		pOut[62] = 0.0f;
		pOut[63] = m_bFrozen ? 1.0f : 0.0f;
	}
#endif

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
#ifdef ADELSPRING_DEBUG
		// does the Shake parameter (index 10) go from off to on? And do the other parameters that reset the spring (Tank 4, Springs 5) change?
		if (pP[10] > 0.5f && m_fLastShake <= 0.5f) { m_fShakeEdges += 1.0f; }
		if (pP[10] != m_fLastShake) { m_fShakeChanges += 1.0f; }
		if (pP[4] != m_fLastTank || pP[5] != m_fLastSprings) { m_fResetChanges += 1.0f; }
		m_fLastShake = pP[10]; m_fLastTank = pP[4]; m_fLastSprings = pP[5]; m_fShakeMax = pP[10] > m_fShakeMax ? pP[10] : m_fShakeMax;
#endif
		for (unsigned j = 0; j < n; j++)
		{
			m_fIn[j] = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
		}
#ifdef ADELSPRING_DEBUG
		if (!m_bFrozen)
		{
			// state saved every 100 ms (three in rotation: the oldest is 200-300 ms old) and input history
			if (m_nSinceSnap >= SPR_SNAP_EVERY || !m_bSnapOk[m_nSnapNext])
			{
				m_Eng.exportState (m_pSnap[m_nSnapNext]);
				m_nSnapAbs[m_nSnapNext] = m_nAbs;
				m_bSnapOk[m_nSnapNext] = true;
				m_nSnapNext = (m_nSnapNext + 1) % 3;
				m_nSinceSnap = 0;
			}
			for (unsigned j = 0; j < n; j++)
			{
				m_pHist[(m_nAbs + j) % SPR_HIST] = m_fIn[j];
			}
			m_nAbs += n;
			m_nSinceSnap += n;
		}
#endif
		m_Eng.process (m_fIn, pL, pR, n, paramsFromControls (pP));
		*pMono = 0;
#ifdef ADELSPRING_DEBUG
		if (!m_bFrozen)
		{
			if (m_nFreezeAt == 0 && m_Eng.dbg.events > m_fEvSeen)
			{
				m_fEvSeen = m_Eng.dbg.events;
				m_nFreezeAt = m_nAbs + SPR_AFTER;
			}
			if (m_nFreezeAt != 0 && m_nAbs >= m_nFreezeAt)
			{
				// freezes: the oldest saved state (the one about to be overwritten) and the input history from that moment on
				const unsigned nOld = m_nSnapNext;
				if (m_bSnapOk[nOld] && m_nAbs - m_nSnapAbs[nOld] < SPR_HIST)
				{
					m_Eng.exportState (m_pEnd);
					m_Eng.exportDbg (m_pDbgEnd);
					m_pFrozenSnap = m_pSnap[nOld];
					m_nFrozenSnapAbs = m_nSnapAbs[nOld];
					m_nFrozenNow = m_nAbs;
					m_nFrozenHistStart = (unsigned) (m_nSnapAbs[nOld] % SPR_HIST);
					m_nSlice = 0;
					m_bFrozen = true;
				}
				else
				{
					m_nFreezeAt = 0;
				}
			}
		}
#endif
	}

private:
#ifdef ADELSPRING_DEBUG
	float m_fLastShake = 0.0f, m_fLastTank = -1.0f, m_fLastSprings = -1.0f, m_fShakeEdges = 0.0f, m_fShakeChanges = 0.0f, m_fResetChanges = 0.0f, m_fShakeMax = 0.0f;
	float *m_pSnap[3];
	unsigned long long m_nSnapAbs[3];
	bool m_bSnapOk[3];
	unsigned m_nSnapNext = 0, m_nSinceSnap = 0;
	float *m_pHist;
	float *m_pEnd;
	float *m_pDbgEnd;
	unsigned long long m_nAbs = 0, m_nFreezeAt = 0;
	float m_fEvSeen = 0.0f;
	bool m_bFrozen = false;
	float *m_pFrozenSnap = 0;
	unsigned long long m_nFrozenSnapAbs = 0, m_nFrozenNow = 0;
	unsigned m_nFrozenHistStart = 0;
	mutable unsigned m_nSlice = 0;
#endif
	adelspring::Tank m_Eng;
	float m_fIn[PL_MAX_BLOCK];
};

CFx *NewAdelSpring (float fRate)
{
	return new CAdelSpring (fRate);
}
