//
// ftools.h - 10 service functions with an ON/OFF switch (all off at startup), inspired by studio and recording work:
//   0 Hum Killer          mains hum filter (50 or 60 Hz and harmonics), on the input
//   1 Rumble Filter       80 Hz high-pass on the input: removes hand knocks, wind and stage noises
//   2 Master Limiter      brick-wall limiter on the output (adjustable ceiling): protects amplifier and speakers
//   3 Mono Check          sums L+R to mono on the output: used to check phase compatibility and the sound on a single speaker
//   4 Air Band            12 kHz shelf (Maag EQ4 style): opens the highs without making them harsh
//   5 Console Glue        soft console/tape saturation: rounds the peaks
//   6 Metronome           click in time mixed into the output, with an accent on the first of four beats
//   7 Spectrum (RTA)      third-octave analyzer (24 bands, 63 Hz - 12.7 kHz) of the output, to see where the energy is
//   8 Tuner               chromatic tuner on the input (with output muting)
//   9 Meters              momentary loudness (LUFS style), peak, peak/RMS ratio and phase correlation between L and R
// ProcessIn/ProcessOut run in the audio callback (only if the function is on); the heavy analyses (tuner, conversion to dB) in the main loop (Tick).
//
#ifndef _ftools_h
#define _ftools_h

#include <circle/types.h>

#define FT_COUNT	10
enum TFeat { FtHum = 0, FtRumble, FtLimit, FtMono, FtAir, FtSat, FtClick, FtRta, FtTuner, FtMeter };
#define FT_RTA_BANDS	24

class CFeatures
{
public:
	CFeatures (float fRate);

	// ---- configuration (main loop)
	boolean On (unsigned nId) const { return nId < FT_COUNT && m_On[nId] != 0; }
	void SetOn (unsigned nId, boolean bOn);
	// parameters by name: mains (50/60), bpm (40..240), clickdb (-40..-6), airdb (0..12), satdrive (0..100), ceil (-12..0), tmute (0/1); returns FALSE if the key does not exist
	boolean SetParam (const char *pKey, float fValue);
	float Param (const char *pKey) const;
	static const char *Name (unsigned nId);		// API key of the function ("hum", "rumble", ...)
	static int IdByName (const char *pName);	// -1 if it does not exist
	boolean Any (void) const { return m_bAny; }
	boolean Persistent (unsigned nId) const { return nId <= FtSat; }		// at startup only the signal functions stay on; metronome, analyses and tuner always start off

	// ---- saving to the SD (text "key=value")
	unsigned Serialize (char *pBuf, unsigned nSize) const;
	void Parse (const char *pText);

	// ---- callback audio
	void ProcessIn (float *pL, float *pR, unsigned n);
	void ProcessOut (float *pL, float *pR, unsigned n);

	// ---- main loop: analysis; nMs = clock in milliseconds
	void Tick (unsigned nMs);

	// ---- results for the page
	const float *Rta (void) const { return m_fRtaDb; }		// FT_RTA_BANDS values in dBFS (band RMS)
	static float RtaCenter (unsigned k);
	boolean TunerValid (void) const { return m_bTunerOk; }
	float TunerHz (void) const { return m_fTunerHz; }
	int TunerMidi (void) const { return m_nTunerMidi; }
	float TunerCents (void) const { return m_fTunerCents; }
	float LufsMomentary (void) const { return m_fLufs; }
	float PeakDb (void) const { return m_fPeakDb; }
	float CrestDb (void) const { return m_fCrestDb; }
	float Correlation (void) const { return m_fCorr; }
	float LimiterReductionDb (void) const { return m_fGrDb; }
	static void NoteName (int nMidi, char *pOut);		// "E2", "F#3"...

private:
	struct TBq
	{
		float b0, b1, b2, a1, a2;
		float z1[2], z2[2];
	};
	void Recalc (void);
	static void BqSet (TBq *p, float b0, float b1, float b2, float a0, float a1, float a2);
	static float BqRun (TBq *p, unsigned c, float x);
	void TunerAnalyze (void);

private:
	float m_fRate;
	volatile u8 m_On[FT_COUNT];
	volatile boolean m_bAny;
	// parameters
	float m_fMains, m_fBpm, m_fClickDb, m_fAirDb, m_fSat, m_fCeilDb, m_fTMute;
	// input filters: 4 notches + high-pass; output filter: air shelf
	TBq m_Hum[4], m_Hp, m_Air;
	// limiter, saturation, click
	float m_fLimG, m_fLimThr, m_fLimCeil, m_fGrDb;
	float m_fSatG;
	float m_fClickAmp;
	unsigned m_nClickPos, m_nClickBeat, m_nClickLen;
	float m_fClickPh;
	// RTA
	TBq m_Band[FT_RTA_BANDS];
	float m_fBandAcc[FT_RTA_BANDS];
	unsigned m_nBandCnt;
	float m_fRtaDb[FT_RTA_BANDS];
	unsigned m_nRtaAt;
	// tuner: input decimated by 4, ring of 4096 samples
	float m_fTunBuf[4096];
	volatile unsigned m_nTunW;
	float m_fTunAcc;
	unsigned m_nTunAccN;
	boolean m_bTunerOk;
	float m_fTunerHz, m_fTunerCents;
	int m_nTunerMidi, m_nTunHist[3], m_nTunHistN;
	unsigned m_nTunerAt;
	float m_fTunLevel;
	// meters
	TBq m_KShelf, m_KHp;
	float m_fKAcc, m_fRmsAcc, m_fPeakAcc, m_fCorrLR, m_fCorrLL, m_fCorrRR;
	unsigned m_nKCnt;
	float m_fKBlocks[4], m_fRBlocks[4], m_fPBlocks[4];
	unsigned m_nKBlockN;
	float m_fLufs, m_fPeakDb, m_fCrestDb, m_fCorr;
	unsigned m_nMeterAt;
	float m_fPeakHold;
};

#endif
