//
// kernel.h - Pisound v1.2 + Wi-Fi + syslog test on Circle (Raspberry Pi 5)
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/gpiopin.h>
#include <circle/pwmoutput.h>
#include <circle/bcmwatchdog.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#include <circle/net/netsubsystem.h>
#include <circle/types.h>
#include "config.h"
#include <circle/usb/usbmidi.h>
#include "midi.h"
#include "pisounddin.h"
#include "ftools.h"
#include "pisoundsound.h"
#include "ota.h"
#include "params.h"
#include "control.h"
#include <circle/gpiopin.h>
#include "board.h"
#include "graph.h"
#include "dsp_util.h"

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

#define WARM_FILES	48		// at most this many different models in the list to put in cache
#define WARM_CACHE	32		// models whose warmed-up state copy is kept (preset change without redoing the warm-up)

struct TWarm
{
	u32 Sig;			// model signature (size and CRC of the file)
	size_t nFloats, nCap;
	float *pData;
	u32 Pos[adelnam::kMaxLayers + 2];
	// copy of the model file (already verified): reloading it does not re-read the whole file from the SD card nor redo the CRC if the name, size and CRC stored in the file (last 4 bytes) are those of before
	u32 NameHash, nRaw;
	u8 *pRaw;
	size_t nRawCap;
	char RawName[BD_FILE_LEN];
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	boolean StorageInit (void);			// USB, SD, FatFs
	boolean NetworkInit (void);			// Wi-Fi, network stack, WPA
	boolean LoadSlot (int nId, CBoard *pBoard = 0);			// reads the model of NAM block nId (SD:/<file>.adn) and warms up its instance (pBoard: document being prepared, 0 = the one in use)
	void LoadAllModels (void);
	void RebuildPlan (void);			// compiles the pedalboard into a new plan and passes it to the audio callback
	void BoardLoad (void);
	void BoardSave (void);
	void BoardTick (void);
	void PreViewBuild (void);
	void BoardPath (CString *pPath, unsigned nMode);
	void PreCurPath (CString *pPath, unsigned nMode);
	void LoadIr (int nId, CBoard *pBoard = 0, CFx **ppFx = 0);			// reads the IR (.air) of Cab IR block nId into the effect's instance (pBoard and ppFx: document and instances being prepared)
	boolean PreStageLoad (unsigned nSlot);		// preset change without interruption: prepares the preset aside and swaps it in at once; FALSE = not done (nothing changed)
	void MidiPcApply (void);			// applies the pending MIDI Program Change (after the debounce) by starting the preset change
	void GhostTick (void);				// frees the effect instances of the finished ghosts
	void NamPrepare (unsigned nLane, const char *pFile, const u8 *pData, size_t nSize, boolean *pVerified, boolean *pWarmHit);	// warm-up (or copy) and self-check of a model just initialized
	void WarmScanTick (void);			// prepares in cache, one model per round, those of all the presets (with the card idle, after boot)
	void WarmFile (const char *pFile, unsigned nLane);
	// model file: from the in-memory copy if the file's name, size and CRC (4 bytes at the end, re-read from the card) are those of before, otherwise everything from the SD card. Returns 0 if successful, 1 file missing, 2 implausible size, 3 read error.
	unsigned ModelRead (const char *pFile, unsigned nLane, u8 **ppData, size_t *pnSize, boolean *pbFromCache, u32 *pNameHash);
	void ModelCacheStore (u32 nNameHash, const char *pFile, const u8 *pData, size_t nSize);
	void PreBoardStore (unsigned nSlot);		// keeps the parsed preset in memory (m_pStage)
	void ModelCacheFlush (void);			// discards the copies of the model files (library changed)
	boolean WarmInstance (unsigned nLane, u32 nSig);	// warms up the slot's instance (or restores the copy of the already warmed-up state of that model)

	// control page: parameters, model library, settings on the SD
	void ControlInit (void);
	void ControlTick (void);			// every 100 ms from the main loop
	void ScanLibrary (void);
	void SettingsLoad (void);
	void SettingsSave (void);
	void FavLoad (void);
	void FavSave (void);
	void BpLoad (void);
	void BpSave (void);
	void UpTick (void);
	void LibOrderAdd (const char *pName);
	void PreScan (void);
	void PreTick (void);
	boolean PreWrite (unsigned nSlot, const char *pName);
	boolean PreRead (unsigned nSlot);
	void PreCurRead (void);
	void PreCurWrite (void);
	void ApplyMaster (void);
	unsigned BootCounterRead (void);
	void BootCounterWrite (unsigned nValue);
	void Feed (void) { m_Watchdog.Start (WATCHDOG_SECONDS); }
	static void LedHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext);
	void PisoundInit (unsigned nSampleRate);
	void Stage (unsigned nStage);
	void ErrorHalt (unsigned nCode);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CScreenDevice		m_Screen;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CScheduler		m_Scheduler;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;
	CBcm4343Device		m_WLAN;
	CNetSubSystem		m_Net;
	CWPASupplicant		m_WPASupplicant;

	CGPIOPin		m_Osr0;
	CGPIOPin		m_Osr1;
	CGPIOPin		m_Osr2;
	CGPIOPin		m_ResetAudio;
	CGPIOPin		m_ResetMcu;
	CGPIOPin		m_Button;		// Pisound button (GPIO 17, active low)
	boolean			m_bMcFaultSeen;
	boolean			m_bFanOwned;	// the firmware controls the fan (pin assigned)
	CGPIOPin		m_FanPin;		// active heatsink fan (GPIO 45, low level = on)
	CPWMOutput		*m_pFanPwm;		// PWM fan (PWM1, channel with index 3), 0 = not active
	unsigned		m_nFanPwmSince;		// second of the last manual test command (op fandt)
	boolean			m_bFanRun;		// the fan is in the on phase of the hysteresis
	boolean			m_bFanPwmFailed;	// the PWM did not start: on/off with the thresholds
	boolean FanPwmStart (void);
	void FanPwmSet (unsigned nPct);
	void FanTick (unsigned nTemp);

	CBcmWatchdog		m_Watchdog;

	CPisoundSound		*m_pSound;
	COtaServer		*m_pOta;

	// NAM lane: model (in the memory read from the SD) and instance state, all on the heap
	adelnam::Model		m_Model[BD_MAX_NAM];
	adelnam::Instance	m_Inst[BD_MAX_NAM];
	adelnam::Instance	*m_pInstPtr[BD_MAX_NAM];
	CFx		*m_Fx[BD_MAX_BLOCKS];		// effect instances by block id
	CFx		**m_pFx;			// = m_Fx
	u8			*m_pModelRaw[BD_MAX_NAM];
	u8			*m_pStateRaw[BD_MAX_NAM];
	size_t			m_nStateBytes[BD_MAX_NAM];

	CBoard			m_Board;		// the pedalboard (ADELGrid)
	CBoard			*m_pBoard;		// = &m_Board
	TWarm			m_Warm[WARM_CACHE];
	unsigned		m_nWarm, m_nWarmNext;
	unsigned		m_nMidiTraceAt, m_nMidiTraceApply;		// measurement: instant (ms) of the arrival of the Program Change and of the start of the change (0 = no measurement)
	char			m_WarmFiles[WARM_FILES][BD_FILE_LEN];		// preset models still to be put in cache
	unsigned		m_nWarmFiles, m_nWarmDone, m_nWarmScan;		// m_nWarmScan: 0 = to do, 1 = in progress, 2 = finished
	CBoard			*m_pStage;
	CBoard			*m_pPreBoard[PRE_MAX];		// presets already read and parsed (faster change)
	boolean			m_bPreBoardOk[PRE_MAX];		// document of the preset being prepared (heap, created at the first change)
	CFx			*m_pStageFx[BD_MAX_BLOCKS];	// effect instances of the preset being prepared
	unsigned		m_nGhostSeq;
	unsigned		m_nMode;
	TPlan			m_Plan[2];		// active plan and plan being prepared
	unsigned		m_nPlanActive;
	boolean			m_bBoardSavePending;
	unsigned		m_nBoardSaveAt;
	unsigned		m_nChunkFramesRun;

	// control page
	CParams			m_Params;			// blocks 0..NAM_MAX_LANES-1 = lanes, then "master"
	TCtlStatus		m_Status;
	TCtlContext		m_Ctl;
	char			m_Lib[LIB_MAX_FILES][PARAM_TEXT_LEN];
	volatile unsigned	m_nLib;
	char			m_Ir[LIB_MAX_IR][PARAM_TEXT_LEN];	// cabinet IRs (.air)
	volatile unsigned	m_nIr;
	char			m_LibInf[LIB_MAX_FILES][UP_INF_MAX + 8];	// author and license of the models (NAME.inf)
	char			m_Fav[LIB_MAX_FILES][PARAM_TEXT_LEN];	// favorite models
	volatile unsigned	m_nFav;
	char			m_BpName[BP_MAX][BP_NAME_LEN];	// saved block settings
	char			m_BpData[BP_MAX][BP_DATA_LEN];
	volatile unsigned	m_nBp;
	char			m_PreName[PRE_MAX][BP_NAME_LEN];	// pedalboard presets by position (all the modes)
	u8			m_PreMode[PRE_MAX];		// mode of each preset
	char			m_PreView[PRE_MAX][BP_NAME_LEN];	// only those of the active mode (view given to the page)
	unsigned		m_nPreMark;			// counter of the modifications at load or save
	boolean			m_bPreDirtyBoot;		// modified before the last reboot
	char			m_PreCurWritten[BP_NAME_LEN];
	boolean			m_bPreDirtyWritten;
	unsigned		m_nTick;			// tick da 100 ms
	unsigned		m_nSaveAtTick;
	boolean			m_bSavePending;
	unsigned		m_nPeakLog;			// input peak since the last log line
	unsigned		m_nPeakUi;
	unsigned		m_nPeakUiL;
	unsigned		m_nPeakUiR;
	unsigned		m_nPeakUiTick;
	float			m_fOutPkL, m_fOutPkR, m_fOutSqL, m_fOutSqR;	// accumulation of the outputs for the current second
	unsigned		m_nOutFrames;
	unsigned		m_nTestUntilTick;
	unsigned		m_nStuckSince;

#ifdef MC_PROBE
	class CMcProbe		*m_pMc;
#endif
	volatile unsigned	m_nNetStage;		// 0..8: network steps passed
	unsigned		m_nLedTick;
	boolean			m_bSdOk;
	boolean			m_bSafeMode;
	boolean			m_bApMode;		// this boot is in the fallback network (access point)
	boolean			m_bConcert;		// this boot is in concert mode: without network
	u32			m_VerifiedSig[48];	// models whose self-check has already been done since boot (name and size signature): at preset change it is not redone
	unsigned		m_nVerified;

	// MIDI (midi.h): USB input (MIDI task every 5 ms), queue, map in the presets, echo to the controller
	friend class CKernelMidiTask;
	void MidiInit (void);
	void MidiStart (void);			// creates the MIDI task (after the network startup)
	void MidiTick (void);
	void MidiApply (const TMidiAct &Act, unsigned long long nUs);
	void MidiSend (const u8 *pData, unsigned nLen);
	void MidiEchoClass (unsigned nClass);
	void MidiEchoState (void);			// sends the echo of the state changes (classes and preset)
	void MidiSendState (void);			// status SysEx
	void MidiPresetWant (int nSlot);
	void MidiCfgRead (void);
	void MidiCfgWrite (void);
	void ApCfgRead (void);
	void ApCfgWrite (void);
	boolean ApFlagTake (void);
	void ApFlagPut (void);
	boolean ConcertFlagTake (void);
	void ConcertFlagPut (void);
	void DmaAlarmLog (void);
	void DmaAlarmLoad (void);
	void ApLogPut (const char *pText);
	void ApDrvFlush (void);
	void ApLogLoad (void);
	void ThermalTick (void);
	void ThermalInit (void);
	void McInit (void);
	void McCfgWrite (void);
	void ThermalCfgRead (void);
	void ThermalCfgWrite (void);
	void ApReboot (void);		// reboots into the fallback network
	static void MidiPacketHandler (unsigned nCable, u8 *pPacket, unsigned nLength, unsigned nDevice, void *pParam);
	static void MidiRemovedHandler (CDevice *pDevice, void *pContext);
	CMidiQueue		m_MidiQ;
	CMidiMonitor		m_MidiMon;
	CMidiParser		m_MidiParser;		// used only by the USB handler
	CMidiClock		m_MidiClock;
	CTapTempo		m_MidiTap;
	CUSBMIDIDevice		*volatile m_pUsbMidi;
	unsigned		m_nMidiPnpAt;
	unsigned		m_nSyncedBpm10;	// last tempo (tenths of bpm) applied to the ADEL delays
	CFeatures		*m_pFeat;		// service functions with a switch (all off at startup)
	void FeatInit (void);
	void FeatCfgRead (void);
	void FeatCfgWrite (void);
	CPisoundDin		*m_pDin;		// Pisound DIN socket (via SPI0), created by the MIDI task
	unsigned		m_nDinState;		// 0 to start, 1 reset low, 2 waiting for MCU start, 3 active, 4 given up
	unsigned		m_nDinAt;
	unsigned		m_nDinTries;
	CMidiParser		m_DinParser;
	void MidiFeedByte (CMidiParser *pParser, u8 nByte);
	void MidiDinTick (unsigned nMs, unsigned long long nUs);
	int			m_nMidiPcWant;		// position of the requested preset (-1 = none), applied after 300 ms without other Program Changes
	unsigned		m_nMidiPcAt;
	unsigned		m_nMidiLastMask;
	int			m_nMidiLastPc;
	boolean			m_bMidiMaskValid;
};

#endif
