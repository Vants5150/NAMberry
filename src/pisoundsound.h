//
// pisoundsound.h - Pisound I2S device: the audio is generated and read directly in the DMA callback
// (no queue, no dependency on the main loop or on the network).
//
// Format (from lib/sound/soundbasedevice.cpp, SoundFormatSigned24_32):
//   output: u32 word = 24-bit sample sign-extended to 32 bits (range +-(2^23 - 1))
//   input: the low 24 bits of the word are the sample (bit 23 = sign)
//
// TEST_MODE 0: tone and beep | 1: latency measurement with the loopback cable | 2: passthrough input L -> output L
// TEST_MODE 3: guitar -> NAM (up to 3 lanes in parallel, ADELnam engine) -> output L; with no models loaded it is a passthrough
//
#ifndef _pisoundsound_h
#define _pisoundsound_h

#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/types.h>
#include "oscillator.h"
#include "adelnam_engine.hpp"
#include "graph.h"

#define NAM_MAX_LANES	3
#define NAM_MAX_BLOCK	64		// maximum frames per block in NAM mode

struct CCallbackStats
{
	unsigned long long InSumTicks, InMaxTicks, InCount;
	unsigned long long OutSumTicks, OutMaxTicks, OutCount;
	unsigned long long Freq;	// hardware counter ticks per second
};

class CPisoundSound : public CI2SSoundBaseDevice
{
public:
	CPisoundSound (CInterruptSystem *pInterrupt, unsigned nSampleRate, unsigned nChunkSize);
	~CPisoundSound (void);

	unsigned GetSampleRate (void) const { return m_nSampleRate; }
	unsigned GetLevelClass (void) const { return m_nClass; }		// 1..4, see below
	unsigned long long GetInFrames (void) const { return m_nInFrames; }
	unsigned long long GetOutFrames (void) const { return m_nOutFrames; }
	unsigned GetInPeak24 (void);						// absolute peak (24 bits) since the last read
	void GetChannelPeaks (unsigned *pLeft, unsigned *pRight, unsigned *pSum = 0);		// separate L and R peaks (24 bits), since the last read

	// latency measurement (TEST_MODE 1): delay in frames between the output impulse and its return on the input
	void GetLatency (unsigned *pLast, unsigned *pMin, unsigned *pMax, unsigned *pCount, unsigned *pLost);
	void GetPeakLatency (unsigned *pLast, unsigned *pMin, unsigned *pMax, unsigned *pCount);

	// passthrough (TEST_MODE 2/3): how many times the output block used the input block just completed
	// ("fresh": latency 2 blocks + converters) or the previous one ("old": 3 blocks + converters)
	void GetPassthroughStats (unsigned *pFresh, unsigned *pStale, unsigned *pEmpty);

	// duration of the callbacks measured with the hardware counter (resets the statistics after the read)
	void GetCallbackStats (CCallbackStats *pStats);

	// test source in place of the inputs: 0 = off, bit0 = 440 Hz tone on L, bit1 = 880 Hz tone on R (-20 dBFS).
	// With the source on, the output towards the amps is attenuated by 66 dB; the meters see the signal before the attenuation.
	void SetTestSource (unsigned nMask);
	unsigned GetTestSource (void) const { return m_nTestSrc; }
	unsigned GetShedCount (void) const { return m_nShedCount; }	// how many times the plan was excluded because of overload (callback beyond 93% of the time)
	// peaks and energy of the L and R outputs (before the test attenuation) since the last read
	void GetOutputStats (float *pPeakL, float *pPeakR, float *pSumSqL, float *pSumSqR, unsigned *pFrames);

	// graph engine (TEST_MODE 3): the plan is built by the main loop and swapped in with a single store
	void SetPlan (const TPlan *pPlan);
	// preset change without interruption: switches to the new plan (it enters in 4 ms) and entrusts to the callback the old one's ghost (delay and reverb tails).
	// Returns FALSE if there is no room for the ghost (the main loop frees it immediately and the tail is lost).
	boolean SwapPlan (const TPlan *pNew, struct TGhost *pGhost);
	struct TGhost *GhostPoll (void);		// a finished ghost, removed from the callback (0 if there are none): the main loop frees its instances
	boolean GhostNamBusy (void) const;
	// check of the sound around a preset swap: for 50 ms after the swap it measures the longest stretch of silence (frames) and the maximum jump between two consecutive output samples,
	// to be compared with the maximum jump of the 50 ms before. Returns TRUE (once per swap) when the measurement is finished.
	boolean SwapChunksTake (unsigned *pUs);		// durations (us) of the first 8 callbacks after the swap, once per swap
	boolean SwapProbeTake (unsigned *pZeroRun, float *pStepAfter, float *pStepBefore, float *pPeakAfter, unsigned *pMainUs, unsigned *pGhostUs);		// some ghost is still running its NAMs (for a few ms after the change)
	void SetFeatures (class CFeatures *pFeat) { m_pFeat = pFeat; }		// service functions with a switch (ftools.h)

	// diagnostic recording: raw input and output produced in real time (n frames, whole blocks)
	void CaptureStart (float *pIn, float *pOut, unsigned nFrames);
	boolean CaptureDone (void) const { return !m_bCapOn; }

	// DI recorder: raw 24-bit stereo input before any processing (up to DI_MAX_SECONDS), read back by the web page.
	// DiPrepare and DiFree are called by the main loop (they allocate); the callback only fills the chunks.
	enum { DI_CHUNK_FRAMES = 16384, DI_MAX_CHUNKS = 1800, DI_MAX_MARKS = 64 };		// 1800 chunks of 128 KB = about 614 s of stereo
	// allocates the buffer and starts recording (FALSE: no memory or the buffer is full). With bAppend the previous recording is kept and the new take is added after it.
	boolean DiPrepare (unsigned nSeconds, boolean bAppend = FALSE);
	unsigned DiMaxFrames (void) const { return DI_MAX_CHUNKS * DI_CHUNK_FRAMES; }
	unsigned DiMarkCount (void) const { return m_nDiMarks; }	// start frame of each take
	unsigned DiMark (unsigned i) const { return m_nDiMark[i < DI_MAX_MARKS ? i : 0]; }
	void DiStop (void) { if (m_nDiState == 1) m_nDiState = 2; }
	unsigned DiState (void) const { return m_nDiState; }		// 0 = empty, 1 = recording, 2 = done
	unsigned DiFrames (void) const { return m_nDiFrames; }	// frames recorded so far
	unsigned DiTarget (void) const { return m_nDiTarget; }	// frames requested
	unsigned DiRead (unsigned nFrom, unsigned nCount, u8 *pOut) const;	// 24-bit little-endian stereo, 6 bytes per frame; returns frames written
	void DiFree (void);

protected:
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override;
	void PutChunk (const u32 *pBuffer, unsigned nChunkSize) override;

	using CI2SSoundBaseDevice::GetChunk;
	using CI2SSoundBaseDevice::PutChunk;

private:
	unsigned m_nSampleRate;

	s32 *m_pDiChunk[DI_MAX_CHUNKS];
	volatile unsigned m_nDiState;
	volatile unsigned m_nDiFrames;
	unsigned m_nDiTarget;
	unsigned m_nDiMarks;
	unsigned m_nDiMark[DI_MAX_MARKS];

	COscillator m_Tone440;
	COscillator m_Tone880;

	volatile unsigned long long m_nOutFrames;
	volatile unsigned long long m_nInFrames;

	float m_fSumSq;
	unsigned m_nMeterFrames;
	volatile unsigned m_nClass;	// 1 = < -80 dBFS, 2 = -80..-50, 3 = -50..-20, 4 = > -20 dBFS
	volatile unsigned m_nPeak;
	volatile unsigned m_nPeakL;
	volatile unsigned m_nPeakS;		// peak of the sum (L+R)/2
	volatile unsigned m_nPeakR;

	volatile unsigned long long m_nPulseTx;		// index of the output impulse's frame
	volatile boolean m_bArmed;			// impulse emitted and not yet detected
	volatile unsigned m_nLatLast;
	volatile unsigned m_nLatMin;
	volatile unsigned m_nLatMax;
	volatile unsigned m_nLatCount;
	volatile unsigned m_nLatLost;

	// callback times (hardware counter ticks)
	volatile unsigned long long m_nInSum, m_nInMax, m_nInCnt;
	volatile unsigned long long m_nOutSum, m_nOutMax, m_nOutCnt;
	unsigned long long m_nFreq;
	unsigned long long m_nLoadBusy, m_nLoadStart, m_nShedUntil;	// 100 ms window to measure the load; end of the plan's exclusion
	volatile unsigned m_nShedCount;

	// input -> output passthrough
	u32 m_LastIn[1024];				// last complete input block (raw words, TEST_MODE 2)
	float m_OutF[NAM_MAX_BLOCK];			// output L
	float m_OutR[NAM_MAX_BLOCK];			// output R			// last processed block (TEST_MODE 3)
	volatile unsigned m_nInChunkSeq;		// number of the complete input block
	unsigned m_nUsedSeq;				// last number used by the output
	volatile unsigned m_nFresh;
	volatile unsigned m_nStale;
	volatile unsigned m_nEmpty;

	const TPlan *volatile m_pPlan;
	struct TGhost *volatile m_pGhost[4];
	volatile unsigned m_nFadeInLeft;
	volatile unsigned m_nFadeInTotal;
	volatile unsigned m_nProbeArm;		// 1 = the swap has just happened: the measurement starts at the next block
	unsigned m_nProbeLeft, m_nProbeZero, m_nProbeZeroMax, m_nWinCnt;
	float m_fProbeOutG = 1.0f;		// gain of the output while an ADELSendSweep with Mute Output plays
	float m_fProbePrev, m_fProbeStep, m_fProbePeak, m_fWinStep, m_fBeforeStep;
	volatile unsigned m_nProbeDone;
	volatile unsigned m_nProbeRes[1];
	unsigned m_nProbeMainTicks, m_nProbeGhostTicks;		// maximum time (ticks) of the main plan and of the ghosts in a block, in the 50 ms after the swap
	volatile unsigned m_nProbeResTicks[2];
	volatile unsigned m_nSwapChunk, m_nSwapChunkDone;		// duration (ticks) of the first 8 callbacks after the swap
	volatile unsigned m_nSwapChunkTicks[8];
	volatile float m_fProbeResAfter, m_fProbeResBefore, m_fProbeResPeak;
	class CFeatures *volatile m_pFeat;
	volatile unsigned m_nTestSrc;
	volatile float m_fOutPkL, m_fOutPkR, m_fOutSqL, m_fOutSqR;
	volatile unsigned m_nOutStatFrames;

	float *volatile m_pCapIn;
	float *volatile m_pCapOut;
	volatile unsigned m_nCapPos;
	volatile unsigned m_nCapMax;
	volatile boolean m_bCapOn;

	// search window for the impulse's peak
	boolean m_bWindow;
	unsigned long long m_nWindowEnd;
	unsigned long long m_nWindowPeakFrame;
	float m_fWindowPeak;
	volatile unsigned m_nPkLast;
	volatile unsigned m_nPkMin;
	volatile unsigned m_nPkMax;
	volatile unsigned m_nPkCount;
};

#endif
