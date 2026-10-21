//
// ftools.cpp - see ftools.h
//
#include "ftools.h"
#include "namtone.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{
inline float Clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Absf (float v) { return v < 0.0f ? -v : v; }
inline float ExpF (float x) { return DbToLin (Clampf (x, -80.0f, 80.0f) * 8.685889638f); }
inline float Tanhf (float x)
{
	if (x > 3.0f) return 1.0f;
	if (x < -3.0f) return -1.0f;
	const float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// sine of 2 pi p, p in [0, 1)
inline float Sin1 (float p)
{
	float x = 3.14159265f - p * 6.2831853f;
	const float y = 1.27323954f * x - 0.40528473f * x * Absf (x);
	return 0.225f * (y * Absf (y) - y) + y;
}
inline float LnF (float x)
{
	if (x < 1e-20f) x = 1e-20f;
	union { float f; u32 u; } v;
	v.f = x;
	const int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
	const float m = v.f;
	return (float) e * 0.69314718f + (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}
inline float Db10 (float x) { return 4.3429448f * LnF (x); }		// 10 log10
inline float SqrtF (float x)		// root with three Newton iterations starting from an estimate of the exponent (without machine instructions: it also runs on the PC)
{
	if (x <= 0.0f) return 0.0f;
	union { float f; u32 u; } v;
	v.f = x;
	v.u = (v.u >> 1) + 0x1FBD1DF5u;
	float r = v.f;
	r = 0.5f * (r + x / r);
	r = 0.5f * (r + x / r);
	r = 0.5f * (r + x / r);
	return r;
}
const char *const kNames[FT_COUNT] = {"hum", "rumble", "limit", "mono", "air", "sat", "click", "rta", "tuner", "meter"};
const char *const kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}	// namespace

CFeatures::CFeatures (float fRate)
:	m_fRate (fRate)
{
	memset (&m_Hum, 0, sizeof m_Hum);
	memset (&m_Hp, 0, sizeof m_Hp);
	memset (&m_Air, 0, sizeof m_Air);
	memset (m_Band, 0, sizeof m_Band);
	memset (&m_KShelf, 0, sizeof m_KShelf);
	memset (&m_KHp, 0, sizeof m_KHp);
	for (unsigned i = 0; i < FT_COUNT; i++) m_On[i] = 0;
	m_bAny = FALSE;
	m_fMains = 50.0f; m_fBpm = 100.0f; m_fClickDb = -24.0f; m_fAirDb = 4.0f; m_fSat = 30.0f; m_fCeilDb = -1.0f; m_fTMute = 1.0f;
	m_fLimG = 1.0f; m_fGrDb = 0.0f;
	m_nClickPos = m_nClickBeat = 0; m_fClickPh = 0.0f; m_nClickLen = 0;
	for (unsigned k = 0; k < FT_RTA_BANDS; k++) { m_fBandAcc[k] = 0.0f; m_fRtaDb[k] = -100.0f; }
	m_nBandCnt = 0; m_nRtaAt = 0;
	memset (m_fTunBuf, 0, sizeof m_fTunBuf);
	m_nTunW = 0; m_fTunAcc = 0.0f; m_nTunAccN = 0;
	m_bTunerOk = FALSE; m_fTunerHz = 0.0f; m_fTunerCents = 0.0f; m_nTunerMidi = 0; m_nTunHistN = 0; m_nTunHist[0] = m_nTunHist[1] = m_nTunHist[2] = 0;
	m_nTunerAt = 0; m_fTunLevel = 0.0f;
	m_fKAcc = m_fRmsAcc = m_fPeakAcc = m_fCorrLR = m_fCorrLL = m_fCorrRR = 0.0f;
	m_nKCnt = 0; m_nKBlockN = 0;
	for (unsigned i = 0; i < 4; i++) m_fKBlocks[i] = m_fRBlocks[i] = m_fPBlocks[i] = 0.0f;
	m_fLufs = -120.0f; m_fPeakDb = -120.0f; m_fCrestDb = 0.0f; m_fCorr = 0.0f; m_nMeterAt = 0; m_fPeakHold = 0.0f;
	m_fSatG = 1.0f; m_fClickAmp = 0.0f; m_fLimThr = 1.0f; m_fLimCeil = 1.0f;
	Recalc ();
}

const char *CFeatures::Name (unsigned nId) { return nId < FT_COUNT ? kNames[nId] : ""; }

int CFeatures::IdByName (const char *p)
{
	for (unsigned i = 0; i < FT_COUNT; i++)
	{
		if (strcmp (kNames[i], p) == 0) return (int) i;
	}
	return -1;
}

float CFeatures::RtaCenter (unsigned k) { return 63.0f * ExpF (0.23104906f * (float) k); }		// 63 Hz x 2^(k/3)

void CFeatures::BqSet (TBq *p, float b0, float b1, float b2, float a0, float a1, float a2)
{
	const float r = 1.0f / a0;
	p->b0 = b0 * r; p->b1 = b1 * r; p->b2 = b2 * r; p->a1 = a1 * r; p->a2 = a2 * r;
}

float CFeatures::BqRun (TBq *p, unsigned c, float x)
{
	const float y = p->b0 * x + p->z1[c];
	p->z1[c] = p->b1 * x - p->a1 * y + p->z2[c];
	p->z2[c] = p->b2 * x - p->a2 * y;
	return y;
}

void CFeatures::Recalc (void)
{
	const float fs = m_fRate;
	// mains notch: f0, 2 f0, 3 f0, 4 f0 with Q 30
	for (unsigned k = 0; k < 4; k++)
	{
		const float f = m_fMains * (float) (k + 1);
		const float w0 = 6.2831853f * Clampf (f, 20.0f, 0.45f * fs) / fs, cw = DspCos (w0), al = DspSin (w0) / 60.0f;
		BqSet (&m_Hum[k], 1.0f, -2.0f * cw, 1.0f, 1.0f + al, -2.0f * cw, 1.0f - al);
	}
	{	// 80 Hz high-pass, second-order Butterworth
		const float w0 = 6.2831853f * 80.0f / fs, cw = DspCos (w0), al = DspSin (w0) / (2.0f * 0.70710678f);
		BqSet (&m_Hp, (1.0f + cw) * 0.5f, -(1.0f + cw), (1.0f + cw) * 0.5f, 1.0f + al, -2.0f * cw, 1.0f - al);
	}
	{	// air band: shelf a 12 kHz
		float c[NT_COEF];
		NtShelf (c, 0, TRUE, 12000.0f, Clampf (m_fAirDb, 0.0f, 12.0f), fs);
		m_Air.b0 = c[0]; m_Air.b1 = c[1]; m_Air.b2 = c[2]; m_Air.a1 = c[3]; m_Air.a2 = c[4];
	}
	{	// K weighting (approximate, ITU-R BS.1770 style): +4 dB shelf at 1.68 kHz and 38 Hz high-pass with Q 0.5
		float c[NT_COEF];
		NtShelf (c, 0, TRUE, 1682.0f, 4.0f, fs);
		m_KShelf.b0 = c[0]; m_KShelf.b1 = c[1]; m_KShelf.b2 = c[2]; m_KShelf.a1 = c[3]; m_KShelf.a2 = c[4];
		const float w0 = 6.2831853f * 38.135f / fs, cw = DspCos (w0), al = DspSin (w0) / (2.0f * 0.5003f);
		BqSet (&m_KHp, (1.0f + cw) * 0.5f, -(1.0f + cw), (1.0f + cw) * 0.5f, 1.0f + al, -2.0f * cw, 1.0f - al);
	}
	for (unsigned k = 0; k < FT_RTA_BANDS; k++)
	{
		// third-octave band: band-pass with 0 dB gain at the center, Q = 4.32
		const float w0 = 6.2831853f * RtaCenter (k) / fs, cw = DspCos (w0), al = DspSin (w0) / (2.0f * 4.32f);
		BqSet (&m_Band[k], al, 0.0f, -al, 1.0f + al, -2.0f * cw, 1.0f - al);
	}
	m_fLimCeil = DbToLin (Clampf (m_fCeilDb, -12.0f, 0.0f));
	m_fSatG = 1.0f + 6.0f * Clampf (m_fSat, 0.0f, 100.0f) * 0.01f;		// it was 1 + 2.5 x: at 80% it reduced the peaks at -12 dBFS by only 1.7 dB and with the NAM (output normalized to about -20 dBFS RMS) it could not be heard
	m_fClickAmp = DbToLin (Clampf (m_fClickDb, -40.0f, -6.0f));
	m_nClickLen = (unsigned) (60.0f / Clampf (m_fBpm, 40.0f, 240.0f) * fs);
}

void CFeatures::SetOn (unsigned nId, boolean bOn)
{
	if (nId >= FT_COUNT) return;
	m_On[nId] = bOn ? 1 : 0;
	// clean state at power-on: no tails of old signal
	if (nId == FtHum) for (unsigned k = 0; k < 4; k++) memset (m_Hum[k].z1, 0, sizeof m_Hum[k].z1), memset (m_Hum[k].z2, 0, sizeof m_Hum[k].z2);
	if (nId == FtRumble) { memset (m_Hp.z1, 0, sizeof m_Hp.z1); memset (m_Hp.z2, 0, sizeof m_Hp.z2); }
	if (nId == FtAir) { memset (m_Air.z1, 0, sizeof m_Air.z1); memset (m_Air.z2, 0, sizeof m_Air.z2); }
	if (nId == FtLimit) { m_fLimG = 1.0f; m_fGrDb = 0.0f; }
	if (nId == FtClick) { m_nClickPos = 0; m_nClickBeat = 0; m_fClickPh = 0.0f; }
	if (nId == FtRta)
	{
		for (unsigned k = 0; k < FT_RTA_BANDS; k++) { m_fBandAcc[k] = 0.0f; m_fRtaDb[k] = -100.0f; memset (m_Band[k].z1, 0, sizeof m_Band[k].z1); memset (m_Band[k].z2, 0, sizeof m_Band[k].z2); }
		m_nBandCnt = 0;
	}
	if (nId == FtTuner) { m_nTunW = 0; m_fTunAcc = 0.0f; m_nTunAccN = 0; m_bTunerOk = FALSE; m_nTunHistN = 0; memset (m_fTunBuf, 0, sizeof m_fTunBuf); }
	if (nId == FtMeter)
	{
		memset (m_KShelf.z1, 0, sizeof m_KShelf.z1); memset (m_KShelf.z2, 0, sizeof m_KShelf.z2); memset (m_KHp.z1, 0, sizeof m_KHp.z1); memset (m_KHp.z2, 0, sizeof m_KHp.z2);
		m_fKAcc = m_fRmsAcc = m_fPeakAcc = m_fCorrLR = m_fCorrLL = m_fCorrRR = 0.0f; m_nKCnt = 0; m_nKBlockN = 0; m_fPeakHold = 0.0f;
		m_fLufs = -120.0f; m_fPeakDb = -120.0f; m_fCrestDb = 0.0f; m_fCorr = 0.0f;
	}
	boolean bAny = FALSE;
	for (unsigned i = 0; i < FT_COUNT; i++) if (m_On[i]) bAny = TRUE;
	m_bAny = bAny;
}

boolean CFeatures::SetParam (const char *k, float v)
{
	if (strcmp (k, "mains") == 0) { m_fMains = v > 55.0f ? 60.0f : 50.0f; }
	else if (strcmp (k, "bpm") == 0) { m_fBpm = Clampf (v, 40.0f, 240.0f); }
	else if (strcmp (k, "clickdb") == 0) { m_fClickDb = Clampf (v, -40.0f, -6.0f); }
	else if (strcmp (k, "airdb") == 0) { m_fAirDb = Clampf (v, 0.0f, 12.0f); }
	else if (strcmp (k, "satdrive") == 0) { m_fSat = Clampf (v, 0.0f, 100.0f); }
	else if (strcmp (k, "ceil") == 0) { m_fCeilDb = Clampf (v, -12.0f, 0.0f); }
	else if (strcmp (k, "tmute") == 0) { m_fTMute = v > 0.5f ? 1.0f : 0.0f; }
	else return FALSE;
	Recalc ();
	return TRUE;
}

float CFeatures::Param (const char *k) const
{
	if (strcmp (k, "mains") == 0) return m_fMains;
	if (strcmp (k, "bpm") == 0) return m_fBpm;
	if (strcmp (k, "clickdb") == 0) return m_fClickDb;
	if (strcmp (k, "airdb") == 0) return m_fAirDb;
	if (strcmp (k, "satdrive") == 0) return m_fSat;
	if (strcmp (k, "ceil") == 0) return m_fCeilDb;
	if (strcmp (k, "tmute") == 0) return m_fTMute;
	return 0.0f;
}

static unsigned PutNum (char *p, int v)
{
	unsigned n = 0;
	if (v < 0) { p[n++] = '-'; v = -v; }
	char t[12];
	unsigned m = 0;
	do { t[m++] = (char) ('0' + v % 10); v /= 10; } while (v > 0 && m < 11);
	while (m > 0) p[n++] = t[--m];
	return n;
}

unsigned CFeatures::Serialize (char *p, unsigned nSize) const
{
	if (nSize < 120) return 0;
	unsigned n = 0;
	const char *pre = "on=";
	for (const char *s = pre; *s; s++) p[n++] = *s;
	for (unsigned i = 0; i < FT_COUNT; i++) p[n++] = (Persistent (i) && m_On[i]) ? '1' : '0';
	p[n++] = '\n';
	struct { const char *k; int v; } kv[] = {{"mains", (int) m_fMains}, {"bpm", (int) (m_fBpm + 0.5f)}, {"clickdb", (int) (m_fClickDb - (m_fClickDb < 0 ? 0.5f : -0.5f))}, {"airdb", (int) (m_fAirDb + 0.5f)},
						 {"satdrive", (int) (m_fSat + 0.5f)}, {"ceil", (int) (m_fCeilDb - (m_fCeilDb < 0 ? 0.5f : -0.5f))}, {"tmute", (int) m_fTMute}};
	for (unsigned i = 0; i < 7; i++)
	{
		for (const char *s = kv[i].k; *s; s++) p[n++] = *s;
		p[n++] = '=';
		n += PutNum (p + n, kv[i].v);
		p[n++] = '\n';
	}
	p[n] = 0;
	return n;
}

void CFeatures::Parse (const char *pText)
{
	const char *p = pText;
	while (*p)
	{
		const char *e = p;
		while (*e && *e != '\n' && *e != '\r') e++;
		const char *eq = p;
		while (eq < e && *eq != '=') eq++;
		if (eq < e)
		{
			char key[12];
			unsigned kl = (unsigned) (eq - p);
			if (kl >= sizeof key) kl = sizeof key - 1;
			memcpy (key, p, kl);
			key[kl] = 0;
			const char *v = eq + 1;
			if (strcmp (key, "on") == 0)
			{
				for (unsigned i = 0; i < FT_COUNT && v + i < e; i++)
				{
					SetOn (i, Persistent (i) && v[i] == '1');
				}
			}
			else
			{
				int sign = 1, val = 0;
				if (v < e && *v == '-') { sign = -1; v++; }
				while (v < e && *v >= '0' && *v <= '9' && val < 100000) val = val * 10 + (*v++ - '0');
				SetParam (key, (float) (sign * val));
			}
		}
		p = e;
		while (*p == '\n' || *p == '\r') p++;
	}
}

void CFeatures::NoteName (int nMidi, char *pOut)
{
	if (nMidi < 0) nMidi = 0;
	const int oct = nMidi / 12 - 1;
	const char *nm = kNotes[nMidi % 12];
	unsigned n = 0;
	while (*nm) pOut[n++] = *nm++;
	if (oct < 0) { pOut[n++] = '-'; pOut[n++] = '1'; }
	else n += PutNum (pOut + n, oct);
	pOut[n] = 0;
}

// ---------------------------------------------------------------------------------------------------------------- callback audio
void CFeatures::ProcessIn (float *pL, float *pR, unsigned n)
{
	if (!m_bAny) return;
	const boolean bHum = m_On[FtHum], bHp = m_On[FtRumble], bTun = m_On[FtTuner];
	if (!bHum && !bHp && !bTun) return;
	for (unsigned j = 0; j < n; j++)
	{
		float l = pL[j], r = pR[j];
		if (bHum)
		{
			for (unsigned k = 0; k < 4; k++) { l = BqRun (&m_Hum[k], 0, l); r = BqRun (&m_Hum[k], 1, r); }
		}
		if (bHp)
		{
			l = BqRun (&m_Hp, 0, l);
			r = BqRun (&m_Hp, 1, r);
		}
		pL[j] = l;
		pR[j] = r;
		if (bTun)
		{
			m_fTunAcc += 0.5f * (l + r);
			if (++m_nTunAccN >= 4)
			{
				m_fTunBuf[m_nTunW & 4095u] = m_fTunAcc * 0.25f;
				m_nTunW = m_nTunW + 1;
				m_fTunAcc = 0.0f;
				m_nTunAccN = 0;
			}
		}
	}
}

void CFeatures::ProcessOut (float *pL, float *pR, unsigned n)
{
	if (!m_bAny) return;
	const boolean bSat = m_On[FtSat], bAir = m_On[FtAir], bClk = m_On[FtClick], bLim = m_On[FtLimit], bMono = m_On[FtMono];
	const boolean bRta = m_On[FtRta], bMet = m_On[FtMeter], bMute = m_On[FtTuner] && m_fTMute > 0.5f;
	if (!bSat && !bAir && !bClk && !bLim && !bMono && !bRta && !bMet && !bMute) return;
	const float g = m_fSatG, ig = 1.0f / m_fSatG;
	const boolean bSatOn = bSat && m_fSat > 0.5f;
	const float aRel = 1.0f - ExpF (-1.0f / (0.15f * m_fRate)), aAtt = 0.5f;
	const float ceil_ = m_fLimCeil;
	float grMin = 1.0f;
	for (unsigned j = 0; j < n; j++)
	{
		float l = pL[j], r = pR[j];
		if (bSatOn) { l = Tanhf (l * g) * ig; r = Tanhf (r * g) * ig; }
		if (bAir) { l = BqRun (&m_Air, 0, l); r = BqRun (&m_Air, 1, r); }
		if (bClk)
		{
			if (m_nClickPos >= m_nClickLen) { m_nClickPos = 0; m_nClickBeat = (m_nClickBeat + 1) & 3; }
			const unsigned burst = (unsigned) (0.015f * m_fRate);
			if (m_nClickPos < burst)
			{
				const float f = m_nClickBeat == 0 ? 1500.0f : 1000.0f;
				m_fClickPh += f / m_fRate;
				if (m_fClickPh >= 1.0f) m_fClickPh -= 1.0f;
				const float env = 1.0f - (float) m_nClickPos / (float) burst;
				const float s = Sin1 (m_fClickPh) * env * m_fClickAmp;
				l += s;
				r += s;
			}
			m_nClickPos++;
		}
		if (bLim)
		{
			float ax = Absf (l);
			if (Absf (r) > ax) ax = Absf (r);
			const float gt = ax * m_fLimG > ceil_ ? ceil_ / (ax + 1e-12f) : 1.0f;
			m_fLimG += (gt < m_fLimG ? aAtt : aRel) * (gt - m_fLimG);
			l = Clampf (l * m_fLimG, -ceil_, ceil_);
			r = Clampf (r * m_fLimG, -ceil_, ceil_);
			if (m_fLimG < grMin) grMin = m_fLimG;
		}
		if (bMono) { const float m = 0.5f * (l + r); l = r = m; }
		if (bRta)
		{
			const float x = 0.5f * (l + r);
			for (unsigned k = 0; k < FT_RTA_BANDS; k++)
			{
				const float y = BqRun (&m_Band[k], 0, x);
				m_fBandAcc[k] += y * y;
			}
			m_nBandCnt++;
		}
		if (bMet)
		{
			const float kl = BqRun (&m_KHp, 0, BqRun (&m_KShelf, 0, l)), kr = BqRun (&m_KHp, 1, BqRun (&m_KShelf, 1, r));
			m_fKAcc += kl * kl + kr * kr;
			m_fRmsAcc += l * l + r * r;
			const float al = Absf (l), ar = Absf (r);
			if (al > m_fPeakAcc) m_fPeakAcc = al;
			if (ar > m_fPeakAcc) m_fPeakAcc = ar;
			m_fCorrLR += l * r; m_fCorrLL += l * l; m_fCorrRR += r * r;
			m_nKCnt++;
		}
		if (bMute) { l = 0.0f; r = 0.0f; }
		pL[j] = l;
		pR[j] = r;
	}
	if (bLim)
	{
		const float gdb = grMin < 1.0f ? -Db10 (grMin) * 2.0f : 0.0f;		// reduction in dB (20 log10)
		m_fGrDb = gdb > m_fGrDb ? gdb : m_fGrDb * 0.995f;
	}
}

// ---------------------------------------------------------------------------------------------------------------- main loop
void CFeatures::TunerAnalyze (void)
{
	static float x[1024];
	static float d[181], cm[181];
	const unsigned W = 768, TMAX = 180, TMIN = 10, N = W + TMAX;
	const unsigned w = m_nTunW;
	if (w < N) { m_bTunerOk = FALSE; return; }
	float e = 0.0f;
	for (unsigned i = 0; i < N; i++)
	{
		x[i] = m_fTunBuf[(w - N + i) & 4095u];
	}
	for (unsigned i = 0; i < W; i++) e += x[i] * x[i];
	m_fTunLevel = SqrtF (e / (float) W);
	if (m_fTunLevel < 0.002f)		// below about -54 dBFS: no signal
	{
		m_bTunerOk = FALSE;
		m_nTunHistN = 0;
		return;
	}
	for (unsigned tau = 1; tau <= TMAX; tau++)
	{
		float s = 0.0f;
		for (unsigned j = 0; j < W; j++)
		{
			const float df = x[j] - x[j + tau];
			s += df * df;
		}
		d[tau] = s;
	}
	float run = 0.0f;
	cm[0] = 1.0f;
	for (unsigned tau = 1; tau <= TMAX; tau++)
	{
		run += d[tau];
		cm[tau] = run > 1e-20f ? d[tau] * (float) tau / run : 1.0f;
	}
	int best = -1;
	for (unsigned tau = TMIN; tau < TMAX; tau++)
	{
		if (cm[tau] < 0.15f)
		{
			unsigned t = tau;
			while (t + 1 < TMAX && cm[t + 1] < cm[t]) t++;
			best = (int) t;
			break;
		}
	}
	if (best < 0)
	{
		float mn = 1.0f;
		for (unsigned tau = TMIN; tau < TMAX; tau++) if (cm[tau] < mn) { mn = cm[tau]; best = (int) tau; }
		if (mn > 0.3f) best = -1;
	}
	if (best < 2 || best >= (int) TMAX)
	{
		m_bTunerOk = FALSE;
		return;
	}
	const float a = cm[best - 1], b = cm[best], c = cm[best + 1];
	const float den = a - 2.0f * b + c;
	const float tauF = (float) best + (den > 1e-9f || den < -1e-9f ? 0.5f * (a - c) / den : 0.0f);
	const float hz = (m_fRate * 0.25f) / tauF;
	const float midiF = 69.0f + 12.0f * LnF (hz / 440.0f) / 0.69314718f;
	const int nm = (int) (midiF + (midiF >= 0.0f ? 0.5f : -0.5f));
	// median of three measurements so that the note does not jitter
	if (m_nTunHistN < 3) m_nTunHist[m_nTunHistN++] = nm;
	else { m_nTunHist[0] = m_nTunHist[1]; m_nTunHist[1] = m_nTunHist[2]; m_nTunHist[2] = nm; }
	int med = nm;
	if (m_nTunHistN == 3)
	{
		int h0 = m_nTunHist[0], h1 = m_nTunHist[1], h2 = m_nTunHist[2];
		if (h0 > h1) { const int t = h0; h0 = h1; h1 = t; }
		if (h1 > h2) { const int t = h1; h1 = h2; h2 = t; }
		if (h0 > h1) { const int t = h0; h0 = h1; h1 = t; }
		med = h1;
	}
	m_fTunerHz = hz;
	m_nTunerMidi = med;
	m_fTunerCents = (midiF - (float) med) * 100.0f;
	m_bTunerOk = TRUE;
}

void CFeatures::Tick (unsigned nMs)
{
	if (!m_bAny) return;
	if (m_On[FtTuner] && nMs - m_nTunerAt >= 100)
	{
		m_nTunerAt = nMs;
		TunerAnalyze ();
	}
	if (m_On[FtRta] && nMs - m_nRtaAt >= 100)
	{
		m_nRtaAt = nMs;
		const unsigned cnt = m_nBandCnt;
		if (cnt > 0)
		{
			for (unsigned k = 0; k < FT_RTA_BANDS; k++)
			{
				const float ms = m_fBandAcc[k] / (float) cnt;
				m_fBandAcc[k] = 0.0f;
				float db = Db10 (2.0f * ms + 1e-12f);		// a sine wave at full scale = 0 dB
				if (db < -100.0f) db = -100.0f;
				m_fRtaDb[k] = db > m_fRtaDb[k] ? db : (m_fRtaDb[k] - 4.0f > db ? m_fRtaDb[k] - 4.0f : db);
			}
			m_nBandCnt = 0;
		}
	}
	if (m_On[FtMeter] && nMs - m_nMeterAt >= 100)
	{
		m_nMeterAt = nMs;
		const unsigned cnt = m_nKCnt;
		if (cnt > 0)
		{
			const float kms = m_fKAcc / (float) cnt, rms = m_fRmsAcc / (float) cnt;
			const unsigned i = m_nKBlockN & 3u;
			m_fKBlocks[i] = kms; m_fRBlocks[i] = rms; m_fPBlocks[i] = m_fPeakAcc;
			m_nKBlockN++;
			m_fKAcc = m_fRmsAcc = m_fPeakAcc = 0.0f;
			m_nKCnt = 0;
			const unsigned nb = m_nKBlockN < 4 ? m_nKBlockN : 4;
			float ks = 0.0f, rs = 0.0f, pk = 0.0f;
			for (unsigned q = 0; q < nb; q++) { ks += m_fKBlocks[q]; rs += m_fRBlocks[q]; if (m_fPBlocks[q] > pk) pk = m_fPBlocks[q]; }
			ks /= (float) nb; rs /= (float) nb;
			m_fLufs = ks > 1e-12f ? -0.691f + Db10 (ks) : -120.0f;
			m_fPeakDb = pk > 1e-6f ? Db10 (pk * pk) : -120.0f;		// 20 log10 of the peak
			const float rmsDb = rs > 1e-12f ? Db10 (rs * 0.5f) : -120.0f;
			m_fCrestDb = (m_fPeakDb > -119.0f && rmsDb > -119.0f) ? m_fPeakDb - rmsDb : 0.0f;		// peak minus RMS of the channel: 3 dB for a sine wave, more for very dynamic sound
			m_fCorrLR *= 0.85f; m_fCorrLL *= 0.85f; m_fCorrRR *= 0.85f;
			const float dn = SqrtF (m_fCorrLL * m_fCorrRR);
			m_fCorr = dn > 1e-12f ? m_fCorrLR / dn : 0.0f;
		}
	}
}
