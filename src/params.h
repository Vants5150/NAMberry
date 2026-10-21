//
// params.h - registry of the blocks' parameters (NAM, and in the future reverb, delay, ...) and state shown by the web page.
// Every block declares its parameters: the page draws them by itself (slider, switch, file choice).
// Used only by cooperative tasks (main loop and HTTP server): no synchronization needed.
//
#ifndef _params_h
#define _params_h

#include <circle/types.h>
#include <circle/util.h>

#define PARAM_MAX_BLOCKS	8
#define PARAM_MAX_PARAMS	10
#define PARAM_TEXT_LEN		48
#define LIB_MAX_FILES		320	// .adn models in the library (it was 256: 790 bytes of fixed memory per slot, offset by lower LIB_MAX_IR and UP_INF_MAX)
#define LIB_MAX_IR		128	// cabinet IRs (.air)

enum TParamKind
{
	ParamSlider = 0,	// numeric value between min and max
	ParamToggle = 1,	// 0 / 1
	ParamFile = 2,		// name of a file of the model library
	ParamChoice = 3,	// an entry of a list (Opts, comma-separated); Value = index
	ParamText = 4		// short free text (name of a guitar): Text, cleaned by CleanLabel
};

struct TParam
{
	char Key[12];
	char Label[24];
	char Unit[6];
	int Kind;
	float Min, Max, Step;
	float Value;
	char Text[PARAM_TEXT_LEN];
	char Opts[40];		// ParamChoice entries separated by commas, e.g. "L,R,Stereo"
	boolean Cfg;		// shown in the "Configuration" tab instead of "Play"
	boolean Changed;
};

struct TBlock
{
	char Id[12];
	char Title[32];
	char Info[PARAM_TEXT_LEN + 16];		// information row (for the lanes: name of the loaded model)
	boolean Loaded;
	unsigned nParams;
	TParam P[PARAM_MAX_PARAMS];
};

// values read by the main loop and shown by the page
struct TCtlStatus
{
	unsigned nSampleRate;
	unsigned nChunkFrames;
	unsigned nCpuAvg;		// tenths of a % of the callback period
	unsigned nCpuMax;
	unsigned nPeriodUs;
	unsigned nPeakPpm;		// input peak (parts per million of full scale) since the last read
	unsigned nPeakLPpm;		// separate L and R peaks (for the configuration page)
	unsigned nPeakRPpm;
	unsigned nOutPkLPpm, nOutPkRPpm;	// peaks of the L and R outputs (ppm of full scale, before the test attenuation)
	unsigned nOutRmsLPpm, nOutRmsRPpm;	// RMS value of the outputs in the last second
	unsigned nTestSrc;		// test source on (0 = no)
	unsigned nUptimeSec;
	unsigned nMtInL, nMtInR, nMtInS, nMtOutL, nMtOutR;	// peaks of the last 200 ms for the real-time bars (ppm of full scale)
	volatile unsigned nRequests;	// HTTP requests served
	volatile unsigned nReqStart, nReqDone;	// requests started and finished (difference > 0 for long = server task dead)
	boolean bSafeMode;
	unsigned nShed;		// times the plan was excluded because of overload
	char IP[24];
};

class CParams
{
public:
	CParams (void) : m_nBlocks (0) {}

	unsigned AddBlock (const char *pId, const char *pTitle)
	{
		unsigned n = m_nBlocks < PARAM_MAX_BLOCKS ? m_nBlocks++ : PARAM_MAX_BLOCKS - 1;
		TBlock &B = m_B[n];
		memset (&B, 0, sizeof B);
		Copy (B.Id, pId, sizeof B.Id);
		Copy (B.Title, pTitle, sizeof B.Title);
		return n;
	}

	unsigned AddParam (unsigned nBlock, const char *pKey, const char *pLabel, const char *pUnit, int nKind,
			   float fMin, float fMax, float fStep, float fValue, const char *pText = "", const char *pOpts = "")
	{
		TBlock &B = m_B[nBlock];
		unsigned n = B.nParams < PARAM_MAX_PARAMS ? B.nParams++ : PARAM_MAX_PARAMS - 1;
		TParam &P = B.P[n];
		memset (&P, 0, sizeof P);
		Copy (P.Key, pKey, sizeof P.Key);
		Copy (P.Label, pLabel, sizeof P.Label);
		Copy (P.Unit, pUnit, sizeof P.Unit);
		P.Kind = nKind;
		P.Min = fMin;
		P.Max = fMax;
		P.Step = fStep;
		P.Value = fValue;
		Copy (P.Text, pText, sizeof P.Text);
		Copy (P.Opts, pOpts, sizeof P.Opts);
		return n;
	}

	unsigned GetBlockCount (void) const { return m_nBlocks; }
	TBlock &Block (unsigned n) { return m_B[n]; }

	int FindBlock (const char *pId) const
	{
		for (unsigned b = 0; b < m_nBlocks; b++)
		{
			if (strcmp (m_B[b].Id, pId) == 0)
			{
				return (int) b;
			}
		}
		return -1;
	}

	int FindParam (unsigned nBlock, const char *pKey) const
	{
		for (unsigned p = 0; p < m_B[nBlock].nParams; p++)
		{
			if (strcmp (m_B[nBlock].P[p].Key, pKey) == 0)
			{
				return (int) p;
			}
		}
		return -1;
	}

	// the next parameter changed from the page (or from the settings): TRUE if there is one
	boolean TakeChange (unsigned *pBlock, unsigned *pParam)
	{
		for (unsigned b = 0; b < m_nBlocks; b++)
		{
			for (unsigned p = 0; p < m_B[b].nParams; p++)
			{
				if (m_B[b].P[p].Changed)
				{
					m_B[b].P[p].Changed = FALSE;
					*pBlock = b;
					*pParam = p;
					return TRUE;
				}
			}
		}
		return FALSE;
	}

	static void Copy (char *pDst, const char *pSrc, unsigned nSize)
	{
		unsigned i = 0;
		for (; i + 1 < nSize && pSrc[i]; i++)
		{
			pDst[i] = pSrc[i];
		}
		pDst[i] = 0;
	}

private:
	TBlock m_B[PARAM_MAX_BLOCKS];
	unsigned m_nBlocks;
};

// allowed text label: simple printable characters (no quotation marks, slashes, equals, < > & %), without spaces at the edges, at most nMax - 1 characters
static inline void CleanLabel (char *pDst, const char *pSrc, unsigned nMax)
{
	unsigned n = 0;
	for (; *pSrc && n + 1 < nMax; pSrc++)
	{
		const unsigned char c = (unsigned char) *pSrc;
		if (c < 32 || c > 126 || c == 34 || c == 92 || c == 61 || c == 60 || c == 62 || c == 38 || c == 37 || c == 39)
		{
			continue;
		}
		if (c == 32 && n == 0)
		{
			continue;
		}
		pDst[n++] = (char) c;
	}
	while (n > 0 && pDst[n - 1] == 32)
	{
		n--;
	}
	pDst[n] = 0;
}

// decimal number without libm
static inline float ParseFloat (const char *p)
{
	float fSign = 1.0f, fValue = 0.0f;
	if (*p == '-')
	{
		fSign = -1.0f;
		p++;
	}
	else if (*p == '+')
	{
		p++;
	}
	while (*p >= '0' && *p <= '9')
	{
		fValue = fValue * 10.0f + (float) (*p - '0');
		p++;
	}
	if (*p == '.')
	{
		p++;
		float fScale = 0.1f;
		while (*p >= '0' && *p <= '9')
		{
			fValue += (float) (*p - '0') * fScale;
			fScale *= 0.1f;
			p++;
		}
	}
	return fSign * fValue;
}

// writes v with 'dec' decimals (0..3); returns the number of characters
static inline unsigned FormatFixed (char *pOut, float v, unsigned dec)
{
	unsigned n = 0;
	if (v < 0.0f)
	{
		pOut[n++] = '-';
		v = -v;
	}
	unsigned nScale = 1;
	for (unsigned i = 0; i < dec; i++)
	{
		nScale *= 10;
	}
	unsigned long long nInt = (unsigned long long) (v * (float) nScale + 0.5f);
	unsigned long long nWhole = nInt / nScale;
	unsigned nFrac = (unsigned) (nInt % nScale);

	char Digits[24];
	unsigned nDigits = 0;
	do
	{
		Digits[nDigits++] = (char) ('0' + nWhole % 10);
		nWhole /= 10;
	}
	while (nWhole > 0 && nDigits < sizeof Digits);
	while (nDigits > 0)
	{
		pOut[n++] = Digits[--nDigits];
	}
	if (dec > 0)
	{
		pOut[n++] = '.';
		for (unsigned i = dec; i > 0; i--)
		{
			nScale /= 10;
			pOut[n++] = (char) ('0' + (nFrac / nScale) % 10);
		}
	}
	pOut[n] = 0;
	return n;
}

#endif
