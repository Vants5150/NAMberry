//
// midi.h - MIDI core (phase 1): byte parser, decoding of the USB-MIDI packets, queue from the interrupt context to the main loop, CC/PC -> actions map
// (like the Sonulab StompStation PRO), assignable controllers with learn, MIDI clock, tap tempo and return messages (CC echo, status SysEx).
// No hardware dependency: it is tested on the PC (test_midi.cpp). The link to the document (CBoard) and to the presets is phase 2.
//
#ifndef _midi_h
#define _midi_h

#include <circle/types.h>

enum TMidiType
{
	MidiNone = 0, MidiProgram, MidiControl, MidiNoteOn, MidiNoteOff, MidiClock, MidiStart, MidiContinue, MidiStop, MidiSysEx
};

struct TMidiMsg
{
	u8 Type;	// TMidiType
	u8 Chan;	// 0..15
	u8 A, B;	// Program: A = number; Control: A = CC, B = value; Note: A = note, B = velocity; SysEx: A = length (at most 255)
};

#define MIDI_SYSEX_MAX	64

class CMidiParser
{
public:
	CMidiParser (void);
	boolean Feed (u8 nByte, TMidiMsg *pMsg);		// TRUE if the byte completes a message; real time does not interrupt the ones in progress
	const u8 *SysEx (void) const { return m_SysEx; }		// content of the last complete SysEx (without F0 and F7), at most MIDI_SYSEX_MAX bytes
	unsigned SysExLength (void) const { return m_nSysEx; }
private:
	u8 m_Status;		// current status (running status); 0 = none
	u8 m_Data[2];
	u8 m_nData, m_nNeed;
	boolean m_bIgnore;	// common system message: the data are consumed without emitting anything
	boolean m_bInSysEx;
	u8 m_SysEx[MIDI_SYSEX_MAX];
	unsigned m_nSysEx;
};

// 4-byte USB-MIDI packet (cable number and message code, then 1-3 bytes): it feeds the parser and returns how many messages came out
unsigned MidiUsbDecode (const u8 *pPacket, CMidiParser *pParser, TMidiMsg *pOut, unsigned nOutMax);

// queue with a single producer (interrupt or USB handler) and a single consumer (main loop)
#define MIDI_QUEUE_SIZE	64
class CMidiQueue
{
public:
	CMidiQueue (void) : m_nHead (0), m_nTail (0), m_nDropped (0) {}
	boolean Push (const TMidiMsg &Msg);		// FALSE (and counts) if full
	boolean Pop (TMidiMsg *pMsg);
	unsigned Dropped (void) const { return m_nDropped; }
private:
	TMidiMsg m_Buf[MIDI_QUEUE_SIZE];
	volatile unsigned m_nHead, m_nTail;
	unsigned m_nDropped;
};

// ---- actions ----
// block classes of the fixed CCs 1..16 (same numbering as the StompStation PRO)
enum TMidiClass
{
	McDrive = 1, McModPre = 2, McAmp = 3, McIr = 4, McReverb = 5, McModPost = 6, McTremolo = 7, McCompressor = 8, McGate = 9, McDelay = 10, McTuner = 11,
	McExpr = 12, McBoost = 13, McWah = 14, McEq = 15, McPitch = 16
};
#define MIDI_CC_MASTER		20
#define MIDI_CC_PRESET_UP	21
#define MIDI_CC_PRESET_DOWN	22
#define MIDI_CC_TAP		23
#define MIDI_CC_FIRST_FREE	32		// CCs below 32 are reserved for the fixed CCs

enum TMidiActType
{
	ActNone = 0,
	ActPreset,		// A = index in the preset list (Program Change)
	ActPresetUp, ActPresetDown,
	ActClassEnable,		// A = class, B = 1 on / 0 off: applies to the first block of that class in the preset
	ActBlockEnable,		// A = block id, B = 1 / 0 (CC assigned to the block with learn)
	ActMaster,		// A = 0..127
	ActParam,		// A = block id, B = parameter index, F = 0..1 (already within the link's min/max range)
	ActTap,			// tap tempo
	ActClockTempo,		// F = bpm estimated from the MIDI clock
	ActClockStart, ActClockStop,
	ActStateRequest,	// a controller asks for the state (SysEx)
	ActLearned		// A = controller (0..2) or -1 if it is a block CC, B = learned CC
};

struct TMidiAct
{
	u8 Type;
	int A, B;
	float F;
};

#define MIDI_CTL_COUNT		3
#define MIDI_CTL_LINKS		3
#define MIDI_BLOCK_CC_MAX	32

struct TMidiLink
{
	int Block;		// block id, -1 = free
	int Param;
	float MinPct, MaxPct;	// interval, in % of the parameter's range
};

struct TMidiCtl
{
	int Cc;			// 0 = no source; 32..127
	TMidiLink Link[MIDI_CTL_LINKS];
};

struct TMidiBlockCc
{
	int Cc;			// 0 = free
	int Block;
};

// tempo estimate from the MIDI clock (24 pulses per quarter)
class CMidiClock
{
public:
	CMidiClock (void) { Reset (); }
	void Reset (void);
	boolean Pulse (unsigned long long nNowUs, float *pBpm);	// TRUE every 24 pulses with the new estimate
	boolean Active (unsigned long long nNowUs) const;		// recent pulses (the last less than half a second ago)
	float Bpm (void) const { return m_fBpm; }
private:
	unsigned long long m_nLast;
	unsigned m_nCount, m_nPulses;
	unsigned long long m_Sum;
	float m_fBpm;
};

class CTapTempo
{
public:
	CTapTempo (void) { m_nTaps = 0; }
	boolean Tap (unsigned long long nNowUs, float *pBpm);	// TRUE from the second tap, with the average of the last 4 intervals
private:
	unsigned long long m_T[5];
	unsigned m_nTaps;
};

// duration in ms of a subdivision at a given tempo: 0 = 1/4, 1 = dotted 1/8, 2 = 1/8, 3 = 1/8 triplet, 4 = dotted 1/4, 5 = 1/2, 6 = 1/16
float MidiNoteMs (float fBpm, unsigned nDiv);

class CMidiMap
{
public:
	CMidiMap (void);
	void SetChannel (unsigned n) { m_nChan = n > 16 ? 0 : n; }	// 0 = all, 1..16
	unsigned Channel (void) const { return m_nChan; }
	// applies a message; returns the number of actions written to pOut (at most nMax)
	unsigned Handle (const TMidiMsg &Msg, TMidiAct *pOut, unsigned nMax);
	unsigned HandleSysEx (const u8 *pData, unsigned nLen, TMidiAct *pOut, unsigned nMax);
	// assignable controllers and block CCs
	TMidiCtl &Ctl (unsigned n) { return m_Ctl[n]; }
	const TMidiCtl &Ctl (unsigned n) const { return m_Ctl[n]; }
	void LearnController (int nCtl) { m_nLearnCtl = nCtl; m_nLearnBlock = -1; }	// the next CC >= 32 becomes the source
	void LearnBlock (int nBlock) { m_nLearnBlock = nBlock; m_nLearnCtl = -1; }	// the next CC >= 32 turns that block on/off
	void CancelLearn (void) { m_nLearnCtl = m_nLearnBlock = -1; }
	boolean Learning (void) const { return m_nLearnCtl >= 0 || m_nLearnBlock >= 0; }
	int LearnCtlIndex (void) const { return m_nLearnCtl; }
	int LearnBlockId (void) const { return m_nLearnBlock; }
	boolean SetBlockCc (int nCc, int nBlock);
	void ForgetBlock (int nBlock);				// when a block is deleted
	unsigned BlockCcCount (void) const;
	const TMidiBlockCc &BlockCc (unsigned n) const { return m_BlockCc[n]; }
private:
	unsigned m_nChan;
	TMidiCtl m_Ctl[MIDI_CTL_COUNT];
	TMidiBlockCc m_BlockCc[MIDI_BLOCK_CC_MAX];
	int m_nLearnCtl, m_nLearnBlock;
};

// ---- monitor: latest incoming ('i') and outgoing ('o') messages for the page ----
#define MIDI_MON_COUNT	32
#define MIDI_MON_TEXT	48
// Two separate queues (received and sent), each with its latest MIDI_MON_COUNT messages, and a common sequence number: after every preset change the board sends
// about twenty status messages to the controllers, which would otherwise push out the received ones in a few seconds. Get() returns them merged, from the most recent.
class CMidiMonitor
{
public:
	CMidiMonitor (void) : m_nSeq (0) { m_nTotal[0] = m_nTotal[1] = 0; }
	void Add (char cDir, const char *pText);
	unsigned Total (void) const { return m_nSeq; }
	unsigned Count (void) const { return Held (0) + Held (1); }
	boolean Get (unsigned nAgo, char *pDir, const char **ppText, unsigned *pSeq) const;	// 0 = the most recent
private:
	unsigned Held (unsigned nRing) const { return m_nTotal[nRing] < MIDI_MON_COUNT ? m_nTotal[nRing] : MIDI_MON_COUNT; }
	char m_Text[2][MIDI_MON_COUNT][MIDI_MON_TEXT];
	unsigned m_Seq[2][MIDI_MON_COUNT];
	volatile unsigned m_nTotal[2];		// 0 = received ('i'), 1 = sent ('o')
	volatile unsigned m_nSeq;
};
unsigned MidiFormat (char *pOut, unsigned nSize, const TMidiMsg &Msg);		// "CC 5 = 127 (can. 1)"

// ---- return messages for the controllers (LEDs of the active effects) ----
// The common method is the echo: when a block changes state the pedalboard replies with the same CC (127 on, 0 off) and, when the preset changes, with the Program Change.
// In addition a status SysEx of ours (identifier 7D = non-commercial use), for the pages and controllers that will be able to read it:
//   request  F0 7D 4E 42 01 F7
//   status     F0 7D 4E 42 02 <preset high> <preset low> <class mask 3 x 7 bits> <master> F7   (bit n-1 of the mask = class n on)
unsigned MidiBuildCC (u8 *pOut, unsigned nChan, unsigned nCc, unsigned nValue);
unsigned MidiBuildPC (u8 *pOut, unsigned nChan, unsigned nProgram);
unsigned MidiBuildStateSysEx (u8 *pOut, unsigned nPreset, unsigned nClassMask, unsigned nMaster);
boolean MidiIsStateRequest (const u8 *pData, unsigned nLen);		// pData without F0 and F7

#endif
