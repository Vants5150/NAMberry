//
// graph.h - graph engine (ADELGrid): blocks (NAM, volume, effects, ...) linked together, with L/R inputs and outputs.
//
// The "plan" (TPlan) is a compact copy, immutable during use: the main loop (board.cpp) builds it in an
// inactive buffer and swaps it in with a single pointer store; the audio callback reads it without allocations or locks.
// Circle uses a single core: the callback (IRQ) interrupts the main loop but not vice versa.
//
#ifndef _graph_h
#define _graph_h

#include <circle/types.h>
#include "adelnam_engine.hpp"

#define PL_MAX_NODES	40
#define PL_MAX_BLOCK	64
#define GR_MAX_CORES	4		// processor cores usable by the engine (the audio callback runs on core 0)
#define PL_MAX_LINKS	160		// links of the network mode (Matrix and Grid)
#define PL_NET_IN	(-1)		// source of the links: the pedalboard input
#define PL_NET_OUT	PL_MAX_NODES	// destination of the links: the output (index after the last node)
#define FX_MAX_P	64		// values per block (parameters of the ADEL plugins: up to 41 plus the outputs)		// maximum frames per audio block

enum TNodeKind
{
	NodeNone = 0,
	NodeNam = 1,
	NodeVolume = 2,
	NodePan = 4		// (3 was ADELFlow's Split, it no longer exists) // balance / position in the stereo field (mono or stereo -> stereo)
	// 5..8 = effects (fx_defs.h): phaser, reverb, delay, compressor
};

class CFx;

struct TPNode
{
	u8 Kind;
	u8 On;			// 0 = excluded (the signal passes unchanged)
	u8 Sel;			// NAM: mask of the active Bass/Mid/Treble filters (namtone.h)
	adelnam::Instance *pInst;	// NAM: loaded and warmed-up instance (0 = not ready: the block passes the signal)
	float In, Out;		// NAM: linear gains (model calibration x sliders)
	float Gain;		// Volume
	CFx *pFx;		// effect: instance with its state (0 = not ready: the block passes the signal)
	float P[FX_MAX_P];		// effect: parameters in the units of fx_defs.h
};

// network link: from a node's output (or from the pedalboard input) to a node's input (or to the output)
// ports: 0 = stereo (L and R), 1 = L only, 2 = R only
struct TPLink
{
	short From;		// id of the source node or PL_NET_IN
	u8 PF;			// source port
	u8 PT;			// destination port
	float Gain;		// linear level
};

struct TPlan
{
	TPNode Node[PL_MAX_NODES];
	// the nodes are linked by links. Order = calculation order (topological); the links are
	// grouped by destination: those of node n are Link[LFirst[n] .. LFirst[n+1]-1], those of the output come last
	short NOrder;
	short Order[PL_MAX_NODES];
	u8 Skip[PL_MAX_NODES];		// 1 = the node is not processed (silence at the input or output that does not reach any output): its output is zero
	short LFirst[PL_MAX_NODES + 2];
	TPLink Link[PL_MAX_LINKS];
	float Master;		// linear output gain
	float InTrim;		// linear input gain MINUS 1 (0 = no trim): active guitar profile; this way a zeroed plan stays at gain 1
	u8 NCores;		// cores used by this plan (<= 1: everything on core 0, as always)
	u8 Core[PL_MAX_NODES];	// core that computes each node (only if NCores > 1)
	u8 Bypass;		// 1 = input L -> output L and R -> R, without blocks
};

// processes a block of nFrames frames (<= PL_MAX_BLOCK) from the plan; pOutL/pOutR are always written
void GraphProcess (const TPlan *pPlan, const float *pInL, const float *pInR, float *pOutL, float *pOutR, unsigned nFrames);
// peaks (ppm of full scale) of the L and R output of the first nNodes nodes since the last call, in the order L0 R0 L1 R1 ...; it resets the maxima (2 * nNodes values are needed)
void GraphNodePeaks (unsigned *pPpm, unsigned nNodes);

// multi-core: cores 1..n-1 run GraphWorker(core) (called inside CMultiCoreSupport::Run); core 0 enables the multi-core plan with GraphMcEnable.
// If a core does not respond within 4 ms (a block lasts 83 us) the multi-core disables itself (GraphMcFaults > 0) and the engine goes back to core 0;
// blocks that last more than one period are counted (GraphMcLate) and the maximum can be read with GraphMcMaxUs.
void GraphWorker (unsigned nCore);
void GraphMcEnable (unsigned nCores);		// after the workers have started; 1 = off
unsigned GraphMcCores (void);
unsigned GraphMcFaults (void);
void GraphMcSetTimeout (unsigned nTicks);	// maximum wait time between cores in 54 MHz ticks (default 216000 = 4 ms); the PC tests lengthen it
unsigned GraphMcBlocks (void);
unsigned GraphMcLate (void);		// blocks with several cores whose processing exceeded 167 us
unsigned GraphMcMaxUs (void);	// longest wait observed by core 0 in a block (microseconds)			// blocks processed with several cores

// ---- queues (preset change without interruption) ----
// When you switch to another preset the old plan is no longer the active plan, but a copy of it ("ghost") keeps running with zero input
// and with ITS effect instances: delays, reverbs and springs finish their tail and add it to the new plan's output. The ghost's NAMs receive
// the input with a 4 ms fade (no jump at the change) and are then excluded (they cost too much to run idle).
#define GH_RUN		1
#define GH_DONE		2		// finished: the main loop can free the effect instances
#define GH_RAMP_FRAMES	192		// fade of the ghost's input and of the new plan's entry (4 ms at 48 kHz)
#define GH_KILL_FRAMES	240		// fade-out of a ghost replaced by a more recent one
#define GH_QUIET_PEAK	1.0e-6f		// below this peak (-120 dBFS) the output is considered finished
#define GH_QUIET_FRAMES	96000		// 2 s of silence in a row (at 48 kHz)
#define GH_MAX_FRAMES	960000		// maximum lifetime: 20 s

class CFx;

struct TGhost
{
	TPlan Plan;		// copy of the old plan
	CFx *Fx[PL_MAX_NODES];	// effect instances of the old plan (of the main loop: it frees them when State == GH_DONE)
	volatile u32 State;
	volatile u32 NamLive;	// 1 while the ghost's NAMs run (their instances must not be rewritten)
	volatile u32 Kill;	// 1 = replaced by a more recent ghost: it fades and ends
	u32 Seq;		// sequence number (assigned by the main loop)
	u32 RampLeft, RampTotal, KillLeft, Quiet, Age;
};

void GraphGhostInit (TGhost *pG);		// after Plan and Fx have been filled: starts the fade and the GH_RUN state
// processes a block of the ghost (core 0 only): pOutL/pOutR are always written (silence if it is not running)
void GraphGhostProcess (TGhost *pG, const float *pInL, const float *pInR, float *pOutL, float *pOutR, unsigned nFrames);

// forgets the current link levels: at the next block the plan's ones apply immediately (used by the tests)
void GraphResetSmoothing (void);

#endif
