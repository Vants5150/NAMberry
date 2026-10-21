// test_din.cpp - PC tests of the Pisound DIN socket (pisounddin.cpp) with a fake MCU that follows the official kernel module's protocol
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ihost_shim -I<circle>/include -I. test_din.cpp pisounddin.cpp midi.cpp -o test_din
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#include "pisounddin.h"
#include "midi.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// fake MCU: information block as 16-bit words, bytes to give (pairs [tag 1, data]), bytes received with tag 0x0F, 127-byte output buffer
class CFakeMcu : public CDinBus
{
public:
	std::deque<u16> Info;		// replies to the 2-byte transfers
	std::deque<u8> ToHost;			// MIDI bytes received from the DIN socket, to be delivered to the host
	std::vector<u8> FromHost;		// MIDI bytes that the host sent
	int Fill1000 = 0;			// output buffer occupancy, thousandths of a byte
	unsigned long long NowUs = 0;
	unsigned long long LastUs = 0;
	int Overflows = 0;
	unsigned Transfers = 0;
	unsigned BadFrames = 0;
	int Mode = 0;				// 0 normal, 1 bus at zero, 2 bus at one

	void Tick (unsigned long long nUs)
	{
		NowUs = nUs;
		if (LastUs && nUs > LastUs)
		{
			const long long d = (long long) ((nUs - LastUs) * 3125ULL / 1000ULL);
			Fill1000 = d >= Fill1000 ? 0 : Fill1000 - (int) d;
		}
		LastUs = nUs;
	}

	void Transfer (const u8 *pTx, u8 *pRx, unsigned nLen) override
	{
		Transfers++;
		memset (pRx, Mode == 2 ? 0xFF : 0, nLen);
		if (Mode != 0)
		{
			return;
		}
		if (nLen == 2)
		{
			if (!Info.empty ())
			{
				const u16 w = Info.front ();
				Info.pop_front ();
				pRx[0] = (u8) (w >> 8);
				pRx[1] = (u8) (w & 0xFF);
			}
			return;
		}
		if (nLen != 4)
		{
			BadFrames++;
			return;
		}
		for (unsigned i = 0; i < 4; i += 2)
		{
			if (pTx[i] == 0x0F)
			{
				FromHost.push_back (pTx[i + 1]);
				Fill1000 += 1000;
				if (Fill1000 > 127000)
				{
					Overflows++;
				}
			}
			else if (pTx[i] != 0)
			{
				BadFrames++;
			}
			if (!ToHost.empty ())
			{
				pRx[i] = 1;
				pRx[i + 1] = ToHost.front ();
				ToHost.pop_front ();
			}
		}
	}

	boolean DataAvailable (void) override { return Mode == 0 && !ToHost.empty (); }
};

static void SetInfo (CFakeMcu &M, unsigned fw, unsigned hw)
{
	auto w = [&](unsigned b) { M.Info.push_back ((u16) (0x100 | (b & 0xFF))); };
	w (4);						// four voices
	w (2); w (fw >> 8); w (fw & 0xFF);		// firmware version
	w (3); w ('1'); w ('2'); w ('3');		// serial
	w (2); w (0xAB); w (0xCD);			// id
	w (2); w (hw >> 8); w (hw & 0xFF);		// hardware version
}

int main ()
{
	// ---- information block
	{
		CFakeMcu M;
		CPisoundDin D (&M);
		SetInfo (M, 0x0103, 0x0200);
		CHECK (D.ReadInfo ());
		CHECK (D.FwVersion () == 0x0103);
		CHECK (D.HwVersion () == 0x0200);
		CHECK (M.Info.empty ());
	}
	{
		CFakeMcu M;				// without an MCU: bus all zeros
		M.Mode = 1;
		CPisoundDin D (&M);
		CHECK (!D.ReadInfo ());
	}
	{
		CFakeMcu M;				// without an MCU: bus all ones
		M.Mode = 2;
		CPisoundDin D (&M);
		CHECK (!D.ReadInfo ());
		CHECK (M.Transfers < 20);		// does not spin idle
	}
	{
		CFakeMcu M;				// truncated reply
		CPisoundDin D (&M);
		M.Info.push_back (0x0104);
		M.Info.push_back (0x0102);
		M.Info.push_back (0x0101);
		CHECK (!D.ReadInfo ());
	}

	// ---- reception: the bytes arrive two at a time and go through the MIDI parser
	{
		CFakeMcu M;
		CPisoundDin D (&M);
		SetInfo (M, 0x0103, 0x0200);
		CHECK (D.ReadInfo ());
		const u8 In[] = {0x90, 0x3C, 0x7F, 0xB0, 0x07, 0x40, 0xF8, 0xC1, 0x05, 0xF0, 0x7D, 0x4E, 0x42, 0x01, 0xF7};
		for (unsigned i = 0; i < sizeof In; i++) M.ToHost.push_back (In[i]);
		u8 Got[64];
		const unsigned n = D.Poll (1000, Got, sizeof Got);
		CHECK (n == sizeof In);
		CHECK (memcmp (Got, In, sizeof In) == 0);
		CHECK (M.ToHost.empty ());
		CMidiParser P;
		unsigned nm = 0;
		TMidiMsg Msg;
		boolean bCC = FALSE, bPC = FALSE, bClk = FALSE;
		for (unsigned i = 0; i < n; i++)
		{
			if (P.Feed (Got[i], &Msg))
			{
				nm++;
				if (Msg.Type == MidiControl && Msg.A == 7 && Msg.B == 0x40) bCC = TRUE;
				if (Msg.Type == MidiProgram && Msg.Chan == 1 && Msg.A == 5) bPC = TRUE;
				if (Msg.Type == MidiClock) bClk = TRUE;
			}
		}
		CHECK (bCC && bPC && bClk && nm >= 4);
		CHECK (D.Poll (2000, Got, sizeof Got) == 0);		// nothing new: a single idle transfer
	}
	{
		CFakeMcu M;				// more bytes than fit in one call: they stay for the next one, none lost
		CPisoundDin D (&M);
		SetInfo (M, 0x0103, 0x0200);
		CHECK (D.ReadInfo ());
		for (unsigned i = 0; i < 100; i++) M.ToHost.push_back ((u8) (0x40 + (i & 0x1F)));
		u8 Got[64];
		std::vector<u8> All;
		unsigned long long t = 1000;
		for (int k = 0; k < 20 && !M.ToHost.empty (); k++)
		{
			const unsigned n = D.Poll (t, Got, 10);
			CHECK (n <= 10);
			All.insert (All.end (), Got, Got + n);
			t += 5000;
		}
		D.Poll (t, Got, sizeof Got);
		CHECK (M.ToHost.empty ());
		CHECK (All.size () >= 90);
		for (unsigned i = 0; i < All.size (); i++) CHECK (All[i] == (u8) (0x40 + (i & 0x1F)));
	}

	// ---- transmission: order preserved and the MCU's buffer never filled beyond 127 bytes
	{
		CFakeMcu M;
		CPisoundDin D (&M);
		SetInfo (M, 0x0103, 0x0200);
		CHECK (D.ReadInfo ());
		std::vector<u8> Out;
		for (unsigned i = 0; i < 400; i++) Out.push_back ((u8) (i * 7 + 1));
		CHECK (D.Send (Out.data (), 400) == 400);
		CHECK (D.Pending () == 400);
		u8 Got[64];
		unsigned long long t = 1000;
		for (int k = 0; k < 400 && D.Pending (); k++)
		{
			t += 5000;
			M.Tick (t);
			D.Poll (t, Got, sizeof Got);
		}
		CHECK (D.Pending () == 0);
		CHECK (M.FromHost.size () == 400);
		CHECK (M.FromHost == Out);
		CHECK (M.Overflows == 0);
		CHECK (M.BadFrames == 0);
		CHECK (D.Dropped () == 0);
	}
	{
		CFakeMcu M;				// all at once, without letting time pass: it stops at 126 bytes instead of overflowing the MCU
		CPisoundDin D (&M);
		SetInfo (M, 0x0103, 0x0200);
		CHECK (D.ReadInfo ());
		u8 Blob[300];
		for (unsigned i = 0; i < 300; i++) Blob[i] = (u8) i;
		D.Send (Blob, 300);
		u8 Got[16];
		for (int k = 0; k < 10; k++) D.Poll (1000, Got, sizeof Got);
		CHECK (M.Overflows == 0);
		CHECK (M.FromHost.size () <= 127 && M.FromHost.size () >= 120);
		CHECK (D.Pending () > 150);
	}
	{
		CFakeMcu M;				// ring full: the extra bytes are counted as lost, they do not write outside
		CPisoundDin D (&M);
		u8 Blob[DIN_TX_RING * 2];
		memset (Blob, 0x55, sizeof Blob);
		const unsigned a = D.Send (Blob, sizeof Blob);
		CHECK (a == DIN_TX_RING - 1);
		CHECK (D.Dropped () == sizeof Blob - a);
	}

	// ---- reception and transmission together, with a complete echo message (CC 3 = 127 on channel 1)
	{
		CFakeMcu M;
		CPisoundDin D (&M);
		SetInfo (M, 0x0103, 0x0200);
		CHECK (D.ReadInfo ());
		const u8 Echo[] = {0xB0, 0x03, 0x7F};
		D.Send (Echo, 3);
		M.ToHost.push_back (0xB0); M.ToHost.push_back (3); M.ToHost.push_back (0);
		u8 Got[16];
		const unsigned n = D.Poll (1000, Got, sizeof Got);
		CHECK (n == 3 && Got[0] == 0xB0 && Got[1] == 3 && Got[2] == 0);
		CHECK (M.FromHost.size () == 3 && M.FromHost[0] == 0xB0 && M.FromHost[2] == 0x7F);
	}

	printf ("\nRISULTATO: %s\n", fails ? "tests failed" : "all tests passed");
	return fails ? 1 : 0;
}
