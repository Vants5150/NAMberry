//
// fx.cpp - effects: phaser (others coming). No math library, no allocation in the callback.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

// ---- Phaser: chain of first-order all-pass filters, frequency swept by an LFO (stereo: right channel shifted by 90 degrees)
class CPhaser : public CFx
{
public:
	CPhaser (float fRate) : m_fRate (fRate), m_fPhase (0.0f)
	{
		memset (m_z, 0, sizeof m_z);
		memset (m_fb, 0, sizeof m_fb);
	}

	unsigned Type (void) const { return FxPhaser; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *P)
	{
		const float fHz = P[0] < 0.05f ? 0.05f : (P[0] > 5.0f ? 5.0f : P[0]);
		const float fDepth = (P[1] < 0.0f ? 0.0f : (P[1] > 100.0f ? 100.0f : P[1])) * 0.01f;
		float fFb = P[2] * 0.01f;
		fFb = fFb < -0.9f ? -0.9f : (fFb > 0.9f ? 0.9f : fFb);
		unsigned nSt = (unsigned) (P[3] + 0.5f);
		nSt = nSt < 2 ? 2 : (nSt > MAX_STAGES ? MAX_STAGES : (nSt & ~1u));
		const float fMix = (P[4] < 0.0f ? 0.0f : (P[4] > 100.0f ? 100.0f : P[4])) * 0.01f;

		// center frequency of the all-passes for the two channels: from 150 Hz to about 3.8 kHz (28 dB = x25) exponentially
		float a[2];
		for (unsigned c = 0; c < 2; c++)
		{
			float ph = m_fPhase + (c ? 0.25f : 0.0f);
			ph -= (float) (int) ph;
			float tri = ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
			tri = tri * tri * (3.0f - 2.0f * tri);
			const float fc = 150.0f * DbToLin (28.0f * fDepth * tri);
			const float x = 3.14159265f * fc / m_fRate;
			const float w = x + x * x * x * (1.0f / 3.0f) + x * x * x * x * x * (2.0f / 15.0f);	// tan(x) per x < 0,3
			a[c] = (1.0f - w) / (1.0f + w);
		}
		m_fPhase += fHz * (float) n / m_fRate;
		m_fPhase -= (float) (int) m_fPhase;

		float *pCh[2] = {pL, pR};
		for (unsigned c = 0; c < 2; c++)
		{
			float *p = pCh[c];
			float *z = m_z[c];
			const float ac = a[c];
			float fbk = m_fb[c];
			for (unsigned j = 0; j < n; j++)
			{
				const float x0 = p[j];
				float y = x0 + fbk * fFb;
				for (unsigned s = 0; s < nSt; s++)
				{
					const float o = ac * y + z[s];		// y[n] = a x[n] + z ; z = x[n] - a y[n]
					z[s] = y - ac * o;
					y = o;
				}
				fbk = y;
				p[j] = x0 + (y - x0) * fMix;		// mix 0 = dry, 100 = effect only
			}
			m_fb[c] = fbk;
			for (unsigned s = 0; s < MAX_STAGES; s++)
			{
				if (z[s] < 1e-15f && z[s] > -1e-15f)
				{
					z[s] = 0.0f;	// no denormal numbers in silence
				}
			}
			if (fbk < 1e-15f && fbk > -1e-15f)
			{
				m_fb[c] = 0.0f;
			}
		}
		if (fMix > 0.0f)
		{
			*pMono = 0;		// the two channels have shifted LFOs: the output is stereo
		}
	}

private:
	enum { MAX_STAGES = 8 };
	float m_fRate;
	float m_fPhase;
	float m_z[2][MAX_STAGES];
	float m_fb[2];
};

// ---- Reverb: 8 damped comb filters + 4 all-passes per channel (Freeverb scheme), pre-delay and stereo width
class CReverb : public CFx
{
public:
	CReverb (float fRate) : m_nPre (0)
	{
		static const unsigned nComb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
		static const unsigned nAp[4] = {556, 441, 341, 225};
		const float k = fRate / 44100.0f;
		memset (m_Comb, 0, sizeof m_Comb);
		memset (m_Ap, 0, sizeof m_Ap);
		memset (m_Pre, 0, sizeof m_Pre);
		memset (m_fStore, 0, sizeof m_fStore);
		for (unsigned c = 0; c < 2; c++)
		{
			for (unsigned i = 0; i < 8; i++)
			{
				m_nCombLen[c][i] = (unsigned) ((float) (nComb[i] + (c ? 23 : 0)) * k);
				m_nCombPos[c][i] = 0;
			}
			for (unsigned i = 0; i < 4; i++)
			{
				m_nApLen[c][i] = (unsigned) ((float) (nAp[i] + (c ? 23 : 0)) * k);
				m_nApPos[c][i] = 0;
			}
		}
		m_fRate = fRate;
	}

	unsigned Type (void) const { return FxReverb; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *P)
	{
		const float fSize = (P[0] < 0.0f ? 0.0f : (P[0] > 100.0f ? 100.0f : P[0])) * 0.01f;
		const float fDamp = (P[1] < 0.0f ? 0.0f : (P[1] > 100.0f ? 100.0f : P[1])) * 0.01f * 0.4f;
		float fPre = P[2] < 0.0f ? 0.0f : (P[2] > 100.0f ? 100.0f : P[2]);
		unsigned nPre = (unsigned) (fPre * 0.001f * m_fRate);
		if (nPre >= PRE_MAX)
		{
			nPre = PRE_MAX - 1;
		}
		const float fWidth = (P[3] < 0.0f ? 0.0f : (P[3] > 100.0f ? 100.0f : P[3])) * 0.01f;
		const float fMix = (P[4] < 0.0f ? 0.0f : (P[4] > 100.0f ? 100.0f : P[4])) * 0.01f;
		const float fFeed = fSize * 0.28f + 0.7f;
		const float fW1 = 0.5f * (1.0f + fWidth), fW2 = 0.5f * (1.0f - fWidth);

		for (unsigned j = 0; j < n; j++)
		{
			const float fDry0 = pL[j], fDry1 = pR[j];
			// pre-delay on the mono sum
			const float fIn0 = (fDry0 + fDry1) * 0.5f;
			m_Pre[m_nPre] = fIn0;
			unsigned nRd = m_nPre >= nPre ? m_nPre - nPre : m_nPre + PRE_MAX - nPre;
			const float fIn = m_Pre[nRd] * INPUT_GAIN;
			m_nPre = m_nPre + 1 >= PRE_MAX ? 0 : m_nPre + 1;

			float fOut[2];
			for (unsigned c = 0; c < 2; c++)
			{
				float s = 0.0f;
				for (unsigned i = 0; i < 8; i++)
				{
					float *b = m_Comb[c][i];
					unsigned &p = m_nCombPos[c][i];
					const float o = b[p];
					m_fStore[c][i] = o * (1.0f - fDamp) + m_fStore[c][i] * fDamp;
					b[p] = fIn + m_fStore[c][i] * fFeed;
					p = p + 1 >= m_nCombLen[c][i] ? 0 : p + 1;
					s += o;
				}
				for (unsigned i = 0; i < 4; i++)
				{
					float *b = m_Ap[c][i];
					unsigned &p = m_nApPos[c][i];
					const float bo = b[p];
					const float o = bo - s;
					b[p] = s + bo * 0.5f;
					p = p + 1 >= m_nApLen[c][i] ? 0 : p + 1;
					s = o;
				}
				fOut[c] = s * WET_SCALE;
			}
			const float wl = fOut[0] * fW1 + fOut[1] * fW2;
			const float wr = fOut[1] * fW1 + fOut[0] * fW2;
			pL[j] = fDry0 * (1.0f - fMix) + wl * fMix;
			pR[j] = fDry1 * (1.0f - fMix) + wr * fMix;
		}

		// no denormal numbers in the tails
		for (unsigned c = 0; c < 2; c++)
		{
			for (unsigned i = 0; i < 8; i++)
			{
				if (m_fStore[c][i] < 1e-15f && m_fStore[c][i] > -1e-15f)
				{
					m_fStore[c][i] = 0.0f;
				}
			}
		}
		if (fMix > 0.0f)
		{
			*pMono = 0;
		}
	}

private:
	enum { COMB_MAX = 1800, AP_MAX = 640, PRE_MAX = 4900 };
	static constexpr float INPUT_GAIN = 0.015f;
	static constexpr float WET_SCALE = 3.0f;

	float m_fRate;
	float m_Comb[2][8][COMB_MAX];
	float m_Ap[2][4][AP_MAX];
	float m_Pre[PRE_MAX];
	float m_fStore[2][8];
	unsigned m_nCombLen[2][8], m_nCombPos[2][8];
	unsigned m_nApLen[2][4], m_nApPos[2][4];
	unsigned m_nPre;
};

// ---- Stereo delay with filtered repeats (Tone) and ping-pong (Spread). Mix: from 0 to 50 the effect rises, from 50 to 100 the dry falls (100 = repeats only)
class CDelay : public CFx
{
public:
	CDelay (float fRate) : m_fRate (fRate), m_nWr (0), m_fDly (0.0f), m_bInit (false)
	{
		// two separate 308 KB buffers: a single 616 KB block exceeds the 512 KB beyond which Circle's heap does not give memory back
		for (unsigned c = 0; c < 2; c++)
		{
			m_pBuf[c] = new float[BUF_LEN];
			memset (m_pBuf[c], 0, sizeof (float) * BUF_LEN);
		}
		m_fLp[0] = m_fLp[1] = 0.0f;
	}

	~CDelay (void)
	{
		delete[] m_pBuf[0];
		delete[] m_pBuf[1];
	}

	unsigned Type (void) const { return FxDelay; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *P)
	{
		float fTime = P[0] < 10.0f ? 10.0f : (P[0] > 1500.0f ? 1500.0f : P[0]);
		const float fFb = (P[1] < 0.0f ? 0.0f : (P[1] > 95.0f ? 95.0f : P[1])) * 0.01f;
		const float fTone = (P[2] < 0.0f ? 0.0f : (P[2] > 100.0f ? 100.0f : P[2])) * 0.01f;
		const float fSpread = (P[3] < 0.0f ? 0.0f : (P[3] > 100.0f ? 100.0f : P[3])) * 0.01f;
		const float fMix = (P[4] < 0.0f ? 0.0f : (P[4] > 100.0f ? 100.0f : P[4])) * 0.01f;
		const float fTarget = fTime * 0.001f * m_fRate;
		if (!m_bInit)
		{
			m_fDly = fTarget;
			m_bInit = true;
		}
		// low-pass filter in the repeats: from 500 Hz to about 12 kHz
		const float x = 6.2831853f * 500.0f * DbToLin (27.6f * fTone) / m_fRate;
		const float kLp = x > 2.0f ? 0.667f : x / (1.0f + x);
		const float fDryG = fMix > 0.5f ? 2.0f * (1.0f - fMix) : 1.0f;
		const float fWetG = fMix < 0.5f ? 2.0f * fMix : 1.0f;
		const boolean bSum = *pMono || fSpread > 0.0f;

		for (unsigned j = 0; j < n; j++)
		{
			m_fDly += (fTarget - m_fDly) * 0.0004f;		// time flows without jumps (like a tape)
			float fRd = (float) m_nWr - m_fDly;
			if (fRd < 0.0f)
			{
				fRd += (float) BUF_LEN;
			}
			unsigned i0 = (unsigned) fRd;
			const float fr = fRd - (float) i0;
			const unsigned i1 = i0 + 1 >= BUF_LEN ? 0 : i0 + 1;
			float y[2];
			for (unsigned c = 0; c < 2; c++)
			{
				y[c] = m_pBuf[c][i0] * (1.0f - fr) + m_pBuf[c][i1] * fr;
			}
			const float dry0 = pL[j], dry1 = pR[j];
			float in0, in1;
			if (bSum)
			{
				const float s = (dry0 + dry1) * 0.5f;
				in0 = s;
				in1 = s * (1.0f - fSpread);
			}
			else
			{
				in0 = dry0;
				in1 = dry1;
			}
			// repeats: filtered, then crossed between the channels in proportion to the ping-pong
			m_fLp[0] += (y[0] - m_fLp[0]) * kLp;
			m_fLp[1] += (y[1] - m_fLp[1]) * kLp;
			const float f0 = m_fLp[0] * (1.0f - fSpread) + m_fLp[1] * fSpread;
			const float f1 = m_fLp[1] * (1.0f - fSpread) + m_fLp[0] * fSpread;
			m_pBuf[0][m_nWr] = in0 + f0 * fFb;
			m_pBuf[1][m_nWr] = in1 + f1 * fFb;
			m_nWr = m_nWr + 1 >= BUF_LEN ? 0 : m_nWr + 1;
			pL[j] = dry0 * fDryG + y[0] * fWetG;
			pR[j] = dry1 * fDryG + y[1] * fWetG;
		}
		if (fabs_ (m_fLp[0]) < 1e-15f)
		{
			m_fLp[0] = 0.0f;
		}
		if (fabs_ (m_fLp[1]) < 1e-15f)
		{
			m_fLp[1] = 0.0f;
		}
		if (fSpread > 0.0f && fMix > 0.0f)
		{
			*pMono = 0;
		}
	}

private:
	enum { BUF_LEN = 77000 };		// 1,6 s a 48 kHz
	static float fabs_ (float v) { return v < 0.0f ? -v : v; }

	float m_fRate;
	unsigned m_nWr;
	float m_fDly;
	bool m_bInit;
	float m_fLp[2];
	float *m_pBuf[2];
};

// ---- Compressor: peak detector linked on the two channels (same gain: a mono path stays mono), soft knee,
// attack and release on the gain reduction in dB. Mix = parallel compression.
static inline float FastLog2 (float x)
{
	union { float f; u32 u; } v;
	v.f = x;
	const int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
	const float m = v.f;
	const float p = -1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m;
	return (float) e + p;
}

class CCompressor : public CFx
{
public:
	CCompressor (float fRate) : m_fRate (fRate), m_fGr (0.0f) {}

	unsigned Type (void) const { return FxCompressor; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *P)
	{
		(void) pMono;
		const float fThr = P[0] < -60.0f ? -60.0f : (P[0] > 0.0f ? 0.0f : P[0]);
		const float fRatio = P[1] < 1.0f ? 1.0f : (P[1] > 20.0f ? 20.0f : P[1]);
		const float fAtk = P[2] < 0.1f ? 0.1f : (P[2] > 100.0f ? 100.0f : P[2]);
		const float fRel = P[3] < 10.0f ? 10.0f : (P[3] > 1000.0f ? 1000.0f : P[3]);
		const float fKnee = P[4] < 0.0f ? 0.0f : (P[4] > 18.0f ? 18.0f : P[4]);
		const float fMk = DbToLin (P[5] < 0.0f ? 0.0f : (P[5] > 24.0f ? 24.0f : P[5]));
		const float fMix = (P[6] < 0.0f ? 0.0f : (P[6] > 100.0f ? 100.0f : P[6])) * 0.01f;
		// coefficients: exp(-1/(t fs)) = 10^(-8.686/(t fs) / 20)
		const float kA = DbToLin (-8.685889638f / (fAtk * 0.001f * m_fRate));
		const float kR = DbToLin (-8.685889638f / (fRel * 0.001f * m_fRate));
		const float fSlope = 1.0f / fRatio - 1.0f;

		for (unsigned j = 0; j < n; j++)
		{
			const float l = pL[j], r = pR[j];
			float pk = l < 0.0f ? -l : l;
			const float pr = r < 0.0f ? -r : r;
			pk = pk > pr ? pk : pr;
			// level in dB (peak); below -120 dB no reduction
			const float fLvl = 6.0205999f * FastLog2 (pk + 1e-9f);
			const float x = fLvl - fThr;
			float fTgt;		// desired reduction, dB (>= 0)
			if (2.0f * x <= -fKnee)
			{
				fTgt = 0.0f;
			}
			else if (fKnee > 0.0f && 2.0f * x < fKnee)
			{
				const float t = x + fKnee * 0.5f;
				fTgt = -fSlope * t * t / (2.0f * fKnee);
			}
			else
			{
				fTgt = -fSlope * x;
			}
			m_fGr = fTgt > m_fGr ? fTgt + (m_fGr - fTgt) * kA : fTgt + (m_fGr - fTgt) * kR;
			const float g = DbToLin (-m_fGr) * fMk;
			pL[j] = l + (l * g - l) * fMix;
			pR[j] = r + (r * g - r) * fMix;
		}
		if (m_fGr < 1e-12f)
		{
			m_fGr = 0.0f;
		}
	}

private:
	float m_fRate;
	float m_fGr;		// current gain reduction, dB
};

volatile float g_fFxTempoBpm = 0.0f;		// see fx.h

CFx *FxCreate (unsigned nType, float fRate)
{
	switch (nType)
	{
	case FxAdelComp:
		return NewAdelComp (fRate);
	case FxAdelDelay:
		return NewAdelDelay (fRate);
	case FxAdelVerb:
		return NewAdelVerb (fRate);
	case FxAdelPhase:
		return NewAdelPhase (fRate);
	case FxAdelSweep:
		return NewAdelSweep (fRate);
	case FxAdelSpring:
		return NewAdelSpring (fRate);
	case FxAdelChorus:
		return NewAdelChorus (fRate);
	case FxCabIR:
		return NewCabIR (fRate);
	case FxAdelEqual:
		return NewAdelEqual (fRate);
	case FxAdelShifter:
		return NewAdelShifter (fRate);
	case FxShifterManual:
		return NewShifterManual (fRate);
	case FxAdelDelayM:
		return NewAdelDelayM (fRate);
	case FxAdelVerbM:
		return NewAdelVerbM (fRate);
	case FxCompressor:
		return new CCompressor (fRate);
	case FxDelay:
		return new CDelay (fRate);
	case FxReverb:
		return new CReverb (fRate);
	case FxPhaser:
		return new CPhaser (fRate);
	case FxTubeScreamer:
		return NewTubeScreamer (fRate);
	case FxRatTS:
		return NewRatTS (fRate);
	case FxOpticalLA:
		return NewOpticalLA (fRate);
	case FxDimD:
		return NewDimD (fRate);
	case FxMuff:
		return NewMuff (fRate);
	case FxFuzzFace:
		return NewFuzzFace (fRate);
	case FxFlanger:
		return NewFlanger117 (fRate);
	case FxOctaver:
		return NewOctaver (fRate);
	case FxOcd:
		return NewOcd (fRate);
	case FxUniVibe:
		return NewUniVibe (fRate);
	case FxAd9:
		return NewAd9 (fRate);
	case FxMicroFlanger:
		return NewMicroFlanger (fRate);
	case FxCe1:
		return NewCe1 (fRate);
	case FxPhase90:
		return NewPhase90 (fRate);
	case FxFactory:
		return NewFuzzFactory (fRate);
	case FxCe2:
		return NewCe2 (fRate);
	case FxDynaComp:
		return NewDynaComp (fRate);
	case FxToneStack:
		return NewToneStack (fRate);
	case FxCry:
		return NewWah (fRate);
	case FxBender:
		return NewBender (fRate);
	case FxCentaur:
		return NewKlon (fRate);
	case FxGuv:
		return NewGuvnor (fRate);
	case FxDs1:
		return NewDs1 (fRate);
	case FxRange:
		return NewRangemaster (fRate);
	default:
		if (nType >= 48)
		{
			return NewExtraFx2 (nType, fRate);		// 48..77: other ToTest effects
		}
		return NewExtraFx (nType, fRate);		// 28..47: ToTest effects (0 if the type does not exist)
	}
}
