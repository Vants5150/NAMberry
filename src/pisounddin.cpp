//
// pisounddin.cpp
//
#include "pisounddin.h"

#define DIN_OUT_BUF_MILLI	127000		// MCU output buffer: 127 bytes
#define DIN_DRAIN_PER_S		3125		// bytes/s (MIDI UART at 31250 baud, 10 bits per byte)

CPisoundDin::CPisoundDin (CDinBus *pBus)
:	m_pBus (pBus),
	m_nFw (0),
	m_nHw (0x10),
	m_nHead (0),
	m_nTail (0),
	m_nDropped (0),
	m_nOutUsedMilli (0),
	m_nLastUs (0)
{
}

u16 CPisoundDin::Transfer16 (u16 nVal)
{
	u8 Tx[2] = {(u8) (nVal >> 8), (u8) (nVal & 0xFF)};
	u8 Rx[2] = {0, 0};
	m_pBus->Transfer (Tx, Rx, 2);
	return (u16) ((Rx[0] << 8) | Rx[1]);
}

// length-prefixed string: one word with the length (tag other than 0), then one word per byte
int CPisoundDin::ReadBytes (u8 *pDst, unsigned nMax, unsigned *pnRead)
{
	*pnRead = 0;
	u16 nRx = Transfer16 (0);
	if (!(nRx >> 8))
	{
		return -1;
	}
	const unsigned nSize = nRx & 0xFF;
	if (nSize > nMax)
	{
		return -1;
	}
	for (unsigned i = 0; i < nSize; i++)
	{
		nRx = Transfer16 (0);
		if (!(nRx >> 8))
		{
			return -1;
		}
		pDst[i] = (u8) (nRx & 0xFF);
	}
	*pnRead = nSize;
	return 0;
}

boolean CPisoundDin::ReadInfo (void)
{
	m_nFw = 0;
	m_nHw = 0x10;
	const u16 nHead = Transfer16 (0);
	if (!(nHead >> 8))
	{
		return FALSE;
	}
	const unsigned nCount = nHead & 0xFF;
	if (nCount < 2 || nCount > 8)		// the Pisound sends at least version and serial; a bus without an MCU reads all 0s or all 1s
	{
		return FALSE;
	}
	boolean bFw = FALSE;
	for (unsigned i = 0; i < nCount; i++)
	{
		u8 Buf[32];
		unsigned n;
		if (ReadBytes (Buf, sizeof Buf, &n) < 0)
		{
			return FALSE;
		}
		if (i == 0)
		{
			if (n != 2)
			{
				return FALSE;
			}
			m_nFw = (unsigned) (Buf[0] << 8) | Buf[1];
			bFw = TRUE;
		}
		else if (i == 3 && n == 2)
		{
			m_nHw = (unsigned) (Buf[0] << 8) | Buf[1];
		}
	}
	m_nLastUs = 0;
	m_nOutUsedMilli = 0;
	return bFw;
}

unsigned CPisoundDin::Send (const u8 *pData, unsigned nLen)
{
	unsigned nDone = 0;
	for (; nDone < nLen; nDone++)
	{
		const unsigned nNext = (m_nHead + 1) % DIN_TX_RING;
		if (nNext == m_nTail)
		{
			m_nDropped += nLen - nDone;
			break;
		}
		m_Tx[m_nHead] = pData[nDone];
		m_nHead = nNext;
	}
	return nDone;
}

unsigned CPisoundDin::Poll (unsigned long long nUs, u8 *pIn, unsigned nInMax)
{
	unsigned nIn = 0;

	if (m_nLastUs != 0 && nUs > m_nLastUs)		// the MCU's output buffer empties over time
	{
		const unsigned long long nDrain = (nUs - m_nLastUs) * DIN_DRAIN_PER_S / 1000ULL;		// thousandths of a byte
		m_nOutUsedMilli = nDrain >= (unsigned long long) m_nOutUsedMilli ? 0 : m_nOutUsedMilli - (int) nDrain;
	}
	m_nLastUs = nUs != 0 ? nUs : 1;

	for (unsigned nStep = 0; nStep < DIN_STEPS_PER_POLL; nStep++)
	{
		u8 Tx[4] = {0, 0, 0, 0};
		u8 Rx[4] = {0, 0, 0, 0};
		boolean bSent = FALSE;
		for (unsigned i = 0; i < 4; i += 2)
		{
			if (m_nTail != m_nHead && m_nOutUsedMilli + 1000 < DIN_OUT_BUF_MILLI)
			{
				Tx[i] = 0x0F;
				Tx[i + 1] = m_Tx[m_nTail];
				m_nTail = (m_nTail + 1) % DIN_TX_RING;
				m_nOutUsedMilli += 1000;
				bSent = TRUE;
			}
		}

		m_pBus->Transfer (Tx, Rx, 4);

		boolean bGot = FALSE;
		for (unsigned i = 0; i < 4; i += 2)
		{
			if (Rx[i])
			{
				bGot = TRUE;
				if (nIn < nInMax)
				{
					pIn[nIn++] = Rx[i + 1];
				}
			}
		}

		if (!bGot && !bSent && !m_pBus->DataAvailable ())
		{
			break;
		}
		if (nIn + 2 > nInMax)		// another exchange is not started if the two bytes it can carry do not fit
		{
			break;
		}
	}
	return nIn;
}
