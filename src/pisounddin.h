//
// pisounddin.h
//
// Pisound MIDI DIN socket: the board's MCU talks over SPI0 (CE0, 150 kHz, mode 0) with the protocol of the official kernel module (BlokasLabs/pisound, pisound.c):
//  - 4-byte transfers = 2 [tag, data] pairs; towards the MCU tag 0x0F = MIDI byte to send, 0 = nothing; from the MCU tag other than 0 = MIDI byte received
//  - the "data available" pin (GPIO25) is high as long as the MCU has more bytes to give
//  - at startup: MCU reset (GPIO24 low 1 ms, then high, 64 ms wait), then reading of the information block (version, serial, id)
//  - the MCU has a 127-byte output buffer that empties at 3125 bytes/s: the occupancy is estimated and it is not filled beyond that
// The class does not touch the hardware: it uses a CDinBus (in kernel.cpp the real one, in the PC tests a fake MCU).
//
#ifndef _pisounddin_h
#define _pisounddin_h

#include <circle/types.h>

class CDinBus
{
public:
	virtual ~CDinBus (void) {}
	virtual void Transfer (const u8 *pTx, u8 *pRx, unsigned nLen) = 0;		// an SPI transaction with CE0 low for the whole duration
	virtual boolean DataAvailable (void) = 0;					// level of the data available pin
};

#define DIN_TX_RING		512
#define DIN_STEPS_PER_POLL	16		// at most 16 4-byte transfers per call (about 3.5 ms at 150 kHz)

class CPisoundDin
{
public:
	CPisoundDin (CDinBus *pBus);

	// reads the information block after the reset: TRUE if the reply is that of a Pisound
	boolean ReadInfo (void);
	unsigned FwVersion (void) const { return m_nFw; }		// for example 0x0103 = 1.03
	unsigned HwVersion (void) const { return m_nHw; }

	// queues MIDI bytes to send to the DIN socket; returns how many it accepted
	unsigned Send (const u8 *pData, unsigned nLen);

	// to be called often (every few ms): exchanges bytes with the MCU and puts the received ones in pIn; nUs = clock in microseconds
	unsigned Poll (unsigned long long nUs, u8 *pIn, unsigned nInMax);

	unsigned Dropped (void) const { return m_nDropped; }
	unsigned Pending (void) const { return (m_nHead - m_nTail) % DIN_TX_RING; }

private:
	u16 Transfer16 (u16 nVal);
	int ReadBytes (u8 *pDst, unsigned nMax, unsigned *pnRead);

private:
	CDinBus *m_pBus;
	unsigned m_nFw, m_nHw;
	u8 m_Tx[DIN_TX_RING];
	volatile unsigned m_nHead, m_nTail;
	unsigned m_nDropped;
	int m_nOutUsedMilli;			// estimate of the occupancy of the MCU's output buffer, in thousandths of a byte
	unsigned long long m_nLastUs;
};

#endif
