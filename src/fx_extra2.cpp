//
// fx_extra2.cpp - 30 new "ToTest" effects (types 48..77), inspired by classic pedals, racks and plugins and by the sounds of famous guitarists.
// Same conventions as fx_extra.cpp: no libm (only dsp_util.h), state inside the object, delay buffers allocated separately,
// Process (pL, pR) in place, *pMono != 0 = L and R identical; the effects that make the output stereo set *pMono = 0.
// Compiled with -Os (Makefile): it is not the heaviest callback code and it takes less of the kernel's 2 MB limit.
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
inline float Sin1 (float p)		// sin(2 pi p) per p in [0, 1)
{
	float x = 3.14159265f - p * 6.2831853f;
	const float y = 1.27323954f * x - 0.40528473f * x * Absf (x);
	return 0.225f * (y * Absf (y) - y) + y;
}
inline float Wrap1 (float p) { return p >= 1.0f ? p - 1.0f : (p < 0.0f ? p + 1.0f : p); }
inline float LpA (float fHz, float fRate) { return 1.0f - ExpF (-6.2831853f * Clampf (fHz, 0.05f, 0.45f * fRate) / fRate); }
// approximate natural logarithm (error below 1e-4): the compressors need it to work in dB
inline float LnF (float x)
{
	if (x < 1e-12f) x = 1e-12f;
	union { float f; u32 u; } v;
	v.f = x;
	const int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
	const float m = v.f;
	return (float) e * 0.69314718f + (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}
inline float ToDb (float x) { return 8.685889638f * LnF (x); }

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
	float At (unsigned nBack) const { return m_p[(m_nPos + m_nLen - 1 - (nBack % m_nLen)) % m_nLen]; }
	unsigned Len (void) const { return m_nLen; }
private:
	float *m_p;
	unsigned m_nLen, m_nPos;
};

class CBase : public CFx
{
public:
	CBase (float fRate) : m_fRate (fRate) {}
protected:
	static void CopyLR (float *pL, float *pR, unsigned n) { for (unsigned j = 0; j < n; j++) pR[j] = pL[j]; }
	float m_fRate;
};

// RBJ biquad in transposed form, one channel
struct TBq
{
	float b0, b1, b2, a1, a2, z1, z2;
	TBq (void) : b0 (1), b1 (0), b2 (0), a1 (0), a2 (0), z1 (0), z2 (0) {}
	void Set (float B0, float B1, float B2, float A0, float A1, float A2)
	{
		const float r = 1.0f / A0;
		b0 = B0 * r; b1 = B1 * r; b2 = B2 * r; a1 = A1 * r; a2 = A2 * r;
	}
	void Peak (float f, float g, float q, float fs)
	{
		const float A = DbToLin (g * 0.5f), w0 = 6.2831853f * Clampf (f, 20.0f, 0.45f * fs) / fs, cw = DspCos (w0), al = DspSin (w0) / (2.0f * q);
		Set (1.0f + al * A, -2.0f * cw, 1.0f - al * A, 1.0f + al / A, -2.0f * cw, 1.0f - al / A);
	}
	float Run (float x)
	{
		const float y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
};

// ---- 49 Blues Driver (Boss BD-2): soft asymmetric clipping, very dynamic ------------------------------------------------------------
class CBluesDrv : public CBase
{
public:
	CBluesDrv (float r) : CBase (r) { for (unsigned c = 0; c < 2; c++) m_dc[c] = m_lp[c] = m_hp[c] = 0.0f; }
	unsigned Type (void) const { return FxBluesDrv; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float g = Clampf (pP[0], 0.0f, 100.0f) * 0.01f;
		const float G = 1.0f + 30.0f * g * g;
		const float aDc = LpA (35.0f, m_fRate), aT = LpA (1800.0f + Clampf (pP[1], 0.0f, 100.0f) * 0.01f * 6500.0f, m_fRate), aHp = LpA (400.0f, m_fRate);
		const float fOut = DbToLin (Clampf (pP[2], -24.0f, 12.0f)) * 0.8f;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				float x = p[j];
				m_dc[c] += aDc * (x - m_dc[c]);
				x -= m_dc[c];
				m_hp[c] += aHp * (x - m_hp[c]);
				const float v = (x + 0.4f * (x - m_hp[c])) * G;
				const float y = v >= 0.0f ? Tanhf (v) : 0.8f * Tanhf (v * 1.2f);
				m_lp[c] += aT * (y - m_lp[c]);
				p[j] = m_lp[c] * fOut;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_dc[2], m_lp[2], m_hp[2];
};

// ---- 54 Fuzz (Maestro FZ-1, 1962): very hard, asymmetric clipping with a tone filter ----------------------------------------------------
class CFz1 : public CBase
{
public:
	CFz1 (float r) : CBase (r) { for (unsigned c = 0; c < 2; c++) m_dc[c] = m_lp[c] = m_lo[c] = 0.0f; }
	unsigned Type (void) const { return FxFz1; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float f = Clampf (pP[0], 0.0f, 100.0f) * 0.01f, tone = Clampf (pP[1], 0.0f, 100.0f) * 0.01f;
		const float G = 1.0f + 80.0f * f * f;
		const float aDc = LpA (45.0f, m_fRate), aTn = LpA (900.0f + tone * 5500.0f, m_fRate), aLo = LpA (160.0f, m_fRate);
		const float fOut = DbToLin (Clampf (pP[2], -24.0f, 12.0f)) * 0.6f;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				float x = p[j];
				m_lo[c] += aLo * (x - m_lo[c]);
				x -= 0.6f * m_lo[c];
				const float u = x * G;
				float y = u >= 0.0f ? Tanhf (u * 3.0f) * 0.5f : Tanhf (u * 2.0f) * 0.75f;
				m_dc[c] += aDc * (y - m_dc[c]);
				y -= m_dc[c];
				m_lp[c] += aTn * (y - m_lp[c]);
				p[j] = m_lp[c] * fOut * 1.6f;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_dc[2], m_lp[2], m_lo[2];
};

// first-order all-pass with coefficient a, one channel
struct TAp1
{
	float z;
	TAp1 (void) : z (0) {}
	float Run (float x, float a) { const float o = a * x + z; z = x - a * o; return o; }
};
inline float ApCoef (float fc, float fs) { const float w = 3.14159265f * Clampf (fc, 20.0f, 0.4f * fs) / fs; return (w - 1.0f) / (w + 1.0f); }

// ---- 59 Small Stone (Electro-Harmonix): six-stage phaser, the Color button turns the feedback on ---------------------------------------------
class CStone : public CBase
{
public:
	CStone (float r) : CBase (r), m_fPh (0.0f) { m_fb[0] = m_fb[1] = 0.0f; }
	unsigned Type (void) const { return FxStone; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float rate = Clampf (pP[0], 0.05f, 8.0f);
		const float fb = pP[1] > 0.5f ? 0.65f : 0.0f;
		const unsigned nCh = *pMono ? 1 : 2;
		const float dPh = rate / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			const float fc = 120.0f * ExpF (2.4f * (0.5f + 0.5f * Sin1 (m_fPh)));
			const float a = ApCoef (fc, m_fRate);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				const float x = p[j];
				float y = x + fb * m_fb[c];
				for (unsigned s = 0; s < 6; s++) y = m_ap[c][s].Run (y, a);
				m_fb[c] = Clampf (y, -4.0f, 4.0f);
				p[j] = 0.5f * (x + y);
			}
			m_fPh = Wrap1 (m_fPh + dPh);
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fPh, m_fb[2];
	TAp1 m_ap[2][6];
};

// ---- 62 Auto-Pan: the LFO moves the sound between left and right (constant-power law); stereo output --------------------------------------
class CAutoPan : public CBase
{
public:
	CAutoPan (float r) : CBase (r), m_fPh (0.0f), m_fSq (0.0f) {}
	unsigned Type (void) const { return FxAutoPan; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float rate = Clampf (pP[0], 0.1f, 12.0f), depth = Clampf (pP[1], 0.0f, 100.0f) * 0.01f;
		const unsigned wave = (unsigned) (Clampf (pP[2], 0.0f, 2.0f) + 0.5f);
		const float aSq = LpA (60.0f, m_fRate);
		if (*pMono) { CopyLR (pL, pR, n); *pMono = 0; }
		const float dPh = rate / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			float l;
			if (wave == 0) l = Sin1 (m_fPh);
			else if (wave == 1) l = m_fPh < 0.5f ? 4.0f * m_fPh - 1.0f : 3.0f - 4.0f * m_fPh;
			else { m_fSq += aSq * ((m_fPh < 0.5f ? 1.0f : -1.0f) - m_fSq); l = m_fSq; }
			const float pan = 0.5f + 0.5f * depth * l;			// 0 = fully left, 1 = fully right
			const float th = pan * 1.57079633f;
			pL[j] *= DspCos (th) * 1.4142f;
			pR[j] *= DspSin (th) * 1.4142f;
			m_fPh = Wrap1 (m_fPh + dPh);
		}
	}
private:
	float m_fPh, m_fSq;
};

// ---- 63 Slicer (Trem Trigger / Boss SL-2): 8-step patterns that open and close the volume in time (Eighths = open and closed on every eighth; before they were all open, that is a bypass) ----------------------------------------------
class CSlicer : public CBase
{
public:
	CSlicer (float r) : CBase (r), m_fPh (0.0f) { m_fG[0] = m_fG[1] = 1.0f; }
	unsigned Type (void) const { return FxSlicer; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		static const u8 pat[4][8] = {{1, 0, 1, 0, 1, 0, 1, 0}, {1, 0, 1, 1, 0, 1, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}, {1, 1, 0, 0, 1, 0, 0, 0}};
		const float rate = Clampf (pP[0], 0.25f, 8.0f);
		unsigned p = (unsigned) (Clampf (pP[1], 0.0f, 3.0f) + 0.5f);
		if (p > 3) p = 3;
		const float depth = Clampf (pP[2], 0.0f, 100.0f) * 0.01f, a = LpA (1000.0f / Clampf (pP[3], 1.0f, 50.0f) / 6.2831853f * 2.0f, m_fRate);
		const unsigned nCh = *pMono ? 1 : 2;
		const float dPh = rate / m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			unsigned step = (unsigned) (m_fPh * 8.0f);
			if (step > 7) step = 7;
			const float target = 1.0f - depth * (pat[p][step] ? 0.0f : 1.0f);
			for (unsigned c = 0; c < nCh; c++)
			{
				m_fG[c] += a * (target - m_fG[c]);
				(c ? pR : pL)[j] *= m_fG[c];
			}
			m_fPh = Wrap1 (m_fPh + dPh);
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fPh, m_fG[2];
};

// state-variable filter (Chamberlin), one channel: f = 2 sin(pi fc/fs), d = 1/Q
struct TSvf
{
	float lp, bp;
	TSvf (void) : lp (0), bp (0) {}
	void Run (float x, float f, float d, float *pLp, float *pBp, float *pHp)
	{
		const float hp = x - lp - d * bp;
		bp += f * hp;
		lp += f * bp;
		bp = Clampf (bp, -8.0f, 8.0f);
		lp = Clampf (lp, -8.0f, 8.0f);
		*pLp = lp; *pBp = bp; *pHp = hp;
	}
};
inline float SvfF (float fc, float fs) { return 2.0f * DspSin (3.14159265f * Clampf (fc, 20.0f, 0.2f * fs) / fs); }

// ---- 65 Woman Tone (Eric Clapton, Cream): neck pickup with the tone fully closed: dark, round low-pass with a resonance peak -----
class CWoman : public CBase
{
public:
	CWoman (float r) : CBase (r) {}
	unsigned Type (void) const { return FxWoman; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float tone = Clampf (pP[0], 0.0f, 100.0f) * 0.01f, body = Clampf (pP[1], 0.0f, 100.0f) * 0.01f;
		const float f = SvfF (250.0f * ExpF (1.79f * tone), m_fRate), d = 1.0f / (0.8f + 3.0f * body);
		const float fOut = DbToLin (Clampf (pP[2], -24.0f, 12.0f));
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				float lp, bp, hp;
				m_s[c].Run (p[j], f, d, &lp, &bp, &hp);
				p[j] = lp * fOut;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	TSvf m_s[2];
};

// ---- 67 Six-band Graphic EQ (simplified MXR 10-band): 100, 200, 400, 800, 1600, 3200 Hz, +-12 dB --------------------------------------------
class CGeq : public CBase
{
public:
	CGeq (float r) : CBase (r), m_bValid (FALSE) { for (unsigned i = 0; i < 7; i++) m_fKey[i] = 0.0f; for (unsigned s = 0; s < 6; s++) m_On[s] = FALSE; }
	unsigned Type (void) const { return FxGeq; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		Setup (pP);
		const float fOut = DbToLin (Clampf (pP[6], -24.0f, 12.0f));
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			for (unsigned j = 0; j < n; j++)
			{
				float x = p[j];
				for (unsigned s = 0; s < 6; s++) if (m_On[s]) x = m_B[c][s].Run (x);
				p[j] = x * fOut;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	void Setup (const float *pP)
	{
		boolean bSame = m_bValid;
		for (unsigned i = 0; i < 6; i++) if (pP[i] != m_fKey[i]) bSame = FALSE;
		if (bSame) return;
		static const float fc[6] = {100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f};
		for (unsigned s = 0; s < 6; s++)
		{
			m_fKey[s] = pP[s];
			const float g = Clampf (pP[s], -12.0f, 12.0f);
			m_On[s] = g > 0.05f || g < -0.05f;
			if (m_On[s]) for (unsigned c = 0; c < 2; c++) m_B[c][s].Peak (fc[s], g, 1.4f, m_fRate);
		}
		m_bValid = TRUE;
	}
	boolean m_bValid, m_On[6];
	float m_fKey[7];
	TBq m_B[2][6];
};

// level estimate in dB with a peak follower (attack and release as coefficients)
struct TEnv
{
	float e;
	TEnv (void) : e (0) {}
	float Run (float ax, float aA, float aR) { e += (ax > e ? aA : aR) * (ax - e); return e; }
};

// ---- 70 Limiter (brick-wall): the peak never exceeds the ceiling; it protects the downstream blocks and the amplifier -----------------------------
class CLimiter : public CBase
{
public:
	CLimiter (float r) : CBase (r) { m_fG = 1.0f; }
	unsigned Type (void) const { return FxLimiter; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float thr = DbToLin (Clampf (pP[0], -30.0f, 0.0f)), ceil_ = DbToLin (Clampf (pP[2], -12.0f, 0.0f));
		const float aA = LpA (3000.0f, m_fRate), aR = LpA (1000.0f / Clampf (pP[1], 20.0f, 1000.0f) / 6.2831853f * 4.0f, m_fRate);
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned j = 0; j < n; j++)
		{
			float ax = Absf (pL[j]);
			if (nCh > 1 && Absf (pR[j]) > ax) ax = Absf (pR[j]);
			const float gt = ax * m_fG > thr ? thr / (ax + 1e-9f) : 1.0f;
			m_fG += (gt < m_fG ? aA : aR) * (gt - m_fG);
			pL[j] = Clampf (pL[j] * m_fG, -ceil_, ceil_);
			if (nCh > 1) pR[j] = Clampf (pR[j] * m_fG, -ceil_, ceil_);
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fG;
};

// ---- 71 Analog Delay (Electro-Harmonix Memory Man, Boss DM-2): repeats that get darker as in BBD circuits, with chorus modulation -------
class CAnalog : public CBase
{
public:
	CAnalog (float r) : CBase (r), m_fPh (0.0f), m_fT (0.0f)
	{
		m_nLen = (unsigned) (r * 0.66f);
		m_p[0] = new CLine (m_nLen); m_p[1] = new CLine (m_nLen);
		for (unsigned c = 0; c < 2; c++) m_l1[c] = m_l2[c] = m_dc[c] = 0.0f;
	}
	~CAnalog (void) { delete m_p[0]; delete m_p[1]; }
	unsigned Type (void) const { return FxAnalog; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float tMs = FxSyncMs (Clampf (pP[0], 20.0f, 600.0f), pP[4], 20.0f, 600.0f), fb = Clampf (pP[1], 0.0f, 100.0f) * 0.0092f, mix = Clampf (pP[2], 0.0f, 100.0f) * 0.01f, mod = Clampf (pP[3], 0.0f, 100.0f) * 0.01f;
		const float aBbd = LpA (3200.0f, m_fRate) , aDc = LpA (30.0f, m_fRate);
		const float tSamp = tMs * 0.001f * m_fRate;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned j = 0; j < n; j++)
		{
			m_fPh = Wrap1 (m_fPh + 0.55f / m_fRate);
			m_fT += 0.0015f * (tSamp - m_fT);
			const float d = m_fT + 0.0018f * m_fRate * mod * Sin1 (m_fPh);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				const float x = p[j];
				float y = m_p[c]->Get (d - 1.0f);
				m_l1[c] += aBbd * (y - m_l1[c]);
				m_l2[c] += aBbd * (m_l1[c] - m_l2[c]);
				const float s = Tanhf (m_l2[c] * 1.1f) / 1.1f;
				m_dc[c] += aDc * (s - m_dc[c]);
				m_p[c]->Put (x + (s - m_dc[c]) * fb);
				p[j] = x + m_l2[c] * mix * 1.2f;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fPh, m_fT, m_l1[2], m_l2[2], m_dc[2];
	unsigned m_nLen;
	CLine *m_p[2];
};

// ---- 75 Ducking Delay (TC Electronic 2290): the repeats lower while you play and re-emerge in the pauses --------------------------------
class CDuck : public CBase
{
public:
	CDuck (float r) : CBase (r), m_fT (0.0f)
	{
		m_nLen = (unsigned) (r * 1.05f);
		m_p[0] = new CLine (m_nLen); m_p[1] = new CLine (m_nLen);
		for (unsigned c = 0; c < 2; c++) m_lp[c] = m_dc[c] = 0.0f;
		m_fEnv = 0.0f;
	}
	~CDuck (void) { delete m_p[0]; delete m_p[1]; }
	unsigned Type (void) const { return FxDuck; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float tMs = FxSyncMs (Clampf (pP[0], 20.0f, 1000.0f), pP[4], 20.0f, 1000.0f), fb = Clampf (pP[1], 0.0f, 90.0f) * 0.0095f, duck = Clampf (pP[2], 0.0f, 100.0f) * 0.01f, mix = Clampf (pP[3], 0.0f, 100.0f) * 0.01f;
		const float aLp = LpA (6000.0f, m_fRate), aDc = LpA (30.0f, m_fRate), aA = LpA (120.0f, m_fRate), aR = LpA (4.0f, m_fRate);
		const float tSamp = tMs * 0.001f * m_fRate;
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned j = 0; j < n; j++)
		{
			m_fT += 0.002f * (tSamp - m_fT);
			float ax = Absf (pL[j]);
			if (nCh > 1 && Absf (pR[j]) > ax) ax = Absf (pR[j]);
			m_fEnv += (ax > m_fEnv ? aA : aR) * (ax - m_fEnv);
			const float wg = 1.0f - duck * Clampf (m_fEnv * 10.0f, 0.0f, 1.0f);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				const float x = p[j];
				const float y = m_p[c]->Get (m_fT - 1.0f);
				m_lp[c] += aLp * (y - m_lp[c]);
				m_dc[c] += aDc * (m_lp[c] - m_dc[c]);
				m_p[c]->Put (x + (m_lp[c] - m_dc[c]) * fb);
				p[j] = x + m_lp[c] * mix * wg;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fT, m_lp[2], m_dc[2], m_fEnv;
	unsigned m_nLen;
	CLine *m_p[2];
};

// ---- 77 Sustainer (Boss SUS, Fernandes Sustainer): the gain follows the envelope and keeps the level high while the note decays ----------------------
class CSustain : public CBase
{
public:
	CSustain (float r) : CBase (r) { m_fEnv = 0.0f; m_fG = 1.0f; m_fLp[0] = m_fLp[1] = 0.0f; }
	unsigned Type (void) const { return FxSustain; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const float s = Clampf (pP[0], 0.0f, 100.0f) * 0.01f;
		const float gMax = 1.0f + 80.0f * s * s, fOut = DbToLin (Clampf (pP[1], -24.0f, 12.0f));
		const float aT = LpA (2500.0f + Clampf (pP[2], 0.0f, 100.0f) * 0.01f * 6000.0f, m_fRate);
		const float aA = LpA (50.0f, m_fRate), aR = LpA (0.55f, m_fRate), aG = LpA (30.0f, m_fRate);
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned j = 0; j < n; j++)
		{
			float ax = Absf (pL[j]);
			if (nCh > 1 && Absf (pR[j]) > ax) ax = Absf (pR[j]);
			m_fEnv += (ax > m_fEnv ? aA : aR) * (ax - m_fEnv);
			const float target = m_fEnv > 1e-4f ? Clampf (0.3f / m_fEnv, 1.0f, gMax) : 1.0f;
			m_fG += aG * (target - m_fG);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				const float y = Tanhf (p[j] * m_fG * 0.8f);
				m_fLp[c] += aT * (y - m_fLp[c]);
				p[j] = m_fLp[c] * fOut;
			}
		}
		if (nCh == 1) CopyLR (pL, pR, n);
	}
private:
	float m_fEnv, m_fG, m_fLp[2];
};

// ---- 78 Selector (A/B/C): it does not change the signal; the pedalboard plan lets only the chosen output link through (see CBoard::NetBuild) ----
class CSelector : public CBase
{
public:
	CSelector (float r) : CBase (r) {}
	unsigned Type (void) const { return FxSelector; }
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP) { (void) pL; (void) pR; (void) n; (void) pMono; (void) pP; }
};

// ---- 85 SecondHand: a second guitarist made from the same signal (an overdub). A hand is the input delayed by a few ms, with a different timing, pitch wander, touch, tone and pick noise
//      at every new note. The timing changes at the attack with a short crossfade between two delays (no pitch glide); the pitch wanders slowly and irregularly (modulated delay, sharp and flat
//      around the original: a fixed detune would need processing synchronised to the pitch and is not done). Outputs: L = clean | R = hand 1, or L = hand 1 | R = hand 2. -----------------------
class CSecondHand : public CBase
{
public:
	CSecondHand (float r) : CBase (r)
	{
		m_in = new CLine (8192);
		m_nTake = 0xFFFFFFFFu;
		m_nHop = 0; m_nHold = 0; m_fE = 0.0f; m_fEF = 0.0f; m_fEP = 0.0f; m_bArmed = true; m_bInit = false;
		for (unsigned h = 0; h < 2; h++) Reset (h, 1u + h);
	}
	~CSecondHand (void) { delete m_in; }
	unsigned Type (void) const { return FxSecondHand; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const unsigned nMode = pP[0] > 0.5f ? 1 : 0, nHands = nMode ? 2 : 1;
		const float fLevel = DbToLin (Clampf (pP[1], -24.0f, 12.0f));
		const float fThr = DbToLin (Clampf (pP[2], -70.0f, -30.0f));
		const float hop = 32.0f / m_fRate;
		const unsigned nTake = (unsigned) Clampf (pP[3], 0.0f, 99.0f);
		if (nTake != m_nTake)
		{
			m_nTake = nTake;
			for (unsigned h = 0; h < 2; h++) Reset (h, 2654435761u * (nTake + 1u) + 40503u * (h + 1u));
		}
		const float fs = m_fRate;
		const float off[2] = {Clampf (pP[4], 0.0f, 60.0f), Clampf (pP[6], 0.0f, 60.0f)};
		const float loose[2] = {Clampf (pP[5], 0.0f, 40.0f), Clampf (pP[7], 0.0f, 40.0f)};
		const float drift = Clampf (pP[8], 0.0f, 30.0f), drate = Clampf (pP[9], 0.1f, 3.0f);
		const float dyn = Clampf (pP[10], 0.0f, 6.0f), tone = Clampf (pP[11], -100.0f, 100.0f), tvar = Clampf (pP[12], 0.0f, 100.0f), pick = Clampf (pP[13], 0.0f, 100.0f) * 0.01f;
		const float guard = 0.001f * fs, xfStep = 1.0f / (0.008f * fs), aLp = LpA (1000.0f, fs);
		float f1[2], f2[2], A1[2], A2[2];
		for (unsigned h = 0; h < 2; h++)
		{
			f1[h] = drate * (h ? 1.31f : 1.0f);
			f2[h] = f1[h] * 1.618f;
			// peak pitch deviation (cents) = 10877 f A (A in seconds): the amplitude is limited to 20 ms of delay
			A1[h] = Clampf (drift * 0.7f / (10877.0f * f1[h]), 0.0f, 0.020f) * fs;
			A2[h] = Clampf (drift * 0.7f / (10877.0f * f2[h]), 0.0f, 0.020f) * fs;
		}
		if (!m_bInit)
		{
			m_bInit = true;
			for (unsigned h = 0; h < 2; h++) { m_h[h].dNew = m_h[h].dOld = (off[h] + loose[h] * m_h[h].rD) * 0.001f * fs; }
		}
		// the targets that follow the knobs
		for (unsigned h = 0; h < nHands; h++)
		{
			THand &H = m_h[h];
			const float dT = Clampf (off[h] + loose[h] * H.rD, 0.3f, 95.0f) * 0.001f * fs;
			if (H.xf >= 1.0f) H.dNew += 0.02f * (dT - H.dNew);
			H.gainT = DbToLin (dyn * H.rG);
			H.toneT = Clampf ((tone + tvar * H.rT) * 0.01f, -1.0f, 1.0f);
		}
		for (unsigned j = 0; j < n; j++)
		{
			const float x = *pMono ? pL[j] : 0.5f * (pL[j] + pR[j]);
			m_in->Put (x);
			{	// peak of the hop (32 samples). Energy over such a short window depends on the phase of the waveform (a note below about 400 Hz has a period longer than the hop), so it cannot be used to see an attack
				const float ax = Absf (x);
				if (ax > m_fE) m_fE = ax;
			}
			if (++m_nHop >= 32)
			{
				m_nHop = 0;
				if (m_nHold > 0) m_nHold--;
				// attack = the fast envelope (peak detector, 20 ms release) jumps to more than twice the slow one (100 ms), above the threshold, after it had settled back below 1.5 times
				const float fFast = m_fEF * (1.0f - hop / 0.020f);
				m_fEF = m_fE > fFast ? m_fE : fFast;
				m_fEP += (hop / 0.100f) * (m_fEF - m_fEP);
				if (m_fEF < 1.5f * m_fEP) m_bArmed = true;
				if (m_bArmed && m_nHold == 0 && m_fEF > fThr && m_fEF > 2.0f * m_fEP)
				{
					m_bArmed = false;
					m_nHold = (unsigned) (0.080f * fs / 32.0f);
					for (unsigned h = 0; h < nHands; h++)
					{
						THand &H = m_h[h];
						H.rD = Rnd (H); H.rG = Rnd (H); H.rT = Rnd (H);
						const float rn = Absf (Rnd (H));
						H.dOld = H.dNew;
						H.dNew = Clampf (off[h] + loose[h] * H.rD, 0.3f, 95.0f) * 0.001f * fs;
						H.xf = 0.0f;
						H.nEnv = 1.0f;
						H.nAmp = pick * 0.4f * (0.5f + 0.5f * rn) * 0.7f * m_fEF;
						H.gainT = DbToLin (dyn * H.rG);
						H.toneT = Clampf ((tone + tvar * H.rT) * 0.01f, -1.0f, 1.0f);
					}
				}
				m_fE = 0.0f;
			}
			float out[2] = {x, x};
			for (unsigned h = 0; h < nHands; h++)
			{
				THand &H = m_h[h];
				if (H.xf < 1.0f) { H.xf += xfStep; if (H.xf > 1.0f) H.xf = 1.0f; }
				H.ph1 = Wrap1 (H.ph1 + f1[h] / fs);
				H.ph2 = Wrap1 (H.ph2 + f2[h] / fs);
				const float ex = guard + A1[h] + A2[h] + A1[h] * Sin1 (H.ph1) + A2[h] * Sin1 (H.ph2);
				const float acc = H.xf >= 1.0f ? m_in->Get (H.dNew + ex) : (1.0f - H.xf) * m_in->Get (H.dOld + ex) + H.xf * m_in->Get (H.dNew + ex);
				H.gain += 0.0008f * (H.gainT - H.gain);
				H.tone += 0.0008f * (H.toneT - H.tone);
				H.lp += aLp * (acc - H.lp);
				const float hp = acc - H.lp;
				float y = H.lp * (1.0f - 0.5f * H.tone) + hp * (1.0f + 0.5f * H.tone);
				if (H.nEnv > 1e-4f)
				{
					const float wn = Rnd (H);
					y += H.nAmp * H.nEnv * 0.5f * (wn - H.nPrev);
					H.nPrev = wn;
					H.nEnv *= 0.9985f;
				}
				out[nMode ? h : 1] = y * H.gain * fLevel;
			}
			pL[j] = out[0];
			pR[j] = out[1];
		}
		*pMono = 0;
	}
private:
	struct THand
	{
		float dOld, dNew, xf, gain, gainT, tone, toneT, lp, ph1, ph2, nEnv, nAmp, nPrev, rD, rG, rT;
		unsigned rng;
	};
	static float Rnd (THand &H)
	{
		H.rng ^= H.rng << 13; H.rng ^= H.rng >> 17; H.rng ^= H.rng << 5;
		return (float) (int) H.rng * (1.0f / 2147483648.0f);
	}
	void Reset (unsigned h, unsigned nSeed)
	{
		THand &H = m_h[h];
		H.rng = nSeed | 1u;
		for (unsigned i = 0; i < 4; i++) Rnd (H);
		H.rD = Rnd (H); H.rG = Rnd (H); H.rT = Rnd (H);
		H.dOld = H.dNew = 0.0f;
		H.xf = 1.0f; H.gain = 1.0f; H.gainT = 1.0f; H.tone = 0.0f; H.toneT = 0.0f; H.lp = 0.0f;
		H.ph1 = Wrap1 (0.21f + 0.4f * (float) h); H.ph2 = Wrap1 (0.67f + 0.1f * (float) h);
		H.nEnv = 0.0f; H.nAmp = 0.0f; H.nPrev = 0.0f;
		m_bInit = false;
	}
	CLine *m_in;
	THand m_h[2];
	unsigned m_nTake, m_nHop, m_nHold;
	float m_fE, m_fEF, m_fEP;
	bool m_bInit, m_bArmed;
};
}	// namespace

CFx *NewExtraFx2 (unsigned nType, float fRate)
{
	switch (nType)
	{
	case FxBluesDrv:	return new CBluesDrv (fRate);
	case FxFz1:		return new CFz1 (fRate);
	case FxStone:		return new CStone (fRate);
	case FxAutoPan:		return new CAutoPan (fRate);
	case FxSlicer:		return new CSlicer (fRate);
	case FxWoman:		return new CWoman (fRate);
	case FxGeq:		return new CGeq (fRate);
	case FxLimiter:		return new CLimiter (fRate);
	case FxAnalog:		return new CAnalog (fRate);
	case FxDuck:		return new CDuck (fRate);
	case FxSustain:		return new CSustain (fRate);
	case FxSelector:	return new CSelector (fRate);
	case FxSecondHand:	return new CSecondHand (fRate);
	default:		return 0;
	}
}
