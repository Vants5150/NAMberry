//
// board.h - pedalboard document (ADELGrid): blocks in a grid and the links between them.
// It is modified by the web page (HTTP task) and by the main loop; they are cooperative tasks, so the functions below never
// yield control and the changes are atomic with respect to each other. The audio callback does not touch it: it uses the plan (graph.h).
//
#ifndef _board_h
#define _board_h

#include <circle/types.h>
#include "graph.h"
#include "midi.h"

#define BD_MAX_BLOCKS	PL_MAX_NODES
#define BD_MAX_NAM	8		// simultaneous NAM instances (about 21% of the period at 4 frames each at 1500 MHz: with the 75% limit, 3 fit (5 with CPU_BOOST))
#define BD_LOAD_LIMIT10	750		// ADELGrid: above the estimated 75% no blocks are added or turned on, and a grid saved like that does not load (5 NAMs + 10 effects took the board off the network twice)
#define BD_LOAD_MSG	"estimated processor load too high: remove a block or turn one off"
#define BD_FILE_LEN	48
#define BD_PRESET_LEN	32		// name of the saved setting linked to the block

struct TBdBlock
{
	boolean Used;
	u8 Type;		// TNodeKind
	boolean On;
	int A, B;		// Grid: row and column of the cell
	float V[FX_MAX_P];		// NAM: V0 = input dB, V1 = output dB, V2/V3/V4 = bass/mid/treble dB (0 = flat) | Volume: V0 = dB | Pan: V0 = position
	char Preset[BD_PRESET_LEN];	// saved setting in use (empty = none); the page compares it with the values
	char File[BD_FILE_LEN];		// NAM: model file in the library
	int Slot;		// NAM: assigned instance
	// model state (written by the main loop)
	boolean Loaded;
	char LoadedFile[BD_FILE_LEN];
	char FailedFile[BD_FILE_LEN];
	char Name[64];		// internal name of the loaded model
	char Status[64];	// "ok" or the reason for the failure
	float BaseIn, BaseOut;	// model calibration, linear
	boolean HasModelIn;	// the model has the send level (input_level_dbu): needed to recompute BaseIn when the calibration changes
	float ModelInDbu;	// input_level_dbu of the model
};

// links between blocks; the blocks use A = row and B = column (Grid) or B = position in the list (Matrix, only in tests)
#define BD_MAX_LINKS	96
#define BD_GRID_ROWS	6
#define BD_GRID_COLS	12		// 12 columns (it was 10): inputs and outputs sit at the two ends with room for the arrow labels
#define BD_NET_IN	(-1)		// source: input of the pedalboard
#define BD_NET_OUT	(-2)		// destination: output of the pedalboard (port 1 = Output L, 2 = Output R); from BD_NET_IN the starting port is 1 = Input L, 2 = Input R

struct TBdLink
{
	int From;		// id of the block or BD_NET_IN
	int To;			// id of the block or BD_NET_OUT
	u8 PF;			// source port: 0 = stereo, 1 = L, 2 = R
	u8 PT;			// destination port
	float Db;
};

class CBoard
{
public:
	CBoard (void);

	void Reset (void);					// no blocks, no links

	// page operations: they return 0 if they succeed, otherwise the reason (in English)
	const char *SetValue (int nId, const char *pKey, float fValue);
	const char *SetPreset (int nId, const char *pName);	// empty name = no linked setting
	const char *SetFile (int nId, const char *pFile);	// the name must already have been checked

	// blocks and links
	const char *NetAdd (const char *pType, int nRow, int nCol, const char *pDefaultFile, int *pNewId);	// Grid: free cell (row, column); Matrix: at the end of the list
	const char *NetRemove (int nId);
	const char *NetRetype (int nId, const char *pType, const char *pDefaultFile);	// changes the type staying in the same cell and with the same links
	const char *NetMove (int nId, int nRow, int nCol);		// Grid: another free cell (the links must keep going to the right); Matrix: new position
	const char *NetLink (int nFrom, int nTo, int nPf, int nPt, float fDb);	// adds or updates the link
	const char *NetUnlink (int nFrom, int nTo, int nPf, int nPt);
	// Grid: atomic automatic reordering. Positions (row, column) of ALL the used blocks (once each) and of the four inputs/outputs; it changes neither links nor parameters.
	// Rejected (leaving everything as it was) if a cell is doubled or outside the grid, or if with the new positions a link would go to the left.
	const char *NetRelayout (const int *pId, const int *pRow, const int *pCol, unsigned nBlocks, const int *pIoRow, const int *pIoCol);
	const char *NetMoveIo (int nWhich, int nRow, int nCol);	// Grid: moves Input L (0), Input R (1), Output L (2) or Output R (3) to another free cell
	unsigned LinkCount (void) const { return m_nLink; }
	const TBdLink &Link (unsigned n) const { return m_Link[n]; }
	int IoRow (unsigned nWhich) const { return m_IoRow[nWhich]; }	// Grid: position of Input L (0), Input R (1), Output L (2), Output R (3)
	int IoCol (unsigned nWhich) const { return m_IoCol[nWhich]; }

	TBdBlock &Block (unsigned n) { return m_Block[n]; }
	const TBdBlock &Block (unsigned n) const { return m_Block[n]; }

	// document mode: 1 = ADELMatrix (tests only), 2 = ADELGrid (fixed for the whole life of the object)
	void SetMode (unsigned nMode) { m_nMode = nMode; }
	unsigned GetMode (void) const { return m_nMode; }

	// MIDI (phase 2): the map lives in the document and therefore in the presets
	CMidiMap &Midi (void) { return m_Midi; }
	static unsigned MidiClassOfType (u8 nType);			// class of the fixed CCs (TMidiClass) of a block type, 0 = none; the modulation blocks are McModPre
	int MidiFindClass (unsigned nClass) const;			// ADELGrid: id of the leftmost (then topmost) block of a class, -1 if there is none; modulation to the left of the first NAM = McModPre, to the right = McModPost
	unsigned MidiClassMask (void) const;				// bit n-1 is set if the first block of class n exists and is on
	const char *SetParamNorm (int nId, unsigned nParam, float f01);	// normalized value 0..1 in the parameter's range (NAM: 0 = input, 1 = output, 2 = bass, 3 = mid, 4 = treble; Volume and Pan: parameter 0)
	unsigned CountNam (void) const;
	unsigned EstimateLoad10 (void) const;			// estimated processor load in tenths of a % (blocks that are on; measured: 2 NAM + compressor + springs 51%, each NAM 21%)
	static unsigned NodeCost10 (u8 nType);
	static void SetCores (unsigned nCores);			// processor cores available to the engine (1 = core 0 only, as always)
	static unsigned Cores (void);
	static void SetInTrim (float fDb);			// input trim of the active guitar profile (dB), applied by the plan before all the blocks
	static void SetCal (float fMaxInDbu, float fOffsetDb);		// NAM calibration: maximum input level (dBu of 0 dBFS) and offset; the block can exclude the offset (V5)
	static void SetCpuMhz (unsigned nMhz);		// processor frequency: the block costs are measured at 1500 MHz and scale with the frequency
	unsigned GetVersion (void) const { return m_nVersion; }		// changes with every modification
	unsigned GetStructVersion (void) const { return m_nStruct; }
	unsigned GetEdit (void) const { return m_nEdit; }		// counts the user's modifications (not the model loads)	// changes when the structure changes (blocks, lanes, order)
	boolean TakeDirty (void) { boolean b = m_bDirty; m_bDirty = FALSE; return b; }	// the plan must be rebuilt
	boolean TakeSaveDirty (void) { boolean b = m_bSaveDirty; m_bSaveDirty = FALSE; return b; }
	void MarkDirty (void) { m_bDirty = TRUE; m_nVersion++; }	// e.g. a model was loaded
	void MarkSave (void) { m_bSaveDirty = TRUE; }
	void MarkEdit (void) { m_nEdit++; m_bSaveDirty = TRUE; }		// modification that does not change the audio plan (MIDI map): the preset is marked as modified and gets saved

	// plan for the audio callback; ppInst[slot] = NAM instance of the slot
	// ppFx[id] = effect instance of block id (may be 0)
	void Compile (TPlan *pPlan, adelnam::Instance *const *ppInst, float fMaster, boolean bBypass, CFx *const *ppFx = 0) const;

	// saving (text, one line per element)
	unsigned Serialize (char *pBuffer, unsigned nSize) const;
	boolean Parse (const char *pText);		// FALSE if invalid (the pedalboard stays as it was)

	// preset change without interruption: the new document is prepared in a second CBoard (Parse), its NAMs go into the slots that the document in use does not occupy
	// (pBusy[slot] = TRUE if occupied), then it is adopted at once. RemapNamSlots returns FALSE (changing nothing) if the free slots are not enough.
	boolean RemapNamSlots (const boolean *pBusy);
	void Adopt (const CBoard &Other);		// takes the content of Other as if it had just been read from a preset (Parse)

private:
	int NewBlock (void);
	void FreeBlock (int nId);
	void Touch (boolean bStruct);
	boolean ValidateNet (void) const;
	boolean NetReaches (int nFrom, int nTarget) const;	// is there a path of links from nFrom to nTarget?
	int NetCell (int nRow, int nCol) const;			// Grid: id of the block in the cell, -1 if free
	boolean NetCellFree (int nRow, int nCol) const;		// Grid: cell with no blocks and no IN/OUT
	void SelectorRanks (int *pRank) const;			// for every link leaving a Selector: its position among the branches (from the position of the destinations); -1 for the others
	int NetColE (int nId, int nPort) const;			// column (Grid) of a block, or of the IN (-1) / OUT (-2) entity with that port (1 = L, 2 = R)
	boolean NetForward (int nFrom, int nPf, int nTo, int nPt) const;	// Grid: the link goes to a column on the right
	boolean NetLinkOnIo (const TBdLink &K, int nWhich) const;	// the link starts or ends at entity nWhich (0..3)
	void NetBuild (TPlan *pPlan) const;			// links and calculation order in the plan
	int NetNewBlock (const char *pType, const char *pDefaultFile, const char **ppErr);
	static const char *TypeFromName (const char *pType, u8 *pType8);

private:
	CMidiMap m_Midi;
	TBdBlock m_Block[BD_MAX_BLOCKS];
	TBdLink m_Link[BD_MAX_LINKS];
	unsigned m_nLink;
	int m_IoRow[4];		// Grid: cells of Input L, Input R, Output L, Output R
	int m_IoCol[4];
	boolean m_bSlot[BD_MAX_NAM];
	unsigned m_nVersion;
	unsigned m_nStruct;
	unsigned m_nEdit;
	boolean m_bDirty;
	boolean m_bSaveDirty;
	unsigned m_nMode;
};

boolean BoardValidFileName (const char *pName);
boolean BoardValidIrName (const char *pName);	// NAME.air (cabinet IR)

#endif
