//
// control.cpp - control web page
//
#include "control.h"
#include "config.h"
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
#include "graph.h"
#include "ftools.h"
#include <circle/logger.h>
#include <circle/util.h>

#define CONTROL_BUFFER_SIZE	196608
#define CONTROL_TIMEOUT_SEC	10

static const char FromControl[] = "control";
#define DBG(msg)		// debug traces (CLogger::Get ()->Write (FromControl, LogNotice, msg))

// library version (models, IRs, information): FNV-1a on the names; it changes when one of the three lists changes
static unsigned LibVersion (const TCtlContext *pC)
{
	unsigned h = 2166136261u;
	const unsigned nLib = *pC->pLibCount, nIr = *pC->pIrCount;
	for (unsigned i = 0; i < nLib; i++)
	{
		for (const char *p = pC->pLib[i]; *p; p++)
		{
			h = (h ^ (unsigned char) *p) * 16777619u;
		}
		h = (h ^ 1u) * 16777619u;
		for (const char *p = pC->pLibInf[i]; *p; p++)
		{
			h = (h ^ (unsigned char) *p) * 16777619u;
		}
		h = (h ^ 2u) * 16777619u;
	}
	for (unsigned i = 0; i < nIr; i++)
	{
		for (const char *p = pC->pIr[i]; *p; p++)
		{
			h = (h ^ (unsigned char) *p) * 16777619u;
		}
		h = (h ^ 3u) * 16777619u;
	}
	return h ^ (nLib << 20) ^ (nIr << 8);
}

#include "control_page_gz.h"
#include "fx_defs.h"
#include "fx.h"

// writes into a fixed-size buffer
class CBuf
{
public:
	CBuf (u8 *pBuf, unsigned nMax) : m_p ((char *) pBuf), m_nMax (nMax), m_n (0), m_bOver (FALSE) {}

	void C (char c)
	{
		if (m_n + 1 < m_nMax)
		{
			m_p[m_n++] = c;
		}
		else
		{
			m_bOver = TRUE;
		}
	}

	void S (const char *p)
	{
		while (*p)
		{
			C (*p++);
		}
	}

	void Esc (const char *p)
	{
		C ('"');
		for (; *p; p++)
		{
			if (*p == '"' || *p == '\\')
			{
				C ('\\');
				C (*p);
			}
			else if ((unsigned char) *p >= 0x20)
			{
				C (*p);
			}
		}
		C ('"');
	}

	void I (unsigned long long v)
	{
		char T[24];
		FormatFixed (T, (float) v, 0);	// small values (< 2^24): exact
		S (T);
	}

	void F (float v, unsigned dec)
	{
		char T[32];
		FormatFixed (T, v, dec);
		S (T);
	}

	unsigned Length (void) const { return m_n; }
	boolean Overflow (void) const { return m_bOver; }

private:
	char *m_p;
	unsigned m_nMax;
	unsigned m_n;
	boolean m_bOver;
};

CControl::CControl (CNetSubSystem *pNetSubSystem, TCtlContext *pContext, CSocket *pSocket)
:	CHTTPDaemon (pNetSubSystem, pSocket, CONTROL_BUFFER_SIZE, HTTP_PORT, 0, CONTROL_TIMEOUT_SEC),
	m_pContext (pContext)
{
}

CControl::~CControl (void)
{
	m_pContext = 0;
}

CHTTPDaemon *CControl::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CControl (pNetSubSystem, m_pContext, pSocket);
}

// "name=value" field of a url-encoded form (the values used here have no characters to encode)
static boolean GetField (const char *pForm, const char *pName, char *pOut, unsigned nSize)
{
	unsigned nNameLen = strlen (pName);
	const char *p = pForm;
	while (*p)
	{
		if (strncmp (p, pName, nNameLen) == 0 && p[nNameLen] == '=')
		{
			p += nNameLen + 1;
			unsigned i = 0;
			while (*p && *p != '&' && i + 1 < nSize)
			{
				pOut[i++] = *p++;
			}
			pOut[i] = 0;
			return TRUE;
		}
		while (*p && *p != '&')
		{
			p++;
		}
		if (*p == '&')
		{
			p++;
		}
	}
	return FALSE;
}

static THTTPStatus Reply (u8 *pBuffer, unsigned *pLength, const char **ppContentType, const char *pText)
{
	CBuf B (pBuffer, *pLength);
	B.S (pText);
	*pLength = B.Length ();
	*ppContentType = "application/json";
	return HTTPOK;
}

static void UrlDecode (const char *pIn, char *pOut, unsigned nSize);

THTTPStatus CControl::HandleSet (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Block[16], Key[16], Val[PARAM_TEXT_LEN + 8];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (!GetField (pFormData, "b", Block, sizeof Block) || !GetField (pFormData, "k", Key, sizeof Key))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
	}

	CParams *pParams = m_pContext->pParams;
	int nB = pParams->FindBlock (Block);
	int nP = nB >= 0 ? pParams->FindParam ((unsigned) nB, Key) : -1;
	if (nB < 0 || nP < 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"unknown parameter\"}");
	}

	TParam &P = pParams->Block ((unsigned) nB).P[nP];
	if (P.Kind == ParamFile)
	{
		if (!GetField (pFormData, "s", Val, sizeof Val))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"missing name\"}");
		}
		boolean bFound = FALSE;
		for (unsigned i = 0; i < *m_pContext->pLibCount; i++)
		{
			if (strcmp (m_pContext->pLib[i], Val) == 0)
			{
				bFound = TRUE;
			}
		}
		if (!bFound)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"file not in the library\"}");
		}
		CParams::Copy (P.Text, Val, sizeof P.Text);
	}
	else if (P.Kind == ParamText)
	{
		char Raw[96], Dec[96];
		if (!GetField (pFormData, "s", Raw, sizeof Raw))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"missing text\"}");
		}
		UrlDecode (Raw, Dec, sizeof Dec);
		CleanLabel (P.Text, Dec, 22);
	}
	else
	{
		if (!GetField (pFormData, "v", Val, sizeof Val))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"missing value\"}");
		}
		float fValue = ParseFloat (Val);
		if (P.Kind == ParamToggle)
		{
			fValue = fValue > 0.5f ? 1.0f : 0.0f;
		}
		else
		{
			fValue = fValue < P.Min ? P.Min : (fValue > P.Max ? P.Max : fValue);
			if (P.Kind == ParamChoice)
			{
				fValue = (float) (int) (fValue + 0.5f);
			}
		}
		P.Value = fValue;
	}
	P.Changed = TRUE;

	return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
}

// pedalboard operations: op = add | del | ty | mv | lk | ul | mvio | bpset | set | file | test
static void UrlDecode (const char *pIn, char *pOut, unsigned nSize);

// text allowed for the name and password of the fallback network: length within limits and only letters, digits, _ - .
static boolean ApTextOk (const char *p, unsigned nMin, unsigned nMax)
{
	unsigned n = 0;
	for (; p[n] != 0; n++)
	{
		const char c = p[n];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
		{
			return FALSE;
		}
	}
	return n >= nMin && n <= nMax;
}

// API numbers: ParseFloat rejects nothing (any text becomes 0), so we first check that it really is a number
static boolean ValidNumber (const char *p, boolean bInt)
{
	const char *q = p;
	if (*q == '+' || *q == '-')
	{
		q++;
	}
	unsigned nDig = 0;
	while (*q >= '0' && *q <= '9')
	{
		q++;
		nDig++;
	}
	if (!bInt && *q == '.')
	{
		q++;
		while (*q >= '0' && *q <= '9')
		{
			q++;
			nDig++;
		}
	}
	if (!bInt && nDig > 0 && (*q == 'e' || *q == 'E'))
	{
		q++;
		if (*q == '+' || *q == '-')
		{
			q++;
		}
		unsigned nExp = 0;
		while (*q >= '0' && *q <= '9')
		{
			q++;
			nExp++;
		}
		if (nExp == 0)
		{
			return FALSE;
		}
	}
	if (nDig == 0 || *q != 0 || strlen (p) > 14 || (bInt && nDig > 7))
	{
		return FALSE;
	}
	if (!bInt)
	{
		const float f = ParseFloat (p);
		return f > -1.0e9f && f < 1.0e9f;		// no infinities or huge values (and the comparison also discards NaN)
	}
	return TRUE;
}

// decimal number of the API with an optional exponent (ParseFloat would stop at the 'e'): strict syntax, no infinities, no huge values
static boolean ParseNum (const char *p, float *pOut)
{
	char M[24];
	unsigned n = 0;
	const char *q = p;
	if (*q == '+')
	{
		q++;
	}
	else if (*q == '-')
	{
		M[n++] = *q++;
	}
	unsigned nDig = 0;
	while (*q >= '0' && *q <= '9')
	{
		if (n >= 20)
		{
			return FALSE;
		}
		M[n++] = *q++;
		nDig++;
	}
	if (*q == '.')
	{
		M[n++] = *q++;
		while (*q >= '0' && *q <= '9')
		{
			if (n >= 20)
			{
				return FALSE;
			}
			M[n++] = *q++;
			nDig++;
		}
	}
	if (nDig == 0)
	{
		return FALSE;
	}
	M[n] = 0;
	int nExp = 0;
	if (*q == 'e' || *q == 'E')
	{
		q++;
		int nSign = 1;
		if (*q == '+')
		{
			q++;
		}
		else if (*q == '-')
		{
			nSign = -1;
			q++;
		}
		unsigned nExpDig = 0;
		while (*q >= '0' && *q <= '9')
		{
			nExp = nExp * 10 + (*q - '0');
			if (nExp > 1000)
			{
				nExp = 1000;
			}
			q++;
			nExpDig++;
		}
		if (nExpDig == 0)
		{
			return FALSE;
		}
		nExp *= nSign;
	}
	if (*q != 0 || nExp > 9)
	{
		return FALSE;
	}
	float f = ParseFloat (M);
	if (nExp < -9)
	{
		f = 0.0f;		// negligible
	}
	for (; nExp > 0; nExp--)
	{
		f *= 10.0f;
	}
	for (; nExp < 0; nExp++)
	{
		f /= 10.0f;
	}
	if (!(f > -1.0e9f && f < 1.0e9f))
	{
		return FALSE;
	}
	*pOut = f;
	return TRUE;
}

// integer field: *pFound = present; returns FALSE if it is present but is not a valid integer
static boolean FieldInt (const char *pForm, const char *pKey, int *pVal, boolean *pFound)
{
	char T[20];
	*pFound = GetField (pForm, pKey, T, sizeof T);
	if (!*pFound)
	{
		return TRUE;
	}
	if (!ValidNumber (T, TRUE))
	{
		return FALSE;
	}
	*pVal = (int) ParseFloat (T);
	return TRUE;
}

THTTPStatus CControl::HandleBoard (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Op[12], A[PARAM_TEXT_LEN + 8], K[16], V[16];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (!GetField (pFormData, "op", Op, sizeof Op))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
	}

	if (strcmp (Op, "testlock") == 0)
	{
		// lock on changes from the page during an acceptance test: v=1 turns it on (it expires after 15 minutes without operations), v=0 removes it
		char Vv[8];
		const boolean bOn = GetField (pFormData, "v", Vv, sizeof Vv) && Vv[0] == '1';
		m_pContext->TestLock = bOn ? 9000 : 0;
		return Reply (pBuffer, pLength, ppContentType, bOn ? "{\"ok\":1,\"lock\":1}" : "{\"ok\":1,\"lock\":0}");
	}

	if (strcmp (Op, "mode") == 0)
	{
		// there is only one mode (ADELGrid): v = 2 is accepted and does nothing, anything else is not
		char Vm[8];
		if (!GetField (pFormData, "v", Vm, sizeof Vm) || Vm[0] != '2' || Vm[1] != 0)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"ADELFlow and ADELMatrix no longer exist: there is only ADELGrid\"}");
		}
		if (m_pContext->ModePending != MODE_NONE)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"a mode change is still in progress\"}");
		}
		m_pContext->ModePending = (unsigned) (Vm[0] - '0');
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "apset") == 0)
	{
		// fallback network: s = name (1..32 characters), p = password (8..63), c = channel 1..13; letters, digits, _ - . (used at the next boot into the fallback network)
		char Ss[40], Pp[72], Cc[8];
		int nCh = 0;
		boolean bOkCh = FALSE;
		if (!GetField (pFormData, "s", Ss, sizeof Ss) || !GetField (pFormData, "p", Pp, sizeof Pp) || !FieldInt (pFormData, "c", &nCh, &bOkCh) || !bOkCh
		    || !ApTextOk (Ss, 1, 32) || !ApTextOk (Pp, 8, 63) || nCh < 1 || nCh > 13)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"name (1-32) and password (8-63): only letters, digits, _ - . ; channel from 1 to 13\"}");
		}
		(void) Cc;
		CParams::Copy (m_pContext->ApSsid, Ss, sizeof m_pContext->ApSsid);
		CParams::Copy (m_pContext->ApPass, Pp, sizeof m_pContext->ApPass);
		m_pContext->ApChan = (unsigned) nCh;
		m_pContext->ApAction = 1;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "reboot") == 0)
	{
		// reboot of the board (to go back from the fallback network to the home network)
		m_pContext->ApAction = 3;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "di") == 0)
	{
		// DI recorder: v = 1 start (s = seconds, 1..40), 2 stop, 3 free the memory
		int nV = 0, nS = 10;
		boolean bV = FALSE, bS = FALSE;
		if (!FieldInt (pFormData, "v", &nV, &bV) || !bV || nV < 1 || nV > 3)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"v from 1 to 3\"}");
		}
		if (nV == 1)
		{
			if (!FieldInt (pFormData, "s", &nS, &bS) || !bS || nS < 1 || nS > 40)
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"s (seconds) from 1 to 40\"}");
			}
			m_pContext->DiSeconds = (unsigned) nS;
		}
		m_pContext->DiReq = (unsigned) nV;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "concert") == 0)
	{
		// reboot into concert mode: no Wi-Fi or page for a single boot (the pedalboard and MIDI work as always)
		m_pContext->ApAction = 4;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "mcset") == 0)
	{
		// extra worker cores (0..3) from the next boot: v
		int nV = 0;
		boolean bV = FALSE;
		if (!FieldInt (pFormData, "v", &nV, &bV) || !bV || nV < 0 || nV > 3 || !m_pContext->McAvail)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid value or multi-core not available in this version\"}");
		}
		m_pContext->McCfg = (unsigned) nV;
		m_pContext->McDirty = TRUE;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "fandt") == 0)
	{
		// fan PWM test: v = -1 (end) or 0..100 (percentage); the test expires by itself (see FAN_PWM_TEST_SECONDS)
		int nV = 0;
		boolean bV = FALSE;
		if (!FieldInt (pFormData, "v", &nV, &bV) || !bV || nV < -1 || nV > 100)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"v from -1 to 100\"}");
		}
		m_pContext->FanDuty = nV;
		m_pContext->FanDutyNew = TRUE;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "thset") == 0)
	{
		// temperature and fan: fc = 0/1 (I control the fan), on/off = thresholds in degrees, bo = 0/1 (2400 MHz at the next boot)
		int nFc = 0, nOn = 0, nOff = 0, nBo = 0;
		boolean b1 = FALSE, b2 = FALSE, b3 = FALSE, b4 = FALSE;
		if (   !FieldInt (pFormData, "fc", &nFc, &b1) || !FieldInt (pFormData, "on", &nOn, &b2) || !FieldInt (pFormData, "off", &nOff, &b3)
		    || !FieldInt (pFormData, "bo", &nBo, &b4) || !b1 || !b2 || !b3 || !b4
		    || nFc < 0 || nFc > 1 || nBo < 0 || nBo > 1 || nOn < 40 || nOn > 80 || nOff < 30 || nOff > nOn - 2)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"turn-on from 40 to 80 degrees; turn-off from 30 and at least 2 degrees below the turn-on\"}");
		}
		int nMx = (int) m_pContext->FanMaxPct;
		boolean b5 = FALSE;
		if (!FieldInt (pFormData, "mx", &nMx, &b5) || (b5 && (nMx < 35 || nMx > 100)))		// maximum speed of the quiet part (optional)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"maximum speed from 35 to 100 percent\"}");
		}
		if (!b5)
		{
			nMx = (int) m_pContext->FanMaxPct;
		}
		m_pContext->FanMaxPct = (unsigned) nMx;
		m_pContext->FanCtl = (unsigned) nFc;
		m_pContext->FanOnC = (unsigned) nOn;
		m_pContext->FanOffC = (unsigned) nOff;
		m_pContext->BootNext = (unsigned) nBo;
		m_pContext->ThermDirty = TRUE;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "apnow") == 0)
	{
		// reboots the board into the fallback network (to try it); at the next reboot it goes back to looking for the home network
		m_pContext->ApAction = 2;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strcmp (Op, "feat") == 0)
	{
		// service functions: k = name (hum, rumble, limit, mono, air, sat, click, rta, tuner, meter) with v = 0/1, or a parameter (mains, bpm, clickdb, airdb, satdrive, ceil, tmute) with its value
		CFeatures *pF = m_pContext->pFeat;
		char Vf[16];
		float fv = 0.0f;
		if (pF == 0 || !GetField (pFormData, "k", K, sizeof K) || !GetField (pFormData, "v", Vf, sizeof Vf) || !ParseNum (Vf, &fv))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid request\"}");
		}
		const int nId = CFeatures::IdByName (K);
		if (nId >= 0)
		{
			pF->SetOn ((unsigned) nId, fv > 0.5f);
		}
		else if (!pF->SetParam (K, fv))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"unknown function or parameter\"}");
		}
		m_pContext->FeatDirty = TRUE;
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	if (strncmp (Op, "midi", 4) == 0)
	{
		// MIDI: settings, test message, learn, controller links, block CCs
		CBoard *pB = m_pContext->pBoard;
		int a = 0, b = 0, c = 0, d = 0;
		boolean fa = FALSE, fb = FALSE, fc = FALSE, fd = FALSE;
		const char *pE = 0;
		if (   !FieldInt (pFormData, "id", &a, &fa) || !FieldInt (pFormData, "cc", &b, &fb)
		    || !FieldInt (pFormData, "c", &c, &fc) || !FieldInt (pFormData, "k", &d, &fd))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid number\"}");
		}
		if (strcmp (Op, "midiset") == 0)
		{
			int e = 0, sv = 0;
			boolean fe = FALSE, fsy = FALSE;
			if (!FieldInt (pFormData, "e", &e, &fe) || !FieldInt (pFormData, "s", &sv, &fsy))
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid number\"}");
			}
			if (fc)
			{
				if (c < 0 || c > 16)
				{
					return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid MIDI channel (0 = all, 1-16)\"}");
				}
				m_pContext->MidiChan = (unsigned) c;
			}
			if (fe)
			{
				m_pContext->MidiEcho = e != 0;
			}
			if (fsy)
			{
				m_pContext->MidiSyncAdel = sv != 0;
			}
			m_pContext->MidiCfgDirty = TRUE;
		}
		else if (strcmp (Op, "midiinj") == 0)
		{
			// test message: ty 1 PC, 2 CC, 3 Note On, 4 Note Off, 5 Clock, 6 Start, 7 Stop, 8 SysEx status request; ch 0..15; a, b
			int ty = 0, ch = 0, va = 0, vb = 0;
			boolean f1, f2, f3, f4;
			if (!FieldInt (pFormData, "ty", &ty, &f1) || !FieldInt (pFormData, "ch", &ch, &f2) || !FieldInt (pFormData, "a", &va, &f3) || !FieldInt (pFormData, "b", &vb, &f4)
			    || !f1 || ty < 1 || ty > 8 || ch < 0 || ch > 15 || va < 0 || va > 127 || vb < 0 || vb > 127)
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid MIDI message\"}");
			}
			static const u8 Map[9] = {MidiNone, MidiProgram, MidiControl, MidiNoteOn, MidiNoteOff, MidiClock, MidiStart, MidiStop, MidiSysEx};
			TMidiMsg Mm;
			Mm.Type = Map[ty];
			Mm.Chan = (u8) ch;
			Mm.A = (u8) va;
			Mm.B = (u8) vb;
			if (ty == 8)
			{
				Mm.A = 4;
				Mm.B = 1;		// status request
			}
			if (m_pContext->pMidiQ == 0 || !m_pContext->pMidiQ->Push (Mm))
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"MIDI queue full\"}");
			}
		}
		else if (strcmp (Op, "midilearn") == 0)
		{
			// k = 1 controller (id = 0..2), k = 2 block (id = block id), id = -1 cancels
			if (!fa || a < 0)
			{
				pB->Midi ().CancelLearn ();
			}
			else if (fd && d == 1 && a < (int) MIDI_CTL_COUNT)
			{
				pB->Midi ().LearnController (a);
			}
			else if (fd && d == 2 && a < (int) BD_MAX_BLOCKS && pB->Block ((unsigned) a).Used)
			{
				pB->Midi ().LearnBlock (a);
			}
			else
			{
				pE = "learn: invalid controller or block";
			}
		}
		else if (strcmp (Op, "midilink") == 0)
		{
			// c = controller 0..2, k = position 0..2, id = block (-1 removes the link), p = parameter, mn and mx = range in %
			int p = 0;
			boolean fp = FALSE;
			char Tn[16], Tx[16];
			float fMin = 0.0f, fMax = 100.0f;
			if (!FieldInt (pFormData, "p", &p, &fp) || (GetField (pFormData, "mn", Tn, sizeof Tn) && !ParseNum (Tn, &fMin))
			    || (GetField (pFormData, "mx", Tx, sizeof Tx) && !ParseNum (Tx, &fMax)))
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid number\"}");
			}
			if (!fc || c < 0 || c >= (int) MIDI_CTL_COUNT || !fd || d < 0 || d >= (int) MIDI_CTL_LINKS)
			{
				pE = "invalid controller or position";
			}
			else if (!fa || a < 0)
			{
				pB->Midi ().Ctl ((unsigned) c).Link[d].Block = -1;
				pB->MarkEdit ();
			}
			else if (a >= (int) BD_MAX_BLOCKS || !pB->Block ((unsigned) a).Used || p < 0 || p >= (int) FX_MAX_P)
			{
				pE = "invalid block or parameter";
			}
			else
			{
				TMidiLink &L = pB->Midi ().Ctl ((unsigned) c).Link[d];
				L.Block = a;
				L.Param = p;
				L.MinPct = fMin < 0.0f ? 0.0f : (fMin > 100.0f ? 100.0f : fMin);
				L.MaxPct = fMax < 0.0f ? 0.0f : (fMax > 100.0f ? 100.0f : fMax);
				pB->MarkEdit ();
			}
		}
		else if (strcmp (Op, "midicc") == 0)
		{
			// CC of a block (id = block, cc = 32..127; cc = 0 removes it)
			if (!fa || a < 0 || a >= (int) BD_MAX_BLOCKS || !pB->Block ((unsigned) a).Used)
			{
				pE = "invalid block";
			}
			else if (fb && b == 0)
			{
				pB->Midi ().ForgetBlock (a);
				pB->MarkEdit ();
			}
			else if (!fb || !pB->Midi ().SetBlockCc (b, a))
			{
				pE = "invalid CC (32-127) or table full";
			}
			else
			{
				pB->MarkEdit ();
			}
		}
		else if (strcmp (Op, "midictl") == 0)
		{
			// source of a controller (c = 0..2, cc = 32..127; cc = 0 removes it)
			if (!fc || c < 0 || c >= (int) MIDI_CTL_COUNT || !fb || (b != 0 && (b < MIDI_CC_FIRST_FREE || b > 127)))
			{
				pE = "invalid controller or CC";
			}
			else
			{
				pB->Midi ().Ctl ((unsigned) c).Cc = b;
				if (c == 2)
				{
					m_pContext->MidiCtl3Cc = (unsigned) b;		// global controller 3: the source is saved in midi.cfg, not in the preset
					m_pContext->MidiCfgDirty = TRUE;
				}
				else
				{
					pB->MarkEdit ();
				}
			}
		}
		else
		{
			pE = "unknown MIDI operation";
		}
		if (pE != 0)
		{
			CBuf Be (pBuffer, *pLength);
			Be.S ("{\"ok\":0,\"err\":");
			Be.Esc (pE);
			Be.C ('}');
			*pLength = Be.Length ();
			*ppContentType = "application/json";
			return HTTPOK;
		}
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
	}

	CBoard *pBoard = m_pContext->pBoard;
	const char *pErr = 0;
	int nNewId = -1;
	char Msg[96];
	int nId = -1;
	boolean bFound = FALSE;
	if (!FieldInt (pFormData, "id", &nId, &bFound))
	{
		nId = -1000000;		// non-numeric id: no such block
	}
	float fValue = 0.0f;
	if (GetField (pFormData, "v", V, sizeof V))
	{
		if (!ParseNum (V, &fValue))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid value\"}");
		}
	}

	if (strcmp (Op, "add") == 0)
	{
		char Type[24];
		if (!GetField (pFormData, "k", Type, sizeof Type))
		{
			pErr = "incomplete request";
		}
		else
		{
			// starting model of a new NAM: the first favorite, otherwise the first in the library
			const char *pFile = "";
			if (*m_pContext->pLibCount > 0)
			{
				pFile = m_pContext->pLib[0];
				for (unsigned i = 0; i < *m_pContext->pFavCount; i++)
				{
					pFile = m_pContext->pFav[i];
					break;
				}
			}
			if (strcmp (Type, "cabir") == 0)
				{
					pFile = *m_pContext->pIrCount > 0 ? m_pContext->pIr[0] : "";		// the first IR (none = the signal passes through)
				}
				if (strcmp (Type, "nam") == 0 && pFile[0] == 0)
			{
				pErr = "the model library is empty";
			}
			else
			{
				int nPos = -1, nRow = 0;
				boolean bP = FALSE, bR = FALSE;
				if (!FieldInt (pFormData, "p", &nPos, &bP) || !FieldInt (pFormData, "r", &nRow, &bR))
				{
					pErr = "invalid position";
				}
				else
				{
					pErr = pBoard->NetAdd (Type, nRow, nPos, pFile, &nNewId);
				}
			}
		}
	}
	else if (strcmp (Op, "del") == 0)
	{
		pErr = pBoard->NetRemove (nId);
	}
	else if (strcmp (Op, "ty") == 0)
	{
		// changes the block's type in its cell (Grid)
		char Type[24];
		if (!GetField (pFormData, "k", Type, sizeof Type))
		{
			pErr = "incomplete request";
		}
		else
		{
			const char *pFile = "";
			if (*m_pContext->pLibCount > 0)
			{
				pFile = m_pContext->pLib[0];
				for (unsigned i = 0; i < *m_pContext->pFavCount; i++)
				{
					pFile = m_pContext->pFav[i];
					break;
				}
			}
			if (strcmp (Type, "cabir") == 0)
			{
				pFile = *m_pContext->pIrCount > 0 ? m_pContext->pIr[0] : "";
			}
			if (strcmp (Type, "nam") == 0 && pFile[0] == 0)
			{
				pErr = "the model library is empty";
			}
			else
			{
				pErr = pBoard->NetRetype (nId, Type, pFile);
			}
		}
	}
	else if (strcmp (Op, "mv") == 0)
	{
		// Grid: cell (row r, column p); Matrix: position p in the list
		int nRow = 0, nCol = 0;
		boolean bR = FALSE, bP = FALSE;
		if (!FieldInt (pFormData, "r", &nRow, &bR) || !FieldInt (pFormData, "p", &nCol, &bP) || !bR || !bP)
		{
			pErr = "incomplete request or invalid numbers";
		}
		else
		{
			pErr = pBoard->NetMove (nId, nRow, nCol);
		}
	}
	else if (strcmp (Op, "lk") == 0 || strcmp (Op, "ul") == 0)
	{
		// link: fr = source (-1 = input), to = destination (-2 = output), pf/pt = ports (0 stereo, 1 L, 2 R), v = dB
		int nFr = 0, nTo = 0, nPf = 0, nPt = 0;
		boolean bFr = FALSE, bTo = FALSE, bPf = FALSE, bPt = FALSE;
		if (!FieldInt (pFormData, "fr", &nFr, &bFr) || !FieldInt (pFormData, "to", &nTo, &bTo) || !FieldInt (pFormData, "pf", &nPf, &bPf) || !FieldInt (pFormData, "pt", &nPt, &bPt)
		    || !bFr || !bTo || !bPf || !bPt)
		{
			pErr = "incomplete request or invalid numbers";
		}
		else if (Op[0] == 'l')
		{
			pErr = pBoard->NetLink (nFr, nTo, nPf, nPt, fValue);
		}
		else
		{
			pErr = pBoard->NetUnlink (nFr, nTo, nPf, nPt);
		}
	}
	else if (strcmp (Op, "relayout") == 0)
	{
		// automatic reordering of the blocks (positions only): s = "id.row.column_id.row.column_..." for ALL the blocks, io = "row.column_" for Input L, Input R, Output L, Output R
		static char Sb[700], Ib[96];
		int Nb[3 * BD_MAX_BLOCKS], Ni[8];
		unsigned nNb = 0, nNi = 0;
		boolean bOk = GetField (pFormData, "s", Sb, sizeof Sb) && GetField (pFormData, "io", Ib, sizeof Ib);
		for (unsigned pass = 0; pass < 2 && bOk; pass++)
		{
			const char *p = pass == 0 ? Sb : Ib;
			int *pOut = pass == 0 ? Nb : Ni;
			const unsigned nMax = pass == 0 ? 3 * BD_MAX_BLOCKS : 8;
			unsigned nCount = 0;
			while (*p && bOk)
			{
				if (*p < '0' || *p > '9' || nCount >= nMax)
				{
					bOk = FALSE;
					break;
				}
				int v = 0;
				while (*p >= '0' && *p <= '9' && v < 100000)
				{
					v = v * 10 + (*p++ - '0');
				}
				pOut[nCount++] = v;
				if (*p == '.' || *p == '_')
				{
					p++;
				}
			}
			(pass == 0 ? nNb : nNi) = nCount;
		}
		if (!bOk || nNb % 3 != 0 || nNi != 8)
		{
			pErr = "incomplete request or invalid numbers";
		}
		else
		{
			int Id[BD_MAX_BLOCKS], Row[BD_MAX_BLOCKS], Col[BD_MAX_BLOCKS], IoR[4], IoC[4];
			for (unsigned k = 0; k < nNb / 3; k++)
			{
				Id[k] = Nb[3 * k];
				Row[k] = Nb[3 * k + 1];
				Col[k] = Nb[3 * k + 2];
			}
			for (unsigned w = 0; w < 4; w++)
			{
				IoR[w] = Ni[2 * w];
				IoC[w] = Ni[2 * w + 1];
			}
			pErr = pBoard->NetRelayout (Id, Row, Col, nNb / 3, IoR, IoC);
		}
	}
	else if (strcmp (Op, "mvio") == 0)
	{
		// Grid: moves Input L (w = 0), Input R (1), Output L (2) or Output R (3) to the cell (row r, column p)
		int nW = 0, nRow = 0, nCol = 0;
		boolean bW = FALSE, bR = FALSE, bP = FALSE;
		if (!FieldInt (pFormData, "w", &nW, &bW) || !FieldInt (pFormData, "r", &nRow, &bR) || !FieldInt (pFormData, "p", &nCol, &bP) || !bW || !bR || !bP)
		{
			pErr = "incomplete request or invalid numbers";
		}
		else
		{
			pErr = pBoard->NetMoveIo (nW, nRow, nCol);
		}
	}
	else if (strcmp (Op, "bpset") == 0)
	{
		char Raw[BD_PRESET_LEN * 3 + 8], Name[BD_PRESET_LEN + 4];
		if (!GetField (pFormData, "s", Raw, sizeof Raw))
		{
			Raw[0] = 0;
		}
		UrlDecode (Raw, Name, sizeof Name);
		pErr = pBoard->SetPreset (nId, Name);
	}
	else if (strcmp (Op, "set") == 0)
	{
		if (!GetField (pFormData, "k", K, sizeof K))
		{
			pErr = "incomplete request";
		}
		else
		{
			pErr = pBoard->SetValue (nId, K, fValue);
		}
	}
	else if (strcmp (Op, "file") == 0)
	{
		if (!GetField (pFormData, "s", A, sizeof A))
		{
			pErr = "missing name";
		}
		else
		{
			boolean bInLib = FALSE;
			const boolean bCab = nId >= 0 && nId < (int) BD_MAX_BLOCKS && pBoard->Block ((unsigned) nId).Used && pBoard->Block ((unsigned) nId).Type == FxCabIR;
			const unsigned nCount = bCab ? *m_pContext->pIrCount : *m_pContext->pLibCount;
			for (unsigned i = 0; i < nCount; i++)
			{
				if (strcmp (bCab ? m_pContext->pIr[i] : m_pContext->pLib[i], A) == 0)
				{
					bInLib = TRUE;
				}
			}
			pErr = bInLib ? pBoard->SetFile (nId, A) : "file not in the library";
		}
	}
	else if (strcmp (Op, "test") == 0)
	{
		// internal test source (for checks): 0 = off, 1 = tone on L, 2 = tone on R, 3 = both
		m_pContext->TestMask = (unsigned) fValue & 3;
		m_pContext->TestPending = TRUE;
	}
	else
	{
		pErr = "unknown operation";
	}

	CBuf B ((u8 *) Msg, sizeof Msg);
	if (pErr != 0)
	{
		// the message is a string of ours, with no characters to protect
		CBuf R (pBuffer, *pLength);
		R.S ("{\"ok\":0,\"err\":");
		R.Esc (pErr);
		R.C ('}');
		*pLength = R.Length ();
		*ppContentType = "application/json";
		return HTTPOK;
	}
	(void) B;
	CBuf R (pBuffer, *pLength);
	R.S ("{\"ok\":1,\"id\":");
	R.I ((unsigned) (nNewId < 0 ? 0 : nNewId));
	R.C ('}');
	*pLength = R.Length ();
	*ppContentType = "application/json";
	return HTTPOK;
}

// url decoding (%XX and +)
static void UrlDecode (const char *pIn, char *pOut, unsigned nSize)
{
	unsigned i = 0;
	while (*pIn && i + 1 < nSize)
	{
		if (*pIn == '+')
		{
			pOut[i++] = ' ';
			pIn++;
		}
		else if (pIn[0] == '%' && ((pIn[1] >= '0' && pIn[1] <= '9') || (pIn[1] >= 'A' && pIn[1] <= 'F') || (pIn[1] >= 'a' && pIn[1] <= 'f'))
			 && ((pIn[2] >= '0' && pIn[2] <= '9') || (pIn[2] >= 'A' && pIn[2] <= 'F') || (pIn[2] >= 'a' && pIn[2] <= 'f')))
		{
			unsigned v = 0;
			for (unsigned k = 1; k <= 2; k++)
			{
				const char c = pIn[k];
				v = v * 16 + (unsigned) (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
			}
			pOut[i++] = (char) v;
			pIn += 3;
		}
		else
		{
			pOut[i++] = *pIn++;
		}
	}
	pOut[i] = 0;
}

// url-safe base64 without padding (A-Z a-z 0-9 - _): returns the bytes written or -1
static int B64UrlDecode (const char *p, u8 *pOut, unsigned nMax)
{
	unsigned nAcc = 0, nBits = 0, n = 0;
	for (; *p; p++)
	{
		const char c = *p;
		unsigned v;
		if (c >= 'A' && c <= 'Z')
		{
			v = (unsigned) (c - 'A');
		}
		else if (c >= 'a' && c <= 'z')
		{
			v = (unsigned) (c - 'a') + 26;
		}
		else if (c >= '0' && c <= '9')
		{
			v = (unsigned) (c - '0') + 52;
		}
		else if (c == '-')
		{
			v = 62;
		}
		else if (c == '_')
		{
			v = 63;
		}
		else
		{
			return -1;
		}
		nAcc = (nAcc << 6) | v;
		nBits += 6;
		if (nBits >= 8)
		{
			nBits -= 8;
			if (n >= nMax)
			{
				return -1;
			}
			pOut[n++] = (u8) ((nAcc >> nBits) & 0xFF);
		}
	}
	return (int) n;
}

// name for the upload: NAME.adn (model) or NAME.inf (author and license); NAME = letters, digits, _ and -, at most 40
static boolean UpValidName (const char *p)
{
	const unsigned n = strlen (p);
	if (n < 5 || n > 44)
	{
		return FALSE;
	}
	if (strcmp (p + n - 4, ".adn") != 0 && strcmp (p + n - 4, ".inf") != 0 && strcmp (p + n - 4, ".air") != 0)
	{
		return FALSE;
	}
	for (unsigned i = 0; i + 4 < n; i++)
	{
		const char c = p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
		{
			return FALSE;
		}
	}
	return TRUE;
}

// upload of a file in pieces (the server request is limited to 2 KB): n = name, o = position, z = total size, d = data (url-safe base64)
THTTPStatus CControl::HandleUp (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Name[56], Off[16], Tot[16], Data[2048];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (   !GetField (pFormData, "n", Name, sizeof Name) || !GetField (pFormData, "o", Off, sizeof Off)
	    || !GetField (pFormData, "z", Tot, sizeof Tot) || !GetField (pFormData, "d", Data, sizeof Data))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
	}
	if (!UpValidName (Name))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid file name\"}");
	}
	const unsigned nOff = (unsigned) ParseFloat (Off), nTotal = (unsigned) ParseFloat (Tot);
	const boolean bInf = strcmp (Name + strlen (Name) - 4, ".inf") == 0;
	if (nTotal < 1 || nTotal > (bInf ? (unsigned) UP_INF_MAX : (unsigned) UP_MAX))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"size not accepted (models can weigh at most 256 KB)\"}");
	}
	TCtlContext *pC = m_pContext;
	if (nOff == 0)
	{
		if (pC->UpPending != 0)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"another upload is still in progress\"}");
		}
		CParams::Copy (pC->UpName, Name, sizeof pC->UpName);
		pC->UpTotal = nTotal;
		pC->UpGot = 0;
		CParams::Copy (pC->UpResName, Name, sizeof pC->UpResName);
		pC->UpResOk = 0;
		pC->UpResMsg[0] = 0;
	}
	else if (strcmp (pC->UpName, Name) != 0 || nOff != pC->UpGot || nTotal != pC->UpTotal || pC->UpPending != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"chunk out of sequence\"}");
	}
	const int nLen = B64UrlDecode (Data, pC->pUpBuf + nOff, UP_MAX + 16 - nOff);
	if (nLen < 0 || nOff + (unsigned) nLen > nTotal)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid data\"}");
	}
	pC->UpGot = nOff + (unsigned) nLen;
	if (pC->UpGot == pC->UpTotal)
	{
		pC->UpPending = 1;
	}
	return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
}

// pedalboard presets: op = save | load | del, n = name. The operation is run by the main loop (it is the only one that uses the card)
THTTPStatus CControl::HandlePre (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Op[8], RawN[BP_NAME_LEN * 3 + 8], Name[BP_NAME_LEN + 4];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (!GetField (pFormData, "op", Op, sizeof Op) || !GetField (pFormData, "n", RawN, sizeof RawN))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
	}
	UrlDecode (RawN, Name, sizeof Name);
	if (!BpValidName (Name))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid name (1-31 characters, no quotation marks or backslashes)\"}");
	}
	if (m_pContext->PrePending != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"a preset operation is still in progress\"}");
	}

	unsigned nPreMode = m_pContext->Mode;
	{
		char Mm[8];
		if (GetField (pFormData, "m", Mm, sizeof Mm) && Mm[0] >= '0' && Mm[0] <= '2' && Mm[1] == 0)
		{
			nPreMode = (unsigned) (Mm[0] - '0');
		}
	}
	int nAt = -1, nFree = -1;
	for (unsigned i = 0; i < PRE_MAX; i++)
	{
		if (m_pContext->pPreName[i][0] == 0)
		{
			if (nFree < 0)
			{
				nFree = (int) i;
			}
		}
		else if (strcmp (m_pContext->pPreName[i], Name) == 0 && m_pContext->pPreMode[i] == nPreMode)
		{
			nAt = (int) i;
		}
	}

	unsigned nOp;
	if (strcmp (Op, "save") == 0)
	{
		if (nAt < 0 && m_pContext->PreFull)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"too many presets: delete some\"}");
		}
		nOp = 1;
	}
	else if (strcmp (Op, "load") == 0 || strcmp (Op, "del") == 0)
	{
		if (nAt < 0)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"no such preset\"}");
		}
		nOp = Op[0] == 'l' ? 2 : 3;
	}
	else
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"unknown operation\"}");
	}
	CParams::Copy (m_pContext->PreArg, Name, BP_NAME_LEN);
	m_pContext->PreArgMode = nPreMode;
	m_pContext->PrePending = nOp;
	return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
}

// saved block settings: op = save (n = name, d = data) | del (n = name)
THTTPStatus CControl::HandleBp (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Op[8], RawN[BP_NAME_LEN * 3 + 8], RawD[BP_DATA_LEN * 3 + 8], Name[BP_NAME_LEN + 4], Data[BP_DATA_LEN + 4];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (!GetField (pFormData, "op", Op, sizeof Op) || !GetField (pFormData, "n", RawN, sizeof RawN))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
	}
	UrlDecode (RawN, Name, sizeof Name);
	if (!BpValidName (Name))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid name (1-31 characters, no quotation marks or backslashes)\"}");
	}

	unsigned nCount = *m_pContext->pBpCount;
	int nAt = -1;
	for (unsigned i = 0; i < nCount; i++)
	{
		if (strcmp (m_pContext->pBpName[i], Name) == 0)
		{
			nAt = (int) i;
		}
	}

	if (strcmp (Op, "save") == 0)
	{
		if (!GetField (pFormData, "d", RawD, sizeof RawD))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
		}
		UrlDecode (RawD, Data, sizeof Data);
		if (!BpValidData (Data))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid data\"}");
		}
		if (nAt < 0)
		{
			if (nCount >= BP_MAX)
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"too many saved settings: delete some\"}");
			}
			nAt = (int) nCount;
			CParams::Copy (m_pContext->pBpName[nAt], Name, BP_NAME_LEN);
			CParams::Copy (m_pContext->pBpData[nAt], Data, BP_DATA_LEN);
			*m_pContext->pBpCount = nCount + 1;
		}
		else
		{
			CParams::Copy (m_pContext->pBpData[nAt], Data, BP_DATA_LEN);
		}
	}
	else if (strcmp (Op, "del") == 0)
	{
		if (nAt < 0)
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"no such setting\"}");
		}
		for (unsigned i = (unsigned) nAt; i + 1 < nCount; i++)
		{
			memcpy (m_pContext->pBpName[i], m_pContext->pBpName[i + 1], BP_NAME_LEN);
			memcpy (m_pContext->pBpData[i], m_pContext->pBpData[i + 1], BP_DATA_LEN);
		}
		*m_pContext->pBpCount = nCount - 1;
	}
	else
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"unknown operation\"}");
	}
	m_pContext->BpDirty = TRUE;
	return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
}

// favorites: s = library file name, v = 1 adds, 0 removes
THTTPStatus CControl::HandleFav (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Name[PARAM_TEXT_LEN + 8], Val[8];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (!GetField (pFormData, "s", Name, sizeof Name) || !GetField (pFormData, "v", Val, sizeof Val))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"incomplete request\"}");
	}

	boolean bInLib = FALSE;
	for (unsigned i = 0; i < *m_pContext->pLibCount; i++)
	{
		if (strcmp (m_pContext->pLib[i], Name) == 0)
		{
			bInLib = TRUE;
		}
	}
	if (!bInLib)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"file not in the library\"}");
	}

	unsigned nCount = *m_pContext->pFavCount;
	int nAt = -1;
	for (unsigned i = 0; i < nCount; i++)
	{
		if (strcmp (m_pContext->pFav[i], Name) == 0)
		{
			nAt = (int) i;
		}
	}

	if (Val[0] == '1')
	{
		if (nAt < 0)
		{
			if (nCount >= LIB_MAX_FILES)
			{
				return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"too many favorites\"}");
			}
			CParams::Copy (m_pContext->pFav[nCount], Name, PARAM_TEXT_LEN);
			*m_pContext->pFavCount = nCount + 1;
		}
	}
	else if (nAt >= 0)
	{
		for (unsigned i = (unsigned) nAt; i + 1 < nCount; i++)
		{
			memcpy (m_pContext->pFav[i], m_pContext->pFav[i + 1], PARAM_TEXT_LEN);
		}
		*m_pContext->pFavCount = nCount - 1;
	}
	m_pContext->FavDirty = TRUE;

	return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
}

// deletion of a model from the library: only with the code, only library files, never a file in use by a lane
THTTPStatus CControl::HandleDelete (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType)
{
	char Token[16], Name[PARAM_TEXT_LEN + 8];

	if (   !GetField (pFormData, "t", Token, sizeof Token)
	    || strcmp (Token, m_pContext->Token) != 0)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"invalid code: reload the page\"}");
	}
	if (!GetField (pFormData, "s", Name, sizeof Name))
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"missing name\"}");
	}

	boolean bFound = FALSE;
	for (unsigned i = 0; i < *m_pContext->pLibCount; i++)
	{
		if (strcmp (m_pContext->pLib[i], Name) == 0)
		{
			bFound = TRUE;
		}
	}
	boolean bIr = FALSE;
	for (unsigned i = 0; i < *m_pContext->pIrCount; i++)
	{
		if (strcmp (m_pContext->pIr[i], Name) == 0)
		{
			bFound = TRUE;
			bIr = TRUE;
		}
	}
	if (!bFound)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"file not in the library\"}");
	}

	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		const TBdBlock &Bl = m_pContext->pBoard->Block (id);
		if (Bl.Used && Bl.Type == (bIr ? (u8) FxCabIR : (u8) NodeNam) && strcmp (Bl.File, Name) == 0)
		{
			return Reply (pBuffer, pLength, ppContentType, bIr
				? "{\"ok\":0,\"err\":\"the IR is in use by a Cab IR block: choose another one before deleting it\"}"
				: "{\"ok\":0,\"err\":\"the model is in use by a NAM block: choose another one before deleting it\"}");
		}
	}

	if (m_pContext->DelPending)
	{
		return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"another deletion is in progress, try again\"}");
	}
	CParams::Copy (m_pContext->DelName, Name, sizeof m_pContext->DelName);
	m_pContext->DelPending = TRUE;

	return Reply (pBuffer, pLength, ppContentType, "{\"ok\":1}");
}

// block in JSON, in a one-element list (the format the page reads)
static void WriteChain (CBuf &B, const CBoard &Bd, int nFirst)
{
	B.C ('[');
	boolean bFirst = TRUE;
	for (unsigned k = 0; nFirst >= 0 && nFirst < (int) BD_MAX_BLOCKS && k < BD_MAX_BLOCKS; k++)
	{
		const TBdBlock &Bl = Bd.Block ((unsigned) nFirst);
		if (!Bl.Used)
		{
			break;
		}
		if (!bFirst)
		{
			B.C (',');
		}
		bFirst = FALSE;
		B.S ("{\"id\":");
		B.I ((unsigned) nFirst);
		B.S (",\"on\":");
			B.I (Bl.On ? 1 : 0);
			if (Bl.Preset[0] != 0)
			{
				B.S (",\"pr\":");
				B.Esc (Bl.Preset);
			}
		if (Bl.Type == NodeNam)
		{
			B.S (",\"t\":\"nam\",\"in\":");
			B.F (Bl.V[0], 1);
			B.S (",\"out\":");
			B.F (Bl.V[1], 1);
			B.S (",\"bass\":");
			B.F (Bl.V[2], 1);
			B.S (",\"mid\":");
			B.F (Bl.V[3], 1);
			B.S (",\"treb\":");
			B.F (Bl.V[4], 1);
			B.S (",\"noofs\":");
			B.I (Bl.V[5] > 0.5f ? 1 : 0);
			B.S (",\"file\":");
			B.Esc (Bl.File);
			B.S (",\"name\":");
			B.Esc (Bl.Name);
			B.S (",\"loaded\":");
			B.S (Bl.Loaded ? "true" : "false");
			B.S (",\"status\":");
			B.Esc (Bl.Status);
		}
		else if (Bl.Type == NodeVolume)
		{
			B.S (",\"t\":\"vol\",\"rt\":");
				B.I (Bl.V[1] > 0.5f ? 1 : 0);
				B.S (",\"vol\":");
			B.F (Bl.V[0], 1);
		}
		else if (Bl.Type == NodePan)
		{
			B.S (",\"t\":\"pan\",\"pan\":");
			B.F (Bl.V[0], 0);
		}
		else if (const TFxDef *pDef = FxDefByType (Bl.Type))
		{
			B.S (",\"t\":\"fx\",\"fx\":");
			B.Esc (pDef->Name);
			B.S (",\"v\":[");
			for (unsigned k = 0; k < pDef->nParams; k++)
			{
				if (k > 0)
				{
					B.C (',');
				}
				B.F (Bl.V[k], 2);
			}
			B.C (']');
			if (Bl.Type == FxCabIR)
			{
				B.S (",\"file\":");
				B.Esc (Bl.File);
				B.S (",\"status\":");
				B.Esc (Bl.Status);
			}
		}
		B.C ('}');
		break;
	}
	B.C (']');
}

THTTPStatus CControl::GetContent (const char  *pPath,
				  const char  *pParams,
				  const char  *pFormData,
				  u8	      *pBuffer,
				  unsigned    *pLength,
				  const char **ppContentType)
{
	m_pContext->pStatus->nRequests++;
	// counts the requests started and finished: if one does not finish, the main loop notices
	struct TDone
	{
		TCtlStatus *p;
		explicit TDone (TCtlStatus *pS) : p (pS) { p->nReqStart++; }
		~TDone (void) { p->nReqDone++; }
	} ReqGuard (m_pContext->pStatus);
	if (pBuffer == 0 || *pLength < 1024)
	{
		CLogger::Get ()->Write (FromControl, LogError, "invalid reply buffer (%p, %u)", pBuffer, *pLength);
		return HTTPInternalServerError;
	}

	// acceptance test in progress: the requests that modify something and come from the page (field pg=1) are rejected; those of the test
	// (without pg) stay valid and renew the lock, which anyway expires after 15 minutes without operations
	if (   m_pContext->TestLock > 0
	    && (   strcmp (pPath, "/set") == 0 || strcmp (pPath, "/g") == 0 || strcmp (pPath, "/up") == 0 || strcmp (pPath, "/pre") == 0
		|| strcmp (pPath, "/bp") == 0 || strcmp (pPath, "/fav") == 0 || strcmp (pPath, "/delete") == 0))
	{
		char Pg[4];
		if (GetField (pFormData, "pg", Pg, sizeof Pg))
		{
			return Reply (pBuffer, pLength, ppContentType, "{\"ok\":0,\"err\":\"Acceptance test in progress: changes from the page are blocked until the end of the test\"}");
		}
		m_pContext->TestLock = 9000;
	}

	if (strcmp (pPath, "/set") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandleSet (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/g") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandleBoard (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/up") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandleUp (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/pre") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandlePre (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/bp") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandleBp (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/fav") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandleFav (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/delete") == 0)
	{
		if (pFormData[0] == 0)
		{
			return HTTPBadRequest;
		}
		return HandleDelete (pFormData, pBuffer, pLength, ppContentType);
	}

	if (strcmp (pPath, "/") == 0 || strcmp (pPath, "/index.html") == 0)
	{
		// the page is compressed with gzip at build time (control_page_gz.h, from control_page.h): the browser decompresses it by itself.
		// The startup code cannot be inside the compressed bytes: the page takes it from /t.js
		if (*pLength < sizeof s_PageGz)
		{
			CLogger::Get ()->Write (FromControl, LogError, "page too large for the buffer");
			return HTTPInternalServerError;
		}
		memcpy (pBuffer, s_PageGz, sizeof s_PageGz);
		*pLength = sizeof s_PageGz;
		*ppContentType = "text/html; charset=utf-8\r\nContent-Encoding: gzip\r\nCache-Control: no-cache";
		return HTTPOK;
	}

	if (strcmp (pPath, "/t.js") == 0)
	{
		// startup code for the page and for the tools: window.NB_T="..."
		CBuf B (pBuffer, *pLength);
		B.S ("window.NB_T=\"");
		B.S (m_pContext->Token);
		B.S ("\";\n");
		*pLength = B.Length ();
		*ppContentType = "application/javascript\r\nCache-Control: no-store";
		return HTTPOK;
	}

	if (strcmp (pPath, "/mc") == 0)
	{
		// multi-core: available, requested for the next boot, active cores, blocks processed with several cores, faults
		CBuf B (pBuffer, *pLength);
		B.S ("{\"avail\":");
		B.I (m_pContext->McAvail ? 1 : 0);
		B.S (",\"cfg\":");
		B.I (m_pContext->McCfg);
		B.S (",\"cores\":");
		B.I (m_pContext->McActive);
		B.S (",\"blocks\":");
		B.I (GraphMcBlocks ());
		B.S (",\"faults\":");
		B.I (GraphMcFaults ());
		B.S (",\"late\":");
		B.I (GraphMcLate ());
		B.S (",\"maxus\":");
		B.I (GraphMcMaxUs ());
		B.C ('}');
		*pLength = B.Length ();
		*ppContentType = "application/json\r\nCache-Control: no-store";
		return HTTPOK;
	}

	if (strcmp (pPath, "/therm") == 0)
	{
		// temperature (tenths of a degree), frequency (current, minimum, maximum), fan and settings
		CBuf B (pBuffer, *pLength);
		B.S ("{\"temp\":");
		B.I (m_pContext->CpuTemp);
		B.S (",\"mhz\":[");
		B.I (m_pContext->CpuMhz[0]);
		B.C (',');
		B.I (m_pContext->CpuMhz[1]);
		B.C (',');
		B.I (m_pContext->CpuMhz[2]);
		B.S ("],\"fan\":");
		B.I (m_pContext->CpuFan);
		B.S (",\"fanctl\":");
		B.I (m_pContext->FanCtl);
		B.S (",\"on\":");
		B.I (m_pContext->FanOnC);
		B.S (",\"off\":");
		B.I (m_pContext->FanOffC);
		B.S (",\"boost\":");
		B.I (m_pContext->BootNext);
		B.S (",\"da\":");
		B.I (m_pContext->DmaAlarms);
		B.S (",\"mx\":");
		B.I (m_pContext->FanMaxPct);
		B.S (",\"fp\":");
		B.I (m_pContext->FanPct);
		B.S (",\"duty\":");
		if (m_pContext->FanDuty >= 0)
		{
			B.I ((unsigned) m_pContext->FanDuty);
		}
		else
		{
			B.S ("-1");
		}
		B.C ('}');
		*pLength = B.Length ();
		*ppContentType = "application/json\r\nCache-Control: no-store";
		return HTTPOK;
	}

	if (strcmp (pPath, "/ap") == 0)
	{
		// fallback network: name, password and channel in use in the access point, and whether the board is an access point right now
		CBuf B (pBuffer, *pLength);
		B.S ("{\"active\":");
		B.I (m_pContext->ApActive ? 1 : 0);
		B.S (",\"ssid\":");
		B.Esc (m_pContext->ApSsid);
		B.S (",\"pass\":");
		B.Esc (m_pContext->ApPass);
		B.S (",\"ch\":");
		B.I (m_pContext->ApChan);
		B.S (",\"wait\":");
		B.I (STA_WAIT_SECONDS);
		B.S (",\"log\":");
		B.Esc (m_pContext->ApLog);
		B.S (",\"drv\":");
		B.Esc (m_pContext->ApDrv);
		B.C ('}');
		*pLength = B.Length ();
		*ppContentType = "application/json\r\nCache-Control: no-store";
		return HTTPOK;
	}

	if (strcmp (pPath, "/feat") == 0)
	{
		// state of the service functions: switches, parameters and analysis results
		CFeatures *pF = m_pContext->pFeat;
		CBuf B (pBuffer, *pLength);
		B.S ("{\"on\":[");
		for (unsigned i = 0; i < FT_COUNT; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.I (pF != 0 && pF->On (i) ? 1 : 0);
		}
		B.S ("],\"names\":[");
		for (unsigned i = 0; i < FT_COUNT; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.Esc (CFeatures::Name (i));
		}
		B.C (']');
		static const char *const pk[] = {"mains", "bpm", "clickdb", "airdb", "satdrive", "ceil", "tmute"};
		for (unsigned i = 0; i < 7; i++)
		{
			B.S (",\"");
			B.S (pk[i]);
			B.S ("\":");
			B.F (pF != 0 ? pF->Param (pk[i]) : 0.0f, 0);
		}
		if (pF != 0)
		{
			B.S (",\"gr\":");
			B.F (pF->LimiterReductionDb (), 1);
			B.S (",\"lufs\":");
			B.F (pF->LufsMomentary (), 1);
			B.S (",\"peak\":");
			B.F (pF->PeakDb (), 1);
			B.S (",\"crest\":");
			B.F (pF->CrestDb (), 1);
			B.S (",\"corr\":");
			B.F (pF->Correlation (), 2);
			B.S (",\"tun\":");
			if (pF->TunerValid ())
			{
				char Nm[8];
				CFeatures::NoteName (pF->TunerMidi (), Nm);
				B.S ("{\"hz\":");
				B.F (pF->TunerHz (), 1);
				B.S (",\"note\":");
				B.Esc (Nm);
				B.S (",\"cents\":");
				B.F (pF->TunerCents (), 0);
				B.C ('}');
			}
			else
			{
				B.S ("null");
			}
			B.S (",\"rta\":[");
			for (unsigned k = 0; k < FT_RTA_BANDS; k++)
			{
				if (k > 0)
				{
					B.C (',');
				}
				B.F (pF->Rta ()[k], 0);
			}
			B.C (']');
		}
		B.C ('}');
		*pLength = B.Length ();
		*ppContentType = "application/json";
		return HTTPOK;
	}

	if (strcmp (pPath, "/midi") == 0)
	{
		// MIDI state for the page's panel: settings, fixed-CC classes with their block, block CCs, controllers, monitor
		CBoard *pB = m_pContext->pBoard;
		CMidiMap &Mp = pB->Midi ();
		CBuf B (pBuffer, *pLength);
		B.S ("{\"chan\":");
		B.I (m_pContext->MidiChan);
		B.S (",\"echo\":");
		B.I (m_pContext->MidiEcho ? 1 : 0);
		B.S (",\"sync\":");
		B.I (m_pContext->MidiSyncAdel ? 1 : 0);
		B.S (",\"bpm\":");
		B.F ((float) m_pContext->MidiBpm10 * 0.1f, 1);
		B.S (",\"clk\":");
		B.I (m_pContext->MidiClockOn ? 1 : 0);
		B.S (",\"usb\":");
		B.I (m_pContext->MidiUsb);
		B.S (",\"din\":");
		B.I (m_pContext->MidiDin);
		B.S (",\"dinfw\":");
		B.I (m_pContext->MidiDinFw);
		B.S (",\"rx\":");
		B.I (m_pContext->MidiRx);
		B.S (",\"tx\":");
		B.I (m_pContext->MidiTx);
		B.S (",\"drop\":");
		B.I (m_pContext->pMidiQ != 0 ? m_pContext->pMidiQ->Dropped () : 0);
		B.S (",\"lc\":");
		B.I (Mp.Learning () ? (unsigned) (Mp.LearnCtlIndex () >= 0 ? 1 : 2) : 0);
		B.S (",\"lid\":");
		B.I (Mp.LearnCtlIndex () >= 0 ? (unsigned) Mp.LearnCtlIndex () : (Mp.LearnBlockId () >= 0 ? (unsigned) Mp.LearnBlockId () : 0));
		B.S (",\"cls\":[");
		for (unsigned c = 1; c <= 16; c++)
		{
			const int n = pB->MidiFindClass (c);
			if (c > 1)
			{
				B.C (',');
			}
			B.S ("[");
			B.I (c);
			B.C (',');
			B.S (n >= 0 ? "" : "-1");
			if (n >= 0)
			{
				B.I ((unsigned) n);
			}
			B.C (',');
			B.I (n >= 0 && pB->Block ((unsigned) n).On ? 1 : 0);
			B.C (']');
		}
		B.S ("],\"bcc\":[");
		boolean bFirst = TRUE;
		for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
		{
			const TMidiBlockCc &Bc = Mp.BlockCc (i);
			if (Bc.Cc == 0 || Bc.Block < 0)
			{
				continue;
			}
			if (!bFirst)
			{
				B.C (',');
			}
			bFirst = FALSE;
			B.C ('[');
			B.I ((unsigned) Bc.Cc);
			B.C (',');
			B.I ((unsigned) Bc.Block);
			B.C (']');
		}
		B.S ("],\"ctl\":[");
		for (unsigned c = 0; c < MIDI_CTL_COUNT; c++)
		{
			const TMidiCtl &Ct = Mp.Ctl (c);
			if (c > 0)
			{
				B.C (',');
			}
			B.S ("{\"cc\":");
			B.I ((unsigned) Ct.Cc);
			B.S (",\"l\":[");
			boolean bF2 = TRUE;
			for (unsigned k = 0; k < MIDI_CTL_LINKS; k++)
			{
				if (Ct.Link[k].Block < 0)
				{
					continue;
				}
				if (!bF2)
				{
					B.C (',');
				}
				bF2 = FALSE;
				B.C ('[');
				B.I (k);
				B.C (',');
				B.I ((unsigned) Ct.Link[k].Block);
				B.C (',');
				B.I ((unsigned) Ct.Link[k].Param);
				B.C (',');
				B.F (Ct.Link[k].MinPct, 1);
				B.C (',');
				B.F (Ct.Link[k].MaxPct, 1);
				B.C (']');
			}
			B.S ("]}");
		}
		B.S ("],\"mon\":[");
		if (m_pContext->pMidiMon != 0)
		{
			for (unsigned i = 0; i < m_pContext->pMidiMon->Count (); i++)
			{
				char Dir;
				const char *pText;
				unsigned nSeq;
				if (!m_pContext->pMidiMon->Get (i, &Dir, &pText, &nSeq))
				{
					break;
				}
				if (i > 0)
				{
					B.C (',');
				}
				B.S ("{\"s\":");
				B.I (nSeq);
				B.S (",\"d\":\"");
				B.C (Dir);
				B.S ("\",\"t\":");
				B.Esc (pText);
				B.C ('}');
			}
		}
		B.S ("]}");
		*pLength = B.Length ();
		*ppContentType = "application/json";
		return B.Overflow () ? HTTPInternalServerError : HTTPOK;
	}

	if (strcmp (pPath, "/m") == 0)
	{
		// real-time levels for the input and output bars (a few tens of bytes)
		const TCtlStatus &St = *m_pContext->pStatus;
		CBuf B (pBuffer, *pLength);
		B.S ("{\"i\":[");
		B.I (St.nMtInL);
		B.C (',');
		B.I (St.nMtInR);
		B.C (',');
		B.I (St.nMtInS);
		B.S ("],\"o\":[");
		B.I (St.nMtOutL);
		B.C (',');
		B.I (St.nMtOutR);
		B.S ("],\"sw\":");
		B.I (FxProbePlaying ());
		{
			// peaks of every block (L, R in ppm, in the order L0 R0 L1 R1 ...) up to the last used block: for the indicators on the tiles
			unsigned Pk[2 * BD_MAX_BLOCKS];
			GraphNodePeaks (Pk, BD_MAX_BLOCKS);
			unsigned nUsed = 0;
			for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
			{
				if (m_pContext->pBoard->Block (i).Used)
				{
					nUsed = i + 1;
				}
			}
			B.S (",\"n\":[");
			for (unsigned i = 0; i < 2 * nUsed; i++)
			{
				if (i > 0)
				{
					B.C (',');
				}
				B.I (Pk[i]);
			}
			B.C (']');
		}
		char Bid[12];
		if (GetField (pParams, "b", Bid, sizeof Bid))
		{
			// values measured by the chosen block's effect (outputs: gain reduction, correlation, ...), in the order of the controls
			const int nId = (int) ParseFloat (Bid);
			float Out[FX_MAX_P];
			unsigned nCount = 0;
			if (nId >= 0 && nId < (int) BD_MAX_BLOCKS && m_pContext->pFx != 0 && m_pContext->pFx[nId] != 0)
			{
				const TBdBlock &Bl = m_pContext->pBoard->Block ((unsigned) nId);
				const TFxDef *pD = Bl.Used ? FxDefByType (Bl.Type) : 0;
				if (pD != 0 && m_pContext->pFx[nId]->Type () == Bl.Type)
				{
					for (unsigned k = 0; k < FX_MAX_P; k++)
					{
						Out[k] = 0.0f;
					}
					m_pContext->pFx[nId]->GetOutputs (Out);
					nCount = pD->nParams;
				}
			}
			B.S (",\"f\":[");
			for (unsigned k = 0; k < nCount; k++)
			{
				if (k > 0)
				{
					B.C (',');
				}
				B.F (Out[k], 2);
			}
			B.C (']');
		}
		B.C ('}');
		*pLength = B.Length ();
		*ppContentType = "application/json";
		return HTTPOK;
	}

if (strcmp (pPath, "/fx") == 0)
	{
		// description of all the effects (parameters, groups, choices, descriptions): the page reads it only once
		CBuf B (pBuffer, *pLength);
		B.C ('[');
		for (unsigned i = 0; i < FX_DEF_COUNT; i++)
		{
			const TFxDef &D = g_FxDefs[i];
			if (i > 0)
			{
				B.C (',');
			}
			B.S ("{\"fx\":");
			B.Esc (D.Name);
			B.S (",\"title\":");
			B.Esc (D.Title);
			B.S (",\"hid\":");
			B.I (D.Hidden);
			B.S (",\"g\":[");
			for (unsigned g = 0; g < D.nGroups; g++)
			{
				if (g > 0)
				{
					B.C (',');
				}
				B.Esc (D.ppGroups[g]);
			}
			B.S ("],\"p\":[");
			for (unsigned k = 0; k < D.nParams; k++)
			{
				const TFxParam &P = D.pParams[k];
				if (k > 0)
				{
					B.C (',');
				}
				B.S ("{\"k\":");
				B.Esc (P.Key);
				B.S (",\"l\":");
				B.Esc (P.Label);
				B.S (",\"u\":");
				B.Esc (P.Unit);
				B.S (",\"min\":");
				B.F (P.Min, 3);
				B.S (",\"max\":");
				B.F (P.Max, 3);
				B.S (",\"step\":");
				B.F (P.Step, 3);
				B.S (",\"def\":");
				B.F (P.Def, 3);
				B.S (",\"kind\":");
				B.I (P.Kind);
				B.S (",\"log\":");
				B.I (P.Log);
				B.S (",\"gr\":");
				B.I (P.Group);
				if (P.nEnum > 0)
				{
					B.S (",\"e\":[");
					for (unsigned e = 0; e < P.nEnum; e++)
					{
						if (e > 0)
						{
							B.C (',');
						}
						B.Esc (P.pEnum[e]);
					}
					B.C (']');
				}
				if (P.Hint != 0 && P.Hint[0] != 0)
				{
					B.S (",\"h\":");
					B.Esc (P.Hint);
				}
				B.C ('}');
			}
			B.S ("]}");
		}
		B.C (']');
		if (B.Overflow ())
		{
			CLogger::Get ()->Write (FromControl, LogError, "effects description too large for the buffer");
			return HTTPInternalServerError;
		}
		*pLength = B.Length ();
		*ppContentType = "application/json";
		return HTTPOK;
	}

	if (strcmp (pPath, "/di") == 0)
	{
		// DI recorder: without parameters the state; with o (first frame) and n (frames) a piece of the recording, 24-bit little-endian stereo
		int nOff = 0, nCnt = 0;
		boolean bO = FALSE, bN = FALSE;
		if (FieldInt (pParams, "o", &nOff, &bO) && bO && FieldInt (pParams, "n", &nCnt, &bN) && bN)
		{
			if (nOff < 0 || nCnt < 1 || nCnt > 16000 || m_pContext->pDiRead == 0 || (unsigned) nCnt * 6 > *pLength)
			{
				return HTTPBadRequest;
			}
			const unsigned nGot = m_pContext->pDiRead ((unsigned) nOff, (unsigned) nCnt, pBuffer);
			*pLength = nGot * 6;
			*ppContentType = "application/octet-stream\r\nCache-Control: no-store";
			return HTTPOK;
		}
		CBuf B (pBuffer, *pLength);
		B.S ("{\"state\":");
		B.I (m_pContext->DiState);
		B.S (",\"frames\":");
		B.I (m_pContext->DiFrames);
		B.S (",\"target\":");
		B.I (m_pContext->DiTarget);
		B.S (",\"rate\":");
		B.I (m_pContext->pStatus->nSampleRate);
		B.C ('}');
		if (B.Overflow ())
		{
			return HTTPInternalServerError;
		}
		*pLength = B.Length ();
		*ppContentType = "application/json\r\nCache-Control: no-store";
		return HTTPOK;
	}

	if (strcmp (pPath, "/lib") == 0)
	{
		// library of models, IRs and information (author/license): it changes only after an upload or a deletion, the page asks for it when "lv" of /status changes
		CBuf B (pBuffer, *pLength);
		B.S ("{\"lv\":");
		B.I (LibVersion (m_pContext));
		B.S (",\"lib\":[");
		for (unsigned i = 0; i < *m_pContext->pLibCount; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.Esc (m_pContext->pLib[i]);
		}
		B.S ("],\"ir\":[");
		for (unsigned i = 0; i < *m_pContext->pIrCount; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.Esc (m_pContext->pIr[i]);
		}
		B.S ("],\"libinf\":[");
		for (unsigned i = 0; i < *m_pContext->pLibCount; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.Esc (m_pContext->pLibInf[i]);
		}
		B.S ("]}");
		if (B.Overflow ())
		{
			return HTTPInternalServerError;
		}
		*pLength = B.Length ();
		*ppContentType = "application/json\r\nCache-Control: no-store";
		return HTTPOK;
	}

	if (strcmp (pPath, "/status") == 0)
	{
		const TCtlStatus &St = *m_pContext->pStatus;
		CBuf B (pBuffer, *pLength);
		DBG ("status: start");
		B.S ("{\"rate\":");
		B.I (St.nSampleRate);
		B.S (",\"frames\":");
		B.I (St.nChunkFrames);
		B.S (",\"cpu\":{\"avg\":");
		B.F ((float) St.nCpuAvg / 10.0f, 1);
		B.S (",\"max\":");
		B.F ((float) St.nCpuMax / 10.0f, 1);
		B.S (",\"period_us\":");
		B.I (St.nPeriodUs);
		B.S ("},\"peak\":");
		B.I (St.nPeakPpm);
		B.S (",\"peakL\":");
		B.I (St.nPeakLPpm);
		B.S (",\"peakR\":");
		B.I (St.nPeakRPpm);
		B.S (",\"outL\":");
		B.I (St.nOutPkLPpm);
		B.S (",\"outR\":");
		B.I (St.nOutPkRPpm);
		B.S (",\"rmsL\":");
		B.I (St.nOutRmsLPpm);
		B.S (",\"rmsR\":");
		B.I (St.nOutRmsRPpm);
		B.S (",\"test\":");
		B.I (St.nTestSrc);
		B.S (",\"up\":");
		B.I (St.nUptimeSec);
		B.S (",\"safe\":");
		B.S (St.bSafeMode ? "true" : "false");
		B.S (",\"tl\":");
		B.I (m_pContext->TestLock > 0 ? 1 : 0);
		B.S (",\"ov\":");
		B.I (St.nShed);
		B.S (",\"ip\":");
		B.Esc (St.IP);
		B.S (",\"ver\":");
		B.Esc (FW_VERSION);
		B.S (",\"mhz\":[");
		B.I (m_pContext->CpuMhz[0]);
		B.C (',');
		B.I (m_pContext->CpuMhz[1]);
		B.C (',');
		B.I (m_pContext->CpuMhz[2]);
		B.S ("],\"temp\":");
		B.I (m_pContext->CpuTemp);
		B.S (",\"ap\":");
		B.I (m_pContext->ApActive ? 1 : 0);

		// library of models, IRs and information are in /lib (about 10 KB): here only their version, the page re-reads /lib when it changes
		B.S (",\"lv\":");
		B.I (LibVersion (m_pContext));
		B.S (",\"board\":{\"mode\":");
		B.I (m_pContext->Mode);
		B.S (",\"s\":");
		B.I (m_pContext->pBoard->GetStructVersion ());
		B.S (",\"nam\":");
		B.I (m_pContext->pBoard->CountNam ());
		B.S (",\"max\":");
		B.I (BD_MAX_NAM);
		if (m_pContext->Mode != 0)
		{
			// network: cells (row r, column or position c; b = the block in a one-element list), links [from, to, from port, to port, dB] and row levels
			const CBoard &Nb = *m_pContext->pBoard;
			B.S (",\"net\":{\"cells\":[");
			boolean bFirstCell = TRUE;
			for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
			{
				if (!Nb.Block (i).Used)
				{
					continue;
				}
				if (!bFirstCell)
				{
					B.C (',');
				}
				bFirstCell = FALSE;
				B.S ("{\"r\":");
				B.I ((unsigned) Nb.Block (i).A);
				B.S (",\"c\":");
				B.I ((unsigned) Nb.Block (i).B);
				B.S (",\"b\":");
				WriteChain (B, Nb, (int) i);
				B.C ('}');
			}
			B.S ("],\"links\":[");
			for (unsigned k = 0; k < Nb.LinkCount (); k++)
			{
				const TBdLink &Lk = Nb.Link (k);
				if (k > 0)
				{
					B.C (',');
				}
				B.C ('[');
				B.F ((float) Lk.From, 0);
				B.C (',');
				B.F ((float) Lk.To, 0);
				B.C (',');
				B.I (Lk.PF);
				B.C (',');
				B.I (Lk.PT);
				B.C (',');
				B.F (Lk.Db, 1);
				B.C (']');
			}
			B.S ("],\"io\":[");
			for (unsigned w = 0; w < 4; w++)
			{
				if (w > 0)
				{
					B.C (',');
				}
				B.C ('[');
				B.I ((unsigned) Nb.IoRow (w));
				B.C (',');
				B.I ((unsigned) Nb.IoCol (w));
				B.C (']');
			}
			B.S ("]}");
		}
		B.S ("},\"fav\":[");
		for (unsigned i = 0; i < *m_pContext->pFavCount; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.Esc (m_pContext->pFav[i]);
		}
		B.S ("],\"bp\":[");
		for (unsigned i = 0; i < *m_pContext->pBpCount; i++)
		{
			if (i > 0)
			{
				B.C (',');
			}
			B.S ("{\"n\":");
			B.Esc (m_pContext->pBpName[i]);
			B.S (",\"d\":");
			B.Esc (m_pContext->pBpData[i]);
			B.C ('}');
		}
		B.S ("],\"upl\":{\"n\":");
		B.Esc (m_pContext->UpResName);
		B.S (",\"ok\":");
		B.I (m_pContext->UpResOk > 0 ? 1 : (m_pContext->UpResOk < 0 ? 2 : 0));
		B.S (",\"e\":");
		B.Esc (m_pContext->UpResMsg);
		B.S ("},\"pre\":[");
		{
			// one entry per position (the free ones are ""): the number of a preset is its position and does not change
			unsigned nLast = 0;
			for (unsigned i = 0; i < PRE_MAX; i++)
			{
				if (m_pContext->pPreName[i][0] != 0)
				{
					nLast = i + 1;
				}
			}
			for (unsigned i = 0; i < nLast; i++)
			{
				if (i > 0)
				{
					B.C (',');
				}
				B.Esc (m_pContext->pPreName[i]);
			}
			B.S ("],\"prm\":\"");
			for (unsigned i = 0; i < nLast; i++)
			{
				B.C (m_pContext->pPreName[i][0] != 0 ? (char) ('0' + m_pContext->pPreMode[i]) : '9');
			}
		}
		B.S ("\",\"pc\":");
		B.Esc (m_pContext->PreCur);
		B.S (",\"pd\":");
		B.I (m_pContext->PreDirty ? 1 : 0);
		B.S (",\"blocks\":[");
		CParams *pBlocks = m_pContext->pParams;
		for (unsigned b = 0; b < pBlocks->GetBlockCount (); b++)
		{
			TBlock &Blk = pBlocks->Block (b);
			if (b > 0)
			{
				B.C (',');
			}
			DBG ("status: block");
			B.S ("{\"id\":");
			B.Esc (Blk.Id);
			B.S (",\"title\":");
			B.Esc (Blk.Title);
			B.S (",\"info\":");
			B.Esc (Blk.Info);
			B.S (",\"loaded\":");
			B.S (Blk.Loaded ? "true" : "false");
			B.S (",\"params\":[");
			for (unsigned p = 0; p < Blk.nParams; p++)
			{
				TParam &Par = Blk.P[p];
				if (p > 0)
				{
					B.C (',');
				}
				DBG ("status: parameter");
				B.S ("{\"k\":");
				B.Esc (Par.Key);
				B.S (",\"l\":");
				B.Esc (Par.Label);
				B.S (",\"t\":");
				B.I ((unsigned) Par.Kind);
				B.S (Par.Cfg ? ",\"c\":1" : ",\"c\":0");
				if (Par.Kind == ParamSlider)
				{
					B.S (",\"min\":");
					B.F (Par.Min, 1);
					B.S (",\"max\":");
					B.F (Par.Max, 1);
					B.S (",\"step\":");
					B.F (Par.Step, 2);
					B.S (",\"u\":");
					B.Esc (Par.Unit);
					B.S (",\"v\":");
					B.F (Par.Value, 2);
				}
				else if (Par.Kind == ParamToggle)
				{
					B.S (",\"v\":");
					B.I (Par.Value > 0.5f ? 1 : 0);
				}
				else if (Par.Kind == ParamChoice)
				{
					B.S (",\"o\":[");
					const char *pOpt = Par.Opts;
					boolean bFirst = TRUE;
					while (*pOpt)
					{
						char Opt[24];
						unsigned n = 0;
						while (*pOpt && *pOpt != ',' && n + 1 < sizeof Opt)
						{
							Opt[n++] = *pOpt++;
						}
						Opt[n] = 0;
						if (*pOpt == ',')
						{
							pOpt++;
						}
						if (!bFirst)
						{
							B.C (',');
						}
						bFirst = FALSE;
						B.Esc (Opt);
					}
					B.S ("],\"v\":");
					B.I ((unsigned) (Par.Value + 0.5f));
				}
				else
				{
					B.S (",\"s\":");
					B.Esc (Par.Text);
				}
				B.C ('}');
			}
			B.S ("]}");
		}
		B.S ("]}");

		DBG ("status: end");
		if (B.Overflow ())
		{
			return HTTPInternalServerError;
		}
		*pLength = B.Length ();
		*ppContentType = "application/json";
		return HTTPOK;
	}

	return HTTPNotFound;
}
