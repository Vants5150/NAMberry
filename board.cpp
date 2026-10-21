//
// board.cpp - pedalboard document
//
#include "board.h"
#include "namtone.h"
#include "namcal.h"
#include "params.h"
#include "dsp_util.h"
#include "fx_defs.h"
#include <circle/util.h>

boolean BoardValidFileName (const char *p)
{
	unsigned n = strlen (p);
	if (n < 5 || n >= BD_FILE_LEN || strcmp (p + n - 4, ".adn") != 0)
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

boolean BoardValidIrName (const char *p)
{
	unsigned n = strlen (p);
	if (n < 5 || n >= BD_FILE_LEN || strcmp (p + n - 4, ".air") != 0)
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

static float Clamp (float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void Cpy (char *pDst, const char *pSrc, unsigned nSize)
{
	CParams::Copy (pDst, pSrc, nSize);
}

CBoard::CBoard (void)
:	m_nVersion (1),
	m_nStruct (1),
	m_nEdit (0),
	m_bDirty (TRUE),
	m_bSaveDirty (FALSE),
	m_nMode (2)
{
	Reset ();
}

void CBoard::Reset (void)
{
	memset (m_Block, 0, sizeof m_Block);
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		m_Block[i].A = m_Block[i].B = -1;
	}
	for (unsigned i = 0; i < BD_MAX_NAM; i++)
	{
		m_bSlot[i] = FALSE;
	}
	m_nLink = 0;
	m_Midi = CMidiMap ();
	// Grid: Input L and Output L on row 2, Input R and Output R on row 3 (the two middle rows, on the left and on the right)
	m_IoRow[0] = m_IoRow[2] = 2;
	m_IoRow[1] = m_IoRow[3] = 3;
	m_IoCol[0] = m_IoCol[1] = 0;
	m_IoCol[2] = m_IoCol[3] = BD_GRID_COLS - 1;
	Touch (TRUE);
}

void CBoard::Touch (boolean bStruct)
{
	m_nVersion++;
	m_nEdit++;
	if (bStruct)
	{
		m_nStruct++;
	}
	m_bDirty = TRUE;
	m_bSaveDirty = TRUE;
}

unsigned CBoard::CountNam (void) const
{
	unsigned n = 0;
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (m_Block[i].Used && m_Block[i].Type == NodeNam)
		{
			n++;
		}
	}
	return n;
}

// cost in tenths of a % of the processor per block type, from measurements on the board (4 frames at 48 kHz): NAM 21.5% at 1500 MHz (80 models tried on 2026-10-14: all 21.0-21.1% above the idle minimum, same architecture; at 2400 MHz, with CPU_BOOST, it would be 13.5%), reverb 9%, delay 5%, mono delay 3%,
// springs 4%, chorus 2%, compressor 2%, harmonizer 5% (peaks up to 55%), equalizer 0.5%, Cab IR 3%; idle 0.7%
static unsigned s_nCpuMhz = 1500;
static float s_fCalMax = 1.5f;		// dBu corresponding to 0 dBFS at the input (Pisound at Gain 0)
static float s_fInTrimDb = 0.0f;		// input trim of the guitar profile (dB)
static float s_fCalOfs = 6.0f;		// calibration offset (dB)
static unsigned s_nCores = 1;
static unsigned McSchedule (TPlan *pPlan, unsigned nCores);

// which blocks can run on a worker core: NAM and effects, which keep their state only in their own instance.
// ADELphase and ADELSendSweep stay on core 0: they exchange data through a shared global structure (adelphase_dsp.hpp, bus()).
static boolean McOffloadable (u8 nKind)
{
	if (nKind == NodeNam)
	{
		return TRUE;
	}
	return nKind >= FxFirst && nKind <= FxLast && nKind != FxAdelPhase && nKind != FxAdelSweep;
}

void CBoard::SetCores (unsigned nCores)
{
	s_nCores = nCores < 1 ? 1 : (nCores > GR_MAX_CORES ? GR_MAX_CORES : nCores);
}

unsigned CBoard::Cores (void)
{
	return s_nCores;
}

void CBoard::SetInTrim (float fDb)
{
	s_fInTrimDb = fDb < -24.0f ? -24.0f : (fDb > 12.0f ? 12.0f : fDb);
}

void CBoard::SetCal (float fMaxInDbu, float fOffsetDb)
{
	s_fCalMax = fMaxInDbu;
	s_fCalOfs = fOffsetDb;
}

void CBoard::SetCpuMhz (unsigned nMhz)
{
	s_nCpuMhz = nMhz < 600 ? 1500 : (nMhz > 3000 ? 3000 : nMhz);
}

static unsigned CostAt1500 (u8 nType);

unsigned CBoard::NodeCost10 (u8 nType)
{
	return (CostAt1500 (nType) * 1500 + s_nCpuMhz - 1) / s_nCpuMhz;		// rounded up
}

static unsigned CostAt1500 (u8 nType)
{
	switch (nType)
	{
	case NodeNam:		return 215;
	case NodeVolume:
	case NodePan:		return 3;
	case FxPhaser:
	case FxAdelPhase:
	case FxCompressor:
	case FxAdelComp:
	case FxAdelChorus:	return 20;
	case FxReverb:
	case FxAdelVerb:
	case FxAdelVerbM:	return 90;
	case FxDelay:
	case FxAdelDelay:	return 50;
	case FxAdelDelayM:
	case FxCabIR:		return 30;
	case FxAdelSpring:	return 40;
	case FxAdelShifter:	return 50;
	case FxAdelEqual:	return 5;
	case FxAdelSweep:	return 10;
	// ToTest effects (28..77): measured on the board on 2026-10-14 (extra CPU on a 4-frame block at 48 kHz), estimate = 2 x measurement with a minimum of 0.5%
	case FxTsDrive:		return 8;
	case FxMuff:		return 190;		// 7.7 us per 4-sample block (one channel) on the Pi 5 (host test), same scale as the others (tsdrive 0.32 us = 8); not measured on the board
	case FxFuzzFace:	return 165;		// 6.6 us per 4-sample block (one channel) on the Pi 5 (host test), same scale as the others; not measured on the board
	case FxTremolo:		return 5;
	case FxHarmTrem:	return 5;
	case FxAutoWah:		return 6;
	case FxOctaver:		return 56;		// Octaver Real (fx_octaver.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxGate:		return 6;
	case FxUniVibe:		return 23;		// Uni-Vibe Real (fx_univibe.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxFlanger:		return 106;		// MXR 117 Flanger Real (fx_flanger117.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxRotary:		return 8;
	case FxTapeEcho:	return 10;
	case FxBooster:		return 6;
	case FxToneStack:	return 8;		// Tone Stack Real (fx_tonestack.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxSwell:		return 5;
	case FxWidener:		return 5;
	case FxVibrato:		return 5;
	case FxCentaur:		return 460;		// Klon Real (fx_klon.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxBluesDrv:		return 8;
	case FxDs1:		return 500;		// DS-1 Real (fx_ds1.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxOcd:		return 185;		// OCD Real (fx_ocd.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxGuv:		return 226;		// Guv'nor Real (fx_guvnor.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxRange:		return 62;		// Rangemaster Real (fx_rangemaster.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxFz1:		return 8;
	case FxBender:		return 265;		// Tone Bender Real (fx_bender.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxFactory:		return 290;		// Fuzz Factory Real (fx_factory.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxPhase90:		return 70;		// Phase 90 Real (fx_phase90.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxStone:		return 12;
	case FxCe1:		return 55;		// Chorus CE-1 Real (fx_ce1.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxDimD:		return 10;		// 0.43 us per 4-sample stereo block on the Pi 5 (host test), same scale as the others (tsdrive 0.32 us = 8); not measured on the board
	case FxAutoPan:		return 10;
	case FxSlicer:		return 8;
	case FxCry:		return 81;		// Wah Real (fx_wah.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxWoman:		return 8;
	case FxGeq:		return 8;
	case FxDynaComp:		return 640;		// Dyna Comp Real (fx_dynacomp.cpp): cost estimated from the host time (scale: tsdrive 0.32 us per 4-sample block = 8); not measured on the board
	case FxLimiter:		return 10;
	case FxAnalog:		return 12;
	case FxDuck:		return 10;
	case FxSustain:		return 10;
	case FxSelector:		return 3;
	case FxTubeScreamer:	return 20;		// 0.82 us per block on the Pi 5 (host test), on the same scale as the others (tsdrive 0.32 us = 8, univibe 0.67 us = 16); not measured per core on the board
	case FxRatTS:		return 45;		// 1.77 us per 4-sample block with Quality High (0.49 us with Normal) on the Pi 5 (host test), same scale as the others (tsdrive 0.32 us = 8); not measured on the board
	case FxAd9:		return 33;		// Analog Delay AD9 Real (fx_ad9.cpp): 0.33 us per sample on the host (4 linear circuits and the BBD), same scale as the others; not measured on the board
	case FxMicroFlanger:	return 88;		// Flanger Micro Real (fx_mflanger.cpp): 0.88 us per sample on the host (three diodes in the input netlist plus two linear ones), same scale as the others; not measured on the board
	case FxCe2:		return 27;		// Chorus CE-2 Real (fx_ce2.cpp): 0.27 us per sample and channel on the host (two linear circuits plus the delay line), same scale as the others; not measured on the board
	case FxOpticalLA:	return 12;		// 0.2 us per 4-sample mono block (0.4 stereo) on the Pi 5 (host test), same scale as the others (tsdrive 0.32 us = 8); not measured on the board
	default:		return 10;
	}
}

// ---- MIDI: block classes (fixed CCs like the StompStation PRO), first block of a class, normalized parameters ----
unsigned CBoard::MidiClassOfType (u8 nType)
{
	switch (nType)
	{
	case NodeNam:		return McAmp;
	case FxCabIR:		return McIr;
	case FxTsDrive:
	case FxTubeScreamer:
	case FxRatTS:
	case FxMuff:
	case FxFuzzFace:
	case FxCentaur:
	case FxBluesDrv:
	case FxDs1:
	case FxOcd:
	case FxGuv:
	case FxRange:
	case FxFz1:
	case FxBender:
	case FxFactory:		return McDrive;
	case FxAdelVerb:
	case FxAdelVerbM:
	case FxReverb:
	case FxAdelSpring:	return McReverb;
	case FxTremolo:
	case FxSlicer:
	case FxHarmTrem:	return McTremolo;
	case FxAdelComp:
	case FxDynaComp:
	case FxLimiter:
	case FxSustain:
	case FxOpticalLA:
	case FxCompressor:	return McCompressor;
	case FxGate:		return McGate;
	case FxAdelDelay:
	case FxAdelDelayM:
	case FxDelay:
	case FxAnalog:
	case FxDuck:
	case FxAd9:
	case FxTapeEcho:	return McDelay;
	case FxBooster:		return McBoost;
	case FxCry:
	case FxAutoWah:		return McWah;
	case FxAdelEqual:
	case FxGeq:
	case FxWoman:
	case FxToneStack:	return McEq;
	case FxAdelShifter:
	case FxOctaver:		return McPitch;
	case FxAdelChorus:
	case FxPhaser:
	case FxUniVibe:
	case FxFlanger:
	case FxVibrato:
	case FxRotary:
	case FxPhase90:
	case FxStone:
	case FxCe1:
	case FxDimD:
	case FxCe2:
	case FxMicroFlanger:
	case FxAutoPan:		return McModPre;
	default:		return 0;
	}
}

int CBoard::MidiFindClass (unsigned nClass) const
{
	if (m_nMode != 2 || nClass < 1 || nClass > 16)
	{
		return -1;
	}
	int nNamCol = 1000;
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (m_Block[i].Used && m_Block[i].Type == NodeNam && m_Block[i].B < nNamCol)
		{
			nNamCol = m_Block[i].B;
		}
	}
	int nBest = -1, nBestKey = 1 << 30;
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (!m_Block[i].Used)
		{
			continue;
		}
		unsigned c = MidiClassOfType (m_Block[i].Type);
		if (c == McModPre && m_Block[i].B > nNamCol)
		{
			c = McModPost;
		}
		const int nKey = m_Block[i].B * 64 + m_Block[i].A;		// column, then row
		if (c == nClass && nKey < nBestKey)
		{
			nBestKey = nKey;
			nBest = (int) i;
		}
	}
	return nBest;
}

unsigned CBoard::MidiClassMask (void) const
{
	unsigned nMask = 0;
	for (unsigned c = 1; c <= 16; c++)
	{
		const int n = MidiFindClass (c);
		if (n >= 0 && m_Block[n].On)
		{
			nMask |= 1u << (c - 1);
		}
	}
	return nMask;
}

// natural logarithm without libm (for the parameters on a logarithmic scale)
static float LnF (float x)
{
	int e = 0;
	while (x >= 2.0f) { x *= 0.5f; e++; }
	while (x < 1.0f) { x *= 2.0f; e--; }
	const float y = (x - 1.0f) / (x + 1.0f), y2 = y * y;
	return 0.69314718f * (float) e + 2.0f * y * (1.0f + y2 * (1.0f / 3.0f + y2 * (0.2f + y2 * (1.0f / 7.0f))));
}

const char *CBoard::SetParamNorm (int nId, unsigned nParam, float f01)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return "no such block";
	}
	const float f = f01 < 0.0f ? 0.0f : (f01 > 1.0f ? 1.0f : f01);
	const TBdBlock &Bl = m_Block[nId];
	if (Bl.Type == NodeNam)
	{
		if (nParam > 4)
		{
			return "unknown parameter";
		}
		if (nParam >= 2)
		{
			return SetValue (nId, nParam == 2 ? "bass" : (nParam == 3 ? "mid" : "treb"), -NT_MAX_DB + 2.0f * NT_MAX_DB * f);
		}
		return SetValue (nId, nParam == 0 ? "in" : "out", -24.0f + 36.0f * f);
	}
	if (Bl.Type == NodeVolume)
	{
		return nParam != 0 ? "unknown parameter" : SetValue (nId, "vol", -40.0f + 52.0f * f);
	}
	if (Bl.Type == NodePan)
	{
		return nParam != 0 ? "unknown parameter" : SetValue (nId, "pan", -100.0f + 200.0f * f);
	}
	const TFxDef *pDef = FxDefByType (Bl.Type);
	if (pDef == 0 || nParam >= pDef->nParams)
	{
		return "unknown parameter";
	}
	const TFxParam &Pm = pDef->pParams[nParam];
	float v;
	if (Pm.Log && Pm.Min > 0.0f)
	{
		v = Pm.Min * DbToLin (8.685889638f * LnF (Pm.Max / Pm.Min) * f * 1.0f);		// Min * (Max/Min)^f
	}
	else
	{
		v = Pm.Min + (Pm.Max - Pm.Min) * f;
	}
	return SetValue (nId, Pm.Key, v);
}

unsigned CBoard::EstimateLoad10 (void) const
{
	if (s_nCores > 1)
	{
		// several cores: duration of the longest path among the cores (the same scheduling the plan uses), plus the fixed cost
		static TPlan *s_pPlan = 0;		// on the heap the first time (13 KB less in the kernel image)
		if (s_pPlan == 0)
		{
			s_pPlan = new TPlan;
		}
		TPlan &Tmp = *s_pPlan;
		memset (&Tmp, 0, sizeof Tmp);
		for (unsigned i = 0; i < PL_MAX_NODES; i++)
		{
			if (m_Block[i].Used)
			{
				Tmp.Node[i].Kind = m_Block[i].Type;
				Tmp.Node[i].On = m_Block[i].On ? 1 : 0;
			}
		}
		NetBuild (&Tmp);
		return 7 + McSchedule (&Tmp, s_nCores);
	}
	unsigned n = 7;
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (m_Block[i].Used && m_Block[i].On)
		{
			n += NodeCost10 (m_Block[i].Type);
		}
	}
	return n;
}

int CBoard::NewBlock (void)
{
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (!m_Block[i].Used)
		{
			memset (&m_Block[i], 0, sizeof m_Block[i]);
			m_Block[i].Used = TRUE;
			m_Block[i].On = TRUE;
			m_Block[i].A = m_Block[i].B = -1;
			m_Block[i].Slot = -1;
			m_Block[i].BaseIn = m_Block[i].BaseOut = 1.0f;
			m_Block[i].HasModelIn = FALSE;
			m_Block[i].ModelInDbu = 0.0f;
			return (int) i;
		}
	}
	return -1;
}

void CBoard::FreeBlock (int nId)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return;
	}
	TBdBlock &Bl = m_Block[nId];
	if (Bl.Type == NodeNam && Bl.Slot >= 0 && Bl.Slot < (int) BD_MAX_NAM)
	{
		m_bSlot[Bl.Slot] = FALSE;
	}
	Bl.Used = FALSE;
	m_Midi.ForgetBlock (nId);
	Bl.Type = 0;
}

const char *CBoard::SetValue (int nId, const char *pKey, float fValue)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return "no such block";
	}
	TBdBlock &Bl = m_Block[nId];

	if (strcmp (pKey, "on") == 0)
	{
		if (m_nMode == 2 && fValue > 0.5f && !Bl.On && EstimateLoad10 () + NodeCost10 (Bl.Type) > BD_LOAD_LIMIT10)
		{
			return BD_LOAD_MSG;
		}
		Bl.On = fValue > 0.5f;
	}
	else if (Bl.Type == NodeNam && strcmp (pKey, "in") == 0)
	{
		Bl.V[0] = Clamp (fValue, -24.0f, 12.0f);
	}
	else if (Bl.Type == NodeNam && strcmp (pKey, "out") == 0)
	{
		Bl.V[1] = Clamp (fValue, -24.0f, 12.0f);
	}
	else if (Bl.Type == NodeNam && strcmp (pKey, "bass") == 0)
	{
		Bl.V[2] = Clamp (fValue, -NT_MAX_DB, NT_MAX_DB);
	}
	else if (Bl.Type == NodeNam && strcmp (pKey, "mid") == 0)
	{
		Bl.V[3] = Clamp (fValue, -NT_MAX_DB, NT_MAX_DB);
	}
	else if (Bl.Type == NodeNam && strcmp (pKey, "treb") == 0)
	{
		Bl.V[4] = Clamp (fValue, -NT_MAX_DB, NT_MAX_DB);
	}
	else if (Bl.Type == NodeNam && strcmp (pKey, "noofs") == 0)
	{
		Bl.V[5] = fValue > 0.5f ? 1.0f : 0.0f;		// 1 = this block does NOT use the calibration offset (0 = it uses it, as before)
	}
	else if (Bl.Type == NodeVolume && strcmp (pKey, "vol") == 0)
	{
		Bl.V[0] = Clamp (fValue, -40.0f, 12.0f);
	}
	else if (Bl.Type == NodePan && strcmp (pKey, "pan") == 0)
	{
		Bl.V[0] = Clamp (fValue, -100.0f, 100.0f);
	}
	else
	{
		const TFxDef *pDef = FxDefByType (Bl.Type);
		unsigned k = 0;
		while (pDef != 0 && k < pDef->nParams && strcmp (pKey, pDef->pParams[k].Key) != 0)
		{
			k++;
		}
		if (pDef == 0 || k >= pDef->nParams)
		{
			return "unknown parameter";
		}
		const TFxParam &Pm = pDef->pParams[k];
		if (Pm.Kind == FxKindOutput)
		{
			return "read-only parameter";
		}
		float v = Clamp (fValue, Pm.Min, Pm.Max);
		if (Pm.Kind == FxKindChoice || Pm.Kind == FxKindToggle)
		{
			v = (float) (int) (v + 0.5f);
		}
		else if (Pm.Step >= 1.0f)
		{
			v = Pm.Min + (float) (int) ((v - Pm.Min) / Pm.Step + 0.5f) * Pm.Step;
		}
		Bl.V[k] = v;
	}
	Touch (FALSE);
	return 0;
}

static boolean ValidPresetName (const char *p)
{
	unsigned n = 0;
	for (; *p; p++, n++)
	{
		const unsigned char c = (unsigned char) *p;
		if (c < 0x20 || c == 0x7F || c == '"' || c == '\\' || n >= BD_PRESET_LEN - 1)
		{
			return FALSE;
		}
	}
	return TRUE;
}

const char *CBoard::SetPreset (int nId, const char *pName)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return "no such block";
	}
	if (!ValidPresetName (pName))
	{
		return "invalid name";
	}
	Cpy (m_Block[nId].Preset, pName, sizeof m_Block[nId].Preset);
	Touch (FALSE);
	return 0;
}

const char *CBoard::SetFile (int nId, const char *pFile)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used || (m_Block[nId].Type != NodeNam && m_Block[nId].Type != FxCabIR))
	{
		return "not a NAM or Cab IR block";
	}
	if (m_Block[nId].Type == FxCabIR ? !BoardValidIrName (pFile) : !BoardValidFileName (pFile))
	{
		return "invalid file name";
	}
	Cpy (m_Block[nId].File, pFile, sizeof m_Block[nId].File);
	Touch (FALSE);
	return 0;
}

void CBoard::Compile (TPlan *pPlan, adelnam::Instance *const *ppInst, float fMaster, boolean bBypass, CFx *const *ppFx) const
{
	memset (pPlan, 0, sizeof *pPlan);
	for (unsigned i = 0; i < PL_MAX_NODES; i++)
	{
		TPNode &N = pPlan->Node[i];
		const TBdBlock &Bl = m_Block[i];
		if (!Bl.Used)
		{
			continue;
		}
		N.Kind = Bl.Type;
		N.On = Bl.On ? 1 : 0;
if (Bl.Type == NodeNam)
		{
			N.pInst = (Bl.Loaded && Bl.Slot >= 0) ? ppInst[Bl.Slot] : 0;
			// calibration: maximum input level - send level of the model + offset (the block can exclude it with V5); without a send level there is no compensation
			N.In = (Bl.HasModelIn ? DbToLin (NamInputDb (s_fCalMax, Bl.ModelInDbu, true, Bl.V[5] > 0.5f ? 0.0f : s_fCalOfs)) : 1.0f) * DbToLin (Bl.V[0]);
			N.Out = Bl.BaseOut * DbToLin (Bl.V[1]);
			N.Sel = NamToneSetup (N.P, Bl.V[2], Bl.V[3], Bl.V[4], 48000.0f);		// Bass/Mid/Treble: coefficients in the node's P[], mask in Sel
		}
		else if (Bl.Type >= FxFirst && Bl.Type <= FxLast)
		{
			N.pFx = ppFx != 0 ? ppFx[i] : 0;
			for (unsigned k = 0; k < FX_MAX_P; k++)
			{
				N.P[k] = Bl.V[k];
			}
		}
		else if (Bl.Type == NodeVolume)
		{
			N.Gain = DbToLin (Bl.V[0]);
		}
		else if (Bl.Type == NodePan)
		{
			const float fTheta = (Bl.V[0] * 0.01f + 1.0f) * 0.78539816f;		// from 0 (fully left) to pi/2 (fully right)
			N.In = DspCos (fTheta);
			N.Out = DspSin (fTheta);
		}
	}
	NetBuild (pPlan);
	pPlan->Master = fMaster;
	pPlan->InTrim = DbToLin (s_fInTrimDb) - 1.0f;
	pPlan->Bypass = bBypass ? 1 : 0;
}

// ---- network modes (ADELMatrix, ADELGrid) ----

const char *CBoard::TypeFromName (const char *pType, u8 *pType8)
{
	if (strcmp (pType, "nam") == 0)
	{
		*pType8 = NodeNam;
		return 0;
	}
	if (strcmp (pType, "vol") == 0 || strcmp (pType, "rout") == 0)
	{
		*pType8 = NodeVolume;		// "rout" (Routing) is a Volume with V1 = 1: routing only, 0 dB by default
		return 0;
	}
	if (strcmp (pType, "pan") == 0)
	{
		*pType8 = NodePan;
		return 0;
	}
	for (unsigned i = 0; i < FX_DEF_COUNT; i++)
	{
		if (strcmp (pType, g_FxDefs[i].Name) == 0)
		{
			*pType8 = g_FxDefs[i].Type;
			return 0;
		}
	}
	return "unknown block type";
}

// creates a block with the starting values (NAM slot included); returns the id, or -1 and the reason
int CBoard::NetNewBlock (const char *pType, const char *pDefaultFile, const char **ppErr)
{
	u8 nType;
	const char *pE = TypeFromName (pType, &nType);
	if (pE != 0)
	{
		*ppErr = pE;
		return -1;
	}
	int nSlot = -1;
	if (nType == NodeNam)
	{
		for (unsigned i = 0; i < BD_MAX_NAM; i++)
		{
			if (!m_bSlot[i])
			{
				nSlot = (int) i;
				break;
			}
		}
		if (nSlot < 0)
		{
			*ppErr = "too many NAMs: the processor load would be excessive";
			return -1;
		}
	}
	const int nId = NewBlock ();
	if (nId < 0)
	{
		*ppErr = "too many blocks";
		return -1;
	}
	TBdBlock &Bl = m_Block[nId];
	Bl.Type = nType;
	if (strcmp (pType, "rout") == 0)
	{
		Bl.V[1] = 1.0f;
	}
	if (nType == NodeNam)
	{
		Bl.Slot = nSlot;
		m_bSlot[nSlot] = TRUE;
		Cpy (Bl.File, pDefaultFile, sizeof Bl.File);
		Cpy (Bl.Status, "loading", sizeof Bl.Status);
	}
	else if (const TFxDef *pDef = FxDefByType (nType))
	{
		for (unsigned k = 0; k < pDef->nParams; k++)
		{
			Bl.V[k] = pDef->pParams[k].Def;
		}
		if (nType == FxCabIR && BoardValidIrName (pDefaultFile))
		{
			Cpy (Bl.File, pDefaultFile, sizeof Bl.File);
		}
	}
	return nId;
}

int CBoard::NetCell (int nRow, int nCol) const
{
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (m_Block[i].Used && m_Block[i].A == nRow && m_Block[i].B == nCol)
		{
			return (int) i;
		}
	}
	return -1;
}

boolean CBoard::NetCellFree (int nRow, int nCol) const
{
	if (NetCell (nRow, nCol) >= 0)
	{
		return FALSE;
	}
	for (unsigned w = 0; w < 4; w++)
	{
		if (m_IoRow[w] == nRow && m_IoCol[w] == nCol)
		{
			return FALSE;
		}
	}
	return TRUE;
}

int CBoard::NetColE (int nId, int nPort) const
{
	if (nId == BD_NET_IN)
	{
		return m_IoCol[nPort == 2 ? 1 : 0];
	}
	if (nId == BD_NET_OUT)
	{
		return m_IoCol[nPort == 2 ? 3 : 2];
	}
	return m_Block[nId].B;
}

// Grid rule: a link goes to a column on the right or stays in the same one (vertical); inputs and outputs also sit in the grid
boolean CBoard::NetForward (int nFrom, int nPf, int nTo, int nPt) const
{
	return NetColE (nTo, nPt) >= NetColE (nFrom, nPf);		// same column allowed (vertical link); loops are prevented by NetReaches
}

boolean CBoard::NetLinkOnIo (const TBdLink &K, int nWhich) const
{
	if (nWhich < 2)
	{
		return K.From == BD_NET_IN && K.PF == nWhich + 1;
	}
	return K.To == BD_NET_OUT && K.PT == nWhich - 1;
}

// is there a path of links from nFrom to nTarget (blocks)?
boolean CBoard::NetReaches (int nFrom, int nTarget) const
{
	boolean Seen[BD_MAX_BLOCKS];
	int Stack[BD_MAX_BLOCKS + 2];
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		Seen[i] = FALSE;
	}
	unsigned nTop = 0;
	Stack[nTop++] = nFrom;
	Seen[nFrom] = TRUE;
	while (nTop > 0)
	{
		const int n = Stack[--nTop];
		if (n == nTarget)
		{
			return TRUE;
		}
		for (unsigned k = 0; k < m_nLink; k++)
		{
			const int t = m_Link[k].To;
			if (m_Link[k].From == n && t >= 0 && t < (int) BD_MAX_BLOCKS && !Seen[t] && nTop < BD_MAX_BLOCKS)
			{
				Seen[t] = TRUE;
				Stack[nTop++] = t;
			}
		}
	}
	return FALSE;
}

// sequence number of the Matrix blocks: compacts the positions to 0..n-1 keeping the order (the id decides ties)
static void CompactOrder (TBdBlock *pBlocks)
{
	int Rank[BD_MAX_BLOCKS];
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		Rank[i] = -1;
		if (!pBlocks[i].Used)
		{
			continue;
		}
		int r = 0;
		for (unsigned j = 0; j < BD_MAX_BLOCKS; j++)
		{
			if (j != i && pBlocks[j].Used && (pBlocks[j].B < pBlocks[i].B || (pBlocks[j].B == pBlocks[i].B && j < i)))
			{
				r++;
			}
		}
		Rank[i] = r;
	}
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (Rank[i] >= 0)
		{
			pBlocks[i].B = Rank[i];
		}
	}
}

const char *CBoard::NetAdd (const char *pType, int nRow, int nCol, const char *pDefaultFile, int *pNewId)
{
	if (m_nMode == 2 && (nRow < 0 || nRow >= (int) BD_GRID_ROWS || nCol < 0 || nCol >= (int) BD_GRID_COLS))
	{
		return "no such cell";
	}
	if (m_nMode == 2 && !NetCellFree (nRow, nCol))
	{
		return "the cell is occupied";
	}
	if (m_nMode == 2)
	{
		u8 nT = 0;
		if (TypeFromName (pType, &nT) == 0 && EstimateLoad10 () + NodeCost10 (nT) > BD_LOAD_LIMIT10)
		{
			return BD_LOAD_MSG;
		}
	}
	const char *pErr = 0;
	const int nId = NetNewBlock (pType, pDefaultFile, &pErr);
	if (nId < 0)
	{
		return pErr;
	}
	TBdBlock &Bl = m_Block[nId];
	if (m_nMode == 2)
	{
		Bl.A = nRow;
		Bl.B = nCol;
	}
	else
	{
		unsigned nUsed = 0;
		for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
		{
			if (m_Block[i].Used && (int) i != nId)
			{
				nUsed++;
			}
		}
		Bl.A = 0;
		Bl.B = nCol >= 0 && nCol <= (int) nUsed ? nCol : (int) nUsed;
		for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
		{
			if (m_Block[i].Used && (int) i != nId && m_Block[i].B >= Bl.B)
			{
				m_Block[i].B++;
			}
		}
		CompactOrder (m_Block);
	}
	if (pNewId != 0)
	{
		*pNewId = nId;
	}
	Touch (TRUE);
	return 0;
}

const char *CBoard::NetRetype (int nId, const char *pType, const char *pDefaultFile)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return "no such block";
	}
	u8 nType;
	const char *pE = TypeFromName (pType, &nType);
	if (pE != 0)
	{
		return pE;
	}
	TBdBlock &Bl = m_Block[nId];
	const boolean bRout = strcmp (pType, "rout") == 0;
	if (Bl.Type == nType && (nType != NodeVolume || (Bl.V[1] > 0.5f) == bRout))
	{
		return 0;
	}
	if (m_nMode == 2 && Bl.On && NodeCost10 (nType) > NodeCost10 (Bl.Type)
	    && EstimateLoad10 () - NodeCost10 (Bl.Type) + NodeCost10 (nType) > BD_LOAD_LIMIT10)
	{
		return BD_LOAD_MSG;
	}
	// NAM slot: the current block's one (if it is a NAM) is freed, a new one is assigned if needed
	int nSlot = -1;
	if (nType == NodeNam)
	{
		for (unsigned i = 0; i < BD_MAX_NAM && nSlot < 0; i++)
		{
			if (!m_bSlot[i] || (Bl.Type == NodeNam && (int) i == Bl.Slot))
			{
				nSlot = (int) i;
			}
		}
		if (nSlot < 0)
		{
			return "too many NAMs: the processor load would be excessive";
		}
	}
	if (Bl.Type == NodeNam && Bl.Slot >= 0 && Bl.Slot < (int) BD_MAX_NAM)
	{
		m_bSlot[Bl.Slot] = FALSE;
	}
	const int nA = Bl.A, nB = Bl.B;
	const boolean bOn = Bl.On;
	memset (&Bl, 0, sizeof Bl);
	Bl.Used = TRUE;
	Bl.On = bOn;
	Bl.A = nA;
	Bl.B = nB;
	Bl.Slot = -1;
	Bl.BaseIn = Bl.BaseOut = 1.0f;
	Bl.HasModelIn = FALSE;
	Bl.ModelInDbu = 0.0f;
	Bl.Type = nType;
	if (bRout)
	{
		Bl.V[1] = 1.0f;
	}
	if (nType == NodeNam)
	{
		Bl.Slot = nSlot;
		m_bSlot[nSlot] = TRUE;
		Cpy (Bl.File, pDefaultFile, sizeof Bl.File);
		Cpy (Bl.Status, "loading", sizeof Bl.Status);
	}
	else if (const TFxDef *pDef = FxDefByType (nType))
	{
		for (unsigned k = 0; k < pDef->nParams; k++)
		{
			Bl.V[k] = pDef->pParams[k].Def;
		}
		if (nType == FxCabIR && BoardValidIrName (pDefaultFile))
		{
			Cpy (Bl.File, pDefaultFile, sizeof Bl.File);
		}
	}
	Touch (TRUE);
	return 0;
}

const char *CBoard::NetRemove (int nId)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return "no such block";
	}
	unsigned w = 0;
	for (unsigned k = 0; k < m_nLink; k++)
	{
		if (m_Link[k].From != nId && m_Link[k].To != nId)
		{
			m_Link[w++] = m_Link[k];
		}
	}
	m_nLink = w;
	FreeBlock (nId);
	if (m_nMode == 1)
	{
		CompactOrder (m_Block);
	}
	Touch (TRUE);
	return 0;
}

const char *CBoard::NetMove (int nId, int nRow, int nCol)
{
	if (nId < 0 || nId >= (int) BD_MAX_BLOCKS || !m_Block[nId].Used)
	{
		return "no such block";
	}
	TBdBlock &Bl = m_Block[nId];
	if (m_nMode == 2)
	{
		if (nRow < 0 || nRow >= (int) BD_GRID_ROWS || nCol < 0 || nCol >= (int) BD_GRID_COLS)
		{
			return "no such cell";
		}
		if (NetCell (nRow, nCol) != nId && !NetCellFree (nRow, nCol))
		{
			return "the cell is occupied";
		}
		const int nOldA = Bl.A, nOldB = Bl.B;
		Bl.A = nRow;
		Bl.B = nCol;
		for (unsigned k = 0; k < m_nLink; k++)
		{
			if ((m_Link[k].From == nId || m_Link[k].To == nId) && !NetForward (m_Link[k].From, m_Link[k].PF, m_Link[k].To, m_Link[k].PT))
			{
				Bl.A = nOldA;
				Bl.B = nOldB;
				return "a link must go to the right or vertically: move it or remove it first";
			}
		}
	}
	else
	{
		// new position in the list: the numbers are doubled and the block is put before or after the one that occupies the chosen position
		const int nOld = Bl.B;
		const int nTarget = nCol < 0 ? 0 : nCol;
		for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
		{
			m_Block[i].B *= 2;
		}
		Bl.B = nTarget > nOld ? nTarget * 2 + 1 : nTarget * 2 - 1;
		CompactOrder (m_Block);
	}
	Touch (TRUE);
	return 0;
}

const char *CBoard::NetLink (int nFrom, int nTo, int nPf, int nPt, float fDb)
{
	if (nFrom != BD_NET_IN && (nFrom < 0 || nFrom >= (int) BD_MAX_BLOCKS || !m_Block[nFrom].Used))
	{
		return "no such source";
	}
	if (nTo != BD_NET_OUT && (nTo < 0 || nTo >= (int) BD_MAX_BLOCKS || !m_Block[nTo].Used))
	{
		return "no such destination";
	}
	if (nFrom == nTo)
	{
		return "a block cannot link to itself";
	}
	if (nPf < 0 || nPf > 2 || nPt < 0 || nPt > 2)
	{
		return "invalid port";
	}
	if ((nFrom == BD_NET_IN && nPf == 0) || (nTo == BD_NET_OUT && nPt == 0))
	{
		return "the input and the output have two separate channels: choose L or R";
	}
	if (m_nMode == 2 && !NetForward (nFrom, nPf, nTo, nPt))
	{
		return "the link must go to the right or vertically (never to the left)";
	}
	if (nFrom >= 0 && nTo >= 0 && NetReaches (nTo, nFrom))
	{
		return "the link would create a loop (the signal would go back)";
	}
	const float fClamped = Clamp (fDb, -60.0f, 12.0f);
	for (unsigned k = 0; k < m_nLink; k++)
	{
		if (m_Link[k].From == nFrom && m_Link[k].To == nTo && m_Link[k].PF == nPf && m_Link[k].PT == nPt)
		{
			m_Link[k].Db = fClamped;
			Touch (FALSE);
			return 0;
		}
	}
	if (m_nLink >= BD_MAX_LINKS)
	{
		return "too many links";
	}
	TBdLink &K = m_Link[m_nLink++];
	K.From = nFrom;
	K.To = nTo;
	K.PF = (u8) nPf;
	K.PT = (u8) nPt;
	K.Db = fClamped;
	Touch (TRUE);
	return 0;
}

const char *CBoard::NetUnlink (int nFrom, int nTo, int nPf, int nPt)
{
	for (unsigned k = 0; k < m_nLink; k++)
	{
		if (m_Link[k].From == nFrom && m_Link[k].To == nTo && m_Link[k].PF == nPf && m_Link[k].PT == nPt)
		{
			for (unsigned j = k + 1; j < m_nLink; j++)
			{
				m_Link[j - 1] = m_Link[j];
			}
			m_nLink--;
			Touch (TRUE);
			return 0;
		}
	}
	return "no such link";
}

// the branches of a Selector are its output ports (A = port 1, B = port 2, C = port 0), not the position of the destinations: no link has an order to protect
void CBoard::SelectorRanks (int *pRank) const
{
	for (unsigned k = 0; k < BD_MAX_LINKS; k++)
	{
		pRank[k] = -1;
	}
}
const char *CBoard::NetRelayout (const int *pId, const int *pRow, const int *pCol, unsigned nBlocks, const int *pIoRow, const int *pIoCol)
{
	if (m_nMode != 2)
	{
		return "solo in ADELGrid";
	}
	unsigned nUsed = 0;
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		if (m_Block[i].Used)
		{
			nUsed++;
		}
	}
	if (nBlocks != nUsed)
	{
		return "the positions of all the blocks are needed";
	}
	boolean Seen[BD_MAX_BLOCKS];
	boolean Cell[BD_GRID_ROWS][BD_GRID_COLS];
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		Seen[i] = FALSE;
	}
	for (unsigned r = 0; r < BD_GRID_ROWS; r++)
	{
		for (unsigned c = 0; c < BD_GRID_COLS; c++)
		{
			Cell[r][c] = FALSE;
		}
	}
	for (unsigned k = 0; k < nBlocks; k++)
	{
		if (pId[k] < 0 || pId[k] >= (int) BD_MAX_BLOCKS || !m_Block[pId[k]].Used || Seen[pId[k]])
		{
			return "invalid or repeated block";
		}
		if (pRow[k] < 0 || pRow[k] >= (int) BD_GRID_ROWS || pCol[k] < 0 || pCol[k] >= (int) BD_GRID_COLS || Cell[pRow[k]][pCol[k]])
		{
			return "cell outside the grid or occupied twice";
		}
		Seen[pId[k]] = TRUE;
		Cell[pRow[k]][pCol[k]] = TRUE;
	}
	for (unsigned w = 0; w < 4; w++)
	{
		if (pIoRow[w] < 0 || pIoRow[w] >= (int) BD_GRID_ROWS || pIoCol[w] < 0 || pIoCol[w] >= (int) BD_GRID_COLS || Cell[pIoRow[w]][pIoCol[w]])
		{
			return "cell outside the grid or occupied twice";
		}
		Cell[pIoRow[w]][pIoCol[w]] = TRUE;
	}
	int OldA[BD_MAX_BLOCKS], OldB[BD_MAX_BLOCKS], OldIoRow[4], OldIoCol[4];
	int PrevRank[BD_MAX_LINKS], PrevCls[17];
	SelectorRanks (PrevRank);
	for (unsigned c = 1; c <= 16; c++)
	{
		PrevCls[c] = MidiFindClass (c);
	}
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		OldA[i] = m_Block[i].A;
		OldB[i] = m_Block[i].B;
	}
	for (unsigned w = 0; w < 4; w++)
	{
		OldIoRow[w] = m_IoRow[w];
		OldIoCol[w] = m_IoCol[w];
		m_IoRow[w] = pIoRow[w];
		m_IoCol[w] = pIoCol[w];
	}
	for (unsigned k = 0; k < nBlocks; k++)
	{
		m_Block[pId[k]].A = pRow[k];
		m_Block[pId[k]].B = pCol[k];
	}
	for (unsigned k = 0; k < m_nLink; k++)
	{
		if (!NetForward (m_Link[k].From, m_Link[k].PF, m_Link[k].To, m_Link[k].PT))
		{
			for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
			{
				m_Block[i].A = OldA[i];
				m_Block[i].B = OldB[i];
			}
			for (unsigned w = 0; w < 4; w++)
			{
				m_IoRow[w] = OldIoRow[w];
				m_IoCol[w] = OldIoCol[w];
			}
			return "with the new positions a link would go to the left";
		}
	}
	// the reordering must not change anything that depends on position: the A/B/C branches of the Selectors and which block is "the first" of each MIDI class
	int NewRank[BD_MAX_LINKS];
	SelectorRanks (NewRank);
	boolean bSame = TRUE;
	for (unsigned k = 0; k < m_nLink; k++)
	{
		if (NewRank[k] != PrevRank[k])
		{
			bSame = FALSE;
		}
	}
	for (unsigned c = 1; c <= 16; c++)
	{
		if (MidiFindClass (c) != PrevCls[c])
		{
			bSame = FALSE;
		}
	}
	if (!bSame)
	{
		for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
		{
			m_Block[i].A = OldA[i];
			m_Block[i].B = OldB[i];
		}
		for (unsigned w = 0; w < 4; w++)
		{
			m_IoRow[w] = OldIoRow[w];
			m_IoCol[w] = OldIoCol[w];
		}
		return "the reordering would change the order of a Selector's branches or the MIDI numbering of the blocks";
	}
	Touch (TRUE);
	return 0;
}

const char *CBoard::NetMoveIo (int nWhich, int nRow, int nCol)
{
	if (m_nMode != 2)
	{
		return "solo in ADELGrid";
	}
	if (nWhich < 0 || nWhich > 3 || nRow < 0 || nRow >= (int) BD_GRID_ROWS || nCol < 0 || nCol >= (int) BD_GRID_COLS)
	{
		return "no such cell";
	}
	if (m_IoRow[nWhich] == nRow && m_IoCol[nWhich] == nCol)
	{
		return 0;
	}
	if (!NetCellFree (nRow, nCol))
	{
		return "the cell is occupied";
	}
	const int nOldRow = m_IoRow[nWhich], nOldCol = m_IoCol[nWhich];
	m_IoRow[nWhich] = nRow;
	m_IoCol[nWhich] = nCol;
	for (unsigned k = 0; k < m_nLink; k++)
	{
		if (NetLinkOnIo (m_Link[k], nWhich) && !NetForward (m_Link[k].From, m_Link[k].PF, m_Link[k].To, m_Link[k].PT))
		{
			m_IoRow[nWhich] = nOldRow;
			m_IoCol[nWhich] = nOldCol;
			return "a link must go to the right or vertically: move it or remove it first";
		}
	}
	Touch (TRUE);
	return 0;
}

struct TTmpLink
{
	short To;
	short From;
	u8 PF, PT;
	float G;
};

static void PutLink (TTmpLink *pL, unsigned *pN, int nFrom, int nTo, u8 nPf, u8 nPt, float fGain)
{
	if (*pN >= PL_MAX_LINKS)
	{
		return;
	}
	TTmpLink &K = pL[(*pN)++];
	K.From = (short) (nFrom == BD_NET_IN ? PL_NET_IN : nFrom);
	K.To = (short) (nTo == BD_NET_OUT ? PL_NET_OUT : nTo);
	K.PF = nPf;
	K.PT = nPt;
	K.G = fGain;
}

// assigns a core to every node with list scheduling: in calculation order, every NAM or effect that is on goes to the core where it finishes first (node cost + wait for the
// source nodes + a synchronization penalty if the source is on another core). The other blocks (volume, pan, ADELphase and ADELSendSweep) stay on core 0. Returns the estimated duration of the block in tenths of a % of the period (longest path among the cores).
#define MC_SYNC_COST10	5
static unsigned McSchedule (TPlan *pPlan, unsigned nCores)
{
	unsigned Finish[PL_MAX_NODES];
	unsigned CoreFree[GR_MAX_CORES];
	for (unsigned i = 0; i < PL_MAX_NODES; i++)
	{
		Finish[i] = 0;
		pPlan->Core[i] = 0;
	}
	for (unsigned c = 0; c < GR_MAX_CORES; c++)
	{
		CoreFree[c] = 0;
	}
	unsigned nMake = 0, nUsed = 1;
	for (int k = 0; k < pPlan->NOrder; k++)
	{
		const int id = pPlan->Order[k];
		if (id < 0 || id >= PL_MAX_NODES)
		{
			continue;
		}
		const TPNode &N = pPlan->Node[id];
		const unsigned nCost = N.On ? CBoard::NodeCost10 (N.Kind) : 0;
		const boolean bOff = N.On && McOffloadable (N.Kind);
		unsigned nReady = 0;
		for (int q = pPlan->LFirst[id]; q < pPlan->LFirst[id + 1] && q < PL_MAX_LINKS; q++)
		{
			const int nFrom = pPlan->Link[q].From;
			if (nFrom >= 0 && nFrom < PL_MAX_NODES && Finish[nFrom] > nReady)
			{
				nReady = Finish[nFrom];
			}
		}
		unsigned nBest = 0, nBestFin = 0;
		const unsigned nTry = bOff ? nCores : 1;
		for (unsigned c = 0; c < nTry; c++)
		{
			unsigned nSync = 0;
			for (int q = pPlan->LFirst[id]; q < pPlan->LFirst[id + 1] && q < PL_MAX_LINKS; q++)
			{
				const int nFrom = pPlan->Link[q].From;
				if (nFrom >= 0 && nFrom < PL_MAX_NODES && pPlan->Core[nFrom] != c)
				{
					nSync = MC_SYNC_COST10;
				}
			}
			const unsigned nStart = CoreFree[c] > nReady + nSync ? CoreFree[c] : nReady + nSync;
			const unsigned nFin = nStart + nCost;
			if (c == 0 || nFin < nBestFin)
			{
				nBest = c;
				nBestFin = nFin;
			}
		}
		pPlan->Core[id] = (u8) nBest;
		Finish[id] = nBestFin;
		CoreFree[nBest] = nBestFin;
		if (nBest + 1 > nUsed)
		{
			nUsed = nBest + 1;
		}
		if (nBestFin > nMake)
		{
			nMake = nBestFin;
		}
	}
	pPlan->NCores = (u8) nUsed;
	return nMake;
}

// links (explicit and, in the Grid, the implicit ones along the rows) and topological calculation order
void CBoard::NetBuild (TPlan *pPlan) const
{
	TTmpLink Tmp[PL_MAX_LINKS];
	unsigned nTmp = 0;
	for (unsigned k = 0; k < m_nLink; k++)
	{
		const TBdLink &K = m_Link[k];
		if ((K.From >= 0 && !m_Block[K.From].Used) || (K.To >= 0 && !m_Block[K.To].Used))
		{
			continue;
		}
		PutLink (Tmp, &nTmp, K.From, K.To, K.PF, K.PT, DbToLin (K.Db));
	}
	// Selector (A/B/C): the branches are its output ports (A = port 1, B = port 2, C = port 0); only the chosen port passes, the others are zero (a port with no links
	// = silence). The signal is always the block's stereo one: the port only serves to choose the branch, so the compiled link starts from port 0. On = selects; off = all the branches pass
	for (unsigned s = 0; s < PL_MAX_NODES; s++)
	{
		if (!m_Block[s].Used || m_Block[s].Type != FxSelector)
		{
			continue;
		}
		const int nOuts = m_Block[s].V[1] > 0.5f ? 3 : 2;		// Outputs: AB or ABC (a port beyond the chosen number stays muted)
		int nSel = (int) (m_Block[s].V[0] + 0.5f);
		nSel = nSel < 0 ? 0 : (nSel >= nOuts ? nOuts - 1 : nSel);
		for (unsigned k = 0; k < nTmp; k++)
		{
			if (Tmp[k].From != (short) s)
			{
				continue;
			}
			const int nBranch = Tmp[k].PF == 1 ? 0 : (Tmp[k].PF == 2 ? 1 : 2);
			Tmp[k].PF = 0;
			if (m_Block[s].On && nBranch != nSel)
			{
				Tmp[k].G = 0.0f;
			}
		}
	}
	// calculation order: at every step the ready block (all its sources already calculated) with the smallest key
	boolean Done[PL_MAX_NODES];
	unsigned nOrder = 0;
	for (unsigned i = 0; i < PL_MAX_NODES; i++)
	{
		Done[i] = FALSE;
	}
	for (unsigned step = 0; step < PL_MAX_NODES; step++)
	{
		int nBest = -1;
		int nBestKey = 0;
		for (unsigned i = 0; i < PL_MAX_NODES; i++)
		{
			if (!m_Block[i].Used || Done[i])
			{
				continue;
			}
			boolean bReady = TRUE;
			for (unsigned k = 0; k < nTmp; k++)
			{
				if (Tmp[k].To == (short) i && Tmp[k].From >= 0 && !Done[Tmp[k].From] && m_Block[Tmp[k].From].Used)
				{
					bReady = FALSE;
					break;
				}
			}
			const int nKey = ((m_nMode == 2 ? m_Block[i].B * (int) BD_GRID_ROWS + m_Block[i].A : m_Block[i].B) << 6) + (int) i;
			if (bReady && (nBest < 0 || nKey < nBestKey))
			{
				nBest = (int) i;
				nBestKey = nKey;
			}
		}
		if (nBest < 0)
		{
			break;		// blocks left over = loop (it should not exist: the links prevent it)
		}
		Done[nBest] = TRUE;
		pPlan->Order[nOrder++] = (short) nBest;
	}
	pPlan->NOrder = (short) nOrder;

	// nodes to skip: silent ones (no inputs, or only from silent nodes; the ADELSendSweep generator never is) and, among the heavy ones,
	// NAM and Cab IR whose output does not reach any output of the pedalboard
	boolean Silent[PL_MAX_NODES], Alive[PL_MAX_NODES];
	for (unsigned i = 0; i < PL_MAX_NODES; i++)
	{
		Silent[i] = FALSE;
		Alive[i] = FALSE;
	}
	for (unsigned k = 0; k < nOrder; k++)
	{
		const int id = pPlan->Order[k];
		boolean bAnyIn = FALSE, bAllSilent = TRUE;
		for (unsigned q = 0; q < nTmp; q++)
		{
			if (Tmp[q].To == (short) id)
			{
				bAnyIn = TRUE;
				if (Tmp[q].From < 0 || !Silent[Tmp[q].From])
				{
					bAllSilent = FALSE;
				}
			}
		}
		Silent[id] = (!bAnyIn || bAllSilent) && m_Block[id].Type != FxAdelSweep;
	}
	for (unsigned q = 0; q < nTmp; q++)
	{
		if (Tmp[q].To == PL_NET_OUT && Tmp[q].From >= 0)
		{
			Alive[Tmp[q].From] = TRUE;
		}
	}
	for (unsigned pass = 0; pass < nOrder; pass++)
	{
		for (unsigned q = 0; q < nTmp; q++)
		{
			if (Tmp[q].To >= 0 && Tmp[q].To < PL_MAX_NODES && Alive[Tmp[q].To] && Tmp[q].From >= 0)
			{
				Alive[Tmp[q].From] = TRUE;
			}
		}
	}
	for (unsigned k = 0; k < nOrder; k++)
	{
		const int id = pPlan->Order[k];
		pPlan->Skip[id] = (Silent[id] || (!Alive[id] && (m_Block[id].Type == NodeNam || m_Block[id].Type == FxCabIR))) ? 1 : 0;
	}

	// links grouped by destination
	unsigned nCount[PL_MAX_NODES + 1];
	for (unsigned d = 0; d <= PL_MAX_NODES; d++)
	{
		nCount[d] = 0;
	}
	for (unsigned k = 0; k < nTmp; k++)
	{
		nCount[Tmp[k].To]++;
	}
	unsigned nAt = 0;
	unsigned nFill[PL_MAX_NODES + 1];
	for (unsigned d = 0; d <= PL_MAX_NODES; d++)
	{
		pPlan->LFirst[d] = (short) nAt;
		nFill[d] = nAt;
		nAt += nCount[d];
	}
	pPlan->LFirst[PL_MAX_NODES + 1] = (short) nAt;
	for (unsigned k = 0; k < nTmp; k++)
	{
		TPLink &L = pPlan->Link[nFill[Tmp[k].To]++];
		L.From = Tmp[k].From;
		L.PF = Tmp[k].PF;
		L.PT = Tmp[k].PT;
		L.Gain = Tmp[k].G;
	}
	if (s_nCores > 1)
	{
		McSchedule (pPlan, s_nCores);
	}
}

boolean CBoard::ValidateNet (void) const
{
	boolean Cell[BD_GRID_ROWS][BD_GRID_COLS];
	for (unsigned r = 0; r < BD_GRID_ROWS; r++)
	{
		for (unsigned c = 0; c < BD_GRID_COLS; c++)
		{
			Cell[r][c] = FALSE;
		}
	}
	unsigned nUsed = 0;
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		const TBdBlock &Bl = m_Block[i];
		if (!Bl.Used)
		{
			continue;
		}
		nUsed++;
		if ((Bl.Type != NodeNam && Bl.Type != NodeVolume && Bl.Type != NodePan && FxDefByType (Bl.Type) == 0))
		{
			return FALSE;
		}
		if (m_nMode == 2)
		{
			if (Bl.A < 0 || Bl.A >= (int) BD_GRID_ROWS || Bl.B < 0 || Bl.B >= (int) BD_GRID_COLS || Cell[Bl.A][Bl.B])
			{
				return FALSE;
			}
			Cell[Bl.A][Bl.B] = TRUE;
		}
	}
	if (m_nMode == 2)
	{
		for (unsigned w = 0; w < 4; w++)
		{
			if (m_IoRow[w] < 0 || m_IoRow[w] >= (int) BD_GRID_ROWS || m_IoCol[w] < 0 || m_IoCol[w] >= (int) BD_GRID_COLS || Cell[m_IoRow[w]][m_IoCol[w]])
			{
				return FALSE;
			}
			Cell[m_IoRow[w]][m_IoCol[w]] = TRUE;
		}
	}
	// links
	int nIn[BD_MAX_BLOCKS];
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		nIn[i] = 0;
	}
	for (unsigned k = 0; k < m_nLink; k++)
	{
		const TBdLink &K = m_Link[k];
		if ((K.From != BD_NET_IN && (K.From < 0 || K.From >= (int) BD_MAX_BLOCKS || !m_Block[K.From].Used))
			|| (K.To != BD_NET_OUT && (K.To < 0 || K.To >= (int) BD_MAX_BLOCKS || !m_Block[K.To].Used))
			|| K.From == K.To || K.PF > 2 || K.PT > 2
			|| (K.From == BD_NET_IN && K.PF == 0) || (K.To == BD_NET_OUT && K.PT == 0))
		{
			return FALSE;
		}
		if (m_nMode == 2 && !NetForward (K.From, K.PF, K.To, K.PT))
		{
			return FALSE;
		}
		if (K.From >= 0 && K.To >= 0)
		{
			nIn[K.To]++;
		}
	}
	// no loop: the blocks without inputs are removed one by one
	boolean Gone[BD_MAX_BLOCKS];
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		Gone[i] = !m_Block[i].Used;
	}
	for (unsigned step = 0; step < nUsed; step++)
	{
		int nPick = -1;
		for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
		{
			if (!Gone[i] && nIn[i] == 0)
			{
				nPick = (int) i;
				break;
			}
		}
		if (nPick < 0)
		{
			return FALSE;
		}
		Gone[nPick] = TRUE;
		for (unsigned k = 0; k < m_nLink; k++)
		{
			if (m_Link[k].From == nPick && m_Link[k].To >= 0)
			{
				nIn[m_Link[k].To]--;
			}
		}
	}
	return TRUE;
}

// ---- saving ----

static unsigned PutInt (char *p, int v)
{
	unsigned n = 0;
	if (v < 0)
	{
		p[n++] = '-';
		v = -v;
	}
	char D[12];
	unsigned d = 0;
	do
	{
		D[d++] = (char) ('0' + v % 10);
		v /= 10;
	}
	while (v > 0 && d < sizeof D);
	while (d > 0)
	{
		p[n++] = D[--d];
	}
	return n;
}

unsigned CBoard::Serialize (char *pBuffer, unsigned nSize) const
{
	unsigned n = 0;
	pBuffer[n++] = 'm';
	pBuffer[n++] = ' ';
	n += PutInt (pBuffer + n, (int) m_nMode);
	pBuffer[n++] = '\n';
	for (unsigned i = 0;i < BD_MAX_BLOCKS && n + 380 < nSize; i++)
	{
		const TBdBlock &Bl = m_Block[i];
		if (!Bl.Used)
		{
			continue;
		}
		pBuffer[n++] = 'd';
		pBuffer[n++] = ' ';
		n += PutInt (pBuffer + n, (int) i);
		const int v[9] = {Bl.Type, Bl.On ? 1 : 0, -1, Bl.A, Bl.B, -1, -1, 0, Bl.Slot};		// next, c, par and lane no longer exist: they stay in the file format
		for (unsigned k = 0; k < 9; k++)
		{
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, v[k]);
		}
		for (unsigned k = 0; k < 12; k++)
		{
			pBuffer[n++] = ' ';
			n += FormatFixed (pBuffer + n, Bl.V[k], 2);
		}
		pBuffer[n++] = ' ';
		if (Bl.File[0] == 0)
		{
			pBuffer[n++] = '-';
		}
		for (const char *s = Bl.File; *s; s++)
		{
			pBuffer[n++] = *s;
		}
				pBuffer[n++] = '\n';
		const TFxDef *pFd = FxDefByType (Bl.Type);
		if (pFd != 0 && pFd->nParams > 12 && n + 64 + 12 * pFd->nParams < nSize)
		{
			pBuffer[n++] = 'e';
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, (int) i);
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, (int) pFd->nParams);
			for (unsigned k = 0; k < pFd->nParams; k++)
			{
				pBuffer[n++] = ' ';
				n += FormatFixed (pBuffer + n, Bl.V[k], 4);
			}
			pBuffer[n++] = '\n';
		}
		if (Bl.Preset[0] != 0 && n + 60 < nSize)
		{
			pBuffer[n++] = 'p';
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, (int) i);
			pBuffer[n++] = ' ';
			for (const char *s = Bl.Preset; *s; s++)
			{
				pBuffer[n++] = *s;
			}
			pBuffer[n++] = '\n';
		}
	}
	if (m_nMode != 0)
	{
		for (unsigned k = 0; k < m_nLink && n + 100 < nSize; k++)
		{
			const TBdLink &K = m_Link[k];
			pBuffer[n++] = 'k';
			const int v[4] = {K.From, K.To, K.PF, K.PT};
			for (unsigned q = 0; q < 4; q++)
			{
				pBuffer[n++] = ' ';
				n += PutInt (pBuffer + n, v[q]);
			}
			pBuffer[n++] = ' ';
			n += FormatFixed (pBuffer + n, K.Db, 2);
			pBuffer[n++] = '\n';
		}
		if (m_nMode == 2)
		{
			pBuffer[n++] = 'o';
			for (unsigned w = 0; w < 4; w++)
			{
				pBuffer[n++] = ' ';
				n += PutInt (pBuffer + n, m_IoRow[w]);
				pBuffer[n++] = ' ';
				n += PutInt (pBuffer + n, m_IoCol[w]);
			}
			pBuffer[n++] = '\n';
		}
	}
	// MIDI map (only if present: firmwares without MIDI reject the lines they do not know)
	for (unsigned i = 0; i < MIDI_BLOCK_CC_MAX && n + 40 < nSize; i++)
	{
		const TMidiBlockCc &Bc = m_Midi.BlockCc (i);
		if (Bc.Cc != 0 && Bc.Block >= 0)
		{
			pBuffer[n++] = 'x';
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, Bc.Cc);
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, Bc.Block);
			pBuffer[n++] = '\n';
		}
	}
	for (unsigned c = 0; c < MIDI_CTL_COUNT && n + 100 < nSize; c++)
	{
		const TMidiCtl &Ct = m_Midi.Ctl (c);
		if (Ct.Cc == 0 && c != 2)
		{
			continue;
		}
		if (c != 2)		// controller 3 is global: its source is in midi.cfg, only the linked parameters stay in the preset
		{
			pBuffer[n++] = 'y';
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, (int) c);
			pBuffer[n++] = ' ';
			n += PutInt (pBuffer + n, Ct.Cc);
			pBuffer[n++] = '\n';
		}
		for (unsigned k = 0; k < MIDI_CTL_LINKS; k++)
		{
			if (Ct.Link[k].Block < 0)
			{
				continue;
			}
			pBuffer[n++] = 'z';
			const int v[4] = {(int) c, (int) k, Ct.Link[k].Block, Ct.Link[k].Param};
			for (unsigned q = 0; q < 4; q++)
			{
				pBuffer[n++] = ' ';
				n += PutInt (pBuffer + n, v[q]);
			}
			pBuffer[n++] = ' ';
			n += FormatFixed (pBuffer + n, Ct.Link[k].MinPct, 1);
			pBuffer[n++] = ' ';
			n += FormatFixed (pBuffer + n, Ct.Link[k].MaxPct, 1);
			pBuffer[n++] = '\n';
		}
	}
	pBuffer[n] = 0;
	return n;
}

// reads an integer (also negative) and advances; FALSE if missing
static boolean NextInt (const char *&p, int *pValue)
{
	while (*p == ' ')
	{
		p++;
	}
	boolean bNeg = FALSE;
	if (*p == '-')
	{
		bNeg = TRUE;
		p++;
	}
	if (*p < '0' || *p > '9')
	{
		return FALSE;
	}
	int v = 0;
	while (*p >= '0' && *p <= '9')
	{
		v = v * 10 + (*p - '0');
		p++;
	}
	*pValue = bNeg ? -v : v;
	return TRUE;
}

static boolean NextFloat (const char *&p, float *pValue)
{
	while (*p == ' ')
	{
		p++;
	}
	if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '.'))
	{
		return FALSE;
	}
	*pValue = ParseFloat (p);
	if (*p == '-')
	{
		p++;
	}
	while ((*p >= '0' && *p <= '9') || *p == '.')
	{
		p++;
	}
	return TRUE;
}

boolean CBoard::RemapNamSlots (const boolean *pBusy)
{
	int nNew[BD_MAX_BLOCKS];
	boolean bTaken[BD_MAX_NAM];
	for (unsigned s = 0; s < BD_MAX_NAM; s++)
	{
		bTaken[s] = pBusy[s];
	}
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		nNew[id] = -1;
		if (!m_Block[id].Used || m_Block[id].Type != NodeNam)
		{
			continue;
		}
		for (unsigned s = 0; s < BD_MAX_NAM; s++)
		{
			if (!bTaken[s])
			{
				bTaken[s] = TRUE;
				nNew[id] = (int) s;
				break;
			}
		}
		if (nNew[id] < 0)
		{
			return FALSE;
		}
	}
	for (unsigned s = 0; s < BD_MAX_NAM; s++)
	{
		m_bSlot[s] = FALSE;
	}
	for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
	{
		if (nNew[id] >= 0)
		{
			m_Block[id].Slot = nNew[id];
			m_bSlot[nNew[id]] = TRUE;
		}
	}
	return TRUE;
}

void CBoard::Adopt (const CBoard &Other)
{
	// the counters continue from those of the document in use: the page notices the change even if Other has lower ones
	const unsigned nVersion = m_nVersion, nStruct = m_nStruct, nEdit = m_nEdit;
	*this = Other;
	m_nVersion = nVersion;
	m_nStruct = nStruct;
	m_nEdit = nEdit;
	Touch (TRUE);
	m_bSaveDirty = FALSE;
}

boolean CBoard::Parse (const char *pText)
{
	static CBoard *s_pTmp = 0;		// single and on the heap the first time: the structure is too big for a task's stack and for the kernel image (26 KB less)
	if (s_pTmp == 0)
	{
		s_pTmp = new CBoard;
	}
	CBoard &Tmp = *s_pTmp;
	Tmp.Reset ();
	Tmp.m_nMode = m_nMode;

	boolean bModeSeen = FALSE;
	const char *p = pText;
	while (*p)
	{
		const char *pLine = p;
		while (*p && *p != '\n')
		{
			p++;
		}
		const char *pEnd = p;
		if (*p == '\n')
		{
			p++;
		}
		if (pLine == pEnd)
		{
			continue;
		}

		const char *q = pLine + 1;
		if (pLine[0] == 'm')
		{
			int nMode;
			if (!NextInt (q, &nMode) || nMode != (int) m_nMode)
			{
				return FALSE;		// text of another mode
			}
			bModeSeen = TRUE;
		}
		else if (pLine[0] == 'k')
		{
			int v[4];
			float fDb;
			for (unsigned ki = 0; ki < 4; ki++)
			{
				if (!NextInt (q, &v[ki]))
				{
					return FALSE;
				}
			}
			if (!NextFloat (q, &fDb) || Tmp.m_nLink >= BD_MAX_LINKS || v[2] < 0 || v[2] > 2 || v[3] < 0 || v[3] > 2)
			{
				return FALSE;
			}
			TBdLink &K = Tmp.m_Link[Tmp.m_nLink++];
			K.From = v[0];
			K.To = v[1];
			K.PF = (u8) v[2];
			K.PT = (u8) v[3];
			K.Db = fDb;
		}
		else if (pLine[0] == 'r')
		{
			// row levels of a previous version: they no longer exist, the row is ignored
		}
		else if (pLine[0] == 'x')		// MIDI: CC assigned to a block
		{
			int cc, id;
			if (!NextInt (q, &cc) || !NextInt (q, &id))
			{
				return FALSE;
			}
			if (id >= 0 && id < (int) BD_MAX_BLOCKS && Tmp.m_Block[id].Used)
			{
				Tmp.m_Midi.SetBlockCc (cc, id);
			}
		}
		else if (pLine[0] == 'y')		// MIDI: source of a controller
		{
			int c, cc;
			if (!NextInt (q, &c) || !NextInt (q, &cc))
			{
				return FALSE;
			}
			if (c >= 0 && c < (int) MIDI_CTL_COUNT && cc >= MIDI_CC_FIRST_FREE && cc <= 127)
			{
				Tmp.m_Midi.Ctl ((unsigned) c).Cc = cc;
			}
		}
		else if (pLine[0] == 'z')		// MIDI: parameter linked to a controller
		{
			int c, k, id, par;
			float fMin, fMax;
			if (!NextInt (q, &c) || !NextInt (q, &k) || !NextInt (q, &id) || !NextInt (q, &par) || !NextFloat (q, &fMin) || !NextFloat (q, &fMax))
			{
				return FALSE;
			}
			if (c >= 0 && c < (int) MIDI_CTL_COUNT && k >= 0 && k < (int) MIDI_CTL_LINKS && id >= 0 && id < (int) BD_MAX_BLOCKS && Tmp.m_Block[id].Used
			    && par >= 0 && par < (int) FX_MAX_P)
			{
				TMidiLink &L = Tmp.m_Midi.Ctl ((unsigned) c).Link[k];
				L.Block = id;
				L.Param = par;
				L.MinPct = fMin < 0.0f ? 0.0f : (fMin > 100.0f ? 100.0f : fMin);
				L.MaxPct = fMax < 0.0f ? 0.0f : (fMax > 100.0f ? 100.0f : fMax);
			}
		}
		else if (pLine[0] == 'o')
		{
			int v[8];
			for (unsigned ki = 0; ki < 8; ki++)
			{
				if (!NextInt (q, &v[ki]))
				{
					return FALSE;
				}
			}
			for (unsigned w = 0; w < 4; w++)
			{
				Tmp.m_IoRow[w] = v[w * 2];
				Tmp.m_IoCol[w] = v[w * 2 + 1];
			}
		}
		else if (pLine[0] == 'l')
		{
			// ADELFlow lanes: they no longer exist, the row is ignored
		}
		else if (pLine[0] == 'e')
			{
				int i, nVals;
				if (!NextInt (q, &i) || i < 0 || i >= (int) BD_MAX_BLOCKS || !NextInt (q, &nVals) || nVals < 0 || nVals > (int) FX_MAX_P)
				{
					return FALSE;
				}
				if (!Tmp.m_Block[i].Used)
				{
					return FALSE;
				}
				for (int k = 0; k < nVals; k++)
				{
					if (!NextFloat (q, &Tmp.m_Block[i].V[k]))
					{
						return FALSE;
					}
				}
			}
			else if (pLine[0] == 'p')
			{
				int i;
				if (!NextInt (q, &i) || i < 0 || i >= (int) BD_MAX_BLOCKS)
				{
					return FALSE;
				}
				while (*q == ' ')
				{
					q++;
				}
				char Name[BD_PRESET_LEN];
				unsigned f = 0;
				while (q < pEnd && *q != '\r' && f + 1 < sizeof Name)
				{
					Name[f++] = *q++;
				}
				Name[f] = 0;
				if (Tmp.m_Block[i].Used && ValidPresetName (Name))
				{
					Cpy (Tmp.m_Block[i].Preset, Name, sizeof Tmp.m_Block[i].Preset);
				}
			}
			else if (pLine[0] == 'b' || pLine[0] == 'c' || pLine[0] == 'd')
		{
			const unsigned nVals = pLine[0] == 'd' ? 12 : (pLine[0] == 'c' ? 8 : 4);
			const unsigned nInts = pLine[0] == 'd' ? 9 : 8;
			int i, v[9];
			if (!NextInt (q, &i) || i < 0 || i >= (int) BD_MAX_BLOCKS)
			{
				return FALSE;
			}
			for (unsigned k = 0; k < nInts; k++)
			{
				if (!NextInt (q, &v[k]))
				{
					return FALSE;
				}
			}
			// format 'd': type, on, next, a, b, c, par, lane, slot (next, c, par and lane are ignored); formats 'b'/'c': type, on, next, a, b, par, lane, slot
			const int nSlot = nInts == 9 ? v[8] : v[7];
			TBdBlock &Bl = Tmp.m_Block[i];
			Bl.Used = TRUE;
			Bl.Type = (u8) v[0];
			Bl.On = v[1] != 0;
			Bl.A = v[3];
			Bl.B = v[4];
			Bl.Slot = nSlot;
			Bl.BaseIn = Bl.BaseOut = 1.0f;
			Bl.HasModelIn = FALSE;
			Bl.ModelInDbu = 0.0f;
			for (unsigned k = 0; k < nVals; k++)
			{
				if (!NextFloat (q, &Bl.V[k]))
				{
					return FALSE;
				}
			}
			while (*q == ' ')
			{
				q++;
			}
			unsigned f = 0;
			while (q < pEnd && *q != '\r' && f + 1 < sizeof Bl.File)
			{
				Bl.File[f++] = *q++;
			}
			Bl.File[f] = 0;
			if (strcmp (Bl.File, "-") == 0)
			{
				Bl.File[0] = 0;
			}
			if (Bl.Type == NodeNam)
			{
				if (!BoardValidFileName (Bl.File))
				{
					return FALSE;
				}
				if (Bl.Slot < 0 || Bl.Slot >= (int) BD_MAX_NAM || Tmp.m_bSlot[Bl.Slot])
				{
					return FALSE;
				}
				Tmp.m_bSlot[Bl.Slot] = TRUE;
				Cpy (Bl.Status, "loading", sizeof Bl.Status);
			}
			else if (Bl.Type == FxCabIR && Bl.File[0] != 0 && !BoardValidIrName (Bl.File))
			{
				return FALSE;
			}
		}
		else
		{
			return FALSE;
		}
	}

	// removed block types (RAT Distortion 31, Ring Modulator 36, Bit Crusher 42, Octavia 57, Optical Compressor 69, Q-Tron 66, Slapback 72, Dotted Delay 73, Reverse Delay 74, Whammy 76): the block disappears and the signal bypasses it (every input is linked to every one of its outputs), so the preset still loads
	for (unsigned i = 0; i < BD_MAX_BLOCKS; i++)
	{
		const unsigned nOld = Tmp.m_Block[i].Type;
		if (!Tmp.m_Block[i].Used || (nOld != 31 && nOld != 36 && nOld != 42 && nOld != 57 && nOld != 66 && nOld != 69 && nOld != 72 && nOld != 73 && nOld != 74 && nOld != 76))
		{
			continue;
		}
		TBdLink In[BD_MAX_LINKS], Out[BD_MAX_LINKS];
		unsigned nIn = 0, nOut = 0;
		for (unsigned k = 0; k < Tmp.m_nLink; k++)
		{
			if (Tmp.m_Link[k].To == (int) i)
			{
				In[nIn++] = Tmp.m_Link[k];
			}
			if (Tmp.m_Link[k].From == (int) i)
			{
				Out[nOut++] = Tmp.m_Link[k];
			}
		}
		Tmp.NetRemove ((int) i);
		for (unsigned a = 0; a < nIn; a++)
		{
			for (unsigned b = 0; b < nOut; b++)
			{
				Tmp.NetLink (In[a].From, Out[b].To, In[a].PF, Out[b].PT, In[a].Db + Out[b].Db);
			}
		}
	}
	if (!bModeSeen || !Tmp.ValidateNet ())
	{
		return FALSE;
	}
	*this = Tmp;
	Touch (TRUE);
	m_bSaveDirty = FALSE;
	return TRUE;
}
