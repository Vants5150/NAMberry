// test_midi.cpp - PC tests of the MIDI core (midi.cpp)
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ihost_shim -I<circle>/include -I. test_midi.cpp midi.cpp -o test_midi
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include "midi.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b, t) do { if (fabs ((double) (a) - (double) (b)) > (t)) { printf ("FAILED line %d: %g != %g\n", __LINE__, (double) (a), (double) (b)); fails++; } } while (0)

static unsigned FeedAll (CMidiParser &P, const u8 *p, unsigned n, TMidiMsg *out, unsigned max)
{
	unsigned k = 0;
	for (unsigned i = 0; i < n; i++)
	{
		TMidiMsg m;
		if (P.Feed (p[i], &m) && k < max) out[k++] = m;
	}
	return k;
}

int main ()
{
	TMidiMsg M[16];
	TMidiAct A[8];

	// ---- parser
	{
		CMidiParser P;
		const u8 s1[] = {0xB0, 0x07, 0x7F};
		CHECK (FeedAll (P, s1, 3, M, 16) == 1 && M[0].Type == MidiControl && M[0].Chan == 0 && M[0].A == 7 && M[0].B == 127);
		const u8 s2[] = {0xB0, 0x07, 0x7F, 0x08, 0x40};		// running status
		CHECK (FeedAll (P, s2, 5, M, 16) == 2 && M[1].A == 8 && M[1].B == 64 && M[1].Chan == 0);
		const u8 s3[] = {0xB0, 0xF8, 0x07, 0xF8, 0x7F};		// real time in the middle
		CHECK (FeedAll (P, s3, 5, M, 16) == 3 && M[0].Type == MidiClock && M[1].Type == MidiClock && M[2].Type == MidiControl && M[2].A == 7 && M[2].B == 127);
		const u8 s4[] = {0x93, 0x40, 0x00, 0x93, 0x40, 0x50};	// Note On with velocity 0 = Note Off
		CHECK (FeedAll (P, s4, 6, M, 16) == 2 && M[0].Type == MidiNoteOff && M[0].Chan == 3 && M[1].Type == MidiNoteOn && M[1].B == 0x50);
		const u8 s5[] = {0xC2, 0x05};
		CHECK (FeedAll (P, s5, 2, M, 16) == 1 && M[0].Type == MidiProgram && M[0].Chan == 2 && M[0].A == 5);
		const u8 s6[] = {0xF0, 0x7D, 0x4E, 0x42, 0x01, 0xF7};
		CHECK (FeedAll (P, s6, 6, M, 16) == 1 && M[0].Type == MidiSysEx && M[0].A == 4 && P.SysExLength () == 4 && P.SysEx ()[0] == 0x7D && MidiIsStateRequest (P.SysEx (), P.SysExLength ()));
		const u8 s7[] = {0xF0, 0x01, 0x02, 0xB0, 0x07, 0x7F};	// a status byte interrupts the SysEx
		CHECK (FeedAll (P, s7, 6, M, 16) == 1 && M[0].Type == MidiControl);
		const u8 s8[] = {0xF2, 0x01, 0x02, 0x03, 0x04};		// common system: data consumed, no running status afterwards
		CHECK (FeedAll (P, s8, 5, M, 16) == 0);
		const u8 s9[] = {0xE0, 0x00, 0x40, 0xD0, 0x30};		// pitch bend and pressure: ignored
		CHECK (FeedAll (P, s9, 5, M, 16) == 0);
		const u8 s10[] = {0x07, 0x7F};					// data without status: ignored
		CHECK (FeedAll (P, s10, 2, M, 16) == 0);
		u8 big[200];
		big[0] = 0xF0; for (int i = 1; i < 199; i++) big[i] = (u8) (i & 0x7F); big[199] = 0xF7;
		CHECK (FeedAll (P, big, 200, M, 16) == 1 && M[0].A == 198 && P.SysExLength () == 198);
	}

	// ---- USB-MIDI packets
	{
		CMidiParser P;
		const u8 p1[4] = {0x1B, 0xB0, 0x07, 0x7F};		// cable 1, CIN B
		CHECK (MidiUsbDecode (p1, &P, M, 16) == 1 && M[0].Type == MidiControl && M[0].A == 7 && M[0].B == 127);
		const u8 p2[4] = {0x0C, 0xC1, 0x05, 0x00};
		CHECK (MidiUsbDecode (p2, &P, M, 16) == 1 && M[0].Type == MidiProgram && M[0].Chan == 1 && M[0].A == 5);
		const u8 p3[4] = {0x04, 0xF0, 0x7D, 0x4E};		// SysEx in three packets
		const u8 p4[4] = {0x04, 0x42, 0x01, 0xF7};		// does it not end yet? (CIN 4 = continues) -> the F7 closes anyway in the parser
		CHECK (MidiUsbDecode (p3, &P, M, 16) == 0);
		CHECK (MidiUsbDecode (p4, &P, M, 16) == 1 && M[0].Type == MidiSysEx && MidiIsStateRequest (P.SysEx (), P.SysExLength ()));
		const u8 p5[4] = {0x0F, 0xF8, 0x00, 0x00};
		CHECK (MidiUsbDecode (p5, &P, M, 16) == 1 && M[0].Type == MidiClock);
		const u8 p6[4] = {0x00, 0x12, 0x34, 0x56};		// CIN 0: nothing
		CHECK (MidiUsbDecode (p6, &P, M, 16) == 0);
	}

	// ---- queue
	{
		CMidiQueue Q;
		TMidiMsg m = {MidiControl, 0, 1, 2};
		for (int i = 0; i < 63; i++) { m.A = (u8) i; CHECK (Q.Push (m)); }
		CHECK (!Q.Push (m) && Q.Dropped () == 1);
		TMidiMsg o;
		for (int i = 0; i < 63; i++) { CHECK (Q.Pop (&o) && o.A == i); }
		CHECK (!Q.Pop (&o));
		CHECK (Q.Push (m) && Q.Pop (&o));
	}

	// ---- map
	{
		CMidiMap Mp;
		const TMidiMsg pc7 = {MidiProgram, 4, 7, 0};
		CHECK (Mp.Handle (pc7, A, 8) == 1 && A[0].Type == ActPreset && A[0].A == 7);			// all channels
		Mp.SetChannel (3);
		CHECK (Mp.Handle (pc7, A, 8) == 0);								// channel 5 is not 3
		const TMidiMsg pc3 = {MidiProgram, 2, 9, 0};
		CHECK (Mp.Handle (pc3, A, 8) == 1 && A[0].A == 9);
		Mp.SetChannel (0);
		TMidiMsg c = {MidiControl, 0, 5, 100};
		CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActClassEnable && A[0].A == McReverb && A[0].B == 1);
		c.B = 63; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].B == 0);
		c.B = 64; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].B == 1);
		for (int cc = 1; cc <= 16; cc++) { c.A = (u8) cc; c.B = 127; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActClassEnable && A[0].A == cc); }
		c.A = 20; c.B = 90; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActMaster && A[0].A == 90);
		c.A = 21; c.B = 127; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActPresetUp);
		c.B = 0; CHECK (Mp.Handle (c, A, 8) == 0);
		c.A = 22; c.B = 100; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActPresetDown);
		c.A = 23; c.B = 127; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActTap);
		c.A = 33; c.B = 127; CHECK (Mp.Handle (c, A, 8) == 0);					// free CC with no assignment
		c.A = 24; CHECK (Mp.Handle (c, A, 8) == 0);						// 17..31 reserved and without function

		// controller con learn
		Mp.LearnController (0);
		c.A = 10; c.B = 127;
		CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActClassEnable && Mp.Learning ());	// a fixed CC cannot be learned
		c.A = 40;
		CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActLearned && A[0].A == 0 && A[0].B == 40 && !Mp.Learning () && Mp.Ctl (0).Cc == 40);
		Mp.Ctl (0).Link[0].Block = 3; Mp.Ctl (0).Link[0].Param = 2; Mp.Ctl (0).Link[0].MinPct = 20.0f; Mp.Ctl (0).Link[0].MaxPct = 80.0f;
		Mp.Ctl (0).Link[1].Block = 5; Mp.Ctl (0).Link[1].Param = 0; Mp.Ctl (0).Link[1].MinPct = 100.0f; Mp.Ctl (0).Link[1].MaxPct = 0.0f;	// inverted range
		c.B = 127; CHECK (Mp.Handle (c, A, 8) == 2 && A[0].Type == ActParam && A[0].A == 3 && A[0].B == 2); NEAR (A[0].F, 0.8, 1e-5); NEAR (A[1].F, 0.0, 1e-5);
		c.B = 0; CHECK (Mp.Handle (c, A, 8) == 2); NEAR (A[0].F, 0.2, 1e-5); NEAR (A[1].F, 1.0, 1e-5);
		c.B = 64; CHECK (Mp.Handle (c, A, 8) == 2); NEAR (A[0].F, 0.2 + 0.6 * 64.0 / 127.0, 1e-5);

		// CC per block with learn
		Mp.LearnBlock (7);
		c.A = 50; c.B = 127;
		CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActLearned && A[0].A == -1 && A[0].B == 50 && Mp.BlockCcCount () == 1);
		CHECK (Mp.Handle (c, A, 8) == 1 && A[0].Type == ActBlockEnable && A[0].A == 7 && A[0].B == 1);
		c.B = 0; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].B == 0);
		CHECK (Mp.SetBlockCc (51, 7) && Mp.BlockCcCount () == 1);					// a block has only one CC
		CHECK (!Mp.SetBlockCc (20, 8) && !Mp.SetBlockCc (128, 8) && !Mp.SetBlockCc (60, -1));
		CHECK (Mp.SetBlockCc (51, 9));								// the same CC for two blocks: turns on both
		c.A = 51; c.B = 127; CHECK (Mp.Handle (c, A, 8) == 2);
		Mp.ForgetBlock (7);
		CHECK (Mp.Handle (c, A, 8) == 1 && A[0].A == 9);
		Mp.ForgetBlock (3);
		c.A = 40; c.B = 127; CHECK (Mp.Handle (c, A, 8) == 1 && A[0].A == 5);
		for (int b = 20; b < 20 + MIDI_BLOCK_CC_MAX; b++) Mp.SetBlockCc (60, b);
		CHECK (!Mp.SetBlockCc (61, 99));								// table full
		// action limit
		c.A = 60; c.B = 127; CHECK (Mp.Handle (c, A, 3) == 3);
		// clock, start, stop
		const TMidiMsg st = {MidiStart, 0, 0, 0}, sp = {MidiStop, 0, 0, 0}, ck = {MidiClock, 0, 0, 0};
		CHECK (Mp.Handle (st, A, 8) == 1 && A[0].Type == ActClockStart);
		CHECK (Mp.Handle (sp, A, 8) == 1 && A[0].Type == ActClockStop);
		CHECK (Mp.Handle (ck, A, 8) == 0);
		// request SysEx
		const u8 rq[] = {0x7D, 0x4E, 0x42, 0x01};
		CHECK (Mp.HandleSysEx (rq, 4, A, 8) == 1 && A[0].Type == ActStateRequest);
		const u8 nq[] = {0x7D, 0x4E, 0x42, 0x02};
		CHECK (Mp.HandleSysEx (nq, 4, A, 8) == 0 && Mp.HandleSysEx (rq, 3, A, 8) == 0);
	}

	// ---- clock
	{
		CMidiClock K;
		unsigned long long t = 1000000ULL;
		float bpm = 0;
		srand (7);
		unsigned n = 0;
		for (int i = 0; i < 24 * 12; i++)
		{
			t += 20833ULL + (unsigned long long) (rand () % 401) - 200ULL;		// 120 bpm with 0.2 ms of jitter
			if (K.Pulse (t, &bpm)) n++;
		}
		CHECK (n == 11 || n == 12);
		NEAR (bpm, 120.0, 1.0);
		CHECK (K.Active (t + 100000ULL) && !K.Active (t + 600000ULL));
		// pause and restart at 90 bpm
		t += 2000000ULL;
		unsigned long long step = (unsigned long long) (60000000.0 / (90.0 * 24.0));
		float b2 = 0;
		for (int i = 0; i < 24 * 6; i++) { t += step; K.Pulse (t, &b2); }
		NEAR (b2, 90.0, 1.0);
		NEAR (K.Bpm (), 90.0, 1.0);
	}

	// ---- tap tempo
	{
		CTapTempo T;
		unsigned long long t = 5000000ULL;
		float bpm = 0;
		CHECK (!T.Tap (t, &bpm));
		t += 500000ULL; CHECK (T.Tap (t, &bpm)); NEAR (bpm, 120.0, 0.01);
		t += 500000ULL; CHECK (T.Tap (t, &bpm)); NEAR (bpm, 120.0, 0.01);
		t += 600000ULL; CHECK (T.Tap (t, &bpm)); NEAR (bpm, 60e6 / (1600000.0 / 3.0), 0.05);	// average over the intervals
		for (int i = 0; i < 10; i++) { t += 400000ULL; T.Tap (t, &bpm); }
		NEAR (bpm, 150.0, 0.05);									// only the last 4 intervals count
		t += 3000000ULL; CHECK (!T.Tap (t, &bpm));							// after a pause it starts over
		t += 10000ULL; CHECK (T.Tap (t, &bpm)); NEAR (bpm, 300.0, 0.01);				// upper limit
	}
	NEAR (MidiNoteMs (120.0f, 0), 500.0, 1e-3); NEAR (MidiNoteMs (120.0f, 1), 375.0, 1e-3); NEAR (MidiNoteMs (120.0f, 2), 250.0, 1e-3);
	NEAR (MidiNoteMs (120.0f, 3), 166.6667, 1e-2); NEAR (MidiNoteMs (120.0f, 4), 750.0, 1e-3); NEAR (MidiNoteMs (120.0f, 5), 1000.0, 1e-3);
	NEAR (MidiNoteMs (120.0f, 6), 125.0, 1e-3); NEAR (MidiNoteMs (0.0f, 0), 60000.0, 1e-3);

	// ---- return messages
	{
		u8 b[16];
		CHECK (MidiBuildCC (b, 1, 5, 127) == 3 && b[0] == 0xB0 && b[1] == 5 && b[2] == 127);
		CHECK (MidiBuildCC (b, 0, 5, 0) == 3 && b[0] == 0xB0 && b[2] == 0);				// "all" replies on channel 1
		CHECK (MidiBuildCC (b, 16, 127, 500) == 3 && b[0] == 0xBF && b[1] == 127 && b[2] == 127);
		CHECK (MidiBuildPC (b, 2, 130) == 2 && b[0] == 0xC1 && b[1] == (130 & 0x7F));
		const unsigned n = MidiBuildStateSysEx (b, 300, 0x1A5, 99);
		CHECK (n == 12 && b[0] == 0xF0 && b[n - 1] == 0xF7);
		for (unsigned i = 1; i + 1 < n; i++) CHECK (b[i] < 0x80);
		CHECK (((b[5] << 7) | b[6]) == 300 && (b[7] | (b[8] << 7) | (b[9] << 14)) == 0x1A5 && b[10] == 99);
		CMidiParser P;
		CHECK (FeedAll (P, b, n, M, 16) == 1 && M[0].Type == MidiSysEx && !MidiIsStateRequest (P.SysEx (), P.SysExLength ()));
		// echo: a fixed CC sent by the pedalboard is understood by a controller using the same protocol (round trip through the parser)
		u8 e[3]; MidiBuildCC (e, 1, 9, 127);
		CHECK (FeedAll (P, e, 3, M, 16) == 1 && M[0].Type == MidiControl && M[0].A == 9 && M[0].B == 127);
	}

	// ---- monitor and formatting
	{
		char t[MIDI_MON_TEXT];
		TMidiMsg c = {MidiControl, 0, 5, 127}, p = {MidiProgram, 15, 7, 0}, s = {MidiSysEx, 0, 4, 1};
		CHECK (MidiFormat (t, sizeof t, c) > 0 && strcmp (t, "CC 5 = 127 (can. 1)") == 0);
		CHECK (MidiFormat (t, sizeof t, p) > 0 && strcmp (t, "PC 7 (can. 16)") == 0);
		CHECK (MidiFormat (t, sizeof t, s) > 0 && strstr (t, "status request") != 0);
		TMidiMsg n = {MidiNoteOn, 2, 60, 100};
		CHECK (MidiFormat (t, sizeof t, n) > 0 && strcmp (t, "Note on 60") == 0);
		TMidiMsg bad = {MidiNone, 0, 0, 0};
		CHECK (MidiFormat (t, sizeof t, bad) == 0);
		char small[6];
		MidiFormat (small, sizeof small, c);
		CHECK (strlen (small) <= 5);					// does not write beyond the buffer
		CMidiMonitor Mon;
		CHECK (Mon.Count () == 0);
		for (int i = 0; i < 40; i++)
		{
			char tx[20]; snprintf (tx, sizeof tx, "event %02d", i);
			Mon.Add (i & 1 ? 'o' : 'i', tx);
		}
		// 20 received and 20 sent alternating: they all fit (two queues), from the most recent to the least recent
		CHECK (Mon.Total () == 40 && Mon.Count () == 40);
		char d; const char *pt; unsigned sq;
		CHECK (Mon.Get (0, &d, &pt, &sq) && sq == 39 && strcmp (pt, "event 39") == 0 && d == 'o');
		CHECK (Mon.Get (1, &d, &pt, &sq) && sq == 38 && strcmp (pt, "event 38") == 0 && d == 'i');
		CHECK (Mon.Get (39, &d, &pt, &sq) && sq == 0 && strcmp (pt, "event 00") == 0 && d == 'i');
		CHECK (!Mon.Get (40, &d, &pt, &sq));
		Mon.Add ('i', "a very long text that far exceeds the capacity of a monitor line");
		CHECK (Mon.Get (0, &d, &pt, &sq) && strlen (pt) == MIDI_MON_TEXT - 1);
		// the burst of status messages sent after a preset change does not push out the received ones
		CMidiMonitor Fl;
		Fl.Add ('i', "PC 2 (can. 1)");
		for (int k = 0; k < 100; k++)
		{
			Fl.Add ('o', "CC 5 = 0 (can. 1)");
		}
		bool bFound = false;
		unsigned nIn = 0;
		for (unsigned k = 0; k < Fl.Count (); k++)
		{
			if (Fl.Get (k, &d, &pt, &sq) && d == 'i')
			{
				nIn++;
				bFound = strcmp (pt, "PC 2 (can. 1)") == 0;
			}
		}
		CHECK (nIn == 1 && bFound && Fl.Count () == 1 + MIDI_MON_COUNT);
	}

	// ---- fuzz: random bytes into the parser, the map and the queue; values always within limits
	{
		CMidiParser P;
		CMidiMap Mp;
		CMidiQueue Q;
		CMidiClock K;
		unsigned long long t = 1;
		srand (12345);
		Mp.Ctl (1).Cc = 77; Mp.Ctl (1).Link[0].Block = 2; Mp.SetBlockCc (88, 4);
		for (long i = 0; i < 3000000; i++)
		{
			u8 b = (u8) rand ();
			if ((i % 5000) == 0) Mp.LearnController (rand () % 3);
			if ((i % 7000) == 0) Mp.LearnBlock (rand () % 40);
			TMidiMsg m;
			if (P.Feed (b, &m))
			{
				CHECK (m.Chan < 16);
				if (m.Type == MidiControl || m.Type == MidiNoteOn || m.Type == MidiNoteOff) CHECK (m.A < 128 && m.B < 128);
				Q.Push (m);
				if ((i & 3) == 0)
				{
					TMidiMsg o;
					while (Q.Pop (&o))
					{
						TMidiAct act[8];
						const unsigned n = Mp.Handle (o, act, 8);
						CHECK (n <= 8);
						for (unsigned k = 0; k < n; k++) if (act[k].Type == ActParam) CHECK (act[k].F >= 0.0f && act[k].F <= 1.0f + 1e-6f);
						if (o.Type == MidiClock) { float bp; t += 20000 + (rand () % 500); K.Pulse (t, &bp); }
						if (o.Type == MidiSysEx) Mp.HandleSysEx (P.SysEx (), P.SysExLength () > MIDI_SYSEX_MAX ? MIDI_SYSEX_MAX : P.SysExLength (), act, 8);
					}
				}
			}
		}
	}

	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
