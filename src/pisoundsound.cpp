//
// pisoundsound.cpp
//
// TEST_MODE 1: a 4-frame impulse at the start of every second; the loop latency is the delay (in frames)
//   between the index of the output frame and that of the input frame in which the impulse exceeds the threshold.
// TEST_MODE 0: Phase 1 (0-4 s) 440 Hz tone on output L; then every 2 s N beeps at 880 Hz, N = class of the input level.
// TEST_MODE 2: passthrough input L -> output L (raw blocks).
// TEST_MODE 3: guitar -> parallel NAM lanes -> output L, processed in the input callback (complete block just arrived).
//
#include "pisoundsound.h"
#include "ftools.h"
#include "fx.h"
#include "config.h"
#include <circle/synchronize.h>

// input sample chosen from the two words of a frame (L, R): 0 = L, 1 = R, 2 = stereo = (L+R)/2; 24 bits signed
static inline s32 SelectInput (const u32 *pFrame, unsigned nMode)
{
	const s32 nL = ((s32) (pFrame[0] << 8)) >> 8;
	const s32 nR = ((s32) (pFrame[1] << 8)) >> 8;
	return nMode == 0 ? nL : (nMode == 1 ? nR : (nL + nR) / 2);
}

static inline unsigned long long TickNow (void)
{
	unsigned long long nTicks;
	asm volatile ("mrs %0, cntvct_el0" : "=r" (nTicks));
	return nTicks;
}

CPisoundSound::CPisoundSound (CInterruptSystem *pInterrupt, unsigned nSampleRate, unsigned nChunkSize)
:	CI2SSoundBaseDevice (pInterrupt, nSampleRate, nChunkSize, TRUE, 0, 0,	// the Pi is SLAVE
			     CI2SSoundBaseDevice::DeviceModeTXRX),
	m_nSampleRate (nSampleRate),
	m_nOutFrames (0),
	m_nInFrames (0),
	m_fSumSq (0.0f),
	m_nMeterFrames (0),
	m_nClass (1),
	m_nPeak (0),
	m_nPeakL (0),
	m_nPeakS (0),
	m_nPeakR (0),
	m_nPulseTx (0),
	m_bArmed (FALSE),
	m_nLatLast (0),
	m_nLatMin (0xFFFFFFFF),
	m_nLatMax (0),
	m_nLatCount (0),
	m_nLatLost (0),
	m_nInSum (0), m_nInMax (0), m_nInCnt (0),
	m_nOutSum (0), m_nOutMax (0), m_nOutCnt (0),
	m_nInChunkSeq (0),
	m_nUsedSeq (0),
	m_nFresh (0),
	m_nStale (0),
	m_nEmpty (0),
	m_pCapIn (0), m_pCapOut (0), m_nCapPos (0), m_nCapMax (0), m_bCapOn (FALSE),
	m_bWindow (FALSE),
	m_nWindowEnd (0),
	m_nWindowPeakFrame (0),
	m_fWindowPeak (0.0f),
	m_nPkLast (0),
	m_nPkMin (0xFFFFFFFF),
	m_nPkMax (0),
	m_nPkCount (0)
{
	unsigned long long nFreq;
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (nFreq));
	m_nFreq = nFreq;
	m_nLoadBusy = m_nLoadStart = m_nShedUntil = 0;
	m_nShedCount = 0;

	m_pPlan = 0;
	for (unsigned i = 0; i < 4; i++)
	{
		m_pGhost[i] = 0;
	}
	m_nFadeInLeft = 0;
	m_nFadeInTotal = 1;
	m_nProbeArm = m_nProbeLeft = m_nProbeZero = m_nProbeZeroMax = m_nWinCnt = m_nProbeDone = 0;
	m_nProbeRes[0] = 0;
	m_nSwapChunk = 8;
	m_nSwapChunkDone = 0;
	m_nProbeResTicks[0] = m_nProbeResTicks[1] = m_nProbeMainTicks = m_nProbeGhostTicks = 0;
	m_fProbePrev = m_fProbeStep = m_fProbePeak = m_fWinStep = m_fBeforeStep = 0.0f;
	m_fProbeResAfter = m_fProbeResBefore = m_fProbeResPeak = 0.0f;
	m_pFeat = 0;
	m_nTestSrc = 0;
	for (unsigned c = 0; c < DI_MAX_CHUNKS; c++)
	{
		m_pDiChunk[c] = 0;
	}
	m_nDiState = 0;
	m_nDiFrames = 0;
	m_nDiTarget = 0;
	m_nDiMarks = 0;
	m_fOutPkL = m_fOutPkR = m_fOutSqL = m_fOutSqR = 0.0f;
	m_nOutStatFrames = 0;
	for (unsigned i = 0; i < NAM_MAX_BLOCK; i++)
	{
		m_OutF[i] = 0.0f;
		m_OutR[i] = 0.0f;
	}

	m_Tone440.SetWaveform (WaveformSine);
	m_Tone440.SetFrequency (440.0f);
	m_Tone880.SetWaveform (WaveformSine);
	m_Tone880.SetFrequency (880.0f);
}

CPisoundSound::~CPisoundSound (void)
{
}

unsigned CPisoundSound::GetInPeak24 (void)
{
	unsigned nPeak = m_nPeak;
	m_nPeak = 0;
	return nPeak;
}

void CPisoundSound::GetChannelPeaks (unsigned *pLeft, unsigned *pRight, unsigned *pSum)
{
	*pLeft = m_nPeakL;
	*pRight = m_nPeakR;
	if (pSum != 0)
	{
		*pSum = m_nPeakS;
	}
	m_nPeakL = 0;
	m_nPeakR = 0;
	m_nPeakS = 0;
}

void CPisoundSound::GetLatency (unsigned *pLast, unsigned *pMin, unsigned *pMax, unsigned *pCount, unsigned *pLost)
{
	*pLast = m_nLatLast;
	*pMin = m_nLatMin;
	*pMax = m_nLatMax;
	*pCount = m_nLatCount;
	*pLost = m_nLatLost;
}

void CPisoundSound::GetPeakLatency (unsigned *pLast, unsigned *pMin, unsigned *pMax, unsigned *pCount)
{
	*pLast = m_nPkLast;
	*pMin = m_nPkMin == 0xFFFFFFFF ? 0 : m_nPkMin;
	*pMax = m_nPkMax;
	*pCount = m_nPkCount;
}

void CPisoundSound::GetPassthroughStats (unsigned *pFresh, unsigned *pStale, unsigned *pEmpty)
{
	*pFresh = m_nFresh;
	*pStale = m_nStale;
	*pEmpty = m_nEmpty;
}

void CPisoundSound::GetCallbackStats (CCallbackStats *pStats)
{
	pStats->InSumTicks = m_nInSum;
	pStats->InMaxTicks = m_nInMax;
	pStats->InCount = m_nInCnt;
	pStats->OutSumTicks = m_nOutSum;
	pStats->OutMaxTicks = m_nOutMax;
	pStats->OutCount = m_nOutCnt;
	pStats->Freq = m_nFreq;
	m_nInSum = m_nInMax = m_nInCnt = 0;
	m_nOutSum = m_nOutMax = m_nOutCnt = 0;
}

void CPisoundSound::SetTestSource (unsigned nMask)
{
	m_nTestSrc = nMask & 3;
}

boolean CPisoundSound::DiPrepare (unsigned nSeconds, boolean bAppend)
{
	// with bAppend the finished recording is kept and a new take is added after it
	const unsigned nOld = (bAppend && m_nDiState == 2) ? m_nDiFrames : 0;
	const unsigned nState = m_nDiState;
	m_nDiState = 0;
	if (nSeconds < 1)
	{
		nSeconds = 1;
	}
	const unsigned nMax = (unsigned) (DI_MAX_CHUNKS * DI_CHUNK_FRAMES);
	unsigned long long nWant = (unsigned long long) nOld + (unsigned long long) nSeconds * m_nSampleRate;
	if (nWant > nMax)
	{
		nWant = nMax;
	}
	if (nWant <= nOld)
	{
		m_nDiState = nState;		// the buffer is full: nothing changes
		return FALSE;
	}
	const unsigned nChunks = (unsigned) ((nWant + DI_CHUNK_FRAMES - 1) / DI_CHUNK_FRAMES);
	for (unsigned c = 0; c < nChunks; c++)
	{
		if (m_pDiChunk[c] == 0)
		{
			m_pDiChunk[c] = new s32[DI_CHUNK_FRAMES * 2];
			if (m_pDiChunk[c] == 0)
			{
				m_nDiState = nState;
				return FALSE;
			}
		}
	}
	m_nDiTarget = (unsigned) nWant;
	if (nOld == 0)
	{
		m_nDiFrames = 0;
		m_nDiMarks = 0;
	}
	if (m_nDiMarks < DI_MAX_MARKS)
	{
		m_nDiMark[m_nDiMarks++] = nOld;
	}
	asm volatile ("dmb ish" ::: "memory");
	m_nDiState = 1;
	return TRUE;
}
unsigned CPisoundSound::DiRead (unsigned nFrom, unsigned nCount, u8 *pOut) const
{
	if (m_nDiState == 1 || nFrom >= m_nDiFrames)
	{
		return 0;
	}
	if (nCount > m_nDiFrames - nFrom)
	{
		nCount = m_nDiFrames - nFrom;
	}
	for (unsigned i = 0; i < nCount; i++)
	{
		const unsigned f = nFrom + i;
		const s32 *p = m_pDiChunk[f / DI_CHUNK_FRAMES] + (f % DI_CHUNK_FRAMES) * 2;
		for (unsigned ch = 0; ch < 2; ch++)
		{
			const u32 v = (u32) p[ch];
			*pOut++ = (u8) v;
			*pOut++ = (u8) (v >> 8);
			*pOut++ = (u8) (v >> 16);
		}
	}
	return nCount;
}

void CPisoundSound::DiFree (void)
{
	m_nDiState = 0;
	for (unsigned c = 0; c < DI_MAX_CHUNKS; c++)
	{
		delete[] m_pDiChunk[c];
		m_pDiChunk[c] = 0;
	}
	m_nDiFrames = 0;
	m_nDiTarget = 0;
	m_nDiMarks = 0;
}

void CPisoundSound::GetOutputStats (float *pPeakL, float *pPeakR, float *pSumSqL, float *pSumSqR, unsigned *pFrames)
{
	*pPeakL = m_fOutPkL;
	*pPeakR = m_fOutPkR;
	*pSumSqL = m_fOutSqL;
	*pSumSqR = m_fOutSqR;
	*pFrames = m_nOutStatFrames;
	m_fOutPkL = m_fOutPkR = m_fOutSqL = m_fOutSqR = 0.0f;
	m_nOutStatFrames = 0;
}

void CPisoundSound::SetPlan (const TPlan *pPlan)
{
	asm volatile ("dmb ish" ::: "memory");
	m_pPlan = pPlan;
}

#define GHOST_SLOTS	4

boolean CPisoundSound::SwapPlan (const TPlan *pNew, TGhost *pGhost)
{
	boolean bPlaced = FALSE;
	EnterCritical ();		// the callback (IRQ) must not see the new plan without the ghost, nor vice versa
	if (pGhost != 0)
	{
		// at most one older ghost goes on: the others fade (otherwise the load would grow with preset changes in quick succession)
		for (unsigned i = 0; i < GHOST_SLOTS; i++)
		{
			TGhost *pOld = m_pGhost[i];
			if (pOld != 0 && pOld->State == GH_RUN && !pOld->Kill)
			{
				boolean bNewer = FALSE;
				for (unsigned k = 0; k < GHOST_SLOTS; k++)
				{
					TGhost *pOther = m_pGhost[k];
					if (pOther != 0 && pOther != pOld && pOther->State == GH_RUN && !pOther->Kill && pOther->Seq > pOld->Seq)
					{
						bNewer = TRUE;
					}
				}
				if (bNewer)
				{
					pOld->KillLeft = GH_KILL_FRAMES;
					pOld->Kill = 1;
				}
			}
		}
		for (unsigned i = 0; i < GHOST_SLOTS && !bPlaced; i++)
		{
			if (m_pGhost[i] == 0)
			{
				GraphGhostInit (pGhost);
				asm volatile ("dmb ish" ::: "memory");
				m_pGhost[i] = pGhost;
				bPlaced = TRUE;
			}
		}
	}
	m_nFadeInTotal = GH_RAMP_FRAMES;
	m_nFadeInLeft = GH_RAMP_FRAMES;
	m_nProbeArm = 1;
	m_nSwapChunk = 0;
	asm volatile ("dmb ish" ::: "memory");
	m_pPlan = pNew;
	LeaveCritical ();
	return bPlaced || pGhost == 0;
}

boolean CPisoundSound::SwapChunksTake (unsigned *pUs)
{
	if (!m_nSwapChunkDone)
	{
		return FALSE;
	}
	for (unsigned k = 0; k < 8; k++)
	{
		pUs[k] = (unsigned) ((unsigned long long) m_nSwapChunkTicks[k] * 1000000ULL / m_nFreq);
	}
	m_nSwapChunkDone = 0;
	return TRUE;
}

boolean CPisoundSound::SwapProbeTake (unsigned *pZeroRun, float *pStepAfter, float *pStepBefore, float *pPeakAfter, unsigned *pMainUs, unsigned *pGhostUs)
{
	if (!m_nProbeDone)
	{
		return FALSE;
	}
	*pZeroRun = m_nProbeRes[0];
	*pStepAfter = m_fProbeResAfter;
	*pStepBefore = m_fProbeResBefore;
	*pPeakAfter = m_fProbeResPeak;
	*pMainUs = (unsigned) ((unsigned long long) m_nProbeResTicks[0] * 1000000ULL / m_nFreq);
	*pGhostUs = (unsigned) ((unsigned long long) m_nProbeResTicks[1] * 1000000ULL / m_nFreq);
	m_nProbeDone = 0;
	return TRUE;
}

TGhost *CPisoundSound::GhostPoll (void)
{
	for (unsigned i = 0; i < GHOST_SLOTS; i++)
	{
		TGhost *pG = m_pGhost[i];
		if (pG != 0 && pG->State == GH_DONE)
		{
			m_pGhost[i] = 0;
			return pG;
		}
	}
	return 0;
}

boolean CPisoundSound::GhostNamBusy (void) const
{
	for (unsigned i = 0; i < GHOST_SLOTS; i++)
	{
		TGhost *pG = m_pGhost[i];
		if (pG != 0 && pG->State == GH_RUN && pG->NamLive)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void CPisoundSound::CaptureStart (float *pIn, float *pOut, unsigned nFrames)
{
	m_bCapOn = FALSE;
	m_pCapIn = pIn;
	m_pCapOut = pOut;
	m_nCapMax = nFrames;
	m_nCapPos = 0;
	asm volatile ("dmb ish" ::: "memory");
	m_bCapOn = TRUE;
}

// nChunkSize = words (2 per frame: L, R)
unsigned CPisoundSound::GetChunk (u32 *pBuffer, unsigned nChunkSize)
{
	asm volatile ("msr daifclr, #8" ::: "memory");	// watchpoint also active in the callback
#if NO_AUDIO_PROC
	for (unsigned i = 0; i < nChunkSize; i++)
	{
		pBuffer[i] = 0;
	}
	return nChunkSize;
#endif
	const unsigned long long nStart = TickNow ();

#if TEST_MODE >= 2
	{
		unsigned nSeq = m_nInChunkSeq;
		if (nSeq == 0 || nChunkSize > sizeof m_LastIn / sizeof m_LastIn[0] || nChunkSize / 2 > NAM_MAX_BLOCK)
		{
			m_nEmpty++;
			for (unsigned i = 0; i < nChunkSize; i++)
			{
				pBuffer[i] = 0;
			}
		}
		else
		{
			if (nSeq != m_nUsedSeq)
			{
				m_nFresh++;
			}
			else
			{
				m_nStale++;
			}
			m_nUsedSeq = nSeq;

			for (unsigned i = 0; i + 1 < nChunkSize; i += 2)
			{
#if TEST_MODE == 2
				s32 nL = ((s32) (m_LastIn[i] << 8)) >> 8;	// 24 bits signed, extended to 32
				pBuffer[i] = (u32) nL;
#else
				float fOut = m_OutF[i / 2];
#if NAM_LOOPTEST
				m_Tone880.NextSample ();
				fOut = m_Tone880.GetOutputLevel () * 0.1f;		// converter test: pure tone at -20 dBFS
#endif
				if (fOut > 0.99f) fOut = 0.99f;
				if (fOut < -0.99f) fOut = -0.99f;
#if TEST_MODE == 3
				{
					float fOutR = m_OutR[i / 2];
					if (fOutR > 0.99f) fOutR = 0.99f;
					if (fOutR < -0.99f) fOutR = -0.99f;
					const float fAtt = m_nTestSrc ? 0.0005f : 1.0f;		// test source: -66 dB towards the amps
					pBuffer[i] = (u32) (s32) (fOut * fAtt * 8388607.0f);
					pBuffer[i + 1] = (u32) (s32) (fOutR * fAtt * 8388607.0f);
				}
#else
				pBuffer[i] = (u32) (s32) (fOut * 8388607.0f);
#endif
#endif
#if TEST_MODE != 3
				pBuffer[i + 1] = 0;
#endif
			}
		}

		m_nOutFrames += nChunkSize / 2;

		const unsigned long long nTicks = TickNow () - nStart;
		m_nOutSum += nTicks;
		m_nOutCnt++;
		if (nTicks > m_nOutMax)
		{
			m_nOutMax = nTicks;
		}
		return nChunkSize;
	}
#endif

	const float fScale = (float) GetRangeMax ();
	const unsigned long long nPhase1Frames = (unsigned long long) m_nSampleRate * 4;
	const unsigned nBeepCycle = m_nSampleRate * 2;
	const unsigned nBeepOn = m_nSampleRate / 8;
	const unsigned nBeepPeriod = m_nSampleRate * 3 / 10;

	for (unsigned i = 0; i + 1 < nChunkSize; i += 2)
	{
		float fOut = 0.0f;
		unsigned long long nFrame = m_nOutFrames;
		m_nOutFrames = nFrame + 1;

#if TEST_MODE == 1
		// a 4-frame impulse at the start of every second (after 2 s of settling)
		if (nFrame >= (unsigned long long) m_nSampleRate * 2)
		{
			unsigned nInSecond = (unsigned) (nFrame % m_nSampleRate);
			if (nInSecond < 4)
			{
				if (nInSecond == 0)
				{
					if (m_bArmed)
					{
						m_nLatLost++;		// the previous impulse has not come back
					}
					m_nPulseTx = nFrame;
					m_bArmed = TRUE;
				}
				fOut = PULSE_LEVEL;
			}
		}
#else
		if (nFrame < nPhase1Frames)
		{
			m_Tone440.NextSample ();
			fOut = m_Tone440.GetOutputLevel () * TONE_LEVEL;
		}
		else
		{
			unsigned nInCycle = (unsigned) ((nFrame - nPhase1Frames) % nBeepCycle);
			unsigned nBeep = nInCycle / nBeepPeriod;
			unsigned nInBeep = nInCycle % nBeepPeriod;
			if (nBeep < m_nClass && nInBeep < nBeepOn)
			{
				m_Tone880.NextSample ();
				fOut = m_Tone880.GetOutputLevel () * TONE_LEVEL;
			}
		}
#endif

		pBuffer[i] = (u32) (s32) (fOut * fScale);	// L
		pBuffer[i + 1] = 0;				// R mute
	}

	const unsigned long long nTicks = TickNow () - nStart;
	m_nOutSum += nTicks;
	m_nOutCnt++;
	if (nTicks > m_nOutMax)
	{
		m_nOutMax = nTicks;
	}

	return nChunkSize;
}

void CPisoundSound::PutChunk (const u32 *pBuffer, unsigned nChunkSize)
{
	asm volatile ("msr daifclr, #8" ::: "memory");	// watchpoint also active in the callback
#if NO_AUDIO_PROC
	return;			// test: the DMA runs but the callback does nothing
#endif
	const unsigned long long nStart = TickNow ();
	const unsigned nMeterFrames = m_nSampleRate / 4;		// level measurement every 250 ms

	// the DMA has just written this buffer: Circle invalidates it only after the callback, here it is done before reading it
	InvalidateDataCacheRange ((uintptr) pBuffer, nChunkSize * sizeof (u32));

#if TEST_MODE == 2
	// saves the block for the passthrough, then marks the block as complete
	if (nChunkSize <= sizeof m_LastIn / sizeof m_LastIn[0])
	{
		for (unsigned i = 0; i < nChunkSize; i++)
		{
			m_LastIn[i] = pBuffer[i];
		}
		m_nInChunkSeq = m_nInChunkSeq + 1;
	}
#endif

#if TEST_MODE == 3
	// inputs L/R -> graph engine (lanes, block chains, Split) -> output buses L and R
	{
		const unsigned nFrames = nChunkSize / 2;
		if (nFrames > 0 && nFrames <= NAM_MAX_BLOCK)
		{
			float InL[NAM_MAX_BLOCK], InR[NAM_MAX_BLOCK];
			for (unsigned j = 0; j < nFrames; j++)
			{
				InL[j] = (float) (((s32) (pBuffer[j * 2] << 8)) >> 8) * (1.0f / 8388608.0f);
				InR[j] = (float) (((s32) (pBuffer[j * 2 + 1] << 8)) >> 8) * (1.0f / 8388608.0f);
			}

			if (m_nDiState == 1)
			{
				// DI recorder: raw input words, before the test source and any processing
				unsigned nPos = m_nDiFrames;
				for (unsigned j = 0; j < nFrames && nPos < m_nDiTarget; j++, nPos++)
				{
					s32 *p = m_pDiChunk[nPos / DI_CHUNK_FRAMES] + (nPos % DI_CHUNK_FRAMES) * 2;
					p[0] = ((s32) (pBuffer[j * 2] << 8)) >> 8;
					p[1] = ((s32) (pBuffer[j * 2 + 1] << 8)) >> 8;
				}
				m_nDiFrames = nPos;
				if (nPos >= m_nDiTarget)
				{
					m_nDiState = 2;
				}
			}

			const unsigned nTest = m_nTestSrc;
			if (nTest != 0)
			{
				// test source: replaces the inputs
				for (unsigned j = 0; j < nFrames; j++)
				{
					InL[j] = 0.0f;
					InR[j] = 0.0f;
					if (nTest & 1)
					{
						m_Tone440.NextSample ();
						InL[j] = m_Tone440.GetOutputLevel () * 0.1f;
					}
					if (nTest & 2)
					{
						m_Tone880.NextSample ();
						InR[j] = m_Tone880.GetOutputLevel () * 0.1f;
					}
				}
			}

			CFeatures *pFt = m_pFeat;
			if (pFt != 0)
			{
				pFt->ProcessIn (InL, InR, nFrames);		// input functions (hum killer, rumble filter, tuner): they do nothing if off
			}
			const TPlan *pPlan = m_pPlan;
			if (pPlan != 0 && nStart >= m_nShedUntil)
			{
				const unsigned long long nTm0 = TickNow ();
				GraphProcess (pPlan, InL, InR, m_OutF, m_OutR, nFrames);
				if (m_nProbeLeft > 0 && TickNow () - nTm0 > m_nProbeMainTicks)
				{
					m_nProbeMainTicks = (unsigned) (TickNow () - nTm0);
				}
			}
			else
			{
				// no plan, or overload: input -> output without processing (the master gain remains)
				const float fM = pPlan != 0 ? pPlan->Master : 1.0f;
				for (unsigned j = 0; j < nFrames; j++)
				{
					m_OutF[j] = InL[j] * fM;
					m_OutR[j] = InR[j] * fM;
				}
			}

			// preset change: the new plan enters in 4 ms and the old one's tails (ghosts) are added to the output
			if (m_nFadeInLeft > 0)
			{
				unsigned nLeft = m_nFadeInLeft;
				const float fTot = (float) m_nFadeInTotal;
				for (unsigned j = 0; j < nFrames && nLeft > 0; j++, nLeft--)
				{
					const float g = 1.0f - (float) nLeft / fTot;
					m_OutF[j] *= g;
					m_OutR[j] *= g;
				}
				m_nFadeInLeft = nLeft;
			}
			if (nStart >= m_nShedUntil)
			{
				float GL[NAM_MAX_BLOCK], GR[NAM_MAX_BLOCK];
				const unsigned long long nTg0 = TickNow ();
				for (unsigned i = 0; i < 4; i++)
				{
					TGhost *pG = m_pGhost[i];
					if (pG != 0 && pG->State == GH_RUN)
					{
						GraphGhostProcess (pG, InL, InR, GL, GR, nFrames);
						for (unsigned j = 0; j < nFrames; j++)
						{
							m_OutF[j] += GL[j];
							m_OutR[j] += GR[j];
						}
					}
				}
				if (m_nProbeLeft > 0 && TickNow () - nTg0 > m_nProbeGhostTicks)
				{
					m_nProbeGhostTicks = (unsigned) (TickNow () - nTg0);
				}
			}

			if (pFt != 0)
			{
				pFt->ProcessOut (m_OutF, m_OutR, nFrames);		// output functions (limiter, mono, air, saturation, click, analysis, tuner muting)
			}
			{
				// ADELSendSweep with "Mute Output": the sweep stays inside the chain (the measurement sees it) but is not sent to the outputs (about 2 ms fades)
				const float fTgt = FxProbeMuteOut () ? 0.0f : 1.0f;
				if (fTgt < 1.0f || m_fProbeOutG < 1.0f)
				{
					for (unsigned j = 0; j < nFrames; j++)
					{
						const float fStep = 1.0f / 96.0f;
						m_fProbeOutG = fTgt < m_fProbeOutG ? (m_fProbeOutG - fStep > fTgt ? m_fProbeOutG - fStep : fTgt) : (m_fProbeOutG + fStep < fTgt ? m_fProbeOutG + fStep : fTgt);
						m_OutF[j] *= m_fProbeOutG;
						m_OutR[j] *= m_fProbeOutG;
					}
				}
			}
			{
				// check of the sound around a preset swap (output L): 50 ms after the swap the longest stretch of silence and the maximum jump between two consecutive samples are measured
				// consecutive samples; outside that window the maximum jump of the last 50 ms is kept as a reference
				if (m_nProbeArm)
				{
					m_nProbeArm = 0;
					m_nProbeLeft = 2400;
					m_nProbeZero = m_nProbeZeroMax = 0;
					m_fProbeStep = m_fProbePeak = 0.0f;
					m_fProbeResBefore = m_fBeforeStep;
					m_nProbeMainTicks = m_nProbeGhostTicks = 0;
				}
				for (unsigned j = 0; j < nFrames; j++)
				{
					const float y = m_OutF[j];
					float d = y - m_fProbePrev;
					d = d < 0.0f ? -d : d;
					m_fProbePrev = y;
					const float ay = y < 0.0f ? -y : y;
					if (m_nProbeLeft > 0)
					{
						m_fProbeStep = d > m_fProbeStep ? d : m_fProbeStep;
						m_fProbePeak = ay > m_fProbePeak ? ay : m_fProbePeak;
						if (ay < 1.0e-6f)
						{
							if (++m_nProbeZero > m_nProbeZeroMax)
							{
								m_nProbeZeroMax = m_nProbeZero;
							}
						}
						else
						{
							m_nProbeZero = 0;
						}
						if (--m_nProbeLeft == 0)
						{
							m_nProbeRes[0] = m_nProbeZeroMax;
							m_nProbeResTicks[0] = m_nProbeMainTicks;
							m_nProbeResTicks[1] = m_nProbeGhostTicks;
							m_fProbeResAfter = m_fProbeStep;
							m_fProbeResPeak = m_fProbePeak;
							m_nProbeDone = 1;
						}
					}
					else
					{
						m_fWinStep = d > m_fWinStep ? d : m_fWinStep;
						if (++m_nWinCnt >= 2400)
						{
							m_fBeforeStep = m_fWinStep;
							m_fWinStep = 0.0f;
							m_nWinCnt = 0;
						}
					}
				}
			}
			{
				// output meters (before the test attenuation)
				float fPkL = m_fOutPkL, fPkR = m_fOutPkR, fSqL = m_fOutSqL, fSqR = m_fOutSqR;
				for (unsigned j = 0; j < nFrames; j++)
				{
					const float l = m_OutF[j], r = m_OutR[j];
					const float al = l < 0.0f ? -l : l, ar = r < 0.0f ? -r : r;
					fPkL = al > fPkL ? al : fPkL;
					fPkR = ar > fPkR ? ar : fPkR;
					fSqL += l * l;
					fSqR += r * r;
				}
				m_fOutPkL = fPkL;
				m_fOutPkR = fPkR;
				m_fOutSqL = fSqL;
				m_fOutSqR = fSqR;
				m_nOutStatFrames = m_nOutStatFrames + nFrames;
			}
			if (m_bCapOn)
			{
				unsigned nPos = m_nCapPos;
				if (nPos + nFrames <= m_nCapMax)
				{
					for (unsigned j = 0; j < nFrames; j++)
					{
						m_pCapIn[nPos + j] = InL[j];
						m_pCapOut[nPos + j] = m_OutF[j];
					}
					m_nCapPos = nPos + nFrames;
				}
				else
				{
					m_bCapOn = FALSE;
				}
			}
			asm volatile ("dmb ish" ::: "memory");
			m_nInChunkSeq = m_nInChunkSeq + 1;
		}
	}
#endif

	for (unsigned i = 0; i + 1 < nChunkSize; i += 2)
	{
		s32 nL = ((s32) (pBuffer[i] << 8)) >> 8;	// 24 bits signed
#if TEST_MODE == 3
		{
			const s32 nR = ((s32) (pBuffer[i + 1] << 8)) >> 8;		// the overall level is the larger of L and R
			if ((nR < 0 ? -nR : nR) > (nL < 0 ? -nL : nL))
			{
				nL = nR;
			}
		}
#endif
		float fL = (float) nL / 8388608.0f;

		{
			// separate L and R peaks (for the configuration page)
			const s32 nChL = ((s32) (pBuffer[i] << 8)) >> 8;
			const s32 nChR = ((s32) (pBuffer[i + 1] << 8)) >> 8;
			const unsigned nAL = nChL < 0 ? (unsigned) -nChL : (unsigned) nChL;
			const unsigned nAR = nChR < 0 ? (unsigned) -nChR : (unsigned) nChR;
			if (nAL > m_nPeakL)
			{
				m_nPeakL = nAL;
			}
			if (nAR > m_nPeakR)
			{
				m_nPeakR = nAR;
			}
			const s32 nSum = (nChL + nChR) >> 1;
			const unsigned nAS = nSum < 0 ? (unsigned) -nSum : (unsigned) nSum;
			if (nAS > m_nPeakS)
			{
				m_nPeakS = nAS;
			}
		}

		unsigned nAbs = nL < 0 ? (unsigned) -nL : (unsigned) nL;
		if (nAbs > m_nPeak)
		{
			m_nPeak = nAbs;
		}

#if TEST_MODE == 1
		unsigned long long nRxFrame = m_nInFrames + i / 2;
		if (m_bArmed && fL > PULSE_DETECT && nRxFrame >= m_nPulseTx)
		{
			// first threshold crossing (start of the impulse, includes the filters' precursors)
			unsigned nDelay = (unsigned) (nRxFrame - m_nPulseTx);
			m_nLatLast = nDelay;
			if (nDelay < m_nLatMin)
			{
				m_nLatMin = nDelay;
			}
			if (nDelay > m_nLatMax)
			{
				m_nLatMax = nDelay;
			}
			m_nLatCount++;
			m_bArmed = FALSE;

			// opens the peak search window (1 ms)
			m_bWindow = TRUE;
			m_nWindowEnd = nRxFrame + m_nSampleRate / 1000;
			m_fWindowPeak = 0.0f;
			m_nWindowPeakFrame = nRxFrame;
		}

		if (m_bWindow)
		{
			float fAbs = fL < 0.0f ? -fL : fL;
			if (fAbs > m_fWindowPeak)
			{
				m_fWindowPeak = fAbs;
				m_nWindowPeakFrame = nRxFrame;
			}
			if (nRxFrame >= m_nWindowEnd)
			{
				unsigned nPeakDelay = (unsigned) (m_nWindowPeakFrame - m_nPulseTx);
				m_nPkLast = nPeakDelay;
				if (nPeakDelay < m_nPkMin)
				{
					m_nPkMin = nPeakDelay;
				}
				if (nPeakDelay > m_nPkMax)
				{
					m_nPkMax = nPeakDelay;
				}
				m_nPkCount++;
				m_bWindow = FALSE;
			}
		}
#endif

		m_fSumSq += fL * fL;
		if (++m_nMeterFrames >= nMeterFrames)
		{
			float fMeanSq = m_fSumSq / m_nMeterFrames;
			// energy thresholds: -80 dBFS = 1e-8, -50 dBFS = 1e-5, -20 dBFS = 1e-2
			if (fMeanSq < 1e-8f)
			{
				m_nClass = 1;
			}
			else if (fMeanSq < 1e-5f)
			{
				m_nClass = 2;
			}
			else if (fMeanSq < 1e-2f)
			{
				m_nClass = 3;
			}
			else
			{
				m_nClass = 4;
			}

			m_fSumSq = 0.0f;
			m_nMeterFrames = 0;
		}
	}

	m_nInFrames += nChunkSize / 2;

	{
		const unsigned long long nTk = TickNow () - nStart;
		if (m_nSwapChunk < 8)
		{
			m_nSwapChunkTicks[m_nSwapChunk] = (unsigned) nTk;
			if (++m_nSwapChunk == 8)
			{
				m_nSwapChunkDone = 1;
			}
		}
	}
	const unsigned long long nTicks = TickNow () - nStart;
	m_nInSum += nTicks;
	m_nInCnt++;
	if (nTicks > m_nInMax)
	{
		m_nInMax = nTicks;
	}

	// overload protection: in a 100 ms window does the callback occupy more than 85% of the time? Then the plan is excluded for 1.5 s
	m_nLoadBusy += nTicks;
	if (m_nLoadStart == 0)
	{
		m_nLoadStart = nStart;
	}
	const unsigned long long nSpan = nStart + nTicks - m_nLoadStart;
	if (nSpan >= m_nFreq / 10)
	{
		if (nStart >= m_nShedUntil && m_nLoadBusy * 100 > nSpan * 85)
		{
			m_nShedUntil = nStart + nTicks + m_nFreq * 3 / 2;
			m_nShedCount = m_nShedCount + 1;
		}
		m_nLoadBusy = 0;
		m_nLoadStart = 0;
	}
}
