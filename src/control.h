//
// control.h - control web page (state, block parameters) served by the Pi with Circle.
// Changes arrive with POST /set and are accepted only with the code (token) that the page receives at every start.
//
#ifndef _control_h
#define _control_h

#include <circle/net/httpdaemon.h>
#include "params.h"
#include "board.h"

class CFeatures;

#define UP_MAX		(256 * 1024)	// maximum size of a .adn model uploaded from the page
#define UP_INF_MAX	120		// author/license text next to the model (in the real files they are at most 48 characters)
#define MODE_NONE	9		// no mode change requested
#define PRE_MAX		48		// pedalboard presets (one file each on the card)
#define BP_MAX		256		// saved block settings (heap: see CKernel; the page reads the list from /bpl)
#define BP_NAME_LEN	32
#define BP_DATA_LEN	800

// name: 1..31 characters without quotation marks, slashes, tabs or control characters (UTF-8 bytes are allowed)
static inline boolean BpValidName (const char *p)
{
	unsigned n = 0;
	for (; *p; p++, n++)
	{
		const unsigned char c = (unsigned char) *p;
		if (c < 0x20 || c == 0x7F || c == '"' || c == '\\' || n >= BP_NAME_LEN - 1)
		{
			return FALSE;
		}
	}
	return n > 0;
}

// data: "type|key=value|..." with harmless characters only
static inline boolean BpValidData (const char *p)
{
	unsigned n = 0;
	for (; *p; p++, n++)
	{
		const char c = *p;
		const boolean bOk = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				    || c == '_' || c == '.' || c == '=' || c == '|' || c == ',' || c == ':' || c == '-' || c == '+';
		if (!bOk || n >= BP_DATA_LEN - 1)
		{
			return FALSE;
		}
	}
	return n > 0;
}

struct TCtlContext
{
	CParams *pParams;
	TCtlStatus *pStatus;
	char (*pLib)[PARAM_TEXT_LEN];
	char (*pLibInf)[UP_INF_MAX + 8];	// author / license of every model in the library (from NAME.inf, empty if absent)		// model library (.adn files on the card)
	volatile unsigned *pLibCount;
	char (*pIr)[PARAM_TEXT_LEN];		// library of the cabinet IRs (.air files on the card)
	volatile unsigned *pIrCount;
	CBoard *pBoard;
	CFx *const *pFx;			// effect instances by block id (values measured by the engines)
	char Token[16];
	char DelName[PARAM_TEXT_LEN];		// file to delete (done by the main loop, which is the only one that uses the card)
	volatile boolean DelPending;
	volatile unsigned TestLock;		// acceptance test in progress: 100 ms ticks left (0 = none); changes from the page (field pg=1) are rejected
	char (*pFav)[PARAM_TEXT_LEN];		// favorite models (library file names)
	volatile unsigned *pFavCount;
	volatile boolean FavDirty;
	char (*pBpName)[BP_NAME_LEN];		// saved block settings (name, data)
	char (*pBpData)[BP_DATA_LEN];
	volatile unsigned *pBpCount;
	volatile boolean BpDirty;
	volatile unsigned BpVer;		// changes at every save or delete of a block setting: the page re-reads /bpl when it changes
	u8 *pUpBuf;				// upload of a model from the page: area where the pieces are collected
	char UpName[48];
	unsigned UpTotal, UpGot;
	volatile unsigned UpPending;		// 1 = everything received, written by the main loop
	char UpResName[48];			// result of the last upload
	volatile int UpResOk;			// 0 in progress, 1 succeeded, -1 failed
	char UpResMsg[64];
	u8 *pPreMode;				// mode of each preset (0 ADELFlow, 1 ADELMatrix, 2 ADELGrid)
	char (*pPreName)[BP_NAME_LEN];		// pedalboard presets: names by position (empty = free position)
	char PreCur[BP_NAME_LEN];		// preset in use (empty = none)
	volatile boolean PreDirty;		// the pedalboard was modified after the preset was loaded or saved
	volatile unsigned PrePending;		// 0 nothing, 1 save, 2 load, 3 delete: run by the main loop
	char PreArg[BP_NAME_LEN];
	unsigned PreArgMode;		// mode of the preset being operated on		// to be saved on the card (done by the main loop)
	unsigned Mode;				// active mode: 0 = ADELFlow, 1 = ADELMatrix, 2 = ADELGrid
	volatile unsigned ModePending;		// mode requested by the page (MODE_NONE = none): the change is run by the main loop
	volatile boolean PreFull;		// all the preset positions (of all the modes) are occupied
	volatile boolean TestPending;		// test source request (op "test"), run by the main loop
	volatile unsigned TestMask;
	// DI recorder (raw input recording, see CPisoundSound::DiPrepare): the page asks, the main loop runs it
	volatile unsigned DiReq;		// 0 none, 1 start, 2 stop, 3 free the memory
	volatile unsigned DiSeconds;
	volatile unsigned DiAppend;		// 1 = add the new take after the existing recording
	volatile unsigned DiMax;		// frames the buffer can hold
	volatile unsigned DiMarkN;		// number of takes and their start frames
	unsigned DiMark[64];
	volatile unsigned DiState;		// 0 empty, 1 recording, 2 done (published by the main loop)
	volatile unsigned DiFrames;
	volatile unsigned DiTarget;
	unsigned (*pDiRead) (unsigned nFrom, unsigned nCount, u8 *pOut);	// reads the recording (24-bit LE stereo)
	// MIDI (midi.h)
	CMidiQueue *pMidiQ;			// incoming messages (USB, DIN, test from the page) for the MIDI task
	CMidiMonitor *pMidiMon;			// latest incoming and outgoing messages, for the page's monitor
	volatile unsigned MidiChan;		// 0 = all channels, 1..16
	volatile boolean MidiEcho;		// reply with the same CC to the controller's commands (and to state changes)
	CFeatures *pFeat;			// service functions with a switch (ftools.h)
	volatile boolean FeatDirty;		// parameters and switches to save on the SD (features.cfg)
	volatile unsigned MidiCtl3Cc;		// source (CC 32..127, 0 = none) of controller 3: it is global, it applies to all the presets (midi.cfg); the links stay in the preset
	volatile boolean MidiSyncAdel;		// the ADEL delays follow the MIDI tempo (their time becomes the current quarter note); saved in midi.cfg
	volatile boolean MidiCfgDirty;		// channel and echo to save on the SD (midi.cfg)
	volatile unsigned MidiBpm10;		// tempo (MIDI clock or tap tempo) in tenths of a bpm, 0 = none
	volatile boolean MidiClockOn;		// incoming MIDI clock
	volatile unsigned MidiUsb;		// 1 = USB MIDI controller connected
	volatile unsigned MidiDin;		// Pisound DIN socket: 0 off/not found, 1 starting, 2 active
	volatile unsigned MidiDinFw;		// MCU firmware version (0x0103 = 1.03), 0 if not read
	volatile unsigned MidiRx, MidiTx;	// messages received and sent since boot
	// fallback network (WPA2 access point): name and password are used at the boot in which the home network is not found (ap.cfg)
	unsigned CpuMhz[3];			// processor frequency at boot: current, minimum, maximum (MHz)
	volatile unsigned CpuTemp;		// SoC temperature (tenths of a degree), updated every 2 s
	volatile unsigned CpuFan;		// 1 = fan on (only if the firmware controls it)
	volatile unsigned FanCtl;		// 1 = the fan is controlled by the firmware with the thresholds below; 0 = the system manages it (thermal.cfg)
	volatile unsigned FanOnC, FanOffC;	// thresholds in degrees: it turns on above FanOnC, turns off below FanOffC
	volatile unsigned DmaAlarms;		// DMA alarms recorded in the dmaalarm.log file on the SD (one line per alarm)
	volatile unsigned FanMaxPct;		// maximum fan speed in the quiet part of the curve (35..100 %), thermal.cfg
	volatile unsigned FanPct;		// current fan speed in % (only if the firmware controls it)
	volatile int FanDuty;			// fan PWM test: -1 = no test, 0..100 = requested percentage (test image only)
	volatile boolean FanDutyNew;		// there is a new test command (restarts the seconds count)
	volatile unsigned BootNext;		// 1 = at the next boot the processor goes to 2400 MHz
	volatile boolean ThermDirty;		// settings to save on the SD
	volatile unsigned McCfg;		// extra worker cores requested for the next boot (0 = core 0 only, 1..3), multicore.cfg
	volatile unsigned McActive;		// cores used by the engine right now (1 = core 0 only)
	volatile boolean McAvail;		// this version is built with multi-core support
	volatile boolean McDirty;		// McCfg to be saved on SD
	char ApSsid[36];
	char ApPass[68];
	char ApDrv[5100];			// latest messages of the Wi-Fi driver and of the kernel in the last boot into the fallback network (apdrv.log)
	char ApLog[400];			// steps of the last boot into the fallback network (ap.log): it helps to understand where it stops
	volatile unsigned ApChan;
	volatile boolean ApActive;		// the board is an access point right now
	volatile unsigned ApAction;		// 1 = save ap.cfg, 2 = reboot into the fallback network, 3 = reboot, 4 = reboot into concert mode, without network (done by the main loop)
};

class CControl : public CHTTPDaemon
{
public:
	CControl (CNetSubSystem *pNetSubSystem, TCtlContext *pContext, CSocket *pSocket = 0);
	~CControl (void);

	CHTTPDaemon *CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket) override;

	THTTPStatus GetContent (const char  *pPath,
				const char  *pParams,
				const char  *pFormData,
				u8	    *pBuffer,
				unsigned    *pLength,
				const char **ppContentType) override;

private:
	THTTPStatus HandleSet (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);
	THTTPStatus HandleDelete (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);
	THTTPStatus HandleBoard (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);
	THTTPStatus HandlePre (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);
	THTTPStatus HandleUp (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);
	THTTPStatus HandleBp (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);
	THTTPStatus HandleFav (const char *pFormData, u8 *pBuffer, unsigned *pLength, const char **ppContentType);

	TCtlContext *m_pContext;
};

#endif
