//
// kernel.cpp - Pisound v1.2 + Wi-Fi + syslog + OTA updates test on Circle (Raspberry Pi 5), without a monitor.
//
// Startup: Initialize -> SD (USB, EMMC, FatFs) -> boot counter -> Pisound pins -> audio in the DMA callback
//        -> watchdog -> Wi-Fi (WLAN, network, WPA) -> DHCP -> syslog -> OTA server.
// Protection: the SD:/boot.cnt counter grows at every boot and goes back to 0 after STABLE_SECONDS of stable operation
//        (or after a successful OTA update). With the watchdog (WATCHDOG_SECONDS) a hang causes a reboot;
//        at the BOOT_MAX_FAILS-th consecutive unstable boot the Pi starts in SAFE MODE (SAFE_*), reachable over Wi-Fi.
// ACT LED: 7.5 s cycle always repeated = 2 s of very fast blinking, 1 s pause, N slow blinks (N = network steps 0..8).
//
#include "kernel.h"
#include "fancurve.h"
#include "namcal.h"
#include "fx.h"
#include "config.h"
#include <circle/net/syslogdaemon.h>
#include <circle/net/mdnsdaemon.h>
#include <circle/bcmpropertytags.h>
#include "dhcpserver.h"
#include "graph.h"
#ifdef ARM_ALLOW_MULTI_CORE
#include <circle/multicore.h>
#endif
#ifdef MC_PROBE
#include "mcprobe.h"
#endif
#include <circle/net/ipaddress.h>
#include <circle/string.h>
#include <circle/memory.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/sched/task.h>
#include <circle/startup.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <assert.h>

// instructions repeated by the exception handler (Circle's patched stub, see patch_circle_stub.py) after a fetch of a zeroed line
extern "C" unsigned long long ExcRetryTotal;


#define DRIVE		"SD:"
#define FIRMWARE_PATH	DRIVE "/firmware/"
#define CONFIG_FILE	DRIVE "/wpa_supplicant.conf"
#define SETTINGS_FILE	DRIVE "/settings.cfg"
#define SETTINGS_TMP	DRIVE "/settings.tmp"


// work buffer shared by the functions that read and write the large files (pedalboards, presets, block settings): they run one at a time in the main loop.
// Six static buffers of 32-40 KB pushed the end of the kernel beyond Circle's limit (KERNEL_MAX_SIZE = 2 MB, allowed end 0x280000): beyond it, the boot stops without signals.
// shared work area (presets, grids, block presets): on the heap at the first request, so it does not weigh on the kernel image (2 MB limit)
#define SCRATCH_SIZE	(BP_MAX * (BP_NAME_LEN + BP_DATA_LEN + 2) + 16)
typedef char ScratchBigEnough[SCRATCH_SIZE >= 32768 + 64 ? 1 : -1];
static char *s_pScratch = 0;
static char *Scratch (void)
{
	if (s_pScratch == 0)
	{
		s_pScratch = new char[SCRATCH_SIZE];
	}
	return s_pScratch;
}

static const char FromKernel[] = "kernel";

static CPisoundSound *s_pDiSound = 0;
static unsigned DiReadTrampoline (unsigned nFrom, unsigned nCount, u8 *pOut)
{
	return s_pDiSound != 0 ? s_pDiSound->DiRead (nFrom, nCount, pOut) : 0;
}
static const u8 SysLogServer[] = SYSLOG_SERVER_IP;

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
#ifdef MIDI_HW
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),		// with hot-plug device detection (needed by UpdatePlugAndPlay)
#else
	m_USBHCI (&m_Interrupt, &m_Timer),
#endif
	m_EMMC (&m_Interrupt, &m_Timer, 0),		// without LED: ACT is used for the progress signals
	m_WLAN (FIRMWARE_PATH),
	m_Net (0, 0, 0, 0, "namberry5", NetDeviceTypeWLAN),
	m_WPASupplicant (CONFIG_FILE),
	m_Osr0 (PIN_OSR0, GPIOModeOutput),
	m_Osr1 (PIN_OSR1, GPIOModeOutput),
	m_Osr2 (PIN_OSR2, GPIOModeOutput),
	m_ResetAudio (PIN_RESET_AUDIO, GPIOModeOutput),
	m_ResetMcu (PIN_RESET_MCU, GPIOModeOutput),
	m_Button (PIN_BUTTON, GPIOModeInputPullUp),
	m_pSound (0),
	m_pOta (0),
	m_nChunkFramesRun (0),
	m_nLib (0),
	m_nIr (0),
	m_nFav (0),
	m_nBp (0),
	m_nPreMark (0),
	m_bPreDirtyBoot (FALSE),
	m_bPreDirtyWritten (FALSE),
	m_nTick (0),
	m_nSaveAtTick (0),
	m_bSavePending (FALSE),
	m_nPeakLog (0),
	m_nPeakUi (0),
	m_nPeakUiL (0),
	m_nPeakUiR (0),
	m_nPeakUiTick (0),
	m_fOutPkL (0.0f), m_fOutPkR (0.0f), m_fOutSqL (0.0f), m_fOutSqR (0.0f),
	m_nOutFrames (0),
	m_nTestUntilTick (0),
	m_nStuckSince (0),
	m_nNetStage (0),
	m_bApMode (FALSE),
	m_bConcert (FALSE),
	m_nVerified (0),
	m_bFanOwned (FALSE),
	m_bMcFaultSeen (FALSE),
	m_nLedTick (0),
	m_bSdOk (FALSE),
	m_bSafeMode (FALSE),
	m_pUsbMidi (0),
	m_nMidiPnpAt (0),
	m_pDin (0),
	m_pFeat (0),
	m_nSyncedBpm10 (0),
	m_nDinState (0),
	m_nDinAt (0),
	m_nDinTries (0),
	m_nMidiPcWant (-1),
	m_nMidiPcAt (0),
	m_nMidiLastMask (0),
	m_nMidiLastPc (-1),
	m_bMidiMaskValid (FALSE)
{
	m_ActLED.Blink (5);
	for (unsigned i = 0; i < BD_MAX_NAM; i++)
	{
		m_pModelRaw[i] = 0;
		m_pStateRaw[i] = 0;
		m_nStateBytes[i] = 0;
		m_pInstPtr[i] = &m_Inst[i];
	}
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		m_Fx[i] = 0;
		m_pStageFx[i] = 0;
	}
	m_pStage = 0;
	m_nGhostSeq = 0;
	m_nWarm = m_nWarmNext = 0;
	m_nMidiTraceAt = m_nMidiTraceApply = 0;
	for (unsigned i = 0; i < PRE_MAX; i++)
	{
		m_pPreBoard[i] = 0;
		m_bPreBoardOk[i] = FALSE;
	}
	m_nWarmFiles = m_nWarmDone = m_nWarmScan = 0;
	memset (m_Warm, 0, sizeof m_Warm);
	m_Board.SetMode (2);
	m_nMode = 2;		// ADELFlow and ADELMatrix no longer exist: we always work in ADELGrid
	m_pBoard = &m_Board;
	m_pFx = m_Fx;
	m_nPlanActive = 0;
	m_bBoardSavePending = FALSE;
	m_nBoardSaveAt = 0;
}

CKernel::~CKernel (void)
{
}

// initializes only what the log needs: everything else is started in Run()
boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

#ifdef MC_PROBE
	if (bOK)
	{
		m_pMc = new CMcProbe (CMemorySystem::Get ());
		m_pMc->Initialize ();		// last, as Circle requires; if a core does not start the kernel goes on anyway (the measurement reports it)
	}
#endif

	return bOK;
}

// signal on the ACT LED: n blinks = step n passed (then a pause)
void CKernel::Stage (unsigned nStage)
{
	m_Logger.Write (FromKernel, LogNotice, "Step %u", nStage);
	m_ActLED.Blink (nStage, 250, 250);
	m_Timer.MsDelay (500);
}

// error: fast repeated blinks, with n slow blinks of code, forever (the watchdog, if active, reboots)
void CKernel::ErrorHalt (unsigned nCode)
{
	for (;;)
	{
		m_ActLED.Blink (8, 60, 60);
		m_Timer.MsDelay (700);
		m_ActLED.Blink (nCode, 400, 400);
		m_Timer.MsDelay (2000);
	}
}

// kernel timer, every 20 ms: drives the ACT LED according to the network steps passed
void CKernel::LedHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext)
{
	CKernel *pThis = static_cast<CKernel *> (pParam);

	// cycle of 375 ticks of 20 ms = 7.5 s, always repeated:
	//   tick   0-99  : very fast blinking (20 ms on, 20 ms off) for 2 s = start of the cycle
	//   tick 100-149 : 1 s pause (off)
	//   tick 150-...  : N slow blinks (200 ms on, 200 ms off), N = network steps passed (0..8)
	unsigned nTick = pThis->m_nLedTick++ % 375;
	boolean bOn = FALSE;
	if (nTick < 100)
	{
		bOn = (nTick % 2) == 0;
	}
	else if (nTick >= 150 && nTick < 150 + pThis->m_nNetStage * 20)
	{
		bOn = ((nTick - 150) / 10) % 2 == 0;
	}

	if (bOn)
	{
		pThis->m_ActLED.On ();
	}
	else
	{
		pThis->m_ActLED.Off ();
	}

	pThis->m_Timer.StartKernelTimer (MSEC2HZ (20), LedHandler, pThis, 0);
}

// Replicates the Linux driver's sequence (pisnd_spi_gpio_init + pisnd_init_gpio + pisnd_hw_params)
void CKernel::PisoundInit (unsigned nSampleRate)
{
	m_ResetMcu.Write (HIGH);
	m_ResetMcu.Write (LOW);
	m_Timer.MsDelay (1);
	m_ResetMcu.Write (HIGH);
	m_Timer.MsDelay (64);

	m_Osr0.Write (HIGH);
	m_Osr1.Write (HIGH);
	m_Osr2.Write (HIGH);
	m_ResetAudio.Write (HIGH);

	m_ResetAudio.Write (LOW);
	switch (nSampleRate)
	{
	case 96000:			// OSR0=1 OSR1=0 OSR2=1
		m_Osr0.Write (HIGH);
		m_Osr1.Write (LOW);
		m_Osr2.Write (HIGH);
		break;

	case 192000:			// OSR0=1 OSR1=1 OSR2=1
		m_Osr0.Write (HIGH);
		m_Osr1.Write (HIGH);
		m_Osr2.Write (HIGH);
		break;

	default:			// 48 kHz: OSR0=1 OSR1=0 OSR2=0
		m_Osr0.Write (HIGH);
		m_Osr1.Write (LOW);
		m_Osr2.Write (LOW);
		break;
	}
	m_ResetAudio.Write (HIGH);

	m_Timer.MsDelay (100);
}

static float SqrtF (float x)
{
	return DspSqrtF (x);
}

#define NAM_MAX_MODEL_BYTES	(256 * 1024)

// warms up the slot's instance (or restores the state copy) and, the first time the file is seen after boot, runs the self-check
void CKernel::NamPrepare (unsigned nLane, const char *pFile, const u8 *pData, size_t nSize, boolean *pVerified, boolean *pWarmHit)
{
	m_Watchdog.Start (WATCHDOG_SECONDS);
	// the warm-up (silence for the whole receptive field) costs 40-50 ms per model and always gives the same state: the first time it is saved, then it is restored with a copy
	u32 nWarmSig = (u32) nSize * 2654435761u;
	{
		u32 nCrc = 0;
		memcpy (&nCrc, pData + nSize - 4, 4);		// CRC32 of the file (already verified by ParseModel): a file replaced with the same name and the same size does not reuse the old state
		nWarmSig ^= nCrc;
	}
	*pWarmHit = WarmInstance (nLane, nWarmSig);
	m_Watchdog.Start (WATCHDOG_SECONDS);

	// self-check: deterministic signal (square wave + LCG noise) in blocks, same calculation as the program on the PC.
	// It is done only the first time a file is loaded after boot (it costs almost double the loading itself: when changing preset it does not need to be redone)
	u32 nVerSig = 2166136261u ^ (u32) nSize;
	for (const char *pName = pFile; *pName; pName++)
	{
		nVerSig = (nVerSig ^ (u8) *pName) * 16777619u;
	}
	boolean bVerified = FALSE;
	for (unsigned i = 0; i < m_nVerified; i++)
	{
		if (m_VerifiedSig[i] == nVerSig)
		{
			bVerified = TRUE;
		}
	}
	*pVerified = bVerified;
	if (!bVerified)
	{
		if (m_nVerified < sizeof m_VerifiedSig / sizeof m_VerifiedSig[0])
		{
			m_VerifiedSig[m_nVerified++] = nVerSig;
		}
		u32 nSeed = 12345;
		float In[8], Out[8];
		double fSum = 0.0;
		u32 nFirst[3] = {0, 0, 0};
		for (unsigned n = 0; n < 6000; n += 8)
		{
			for (unsigned j = 0; j < 8; j++)
			{
				nSeed = nSeed * 1664525u + 1013904223u;
				float fNoise = ((float) (int) (nSeed >> 16) - 32768.0f) * (0.02f / 32768.0f);
				In[j] = ((((n + j) / 100) & 1) ? 0.15f : -0.15f) + fNoise;
			}
			m_Inst[nLane].Process (In, Out, 8);
			for (unsigned j = 0; j < 8; j++)
			{
				fSum += (double) Out[j] * (double) Out[j];
			}
			if (n == 5992)
			{
				union { float f; u32 u; } c;
				for (unsigned k = 0; k < 3; k++)
				{
					c.f = Out[k];
					nFirst[k] = c.u;
				}
			}
		}
		union { float f; u32 u; } s;
		s.f = (float) fSum;
		m_Logger.Write (FromKernel, LogNotice, "NAM slot %u self-check: sum of squares %08X, outputs %08X %08X %08X",
				nLane, s.u, nFirst[0], nFirst[1], nFirst[2]);
		m_Inst[nLane].Reset ();
		WarmInstance (nLane, nWarmSig);
		m_Watchdog.Start (WATCHDOG_SECONDS);
	}
}

// model file in the slot's buffer: if the in-memory copy has the same name and the same size and the 4 CRC bytes at the end of the file on the card are equal to those of the copy,
// the copy is used (no reading of the whole file, no CRC); otherwise it is read from the card
unsigned CKernel::ModelRead (const char *pFile, unsigned nLane, u8 **ppData, size_t *pnSize, boolean *pbFromCache, u32 *pNameHash)
{
	CString Path;
	Path.Format ("SD:/%s", pFile);
	u32 nHash = 2166136261u;
	for (const char *p = pFile; *p; p++)
	{
		nHash = (nHash ^ (u8) *p) * 16777619u;
	}
	*pNameHash = nHash;
	*pbFromCache = FALSE;
	if (nLane >= BD_MAX_NAM)
	{
		return 1;
	}
	if (m_pModelRaw[nLane] == 0)
	{
		m_pModelRaw[nLane] = new u8[NAM_MAX_MODEL_BYTES + 64];
	}
	u8 *pData = (u8 *) (((uintptr_t) m_pModelRaw[nLane] + 63) & ~(uintptr_t) 63);
	*ppData = pData;

	// in-memory copy with the same name: it is used without touching the card (the copy is thrown away when the library changes: upload from the page, TFTP, deletion; see ModelCacheFlush)
	for (unsigned i = 0; i < m_nWarm; i++)
	{
		const TWarm &W = m_Warm[i];
		if (W.pRaw != 0 && W.nRaw != 0 && W.NameHash == nHash && strcmp (W.RawName, pFile) == 0)
		{
			memcpy (pData, W.pRaw, W.nRaw);
			*pnSize = W.nRaw;
			*pbFromCache = TRUE;
			return 0;
		}
	}
	FIL File;
	if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return 1;
	}
	const FSIZE_t nSize = f_size (&File);
	if (nSize < 64 || nSize > NAM_MAX_MODEL_BYTES)
	{
		f_close (&File);
		return 2;
	}
	UINT nRead = 0;
	const FRESULT Result = f_read (&File, pData, (UINT) nSize, &nRead);
	f_close (&File);
	if (Result != FR_OK || nRead != nSize)
	{
		return 3;
	}
	*pnSize = (size_t) nSize;
	return 0;
}

// the model library has changed (file uploaded, received via TFTP or deleted): the file copies are no longer valid and ModelRead goes back to reading from the card
// (the warmed-up states stay valid: they are tied to the file's CRC)
void CKernel::ModelCacheFlush (void)
{
	for (unsigned i = 0; i < m_nWarm; i++)
	{
		m_Warm[i].nRaw = 0;
		m_Warm[i].NameHash = 0;
	}
}
// after a successful load from the card: copy of the file into the cache entry that has this model's warmed-up state
void CKernel::ModelCacheStore (u32 nNameHash, const char *pFile, const u8 *pData, size_t nSize)
{
	u32 nCrc = 0;
	memcpy (&nCrc, pData + nSize - 4, 4);
	const u32 nSig = (u32) nSize * 2654435761u ^ nCrc;
	for (unsigned i = 0; i < m_nWarm; i++)
	{
		TWarm &W = m_Warm[i];
		if (W.Sig == nSig)
		{
			if (W.pRaw == 0 || W.nRawCap < nSize)
			{
				W.pRaw = new u8[nSize];
				W.nRawCap = nSize;
			}
			memcpy (W.pRaw, pData, nSize);
			W.nRaw = (u32) nSize;
			W.NameHash = nNameHash;
			CParams::Copy (W.RawName, pFile, sizeof W.RawName);
			return;
		}
	}
}
// brings the slot's instance to the "warmed-up" state: if for this model (nSig) there already is a copy of the state it restores it, otherwise it warms up and saves the copy. Returns TRUE if it used the copy.
boolean CKernel::WarmInstance (unsigned nLane, u32 nSig)
{
	adelnam::Instance &Inst = m_Inst[nLane];
	const size_t nFloats = Inst.WarmFloats ();
	for (unsigned i = 0; i < m_nWarm; i++)
	{
		if (m_Warm[i].Sig == nSig && m_Warm[i].nFloats == nFloats)
		{
			Inst.WarmLoad (m_Warm[i].pData, m_Warm[i].Pos);
			return TRUE;
		}
	}
	Inst.Prewarm ();
	const unsigned k = m_nWarm < WARM_CACHE ? m_nWarm++ : (m_nWarmNext++ % WARM_CACHE);
	TWarm &W = m_Warm[k];
	W.nRaw = 0;
	W.NameHash = 0;
	if (W.pData == 0 || W.nCap < nFloats)
	{
		W.pData = new float[nFloats];
		W.nCap = nFloats;
	}
	W.Sig = nSig;
	W.nFloats = nFloats;
	Inst.WarmSave (W.pData, W.Pos);
	return FALSE;
}

// reads the model of NAM block nId (SD:/<file>), checks the format (CRC included) and warms up its slot's instance.
// The caller has already removed the block from the audio plan (Loaded = FALSE and RebuildPlan).
boolean CKernel::LoadSlot (int nId, CBoard *pBoard)
{
	const u32 nT0 = CTimer::GetClockTicks ();
	TBdBlock &Bl = (pBoard != 0 ? pBoard : m_pBoard)->Block ((unsigned) nId);
	const unsigned nLane = (unsigned) Bl.Slot;		// slot index
	Bl.Loaded = FALSE;
	CParams::Copy (Bl.FailedFile, Bl.File, sizeof Bl.FailedFile);		// considered failed until it succeeds
	if (!m_bSdOk || m_pSound == 0 || nLane >= BD_MAX_NAM)
	{
		CParams::Copy (Bl.Status, "card not ready", sizeof Bl.Status);
		return FALSE;
	}

	CString Path;
	Path.Format ("SD:/%s", Bl.File);
	u8 *pData = 0;
	size_t nSize = 0;
	boolean bFromCache = FALSE;
	u32 nNameHash = 0;
	const u32 nTRead0 = CTimer::GetClockTicks ();
	const unsigned nRd = ModelRead (Bl.File, nLane, &pData, &nSize, &bFromCache, &nNameHash);
	if (nRd != 0)
	{
		static const char *const Msg[4] = {"", "missing file", "invalid file (size)", "read error"};
		m_Logger.Write (FromKernel, nRd == 1 ? LogNotice : LogError, "NAM slot %u: %s (%s)", nLane, Msg[nRd], (const char *) Path);
		CParams::Copy (Bl.Status, Msg[nRd], sizeof Bl.Status);
		return FALSE;
	}
	const u32 nTParse0 = CTimer::GetClockTicks ();

	const char *pError = adelnam::ParseModel (pData, nSize, &m_Model[nLane], !bFromCache);
	if (pError != 0)
	{
		m_Logger.Write (FromKernel, LogError, "NAM slot %u: invalid model: %s", nLane, pError);
		CParams::Copy (Bl.Status, "invalid model", sizeof Bl.Status);
		return FALSE;
	}
	const adelnam::Model &Model = m_Model[nLane];
	const size_t nNeeded = adelnam::Instance::StateBytes (Model, (int) m_nChunkFramesRun);
	if (nNeeded > m_nStateBytes[nLane])
	{
		delete[] m_pStateRaw[nLane];
		m_pStateRaw[nLane] = new u8[nNeeded + 64];
		m_nStateBytes[nLane] = nNeeded;
	}
	if (!m_Inst[nLane].Init (&Model, m_pStateRaw[nLane], m_nStateBytes[nLane] + 64, (int) m_nChunkFramesRun))
	{
		m_Logger.Write (FromKernel, LogError, "NAM slot %u: insufficient state memory", nLane);
		CParams::Copy (Bl.Status, "insufficient memory", sizeof Bl.Status);
		return FALSE;
	}

	boolean bVerified = FALSE, bWarmHit = FALSE;
	const u32 nTPrep0 = CTimer::GetClockTicks ();
	NamPrepare (nLane, Bl.File, pData, nSize, &bVerified, &bWarmHit);
	const u32 nTPrep1 = CTimer::GetClockTicks ();
	if (!bFromCache)
	{
		ModelCacheStore (nNameHash, Bl.File, pData, nSize);
	}

	// calibration: input = maximum input level (page, default NAM_CAL_DBU) - model send level + offset (page, default +6 dB like ADELnam on Linux),
	// "Normalized" output = -18 - loudness; recomputed by the main loop when the calibration is changed (ApplyMaster)
	const float fInDbu = Model.inLevelDbu;
	Bl.HasModelIn = (fInDbu == fInDbu);
	Bl.ModelInDbu = Bl.HasModelIn ? fInDbu : 0.0f;
	float fInDb = NamInputDb (m_Params.Block (1).P[0].Value, Bl.ModelInDbu, Bl.HasModelIn, Bl.V[5] > 0.5f ? 0.0f : m_Params.Block (1).P[1].Value);		// for the log only: the plan computes the real gain
	const float fLoud = Model.loudness;
	float fOutDb = (fLoud == fLoud) ? -18.0f - fLoud : 0.0f;
	const float fInGain = DbToLin (fInDb);
	const float fOutGain = DbToLin (fOutDb);

	Bl.BaseIn = fInGain;
	Bl.BaseOut = fOutGain;
	CParams::Copy (Bl.Name, Model.name, sizeof Bl.Name);
	CParams::Copy (Bl.LoadedFile, Bl.File, sizeof Bl.LoadedFile);
	CParams::Copy (Bl.Status, "ok", sizeof Bl.Status);
	Bl.FailedFile[0] = 0;
	Bl.Loaded = TRUE;
	m_Logger.Write (FromKernel, LogNotice,
			"NAM slot %u: \"%s\" loaded from %s (%u bytes), %u channels, quality 1.0, input %d.%02u dB, output %d.%02u dB, in %u ms (read %u us%s, parse+init %u us, warm-up %u us)%s",
			nLane, Model.name, (const char *) Path, (unsigned) nSize, Model.channels,
			(int) fInDb, (unsigned) (((fInDb < 0 ? -fInDb : fInDb) - (float) (int) (fInDb < 0 ? -fInDb : fInDb)) * 100.0f),
			(int) fOutDb, (unsigned) (((fOutDb < 0 ? -fOutDb : fOutDb) - (float) (int) (fOutDb < 0 ? -fOutDb : fOutDb)) * 100.0f),
			(unsigned) ((CTimer::GetClockTicks () - nT0) / 1000), (unsigned) (nTParse0 - nTRead0), bFromCache ? " from the copy" : "", (unsigned) (nTPrep0 - nTParse0), (unsigned) (nTPrep1 - nTPrep0), bVerified ? (bWarmHit ? " (warmed-up state from copy)" : "") : " (with self-check)");
	return TRUE;
}

// valid file name for a model: letters, digits, _ - and a final ".adn"
static boolean ValidModelFile (const char *p)
{
	unsigned n = strlen (p);
	if (n < 5 || n >= PARAM_TEXT_LEN || strcmp (p + n - 4, ".adn") != 0)
	{
		return FALSE;
	}
	for (unsigned i = 0; i + 4 < n; i++)
	{
		char c = p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
		{
			return FALSE;
		}
	}
	return TRUE;
}

// valid file name for a cabinet IR: letters, digits, _ - and a final ".air"
static boolean ValidIrFile (const char *p)
{
	return strlen (p) < PARAM_TEXT_LEN && BoardValidIrName (p);
}

// input trim (dB) of the active guitar profile: "guitar" block, P[0] = active guitar (1..4), P[1..4] = trim of each
static float GuitarTrimDb (const TBlock &G)
{
	int i = (int) (G.P[0].Value + 0.5f) - 1;
	i = i < 0 ? 0 : (i > 3 ? 3 : i);
	return G.P[1 + i].Value;
}

// general parameters (volume, bypass), saved settings, model library, pedalboard
void CKernel::ControlInit (void)
{
	unsigned m = m_Params.AddBlock ("master", "Master output");
	m_Params.AddParam (m, "vol", "Volume", "dB", ParamSlider, -40.0f, 6.0f, 0.5f, 0.0f);
	m_Params.AddParam (m, "byp", "Bypass", "", ParamToggle, 0.0f, 1.0f, 1.0f, 0.0f);
	CParams::Copy (m_Params.Block (m).Info, "after the chains; Bypass = inputs straight to the outputs", sizeof m_Params.Block (m).Info);
	m_Params.Block (m).Loaded = TRUE;

	// NAM calibration (block 1): maximum input level of the interface and offset; they appear in the Input panel
	const unsigned c = m_Params.AddBlock ("cal", "NAM calibration");
	m_Params.AddParam (c, "maxin", "Max input level", "dBu", ParamSlider, -20.0f, 30.0f, 0.5f, NAM_CAL_DBU);
	m_Params.AddParam (c, "ofs", "Calibration offset", "dB", ParamSlider, -12.0f, 12.0f, 0.5f, NAM_CAL_OFFSET_DB);
	CParams::Copy (m_Params.Block (c).Info, "dBu of 0 dBFS with the Pisound Gain at 0; offset +6 = historical ADELnam sound, 0 = TONE3000 guide", sizeof m_Params.Block (c).Info);
	m_Params.Block (c).Loaded = TRUE;

	// guitar profiles (block 2): an input trim for each guitar and the active one; they apply to all presets and are changed from the Input panel
	const unsigned gb = m_Params.AddBlock ("guitar", "Guitar");
	m_Params.AddParam (gb, "sel", "Active guitar", "", ParamSlider, 1.0f, 4.0f, 1.0f, 1.0f);
	m_Params.AddParam (gb, "t1", "Guitar 1: trim", "dB", ParamSlider, -24.0f, 12.0f, 0.5f, 0.0f);
	m_Params.AddParam (gb, "t2", "Guitar 2: trim", "dB", ParamSlider, -24.0f, 12.0f, 0.5f, 0.0f);
	m_Params.AddParam (gb, "t3", "Guitar 3: trim", "dB", ParamSlider, -24.0f, 12.0f, 0.5f, 0.0f);
	m_Params.AddParam (gb, "t4", "Guitar 4: trim", "dB", ParamSlider, -24.0f, 12.0f, 0.5f, 0.0f);
	m_Params.AddParam (gb, "n1", "Guitar 1 name", "", ParamText, 0.0f, 0.0f, 0.0f, 0.0f, "Guitar 1");
	m_Params.AddParam (gb, "n2", "Guitar 2 name", "", ParamText, 0.0f, 0.0f, 0.0f, 0.0f, "Guitar 2");
	m_Params.AddParam (gb, "n3", "Guitar 3 name", "", ParamText, 0.0f, 0.0f, 0.0f, 0.0f, "Guitar 3");
	m_Params.AddParam (gb, "n4", "Guitar 4 name", "", ParamText, 0.0f, 0.0f, 0.0f, 0.0f, "Guitar 4");
	CParams::Copy (m_Params.Block (gb).Info, "input trim per guitar, before all the blocks", sizeof m_Params.Block (gb).Info);
	m_Params.Block (gb).Loaded = TRUE;

	memset (&m_Status, 0, sizeof m_Status);
	m_Status.nSampleRate = m_pSound->GetSampleRate ();
	m_Status.nChunkFrames = m_nChunkFramesRun;
	m_Status.bSafeMode = m_bSafeMode;
	m_Status.nShed = 0;

	m_Ctl.pParams = &m_Params;
	m_Ctl.pStatus = &m_Status;
	m_Ctl.pLib = m_Lib;
	m_Ctl.pLibCount = &m_nLib;
	m_Ctl.pIr = m_Ir;
	m_Ctl.pIrCount = &m_nIr;
	m_Ctl.pLibInf = m_LibInf;
	for (unsigned i = 0; i < LIB_MAX_FILES; i++)
	{
		m_LibInf[i][0] = 0;
	}
	m_Ctl.pUpBuf = new u8[UP_MAX + 64];
	m_Ctl.UpName[0] = 0;
	m_Ctl.UpTotal = m_Ctl.UpGot = 0;
	m_Ctl.UpPending = 0;
	m_Ctl.UpResName[0] = 0;
	m_Ctl.UpResOk = 0;
	m_Ctl.UpResMsg[0] = 0;
	m_Ctl.pBoard = m_pBoard;
	m_Ctl.pFx = m_pFx;
	m_Ctl.Mode = 2;
	m_Ctl.ModePending = MODE_NONE;
	m_Ctl.PreFull = FALSE;
	m_Ctl.Token[0] = 0;
	m_Ctl.DelName[0] = 0;
	m_Ctl.DelPending = FALSE;
	m_Ctl.TestLock = 0;
	m_Ctl.pFav = m_Fav;
	m_Ctl.pFavCount = &m_nFav;
	m_Ctl.FavDirty = FALSE;
	m_BpName = new char[BP_MAX][BP_NAME_LEN];
	m_BpData = new char[BP_MAX][BP_DATA_LEN];
	memset (m_BpName, 0, (size_t) BP_MAX * BP_NAME_LEN);
	memset (m_BpData, 0, (size_t) BP_MAX * BP_DATA_LEN);
	m_Ctl.pBpName = m_BpName;
	m_Ctl.pBpData = m_BpData;
	m_Ctl.pBpCount = &m_nBp;
	m_Ctl.BpDirty = FALSE;
	m_Ctl.BpVer = 1;
	m_Ctl.pPreName = m_PreName;
	m_Ctl.pPreMode = m_PreMode;
	m_Ctl.PreCur[0] = 0;
	m_Ctl.PreArgMode = 0;
	m_Ctl.PreDirty = FALSE;
	m_Ctl.PrePending = 0;
	m_Ctl.PreArg[0] = 0;
	m_PreCurWritten[0] = 0;
	for (unsigned i = 0; i < PRE_MAX; i++)
	{
		m_PreName[i][0] = 0;
		m_PreView[i][0] = 0;
		m_PreMode[i] = 0;
	}
	m_Ctl.TestPending = FALSE;
	m_Ctl.TestMask = 0;
	m_Ctl.DiReq = 0;
	m_Ctl.DiSeconds = 0;
	m_Ctl.DiAppend = 0;
	m_Ctl.DiMax = 0;
	m_Ctl.DiMarkN = 0;
	m_Ctl.DiState = 0;
	m_Ctl.DiFrames = 0;
	m_Ctl.DiTarget = 0;
	m_Ctl.pDiRead = DiReadTrampoline;

	if (m_bSdOk)
	{
		DmaAlarmLoad ();
		SettingsLoad ();
		CBoard::SetCal (m_Params.Block (1).P[0].Value, m_Params.Block (1).P[1].Value);
		CBoard::SetInTrim (GuitarTrimDb (m_Params.Block (2)));
		FavLoad ();
		BpLoad ();
		ScanLibrary ();
		BoardLoad ();
		PreScan ();
		PreCurRead ();
		m_nPreMark = m_pBoard->GetEdit ();
	}
	else
	{
		m_pBoard->Reset ();
	}
	m_Ctl.pBoard = m_pBoard;
	m_Ctl.pFx = m_pFx;
	m_Ctl.Mode = m_nMode;
	PreViewBuild ();
	RebuildPlan ();
	MidiInit ();
	FeatInit ();
}

// compiles the pedalboard into a new plan; the audio callback switches to the new plan with a single store
void CKernel::RebuildPlan (void)
{
	const unsigned nNext = m_nPlanActive ^ 1;
	TBlock &Master = m_Params.Block (0);
	// effects: the instances of the new blocks are created; those of removed blocks or blocks whose type changed are detached now and freed afterwards
	// once the audio callback has moved to the new plan (a single core: the callback cannot be halfway through the old plan)
	CFx *pOld[BD_MAX_BLOCKS];
	unsigned nOld = 0;
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		const TBdBlock &Bl = m_pBoard->Block (id);
		const boolean bWant = Bl.Used && Bl.Type >= FxFirst && Bl.Type <= FxLast;
		if (m_pFx[id] != 0 && (!bWant || m_pFx[id]->Type () != Bl.Type))
		{
			pOld[nOld++] = m_pFx[id];
			m_pFx[id] = 0;
		}
		if (bWant && m_pFx[id] == 0)
		{
			m_pFx[id] = FxCreate (Bl.Type, 48000.0f);
			if (Bl.Type == FxCabIR)
			{
				m_pBoard->Block (id).LoadedFile[0] = 0;		// new instance: the IR is reloaded
				m_pBoard->Block (id).FailedFile[0] = 0;
			}
		}
	}
	m_pBoard->Compile (&m_Plan[nNext], m_pInstPtr, DbToLin (Master.P[0].Value), Master.P[1].Value > 0.5f, m_pFx);
	m_pSound->SetPlan (&m_Plan[nNext]);
	m_nPlanActive = nNext;
	for (unsigned k = 0; k < nOld; k++)
	{
		delete pOld[k];
	}
}

void CKernel::LoadAllModels (void)
{
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		TBdBlock &Bl = m_pBoard->Block (id);
		if (Bl.Used && Bl.Type == NodeNam)
		{
			LoadSlot ((int) id);
			Feed ();
		}
	}
	RebuildPlan ();
}

void CKernel::BoardPath (CString *pPath, unsigned nMode)
{
	pPath->Format (DRIVE "/%s", nMode == 0 ? "board.cfg" : (nMode == 1 ? "boardm.cfg" : "boardg.cfg"));
}

void CKernel::PreCurPath (CString *pPath, unsigned nMode)
{
	pPath->Format (DRIVE "/%s", nMode == 0 ? "preset_cur.txt" : (nMode == 1 ? "preset_curm.txt" : "preset_curg.txt"));
}

// loads the grid from its file
void CKernel::BoardLoad (void)
{
	char (&Text)[32768] = *reinterpret_cast<char (*)[32768]> (Scratch ());
	CString Path;
	BoardPath (&Path, 2);
	boolean bOk = FALSE;
	boolean bDry = FALSE;		// grid rejected (safe mode or excessive load): start again from Input and Output linked, clean sound only
	FIL File;
	if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) == FR_OK)
	{
		UINT nRead = 0;
		FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
		if (Result == FR_OK)
		{
			Text[nRead] = 0;
			if (m_Board.Parse (Text))
			{
				if (m_bSafeMode || m_Board.EstimateLoad10 () > BD_LOAD_LIMIT10)
				{
					// grid that overloads the processor (or boot in safe mode): it is not loaded, the file is kept as .bad
					bDry = TRUE;
					m_Logger.Write (FromKernel, LogWarning, "Grid not loaded (%s, estimated load %u.%u%%): saved as boardg.bad, starting again from Input and Output linked (clean sound only)",
							m_bSafeMode ? "safe mode" : "excessive load", m_Board.EstimateLoad10 () / 10, m_Board.EstimateLoad10 () % 10);
					f_unlink (DRIVE "/boardg.bad");
					f_rename (Path, DRIVE "/boardg.bad");
				}
				else
				{
					m_Logger.Write (FromKernel, LogNotice, "Pedalboard loaded: %u NAM", m_Board.CountNam ());
					bOk = TRUE;
				}
			}
			else
			{
				m_Logger.Write (FromKernel, LogWarning, "%s invalid: restarting from the default configuration", (const char *) Path);
			}
		}
	}
	if (!bOk)
	{
		m_Board.Reset ();
		if (bDry)
		{
			m_Board.NetLink (BD_NET_IN, BD_NET_OUT, 1, 1, 0.0f);		// Input L -> Output L
			m_Board.NetLink (BD_NET_IN, BD_NET_OUT, 2, 2, 0.0f);		// Input R -> Output R
		}
	}
}

void CKernel::BoardSave (void)
{
	char (&Text)[32768] = *reinterpret_cast<char (*)[32768]> (Scratch ());
	const unsigned n = m_pBoard->Serialize (Text, sizeof Text);
	FIL File;
	if (f_open (&File, DRIVE "/board.tmp", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten = 0;
	FRESULT Result = f_write (&File, Text, n, &nWritten);
	f_close (&File);
	if (Result != FR_OK || nWritten != n)
	{
		m_Logger.Write (FromKernel, LogError, "Pedalboard: write failed");
		return;
	}
	CString Path;
	BoardPath (&Path, m_nMode);
	f_unlink (Path);
	if (f_rename (DRIVE "/board.tmp", Path) != FR_OK)
	{
		m_Logger.Write (FromKernel, LogError, "Pedalboard: rename failed");
	}
}

// view of the presets of the active mode (the names of the other modes stay hidden)
void CKernel::PreViewBuild (void)
{
	boolean bFull = TRUE;
	for (unsigned i = 0; i < PRE_MAX; i++)
	{
		if (m_PreName[i][0] == 0)
		{
			bFull = FALSE;
		}
		if (m_PreName[i][0] != 0 && m_PreMode[i] == m_nMode)
		{
			CParams::Copy (m_PreView[i], m_PreName[i], BP_NAME_LEN);
		}
		else
		{
			m_PreView[i][0] = 0;
		}
	}
	m_Ctl.PreFull = bFull;
}

// list of the .adn files in the card's root, in alphabetical order
// work area shared by the library functions (ScanLibrary, FavLoad, LibOrderAdd, FavSave): the main loop calls them one at a time, never nested, so separate buffers are not needed
// (they were six static arrays of about 130 KB in total). None of them relies on initial content: each one writes before reading.
typedef char TLibName[PARAM_TEXT_LEN];
static union TLibScratch
{
	struct { TLibName List[LIB_MAX_FILES]; TLibName Ord[LIB_MAX_FILES]; TLibName Out[LIB_MAX_FILES]; boolean Used[LIB_MAX_FILES]; char OText[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1) + 8]; TLibName Irs[LIB_MAX_IR]; } scan;
	struct { TLibName Names[LIB_MAX_FILES]; char Text[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1) + 8]; } order;
	char fav[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1)];
} s_Lib;

void CKernel::ScanLibrary (void)
{
	ModelCacheFlush ();		// the library may have changed: the in-memory copies of the files are no longer valid
	TLibName (&List)[LIB_MAX_FILES] = s_Lib.scan.List;
	unsigned nCount = 0;

	DIR Dir;
	if (f_opendir (&Dir, DRIVE "/") == FR_OK)
	{
		FILINFO Info;
		while (f_readdir (&Dir, &Info) == FR_OK && Info.fname[0] != 0 && nCount < LIB_MAX_FILES)
		{
			if (!(Info.fattrib & AM_DIR) && ValidModelFile (Info.fname))
			{
				unsigned i = nCount++;
				while (i > 0 && strcmp (List[i - 1], Info.fname) > 0)
				{
					memcpy (List[i], List[i - 1], PARAM_TEXT_LEN);
					i--;
				}
				CParams::Copy (List[i], Info.fname, PARAM_TEXT_LEN);
			}
		}
		f_closedir (&Dir);
	}

	// import order (libord.txt, from the oldest to the newest): the most recently imported models go on top,
	// those with no import date (already present before) stay below in alphabetical order
	{
		TLibName (&Ord)[LIB_MAX_FILES] = s_Lib.scan.Ord;
		TLibName (&Out)[LIB_MAX_FILES] = s_Lib.scan.Out;
		boolean (&Used)[LIB_MAX_FILES] = s_Lib.scan.Used;
		unsigned nOrd = 0;
		FIL OF;
		char (&OText)[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1) + 8] = s_Lib.scan.OText;
		if (f_open (&OF, DRIVE "/libord.txt", FA_READ | FA_OPEN_EXISTING) == FR_OK)
		{
			UINT nRead = 0;
			f_read (&OF, OText, sizeof OText - 1, &nRead);
			f_close (&OF);
			OText[nRead] = 0;
			char *p = OText;
			while (*p && nOrd < LIB_MAX_FILES)
			{
				char *pEnd = p;
				while (*pEnd && *pEnd != '\n' && *pEnd != '\r')
				{
					pEnd++;
				}
				const char Saved = *pEnd;
				*pEnd = 0;
				if (ValidModelFile (p))
				{
					CParams::Copy (Ord[nOrd++], p, PARAM_TEXT_LEN);
				}
				*pEnd = Saved;
				p = pEnd;
				while (*p == '\n' || *p == '\r')
				{
					p++;
				}
			}
		}
		for (unsigned i = 0; i < nCount; i++)
		{
			Used[i] = FALSE;
		}
		unsigned nOut = 0;
		for (unsigned k = nOrd; k > 0; k--)
		{
			for (unsigned i = 0; i < nCount; i++)
			{
				if (!Used[i] && strcmp (List[i], Ord[k - 1]) == 0)
				{
					Used[i] = TRUE;
					CParams::Copy (Out[nOut++], List[i], PARAM_TEXT_LEN);
				}
			}
		}
		for (unsigned i = 0; i < nCount; i++)
		{
			if (!Used[i])
			{
				CParams::Copy (Out[nOut++], List[i], PARAM_TEXT_LEN);
			}
		}
		for (unsigned i = 0; i < nCount; i++)
		{
			CParams::Copy (List[i], Out[i], PARAM_TEXT_LEN);
		}
	}

	memcpy (m_Lib, List, sizeof List);
	m_nLib = nCount;

	// cabinet IRs (.air), in alphabetical order
	{
		TLibName (&Irs)[LIB_MAX_IR] = s_Lib.scan.Irs;
		unsigned nIr = 0;
		DIR IDir;
		if (f_opendir (&IDir, DRIVE "/") == FR_OK)
		{
			FILINFO Info;
			while (f_readdir (&IDir, &Info) == FR_OK && Info.fname[0] != 0 && nIr < LIB_MAX_IR)
			{
				if (!(Info.fattrib & AM_DIR) && ValidIrFile (Info.fname))
				{
					unsigned i = nIr++;
					while (i > 0 && strcmp (Irs[i - 1], Info.fname) > 0)
					{
						memcpy (Irs[i], Irs[i - 1], PARAM_TEXT_LEN);
						i--;
					}
					CParams::Copy (Irs[i], Info.fname, PARAM_TEXT_LEN);
				}
			}
			f_closedir (&IDir);
		}
		memcpy (m_Ir, Irs, sizeof Irs);
		m_nIr = nIr;
		m_Logger.Write (FromKernel, LogNotice, "IR library: %u files", nIr);
	}

	// author and license next to each model (NAME.inf file, a short one-line text)
	for (unsigned i = 0; i < LIB_MAX_FILES; i++)
	{
		m_LibInf[i][0] = 0;
	}
	for (unsigned i = 0; i < nCount; i++)
	{
		CString Path;
		Path.Format ("SD:/%s", List[i]);
		char *pDot = (char *) (const char *) Path + strlen (Path) - 4;
		strcpy (pDot, ".inf");
		FIL F;
		if (f_open (&F, Path, FA_READ | FA_OPEN_EXISTING) == FR_OK)
		{
			UINT nRead = 0;
			f_read (&F, m_LibInf[i], UP_INF_MAX, &nRead);
			f_close (&F);
			m_LibInf[i][nRead] = 0;
			for (char *p = m_LibInf[i]; *p; p++)
			{
				if ((unsigned char) *p < 0x20)
				{
					*p = 0;
					break;
				}
			}
		}
	}
	m_Logger.Write (FromKernel, LogNotice, "Model library: %u files", nCount);

	// favorites of files that no longer exist are removed
	unsigned nKeep = 0;
	for (unsigned i = 0; i < m_nFav; i++)
	{
		boolean bExists = FALSE;
		for (unsigned j = 0; j < nCount; j++)
		{
			if (strcmp (List[j], m_Fav[i]) == 0)
			{
				bExists = TRUE;
			}
		}
		if (bExists)
		{
			if (nKeep != i)
			{
				memcpy (m_Fav[nKeep], m_Fav[i], PARAM_TEXT_LEN);
			}
			nKeep++;
		}
	}
	if (nKeep != m_nFav)
	{
		m_nFav = nKeep;
		m_Ctl.FavDirty = TRUE;
	}
}

// one file name per line
void CKernel::FavLoad (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/favorites.txt", FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	char (&Text)[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1)] = s_Lib.fav;
	UINT nRead = 0;
	FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	if (Result != FR_OK)
	{
		return;
	}
	Text[nRead] = 0;

	unsigned nCount = 0;
	char *p = Text;
	while (*p && nCount < LIB_MAX_FILES)
	{
		char *pEnd = p;
		while (*pEnd && *pEnd != '\n' && *pEnd != '\r')
		{
			pEnd++;
		}
		char Saved = *pEnd;
		*pEnd = 0;
		if (ValidModelFile (p))
		{
			CParams::Copy (m_Fav[nCount++], p, PARAM_TEXT_LEN);
		}
		*pEnd = Saved;
		p = pEnd;
		while (*p == '\n' || *p == '\r')
		{
			p++;
		}
	}
	m_nFav = nCount;
	m_Logger.Write (FromKernel, LogNotice, "Favorites loaded: %u", nCount);
}

// ---- pedalboard presets: one file on the card per position (pre00.cfg ...), first line "n <name>", then the pedalboard
static void PrePath (CString *pPath, unsigned nSlot)
{
	pPath->Format ("SD:/pre%02u.cfg", nSlot);
}

// records a newly imported model as the most recent (or moves it back to the top if it already existed)
void CKernel::LibOrderAdd (const char *pName)
{
	TLibName (&Names)[LIB_MAX_FILES] = s_Lib.order.Names;
	char (&Text)[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1) + 8] = s_Lib.order.Text;
	unsigned n = 0;
	FIL F;
	if (f_open (&F, DRIVE "/libord.txt", FA_READ | FA_OPEN_EXISTING) == FR_OK)
	{
		UINT nRead = 0;
		f_read (&F, Text, sizeof Text - 1, &nRead);
		f_close (&F);
		Text[nRead] = 0;
		char *p = Text;
		while (*p && n < LIB_MAX_FILES)
		{
			char *pEnd = p;
			while (*pEnd && *pEnd != '\n' && *pEnd != '\r')
			{
				pEnd++;
			}
			const char Saved = *pEnd;
			*pEnd = 0;
			if (ValidModelFile (p) && strcmp (p, pName) != 0)
			{
				CParams::Copy (Names[n++], p, PARAM_TEXT_LEN);
			}
			*pEnd = Saved;
			p = pEnd;
			while (*p == '\n' || *p == '\r')
			{
				p++;
			}
		}
	}
	if (n >= LIB_MAX_FILES)
	{
		for (unsigned i = 1; i < n; i++)
		{
			memcpy (Names[i - 1], Names[i], PARAM_TEXT_LEN);
		}
		n = LIB_MAX_FILES - 1;
	}
	CParams::Copy (Names[n++], pName, PARAM_TEXT_LEN);
	unsigned t = 0;
	for (unsigned i = 0; i < n; i++)
	{
		for (const char *s = Names[i]; *s && t + 2 < sizeof Text; s++)
		{
			Text[t++] = *s;
		}
		Text[t++] = '\n';
	}
	if (f_open (&F, DRIVE "/libord.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		f_write (&F, Text, t, &nWritten);
		f_close (&F);
	}
}

// CRC-32 checksum (the same as the .adn files)
static u32 Crc32 (const u8 *p, unsigned n)
{
	u32 c = 0xFFFFFFFFu;
	for (unsigned i = 0; i < n; i++)
	{
		c ^= p[i];
		for (unsigned k = 0; k < 8; k++)
		{
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
		}
	}
	return ~c;
}

// a file uploaded from the page has arrived whole: it is checked and written to the card
void CKernel::UpTick (void)
{
	if (m_Ctl.UpPending == 0)
	{
		return;
	}
	const char *pName = m_Ctl.UpName;
	const unsigned n = m_Ctl.UpTotal;
	const u8 *pData = m_Ctl.pUpBuf;
	const boolean bInf = strcmp (pName + strlen (pName) - 4, ".inf") == 0;
	const boolean bAir = strcmp (pName + strlen (pName) - 4, ".air") == 0;
	const char *pErr = 0;
	if (!m_bSdOk)
	{
		pErr = "card not ready";
	}
	else if (bAir)
	{
		// cabinet IR .air: "ADR1", samples (u32), channels (u32), little-endian floats, final CRC-32
		if (n < 20 || memcmp (pData, "ADR1", 4) != 0)
		{
			pErr = "not a .air IR";
		}
		else
		{
			const u32 nTaps = (u32) pData[4] | ((u32) pData[5] << 8) | ((u32) pData[6] << 16) | ((u32) pData[7] << 24);
			const u32 nCh = (u32) pData[8] | ((u32) pData[9] << 8) | ((u32) pData[10] << 16) | ((u32) pData[11] << 24);
			const u32 nCrc = (u32) pData[n - 4] | ((u32) pData[n - 3] << 8) | ((u32) pData[n - 2] << 16) | ((u32) pData[n - 1] << 24);
			if (nTaps < 1 || nTaps > CABIR_MAX_TAPS || nCh < 1 || nCh > 2 || n != 16 + 4 * nTaps * nCh)
			{
				pErr = "invalid IR (samples or channels)";
			}
			else if (Crc32 (pData, n - 4) != nCrc)
			{
				pErr = "the file is damaged (CRC)";
			}
		}
		if (pErr == 0 && m_nIr >= LIB_MAX_IR)
		{
			boolean bExists = FALSE;
			for (unsigned i = 0; i < m_nIr; i++)
			{
				if (strcmp (m_Ir[i], pName) == 0)
				{
					bExists = TRUE;
				}
			}
			if (!bExists)
			{
				pErr = "IR library full";
			}
		}
	}
	else if (!bInf)
	{
		// .adn model: "ADN1" header and final CRC-32 (as the engine reads it)
		if (n < 128 || memcmp (pData, "ADN1", 4) != 0)
		{
			pErr = "not an .adn model";
		}
		else
		{
			const u32 nCrc = (u32) pData[n - 4] | ((u32) pData[n - 3] << 8) | ((u32) pData[n - 2] << 16) | ((u32) pData[n - 1] << 24);
			if (Crc32 (pData, n - 4) != nCrc)
			{
				pErr = "the file is damaged (CRC)";
			}
		}
		if (pErr == 0 && m_nLib >= LIB_MAX_FILES)
		{
			boolean bExists = FALSE;
			for (unsigned i = 0; i < m_nLib; i++)
			{
				if (strcmp (m_Lib[i], pName) == 0)
				{
					bExists = TRUE;
				}
			}
			if (!bExists)
			{
				pErr = "library full";
			}
		}
	}
	else
	{
		for (unsigned i = 0; i < n; i++)
		{
			if (pData[i] < 0x20 || pData[i] == 0x7F)
			{
				pErr = "invalid text";
			}
		}
	}

	if (pErr == 0)
	{
		CString Path;
		Path.Format ("SD:/%s", pName);
		FIL File;
		if (f_open (&File, Path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		{
			pErr = "write to the card failed";
		}
		else
		{
			UINT nWritten = 0;
			FRESULT Result = f_write (&File, pData, n, &nWritten);
			f_close (&File);
			if (Result != FR_OK || nWritten != n)
			{
				pErr = "write to the card incomplete";
			}
		}
	}
	if (pErr == 0)
	{
		if (!bInf && !bAir)
		{
			LibOrderAdd (pName);
		}
		ScanLibrary ();
		if (bAir)
		{
			// an IR replaced with the same name must be re-read by the blocks that use it
			for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
			{
				TBdBlock &Bl = m_Board.Block (id);
				if (Bl.Used && Bl.Type == FxCabIR && strcmp (Bl.File, pName) == 0)
				{
					Bl.LoadedFile[0] = 0;
					Bl.FailedFile[0] = 0;
				}
			}
		}
		CParams::Copy (m_Ctl.UpResMsg, "ok", sizeof m_Ctl.UpResMsg);
		m_Ctl.UpResOk = 1;
		m_Logger.Write (FromKernel, LogNotice, "Uploaded from the page: %s (%u bytes)", pName, n);
	}
	else
	{
		CParams::Copy (m_Ctl.UpResMsg, pErr, sizeof m_Ctl.UpResMsg);
		m_Ctl.UpResOk = -1;
		m_Logger.Write (FromKernel, LogWarning, "Upload of %s refused: %s", pName, pErr);
	}
	m_Ctl.UpPending = 0;
}

void CKernel::PreScan (void)
{
	unsigned nFound = 0;
	for (unsigned i = 0; i < PRE_MAX; i++)
	{
		m_PreName[i][0] = 0;
		m_PreMode[i] = 0;
		CString Path;
		PrePath (&Path, i);
		FIL File;
		if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		{
			continue;
		}
		char Head[BP_NAME_LEN + 24];
		UINT nRead = 0;
		f_read (&File, Head, sizeof Head - 1, &nRead);
		f_close (&File);
		Head[nRead] = 0;
		if (Head[0] == 'n' && Head[1] == ' ')
		{
			char *pEnd = Head + 2;
			while (*pEnd && *pEnd != '\n' && *pEnd != '\r')
			{
				pEnd++;
			}
			const char *pNl = pEnd;
			if (*pNl == '\r')
			{
				pNl++;
			}
			unsigned nMode = 0;
			if (pNl[0] == '\n' && pNl[1] == 'm' && pNl[2] == ' ' && pNl[3] >= '0' && pNl[3] <= '2')
			{
				nMode = (unsigned) (pNl[3] - '0');
			}
			*pEnd = 0;
			if (nMode != 2)
			{
				continue;		// ADELFlow (or ADELMatrix) presets: no longer listed
			}
			if (BpValidName (Head + 2))
			{
				CParams::Copy (m_PreName[i], Head + 2, BP_NAME_LEN);
				m_PreMode[i] = (u8) nMode;
				nFound++;
			}
		}
	}
	m_Logger.Write (FromKernel, LogNotice, "Pedalboard presets found: %u", nFound);
}

boolean CKernel::PreWrite (unsigned nSlot, const char *pName)
{
	m_bPreBoardOk[nSlot] = FALSE;		// the preset file changes: the already parsed copy is no longer valid
	char (&Text)[32768 + 64] = *reinterpret_cast<char (*)[32768 + 64]> (Scratch ());
	unsigned n = 0;
	Text[n++] = 'n';
	Text[n++] = ' ';
	for (const char *s = pName; *s && n < BP_NAME_LEN + 4; s++)
	{
		Text[n++] = *s;
	}
	Text[n++] = '\n';
	n += m_pBoard->Serialize (Text + n, sizeof Text - 64 - n);

	FIL File;
	if (f_open (&File, DRIVE "/pre.tmp", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return FALSE;
	}
	UINT nWritten = 0;
	FRESULT Result = f_write (&File, Text, n, &nWritten);
	f_close (&File);
	if (Result != FR_OK || nWritten != n)
	{
		return FALSE;
	}
	CString Path;
	PrePath (&Path, nSlot);
	f_unlink (Path);
	return f_rename (DRIVE "/pre.tmp", Path) == FR_OK;
}

boolean CKernel::PreRead (unsigned nSlot)
{
	char (&Text)[32768 + 64] = *reinterpret_cast<char (*)[32768 + 64]> (Scratch ());
	CString Path;
	PrePath (&Path, nSlot);
	FIL File;
	if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return FALSE;
	}
	UINT nRead = 0;
	FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	if (Result != FR_OK)
	{
		return FALSE;
	}
	Text[nRead] = 0;
	char *p = Text;
	while (*p && *p != '\n')
	{
		p++;
	}
	if (*p == '\n')
	{
		p++;
	}
	if (m_nMode == 2)
	{
		// a grid that exceeds the estimated load is not loaded (as at boot): the previous one is put back
		static char *Old = 0;		// on the heap the first time: 32 KB less in the kernel image (the limit is 2 MB)
		if (Old == 0)
		{
			Old = new char[32768];
		}
		m_pBoard->Serialize (Old, 32768);
		if (!m_pBoard->Parse (p))
		{
			return FALSE;
		}
		if (m_pBoard->EstimateLoad10 () > BD_LOAD_LIMIT10)
		{
			m_pBoard->Parse (Old);
			m_Logger.Write (FromKernel, LogWarning, "Preset not loaded: estimated load above the %u%% limit", (unsigned) (BD_LOAD_LIMIT10 / 10));
			return FALSE;
		}
		return TRUE;
	}
	return m_pBoard->Parse (p);
}

// keeps in memory the preset just read and parsed (m_pStage, before the slot remapping): the next change to the same preset does not re-read the file nor re-parse it
void CKernel::PreBoardStore (unsigned nSlot)
{
	if (nSlot >= PRE_MAX)
	{
		return;
	}
	if (m_pPreBoard[nSlot] == 0)
	{
		m_pPreBoard[nSlot] = new CBoard;
	}
	*m_pPreBoard[nSlot] = *m_pStage;
	m_bPreBoardOk[nSlot] = TRUE;
}
// preset change without interruption (live): the new preset is read into a second document, its NAMs are loaded and warmed up in the slots that the preset in use
// does not occupy, its effects are created new, and ONLY at the end is everything swapped in one go: until then the sound of the old preset is not interrupted.
// After the swap the old plan goes on as a "ghost" (zero input, its effect instances): delays, reverbs and springs finish their tail.
// Returns FALSE if it cannot be done (safe mode, insufficient NAM slots, invalid file...): nothing has changed and the caller uses the usual loading.
boolean CKernel::PreStageLoad (unsigned nSlot)
{
	if (!SEAMLESS_PRESET || m_pSound == 0 || !m_bSdOk || m_bSafeMode || m_nMode != 2)
	{
		return FALSE;
	}
	const u32 nT0 = CTimer::GetClockTicks ();
	if (m_pStage == 0)
	{
		m_pStage = new CBoard;
	}

	if (nSlot < PRE_MAX && m_bPreBoardOk[nSlot] && m_pPreBoard[nSlot] != 0)
	{
		*m_pStage = *m_pPreBoard[nSlot];		// preset already read and parsed (in-memory copy): no SD card access
	}
	else
	{
		char (&Text)[32768 + 64] = *reinterpret_cast<char (*)[32768 + 64]> (Scratch ());
		CString Path;
		PrePath (&Path, nSlot);
		FIL File;
		if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		{
			return FALSE;
		}
		UINT nRead = 0;
		FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
		if (Result != FR_OK)
		{
			return FALSE;
		}
		Text[nRead] = 0;
		char *p = Text;
		while (*p && *p != '\n')
		{
			p++;
		}
		if (*p == '\n')
		{
			p++;
		}
		m_pStage->Reset ();
		m_pStage->SetMode (2);
		if (!m_pStage->Parse (p) || m_pStage->EstimateLoad10 () > BD_LOAD_LIMIT10)
		{
			return FALSE;		// the usual loading repeats the read and writes the reason in the log
		}
		PreBoardStore (nSlot);
	}
	// the NAMs of the previous ghost still run for a few ms with their slots' instances: wait for them to finish
	for (unsigned w = 0; w < 50 && m_pSound->GhostNamBusy (); w++)
	{
		CTimer::SimpleMsDelay (1);
	}

	boolean Busy[BD_MAX_NAM];
	for (unsigned s = 0; s < BD_MAX_NAM; s++)
	{
		Busy[s] = FALSE;
	}
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		const TBdBlock &Bl = m_pBoard->Block (id);
		if (Bl.Used && Bl.Type == NodeNam && Bl.Slot >= 0 && Bl.Slot < (int) BD_MAX_NAM)
		{
			Busy[Bl.Slot] = TRUE;
		}
	}
	if (!m_pStage->RemapNamSlots (Busy))
	{
		m_Logger.Write (FromKernel, LogNotice, "Preset change: not enough free NAM slots to prepare it aside, using the loading with interruption");
		return FALSE;
	}

	const u32 nTFx0 = CTimer::GetClockTicks ();
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		const TBdBlock &Bl = m_pStage->Block (id);
		m_pStageFx[id] = (Bl.Used && Bl.Type >= FxFirst && Bl.Type <= FxLast) ? FxCreate (Bl.Type, 48000.0f) : 0;
	}
	const u32 nTNam0 = CTimer::GetClockTicks ();
	unsigned nNam = 0;
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		TBdBlock &Bl = m_pStage->Block (id);
		if (Bl.Used && Bl.Type == NodeNam)
		{
			LoadSlot ((int) id, m_pStage);
			Feed ();
			nNam++;
		}
		else if (Bl.Used && Bl.Type == FxCabIR)
		{
			LoadIr ((int) id, m_pStage, m_pStageFx);
			Feed ();
		}
	}

	// dry run: a few blocks of silence in the new NAMs and effects (before the real noise) bring data and code into the cache; without it, the first block after the swap
	// lasted up to 225 us on a period of 83 us. Silence does not alter the state: the NAMs are already in their rest state and the effects have just been born.
	{
		float Z[PL_MAX_BLOCK], ZR[PL_MAX_BLOCK], O[8];
		for (unsigned k = 0; k < 8; k++)
		{
			Z[k] = 0.0f;
		}
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			const TBdBlock &Bl = m_pStage->Block (id);
			if (Bl.Used && Bl.Type == NodeNam && Bl.Loaded && Bl.Slot >= 0 && Bl.Slot < (int) BD_MAX_NAM)
			{
				for (unsigned k = 0; k < 4; k++)
				{
					m_Inst[Bl.Slot].Process (Z, O, 4);
				}
			}
			else if (m_pStageFx[id] != 0)
			{
				for (unsigned k = 0; k < 8; k++)
				{
					for (unsigned j = 0; j < 4; j++)
					{
						Z[j] = ZR[j] = 0.0f;
					}
					int nMono = 1;
					m_pStageFx[id]->Process (Z, ZR, 4, &nMono, Bl.V);
				}
			}
		}
	}
	const u32 nTCommit0 = CTimer::GetClockTicks ();
	// final check: during the preparation the page may have added a NAM to the document in use in the slot I have just filled
	boolean bOk = TRUE;
	for (unsigned id = 0; id < BD_MAX_BLOCKS && bOk; id++)
	{
		const TBdBlock &Bl = m_pBoard->Block (id);
		if (Bl.Used && Bl.Type == NodeNam && Bl.Slot >= 0 && Bl.Slot < (int) BD_MAX_NAM)
		{
			for (unsigned k = 0; k < BD_MAX_BLOCKS; k++)
			{
				const TBdBlock &St = m_pStage->Block (k);
				if (St.Used && St.Type == NodeNam && St.Slot == Bl.Slot)
				{
					bOk = FALSE;
				}
			}
		}
	}
	if (!bOk)
	{
		m_Logger.Write (FromKernel, LogWarning, "Preset change: the document in use changed during the preparation, using the loading with interruption");
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			delete m_pStageFx[id];
			m_pStageFx[id] = 0;
		}
		return FALSE;
	}

	// swap: ghost = copy of the active plan with the old effect instances; new document, instances and plan
	TGhost *pGhost = new TGhost;
	memset (pGhost, 0, sizeof *pGhost);
	memcpy (&pGhost->Plan, &m_Plan[m_nPlanActive], sizeof pGhost->Plan);
	for (unsigned id = 0; id < BD_MAX_BLOCKS && id < PL_MAX_NODES; id++)
	{
		pGhost->Fx[id] = m_pFx[id];
	}
	pGhost->Seq = ++m_nGhostSeq;

	m_pBoard->Adopt (*m_pStage);
	m_pBoard->TakeDirty ();		// I compile the plan below
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		m_pFx[id] = m_pStageFx[id];
		m_pStageFx[id] = 0;
	}
	const unsigned nNext = m_nPlanActive ^ 1;
	TBlock &Master = m_Params.Block (0);
	m_pBoard->Compile (&m_Plan[nNext], m_pInstPtr, DbToLin (Master.P[0].Value), Master.P[1].Value > 0.5f, m_pFx);
	const boolean bGhost = m_pSound->SwapPlan (&m_Plan[nNext], pGhost);
	m_nPlanActive = nNext;
	if (!bGhost)
	{
		for (unsigned id = 0; id < PL_MAX_NODES; id++)
		{
			delete pGhost->Fx[id];
		}
		delete pGhost;
	}
	m_Logger.Write (FromKernel, LogNotice, "Preset change without interruption: %u NAMs prepared aside, swap in %u ms in total%s [preset read and parse %u us, effects %u us, NAM and IR %u us, swap %u us]", nNam,
			(unsigned) ((CTimer::GetClockTicks () - nT0) / 1000), bGhost ? " (delay and reverb tails kept)" : " (tails lost: no room for the ghost)",
			(unsigned) (nTFx0 - nT0), (unsigned) (nTNam0 - nTFx0), (unsigned) (nTCommit0 - nTNam0), (unsigned) (CTimer::GetClockTicks () - nTCommit0));
	return TRUE;
}

// frees the effect instances of the ghosts that have finished their tail
void CKernel::GhostTick (void)
{
	if (m_pSound == 0)
	{
		return;
	}
	{
		unsigned nUs[8];
		if (m_pSound->SwapChunksTake (nUs))
		{
			m_Logger.Write (FromKernel, LogNotice, "First 8 blocks after the swap (period 83 us): %u %u %u %u %u %u %u %u us", nUs[0], nUs[1], nUs[2], nUs[3], nUs[4], nUs[5], nUs[6], nUs[7]);
		}
	}
	{
		unsigned nZero = 0;
		float fAfter = 0.0f, fBefore = 0.0f, fPeak = 0.0f;
		unsigned nMainUs = 0, nGhostUs = 0;
		if (m_pSound->SwapProbeTake (&nZero, &fAfter, &fBefore, &fPeak, &nMainUs, &nGhostUs))
		{
			// jumps in thousandths of full scale; the gap is the longest run of samples below -120 dBFS in the 50 ms after the swap
			m_Logger.Write (FromKernel, LogNotice, "Swap, 50 ms later: gap %u frames, jump %u.%03u (before %u.%03u), peak %u.%03u; maximum block: new plan %u us, ghost %u us",
					nZero, (unsigned) (fAfter * 1000.0f), (unsigned) (fAfter * 1000000.0f) % 1000, (unsigned) (fBefore * 1000.0f), (unsigned) (fBefore * 1000000.0f) % 1000,
					(unsigned) (fPeak * 1000.0f), (unsigned) (fPeak * 1000000.0f) % 1000, nMainUs, nGhostUs);
		}
	}
	TGhost *pG;
	while ((pG = m_pSound->GhostPoll ()) != 0)
	{
		for (unsigned id = 0; id < PL_MAX_NODES; id++)
		{
			delete pG->Fx[id];
		}
		delete pG;
	}
}

// caches the warmed-up state of a model (and runs its self-check) using the free slot nLane: same reads as LoadSlot, but without blocking the pedalboard
void CKernel::WarmFile (const char *pFile, unsigned nLane)
{
	const u32 nT0 = CTimer::GetClockTicks ();
	u8 *pData = 0;
	size_t nSize = 0;
	boolean bFromCache = FALSE;
	u32 nNameHash = 0;
	if (nLane >= BD_MAX_NAM || ModelRead (pFile, nLane, &pData, &nSize, &bFromCache, &nNameHash) != 0)
	{
		return;
	}
	if (adelnam::ParseModel (pData, nSize, &m_Model[nLane], !bFromCache) != 0)
	{
		return;
	}
	const size_t nNeeded = adelnam::Instance::StateBytes (m_Model[nLane], (int) m_nChunkFramesRun);
	if (nNeeded > m_nStateBytes[nLane])
	{
		delete[] m_pStateRaw[nLane];
		m_pStateRaw[nLane] = new u8[nNeeded + 64];
		m_nStateBytes[nLane] = nNeeded;
	}
	if (!m_Inst[nLane].Init (&m_Model[nLane], m_pStateRaw[nLane], m_nStateBytes[nLane] + 64, (int) m_nChunkFramesRun))
	{
		return;
	}
	boolean bVerified = FALSE, bWarmHit = FALSE;
	NamPrepare (nLane, pFile, pData, nSize, &bVerified, &bWarmHit);
	if (!bFromCache)
	{
		ModelCacheStore (nNameHash, pFile, pData, nSize);
	}
	Feed ();
	m_Logger.Write (FromKernel, LogNotice, "Model in cache: %s (slot %u free, %u ms)", pFile, nLane, (unsigned) ((CTimer::GetClockTicks () - nT0) / 1000));
}
// after boot, with the card idle: list of the models of all the presets, then one per round in the first free slot (not those of the preset in use nor those of a ghost)
void CKernel::WarmScanTick (void)
{
	if (m_nWarmScan == 2 || !SEAMLESS_PRESET || m_pSound == 0 || !m_bSdOk || m_bSafeMode || m_nMode != 2 || m_nTick < 60 || m_Ctl.PrePending != 0)
	{
		return;
	}
	if (m_nWarmScan == 0)
	{
		if (m_pStage == 0)
		{
			m_pStage = new CBoard;
		}
		char (&Text)[32768 + 64] = *reinterpret_cast<char (*)[32768 + 64]> (Scratch ());
		unsigned nPresets = 0;
		m_nWarmFiles = 0;
		for (unsigned i = 0; i < PRE_MAX; i++)
		{
			if (m_PreName[i][0] == 0 || m_PreMode[i] != m_nMode)
			{
				continue;
			}
			CString Path;
			PrePath (&Path, i);
			FIL File;
			if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
			{
				continue;
			}
			UINT nRead = 0;
			FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
			f_close (&File);
			if (Result != FR_OK)
			{
				continue;
			}
			Text[nRead] = 0;
			char *p = Text;
			while (*p && *p != '\n')
			{
				p++;
			}
			if (*p == '\n')
			{
				p++;
			}
			m_pStage->Reset ();
			m_pStage->SetMode (2);
			if (!m_pStage->Parse (p))
			{
				continue;
			}
			nPresets++;
			if (m_pStage->EstimateLoad10 () <= BD_LOAD_LIMIT10)
			{
				PreBoardStore (i);
			}
			for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
			{
				const TBdBlock &Bl = m_pStage->Block (id);
				if (!Bl.Used || Bl.Type != NodeNam || Bl.File[0] == 0 || m_nWarmFiles >= WARM_FILES)
				{
					continue;
				}
				boolean bKnown = FALSE;
				for (unsigned k = 0; k < m_nWarmFiles; k++)
				{
					bKnown = bKnown || strcmp (m_WarmFiles[k], Bl.File) == 0;
				}
				if (!bKnown)
				{
					CParams::Copy (m_WarmFiles[m_nWarmFiles++], Bl.File, BD_FILE_LEN);
				}
			}
			Feed ();
		}
		m_nWarmDone = 0;
		m_nWarmScan = 1;
		m_Logger.Write (FromKernel, LogNotice, "Model cache: %u different files in %u presets (at most %u in cache)", m_nWarmFiles, nPresets, (unsigned) WARM_CACHE);
		return;
	}
	if (m_nWarmDone >= m_nWarmFiles || m_nWarmDone >= WARM_CACHE)
	{
		m_nWarmScan = 2;
		m_Logger.Write (FromKernel, LogNotice, "Model cache ready: %u warmed-up states", m_nWarm);
		return;
	}
	if (m_pSound->GhostNamBusy ())
	{
		return;
	}
	boolean Busy[BD_MAX_NAM];
	for (unsigned s = 0; s < BD_MAX_NAM; s++)
	{
		Busy[s] = FALSE;
	}
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		const TBdBlock &Bl = m_pBoard->Block (id);
		if (Bl.Used && Bl.Type == NodeNam && Bl.Slot >= 0 && Bl.Slot < (int) BD_MAX_NAM)
		{
			Busy[Bl.Slot] = TRUE;
		}
	}
	int nLane = -1;
	for (unsigned s = 0; s < BD_MAX_NAM && nLane < 0; s++)
	{
		if (!Busy[s])
		{
			nLane = (int) s;
		}
	}
	if (nLane < 0)
	{
		m_nWarmScan = 2;
		return;
	}
	WarmFile (m_WarmFiles[m_nWarmDone++], (unsigned) nLane);
}

// preset in use and "modified" state survive the reboot
void CKernel::PreCurRead (void)
{
	FIL File;
	CString CurPath;
	PreCurPath (&CurPath, m_nMode);
	if (f_open (&File, CurPath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	char Text[BP_NAME_LEN + 8];
	UINT nRead = 0;
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = 0;
	char *pEnd = Text;
	while (*pEnd && *pEnd != '\n')
	{
		pEnd++;
	}
	const boolean bDirty = *pEnd == '\n' && pEnd[1] == '1';
	*pEnd = 0;
	if (BpValidName (Text))
	{
		for (unsigned i = 0; i < PRE_MAX; i++)
		{
			if (strcmp (m_PreName[i], Text) == 0 && m_PreMode[i] == m_nMode)
			{
				CParams::Copy (m_Ctl.PreCur, Text, BP_NAME_LEN);
				m_bPreDirtyBoot = bDirty;
			}
		}
	}
	CParams::Copy (m_PreCurWritten, m_Ctl.PreCur, BP_NAME_LEN);
	m_bPreDirtyWritten = m_bPreDirtyBoot;
	m_Ctl.PreDirty = m_bPreDirtyBoot;
}

void CKernel::PreCurWrite (void)
{
	char Text[BP_NAME_LEN + 8];
	unsigned n = 0;
	for (const char *s = m_Ctl.PreCur; *s && n < BP_NAME_LEN; s++)
	{
		Text[n++] = *s;
	}
	Text[n++] = '\n';
	Text[n++] = m_Ctl.PreDirty ? '1' : '0';
	Text[n++] = '\n';
	FIL File;
	CString CurPath;
	PreCurPath (&CurPath, m_nMode);
	if (f_open (&File, CurPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten = 0;
	f_write (&File, Text, n, &nWritten);
	f_close (&File);
	CParams::Copy (m_PreCurWritten, m_Ctl.PreCur, BP_NAME_LEN);
	m_bPreDirtyWritten = m_Ctl.PreDirty;
}

void CKernel::PreTick (void)
{
	if (!m_bSdOk)
	{
		return;
	}
	const unsigned nOp = m_Ctl.PrePending;
	if (nOp != 0)
	{
		char Name[BP_NAME_LEN];
		CParams::Copy (Name, m_Ctl.PreArg, BP_NAME_LEN);
		const unsigned nWant = m_nMode;		// there is only one mode (ADELGrid)
		int nAt = -1, nFree = -1;
		for (unsigned i = 0; i < PRE_MAX; i++)
		{
			if (m_PreName[i][0] == 0)
			{
				if (nFree < 0)
				{
					nFree = (int) i;
				}
			}
			else if (strcmp (m_PreName[i], Name) == 0 && m_PreMode[i] == nWant)
			{
				nAt = (int) i;
			}
		}
		if (nOp == 1)
		{
			const int nSlot = nAt >= 0 ? nAt : nFree;
			if (nSlot >= 0 && PreWrite ((unsigned) nSlot, Name))
			{
				CParams::Copy (m_PreName[nSlot], Name, BP_NAME_LEN);
				m_PreMode[nSlot] = (u8) m_nMode;
				CParams::Copy (m_Ctl.PreCur, Name, BP_NAME_LEN);
				m_nPreMark = m_pBoard->GetEdit ();
				m_bPreDirtyBoot = FALSE;
				m_Logger.Write (FromKernel, LogNotice, "Preset \"%s\" saved", Name);
			}
			else
			{
				m_Logger.Write (FromKernel, LogError, "Preset \"%s\": save failed", Name);
			}
		}
		else if (nOp == 2 && nAt >= 0)
		{
			if (PreStageLoad ((unsigned) nAt) || PreRead ((unsigned) nAt))
			{
				CParams::Copy (m_Ctl.PreCur, Name, BP_NAME_LEN);
				m_nPreMark = m_pBoard->GetEdit ();
				m_bPreDirtyBoot = FALSE;
				m_pBoard->MarkSave ();
				m_Logger.Write (FromKernel, LogNotice, "Preset \"%s\" loaded", Name);
				if (m_nMidiTraceAt != 0)
				{
					const unsigned nEnd = (unsigned) (CTimer::GetClockTicks64 () / 1000ULL);
					m_Logger.Write (FromKernel, LogNotice, "MIDI: from the arrival of the Program Change to the swap %u ms (wait before the start %u ms, preparation and swap %u ms)", nEnd - m_nMidiTraceAt, m_nMidiTraceApply - m_nMidiTraceAt, nEnd - m_nMidiTraceApply);
					m_nMidiTraceAt = 0;
				}
			}
			else
			{
				m_Logger.Write (FromKernel, LogError, "Preset \"%s\": invalid file, pedalboard unchanged", Name);
			}
		}
		else if (nOp == 3 && nAt >= 0)
		{
			CString Path;
			PrePath (&Path, (unsigned) nAt);
			f_unlink (Path);
			m_bPreBoardOk[nAt] = FALSE;
			m_PreName[nAt][0] = 0;
			if (nWant == m_nMode && strcmp (m_Ctl.PreCur, Name) == 0)
			{
				m_Ctl.PreCur[0] = 0;
				m_bPreDirtyBoot = FALSE;
			}
			m_Logger.Write (FromKernel, LogNotice, "Preset \"%s\" deleted", Name);
		}
		m_Ctl.PrePending = 0;
		PreViewBuild ();
	}

	m_Ctl.PreDirty = m_Ctl.PreCur[0] != 0 && (m_bPreDirtyBoot || m_pBoard->GetEdit () != m_nPreMark);
	if (strcmp (m_PreCurWritten, m_Ctl.PreCur) != 0 || m_bPreDirtyWritten != (boolean) m_Ctl.PreDirty)
	{
		PreCurWrite ();
	}
}

// one setting per line: name, tab, data
void CKernel::BpLoad (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/blockpresets.txt", FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	char (&Text)[BP_MAX * (BP_NAME_LEN + BP_DATA_LEN + 2) + 16] = *reinterpret_cast<char (*)[BP_MAX * (BP_NAME_LEN + BP_DATA_LEN + 2) + 16]> (Scratch ());
	UINT nRead = 0;
	FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	if (Result != FR_OK)
	{
		return;
	}
	Text[nRead] = 0;

	unsigned nCount = 0;
	char *p = Text;
	while (*p && nCount < BP_MAX)
	{
		char *pEnd = p;
		char *pTab = 0;
		while (*pEnd && *pEnd != '\n' && *pEnd != '\r')
		{
			if (*pEnd == '\t' && pTab == 0)
			{
				pTab = pEnd;
			}
			pEnd++;
		}
		const char Saved = *pEnd;
		*pEnd = 0;
		if (pTab != 0)
		{
			*pTab = 0;
			if (BpValidName (p) && BpValidData (pTab + 1))
			{
				CParams::Copy (m_BpName[nCount], p, BP_NAME_LEN);
				CParams::Copy (m_BpData[nCount], pTab + 1, BP_DATA_LEN);
				nCount++;
			}
		}
		*pEnd = Saved;
		p = pEnd;
		while (*p == '\n' || *p == '\r')
		{
			p++;
		}
	}
	m_nBp = nCount;
	m_Ctl.BpVer++;
	m_Logger.Write (FromKernel, LogNotice, "Block settings loaded: %u", nCount);
}

void CKernel::BpSave (void)
{
	char (&Text)[BP_MAX * (BP_NAME_LEN + BP_DATA_LEN + 2) + 16] = *reinterpret_cast<char (*)[BP_MAX * (BP_NAME_LEN + BP_DATA_LEN + 2) + 16]> (Scratch ());
	unsigned n = 0;
	for (unsigned i = 0; i < m_nBp; i++)
	{
		for (const char *s = m_BpName[i]; *s && n + 4 < sizeof Text; s++)
		{
			Text[n++] = *s;
		}
		Text[n++] = '\t';
		for (const char *s = m_BpData[i]; *s && n + 4 < sizeof Text; s++)
		{
			Text[n++] = *s;
		}
		Text[n++] = '\n';
	}

	FIL File;
	if (f_open (&File, DRIVE "/blockpresets.txt", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten = 0;
	f_write (&File, Text, n, &nWritten);
	f_close (&File);
}

void CKernel::FavSave (void)
{
	char (&Text)[LIB_MAX_FILES * (PARAM_TEXT_LEN + 1)] = s_Lib.fav;
	unsigned n = 0;
	for (unsigned i = 0; i < m_nFav; i++)
	{
		for (const char *s = m_Fav[i]; *s && n + 2 < sizeof Text; s++)
		{
			Text[n++] = *s;
		}
		Text[n++] = '\n';
	}

	FIL File;
	if (f_open (&File, DRIVE "/favorites.txt", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten = 0;
	f_write (&File, Text, n, &nWritten);
	f_close (&File);
}

// "block.key=value", one line per parameter
void CKernel::SettingsLoad (void)
{
	FIL File;
	if (f_open (&File, SETTINGS_FILE, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	static char Text[3072];
	UINT nRead = 0;
	FRESULT Result = f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	if (Result != FR_OK)
	{
		return;
	}
	Text[nRead] = 0;

	unsigned nApplied = 0;
	char *p = Text;
	while (*p)
	{
		char *pEnd = p;
		while (*pEnd && *pEnd != '\n' && *pEnd != '\r')
		{
			pEnd++;
		}
		char Saved = *pEnd;
		*pEnd = 0;

		char *pDot = p;
		while (*pDot && *pDot != '.' && *pDot != '=')
		{
			pDot++;
		}
		char *pEq = pDot;
		while (*pEq && *pEq != '=')
		{
			pEq++;
		}
		if (*pDot == '.' && *pEq == '=')
		{
			*pDot = 0;
			*pEq = 0;
			int b = m_Params.FindBlock (p);
			int q = b >= 0 ? m_Params.FindParam ((unsigned) b, pDot + 1) : -1;
			if (q >= 0)
			{
				TParam &P = m_Params.Block ((unsigned) b).P[q];
				const char *pValue = pEq + 1;
				if (P.Kind == ParamFile)
				{
					if (ValidModelFile (pValue))
					{
						CParams::Copy (P.Text, pValue, sizeof P.Text);
						nApplied++;
					}
				}
				else if (P.Kind == ParamText)
				{
					CleanLabel (P.Text, pValue, 22);
					nApplied++;
				}
				else
				{
					float v = ParseFloat (pValue);
					P.Value = P.Kind == ParamToggle ? (v > 0.5f ? 1.0f : 0.0f) : (v < P.Min ? P.Min : (v > P.Max ? P.Max : v));
					nApplied++;
				}
			}
		}

		*pEnd = Saved;
		p = pEnd;
		while (*p == '\n' || *p == '\r')
		{
			p++;
		}
	}
	m_Logger.Write (FromKernel, LogNotice, "Settings loaded from %s (%u values)", SETTINGS_FILE, nApplied);
}

void CKernel::SettingsSave (void)
{
	static char Text[3072];
	unsigned n = 0;
	for (unsigned b = 0; b < m_Params.GetBlockCount () && n + 160 < sizeof Text; b++)
	{
		TBlock &Blk = m_Params.Block (b);
		for (unsigned q = 0; q < Blk.nParams && n + 160 < sizeof Text; q++)
		{
			TParam &P = Blk.P[q];
			for (const char *s = Blk.Id; *s; s++) Text[n++] = *s;
			Text[n++] = '.';
			for (const char *s = P.Key; *s; s++) Text[n++] = *s;
			Text[n++] = '=';
			if (P.Kind == ParamFile || P.Kind == ParamText)
			{
				for (const char *s = P.Text; *s; s++) Text[n++] = *s;
			}
			else
			{
				n += FormatFixed (Text + n, P.Value, 2);
			}
			Text[n++] = '\n';
		}
	}

	FIL File;
	if (f_open (&File, SETTINGS_TMP, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten = 0;
	FRESULT Result = f_write (&File, Text, n, &nWritten);
	f_close (&File);
	if (Result != FR_OK || nWritten != n)
	{
		m_Logger.Write (FromKernel, LogError, "Settings: write failed");
		return;
	}
	f_unlink (SETTINGS_FILE);
	if (f_rename (SETTINGS_TMP, SETTINGS_FILE) != FR_OK)
	{
		m_Logger.Write (FromKernel, LogError, "Settings: rename failed");
	}
}

void CKernel::ApplyMaster (void)
{
	// NAM calibration changed from the page: the plan recomputes the models' input (CBoard::Compile)
	CBoard::SetCal (m_Params.Block (1).P[0].Value, m_Params.Block (1).P[1].Value);
	CBoard::SetInTrim (GuitarTrimDb (m_Params.Block (2)));
	RebuildPlan ();
}

// ---------------------------------------------------------------- MIDI
// The USB handler (interrupt) and the page (test messages) put the messages in the queue; a task reads them every 5 ms, applies the map of the preset in use
// and replies to the controller (CC echo, Program Change, status SysEx). None of this runs in the audio callback.
class CKernelMidiTask : public CTask
{
public:
	CKernelMidiTask (CKernel *pKernel) : m_pKernel (pKernel) {}
	void Run (void) override
	{
		while (TRUE)
		{
			m_pKernel->MidiTick ();
			CScheduler::Get ()->MsSleep (5);
		}
	}
private:
	CKernel *m_pKernel;
};

void CKernel::MidiCfgRead (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/midi.cfg", FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	char Text[32];
	UINT nRead = 0;
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = 0;
	// fields separated by a space: channel, echo (0/1), source of controller 3 (0 = none), ADEL delay tempo (0/1); the old files only have the first two
	unsigned v[4] = {0, 0, 0, 0};
	unsigned nF = 0;
	const char *p = Text;
	while (nF < 4 && *p >= '0' && *p <= '9')
	{
		unsigned x = 0;
		while (*p >= '0' && *p <= '9' && x < 1000)
		{
			x = x * 10 + (unsigned) (*p++ - '0');
		}
		v[nF++] = x;
		if (*p == ' ')
		{
			p++;
		}
		else
		{
			break;
		}
	}
	if (nF >= 2 && v[0] <= 16 && v[1] <= 1)
	{
		m_Ctl.MidiChan = v[0];
		m_Ctl.MidiEcho = v[1] == 1;
		if (nF >= 3 && v[2] >= MIDI_CC_FIRST_FREE && v[2] <= 127)
		{
			m_Ctl.MidiCtl3Cc = v[2];
		}
		if (nF >= 4)
		{
			m_Ctl.MidiSyncAdel = v[3] == 1;
		}
	}
}

void CKernel::MidiCfgWrite (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/midi.cfg", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	char Text[32];
	unsigned n = 0;
	const unsigned c = m_Ctl.MidiChan;
	if (c >= 10)
	{
		Text[n++] = (char) ('0' + c / 10);
	}
	Text[n++] = (char) ('0' + c % 10);
	Text[n++] = ' ';
	Text[n++] = m_Ctl.MidiEcho ? '1' : '0';
	Text[n++] = ' ';
	const unsigned k = m_Ctl.MidiCtl3Cc;
	if (k >= 100)
	{
		Text[n++] = (char) ('0' + k / 100);
	}
	if (k >= 10)
	{
		Text[n++] = (char) ('0' + (k / 10) % 10);
	}
	Text[n++] = (char) ('0' + k % 10);
	Text[n++] = ' ';
	Text[n++] = m_Ctl.MidiSyncAdel ? '1' : '0';
	Text[n++] = '\n';
	UINT nWritten = 0;
	f_write (&File, Text, n, &nWritten);
	f_close (&File);
}

// service functions (ftools.h): all off at startup except the signal ones the user left on; parameters in SD:/features.cfg
void CKernel::FeatInit (void)
{
	m_pFeat = new CFeatures (48000.0f);
	m_Ctl.pFeat = m_pFeat;
	m_Ctl.FeatDirty = FALSE;
	if (m_bSdOk)
	{
		FeatCfgRead ();
	}
	m_pSound->SetFeatures (m_pFeat);
}

void CKernel::FeatCfgRead (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/features.cfg", FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	char Text[200];
	UINT nRead = 0;
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = 0;
	m_pFeat->Parse (Text);
}

void CKernel::FeatCfgWrite (void)
{
	char Text[200];
	const unsigned n = m_pFeat->Serialize (Text, sizeof Text);
	FIL File;
	if (n == 0 || f_open (&File, DRIVE "/features.cfg", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten = 0;
	f_write (&File, Text, n, &nWritten);
	f_close (&File);
}

void CKernel::MidiInit (void)
{
	m_Ctl.pMidiQ = &m_MidiQ;
	m_Ctl.pMidiMon = &m_MidiMon;
	m_Ctl.MidiChan = 0;
	m_Ctl.MidiEcho = TRUE;
	m_Ctl.MidiCtl3Cc = 0;
	m_Ctl.MidiSyncAdel = FALSE;
	m_Ctl.MidiCfgDirty = FALSE;
	m_Ctl.ApActive = FALSE;
	m_Ctl.ApAction = 0;
	m_Ctl.ApSsid[0] = 0;
	m_Ctl.ApPass[0] = 0;
	m_Ctl.ApChan = 6;
	m_Ctl.MidiBpm10 = 0;
	m_Ctl.MidiClockOn = FALSE;
	m_Ctl.MidiUsb = 0;
	m_Ctl.MidiDin = 0;
	m_Ctl.MidiDinFw = 0;
	m_Ctl.MidiRx = 0;
	m_Ctl.MidiTx = 0;
	if (m_bSdOk)
	{
		MidiCfgRead ();
	}
}

// the MIDI task starts only when the network and the web page are ready: with the task active during Wi-Fi startup (the board stopped at step 5, between the network stack and the supplicant) update-fix1 did not start
void CKernel::MidiStart (void)
{
	new CKernelMidiTask (this);
}

// one MIDI byte from a port (USB or DIN) to its parser; the complete messages go into the MIDI task's queue (Push is safe with several producers: it masks interrupts)
void CKernel::MidiFeedByte (CMidiParser *pParser, u8 nByte)
{
	TMidiMsg Msg;
	if (pParser->Feed (nByte, &Msg))
	{
		if (Msg.Type == MidiSysEx)
		{
			if (!MidiIsStateRequest (pParser->SysEx (), pParser->SysExLength ()))
			{
				return;
			}
			Msg.A = 4;
			Msg.B = 1;		// status request
		}
		m_MidiQ.Push (Msg);
	}
}

void CKernel::MidiPacketHandler (unsigned nCable, u8 *pPacket, unsigned nLength, unsigned nDevice, void *pParam)
{
	(void) nCable;
	(void) nDevice;
	CKernel *p = (CKernel *) pParam;
	for (unsigned i = 0; i < nLength; i++)
	{
		p->MidiFeedByte (&p->m_MidiParser, pPacket[i]);
	}
}

void CKernel::MidiRemovedHandler (CDevice *pDevice, void *pContext)
{
	(void) pDevice;
	CKernel *p = (CKernel *) pContext;
	p->m_pUsbMidi = 0;
	p->m_Ctl.MidiUsb = 0;
	p->m_MidiMon.Add ('i', "USB MIDI disconnected");
}

// sends MIDI bytes to the controller (USB; the Pisound's DIN socket is phase 4) and shows them in the monitor
void CKernel::MidiSend (const u8 *pData, unsigned nLen)
{
	TMidiMsg Msg;
	Msg.Type = MidiNone;
	Msg.Chan = pData[0] & 0x0F;
	Msg.A = nLen > 1 ? pData[1] : 0;
	Msg.B = nLen > 2 ? pData[2] : 0;
	if ((pData[0] & 0xF0) == 0xB0) Msg.Type = MidiControl;
	else if ((pData[0] & 0xF0) == 0xC0) Msg.Type = MidiProgram;
	else if (pData[0] == 0xF0) { Msg.Type = MidiSysEx; Msg.A = (u8) (nLen > 2 ? nLen - 2 : 0); Msg.B = 0; }
	char T[MIDI_MON_TEXT];
	if (MidiFormat (T, sizeof T, Msg) > 0)
	{
		m_MidiMon.Add ('o', T);
	}
	m_Ctl.MidiTx++;
	CUSBMIDIDevice *pUsb = m_pUsbMidi;
	if (pUsb != 0)
	{
		pUsb->SendPlainMIDI (0, pData, nLen);
	}
	if (m_pDin != 0 && m_nDinState == 3)
	{
		m_pDin->Send (pData, nLen);
	}
}

// echo of a fixed-CC class: 127 if its first block exists and is on, otherwise 0
void CKernel::MidiEchoClass (unsigned nClass)
{
	if (!m_Ctl.MidiEcho || nClass < 1 || nClass > 16)
	{
		return;
	}
	const int n = m_pBoard->MidiFindClass (nClass);
	const boolean bOn = n >= 0 && m_pBoard->Block ((unsigned) n).On;
	u8 Out[3];
	const unsigned nLen = MidiBuildCC (Out, m_Ctl.MidiChan, nClass, bOn ? 127 : 0);
	MidiSend (Out, nLen);
	if (bOn)
	{
		m_nMidiLastMask |= 1u << (nClass - 1);
	}
	else
	{
		m_nMidiLastMask &= ~(1u << (nClass - 1));
	}
}

// echo of the changes not caused by the controller (from the page, from another preset): classes that changed state and preset in use
void CKernel::MidiEchoState (void)
{
	const unsigned nMask = m_pBoard->MidiClassMask ();
	if (!m_bMidiMaskValid)
	{
		m_bMidiMaskValid = TRUE;
		m_nMidiLastMask = nMask;
	}
	else if (nMask != m_nMidiLastMask)
	{
		const unsigned nDiff = nMask ^ m_nMidiLastMask;
		for (unsigned c = 1; c <= 16; c++)
		{
			if (nDiff & (1u << (c - 1)))
			{
				MidiEchoClass (c);
			}
		}
		m_nMidiLastMask = nMask;
	}
	int nSlot = -1;
	for (unsigned i = 0; i < PRE_MAX && m_Ctl.PreCur[0] != 0; i++)
	{
		if (m_PreName[i][0] != 0 && m_PreMode[i] == m_nMode && strcmp (m_PreName[i], m_Ctl.PreCur) == 0)
		{
			nSlot = (int) i;
			break;
		}
	}
	if (nSlot != m_nMidiLastPc)
	{
		m_nMidiLastPc = nSlot;
		if (nSlot >= 0 && m_Ctl.MidiEcho)
		{
			u8 Out[2];
			MidiSend (Out, MidiBuildPC (Out, m_Ctl.MidiChan, (unsigned) nSlot));
			m_bMidiMaskValid = FALSE;		// with another preset the classes' state restarts: everything is sent again
			const unsigned nNow = m_pBoard->MidiClassMask ();
			for (unsigned c = 1; c <= 16; c++)
			{
				u8 Cc[3];
				MidiSend (Cc, MidiBuildCC (Cc, m_Ctl.MidiChan, c, (nNow >> (c - 1)) & 1u ? 127 : 0));
			}
			m_bMidiMaskValid = TRUE;
			m_nMidiLastMask = nNow;
		}
	}
}

void CKernel::MidiSendState (void)
{
	if (!m_Ctl.MidiEcho && m_Ctl.MidiChan == 0)
	{
		// status requests are answered even with the echo off
	}
	u8 Out[16];
	const TParam &M = m_Params.Block (0).P[0];
	const float fSpan = M.Max - M.Min;
	const unsigned nMaster = fSpan > 0.0f ? (unsigned) ((M.Value - M.Min) / fSpan * 127.0f + 0.5f) : 0;
	const unsigned nLen = MidiBuildStateSysEx (Out, m_nMidiLastPc >= 0 ? (unsigned) m_nMidiLastPc : 0, m_pBoard->MidiClassMask (), nMaster);
	MidiSend (Out, nLen);
}

void CKernel::MidiPresetWant (int nSlot)
{
	if (nSlot < 0 || nSlot >= (int) PRE_MAX || m_PreName[nSlot][0] == 0)
	{
		m_MidiMon.Add ('i', "no such preset: ignored");
		return;
	}
	m_nMidiPcWant = nSlot;
	m_nMidiPcAt = (unsigned) (CTimer::GetClockTicks64 () / 1000ULL);
}

void CKernel::MidiApply (const TMidiAct &Act, unsigned long long nUs)
{
	switch (Act.Type)
	{
	case ActPreset:
		MidiPresetWant (Act.A);
		break;

	case ActPresetUp:
	case ActPresetDown:
	{
		// from the preset in use (or the last one requested) to the next or the previous one that exists, wrapping around
		const int nCur = m_nMidiPcWant >= 0 ? m_nMidiPcWant : (m_nMidiLastPc >= 0 ? m_nMidiLastPc : 0);
		const int nMax = (int) PRE_MAX;
		for (int k = 1; k <= nMax; k++)
		{
			const int nTry = Act.Type == ActPresetUp ? (nCur + k) % nMax : ((nCur - k) % nMax + nMax) % nMax;
			if (m_PreName[nTry][0] != 0)
			{
				MidiPresetWant (nTry);
				break;
			}
		}
		break;
	}

	case ActClassEnable:
	{
		const int nId = m_pBoard->MidiFindClass ((unsigned) Act.A);
		if (nId < 0)
		{
			m_MidiMon.Add ('i', "no block of this class in the preset");
		}
		else
		{
			const char *pErr = m_pBoard->SetValue (nId, "on", Act.B ? 1.0f : 0.0f);
			if (pErr != 0)
			{
				m_MidiMon.Add ('i', pErr);
			}
		}
		MidiEchoClass ((unsigned) Act.A);		// the echo tells how the state really is (even if the command was refused)
		break;
	}

	case ActBlockEnable:
	{
		const char *pErr = m_pBoard->SetValue (Act.A, "on", Act.B ? 1.0f : 0.0f);
		if (pErr != 0)
		{
			m_MidiMon.Add ('i', pErr);
		}
		if (m_Ctl.MidiEcho)
		{
			for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX; i++)
			{
				const TMidiBlockCc &Bc = m_pBoard->Midi ().BlockCc (i);
				if (Bc.Cc != 0 && Bc.Block == Act.A)
				{
					u8 Out[3];
					MidiSend (Out, MidiBuildCC (Out, m_Ctl.MidiChan, (unsigned) Bc.Cc, m_pBoard->Block ((unsigned) Act.A).On ? 127 : 0));
				}
			}
		}
		break;
	}

	case ActMaster:
	{
		TParam &M = m_Params.Block (0).P[0];
		M.Value = M.Min + (M.Max - M.Min) * (float) Act.A / 127.0f;
		RebuildPlan ();
		break;
	}

	case ActParam:
		if (m_pBoard->SetParamNorm (Act.A, (unsigned) Act.B, Act.F) != 0)
		{
			m_MidiMon.Add ('i', "invalid linked parameter");
		}
		break;

	case ActTap:
	{
		float fBpm;
		if (m_MidiTap.Tap (nUs, &fBpm))
		{
			m_Ctl.MidiBpm10 = (unsigned) (fBpm * 10.0f + 0.5f);
		}
		break;
	}

	case ActClockStart:
		m_MidiMon.Add ('i', "Start");
		break;
	case ActClockStop:
		m_MidiMon.Add ('i', "Stop");
		break;

	case ActStateRequest:
		MidiSendState ();
		break;

	case ActLearned:
		m_MidiMon.Add ('i', Act.A >= 0 ? "controller assigned to the received CC" : "CC assigned to the block");
		if (Act.A == 2)
		{
			m_Ctl.MidiCtl3Cc = (unsigned) Act.B;		// global controller 3: the source is saved in midi.cfg and does not mark the preset as "modified"
			m_Ctl.MidiCfgDirty = TRUE;
		}
		else
		{
			m_pBoard->MarkEdit ();
		}
		break;

	default:
		break;
	}
}

// SPI0 bus (CE0, 150 kHz, mode 0) and data available pin (GPIO25) of the Pisound
#define PIN_DIN_DATA_AVAILABLE	25
class CKernelDinBus : public CDinBus
{
public:
	CKernelDinBus (void)
	:	m_Spi (150000, 0, 0, 0),
		m_DataAvail (PIN_DIN_DATA_AVAILABLE, GPIOModeInput)
	{
		m_bOk = m_Spi.Initialize ();
	}
	boolean Ok (void) const { return m_bOk; }
	void Transfer (const u8 *pTx, u8 *pRx, unsigned nLen) override
	{
		if (m_bOk)
		{
			m_Spi.WriteRead (0, pTx, pRx, nLen);
		}
		else
		{
			for (unsigned i = 0; i < nLen; i++) pRx[i] = 0;
		}
	}
	boolean DataAvailable (void) override { return m_DataAvail.Read () != LOW; }
private:
	CSPIMaster m_Spi;
	CGPIOPin m_DataAvail;
	boolean m_bOk;
};

// Pisound DIN socket. Startup without long waits in the task: 0 -> MCU reset low; 1 -> (after at least 2 ms) high; 2 -> (after at least 80 ms) reading of the information;
// if the MCU does not respond it is retried (3 attempts in all) and then given up without disturbing the rest. In safe mode the DIN socket stays off.
void CKernel::MidiDinTick (unsigned nMs, unsigned long long nUs)
{
#ifdef MIDI_NO_DIN
	(void) nMs; (void) nUs;
#else
	if (m_bSafeMode || m_nDinState == 4)
	{
		return;
	}
	switch (m_nDinState)
	{
	case 0:
		if (m_pDin == 0)
		{
			static CKernelDinBus *s_pBus = 0;
			if (s_pBus == 0)
			{
				s_pBus = new CKernelDinBus;
			}
			if (!s_pBus->Ok ())
			{
				m_MidiMon.Add ('i', "Pisound DIN: SPI not available");
				m_nDinState = 4;
				return;
			}
			m_pDin = new CPisoundDin (s_pBus);
		}
		m_Ctl.MidiDin = 1;
		m_ResetMcu.Write (LOW);
		m_nDinAt = nMs != 0 ? nMs : 1;
		m_nDinState = 1;
		break;

	case 1:
		if (nMs - m_nDinAt >= 2)
		{
			m_ResetMcu.Write (HIGH);
			m_nDinAt = nMs != 0 ? nMs : 1;
			m_nDinState = 2;
		}
		break;

	case 2:
		if (nMs - m_nDinAt >= 80)
		{
			if (m_pDin->ReadInfo ())
			{
				m_Ctl.MidiDinFw = m_pDin->FwVersion ();
				m_Ctl.MidiDin = 2;
				m_nDinState = 3;
				m_MidiMon.Add ('i', "Pisound DIN active");
			}
			else if (++m_nDinTries >= 3)
			{
				m_Ctl.MidiDin = 0;
				m_nDinState = 4;
				m_MidiMon.Add ('i', "Pisound DIN: the MCU does not respond");
			}
			else
			{
				m_nDinState = 0;
			}
		}
		break;

	case 3:
	{
		u8 In[64];
		const unsigned n = m_pDin->Poll (nUs, In, sizeof In);
		for (unsigned i = 0; i < n; i++)
		{
			MidiFeedByte (&m_DinParser, In[i]);
		}
	}
		break;

	default:
		break;
	}
#endif
}

void CKernel::MidiTick (void)
{
	const unsigned long long nUs = CTimer::GetClockTicks64 ();
	const unsigned nMs = (unsigned) (nUs / 1000ULL);

	// USB controller: looks for the "umidi1" device (with detection of devices connected after boot) once a second
#ifndef MIDI_NO_USB		// safety switch: defining it in config.h the firmware no longer looks for USB controllers (the queue and the test messages from the page remain)
	if (!m_bSafeMode && m_pUsbMidi == 0 && m_nNetStage >= 1 && (m_nMidiPnpAt == 0 || nMs - m_nMidiPnpAt >= 1000))
	{
		m_nMidiPnpAt = nMs != 0 ? nMs : 1;
		m_USBHCI.UpdatePlugAndPlay ();
		CUSBMIDIDevice *pDev = (CUSBMIDIDevice *) CDeviceNameService::Get ()->GetDevice ("umidi1", FALSE);
		if (pDev != 0)
		{
			pDev->RegisterRemovedHandler (MidiRemovedHandler, this);
			pDev->RegisterPacketHandler (MidiPacketHandler, this);
			m_pUsbMidi = pDev;
			m_Ctl.MidiUsb = 1;
			m_MidiMon.Add ('i', "USB MIDI connected");
		}
	}
#endif

	MidiDinTick (nMs, nUs);

	m_pBoard->Midi ().Ctl (2).Cc = (int) m_Ctl.MidiCtl3Cc;		// global controller 3: same source in all the presets

	TMidiMsg Msg;
	for (unsigned nGuard = 0; nGuard < 32 && m_MidiQ.Pop (&Msg); nGuard++)
	{
		m_Ctl.MidiRx++;
		if (Msg.Type == MidiClock)
		{
			float fBpm;
			if (m_MidiClock.Pulse (nUs, &fBpm))
			{
				m_Ctl.MidiBpm10 = (unsigned) (fBpm * 10.0f + 0.5f);
			}
			continue;
		}
		char T[MIDI_MON_TEXT];
		if (MidiFormat (T, sizeof T, Msg) > 0)
		{
			m_MidiMon.Add ('i', T);
		}
		if (Msg.Type == MidiSysEx)
		{
			if (Msg.B == 1)
			{
				MidiSendState ();
			}
			continue;
		}
		CMidiMap &Map = m_pBoard->Midi ();
		Map.SetChannel (m_Ctl.MidiChan);
		TMidiAct Act[8];
		const unsigned n = Map.Handle (Msg, Act, 8);
		for (unsigned i = 0; i < n; i++)
		{
			MidiApply (Act[i], nUs);
		}
	}
	m_Ctl.MidiClockOn = m_MidiClock.Active (nUs);

	// MIDI tempo for the delays: the ToTest ones with Sync read it from the global variable; the ADEL delays (which already have tempo and rhythm) receive it as base tempo if the option is on
	g_fFxTempoBpm = (float) m_Ctl.MidiBpm10 * 0.1f;
	if (m_Ctl.MidiSyncAdel && m_Ctl.MidiBpm10 >= 300 && m_Ctl.MidiBpm10 != m_nSyncedBpm10)
	{
		m_nSyncedBpm10 = m_Ctl.MidiBpm10;
		const float fMs = 600000.0f / (float) m_Ctl.MidiBpm10;		// 60000 / bpm, con bpm = Bpm10 / 10
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			const TBdBlock &Bl = m_pBoard->Block (id);
			if (Bl.Used && (Bl.Type == FxAdelDelay || Bl.Type == FxAdelDelayM))
			{
				m_pBoard->SetValue ((int) id, "time", fMs < 20.0f ? 20.0f : (fMs > 2000.0f ? 2000.0f : fMs));
			}
		}
	}
	else if (!m_Ctl.MidiSyncAdel)
	{
		m_nSyncedBpm10 = 0;		// turning the option on again realigns immediately
	}

	MidiPcApply ();

	MidiEchoState ();
}

// preset change from MIDI: the last requested Program Change is applied as soon as MIDI_PC_DEBOUNCE_MS have passed without other messages (and only if another one is not in progress); called from the 100 ms loop and,
// so as not to wait for the next round, from the main loop as soon as the request arrives (WaitTick)
void CKernel::MidiPcApply (void)
{
	const unsigned nMs = (unsigned) (CTimer::GetClockTicks64 () / 1000ULL);
	if (m_nMidiPcWant >= 0 && nMs - m_nMidiPcAt >= MIDI_PC_DEBOUNCE_MS && m_Ctl.PrePending == 0)
	{
		const int nSlot = m_nMidiPcWant;
		m_nMidiPcWant = -1;
		if (m_PreName[nSlot][0] != 0)
		{
			if (nSlot == m_nMidiLastPc && m_Ctl.PreCur[0] != 0 && strcmp (m_PreName[nSlot], m_Ctl.PreCur) == 0 && m_PreMode[nSlot] == m_nMode)
			{
				// preset already in use: it is not reloaded (unsaved changes would be lost); only the echo is sent again, so the controller realigns
				m_MidiMon.Add ('i', "preset already active: sending only the echo again");
				m_nMidiLastPc = -2;
			}
			else
			{
				CParams::Copy (m_Ctl.PreArg, m_PreName[nSlot], BP_NAME_LEN);
				m_Ctl.PreArgMode = m_PreMode[nSlot];
				m_nMidiTraceAt = m_nMidiPcAt;		// for the measurement: from the MIDI message to the swap
				m_nMidiTraceApply = nMs;
				m_Ctl.PrePending = 2;
			}
		}
	}

}

// pedalboard: loads one model at a time (the new or changed one), rebuilds the plan if needed, saves
// loads the IR of a Cab IR block from the card into the effect's instance (the audio callback goes on: the IR is activated with a single store)
void CKernel::LoadIr (int nId, CBoard *pBoard, CFx **ppFx)
{
	TBdBlock &Bl = (pBoard != 0 ? pBoard : m_pBoard)->Block ((unsigned) nId);
	CFx *pFx = (ppFx != 0 ? ppFx : m_pFx)[nId];
	if (pFx == 0)
	{
		return;
	}
	const char *pErr = 0;
	if (Bl.File[0] == 0)
	{
		pFx->SetImpulse (0, 0, 1);		// no IR: the signal passes
		CParams::Copy (Bl.Status, "no IR", sizeof Bl.Status);
	}
	else if (!m_bSdOk || !ValidIrFile (Bl.File))
	{
		pErr = "IR not available";
	}
	else
	{
		CString Path;
		Path.Format ("SD:/%s", Bl.File);
		FIL File;
		if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		{
			pErr = "file not found";
		}
		else
		{
			const unsigned nSize = (unsigned) f_size (&File);
			if (nSize < 20 || nSize > 16 + 4 * 2 * CABIR_MAX_TAPS)
			{
				pErr = "invalid file";
			}
			else
			{
				u8 *pBuf = new u8[nSize];
				UINT nRead = 0;
				if (f_read (&File, pBuf, nSize, &nRead) != FR_OK || nRead != nSize)
				{
					pErr = "read failed";
				}
				else if (memcmp (pBuf, "ADR1", 4) != 0)
				{
					pErr = "not a .air IR";
				}
				else
				{
					const u32 nTaps = (u32) pBuf[4] | ((u32) pBuf[5] << 8) | ((u32) pBuf[6] << 16) | ((u32) pBuf[7] << 24);
					const u32 nCh = (u32) pBuf[8] | ((u32) pBuf[9] << 8) | ((u32) pBuf[10] << 16) | ((u32) pBuf[11] << 24);
					const u32 nCrc = (u32) pBuf[nSize - 4] | ((u32) pBuf[nSize - 3] << 8) | ((u32) pBuf[nSize - 2] << 16) | ((u32) pBuf[nSize - 1] << 24);
					if (nTaps < 1 || nTaps > CABIR_MAX_TAPS || nCh < 1 || nCh > 2 || nSize != 16 + 4 * nTaps * nCh)
					{
						pErr = "invalid IR";
					}
					else if (Crc32 (pBuf, nSize - 4) != nCrc)
					{
						pErr = "damaged file (CRC)";
					}
					else
					{
						float *pF = new float[nTaps * nCh];
						memcpy (pF, pBuf + 12, nTaps * nCh * sizeof (float));
						if (!pFx->SetImpulse (pF, nTaps, nCh))
						{
							pErr = "IR unusable";
						}
						delete[] pF;
						if (pErr == 0)
						{
							CString Msg;
							Msg.Format ("ok: %u samples, %u channels", nTaps, nCh);
							CParams::Copy (Bl.Status, Msg, sizeof Bl.Status);
						}
					}
				}
				delete[] pBuf;
			}
			f_close (&File);
		}
	}
	if (pErr != 0)
	{
		CParams::Copy (Bl.Status, pErr, sizeof Bl.Status);
		CParams::Copy (Bl.FailedFile, Bl.File, sizeof Bl.FailedFile);
		m_Logger.Write (FromKernel, LogWarning, "IR %s: %s", Bl.File, pErr);
	}
	else
	{
		CParams::Copy (Bl.LoadedFile, Bl.File, sizeof Bl.LoadedFile);
		Bl.FailedFile[0] = 0;
		m_Logger.Write (FromKernel, LogNotice, "IR loaded: %s", Bl.File);
	}
}

void CKernel::BoardTick (void)
{
	GhostTick ();
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		TBdBlock &Bl = m_pBoard->Block (id);
		if (Bl.Used && Bl.Type == FxCabIR && strcmp (Bl.File, Bl.LoadedFile) != 0 && strcmp (Bl.File, Bl.FailedFile) != 0)
		{
			LoadIr ((int) id);
			break;
		}
	}

	// models to load (new preset or changed model): all in the same round, with a single recompute of the plan before (the instances leave the plan before being rewritten)
	// and one after; before it was one per 100 ms tick, with two recomputes each
	{
		unsigned nPending = 0;
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			TBdBlock &Bl = m_pBoard->Block (id);
			if (Bl.Used && Bl.Type == NodeNam && strcmp (Bl.File, Bl.LoadedFile) != 0 && strcmp (Bl.File, Bl.FailedFile) != 0)
			{
				Bl.Loaded = FALSE;
				nPending++;
			}
		}
		if (nPending > 0)
		{
			const u32 nT0 = CTimer::GetClockTicks ();
			RebuildPlan ();
			for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
			{
				TBdBlock &Bl = m_pBoard->Block (id);
				if (Bl.Used && Bl.Type == NodeNam && !Bl.Loaded && strcmp (Bl.File, Bl.LoadedFile) != 0 && strcmp (Bl.File, Bl.FailedFile) != 0)
				{
					LoadSlot ((int) id);
					Feed ();
				}
			}
			RebuildPlan ();
			m_pBoard->MarkDirty ();
			m_Logger.Write (FromKernel, LogNotice, "NAM models loaded: %u in %u ms", nPending, (unsigned) ((CTimer::GetClockTicks () - nT0) / 1000));
		}
	}

	if (m_pBoard->TakeDirty ())
	{
		RebuildPlan ();
	}
	WarmScanTick ();
	if (m_pBoard->TakeSaveDirty ())
	{
		m_bBoardSavePending = TRUE;
		m_nBoardSaveAt = m_nTick + 15;
	}
	if (m_bBoardSavePending && m_nTick >= m_nBoardSaveAt && m_bSdOk)
	{
		m_bBoardSavePending = FALSE;
		BoardSave ();
	}
}

// integrity check of the code in memory: copy at the start, then comparison of all the pages; it reports the changed areas
extern "C" char _start[];
extern "C" char _etext[];
extern "C" char __init_start[];		// right after .rodata: [_start, __init_start) = .init + .text + .rodata (everything that never changes)

static u32 *s_pCodeCopy = 0;
static unsigned s_nCodePages = 0;

static void CodeCheckStart (CLogger *pLogger)
{
	// constant code and data (.rodata: strings, tables, vtables): the 64-byte corruption hits those too (in the JSON state pieces of
	// text like "id": and ,"on": were missing) and before it was neither seen nor repaired
	const unsigned long nBytes = (unsigned long) (__init_start - _start);
	s_nCodePages = (unsigned) ((nBytes + 4095) / 4096);
	s_pCodeCopy = new u32[s_nCodePages * 1024];
	memcpy (s_pCodeCopy, _start, s_nCodePages * 4096);
	pLogger->Write (FromKernel, LogNotice, "Check of code and constant data: %u pages from %p (code up to %p), copy at %p", s_nCodePages, (void *) _start, (void *) _etext, (void *) s_pCodeCopy);
}

// ---- hardware watchpoint: reports (with the instruction address) any processor write in the first 512 KB of the kernel
// (the code is never written: if something alters it, either it is an instruction, which the watchpoint stops, or it is an external agent such as the DMA)
static boolean s_bWatchOn = FALSE;

static void WatchSet (boolean bOn)
{
	if (!s_bWatchOn)
	{
		return;
	}
	// E=1, PAC=EL1+EL0, LSC=writes, BAS=0xFF, MASK=2^19 (512 KB)
	const unsigned long long nWcr = bOn ? ((19ULL << 24) | (0xFFULL << 5) | (2ULL << 3) | (3ULL << 1) | 1ULL) : 0ULL;
	asm volatile ("msr dbgwcr0_el1, %0\n\tisb" :: "r" (nWcr) : "memory");
}

static void WatchInit (CLogger *pLogger)
{
	unsigned long long nEl, nMd;
	asm volatile ("mrs %0, CurrentEL" : "=r" (nEl));
	asm volatile ("msr oslar_el1, xzr\n\tisb" ::: "memory");		// unlocks the OS debug
	asm volatile ("mrs %0, mdscr_el1" : "=r" (nMd));
	nMd |= (1ULL << 15) | (1ULL << 13);					// MDE and KDE: debug exceptions also for the kernel
	asm volatile ("msr mdscr_el1, %0\n\tisb" :: "r" (nMd) : "memory");
	asm volatile ("msr dbgwvr0_el1, %0" :: "r" (0x80000ULL) : "memory");
	s_bWatchOn = TRUE;
	WatchSet (TRUE);
	asm volatile ("msr daifclr, #8\n\tisb" ::: "memory");			// unmasks the debug exceptions
	unsigned long long nWcr;
	asm volatile ("mrs %0, dbgwcr0_el1" : "=r" (nWcr));
	pLogger->Write (FromKernel, LogNotice, "Watchpoint on code 0x80000..0xFFFFF: EL%u, MDSCR %llX, WCR %llX", (unsigned) ((nEl >> 2) & 3), nMd, nWcr);
}

// compares all the code with the copy; returns the number of changed words (and describes them in the log, at most 4 areas)
static unsigned s_nCodeFixes = 0;		// alterations repaired since boot

static unsigned CodeCheckRun (CLogger *pLogger, const char *pWhere, boolean bQuiet = FALSE)
{
	if (s_pCodeCopy == 0)
	{
		return 0;
	}
	const u32 *pLive = (const u32 *) _start;
	const unsigned nWords = s_nCodePages * 1024;
	// (discarding the lines from the cache before the comparison was tried on 2026-10-12: it finds nothing more and makes the exceptions rise,
	// because every discarded line has to be re-read from memory)
	unsigned nChanged = 0, nRuns = 0;
	unsigned w = 0;
	while (w < nWords)
	{
		if (pLive[w] == s_pCodeCopy[w])
		{
			w++;
			continue;
		}
		unsigned nStart = w, nCount = 0;
		while (w < nWords && pLive[w] != s_pCodeCopy[w])
		{
			nCount++;
			w++;
		}
		nChanged += nCount;
		if (nRuns++ < 4 && s_nCodeFixes < 64)
		{
			pLogger->Write (FromKernel, LogError, "CODE ALTERED (%s%s): zone %p..%p, %u words; first %08X->%08X, last %08X->%08X",
					pWhere, (const char *) &pLive[nStart] >= _etext ? ", constant data" : "", (void *) &pLive[nStart], (void *) &pLive[nStart + nCount - 1], nCount,
					s_pCodeCopy[nStart], pLive[nStart], s_pCodeCopy[nStart + nCount - 1], pLive[nStart + nCount - 1]);
		}
		// full content (up to 16 words = one 64-byte line) of what is found in place of the code: it helps to understand who writes it
			if (nRuns <= 4 && s_nCodeFixes < 64)
			{
				char Hex[16 * 9 + 8];
				unsigned h = 0;
				for (unsigned k = 0; k < nCount && k < 16; k++)
				{
					static const char D[] = "0123456789ABCDEF";
					const u32 v = pLive[nStart + k];
					for (int s = 28; s >= 0; s -= 4)
					{
						Hex[h++] = D[(v >> s) & 15];
					}
					Hex[h++] = ' ';
				}
				Hex[h] = 0;
				pLogger->Write (FromKernel, LogError, "CODE ALTERED contents @%p: %s", (void *) &pLive[nStart], Hex);
			}
			// repair from the clean copy made at boot
		WatchSet (FALSE);
			memcpy ((void *) &pLive[nStart], &s_pCodeCopy[nStart], nCount * sizeof (u32));
			WatchSet (TRUE);
		s_nCodeFixes++;
	}
	if (nChanged != 0)
	{
		SyncDataAndInstructionCache ();
	}
	else if (!bQuiet)
	{
		pLogger->Write (FromKernel, LogNotice, "Code intact (%s)", pWhere);
	}
	return nChanged;
}

// diagnostics of the audio DMA's LLI descriptors: snapshot after boot, then comparison at every tick
static unsigned s_Lli[2][2][16];
static boolean s_bLliSnap = FALSE;
static unsigned s_nLliReports = 0;

static void LliSnapshot (CLogger *pLogger, CPisoundSound *pSound)
{
	for (int rx = 0; rx < 2; rx++)
	{
		for (unsigned n = 0; n < 2; n++)
		{
			const unsigned *p = pSound->GetDmaLli (rx, n);
			memcpy (s_Lli[rx][n], p, 64);
			pLogger->Write (FromKernel, LogNotice, "DMA %s lli%u a %p: sar=%08X%08X dar=%08X%08X llp=%08X%08X ts=%08X ctl=%08X/%08X buffer=%p",
					rx ? "RX" : "TX", n, p, p[1], p[0], p[3], p[2], p[7], p[6], p[4], p[8], p[9], (const void *) pSound->GetDmaBuffer (rx, n));
		}
	}
	s_bLliSnap = TRUE;
}

static boolean LliCheck (CLogger *pLogger, CPisoundSound *pSound, unsigned nTick)
{
	boolean bAnyDiff = FALSE;
	if (!s_bLliSnap)
	{
		return FALSE;
	}
	for (int rx = 0; rx < 2; rx++)
	{
		for (unsigned n = 0; n < 2; n++)
		{
			const unsigned *p = pSound->GetDmaLli (rx, n);
			boolean bDiff = FALSE;
			for (unsigned w = 0; w < 9; w++)			// sar, dar, ts, llp, ctl_lo: constants
			{
				if (p[w] != s_Lli[rx][n][w])
				{
					bDiff = TRUE;
					bAnyDiff = TRUE;
				}
			}
			if (bDiff && s_nLliReports < 8)
			{
				s_nLliReports++;
				pLogger->Write (FromKernel, LogError, "LLI %s%u ALTERATO (tick %u): sar=%08X%08X dar=%08X%08X llp=%08X%08X ts=%08X ctl=%08X/%08X (era dar=%08X%08X)",
						rx ? "RX" : "TX", n, nTick, p[1], p[0], p[3], p[2], p[7], p[6], p[4], p[8], p[9], s_Lli[rx][n][3], s_Lli[rx][n][2]);
			}
		}
	}
	return bAnyDiff;
}

// every 100 ms: changes coming from the page, models written via TFTP, saving, levels
void CKernel::ControlTick (void)
{
	m_nTick++;
	{
		// Pisound button held for 3 s: concert mode (no network) if in normal mode, normal reboot if in concert.
		// It arms only after having seen it released (a button pressed at power-on does nothing) and fires once per press.
		static unsigned s_nBtn = 0;
		static boolean s_bBtnArmed = FALSE;
		if (m_Button.Read () == HIGH)
		{
			s_bBtnArmed = TRUE;
			s_nBtn = 0;
		}
		else if (s_bBtnArmed && ++s_nBtn == BUTTON_HOLD_TICKS && m_Ctl.ApAction == 0)
		{
			m_Logger.Write (FromKernel, LogNotice, "Pisound button held down: %s", m_bConcert ? "reboot into normal mode" : "reboot into concert mode (no network)");
			m_Ctl.ApAction = m_bConcert ? 3 : 4;
		}
	}
	if (m_Ctl.TestLock > 0)
	{
		m_Ctl.TestLock--;		// the acceptance test lock expires by itself (the test renews it at every operation)
	}
	// audio DMA descriptors zeroed (the same 64-byte alteration as the code, but in the heap): the channel stops and the audio stays muted
	// until someone reboots the Pi; after 1 s of continuous alteration it saves and reboots by itself
	static unsigned s_nLliBad = 0;
	if (LliCheck (&m_Logger, m_pSound, m_nTick))
	{
		if (++s_nLliBad == 10)
		{
			m_Logger.Write (FromKernel, LogError, "Audio DMA descriptors altered for 1 s: the audio is stopped, saving and rebooting");
			DmaAlarmLog ();
			if (m_bConcert && m_bSdOk)
			{
				ConcertFlagPut ();		// in concert an alarm reboot again restarts without network (you leave it by powering off and on)
			}
			if (m_bSdOk)
			{
				SettingsSave ();
				BoardSave ();
			}
			m_Scheduler.MsSleep (500);
			reboot ();
		}
	}
	else
	{
		s_nLliBad = 0;
	}
	CodeCheckRun (&m_Logger, "cycle", TRUE);		// every 100 ms: checks and repairs the code in memory

	// web server: a request started and never finished means its task died (exception): save and restart
	if (m_Status.nReqStart != m_Status.nReqDone)
	{
		if (m_nStuckSince == 0)
		{
			m_nStuckSince = m_nTick;
		}
		else if (m_nTick - m_nStuckSince > 50)
		{
			m_Logger.Write (FromKernel, LogError, "Web server stuck (requests started %u, finished %u): saving and rebooting",
					m_Status.nReqStart, m_Status.nReqDone);
			if (m_bSdOk)
			{
				SettingsSave ();
				BoardSave ();
			}
			m_Scheduler.MsSleep (500);
			reboot ();
		}
	}
	else
	{
		m_nStuckSince = 0;
	}

	const unsigned nPeak = m_pSound->GetInPeak24 ();
	if (nPeak > m_nPeakLog)
	{
		m_nPeakLog = nPeak;
	}
	if (nPeak > m_nPeakUi)
	{
		m_nPeakUi = nPeak;
	}
	unsigned nPeakL, nPeakR, nPeakS;
	m_pSound->GetChannelPeaks (&nPeakL, &nPeakR, &nPeakS);
	{
			// real-time bars: peak of the last tick and of the previous one (200 ms), so a skipped read does not lose a peak
			static unsigned s_nPrevL = 0, s_nPrevR = 0, s_nPrevS = 0;
			const unsigned l = nPeakL > s_nPrevL ? nPeakL : s_nPrevL, r = nPeakR > s_nPrevR ? nPeakR : s_nPrevR;
			m_Status.nMtInL = (unsigned) ((unsigned long long) l * 1000000ULL / 8388608ULL);
			m_Status.nMtInR = (unsigned) ((unsigned long long) r * 1000000ULL / 8388608ULL);
			m_Status.nMtInS = (unsigned) ((unsigned long long) (nPeakS > s_nPrevS ? nPeakS : s_nPrevS) * 1000000ULL / 8388608ULL);
			s_nPrevS = nPeakS;
			s_nPrevL = nPeakL;
			s_nPrevR = nPeakR;
		}
		m_nPeakUiL = nPeakL > m_nPeakUiL ? nPeakL : m_nPeakUiL;
	m_nPeakUiR = nPeakR > m_nPeakUiR ? nPeakR : m_nPeakUiR;
	if (m_nTick - m_nPeakUiTick >= 10)
	{
		m_Status.nPeakPpm = (unsigned) ((unsigned long long) m_nPeakUi * 1000000ULL / 8388608ULL);
		m_Status.nPeakLPpm = (unsigned) ((unsigned long long) m_nPeakUiL * 1000000ULL / 8388608ULL);
		m_Status.nPeakRPpm = (unsigned) ((unsigned long long) m_nPeakUiR * 1000000ULL / 8388608ULL);
		m_nPeakUiL = 0;
		m_nPeakUiR = 0;
		m_nPeakUi = 0;
		m_nPeakUiTick = m_nTick;
	}
	m_Status.nUptimeSec = m_Timer.GetUptime ();
	m_Status.nShed = m_pSound != 0 ? m_pSound->GetShedCount () : 0;

	// outputs: peaks and energy accumulated for one second (ppm of full scale)
	{
		float fPkL, fPkR, fSqL, fSqR;
		unsigned nFrames;
		m_pSound->GetOutputStats (&fPkL, &fPkR, &fSqL, &fSqR, &nFrames);
		{
			static float s_fPrevL = 0.0f, s_fPrevR = 0.0f;
			const float l = fPkL > s_fPrevL ? fPkL : s_fPrevL, r = fPkR > s_fPrevR ? fPkR : s_fPrevR;
			m_Status.nMtOutL = (unsigned) (l * 1e6f);
			m_Status.nMtOutR = (unsigned) (r * 1e6f);
			s_fPrevL = fPkL;
			s_fPrevR = fPkR;
		}
		m_fOutPkL = fPkL > m_fOutPkL ? fPkL : m_fOutPkL;
		m_fOutPkR = fPkR > m_fOutPkR ? fPkR : m_fOutPkR;
		m_fOutSqL += fSqL;
		m_fOutSqR += fSqR;
		m_nOutFrames += nFrames;
		if (m_nTick % 10 == 0 && m_nOutFrames > 0)
		{
			m_Status.nOutPkLPpm = (unsigned) (m_fOutPkL * 1e6f);
			m_Status.nOutPkRPpm = (unsigned) (m_fOutPkR * 1e6f);
			m_Status.nOutRmsLPpm = (unsigned) (DspSqrtF (m_fOutSqL / (float) m_nOutFrames) * 1e6f);
			m_Status.nOutRmsRPpm = (unsigned) (DspSqrtF (m_fOutSqR / (float) m_nOutFrames) * 1e6f);
			m_fOutPkL = m_fOutPkR = m_fOutSqL = m_fOutSqR = 0.0f;
			m_nOutFrames = 0;
		}
	}

	// DI recorder
	if (m_Ctl.DiReq != 0)
	{
		const unsigned nReq = m_Ctl.DiReq;
		m_Ctl.DiReq = 0;
		if (nReq == 1)
		{
			const boolean bAppend = m_Ctl.DiAppend != 0;
			if (!m_pSound->DiPrepare (m_Ctl.DiSeconds, bAppend))
			{
				if (!bAppend)
				{
					m_pSound->DiFree ();
				}
				m_Logger.Write (FromKernel, LogWarning, "DI recorder: not enough memory or the buffer is full");
			}
			else
			{
				m_Logger.Write (FromKernel, LogNotice, "DI recorder: started (%u frames)", m_pSound->DiTarget ());
			}
		}
		else if (nReq == 2)
		{
			m_pSound->DiStop ();
		}
		else if (nReq == 3)
		{
			m_pSound->DiFree ();
		}
	}
	m_Ctl.DiState = m_pSound->DiState ();
	m_Ctl.DiFrames = m_pSound->DiFrames ();
	m_Ctl.DiTarget = m_pSound->DiTarget ();
	m_Ctl.DiMax = m_pSound->DiMaxFrames ();
	m_Ctl.DiMarkN = m_pSound->DiMarkCount ();
	for (unsigned i = 0; i < m_Ctl.DiMarkN && i < 64; i++)
	{
		m_Ctl.DiMark[i] = m_pSound->DiMark (i);
	}

	// test source: turns off by itself after 2 minutes
	if (m_Ctl.TestPending)
	{
		m_Ctl.TestPending = FALSE;
		m_pSound->SetTestSource (m_Ctl.TestMask);
		m_Status.nTestSrc = m_Ctl.TestMask;
		m_nTestUntilTick = m_Ctl.TestMask ? m_nTick + 1200 : 0;
		m_Logger.Write (FromKernel, LogNotice, "Test source: mask %u (outputs towards the amps at -66 dB)", m_Ctl.TestMask);
	}
	if (m_nTestUntilTick != 0 && m_nTick >= m_nTestUntilTick)
	{
		m_nTestUntilTick = 0;
		m_pSound->SetTestSource (0);
		m_Status.nTestSrc = 0;
		m_Logger.Write (FromKernel, LogNotice, "Test source off (timeout)");
	}

	unsigned b, q;
	while (m_Params.TakeChange (&b, &q))
	{
		if (m_Params.Block (b).P[q].Kind != ParamText)
		{
			ApplyMaster ();			// master volume and bypass (a guitar name does not change the audio: no new plan)
		}
		m_bSavePending = TRUE;
		m_nSaveAtTick = m_nTick + 15;
	}

	char Name[PARAM_TEXT_LEN];
	if (m_pOta != 0 && m_pOta->TakeModelFile (Name, sizeof Name))
	{
		const boolean bAir = strlen (Name) > 4 && strcmp (Name + strlen (Name) - 4, ".air") == 0;
		m_Logger.Write (FromKernel, LogNotice, "%s received: %s", bAir ? "IR" : "Model", Name);
		if (!bAir)
		{
			LibOrderAdd (Name);
		}
		ScanLibrary ();
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			TBdBlock &Bl = m_pBoard->Block (id);
			if (Bl.Used && Bl.Type == (bAir ? FxCabIR : NodeNam) && strcmp (Bl.File, Name) == 0)
			{
				Bl.LoadedFile[0] = 0;		// to be reloaded
				Bl.FailedFile[0] = 0;
			}
		}
	}

	if (m_Ctl.DelPending)
	{
		// deletion requested by the page (already checked: library file and not in use); re-check before touching the card
		const boolean bAirDel = ValidIrFile (m_Ctl.DelName);
		boolean bInUse = !ValidModelFile (m_Ctl.DelName) && !bAirDel;
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			const TBdBlock &Bl = m_pBoard->Block (id);
			if (Bl.Used && (Bl.Type == NodeNam || Bl.Type == FxCabIR) && strcmp (Bl.File, m_Ctl.DelName) == 0)
			{
				bInUse = TRUE;
			}
		}
		if (bInUse)
		{
			m_Logger.Write (FromKernel, LogWarning, "Deletion refused: %s", m_Ctl.DelName);
		}
		else
		{
			CString Path;
			Path.Format ("SD:/%s", m_Ctl.DelName);
			FRESULT Result = f_unlink (Path);
			{
				CString InfPath;
				InfPath.Format ("SD:/%s", m_Ctl.DelName);
				char *pDot = (char *) (const char *) InfPath + strlen (InfPath) - 4;
				strcpy (pDot, ".inf");
				f_unlink (InfPath);
			}
			m_Logger.Write (FromKernel, LogNotice, "Model deleted: %s (%s)", m_Ctl.DelName, Result == FR_OK ? "ok" : "error");
			ScanLibrary ();
		}
		m_Ctl.DelPending = FALSE;
	}

	if (m_Ctl.ModePending != MODE_NONE)
	{
		m_Ctl.ModePending = MODE_NONE;
	}
	BoardTick ();
	PreTick ();
	UpTick ();

	if (m_Ctl.MidiCfgDirty && m_bSdOk)
	{
		m_Ctl.MidiCfgDirty = FALSE;
		MidiCfgWrite ();
	}
	if (m_Ctl.FeatDirty && m_bSdOk)
	{
		m_Ctl.FeatDirty = FALSE;
		FeatCfgWrite ();
	}
	if (m_Ctl.McDirty && m_bSdOk)
	{
		m_Ctl.McDirty = FALSE;
		McCfgWrite ();
	}
	if (m_Ctl.ThermDirty && m_bSdOk)
	{
		m_Ctl.ThermDirty = FALSE;
		ThermalCfgWrite ();
	}
	if (m_Ctl.ApAction != 0 && m_bSdOk)
	{
		const unsigned nAct = m_Ctl.ApAction;
		m_Ctl.ApAction = 0;
		if (nAct == 3 || nAct == 4)
		{
			BootCounterWrite (0);		// intended reboot, does not count as unstable
			if (nAct == 4)
			{
				SettingsSave ();
				BoardSave ();
				ConcertFlagPut ();
			}
			m_Logger.Write (FromKernel, LogNotice, nAct == 4 ? "Reboot into concert mode (no network) requested by the page" : "Reboot requested by the page");
			m_Scheduler.MsSleep (1500);
			reboot ();
		}
		ApCfgWrite ();
		if (nAct == 2)
		{
			ApReboot ();
		}
	}
	if (m_pFeat != 0)
	{
		m_pFeat->Tick ((unsigned) (CTimer::GetClockTicks64 () / 1000ULL));		// analysis (tuner, spectrum, meters)
	}

	if (m_Ctl.BpDirty && m_bSdOk)
	{
		m_Ctl.BpDirty = FALSE;
		BpSave ();
	}

	if (m_Ctl.FavDirty && m_bSdOk)
	{
		m_Ctl.FavDirty = FALSE;
		FavSave ();
	}

	if (m_bSavePending && m_nTick >= m_nSaveAtTick && m_bSdOk)
	{
		m_bSavePending = FALSE;
		SettingsSave ();
	}
}

// counter of the boots that are not (yet) stable
unsigned CKernel::BootCounterRead (void)
{
	FIL File;
	if (f_open (&File, BOOT_COUNTER_FILE, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return 0;
	}

	char Buffer[16];
	UINT nRead = 0;
	f_read (&File, Buffer, sizeof Buffer - 1, &nRead);
	f_close (&File);

	unsigned nValue = 0;
	for (UINT i = 0; i < nRead && Buffer[i] >= '0' && Buffer[i] <= '9'; i++)
	{
		nValue = nValue * 10 + (unsigned) (Buffer[i] - '0');
	}

	return nValue;
}

void CKernel::BootCounterWrite (unsigned nValue)
{
	FIL File;
	if (f_open (&File, BOOT_COUNTER_FILE, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}

	char Buffer[16];
	unsigned nLen = 0;
	char Digits[12];
	unsigned nDigits = 0;
	do
	{
		Digits[nDigits++] = (char) ('0' + nValue % 10);
		nValue /= 10;
	}
	while (nValue > 0 && nDigits < sizeof Digits);
	while (nDigits > 0)
	{
		Buffer[nLen++] = Digits[--nDigits];
	}
	Buffer[nLen++] = '\n';

	UINT nWritten = 0;
	f_write (&File, Buffer, nLen, &nWritten);
	f_close (&File);
}

// USB, SD and file system, in the same order as the hello_wlan example
boolean CKernel::StorageInit (void)
{
	if (!m_USBHCI.Initialize ())
	{
		m_Logger.Write (FromKernel, LogError, "USB not initialized");
		return FALSE;
	}
	m_nNetStage = 1;

	if (!m_EMMC.Initialize ())
	{
		m_Logger.Write (FromKernel, LogError, "EMMC/SD not initialized");
		return FALSE;
	}
	m_nNetStage = 2;

	if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
	{
		m_Logger.Write (FromKernel, LogError, "Unable to mount %s", DRIVE);
		return FALSE;
	}
	m_nNetStage = 3;

	return TRUE;
}

// ---- fallback network: if the home network is not found, a reboot makes the board a WPA2 access point (192.168.4.1) with its own DHCP server ----
#define AP_CFG_FILE	DRIVE "/ap.cfg"
#define AP_FLAG_FILE	DRIVE "/apnext.flg"
#define CONCERT_FLAG_FILE	DRIVE "/concert.flg"
#define DMA_LOG_FILE	DRIVE "/dmaalarm.log"
static const u8 s_ApIp[4] = {192, 168, 4, 1};
static const u8 s_ApMask[4] = {255, 255, 255, 0};

// random 10-character password without ambiguous symbols (from the hardware counter, like the page's boot code)
static void ApRandom (char *pOut, unsigned nLen)
{
	static const char Alpha[] = "abcdefghjkmnpqrstuvwxyz23456789";		// 31 characters
	unsigned long long nCnt;
	asm volatile ("mrs %0, cntvct_el0" : "=r" (nCnt));
	u32 x = (u32) nCnt ^ (u32) (nCnt >> 32) * 2654435761u ^ (u32) CTimer::GetClockTicks () * 40503u;
	for (unsigned i = 0; i < nLen; i++)
	{
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		x += (u32) nCnt >> (i * 3);
		pOut[i] = Alpha[(x >> 7) % 31];
	}
	pOut[nLen] = 0;
}

static boolean ApWordOk (const char *p, unsigned nMin, unsigned nMax)
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

// ap.cfg: "name password channel"; if missing or not valid it creates one with the random password (the page shows it)
void CKernel::ApCfgRead (void)
{
	char Ssid[40], Pass[72];
	unsigned nChan = 6;
	boolean bOk = FALSE;
	FIL File;
	if (m_bSdOk && f_open (&File, AP_CFG_FILE, FA_READ | FA_OPEN_EXISTING) == FR_OK)
	{
		char Text[128];
		UINT nRead = 0;
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
		Text[nRead] = 0;
		unsigned i = 0, k = 0;
		while (Text[i] != 0 && Text[i] != ' ' && Text[i] != '\n' && k < sizeof Ssid - 1)
		{
			Ssid[k++] = Text[i++];
		}
		Ssid[k] = 0;
		if (Text[i] == ' ')
		{
			i++;
		}
		k = 0;
		while (Text[i] != 0 && Text[i] != ' ' && Text[i] != '\n' && k < sizeof Pass - 1)
		{
			Pass[k++] = Text[i++];
		}
		Pass[k] = 0;
		if (Text[i] == ' ')
		{
			i++;
		}
		unsigned c = 0;
		while (Text[i] >= '0' && Text[i] <= '9' && c < 100)
		{
			c = c * 10 + (unsigned) (Text[i++] - '0');
		}
		bOk = ApWordOk (Ssid, 1, 32) && ApWordOk (Pass, 8, 63) && c >= 1 && c <= 13;
		if (bOk)
		{
			nChan = c;
		}
	}
	if (!bOk)
	{
		CParams::Copy (Ssid, "NAMberry5", sizeof Ssid);
		ApRandom (Pass, 10);
	}
	CParams::Copy (m_Ctl.ApSsid, Ssid, sizeof m_Ctl.ApSsid);
	CParams::Copy (m_Ctl.ApPass, Pass, sizeof m_Ctl.ApPass);
	m_Ctl.ApChan = nChan;
	if (!bOk && m_bSdOk)
	{
		ApCfgWrite ();
		m_Logger.Write (FromKernel, LogNotice, "Fallback network: created %s with name \"%s\" and a random password (read it from the page)", AP_CFG_FILE, m_Ctl.ApSsid);
	}
}

void CKernel::ApCfgWrite (void)
{
	FIL File;
	if (f_open (&File, AP_CFG_FILE, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	char Text[128];
	CString Line;
	Line.Format ("%s %s %u\n", m_Ctl.ApSsid, m_Ctl.ApPass, (unsigned) m_Ctl.ApChan);
	CParams::Copy (Text, Line, sizeof Text);
	UINT nWritten = 0;
	f_write (&File, Text, strlen (Text), &nWritten);
	f_close (&File);
}

// concert mode: SD:/concert.flg is valid for a single boot and starts the board WITHOUT network (no Wi-Fi, page, syslog, OTA); MIDI and the pedalboard work as always
boolean CKernel::ConcertFlagTake (void)
{
	FIL File;
	if (f_open (&File, CONCERT_FLAG_FILE, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return FALSE;
	}
	f_close (&File);
	f_unlink (CONCERT_FLAG_FILE);
	return TRUE;
}

void CKernel::ConcertFlagPut (void)
{
	FIL File;
	if (f_open (&File, CONCERT_FLAG_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		f_write (&File, "1\n", 2, &nWritten);
		f_close (&File);
	}
}

// persistent record of the DMA alarms (it stays even without network): one line "seconds_of_operation concert_mode" per alarm
void CKernel::DmaAlarmLog (void)
{
	FIL File;
	if (!m_bSdOk || f_open (&File, DMA_LOG_FILE, FA_WRITE | FA_OPEN_APPEND) != FR_OK)
	{
		return;
	}
	CString Line;
	Line.Format ("%u %u\n", (unsigned) (CTimer::GetClockTicks () / 1000000), m_bConcert ? 1u : 0u);
	UINT nWritten = 0;
	f_write (&File, (const char *) Line, Line.GetLength (), &nWritten);
	f_close (&File);
}

void CKernel::DmaAlarmLoad (void)
{
	m_Ctl.DmaAlarms = 0;
	FIL File;
	if (!m_bSdOk || f_open (&File, DMA_LOG_FILE, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	static char Text[2048];
	UINT nRead = 0;
	f_read (&File, Text, sizeof Text, &nRead);
	f_close (&File);
	for (UINT i = 0; i < nRead; i++)
	{
		if (Text[i] == '\n')
		{
			m_Ctl.DmaAlarms++;
		}
	}
}

boolean CKernel::ApFlagTake (void)
{
	FIL File;
	if (f_open (&File, AP_FLAG_FILE, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return FALSE;
	}
	f_close (&File);
	f_unlink (AP_FLAG_FILE);		// valid for a single boot
	return TRUE;
}

void CKernel::ApFlagPut (void)
{
	FIL File;
	if (f_open (&File, AP_FLAG_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		f_write (&File, "1\n", 2, &nWritten);
		f_close (&File);
	}
}

// in-memory log device (first 3000 bytes): in the fallback network the syslog does not arrive, so the Wi-Fi driver's messages are read afterwards
class CApRingDevice : public CDevice
{
public:
	// filters by line: of the driver dumps only the first line of each command and the replies remain (from there the outcome of each command is read)
	int Write (const void *pBuffer, size_t nCount) override
	{
		const char *p = (const char *) pBuffer;
		for (size_t i = 0; i < nCount; i++)
		{
			const char c = p[i] == '\r' ? ' ' : p[i];
			if (c == '\n')
			{
				m_Line[m_nLine] = 0;
				Flush ();
				m_nLine = 0;
			}
			else if (m_nLine < sizeof m_Line - 1)
			{
				m_Line[m_nLine++] = c;
			}
		}
		return (int) nCount;
	}
	unsigned Length (void) const { return m_nLen; }
	const char *Data (void) const { return m_Buf; }
private:
	static boolean Has (const char *pLine, const char *pPart)
	{
		const unsigned n = strlen (pPart);
		for (const char *q = pLine; *q; q++)
		{
			if (strncmp (q, pPart, n) == 0)
			{
				return TRUE;
			}
		}
		return FALSE;
	}
	void Flush (void)
	{
		boolean bKeep = TRUE;
		if (Has (m_Line, "Dumping") || (Has (m_Line, "event:") && !Has (m_Line, "event: 0020") && !Has (m_Line, "event: 0030")))
		{
			bKeep = FALSE;
		}
		else if (Has (m_Line, "cmd: 0") && !Has (m_Line, "cmd: 0000"))
		{
			bKeep = FALSE;
		}
		if (!bKeep || m_nLen + m_nLine + 1 > sizeof m_Buf)
		{
			return;
		}
		for (unsigned i = 0; i < m_nLine; i++)
		{
			m_Buf[m_nLen++] = m_Line[i];
		}
		m_Buf[m_nLen++] = '\n';
	}
	char m_Buf[5000];
	volatile unsigned m_nLen = 0;
	char m_Line[200];
	unsigned m_nLine = 0;
};
static CApRingDevice s_ApRing;

void CKernel::ApDrvFlush (void)
{
	FIL File;
	if (!m_bSdOk || f_open (&File, DRIVE "/apdrv.log", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	const unsigned n = s_ApRing.Length ();
	UINT nWritten = 0;
	f_write (&File, s_ApRing.Data (), n, &nWritten);
	f_close (&File);
}

// engine's worker cores (only if Circle is compiled with ARM_ALLOW_MULTI_CORE): cores 1..3 run GraphWorker (graph.cpp)
#ifdef ARM_ALLOW_MULTI_CORE
static volatile unsigned s_McAlive[4];

class CDspCores : public CMultiCoreSupport
{
public:
	CDspCores (CMemorySystem *pMemorySystem, unsigned nWorkers)
	:	CMultiCoreSupport (pMemorySystem),
		m_nWorkers (nWorkers)
	{
	}

	void Run (unsigned nCore) override
	{
		if (nCore == 0 || nCore > m_nWorkers)
		{
			for (;;)
			{
				asm volatile ("wfe");
			}
		}
		s_McAlive[nCore] = 1;
		GraphWorker (nCore);
	}

private:
	unsigned m_nWorkers;
};
#endif

// multicore.cfg: number of extra worker cores (0..3), one digit
void CKernel::McCfgWrite (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/multicore.cfg", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	const char Text[3] = {(char) ('0' + (m_Ctl.McCfg > 3 ? 0 : m_Ctl.McCfg)), '\n', 0};
	UINT nWritten = 0;
	f_write (&File, Text, 2, &nWritten);
	f_close (&File);
}

// after the SD and before the audio and the pedalboard loading: starts the requested worker cores; if they do not all start, core 0 remains
void CKernel::McInit (void)
{
	m_Ctl.McCfg = 0;
	m_Ctl.McActive = 1;
	m_Ctl.McDirty = FALSE;
#ifdef ARM_ALLOW_MULTI_CORE
	m_Ctl.McAvail = TRUE;
	FIL File;
	if (m_bSdOk && f_open (&File, DRIVE "/multicore.cfg", FA_READ | FA_OPEN_EXISTING) == FR_OK)
	{
		char Text[8];
		UINT nRead = 0;
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
		if (nRead > 0 && Text[0] >= '0' && Text[0] <= '3')
		{
			m_Ctl.McCfg = (unsigned) (Text[0] - '0');
		}
	}
	if (m_Ctl.McCfg == 0 || m_bSafeMode)
	{
		return;
	}
	const unsigned nWorkers = m_Ctl.McCfg;
	CDspCores *pDsp = new CDspCores (CMemorySystem::Get (), nWorkers);
	if (!pDsp->Initialize ())
	{
		m_Logger.Write (FromKernel, LogError, "Multi-core: the worker cores did not start, core 0 remains");
		return;
	}
	unsigned nAlive = 0;
	for (unsigned nWait = 0; nWait < 200; nWait++)
	{
		nAlive = 0;
		for (unsigned c = 1; c <= nWorkers; c++)
		{
			nAlive += s_McAlive[c] ? 1 : 0;
		}
		if (nAlive == nWorkers)
		{
			break;
		}
		m_Timer.MsDelay (1);
	}
	if (nAlive != nWorkers)
	{
		m_Logger.Write (FromKernel, LogError, "Multi-core: %u cores started out of %u, core 0 remains", nAlive, nWorkers);
		return;
	}
	GraphMcEnable (1 + nWorkers);
	CBoard::SetCores (1 + nWorkers);
	m_Ctl.McActive = 1 + nWorkers;
	m_Logger.Write (FromKernel, LogNotice, "Multi-core: %u cores active (core 0 with the audio and %u workers)", 1 + nWorkers, nWorkers);
#else
	m_Ctl.McAvail = FALSE;
#endif
}

// temperature and fan settings: SD:/thermal.cfg = "fan_control on_threshold off_threshold 2400MHz_at_next_boot"
void CKernel::ThermalCfgRead (void)
{
	m_Ctl.FanCtl = 0;
	m_Ctl.FanDuty = -1;
	m_Ctl.FanDutyNew = FALSE;
	m_Ctl.CpuFan = 0;
	m_Ctl.FanPct = 0;
	m_Ctl.FanMaxPct = FAN_MAX_DEFAULT_PCT;
	m_Ctl.ThermDirty = FALSE;
	m_Ctl.FanOnC = FAN_ON_DEFAULT_C;
	m_Ctl.FanOffC = FAN_OFF_DEFAULT_C;
	m_Ctl.BootNext = 0;
	FIL File;
	if (!m_bSdOk || f_open (&File, DRIVE "/thermal.cfg", FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	char Text[48];
	UINT nRead = 0;
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = 0;
	unsigned v[5] = {0, 0, 0, 0, FAN_MAX_DEFAULT_PCT};
	unsigned nF = 0;
	const char *p = Text;
	while (nF < 5 && *p >= '0' && *p <= '9')
	{
		unsigned x = 0;
		while (*p >= '0' && *p <= '9' && x < 1000)
		{
			x = x * 10 + (unsigned) (*p++ - '0');
		}
		v[nF++] = x;
		if (*p == ' ')
		{
			p++;
		}
	}
	if ((nF == 4 || nF == 5) && v[0] <= 1 && v[3] <= 1 && v[1] >= 40 && v[1] <= 80 && v[2] >= 30 && v[2] + 2 <= v[1] && v[4] >= 35 && v[4] <= 100)
	{
		m_Ctl.FanCtl = v[0];
		m_Ctl.FanOnC = v[1];
		m_Ctl.FanOffC = v[2];
		m_Ctl.BootNext = v[3];
		m_Ctl.FanMaxPct = v[4];
	}
}

void CKernel::ThermalCfgWrite (void)
{
	FIL File;
	if (f_open (&File, DRIVE "/thermal.cfg", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	CString Line;
	Line.Format ("%u %u %u %u %u\n", (unsigned) m_Ctl.FanCtl, (unsigned) m_Ctl.FanOnC, (unsigned) m_Ctl.FanOffC, (unsigned) m_Ctl.BootNext, (unsigned) m_Ctl.FanMaxPct);
	UINT nWritten = 0;
	f_write (&File, (const char *) Line, Line.GetLength (), &nWritten);
	f_close (&File);
}

// processor frequency (same call as CCPUThrottle): nRate in Hz
static void SetCpuClock (unsigned nRate)
{
	CBcmPropertyTags Tags;
	TPropertyTagSetClockRate TagSet;
	TagSet.nClockId = CLOCK_ID_ARM;
	TagSet.nRate = nRate;
	TagSet.nSkipSettingTurbo = SKIP_SETTING_TURBO;
	Tags.GetTag (PROPTAG_SET_CLOCK_RATE, &TagSet, sizeof TagSet, 12);
	CTimer::SimpleusDelay (355);		// settling pause, as in CCPUThrottle
}

// at startup, after the SD and before the audio: reads the settings, takes the processor to the maximum if requested and takes control of the fan
void CKernel::ThermalInit (void)
{
	m_pFanPwm = 0;
	m_nFanPwmSince = 0;
	m_bFanRun = TRUE;
	m_bFanPwmFailed = FALSE;
	ThermalCfgRead ();
	if (m_Ctl.BootNext && m_Ctl.CpuMhz[2] > m_Ctl.CpuMhz[0] && m_Ctl.CpuMhz[0] > 0)
	{
		SetCpuClock (m_Ctl.CpuMhz[2] * 1000000);
		CBcmPropertyTags Tags;
		TPropertyTagClockRate TagNow;
		TagNow.nClockId = CLOCK_ID_ARM;
		if (Tags.GetTag (PROPTAG_GET_CLOCK_RATE, &TagNow, sizeof TagNow, 4))
		{
			m_Ctl.CpuMhz[0] = TagNow.nRate / 1000000;
		}
		m_Logger.Write (FromKernel, LogNotice, "Processor raised to %u MHz", m_Ctl.CpuMhz[0]);
	}
	CBoard::SetCpuMhz (m_Ctl.CpuMhz[0]);
	if (m_Ctl.FanCtl && FanPwmStart ())
	{
		FanPwmSet (100);		// at the maximum until the temperature is read
	}
	else if (m_Ctl.FanCtl)
	{
		m_Ctl.CpuFan = 1;		// PWM not available: pin on (see FanPwmStart)
	}
}

// PWM fan: GPIO 45 is the channel with index 3 (PWM_CHANNEL4 in Circle) of the RP1's PWM1 (alternate function 0), inverted polarity (duty 100 = always on, like Linux's pwm-fan driver)
boolean CKernel::FanPwmStart (void)
{
	if (m_pFanPwm != 0)
	{
		return TRUE;
	}
	if (m_bFanPwmFailed)
	{
		return FALSE;
	}
	if (!m_bFanOwned)
	{
		m_FanPin.AssignPin (FAN_GPIO);
		m_bFanOwned = TRUE;
	}
	m_FanPin.SetMode (GPIOModeAlternateFunction0, FALSE);
	m_pFanPwm = new CPWMOutput (FAN_PWM_HZ * FAN_PWM_RANGE, FAN_PWM_RANGE, TRUE, TRUE, 1);
	if (m_pFanPwm == 0 || !m_pFanPwm->Start ())
	{
		m_Logger.Write (FromKernel, LogWarning, "PWM fan: PWM1 start failed, fan on/off with the thresholds");
		delete m_pFanPwm;
		m_pFanPwm = 0;
		m_bFanPwmFailed = TRUE;
		m_FanPin.SetMode (GPIOModeOutput, FALSE);
		m_FanPin.Write (LOW);			// on; FanTick's threshold logic turns it off below the turn-off threshold
		return FALSE;
	}
	m_Logger.Write (FromKernel, LogNotice, "PWM fan (%u Hz): maximum %u%% up to %u C, 100%% at %u C", FAN_PWM_HZ, (unsigned) m_Ctl.FanMaxPct, FAN_CAP_AT_C, FAN_FULL_C);
	return TRUE;
}

void CKernel::FanPwmSet (unsigned nPct)
{
	if (nPct > 100)
	{
		nPct = 100;
	}
	m_pFanPwm->Write (PWM_CHANNEL4, nPct * FAN_PWM_RANGE / 100);		// Circle's channels start from 1: the RP1's PWM1_CHAN3 (index 3) is PWM_CHANNEL4
	m_Ctl.FanPct = nPct;
	m_Ctl.CpuFan = nPct > 0 ? 1 : 0;
}

// every 2 s: fan. Manual test (op fandt, expires by itself after FAN_PWM_TEST_SECONDS or at FAN_PWM_MAX_C degrees) or, if I control it, proportional curve with hysteresis
void CKernel::FanTick (unsigned nTemp)
{
	const unsigned nNow = (unsigned) (CTimer::GetClockTicks () / 1000000);
	if (m_Ctl.FanDutyNew)
	{
		m_Ctl.FanDutyNew = FALSE;
		m_nFanPwmSince = nNow;
	}
	if (m_Ctl.FanDuty >= 0 && (nTemp >= FAN_PWM_MAX_C * 1000 || nNow - m_nFanPwmSince > FAN_PWM_TEST_SECONDS))
	{
		m_Logger.Write (FromKernel, LogNotice, "PWM fan test finished (%u.%u C)", nTemp / 1000, (nTemp % 1000) / 100);
		m_Ctl.FanDuty = -1;
		m_Ctl.FanCtl = 1;			// after a test the fan stays under the firmware's control (not saved on the SD: at reboot the previous configuration returns)
	}
	if (m_Ctl.FanDuty >= 0)
	{
		if (FanPwmStart ())
		{
			FanPwmSet ((unsigned) m_Ctl.FanDuty);
		}
		else
		{
			m_Ctl.FanDuty = -1;
		}
		return;
	}
	if (!m_Ctl.FanCtl && m_pFanPwm == 0 && !m_bFanOwned)
	{
		return;					// the system manages it
	}
	if (m_pFanPwm == 0 && !FanPwmStart () && !m_bFanPwmFailed)
	{
		return;
	}
	if (!m_bFanRun && nTemp >= m_Ctl.FanOnC * 1000)
	{
		m_bFanRun = TRUE;
	}
	else if (m_bFanRun && nTemp <= m_Ctl.FanOffC * 1000)
	{
		m_bFanRun = FALSE;
	}
	if (m_pFanPwm != 0)
	{
		FanPwmSet (m_bFanRun ? FanCurvePct (nTemp, m_Ctl.FanOnC, m_Ctl.FanMaxPct) : 0);
	}
	else
	{
		m_FanPin.Write (m_bFanRun ? LOW : HIGH);	// PWM not available: on/off as before
		m_Ctl.CpuFan = m_bFanRun ? 1 : 0;
		m_Ctl.FanPct = m_bFanRun ? 100 : 0;
	}
}

// every 2 s: temperature and frequency; fan with hysteresis (if the firmware controls it); above THROTTLE_HI_MC the processor goes back to the minimum, below THROTTLE_LO_MC to the maximum
void CKernel::ThermalTick (void)
{
	CBcmPropertyTags Tags;
	TPropertyTagTemperature TagTemp;
	TagTemp.nTemperatureId = 0;
	if (!Tags.GetTag (PROPTAG_GET_TEMPERATURE, &TagTemp, sizeof TagTemp, 4) || TagTemp.nValue == 0)
	{
		return;
	}
	const unsigned nTemp = TagTemp.nValue;			// thousandths of a degree
	m_Ctl.CpuTemp = nTemp / 100;
	FanTick (nTemp);
	TPropertyTagClockRate TagRate;
	TagRate.nClockId = CLOCK_ID_ARM;
	if (Tags.GetTag (PROPTAG_GET_CLOCK_RATE, &TagRate, sizeof TagRate, 4))
	{
		m_Ctl.CpuMhz[0] = TagRate.nRate / 1000000;
		if (nTemp >= THROTTLE_HI_MC && TagRate.nRate > m_Ctl.CpuMhz[1] * 1000000)
		{
			SetCpuClock (m_Ctl.CpuMhz[1] * 1000000);
			CBoard::SetCpuMhz (m_Ctl.CpuMhz[1]);
			m_Logger.Write (FromKernel, LogWarning, "Processor too hot (%u.%u C): frequency reduced to %u MHz", nTemp / 1000, (nTemp % 1000) / 100, m_Ctl.CpuMhz[1]);
		}
		else if (nTemp <= THROTTLE_LO_MC && TagRate.nRate < m_Ctl.CpuMhz[2] * 1000000 && m_Ctl.BootNext)
		{
			SetCpuClock (m_Ctl.CpuMhz[2] * 1000000);
			CBoard::SetCpuMhz (m_Ctl.CpuMhz[2]);
			m_Logger.Write (FromKernel, LogNotice, "Processor cooled down (%u.%u C): frequency restored to %u MHz", nTemp / 1000, (nTemp % 1000) / 100, m_Ctl.CpuMhz[2]);
		}
	}
}

// ap.log: one line per step of the boot into the fallback network (written immediately, so it stays even if the board hangs and restarts); it is read from the page at the next boot
void CKernel::ApLogPut (const char *pText)
{
	if (!m_bSdOk)
	{
		return;
	}
	FIL File;
	if (f_open (&File, DRIVE "/ap.log", FA_WRITE | FA_OPEN_APPEND) != FR_OK)
	{
		return;
	}
	CString Line;
	Line.Format ("%u %s\n", (unsigned) (CTimer::GetClockTicks () / 1000000), pText);
	UINT nWritten = 0;
	f_write (&File, (const char *) Line, Line.GetLength (), &nWritten);
	f_close (&File);
}

void CKernel::ApLogLoad (void)
{
	m_Ctl.ApLog[0] = 0;
	FIL File;
	if (f_open (&File, DRIVE "/ap.log", FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return;
	}
	UINT nRead = 0;
	f_read (&File, m_Ctl.ApLog, sizeof m_Ctl.ApLog - 1, &nRead);
	f_close (&File);
	m_Ctl.ApLog[nRead] = 0;
	{
		FIL Fd;
		if (f_open (&Fd, DRIVE "/apdrv.log", FA_READ | FA_OPEN_EXISTING) == FR_OK)
		{
			UINT nR = 0;
			f_lseek (&Fd, f_size (&Fd) > sizeof m_Ctl.ApDrv - 1 ? f_size (&Fd) - (sizeof m_Ctl.ApDrv - 1) : 0);
			f_read (&Fd, m_Ctl.ApDrv, sizeof m_Ctl.ApDrv - 1, &nR);
			f_close (&Fd);
			m_Ctl.ApDrv[nR] = 0;
			for (UINT i = 0; i < nR; i++)
			{
				if (m_Ctl.ApDrv[i] == '\n')
				{
					m_Ctl.ApDrv[i] = '|';
				}
			}
		}
		else
		{
			m_Ctl.ApDrv[0] = 0;
		}
	}
	for (unsigned i = 0; i < nRead; i++)
	{
		if (m_Ctl.ApLog[i] == '\n')
		{
			m_Ctl.ApLog[i] = '|';
		}
	}
}

void CKernel::ApReboot (void)
{
	ApFlagPut ();
	BootCounterWrite (0);		// it is an intended reboot: it must not count as an unstable boot
	m_Logger.Write (FromKernel, LogNotice, "Fallback network: reboot into the access point \"%s\"", m_Ctl.ApSsid);
	m_Scheduler.MsSleep (1500);
	reboot ();
}

// Wi-Fi, network stack, WPA supplicant (or, in the fallback network, access point)
boolean CKernel::NetworkInit (void)
{
	Feed ();
	m_bApMode = m_bSdOk && ApFlagTake ();
	if (m_bSdOk)
	{
		ApLogLoad ();
		if (m_bApMode)
		{
			f_unlink (DRIVE "/ap.log");
			m_Logger.SetNewTarget (&s_ApRing);		// the Wi-Fi driver's messages stay in memory and end up in apdrv.log
			ApLogPut ("boot into the fallback network");
		}
	}
	if (m_bSdOk)
	{
		ApCfgRead ();
	}
	if (!m_WLAN.Initialize ())
	{
		m_Logger.Write (FromKernel, LogError, "WLAN not initialized (firmware in %s?)", FIRMWARE_PATH);
		return FALSE;
	}
	m_nNetStage = 4;

	if (m_bApMode)
	{
		// no supplicant: the board creates the network; fixed address, our server does the DHCP
		Feed ();
		ApLogPut ("WLAN initialized");
		if (!m_WLAN.Control ("country IT"))
		{
			m_Logger.Write (FromKernel, LogWarning, "Fallback network: country not set");
		}
		ApLogPut ("country set");
		m_Net.GetConfig ()->SetIPAddress (s_ApIp);
		m_Net.GetConfig ()->SetNetMask (s_ApMask);
		ApLogPut ("creating the network");
		if (!m_WLAN.CreateWPA2Net (m_Ctl.ApSsid, (int) m_Ctl.ApChan, false, m_Ctl.ApPass))
		{
			ApLogPut ("network creation FAILED");
			m_Logger.Write (FromKernel, LogError, "Fallback network: the access point \"%s\" was not created", m_Ctl.ApSsid);
			return FALSE;
		}
		m_nNetStage = 5;
		ApLogPut ("network created, starting the stack");
		Feed ();
		if (!m_Net.Initialize (FALSE))
		{
			ApLogPut ("network stack FAILED");
			m_Logger.Write (FromKernel, LogError, "Network stack not initialized");
			return FALSE;
		}
		m_nNetStage = 6;
		m_Ctl.ApActive = TRUE;
		ApLogPut ("access point active");
		m_Logger.Write (FromKernel, LogNotice, "Fallback network: access point \"%s\" (WPA2) on channel %u, address 192.168.4.1", m_Ctl.ApSsid, (unsigned) m_Ctl.ApChan);
		return TRUE;
	}

	Feed ();
	if (!m_Net.Initialize (FALSE))
	{
		m_Logger.Write (FromKernel, LogError, "Network stack not initialized");
		return FALSE;
	}
	m_nNetStage = 5;

	Feed ();
	if (!m_WPASupplicant.Initialize ())
	{
		m_Logger.Write (FromKernel, LogError, "WPA supplicant not initialized (%s?)", CONFIG_FILE);
		return FALSE;
	}
	m_nNetStage = 6;

	return TRUE;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Pisound+Wi-Fi test on Circle. Compile time: " __DATE__ " " __TIME__);
	{
		// processor frequency (read-only): used to know whether it runs at the maximum
		CBcmPropertyTags Tags;
		unsigned nRate[3] = {0, 0, 0};
		const unsigned nTag[3] = {PROPTAG_GET_CLOCK_RATE, PROPTAG_GET_MIN_CLOCK_RATE, PROPTAG_GET_MAX_CLOCK_RATE};
		for (unsigned i = 0; i < 3; i++)
		{
			TPropertyTagClockRate TagRate;
			TagRate.nClockId = CLOCK_ID_ARM;
			if (Tags.GetTag (nTag[i], &TagRate, sizeof TagRate, 4))
			{
				nRate[i] = TagRate.nRate;
			}
		}
		TPropertyTagTemperature TagTemp;
		TagTemp.nTemperatureId = 0;
		unsigned nTemp = Tags.GetTag (PROPTAG_GET_TEMPERATURE, &TagTemp, sizeof TagTemp, 4) ? TagTemp.nValue : 0;
		for (unsigned i = 0; i < 3; i++)
		{
			m_Ctl.CpuMhz[i] = nRate[i] / 1000000;
		}
		m_Ctl.CpuTemp = nTemp / 100;
		m_Logger.Write (FromKernel, LogNotice, "Processor: %u MHz (minimum %u, maximum %u), temperature %u.%u C",
				nRate[0] / 1000000, nRate[1] / 1000000, nRate[2] / 1000000, nTemp / 1000, (nTemp % 1000) / 100);
	}
	{
		extern unsigned g_nDmaChenAtBoot, g_DmaDarAtBoot[8], g_DmaLlpAtBoot[8], g_nDmaResetOk;
		m_Logger.Write (FromKernel, LogNotice, "RP1 DMA found at startup: active channels (CHEN) %08X, reset %s",
				g_nDmaChenAtBoot, g_nDmaResetOk ? "succeeded" : "NOT succeeded");
		for (unsigned i = 0; i < 8; i++)
		{
			if (g_DmaDarAtBoot[i] != 0 || g_DmaLlpAtBoot[i] != 0)
			{
				m_Logger.Write (FromKernel, LogNotice, "  channel %u: destination %08X, next descriptor %08X", i, g_DmaDarAtBoot[i], g_DmaLlpAtBoot[i]);
			}
		}
	}
	{
		unsigned long long nSp;
		asm volatile ("mov %0, sp" : "=r" (nSp));
		m_Logger.Write (FromKernel, LogNotice, "Memory: CKernel object %u bytes, main stack in use at 0x%lX", (unsigned) sizeof (CKernel), nSp);
	}

	Stage (2);		// Initialize passed
	CodeCheckStart (&m_Logger);
	WatchInit (&m_Logger);

	// SD and boot counter: needed before the audio in order to choose safe mode
	m_bSdOk = StorageInit ();
	ThermalInit ();
	McInit ();
	CodeCheckRun (&m_Logger, "after the SD card");
	unsigned nPreviousFails = 0;
	if (m_bSdOk)
	{
		nPreviousFails = BootCounterRead ();
		BootCounterWrite (nPreviousFails + 1);
		m_bSafeMode = nPreviousFails + 1 >= BOOT_MAX_FAILS;
	}

	unsigned nSampleRate = m_bSafeMode ? SAFE_SAMPLE_RATE : SAMPLE_RATE;
	unsigned nChunkFrames = m_bSafeMode ? SAFE_CHUNK_FRAMES : CHUNK_FRAMES;
	m_nChunkFramesRun = nChunkFrames;
	if (m_bSafeMode)
	{
		m_Logger.Write (FromKernel, LogWarning, "SAFE MODE: %u consecutive unstable boots. Configuration: %u Hz, block %u frames "
				"(the requested configuration was %u Hz, block %u frames)",
				nPreviousFails, nSampleRate, nChunkFrames, (unsigned) SAMPLE_RATE, (unsigned) CHUNK_FRAMES);
	}
	else
	{
		m_Logger.Write (FromKernel, LogNotice, "Configuration: %u Hz, block %u frames, previous unstable boots %u",
				nSampleRate, nChunkFrames, nPreviousFails);
	}

	PisoundInit (nSampleRate);
	Stage (3);		// Pisound pins (OSR, reset) set
	CodeCheckRun (&m_Logger, "after PisoundInit");

	// the Pi is SLAVE: bit clock and frame clock come from the Pisound's ADC (no I2C controller)
	m_pSound = new CPisoundSound (&m_Interrupt, nSampleRate, nChunkFrames * 2);
	s_pDiSound = m_pSound;
	assert (m_pSound != 0);
	Stage (4);		// I2S device created

#if NO_AUDIO_START
	m_Logger.Write (FromKernel, LogWarning, "TEST: audio NOT started (NO_AUDIO_START)");
#else
	if (!m_pSound->Start ())
	{
		m_Logger.Write (FromKernel, LogError, "Unable to start the sound device");
		ErrorHalt (3);
	}
#endif
	m_Logger.Write (FromKernel, LogNotice, "Audio started: slave, TX+RX, %u Hz, block %u frames", nSampleRate, nChunkFrames);
	Feed ();		// from here the watchdog reboots the Pi if the system hangs
	Stage (5);		// Start() succeeded
#if !NO_AUDIO_START
	LliSnapshot (&m_Logger, m_pSound);
#endif
	CodeCheckRun (&m_Logger, "after Start audio");

#if TEST_MODE == 3
	ControlInit ();
	CodeCheckRun (&m_Logger, "after ControlInit");
	if (m_nChunkFramesRun <= NAM_MAX_BLOCK)
	{
		LoadAllModels ();
		CodeCheckRun (&m_Logger, "after the models loading");
	}
	else
	{
		m_Logger.Write (FromKernel, LogWarning, "NAM: block of %u frames too large (maximum %u), no model", m_nChunkFramesRun, (unsigned) NAM_MAX_BLOCK);
	}
#endif

	// from here the LED shows the network steps
	m_Timer.StartKernelTimer (MSEC2HZ (20), LedHandler, this, 0);

	m_bConcert = m_bSdOk && ConcertFlagTake ();
	if (m_bConcert)
	{
		// concert mode: no network (no Wi-Fi, page, syslog, OTA); MIDI remains. You leave it by powering off and on
		m_Logger.Write (FromKernel, LogNotice, "CONCERT MODE: boot without network");
		m_nNetStage = 8;
#if TEST_MODE == 3
		MidiStart ();
#endif
	}
	else if (m_bSdOk && NetworkInit ())
	{
		m_Logger.Write (FromKernel, LogNotice, "Network: waiting for the Wi-Fi association and the DHCP");
		unsigned nWait = 0;
		for (; !m_Net.IsRunning (); nWait++)
		{
			m_Scheduler.MsSleep (100);
			Feed ();
			if (nWait == 600)
			{
				m_Logger.Write (FromKernel, LogWarning, "Network: still without an address after 60 s");
			}
			if (!m_bApMode && m_bSdOk && nWait == STA_WAIT_SECONDS * 10)
			{
				m_Logger.Write (FromKernel, LogWarning, "Home network not found after %u s: switching to the fallback network", (unsigned) STA_WAIT_SECONDS);
				ApReboot ();
			}
		}
		m_nNetStage = 7;
		if (m_bApMode) { ApLogPut ("address assigned"); }

		CString IPString;
		m_Net.GetConfig ()->GetIPAddress ()->Format (&IPString);
		m_Logger.Write (FromKernel, LogNotice, "Network: address %s, ready after %u.%u s", (const char *) IPString, nWait / 10, nWait % 10);
		if (m_bApMode)
		{
			new CDhcpServer (&m_Net, s_ApIp, 10, 40);
			m_Logger.Write (FromKernel, LogNotice, "Fallback network: DHCP server active (addresses 192.168.4.10-40)");
		}

		CIPAddress ServerIP (SysLogServer);
		if (!m_bApMode)		// the log server is in the home network: it cannot be reached from the fallback network
		{
			new CSysLogDaemon (&m_Net, ServerIP, LOG_SERVER_PORT);
		}
		CodeCheckRun (&m_Logger, "after the syslog");
		m_nNetStage = 8;
		if (m_bApMode) { ApLogPut ("syslog started"); }
		m_Logger.Write (FromKernel, LogNotice, "Syslog active: now the logs reach the server");

		// updates over Wi-Fi: TFTP, only update.img/update.sig, only from OTA_ALLOWED_IP, with HMAC signature
		static const u8 OtaAllowed[] = OTA_ALLOWED_IP;
		m_pOta = new COtaServer (&m_Net, &m_FileSystem, OtaAllowed);
		CodeCheckRun (&m_Logger, "after the OTA server");
		if (m_bApMode) { ApLogPut ("OTA server started"); }
		m_Logger.Write (FromKernel, LogNotice, "OTA: TFTP server active (write-only for update.img and update.sig from %u.%u.%u.%u)",
				OtaAllowed[0], OtaAllowed[1], OtaAllowed[2], OtaAllowed[3]);

#if TEST_MODE == 3
		// control page: the code for the changes is born here, from the Wi-Fi/DHCP timings and the hardware counter
		CParams::Copy (m_Status.IP, IPString, sizeof m_Status.IP);
		{
			unsigned long long nCnt;
			asm volatile ("mrs %0, cntvct_el0" : "=r" (nCnt));
			u32 x = (u32) nCnt ^ (u32) (nCnt >> 32) * 2654435761u ^ (u32) CTimer::GetClockTicks () * 40503u;
			static const char Hex[] = "0123456789abcdef";
			for (unsigned i = 0; i < 12; i++)
			{
				x ^= x << 13;
				x ^= x >> 17;
				x ^= x << 5;
				x += (u32) nCnt >> (i * 3);
				m_Ctl.Token[i] = Hex[x & 15];
			}
			m_Ctl.Token[12] = 0;
		}
		new CControl (&m_Net, &m_Ctl);
		CodeCheckRun (&m_Logger, "after the web server");
		if (m_bApMode) { ApLogPut ("web server started"); }
		new CmDNSDaemon (&m_Net);		// answers at http://namberry5.local (mDNS / Bonjour)
		m_Logger.Write (FromKernel, LogNotice, "Control page: http://%s/ and http://namberry5.local/", (const char *) IPString);
		if (m_bApMode) { ApLogPut ("starting the MIDI task"); }
		MidiStart ();		// the MIDI task starts only now: the network and the page are ready
		if (m_bApMode) { ApLogPut ("MIDI task started"); }
#endif
	}

	// every 2 s: audio state, updates, watchdog
	boolean bStableMarked = FALSE;
	unsigned nDumpsDone = 0;
	unsigned nSeconds = 0;
	unsigned nLoopTick = 0;
	while (NO_AUDIO_START || m_pSound->IsActive ())
	{
		// wait of 100 ms in steps of 2 ms: a requested preset change (from MIDI or from the page) starts immediately instead of waiting for the end of the round (before, 100-200 ms of wait)
		{
			unsigned nWaited = 0;
			while (nWaited < 100)
			{
				m_Scheduler.MsSleep (1);
				nWaited += 1;
				if (m_Ctl.PrePending != 0 || m_nMidiPcWant >= 0)
				{
					MidiPcApply ();
					PreTick ();
				}
				GhostTick ();		// finished ghosts: they are freed immediately (the slots are needed for changes in quick succession)
			}
		}
		Feed ();
#if TEST_MODE == 3
		ControlTick ();
#endif
		if (++nLoopTick % 20 != 0)
		{
			continue;			// the rest of the loop runs every 2 s
		}
		nSeconds += 2;
#ifdef MC_PROBE
		if (nSeconds == 12 || nSeconds == 40 || nSeconds == 90)
		{
			char Buf[300];
			m_pMc->Measure (Buf, sizeof Buf);
			m_Logger.Write (FromKernel, LogNotice, "MULTICORE %s", Buf);
		}
#endif
		ThermalTick ();
		if (GraphMcFaults () != 0 && !m_bMcFaultSeen)
		{
			// a core did not respond in time: multi-core is off, the next plans use only core 0
			m_bMcFaultSeen = TRUE;
			m_Ctl.McActive = 1;
			CBoard::SetCores (1);
			m_pBoard->MarkDirty ();
			m_Logger.Write (FromKernel, LogError, "Multi-core: a core did not respond in time, going back to core 0");
		}
		if (m_bApMode && nSeconds <= 240 && nSeconds % 4 == 0)
		{
			ApDrvFlush ();
		}
		if (m_bApMode && nSeconds <= 240)
		{
			ApLogPut ("alive");
		}
		if (m_bApMode && nSeconds >= AP_IDLE_SECONDS && g_nDhcpLeases == 0)
		{
			ApLogPut ("no device connected: reboot");
			ApDrvFlush ();
			BootCounterWrite (0);
			m_Scheduler.MsSleep (500);
			reboot ();
		}

		if (!bStableMarked && nSeconds >= STABLE_SECONDS)
		{
			bStableMarked = TRUE;
			if (m_bSdOk)
			{
				BootCounterWrite (0);
			}
			m_Logger.Write (FromKernel, LogNotice, "Stable operation for %u s: boot counter reset", nSeconds);
		}

		if (m_pOta != 0 && m_pOta->Pending ())
		{
			m_pOta->ClearPending ();
			m_Logger.Write (FromKernel, LogNotice, "OTA: update.sig received, verifying the signature");

			int nResult = OtaApply (&m_Logger);
			if (nResult == 0)
			{
				m_Logger.Write (FromKernel, LogNotice, "OTA: kernel replaced (the old one is kernel_2712.bak). Reboot in 3 s");
				m_Scheduler.MsSleep (3000);
				reboot ();
			}
			else
			{
				m_Logger.Write (FromKernel, LogError, "OTA: refused (code %d), the current kernel was not touched", nResult);
			}
		}

		CCallbackStats Stats;
		m_pSound->GetCallbackStats (&Stats);
		const unsigned long long nPeriodTicks = (unsigned long long) nChunkFrames * Stats.Freq / nSampleRate;
		const unsigned nInAvg = Stats.InCount ? (unsigned) (Stats.InSumTicks * 1000ULL / (Stats.InCount * nPeriodTicks)) : 0;	// tenths of %
		const unsigned nInMax = (unsigned) (Stats.InMaxTicks * 1000ULL / nPeriodTicks);
		const unsigned nOutAvg = Stats.OutCount ? (unsigned) (Stats.OutSumTicks * 1000ULL / (Stats.OutCount * nPeriodTicks)) : 0;
		const unsigned nOutMax = (unsigned) (Stats.OutMaxTicks * 1000ULL / nPeriodTicks);
		const unsigned nPeriodUs = (unsigned) ((unsigned long long) nChunkFrames * 1000000ULL / nSampleRate);

		m_Logger.Write (FromKernel, LogNotice, "frames in %u, out %u, level class %u, input peak %u/8388607",
				(unsigned) m_pSound->GetInFrames (), (unsigned) m_pSound->GetOutFrames (),
				m_pSound->GetLevelClass (), m_nPeakLog);
		m_nPeakLog = 0;
		m_Logger.Write (FromKernel, LogNotice, "free heap memory: %u KB, total web requests %u, repaired code lines %u, repeated instructions after a wrong fetch %u",
				(unsigned) (CMemorySystem::Get ()->GetHeapFreeSpace (HEAP_ANY) / 1024), m_Status.nRequests, s_nCodeFixes, (unsigned) ExcRetryTotal);
		m_Status.nCpuAvg = nInAvg;
		m_Status.nCpuMax = nInMax;
		m_Status.nPeriodUs = nPeriodUs;
		m_Logger.Write (FromKernel, LogNotice,
				"CPU callback (period %u us): input average %u.%u%% max %u.%u%% | output average %u.%u%% max %u.%u%% | NAMs in the plan %u",
				nPeriodUs, nInAvg / 10, nInAvg % 10, nInMax / 10, nInMax % 10,
				nOutAvg / 10, nOutAvg % 10, nOutMax / 10, nOutMax % 10, m_pBoard->CountNam ());

#if TEST_MODE >= 2
		{
			unsigned nFresh, nStale, nEmpty;
			m_pSound->GetPassthroughStats (&nFresh, &nStale, &nEmpty);
			// expected latency of the passthrough: 2 blocks ("fresh" blocks) + converters (44 frames at 48 kHz, about 46 at 96, 21 at 192)
			unsigned nExpect = (nStale == 0 ? 2 : 3) * nChunkFrames;
			m_Logger.Write (FromKernel, LogNotice,
					"PASSTHROUGH: fresh blocks %u, old %u, empty %u -> %u blocks + converters, software %u frames (%u us)",
					nFresh, nStale, nEmpty, nStale == 0 ? 2 : 3, nExpect,
					(unsigned) ((unsigned long long) nExpect * 1000000ULL / nSampleRate));
		}
#endif

#if TEST_MODE == 1
		{
			unsigned nLast, nMin, nMax, nCount, nLost;
			m_pSound->GetLatency (&nLast, &nMin, &nMax, &nCount, &nLost);
			m_Logger.Write (FromKernel, LogNotice,
					"Loop LATENCY: last %u frames (%u us), min %u, max %u, measurements %u, lost %u, rate %u Hz, block %u frames, safe %u",
					nLast, (unsigned) ((unsigned long long) nLast * 1000000ULL / nSampleRate),
					nMin == 0xFFFFFFFF ? 0 : nMin, nMax, nCount, nLost, nSampleRate, nChunkFrames, m_bSafeMode ? 1 : 0);

			unsigned nPkLast, nPkMin, nPkMax, nPkCount;
			m_pSound->GetPeakLatency (&nPkLast, &nPkMin, &nPkMax, &nPkCount);
			m_Logger.Write (FromKernel, LogNotice,
					"Loop PEAK: last %u frames (%u us), min %u, max %u, measurements %u, rate %u Hz, block %u frames",
					nPkLast, (unsigned) ((unsigned long long) nPkLast * 1000000ULL / nSampleRate),
					nPkMin, nPkMax, nPkCount, nSampleRate, nChunkFrames);
		}
#endif
	}

	m_Logger.Write (FromKernel, LogError, "The sound device has stopped");
	return ShutdownHalt;
}
