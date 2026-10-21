//
// midi.cpp - MIDI core (see midi.h)
//
#include "midi.h"

// ---------------------------------------------------------------- parser
CMidiParser::CMidiParser (void)
:	m_Status (0), m_nData (0), m_nNeed (0), m_bIgnore (FALSE), m_bInSysEx (FALSE), m_nSysEx (0)
{
	m_Data[0] = m_Data[1] = 0;
}

boolean CMidiParser::Feed (u8 b, TMidiMsg *pMsg)
{
	if (b >= 0xF8)			// real time: it can arrive in the middle of any message and does not change the state
	{
		pMsg->Chan = 0;
		pMsg->A = pMsg->B = 0;
		switch (b)
		{
		case 0xF8: pMsg->Type = MidiClock; return TRUE;
		case 0xFA: pMsg->Type = MidiStart; return TRUE;
		case 0xFB: pMsg->Type = MidiContinue; return TRUE;
		case 0xFC: pMsg->Type = MidiStop; return TRUE;
		default: return FALSE;
		}
	}

	if (b & 0x80)			// status byte
	{
		if (b == 0xF0)
		{
			m_bInSysEx = TRUE;
			m_nSysEx = 0;
			m_Status = 0;
			m_nData = 0;
			return FALSE;
		}
		if (b == 0xF7)
		{
			if (m_bInSysEx)
			{
				m_bInSysEx = FALSE;
				pMsg->Type = MidiSysEx;
				pMsg->Chan = 0;
				pMsg->A = (u8) (m_nSysEx > 255 ? 255 : m_nSysEx);
				pMsg->B = 0;
				return TRUE;
			}
			return FALSE;
		}
		m_bInSysEx = FALSE;		// any other status byte interrupts a SysEx
		m_nData = 0;
		if (b >= 0xF1)			// common system message: the data are consumed without emitting anything
		{
			m_Status = b;
			m_bIgnore = TRUE;
			m_nNeed = (b == 0xF2) ? 2 : ((b == 0xF1 || b == 0xF3) ? 1 : 0);
			if (m_nNeed == 0)
			{
				m_Status = 0;
			}
			return FALSE;
		}
		m_Status = b;
		m_bIgnore = FALSE;
		const u8 hi = b & 0xF0;
		m_nNeed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
		return FALSE;
	}

	// data bytes
	if (m_bInSysEx)
	{
		if (m_nSysEx < MIDI_SYSEX_MAX)
		{
			m_SysEx[m_nSysEx] = b;
		}
		m_nSysEx++;			// the length keeps counting even beyond the limit (the content stops)
		return FALSE;
	}
	if (m_Status == 0)
	{
		return FALSE;
	}
	m_Data[m_nData++] = b;
	if (m_nData < m_nNeed)
	{
		return FALSE;
	}
	m_nData = 0;
	if (m_bIgnore)
	{
		m_Status = 0;
		return FALSE;
	}

	const u8 hi = m_Status & 0xF0;
	pMsg->Chan = m_Status & 0x0F;
	pMsg->A = m_Data[0];
	pMsg->B = m_nNeed > 1 ? m_Data[1] : 0;
	switch (hi)
	{
	case 0x80: pMsg->Type = MidiNoteOff; return TRUE;
	case 0x90: pMsg->Type = pMsg->B == 0 ? MidiNoteOff : MidiNoteOn; return TRUE;
	case 0xB0: pMsg->Type = MidiControl; return TRUE;
	case 0xC0: pMsg->Type = MidiProgram; return TRUE;
	default: return FALSE;		// aftertouch, pitch bend: not needed
	}
}

unsigned MidiUsbDecode (const u8 *pPacket, CMidiParser *pParser, TMidiMsg *pOut, unsigned nOutMax)
{
	static const u8 Len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
	const unsigned nLen = Len[pPacket[0] & 0x0F];
	unsigned n = 0;
	for (unsigned i = 0; i < nLen; i++)
	{
		TMidiMsg Msg;
		if (pParser->Feed (pPacket[1 + i], &Msg) && n < nOutMax)
		{
			pOut[n++] = Msg;
		}
	}
	return n;
}

// ---------------------------------------------------------------- queue
// several producers possible (USB handler in interrupt, web page): the interrupt is masked for the duration of Push (on a single core it is enough)
// (firmware only: RASPPI is defined by Circle; on the PC or on the build Pi the privileged instructions would raise an exception)
static inline unsigned long CritEnter (void)
{
#if defined(__aarch64__) && defined(RASPPI)
	unsigned long nFlags;
	asm volatile ("mrs %0, daif\n\tmsr daifset, #3" : "=r" (nFlags) : : "memory");
	return nFlags;
#else
	return 0;
#endif
}

static inline void CritLeave (unsigned long nFlags)
{
#if defined(__aarch64__) && defined(RASPPI)
	asm volatile ("msr daif, %0" : : "r" (nFlags) : "memory");
#else
	(void) nFlags;
#endif
}

boolean CMidiQueue::Push (const TMidiMsg &Msg)
{
	const unsigned long nFlags = CritEnter ();
	const unsigned nNext = (m_nHead + 1) % MIDI_QUEUE_SIZE;
	if (nNext == m_nTail)
	{
		m_nDropped++;
		CritLeave (nFlags);
		return FALSE;
	}
	m_Buf[m_nHead] = Msg;
	__sync_synchronize ();
	m_nHead = nNext;
	CritLeave (nFlags);
	return TRUE;
}

boolean CMidiQueue::Pop (TMidiMsg *pMsg)
{
	if (m_nTail == m_nHead)
	{
		return FALSE;
	}
	*pMsg = m_Buf[m_nTail];
	__sync_synchronize ();
	m_nTail = (m_nTail + 1) % MIDI_QUEUE_SIZE;
	return TRUE;
}

// ---------------------------------------------------------------- clock and tap tempo
void CMidiClock::Reset (void)
{
	m_nLast = 0;
	m_nCount = 0;
	m_nPulses = 0;
	m_Sum = 0;
	m_fBpm = 0.0f;
}

boolean CMidiClock::Pulse (unsigned long long nNowUs, float *pBpm)
{
	if (m_nLast != 0 && nNowUs - m_nLast > 500000ULL)		// stopped for more than half a second: it starts over
	{
		const float f = m_fBpm;
		Reset ();
		m_fBpm = f;
	}
	if (m_nLast != 0 && nNowUs > m_nLast)
	{
		m_Sum += nNowUs - m_nLast;
		m_nCount++;
	}
	m_nLast = nNowUs;
	if (++m_nPulses < 24 || m_nCount == 0)
	{
		return FALSE;
	}
	m_nPulses = 0;
	const double fAvg = (double) m_Sum / (double) m_nCount;		// microseconds per pulse
	m_Sum = 0;
	m_nCount = 0;
	float f = (float) (60000000.0 / (fAvg * 24.0));
	if (f < 20.0f) f = 20.0f;
	if (f > 300.0f) f = 300.0f;
	m_fBpm = m_fBpm > 0.0f ? 0.5f * (m_fBpm + f) : f;		// average with the previous estimate
	*pBpm = m_fBpm;
	return TRUE;
}

boolean CMidiClock::Active (unsigned long long nNowUs) const
{
	return m_nLast != 0 && nNowUs >= m_nLast && nNowUs - m_nLast < 500000ULL;
}

boolean CTapTempo::Tap (unsigned long long nNowUs, float *pBpm)
{
	if (m_nTaps > 0 && nNowUs - m_T[m_nTaps - 1] > 2500000ULL)		// long pause: new series
	{
		m_nTaps = 0;
	}
	if (m_nTaps == 5)
	{
		for (unsigned i = 0; i < 4; i++)
		{
			m_T[i] = m_T[i + 1];
		}
		m_nTaps = 4;
	}
	m_T[m_nTaps++] = nNowUs;
	if (m_nTaps < 2)
	{
		return FALSE;
	}
	const double fAvg = (double) (m_T[m_nTaps - 1] - m_T[0]) / (double) (m_nTaps - 1);
	float f = (float) (60000000.0 / fAvg);
	if (f < 30.0f) f = 30.0f;
	if (f > 300.0f) f = 300.0f;
	*pBpm = f;
	return TRUE;
}

float MidiNoteMs (float fBpm, unsigned nDiv)
{
	static const float Fac[7] = {1.0f, 0.75f, 0.5f, 1.0f / 3.0f, 1.5f, 2.0f, 0.25f};
	if (fBpm < 1.0f)
	{
		fBpm = 1.0f;
	}
	return 60000.0f / fBpm * Fac[nDiv > 6 ? 0 : nDiv];
}

// ---------------------------------------------------------------- map
CMidiMap::CMidiMap (void)
:	m_nChan (0), m_nLearnCtl (-1), m_nLearnBlock (-1)
{
	for (unsigned c = 0; c < MIDI_CTL_COUNT; c++)
	{
		m_Ctl[c].Cc = 0;
		for (unsigned k = 0; k < MIDI_CTL_LINKS; k++)
		{
			m_Ctl[c].Link[k].Block = -1;
			m_Ctl[c].Link[k].Param = 0;
			m_Ctl[c].Link[k].MinPct = 0.0f;
			m_Ctl[c].Link[k].MaxPct = 100.0f;
		}
	}
	for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
	{
		m_BlockCc[i].Cc = 0;
		m_BlockCc[i].Block = -1;
	}
}

boolean CMidiMap::SetBlockCc (int nCc, int nBlock)
{
	if (nCc < MIDI_CC_FIRST_FREE || nCc > 127 || nBlock < 0)
	{
		return FALSE;
	}
	for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)		// a block has only one CC: it is replaced
	{
		if (m_BlockCc[i].Cc != 0 && m_BlockCc[i].Block == nBlock)
		{
			m_BlockCc[i].Cc = nCc;
			return TRUE;
		}
	}
	for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
	{
		if (m_BlockCc[i].Cc == 0)
		{
			m_BlockCc[i].Cc = nCc;
			m_BlockCc[i].Block = nBlock;
			return TRUE;
		}
	}
	return FALSE;
}

void CMidiMap::ForgetBlock (int nBlock)
{
	for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
	{
		if (m_BlockCc[i].Cc != 0 && m_BlockCc[i].Block == nBlock)
		{
			m_BlockCc[i].Cc = 0;
			m_BlockCc[i].Block = -1;
		}
	}
	for (unsigned c = 0; c < MIDI_CTL_COUNT; c++)
	{
		for (unsigned k = 0; k < MIDI_CTL_LINKS; k++)
		{
			if (m_Ctl[c].Link[k].Block == nBlock)
			{
				m_Ctl[c].Link[k].Block = -1;
			}
		}
	}
	if (m_nLearnBlock == nBlock)
	{
		m_nLearnBlock = -1;
	}
}

unsigned CMidiMap::BlockCcCount (void) const
{
	unsigned n = 0;
	for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
	{
		if (m_BlockCc[i].Cc != 0)
		{
			n++;
		}
	}
	return n;
}

static boolean Put (TMidiAct *pOut, unsigned nMax, unsigned *pN, u8 nType, int nA, int nB, float fF)
{
	if (*pN >= nMax)
	{
		return FALSE;
	}
	pOut[*pN].Type = nType;
	pOut[*pN].A = nA;
	pOut[*pN].B = nB;
	pOut[*pN].F = fF;
	(*pN)++;
	return TRUE;
}

unsigned CMidiMap::Handle (const TMidiMsg &Msg, TMidiAct *pOut, unsigned nMax)
{
	unsigned n = 0;
	switch (Msg.Type)
	{
	case MidiStart:
	case MidiContinue:
		Put (pOut, nMax, &n, ActClockStart, 0, 0, 0.0f);
		return n;
	case MidiStop:
		Put (pOut, nMax, &n, ActClockStop, 0, 0, 0.0f);
		return n;
	case MidiProgram:
	case MidiControl:
		break;
	default:
		return 0;
	}
	if (m_nChan != 0 && (unsigned) Msg.Chan + 1 != m_nChan)
	{
		return 0;
	}
	if (Msg.Type == MidiProgram)
	{
		Put (pOut, nMax, &n, ActPreset, Msg.A, 0, 0.0f);
		return n;
	}

	const int nCc = Msg.A, nVal = Msg.B;
	const boolean bOn = nVal >= 64;
	if (Learning () && nCc >= MIDI_CC_FIRST_FREE)
	{
		if (m_nLearnCtl >= 0 && m_nLearnCtl < (int) MIDI_CTL_COUNT)
		{
			m_Ctl[m_nLearnCtl].Cc = nCc;
			Put (pOut, nMax, &n, ActLearned, m_nLearnCtl, nCc, 0.0f);
		}
		else if (m_nLearnBlock >= 0 && SetBlockCc (nCc, m_nLearnBlock))
		{
			Put (pOut, nMax, &n, ActLearned, -1, nCc, (float) m_nLearnBlock);
		}
		CancelLearn ();
		return n;
	}
	if (nCc >= 1 && nCc <= 16)
	{
		Put (pOut, nMax, &n, ActClassEnable, nCc, bOn ? 1 : 0, 0.0f);
	}
	else if (nCc == MIDI_CC_MASTER)
	{
		Put (pOut, nMax, &n, ActMaster, nVal, 0, 0.0f);
	}
	else if (nCc == MIDI_CC_PRESET_UP && bOn)
	{
		Put (pOut, nMax, &n, ActPresetUp, 0, 0, 0.0f);
	}
	else if (nCc == MIDI_CC_PRESET_DOWN && bOn)
	{
		Put (pOut, nMax, &n, ActPresetDown, 0, 0, 0.0f);
	}
	else if (nCc == MIDI_CC_TAP && bOn)
	{
		Put (pOut, nMax, &n, ActTap, 0, 0, 0.0f);
	}
	else if (nCc >= MIDI_CC_FIRST_FREE)
	{
		for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
		{
			if (m_BlockCc[i].Cc == nCc && m_BlockCc[i].Block >= 0)
			{
				Put (pOut, nMax, &n, ActBlockEnable, m_BlockCc[i].Block, bOn ? 1 : 0, 0.0f);
			}
		}
		for (unsigned c = 0; c < MIDI_CTL_COUNT; c++)
		{
			if (m_Ctl[c].Cc != nCc)
			{
				continue;
			}
			for (unsigned k = 0; k < MIDI_CTL_LINKS; k++)
			{
				const TMidiLink &L = m_Ctl[c].Link[k];
				if (L.Block >= 0)
				{
					const float f = (L.MinPct + (L.MaxPct - L.MinPct) * (float) nVal / 127.0f) * 0.01f;
					Put (pOut, nMax, &n, ActParam, L.Block, L.Param, f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f));
				}
			}
		}
	}
	return n;
}

unsigned CMidiMap::HandleSysEx (const u8 *pData, unsigned nLen, TMidiAct *pOut, unsigned nMax)
{
	unsigned n = 0;
	if (MidiIsStateRequest (pData, nLen))
	{
		Put (pOut, nMax, &n, ActStateRequest, 0, 0, 0.0f);
	}
	return n;
}

// ---------------------------------------------------------------- monitor
void CMidiMonitor::Add (char cDir, const char *pText)
{
	const unsigned r = cDir == 'i' ? 0 : 1;
	const unsigned n = m_nTotal[r] % MIDI_MON_COUNT;
	unsigned i = 0;
	for (; pText[i] != 0 && i + 1 < MIDI_MON_TEXT; i++)
	{
		m_Text[r][n][i] = pText[i];
	}
	m_Text[r][n][i] = 0;
	m_Seq[r][n] = m_nSeq;
	m_nSeq = m_nSeq + 1;
	m_nTotal[r] = m_nTotal[r] + 1;
}

boolean CMidiMonitor::Get (unsigned nAgo, char *pDir, const char **ppText, unsigned *pSeq) const
{
	if (nAgo >= Count ())
	{
		return FALSE;
	}
	// from the most recent: at every step the message with the highest sequence number among the heads of the two queues is taken
	unsigned nBack[2] = {0, 0};		// how many messages of each queue have already gone by
	for (unsigned step = 0;; step++)
	{
		unsigned nBest = 2, nBestSeq = 0;
		for (unsigned r = 0; r < 2; r++)
		{
			if (nBack[r] < Held (r))
			{
				const unsigned n = (m_nTotal[r] - 1 - nBack[r]) % MIDI_MON_COUNT;
				if (nBest == 2 || m_Seq[r][n] > nBestSeq)
				{
					nBest = r;
					nBestSeq = m_Seq[r][n];
				}
			}
		}
		if (nBest == 2)
		{
			return FALSE;
		}
		const unsigned n = (m_nTotal[nBest] - 1 - nBack[nBest]) % MIDI_MON_COUNT;
		if (step == nAgo)
		{
			*pDir = nBest == 0 ? 'i' : 'o';
			*ppText = m_Text[nBest][n];
			*pSeq = m_Seq[nBest][n];
			return TRUE;
		}
		nBack[nBest]++;
	}
}

static unsigned PutStr (char *p, unsigned nSize, unsigned n, const char *s)
{
	while (*s && n + 1 < nSize)
	{
		p[n++] = *s++;
	}
	p[n] = 0;
	return n;
}

static unsigned PutNum (char *p, unsigned nSize, unsigned n, unsigned v)
{
	char T[12];
	unsigned k = 0;
	do
	{
		T[k++] = (char) ('0' + v % 10);
		v /= 10;
	}
	while (v != 0 && k < sizeof T);
	while (k > 0 && n + 1 < nSize)
	{
		p[n++] = T[--k];
	}
	p[n] = 0;
	return n;
}

unsigned MidiFormat (char *pOut, unsigned nSize, const TMidiMsg &M)
{
	unsigned n = 0;
	if (nSize == 0)
	{
		return 0;
	}
	pOut[0] = 0;
	switch (M.Type)
	{
	case MidiProgram:
		n = PutStr (pOut, nSize, n, "PC ");
		n = PutNum (pOut, nSize, n, M.A);
		break;
	case MidiControl:
		n = PutStr (pOut, nSize, n, "CC ");
		n = PutNum (pOut, nSize, n, M.A);
		n = PutStr (pOut, nSize, n, " = ");
		n = PutNum (pOut, nSize, n, M.B);
		break;
	case MidiNoteOn:
	case MidiNoteOff:
		n = PutStr (pOut, nSize, n, M.Type == MidiNoteOn ? "Note on " : "Note off ");
		n = PutNum (pOut, nSize, n, M.A);
		return n;
	case MidiStart: return PutStr (pOut, nSize, 0, "Start");
	case MidiContinue: return PutStr (pOut, nSize, 0, "Continue");
	case MidiStop: return PutStr (pOut, nSize, 0, "Stop");
	case MidiClock: return PutStr (pOut, nSize, 0, "Clock");
	case MidiSysEx:
		n = PutStr (pOut, nSize, n, "SysEx ");
		n = PutNum (pOut, nSize, n, M.A);
		n = PutStr (pOut, nSize, n, M.B == 1 ? " byte (status request)" : " byte");
		return n;
	default:
		return 0;
	}
	n = PutStr (pOut, nSize, n, " (can. ");
	n = PutNum (pOut, nSize, n, (unsigned) M.Chan + 1);
	n = PutStr (pOut, nSize, n, ")");
	return n;
}

// ---------------------------------------------------------------- return messages
unsigned MidiBuildCC (u8 *pOut, unsigned nChan, unsigned nCc, unsigned nValue)
{
	pOut[0] = (u8) (0xB0 | ((nChan > 0 ? nChan - 1 : 0) & 0x0F));
	pOut[1] = (u8) (nCc & 0x7F);
	pOut[2] = (u8) (nValue > 127 ? 127 : nValue);
	return 3;
}

unsigned MidiBuildPC (u8 *pOut, unsigned nChan, unsigned nProgram)
{
	pOut[0] = (u8) (0xC0 | ((nChan > 0 ? nChan - 1 : 0) & 0x0F));
	pOut[1] = (u8) (nProgram & 0x7F);
	return 2;
}

unsigned MidiBuildStateSysEx (u8 *pOut, unsigned nPreset, unsigned nClassMask, unsigned nMaster)
{
	unsigned n = 0;
	pOut[n++] = 0xF0;
	pOut[n++] = 0x7D;
	pOut[n++] = 0x4E;
	pOut[n++] = 0x42;
	pOut[n++] = 0x02;
	pOut[n++] = (u8) ((nPreset >> 7) & 0x7F);
	pOut[n++] = (u8) (nPreset & 0x7F);
	pOut[n++] = (u8) (nClassMask & 0x7F);
	pOut[n++] = (u8) ((nClassMask >> 7) & 0x7F);
	pOut[n++] = (u8) ((nClassMask >> 14) & 0x7F);
	pOut[n++] = (u8) (nMaster > 127 ? 127 : nMaster);
	pOut[n++] = 0xF7;
	return n;
}

boolean MidiIsStateRequest (const u8 *pData, unsigned nLen)
{
	return nLen == 4 && pData[0] == 0x7D && pData[1] == 0x4E && pData[2] == 0x42 && pData[3] == 0x01;
}
