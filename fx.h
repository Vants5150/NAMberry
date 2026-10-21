//
// fx.h - effects in the audio callback (phaser, ...). Every instance has its own state (created by the main loop, never
// destroyed) and receives the block's parameters at every call, in the units of fx_defs.h.
// Process: (pL, pR) goes in and out in place; *pMono = L and R identical (in and out).
//
#ifndef _fx_h
#define _fx_h

#include <circle/types.h>
#include "fx_defs.h"

#define FX_MAX_PARAMS	8

unsigned FxProbeMuteOut (void);		// 1 if an ADELSendSweep is playing with "Mute Output" on (read and cleared by the audio callback, refreshed at every block)
unsigned FxProbePlaying (void);		// 1 if an ADELSendSweep is playing the sample

class CFx
{
public:
	virtual ~CFx (void) {}
	virtual void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP) = 0;
	virtual unsigned Type (void) const = 0;
	virtual void GetOutputs (float *pOut) const { (void) pOut; }	// values of the measured outputs (control indices, FX_MAX_P values)
	// Cab IR: nCh channels of nTaps floats one after the other (from the main loop, the audio callback does not stop)
	virtual boolean SetImpulse (const float *pData, unsigned nTaps, unsigned nCh) { (void) pData; (void) nTaps; (void) nCh; return FALSE; }
};

CFx *FxCreate (unsigned nType, float fRate);

// ADEL effects (one file per plugin: the control names of each plugin cannot live in the same unit)
CFx *NewAdelComp (float fRate);
CFx *NewAdelDelay (float fRate);
CFx *NewAdelVerb (float fRate);
CFx *NewAdelPhase (float fRate);
CFx *NewAdelSweep (float fRate);
CFx *NewAdelSpring (float fRate);
CFx *NewAdelChorus (float fRate);	// 0 if the type does not exist
CFx *NewCabIR (float fRate);
CFx *NewAdelEqual (float fRate);
CFx *NewAdelShifter (float fRate);
CFx *NewAdelDelayM (float fRate);
CFx *NewAdelVerbM (float fRate);
CFx *NewExtraFx (unsigned nType, float fRate);		// ToTest effects 28..47 (fx_extra.cpp)
CFx *NewExtraFx2 (unsigned nType, float fRate);		// ToTest effects 48..77 (fx_extra2.cpp)
CFx *NewFuzzFace (float fRate);			// Fuzz Face Real (fx_fuzzface.cpp)
CFx *NewRangemaster (float fRate);			// Rangemaster Real (fx_rangemaster.cpp)
CFx *NewDs1 (float fRate);			// DS-1 Real (fx_ds1.cpp)
CFx *NewGuvnor (float fRate);			// Guv'nor Real (fx_guvnor.cpp)
CFx *NewKlon (float fRate);			// Klon Real (fx_klon.cpp)
CFx *NewBender (float fRate);			// Tone Bender Real (fx_bender.cpp)
CFx *NewWah (float fRate);			// Wah Real (fx_wah.cpp)
CFx *NewToneStack (float fRate);			// Tone Stack Real (fx_tonestack.cpp)
CFx *NewDynaComp (float fRate);			// Dyna Comp Real (fx_dynacomp.cpp)
CFx *NewCe2 (float fRate);			// Chorus CE-2 Real (fx_ce2.cpp)
CFx *NewFuzzFactory (float fRate);			// Fuzz Factory Real (fx_factory.cpp)
CFx *NewPhase90 (float fRate);			// Phase 90 Real (fx_phase90.cpp)
CFx *NewCe1 (float fRate);			// Chorus CE-1 Real (fx_ce1.cpp)
CFx *NewAd9 (float fRate);			// Analog Delay AD9 Real (fx_ad9.cpp)
CFx *NewMicroFlanger (float fRate);		// Flanger Micro Real (fx_mflanger.cpp)
CFx *NewUniVibe (float fRate);			// Uni-Vibe Real (fx_univibe.cpp)
CFx *NewOcd (float fRate);			// OCD Real (fx_ocd.cpp)
CFx *NewOctaver (float fRate);			// Octaver Real (fx_octaver.cpp)
CFx *NewFlanger117 (float fRate);			// MXR 117 Flanger Real (fx_flanger117.cpp)
CFx *NewMuff (float fRate);				// Big Muff Pi Real (fx_muff.cpp)
CFx *NewDimD (float fRate);				// Dimension D Real (fx_dimd.cpp)
CFx *NewOpticalLA (float fRate);			// Optical LA Real (fx_la2a.cpp)
CFx *NewRatTS (float fRate);				// RAT (fx_rat.cpp)
CFx *NewTubeScreamer (float fRate);			// Tube Screamer (fx_tube.cpp)

// MIDI tempo (clock or tap tempo) in bpm, 0 = unknown: written by the kernel, read by the delays with the "Sync" parameter
extern volatile float g_fFxTempoBpm;
// delay in ms: with sel (0 = Off, 1 = 1/4, 2 = dotted 1/8, 3 = 1/8, 4 = 1/8 triplet, 5 = 1/16) and a known tempo the subdivision applies, otherwise the manual value; always between mn and mx
static inline float FxSyncMs (float fManualMs, float fSel, float fMin, float fMax)
{
	static const float k[6] = {0.0f, 1.0f, 0.75f, 0.5f, 0.33333333f, 0.25f};
	const int s = (int) (fSel + 0.5f);
	const float bpm = g_fFxTempoBpm;
	if (s <= 0 || s > 5 || bpm < 30.0f)
	{
		return fManualMs;
	}
	const float ms = 60000.0f / bpm * k[s];
	return ms < fMin ? fMin : (ms > fMax ? fMax : ms);
}

#endif
