//
// fx_noise.cpp - ADELNoise: hiss and mains hum added to the signal (the noise that a captured amplifier has and a NAM model cannot reproduce).
//
// Use. HISS goes BEFORE the NAM block: it is the input noise of the amplifier, and the NAM amplifies it like the real stages do (a high-gain model
// amplifies a small noise much more than a clean one, and a loud note compresses it). Measured on this firmware's engine with white noise of -120 dBFS rms at
// the input of two models (input level +15 dBu): a JC800 Hi Lead capture gives -63 dBFS at the output, a Lanney Crunch capture -91 dBFS. HUM goes AFTER the
// NAM block (it comes from the supply and the heaters, not from the input). For both, use two blocks: one with Hum at its minimum, one with Hiss at its minimum.
//
// Facts used (read 2026-11-17):
//  - Svetlana 12AX7 datasheet (https://img.audiomania.ru/data/svetlana_12ax7.pdf): typical equivalent hum and noise voltage 1.8 uV per section (grid resistance 0).
//    1.8 uV = -112.7 dBu; at an input level of +15 dBu for 0 dBFS that is -127.7 dBFS, the default of Hiss.
//  - Mojotone, "Hiss, Hum, Crackle, Buzz - What is a Noise Floor?": the noise floor depends on the gain squeezed into the signal; high-gain amplifiers have
//    built-in hiss, hum and buzz are not acceptable.
//  - Wikipedia, "Hum bar": hum from a rectifier supply is at twice the mains frequency (100 Hz on 50 Hz mains, 120 Hz on 60 Hz) with heavy harmonic content;
//    tube equipment can also hum from heater-cathode leakage and the heater's fields (at the mains frequency).
//
// Values set by design (no source gives them): the white/pink mix of the hiss (tube noise has a 1/f part), its default bandwidth, the harmonic amplitudes
// of the three hum types, the default hum level. Pink noise: Paul Kellet's three-pole filter (coefficients from the public musicdsp archive), normalised
// to the rms of its white input (x 0.3379 at 48 kHz); its slope is right at 44.1 - 48 kHz and a little off at other rates.
//
// Levels are rms BEFORE the bandwidth and low-cut filters of the hiss (the filters take away what they remove, like a tone control). The "Noise level"
// output shows the rms of what is really added (hiss + hum, 0.2 s average). The noise is the same on both channels (one amplifier) and is added to the
// signal: with both levels at their minimum the block is bit-exact transparent.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

enum { PHiss = 0, PColor, PBandwidth, PLowCut, PHum, PHumType, PMains, NParams = 7, POutLevel = 7 };

const float kHissOff = -129.75f;	// at or below this the hiss is off
const float kHumOff = -119.75f;
const float kPinkNorm = 0.3379f;
const unsigned kMaxHarm = 24;

// hum types: fundamental = mains x kFund[type]; amplitudes and phases (radians) of the harmonics; 0 = no more
const unsigned kFund[3] = {2, 1, 1};
const unsigned kHarm[3] = {8, 6, 20};
const float kAmp0[8] = {1.0f, 0.5f, 0.3333f, 0.25f, 0.2f, 0.1667f, 0.1429f, 0.125f};	// supply ripple: sawtooth-like, 1/k
const float kAmp1[6] = {1.0f, 0.4f, 0.25f, 0.12f, 0.08f, 0.05f};				// heater / induction
// buzz: 1/sqrt(k), computed

inline float FastLog2 (float x)
{
	union { float f; u32 u; } v;
	v.f = x;
	const int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
	const float m = v.f - 1.0f;
	return (float) e + m * (1.4418258f + m * (-0.708682101f + m * (0.415421949f + m * (-0.194422672f + m * 0.0458855267f))));
}

inline float Ex (float x)
{
	x = x < -80.0f ? -80.0f : (x > 80.0f ? 80.0f : x);
	const float t = x * 1.44269504f;
	int n = (int) t;
	if ((float) n > t)
	{
		n--;
	}
	const float r = (t - (float) n) * 0.69314718f;
	const float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f + r * (0.041666668f + r * (0.0083333338f + r * (0.0013888889f + r * 0.0001984127f))))));
	union { float f; u32 u; } s;
	s.u = (u32) (n + 127) << 23;
	return p * s.f;
}

inline float Clamp (float x, float lo, float hi)
{
	return !(x >= lo) ? lo : (x > hi ? hi : x);
}

unsigned s_nSeed = 0;		// every instance starts from a different seed (two blocks do not make the same noise)

class CNoise : public CFx
{
public:
	CNoise (float fRate) : m_fRate (fRate)
	{
		m_nRng = 0x9E3779B9u + 0x7F4A7C15u * (++s_nSeed);
		m_fB0 = m_fB1 = m_fB2 = 0.0f;
		m_fLp1 = m_fLp2 = m_fHp = 0.0f;
		m_fMs = 0.0f;
		m_fKMs = 1.0f - Ex (-1.0f / (fRate * 0.2f));
		m_fRe = 1.0f; m_fIm = 0.0f;
		m_nRenorm = 0;
		m_bValid = false;
		for (unsigned i = 0; i < NParams; i++)
		{
			m_fP[i] = 0.0f;
		}
		m_nHarm = 0; m_bHiss = m_bHum = false;
		m_fHissAmp = m_fHumScale = 0.0f; m_fColor = 0.0f; m_fALp = m_fAHp = 0.0f;
		m_fCosT = 1.0f; m_fSinT = 0.0f;
		for (unsigned k = 0; k < kMaxHarm; k++)
		{
			m_fAmp[k] = 0.0f; m_fPhC[k] = 1.0f; m_fPhS[k] = 0.0f;
		}
	}

	unsigned Type (void) const { return FxNoise; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		Update (pP);
		const unsigned nCh = *pMono ? 1 : 2;
		if (!m_bHiss && !m_bHum)
		{
			m_fMs *= Ex (-(float) n * m_fKMs);		// the meter fades towards silence
			m_fMs = m_fMs < 1e-30f ? 0.0f : m_fMs;
			if (nCh == 1)
			{
				for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
			}
			return;
		}
		for (unsigned j = 0; j < n; j++)
		{
			float x = 0.0f;
			if (m_bHiss)
			{
				// uniform noise with unit rms (xorshift32)
				m_nRng ^= m_nRng << 13; m_nRng ^= m_nRng >> 17; m_nRng ^= m_nRng << 5;
				const float w = ((float) (int) m_nRng * (1.0f / 2147483648.0f)) * 1.7320508f;
				m_fB0 = 0.99765f * m_fB0 + w * 0.0990460f;
				m_fB1 = 0.96300f * m_fB1 + w * 0.2965164f;
				m_fB2 = 0.57000f * m_fB2 + w * 1.0526913f;
				const float pink = (m_fB0 + m_fB1 + m_fB2 + w * 0.1848f) * kPinkNorm;
				float h = w + m_fColor * (pink - w);
				m_fLp1 += m_fALp * (h - m_fLp1);
				m_fLp2 += m_fALp * (m_fLp1 - m_fLp2);
				m_fHp += m_fAHp * (m_fLp2 - m_fHp);
				x = (m_fLp2 - m_fHp) * m_fHissAmp;
			}
			if (m_bHum)
			{
				// phasor at the fundamental, harmonics by repeated multiplication
				const float re = m_fRe * m_fCosT - m_fIm * m_fSinT;
				m_fIm = m_fIm * m_fCosT + m_fRe * m_fSinT;
				m_fRe = re;
				if (++m_nRenorm >= 4096)
				{
					m_nRenorm = 0;
					const float k = 0.5f * (3.0f - (m_fRe * m_fRe + m_fIm * m_fIm));
					m_fRe *= k; m_fIm *= k;
				}
				float zr = m_fRe, zi = m_fIm, s = 0.0f;
				for (unsigned k = 0; k < m_nHarm; k++)
				{
					s += m_fAmp[k] * (zr * m_fPhC[k] - zi * m_fPhS[k]);
					const float nr = zr * m_fRe - zi * m_fIm;
					zi = zr * m_fIm + zi * m_fRe;
					zr = nr;
				}
				x += s * m_fHumScale;
			}
			m_fMs += (x * x - m_fMs) * m_fKMs;
			pL[j] += x;
			if (nCh > 1)
			{
				pR[j] += x;
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++)
			{
				pR[j] = pL[j];
			}
		}
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			pOut[i] = 0.0f;
		}
		pOut[POutLevel] = m_fMs > 1e-28f ? 3.0102999f * FastLog2 (m_fMs) : -140.0f;
	}

private:
	void Update (const float *pP)
	{
		bool bSame = m_bValid;
		for (unsigned i = 0; i < NParams && bSame; i++)
		{
			bSame = pP[i] == m_fP[i];
		}
		if (bSame)
		{
			return;
		}
		for (unsigned i = 0; i < NParams; i++)
		{
			m_fP[i] = pP[i];
		}
		m_bValid = true;
		const float fHiss = Clamp (pP[PHiss], -130.0f, -30.0f);
		m_bHiss = fHiss > kHissOff;
		m_fColor = Clamp (pP[PColor], 0.0f, 100.0f) * 0.01f;
		// rms of the mix w + c (pink - w): white and normalised pink have a correlation of 0.5518 (lag-0 coefficient 1.63305 over the 2.9593 rms of Kellet's filter)
		const float fVar = (1.0f - m_fColor) * (1.0f - m_fColor) + m_fColor * m_fColor + 2.0f * m_fColor * (1.0f - m_fColor) * 0.5518f;
		m_fHissAmp = DbToLin (fHiss) / DspSqrtF (fVar);
		const float fBw = Clamp (pP[PBandwidth], 1.0f, 20.0f) * 1000.0f;
		const float fLc = Clamp (pP[PLowCut], 20.0f, 400.0f);
		m_fALp = 1.0f - Ex (-6.2831853f * (fBw > 0.45f * m_fRate ? 0.45f * m_fRate : fBw) / m_fRate);
		m_fAHp = 1.0f - Ex (-6.2831853f * fLc / m_fRate);
		const float fHum = Clamp (pP[PHum], -120.0f, -30.0f);
		m_bHum = fHum > kHumOff;
		const int nType = (int) Clamp (pP[PHumType], 0.0f, 2.49f);
		const float fMains = pP[PMains] > 0.5f ? 60.0f : 50.0f;
		const float f0 = fMains * (float) kFund[nType];
		const float th = 6.2831853f * f0 / m_fRate;
		m_fCosT = DspCos (th);
		m_fSinT = DspSin (th);
		unsigned nH = kHarm[nType];
		while (nH > 1 && f0 * (float) nH > 0.45f * m_fRate)
		{
			nH--;
		}
		m_nHarm = nH;
		float fPow = 0.0f;
		for (unsigned k = 0; k < kMaxHarm; k++)
		{
			float a = 0.0f, ph = 0.0f;
			if (k < nH)
			{
				if (nType == 0) { a = kAmp0[k]; ph = 0.0f; }
				else if (nType == 1) { a = kAmp1[k]; ph = 0.5f * (float) k; }
				else { a = 1.0f / DspSqrtF ((float) (k + 1)); ph = 0.45f * (float) (k * k % 7); }
			}
			m_fAmp[k] = a;
			m_fPhC[k] = DspCos (ph);		// phases are between 0 and pi
			m_fPhS[k] = DspSin (ph);
			fPow += a * a * 0.5f;
		}
		m_fHumScale = fPow > 0.0f ? DbToLin (fHum) / DspSqrtF (fPow) : 0.0f;
	}

	float m_fRate;
	float m_fP[NParams];
	bool m_bValid, m_bHiss, m_bHum;
	u32 m_nRng;
	float m_fB0, m_fB1, m_fB2, m_fLp1, m_fLp2, m_fHp, m_fMs, m_fKMs;
	float m_fHissAmp, m_fColor, m_fALp, m_fAHp, m_fHumScale, m_fCosT, m_fSinT, m_fRe, m_fIm;
	float m_fAmp[kMaxHarm], m_fPhC[kMaxHarm], m_fPhS[kMaxHarm];
	unsigned m_nHarm, m_nRenorm;
};

}

CFx *NewNoise (float fRate)
{
	return new CNoise (fRate);
}
