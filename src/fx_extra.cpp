//
// fx_extra.cpp - 20 new "ToTest" effects (types 28..47), inspired by classic pedals and plugins. All without libm (only dsp_util.h), with the state
// inside the object; the delay buffers are allocated separately (at most 192 KB: beyond 512 KB Circle's allocator does not give the memory back).
// Process convention: (pL, pR) goes in and out in place; *pMono != 0 = L and R identical (only L is processed and then copied into R);
// the effects that make the signal stereo (flanger, rotary, widener, uni-vibe...) set *pMono = 0.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

inline float Clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Absf (float v) { return v < 0.0f ? -v : v; }
inline float ExpF (float x) { return DbToLin (Clampf (x, -80.0f, 80.0f) * 8.685889638f); }		// e^x
inline float Tanhf (float x)
{
	if (x > 3.0f) return 1.0f;
	if (x < -3.0f) return -1.0f;
	const float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// sine of a phase p in [0, 1): parabolic approximation with correction (error about 0.1%)
inline float Sin1 (float p)
{
	float x = 3.14159265f - p * 6.2831853f;		// pi .. -pi
	const float y = 1.27323954f * x - 0.40528473f * x * Absf (x);
	return 0.225f * (y * Absf (y) - y) + y;
}
inline float Tri1 (float p) { return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; }
inline float Wrap1 (float p) { return p >= 1.0f ? p - 1.0f : p; }
// coefficient of a one-pole low-pass: y += a (x - y)
inline float LpA (float fHz, float fRate) { return 1.0f - ExpF (-6.2831853f * Clampf (fHz, 0.05f, 0.45f * fRate) / fRate); }

// circular delay line with linear interpolation
class CLine
{
public:
	CLine (unsigned nLen) : m_nLen (nLen), m_nPos (0) { m_p = new float[nLen]; memset (m_p, 0, nLen * sizeof (float)); }
	~CLine (void) { delete[] m_p; }
	void Put (float x) { m_p[m_nPos] = x; if (++m_nPos >= m_nLen) m_nPos = 0; }
	float Get (float fDelay) const		// fDelay samples before the last Put (0 = the last sample written)
	{
		if (fDelay < 0.0f) fDelay = 0.0f;
		if (fDelay > (float) (m_nLen - 3)) fDelay = (float) (m_nLen - 3);
		const unsigned n = (unsigned) fDelay;
		const float f = fDelay - (float) n;
		const unsigned i0 = (m_nPos + m_nLen - 1 - n) % m_nLen, i1 = (m_nPos + m_nLen - 2 - n) % m_nLen;
		return m_p[i0] + f * (m_p[i1] - m_p[i0]);
	}
private:
	float *m_p;
	unsigned m_nLen, m_nPos;
};

class CBase : public CFx
{
public:
	CBase (float fRate) : m_fRate (fRate) {}
protected:
	// what to do at the end of the block: if the signal was mono and the effect stays mono, L is copied into R
	static void CopyLR (float *pL, float *pR, unsigned n) { for (unsigned j = 0; j < n; j++) pR[j] = pL[j]; }
	float m_fRate;
};

// ---- 28 TS Overdrive (Ibanez Tube Screamer): mid hump (about 720 Hz), soft clipping, low-pass tone ------------------------
class CTsDrive : public CBase
{
public:
	CTsDrive (float r) : CBase (r) { for (unsigned c = 0; c < 2; c++) m_dc[c] = m_lp[c] = m_hp[c] = m_tn[c] = m_lo[c] = 0.0f; }
	unsigned Type (void) const { return FxTsDrive; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float drive = Clampf (pP[0], 0.0f, 100.0f) * 0.01f;
		const float G = 1.0f + 45.0f * drive * drive;			// gain on the mids
		const float aTone = LpA (700.0f + Clampf (pP[1], 0.0f, 100.0f) * 0.01f * 5500.0f, m_fRate);
		const float aHi = LpA (3500.0f, m_fRate), aMid = LpA (500.0f, m_fRate), aDc = LpA (30.0f, m_fRate);
		const float fOut = DbToLin (Clampf (pP[2], -24.0f, 12.0f)) * 0.7f;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				float x = p[j];
				m_dc[c] += aDc * (x - m_dc[c]);
				x -= m_dc[c];
				m_lp[c] += aHi * (x - m_lp[c]);			// 3.5 kHz low-pass
				m_lo[c] += aMid * (m_lp[c] - m_lo[c]);
				const float band = m_lp[c] - m_lo[c];		// mids: above 500 Hz, below 3.5 kHz
				const float u = x + (G - 1.0f) * band;
				const float y = Tanhf (u * 0.9f);
				m_tn[c] += aTone * (y - m_tn[c]);
				p[j] = m_tn[c] * fOut * 1.3f;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_dc[2], m_lp[2], m_hp[2], m_tn[2], m_lo[2];
};

// ---- 32 Tremolo (Fender Vibrolux): amplitude modulation, sine, triangle or soft square wave --------------------------
class CTremolo : public CBase
{
public:
	CTremolo (float r) : CBase (r), m_fPh (0.0f) {}
	unsigned Type (void) const { return FxTremolo; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float rate = Clampf (pP[0], 0.1f, 20.0f), depth = Clampf (pP[1], 0.0f, 100.0f) * 0.01f;
		const unsigned shape = (unsigned) Clampf (pP[2], 0.0f, 2.0f);
		const float dPh = rate / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			float l = shape == 0 ? Sin1 (m_fPh) : (shape == 1 ? Tri1 (m_fPh) : Tanhf (Sin1 (m_fPh) * 4.0f));	// -1..1
			const float g = 1.0f - depth * (0.5f - 0.5f * l);
			pL[j] *= g;
			if (!*pMono) pR[j] *= g;
			m_fPh = Wrap1 (m_fPh + dPh);
		}
		if (*pMono) CopyLR (pL, pR, n);
	}
private:
	float m_fPh;
};

// ---- 33 Harmonic Tremolo (Fender Brownface): bass and treble modulated in opposite phase --------------------------------------------
class CHarmTrem : public CBase
{
public:
	CHarmTrem (float r) : CBase (r), m_fPh (0.0f) { m_lp[0] = m_lp[1] = 0.0f; }
	unsigned Type (void) const { return FxHarmTrem; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float rate = Clampf (pP[0], 0.1f, 15.0f), depth = Clampf (pP[1], 0.0f, 100.0f) * 0.01f;
		const float a = LpA (Clampf (pP[2], 200.0f, 2000.0f), m_fRate);
		const unsigned nCh = *pMono ? 1 : 2;
		const float dPh = rate / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			const float l = Sin1 (m_fPh);
			const float gLo = 1.0f - depth * (0.5f - 0.5f * l), gHi = 1.0f - depth * (0.5f + 0.5f * l);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				const float x = p[j];
				m_lp[c] += a * (x - m_lp[c]);
				p[j] = m_lp[c] * gLo + (x - m_lp[c]) * gHi;
			}
			m_fPh = Wrap1 (m_fPh + dPh);
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fPh, m_lp[2];
};

// ---- 34 Auto-Wah (Mu-Tron III): band-pass filter driven by the signal's envelope ---------------------------------------------
class CAutoWah : public CBase
{
public:
	CAutoWah (float r) : CBase (r) { for (unsigned c = 0; c < 2; c++) m_env[c] = m_lo[c] = m_bp[c] = 0.0f; }
	unsigned Type (void) const { return FxAutoWah; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float sens = Clampf (pP[0], 0.0f, 100.0f) * 0.01f * 20.0f;
		const float fLo = Clampf (pP[1], 150.0f, 1000.0f), fHi = Clampf (pP[2], 800.0f, 4500.0f);
		const float Q = Clampf (pP[3], 1.0f, 12.0f);
		const boolean bDown = pP[4] > 0.5f;
		const float mix = Clampf (pP[5], 0.0f, 100.0f) * 0.01f;
		const float aA = LpA (200.0f, m_fRate), aR = LpA (8.0f, m_fRate);
		const float q1 = 1.0f / Q;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				const float x = p[j];
				const float ax = Absf (x);
				m_env[c] += (ax > m_env[c] ? aA : aR) * (ax - m_env[c]);
				float e = Clampf (m_env[c] * sens, 0.0f, 1.0f);
				if (bDown) e = 1.0f - e;
				const float fc = fLo + (fHi - fLo) * e;
				const float f1 = Clampf (6.2831853f * fc / m_fRate, 0.01f, 0.9f);
				m_lo[c] += f1 * m_bp[c];						// state-variable filter (Chamberlin)
				const float hp = x - m_lo[c] - q1 * m_bp[c];
				m_bp[c] += f1 * hp;
				p[j] = x * (1.0f - mix) + m_bp[c] * mix * Q * 0.5f;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_env[2], m_lo[2], m_bp[2];
};

// ---- 37 Noise Gate (ISP Decimator / Boss NS-2) ------------------------------------------------------------------------------------------
class CGate : public CBase
{
public:
	CGate (float r) : CBase (r), m_fEnv (0.0f), m_fGain (0.0f), m_nHold (0), m_bOpen (FALSE) {}
	unsigned Type (void) const { return FxGate; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float thrOpen = DbToLin (Clampf (pP[0], -90.0f, -10.0f)), thrClose = thrOpen * 0.5f;	// 6 dB hysteresis
		const float aAtt = LpA (1000.0f / Clampf (pP[1], 0.1f, 50.0f) / 6.2831853f * 3.0f, m_fRate);
		const float aRel = LpA (1000.0f / Clampf (pP[2], 5.0f, 1000.0f) / 6.2831853f * 3.0f, m_fRate);
		const float range = DbToLin (-Clampf (pP[3], 0.0f, 90.0f));
		const unsigned nHold = (unsigned) (Clampf (pP[4], 0.0f, 500.0f) * 0.001f * m_fRate);
		for (unsigned j = 0; j < n; j++)
		{
			float ax = Absf (pL[j]);
			if (!*pMono && Absf (pR[j]) > ax) ax = Absf (pR[j]);
			m_fEnv = ax > m_fEnv ? ax : m_fEnv + 0.0005f * (ax - m_fEnv);	// peak with slow decay
			if (m_fEnv > thrOpen) { m_bOpen = TRUE; m_nHold = nHold; }
			else if (m_fEnv < thrClose && m_bOpen)
			{
				if (m_nHold > 0) m_nHold--; else m_bOpen = FALSE;
			}
			const float target = m_bOpen ? 1.0f : range;
			m_fGain += (target > m_fGain ? aAtt : aRel) * (target - m_fGain);
			pL[j] *= m_fGain;
			if (!*pMono) pR[j] *= m_fGain;
		}
		if (*pMono) CopyLR (pL, pR, n);
	}
private:
	float m_fEnv, m_fGain;
	unsigned m_nHold;
	boolean m_bOpen;
};

// ---- 40 Rotary Speaker (Leslie): horn (fast doppler and tremolo) and drum (slow tremolo), with acceleration ------------------------
class CRotary : public CBase
{
public:
	CRotary (float r) : CBase (r), m_fHorn (0.8f), m_fDrum (0.66f), m_fPhH (0.0f), m_fPhD (0.0f)
	{
		m_h[0] = new CLine (512); m_h[1] = new CLine (512); m_fLp[0] = m_fLp[1] = 0.0f;
	}
	~CRotary (void) { delete m_h[0]; delete m_h[1]; }
	unsigned Type (void) const { return FxRotary; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const boolean bFast = pP[0] > 0.5f;
		const float depth = Clampf (pP[1], 0.0f, 100.0f) * 0.01f, mix = Clampf (pP[2], 0.0f, 100.0f) * 0.01f;
		const float tH = bFast ? 6.7f : 0.8f, tD = bFast ? 5.7f : 0.66f;
		const float aX = LpA (800.0f, m_fRate);
		if (*pMono) { CopyLR (pL, pR, n); *pMono = 0; }
		const float kH = n / m_fRate * 1.2f, kD = n / m_fRate * 0.35f;	// the horn accelerates in about one second, the drum in three
		m_fHorn += (tH - m_fHorn) * Clampf (kH, 0.0f, 1.0f);
		m_fDrum += (tD - m_fDrum) * Clampf (kD, 0.0f, 1.0f);
		const float dH = m_fHorn / m_fRate, dD = m_fDrum / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			const float x = 0.5f * (pL[j] + pR[j]);
			m_fLp[0] += aX * (x - m_fLp[0]);
			const float lo = m_fLp[0], hi = x - lo;
			m_h[0]->Put (hi);
			float oL = 0.0f, oR = 0.0f;
			for (unsigned c = 0; c < 2; c++)
			{
				const float sH = Sin1 (Wrap1 (m_fPhH + (c ? 0.25f : 0.0f))), sD = Sin1 (Wrap1 (m_fPhD + (c ? 0.5f : 0.0f)));
				const float horn = m_h[0]->Get (24.0f + 14.0f * depth * sH) * (1.0f - 0.45f * depth * (0.5f - 0.5f * sH));
				const float drum = lo * (1.0f - 0.35f * depth * (0.5f - 0.5f * sD));
				(c ? oR : oL) = horn + drum;
			}
			m_fPhH = Wrap1 (m_fPhH + dH);
			m_fPhD = Wrap1 (m_fPhD + dD);
			pL[j] = pL[j] * (1.0f - mix) + oL * mix;
			pR[j] = pR[j] * (1.0f - mix) + oR * mix;
		}
	}
private:
	float m_fHorn, m_fDrum, m_fPhH, m_fPhD, m_fLp[2];
	CLine *m_h[2];
};

// ---- 41 Tape Echo (Echoplex EP-3 / Roland RE-201): delay with wow and flutter, tone and saturation in the feedback -----------------------
class CTapeEcho : public CBase
{
public:
	CTapeEcho (float r) : CBase (r), m_fPh1 (0.0f), m_fPh2 (0.0f)
	{
		m_nLen = (unsigned) (r * 1.05f);
		m_p[0] = new CLine (m_nLen); m_p[1] = new CLine (m_nLen); m_fLp[0] = m_fLp[1] = 0.0f; m_fDc[0] = m_fDc[1] = 0.0f; m_fT = 0.0f;
	}
	~CTapeEcho (void) { delete m_p[0]; delete m_p[1]; }
	unsigned Type (void) const { return FxTapeEcho; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float tMs = FxSyncMs (Clampf (pP[0], 20.0f, 1000.0f), pP[6], 20.0f, 1000.0f), fb = Clampf (pP[1], 0.0f, 100.0f) * 0.0095f;
		const float aTone = LpA (1500.0f + Clampf (pP[2], 0.0f, 100.0f) * 0.01f * 7500.0f, m_fRate);
		const float wow = Clampf (pP[3], 0.0f, 100.0f) * 0.01f, sat = Clampf (pP[4], 0.0f, 100.0f) * 0.01f * 3.0f + 0.3f;
		const float mix = Clampf (pP[5], 0.0f, 100.0f) * 0.01f;
		const float aDc = LpA (30.0f, m_fRate);
		const unsigned nCh = *pMono ? 1 : 2;
		const float tSamp = tMs * 0.001f * m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			m_fPh1 = Wrap1 (m_fPh1 + 0.6f / m_fRate);
			m_fPh2 = Wrap1 (m_fPh2 + 6.3f / m_fRate);
			m_fT += 0.001f * (tSamp - m_fT);				// the time changes with a glissando, like the tape
			const float d = m_fT * (1.0f + 0.004f * wow * Sin1 (m_fPh1)) + 1.5f * wow * Sin1 (m_fPh2);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				const float x = p[j];
				float y = m_p[c]->Get (d - 1.0f);		// Get comes before Put: one more sample
				m_fLp[c] += aTone * (y - m_fLp[c]);
				const float s = Tanhf (m_fLp[c] * sat) / sat;
				m_fDc[c] += aDc * (s - m_fDc[c]);
				m_p[c]->Put (x + (s - m_fDc[c]) * fb);
				p[j] = x + (y * mix);
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fPh1, m_fPh2, m_fT, m_fLp[2], m_fDc[2];
	unsigned m_nLen;
	CLine *m_p[2];
};

// ---- 43 Clean Booster (Xotic EP Booster / MXR Micro Amp): clean gain with a high-pass filter and a bit of warmth --------------------
class CBooster : public CBase
{
public:
	CBooster (float r) : CBase (r) { for (unsigned c = 0; c < 2; c++) m_hp[c] = m_lp[c] = 0.0f; }
	unsigned Type (void) const { return FxBooster; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float g = DbToLin (Clampf (pP[0], 0.0f, 30.0f));
		const float aHp = LpA (Clampf (pP[1], 20.0f, 500.0f), m_fRate);
		const float warm = Clampf (pP[2], 0.0f, 100.0f) * 0.01f;
		const float aHi = LpA (1800.0f, m_fRate), brt = Clampf (pP[3], 0.0f, 100.0f) * 0.01f * 0.9f;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				float x = p[j];
				m_hp[c] += aHp * (x - m_hp[c]);
				x -= m_hp[c];
				m_lp[c] += aHi * (x - m_lp[c]);
				x += brt * (x - m_lp[c]);				// presence: a little more treble
				float y = x * g;
				if (warm > 0.0f)
				{
					const float s = Tanhf (y);			// soft clipping dosed with the Warmth knob
					y = y + warm * (s - y);
				}
				p[j] = y;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_hp[2], m_lp[2];
};

// ---- 45 Volume Swell (Boss SG-1 Slow Gear): slow attack on every note, violin effect ---------------------------------------------
class CSwell : public CBase
{
public:
	CSwell (float r) : CBase (r), m_fEnv (0.0f), m_fGain (0.0f), m_bOpen (FALSE) {}
	unsigned Type (void) const { return FxSwell; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float thr = DbToLin (Clampf (pP[0], -80.0f, -10.0f));
		const float aAtt = LpA (1000.0f / Clampf (pP[1], 20.0f, 3000.0f) / 6.2831853f * 3.0f, m_fRate);
		const float aRel = LpA (1000.0f / Clampf (pP[2], 20.0f, 3000.0f) / 6.2831853f * 3.0f, m_fRate);
		for (unsigned j = 0; j < n; j++)
		{
			float ax = Absf (pL[j]);
			if (!*pMono && Absf (pR[j]) > ax) ax = Absf (pR[j]);
			m_fEnv = ax > m_fEnv ? ax : m_fEnv + 0.001f * (ax - m_fEnv);
			if (m_fEnv > thr) m_bOpen = TRUE; else if (m_fEnv < thr * 0.4f) m_bOpen = FALSE;
			const float target = m_bOpen ? 1.0f : 0.0f;
			m_fGain += (target > m_fGain ? aAtt : aRel) * (target - m_fGain);
			pL[j] *= m_fGain;
			if (!*pMono) pR[j] *= m_fGain;
		}
		if (*pMono) CopyLR (pL, pR, n);
	}
private:
	float m_fEnv, m_fGain;
	boolean m_bOpen;
};

// ---- 46 Stereo Widener: Mid/Side width and Haas effect (short delay of one channel, also from a mono input) -----------------------
class CWidener : public CBase
{
public:
	CWidener (float r) : CBase (r) { m_h = new CLine (2048); }
	~CWidener (void) { delete m_h; }
	unsigned Type (void) const { return FxWidener; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float width = Clampf (pP[0], 0.0f, 200.0f) * 0.01f;
		const float haas = Clampf (pP[1], 0.0f, 30.0f) * 0.001f * m_fRate;
		if (*pMono)
		{
			if (haas < 1.0f) return;			// mono input and no Haas: nothing to do
			CopyLR (pL, pR, n);
			*pMono = 0;
		}
		for (unsigned j = 0; j < n; j++)
		{
			const float m = 0.5f * (pL[j] + pR[j]), s = 0.5f * (pL[j] - pR[j]) * width;
			float l = m + s, r = m - s;
			if (haas >= 1.0f)
			{
				m_h->Put (r);
				r = m_h->Get (haas);
			}
			pL[j] = l;
			pR[j] = r;
		}
	}
private:
	CLine *m_h;
};

// ---- 47 Vibrato (Boss VB-2): pitch modulation (modulated delay, all effect) -------------------------------------------
class CVibrato : public CBase
{
public:
	CVibrato (float r) : CBase (r), m_fPh (0.0f) { m_p[0] = new CLine (1024); m_p[1] = new CLine (1024); }
	~CVibrato (void) { delete m_p[0]; delete m_p[1]; }
	unsigned Type (void) const { return FxVibrato; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float rate = Clampf (pP[0], 0.1f, 12.0f), depth = Clampf (pP[1], 0.0f, 100.0f) * 0.01f;
		const boolean bTri = pP[2] > 0.5f;
		const unsigned nCh = *pMono ? 1 : 2;
		const float dMax = 0.0035f * m_fRate, dPh = rate / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			const float l = bTri ? Tri1 (m_fPh) : Sin1 (m_fPh);
			const float d = dMax * (1.0f + depth * l) + 2.0f;
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				m_p[c]->Put (p[j]);
				p[j] = m_p[c]->Get (d);
			}
			m_fPh = Wrap1 (m_fPh + dPh);
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fPh;
	CLine *m_p[2];
};

}	// namespace

CFx *NewExtraFx (unsigned nType, float fRate)
{
	switch (nType)
	{
	case FxTsDrive:		return new CTsDrive (fRate);
	case FxTremolo:		return new CTremolo (fRate);
	case FxHarmTrem:	return new CHarmTrem (fRate);
	case FxAutoWah:		return new CAutoWah (fRate);
	case FxGate:		return new CGate (fRate);
	case FxRotary:		return new CRotary (fRate);
	case FxTapeEcho:	return new CTapeEcho (fRate);
	case FxBooster:		return new CBooster (fRate);
	case FxSwell:		return new CSwell (fRate);
	case FxWidener:		return new CWidener (fRate);
	case FxVibrato:		return new CVibrato (fRate);
	default:		return 0;
	}
}
