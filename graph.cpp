//
// graph.cpp - processing of the plan in the audio callback (no allocation, no math library)
//
// Every lane carries a STEREO PAIR (L, R) and a "mono" flag (L and R identical). The mono blocks (NAM) on a mono
// path process a single channel; on a stereo path they process the sum (L+R)/2 and put the same signal back on L and R.
// The stereo blocks (Pan, in the future reverb and delay) produce a stereo path.
//
#include "graph.h"
#include "config.h"
#include "fx.h"
#include "namtone.h"

// work buffer of the NAM nodes (the callback is not reentrant)
static float s_A[GR_MAX_CORES][PL_MAX_BLOCK];	// NAM input (one per core)
static float s_B[GR_MAX_CORES][PL_MAX_BLOCK];	// NAM output
static float s_Tz[2][PL_MAX_NODES][NT_STAGES][2];		// state of the Bass/Mid/Treble filters of every NAM node (it stays between one plan and the next); [1] = ghost
static unsigned s_nGh = 0;		// 1 while a ghost runs (always on core 0, with the workers stopped): no level fade, no peaks

// processes node 'nIdx' in place: (pL, pR) goes in and out; *pMono = L and R identical
static void RunNode (const TPlan *pPlan, int nIdx, float *pL, float *pR, int *pMono, unsigned n, unsigned nCore)
{
	const TPNode &N = pPlan->Node[nIdx];
	if (!N.On)
	{
		return;
	}
	switch (N.Kind)
	{
	case NodeNam:
	{
		float *pTmp = s_A[nCore];
		float *pRes = s_B[nCore];
		const float fIn = N.In;
		if (N.pInst == 0)
		{
			// model not ready (loading or failed): the block is silent
			for (unsigned j = 0; j < n; j++)
			{
				pL[j] = 0.0f;
				pR[j] = 0.0f;
			}
			*pMono = 1;
			break;
		}
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++)
			{
				pTmp[j] = pL[j] * fIn;
			}
		}
		else
		{
			for (unsigned j = 0; j < n; j++)
			{
				pTmp[j] = (pL[j] + pR[j]) * (0.5f * fIn);
			}
		}
		N.pInst->Process (pTmp, pRes, (int) n);
		NamToneRun (N.P, N.Sel, s_Tz[s_nGh][nIdx], pRes, n);		// Bass/Mid/Treble (mask 0 = all flat: it only zeroes the state)
		const float fOut = N.Out;
		for (unsigned j = 0; j < n; j++)
		{
			const float y = pRes[j] * fOut;
			pL[j] = y;
			pR[j] = y;
		}
		*pMono = 1;
		break;
	}

	case NodeVolume:
	{
		const float fGain = N.Gain;
		for (unsigned j = 0; j < n; j++)
		{
			pL[j] *= fGain;
			pR[j] *= fGain;
		}
		break;
	}

	case NodePan:
	{
		// from mono or stereo to stereo: left and right gains (constant-power law)
		const float fGl = N.In, fGr = N.Out;
		for (unsigned j = 0; j < n; j++)
		{
			pL[j] *= fGl;
			pR[j] *= fGr;
		}
		*pMono = 0;
		break;
	}

	default:
		if (N.Kind >= FxFirst && N.Kind <= FxLast && N.pFx != 0)
		{
			N.pFx->Process (pL, pR, n, pMono, N.P);
		}
		break;
	}
}

// ---- network of blocks (ADELGrid) ----
// Every node has an input (the sum of the incoming links, each with its own level) and an output (stereo pair + mono flag).
// The nodes are computed in the plan's topological order. A node with no incoming links receives silence.
static float s_NL[PL_MAX_NODES][PL_MAX_BLOCK];		// left output of every node
static float s_NR[PL_MAX_NODES][PL_MAX_BLOCK];
static unsigned char s_NMono[PL_MAX_NODES];
static volatile unsigned s_NPk[PL_MAX_NODES][2];	// peak (ppm of full scale) of the L and R output of every node since the last read; every node runs on a single core

// link levels with a fade of about 3 ms (coefficient 0.03 per 83 us block): changing the branch of a Selector or touching up a level makes no jumps.
// The state is per position in the plan: if in that position there is a different link (rebuilt plan) the level immediately takes the new value.
static float s_LG[PL_MAX_LINKS];
static unsigned s_LSig[PL_MAX_LINKS];

void GraphResetSmoothing (void)
{
	for (unsigned q = 0; q < PL_MAX_LINKS; q++)
	{
		s_LSig[q] = 0;
	}
}

static float LinkGain (unsigned q, const TPLink &K, unsigned nDst)
{
	if (s_nGh)
	{
		return K.Gain;		// the ghost has fixed levels (its state must not disturb that of the real plan)
	}
	const unsigned nSig = ((unsigned) (K.From + 2) << 20) | ((unsigned) K.PF << 18) | ((unsigned) K.PT << 16) | (nDst + 2);
	if (s_LSig[q] != nSig)
	{
		s_LSig[q] = nSig;
		s_LG[q] = K.Gain;
		return K.Gain;
	}
	const float d = K.Gain - s_LG[q];
	if (d > -1e-5f && d < 1e-5f)
	{
		s_LG[q] = K.Gain;
		return K.Gain;
	}
	s_LG[q] += 0.03f * d;
	return s_LG[q];
}

// adds link K into the destination buffer: sl/sr = source output (sm = mono); *pMono drops to 0 if the result is no longer mono
static void NetAdd (const TPLink &K, const float *sl, const float *sr, int sm, float *dl, float *dr, unsigned n, int *pMono)
{
	const float g = K.Gain;
	if (K.PF == 0)
	{
		if (K.PT == 0)
		{
			for (unsigned j = 0; j < n; j++)
			{
				dl[j] += sl[j] * g;
				dr[j] += sr[j] * g;
			}
			if (!sm)
			{
				*pMono = 0;
			}
		}
		else
		{
			// from stereo to a single port: sum of the two channels (if the source is mono, its channel)
			float *d = K.PT == 1 ? dl : dr;
			for (unsigned j = 0; j < n; j++)
			{
				d[j] += (sm ? sl[j] : (sl[j] + sr[j]) * 0.5f) * g;
			}
			*pMono = 0;
		}
	}
	else
	{
		const float *s = K.PF == 1 ? sl : sr;
		if (K.PT == 0)
		{
			// one port onto both channels: it stays mono
			for (unsigned j = 0; j < n; j++)
			{
				dl[j] += s[j] * g;
				dr[j] += s[j] * g;
			}
		}
		else
		{
			float *d = K.PT == 1 ? dl : dr;
			for (unsigned j = 0; j < n; j++)
			{
				d[j] += s[j] * g;
			}
			*pMono = 0;
		}
	}
}

// computes a node: sum of the incoming links, then the block (state of the node's s_NL/s_NR/s_NMono)
static void NodeStep (const TPlan *pPlan, int id, const float *pInL, const float *pInR, int nInMono, unsigned n, unsigned nCore)
{
	float *pL = s_NL[id], *pR = s_NR[id];
	for (unsigned j = 0; j < n; j++)
	{
		pL[j] = 0.0f;
		pR[j] = 0.0f;
	}
	if (pPlan->Skip[id])
	{
		s_NMono[id] = 1;
		return;
	}
	int nMono = 1;
	for (int q = pPlan->LFirst[id]; q < pPlan->LFirst[id + 1] && q < PL_MAX_LINKS; q++)
	{
		TPLink K = pPlan->Link[q];
		K.Gain = LinkGain ((unsigned) q, K, (unsigned) id);
		if (K.Gain == 0.0f)
		{
			continue;
		}
		if (K.From == PL_NET_IN)
		{
			NetAdd (K, pInL, pInR, nInMono, pL, pR, n, &nMono);
		}
		else if (K.From >= 0 && K.From < PL_MAX_NODES)
		{
			NetAdd (K, s_NL[K.From], s_NR[K.From], s_NMono[K.From], pL, pR, n, &nMono);
		}
	}
	RunNode (pPlan, id, pL, pR, &nMono, n, nCore);
	s_NMono[id] = (unsigned char) (nMono ? 1 : 0);
	// peak of the node's output (for the page's level indicators): the maximum stays until the next read
	if (!s_nGh)
	{
		float fL = 0.0f, fR = 0.0f;
		for (unsigned j = 0; j < n; j++)
		{
			const float a = pL[j] < 0.0f ? -pL[j] : pL[j];
			const float b = pR[j] < 0.0f ? -pR[j] : pR[j];
			fL = a > fL ? a : fL;
			fR = b > fR ? b : fR;
		}
		fL = fL > 1000.0f ? 1000.0f : fL;
		fR = fR > 1000.0f ? 1000.0f : fR;
		const unsigned uL = (unsigned) (fL * 1.0e6f), uR = (unsigned) (fR * 1.0e6f);
		if (uL > s_NPk[id][0])
		{
			s_NPk[id][0] = uL;
		}
		if (uR > s_NPk[id][1])
		{
			s_NPk[id][1] = uR;
		}
	}
}

void GraphNodePeaks (unsigned *pPpm, unsigned nNodes)
{
	for (unsigned i = 0; i < nNodes && i < PL_MAX_NODES; i++)
	{
		pPpm[2 * i] = s_NPk[i][0];
		pPpm[2 * i + 1] = s_NPk[i][1];
		s_NPk[i][0] = 0;
		s_NPk[i][1] = 0;
	}
}

// sum of the links towards the output
static void OutStep (const TPlan *pPlan, const float *pInL, const float *pInR, int nInMono, float *pOutL, float *pOutR, int *pOutMono, unsigned n)
{
	for (unsigned j = 0; j < n; j++)
	{
		pOutL[j] = 0.0f;
		pOutR[j] = 0.0f;
	}
	int nMono = 1;
	for (int q = pPlan->LFirst[PL_NET_OUT]; q < pPlan->LFirst[PL_NET_OUT + 1] && q < PL_MAX_LINKS; q++)
	{
		TPLink K = pPlan->Link[q];
		K.Gain = LinkGain ((unsigned) q, K, (unsigned) PL_NET_OUT);
		if (K.Gain == 0.0f)
		{
			continue;
		}
		if (K.From == PL_NET_IN)
		{
			NetAdd (K, pInL, pInR, nInMono, pOutL, pOutR, n, &nMono);
		}
		else if (K.From >= 0 && K.From < PL_MAX_NODES)
		{
			NetAdd (K, s_NL[K.From], s_NR[K.From], s_NMono[K.From], pOutL, pOutR, n, &nMono);
		}
	}
	*pOutMono = nMono;
}

// (pInL, pInR, nInMono) = pedalboard input; (pOutL, pOutR, *pOutMono) = sum of the links towards the output
static void RunNet (const TPlan *pPlan, const float *pInL, const float *pInR, int nInMono, float *pOutL, float *pOutR, int *pOutMono, unsigned n)
{
	int nOrder = pPlan->NOrder;
	if (nOrder > PL_MAX_NODES)
	{
		nOrder = PL_MAX_NODES;
	}
	for (int k = 0; k < nOrder; k++)
	{
		const int id = pPlan->Order[k];
		if (id < 0 || id >= PL_MAX_NODES)
		{
			continue;
		}
		NodeStep (pPlan, id, pInL, pInR, nInMono, n, 0);
	}
	OutStep (pPlan, pInL, pInR, nInMono, pOutL, pOutR, pOutMono, n);
}

// ---- several cores ----
// Every node has its core (TPlan::Core). Every core walks the calculation order and does its own nodes; before a node it waits for the source nodes of the other cores
// to finish the current block (block number in s_Done). No queue and no lock: only wait loops on shared variables.
#define MC_SPIN_BEFORE_SLEEP	400		// busy-wait rounds before sleeping
static volatile unsigned s_nMcTimeout = 216000;		// 4 ms at 54 MHz: beyond that, multi-core turns off (it is a safety net against hangs, not a performance limit)
#define MC_LATE_TICKS	9000				// 167 us: beyond two block periods
static volatile u32 s_nMcLate = 0;
static volatile u32 s_nMcMaxTicks = 0;
#define MC_TIMEOUT_TICKS	s_nMcTimeout

struct TMcJob
{
	alignas (64) volatile u32 Seq;			// block number (starts from 1)
	const TPlan *Plan;
	unsigned N;
	unsigned Cores;
	alignas (64) volatile u32 Done[GR_MAX_CORES][16];	// one core per cache line: last finished block
};
static TMcJob s_Job;
static volatile u32 s_NodeDone[PL_MAX_NODES];
static float s_McInL[PL_MAX_BLOCK], s_McInR[PL_MAX_BLOCK];
static unsigned s_nMcCores = 1;
static volatile u32 s_nMcFaults = 0;
static volatile u32 s_nMcBlocks = 0;

#if defined (__aarch64__)
static inline u64 McTicks (void)
{
	u64 nTicks;
	asm volatile ("isb; mrs %0, cntvct_el0" : "=r" (nTicks));
	return nTicks;
}
#else
// tests on the PC: 54 ticks per microsecond, like the Pi's counter
#include <chrono>
static inline u64 McTicks (void)
{
	return (u64) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count () * 54 / 1000;
}
#endif

// waits for the source nodes of 'id' computed by other cores; FALSE if the time runs out or the block is overtaken
static boolean McWaitDeps (const TPlan *pPlan, int id, u32 nSeq, unsigned nCore, u64 nStart)
{
	for (int q = pPlan->LFirst[id]; q < pPlan->LFirst[id + 1] && q < PL_MAX_LINKS; q++)
	{
		const int nFrom = pPlan->Link[q].From;
		if (nFrom < 0 || nFrom >= PL_MAX_NODES || pPlan->Core[nFrom] == nCore)
		{
			continue;
		}
		while (__atomic_load_n (&s_NodeDone[nFrom], __ATOMIC_ACQUIRE) != nSeq)
		{
			if (McTicks () - nStart > MC_TIMEOUT_TICKS || __atomic_load_n (&s_nMcFaults, __ATOMIC_RELAXED) != 0)
			{
				return FALSE;
			}
		}
	}
	return TRUE;
}

static void McRunCore (const TPlan *pPlan, unsigned nCore, u32 nSeq, unsigned n, u64 nStart)
{
	int nOrder = pPlan->NOrder;
	if (nOrder > PL_MAX_NODES)
	{
		nOrder = PL_MAX_NODES;
	}
	for (int k = 0; k < nOrder; k++)
	{
		const int id = pPlan->Order[k];
		if (id < 0 || id >= PL_MAX_NODES || pPlan->Core[id] != nCore)
		{
			continue;
		}
		if (!McWaitDeps (pPlan, id, nSeq, nCore, nStart))
		{
			__atomic_store_n (&s_nMcFaults, 1, __ATOMIC_RELAXED);
			return;
		}
		NodeStep (pPlan, id, s_McInL, s_McInR, 0, n, nCore);
		__atomic_store_n (&s_NodeDone[id], nSeq, __ATOMIC_RELEASE);
	}
}

void GraphWorker (unsigned nCore)
{
	if (nCore == 0 || nCore >= GR_MAX_CORES)
	{
		return;
	}
	u32 nLast = 0;
	for (;;)
	{
		u32 nSeq;
		unsigned nSpin = 0;
		while ((nSeq = __atomic_load_n (&s_Job.Seq, __ATOMIC_ACQUIRE)) == nLast)
		{
#if defined (__aarch64__)
			// after a bit of idle waiting the core sleeps until the next event (core 0 sends it at every block): less heat and consumption
			if (++nSpin > MC_SPIN_BEFORE_SLEEP)
			{
				asm volatile ("wfe");
			}
#endif
		}
		nLast = nSeq;
		if (nCore < s_Job.Cores)
		{
			McRunCore (s_Job.Plan, nCore, nSeq, s_Job.N, McTicks ());
		}
		__atomic_store_n (&s_Job.Done[nCore][0], nSeq, __ATOMIC_RELEASE);
	}
}

void GraphMcSetTimeout (unsigned nTicks)
{
	s_nMcTimeout = nTicks;
}

void GraphMcEnable (unsigned nCores)
{
	s_nMcCores = nCores < 1 ? 1 : (nCores > GR_MAX_CORES ? GR_MAX_CORES : nCores);
}

unsigned GraphMcCores (void)
{
	return s_nMcCores;
}

unsigned GraphMcFaults (void)
{
	return s_nMcFaults;
}

unsigned GraphMcBlocks (void)
{
	return s_nMcBlocks;
}

unsigned GraphMcLate (void)
{
	return s_nMcLate;
}

unsigned GraphMcMaxUs (void)
{
	return s_nMcMaxTicks / 54;
}

// the block with several cores: input copied, work published, core 0 does its nodes and waits for the others, then sums the outputs
static void RunNetMc (const TPlan *pPlan, const float *pInL, const float *pInR, float *pOutL, float *pOutR, int *pOutMono, unsigned n)
{
	for (unsigned j = 0; j < n; j++)
	{
		s_McInL[j] = pInL[j];
		s_McInR[j] = pInR[j];
	}
	unsigned nCores = pPlan->NCores;
	if (nCores > s_nMcCores)
	{
		nCores = s_nMcCores;
	}
	static u32 nSeq = 0;
	nSeq++;
	const u64 nStart = McTicks ();
	s_Job.Plan = pPlan;
	s_Job.N = n;
	s_Job.Cores = nCores;
	__atomic_store_n (&s_Job.Seq, nSeq, __ATOMIC_RELEASE);
#if defined (__aarch64__)
	asm volatile ("dsb ish\n\tsev" ::: "memory");		// wakes the sleeping cores
#endif
	McRunCore (pPlan, 0, nSeq, n, nStart);
	for (unsigned c = 1; c < nCores; c++)
	{
		while (__atomic_load_n (&s_Job.Done[c][0], __ATOMIC_ACQUIRE) != nSeq)
		{
			if (McTicks () - nStart > MC_TIMEOUT_TICKS)
			{
				__atomic_store_n (&s_nMcFaults, 1, __ATOMIC_RELAXED);
				break;
			}
		}
	}
	s_nMcBlocks++;
	{
		const u32 nElapsed = (u32) (McTicks () - nStart);
		if (nElapsed > MC_LATE_TICKS)
		{
			s_nMcLate++;
		}
		if (nElapsed > s_nMcMaxTicks)
		{
			s_nMcMaxTicks = nElapsed;
		}
	}
	OutStep (pPlan, s_McInL, s_McInR, 0, pOutL, pOutR, pOutMono, n);
}

void GraphProcess (const TPlan *pPlan, const float *pInL, const float *pInR, float *pOutL, float *pOutR, unsigned n)
{
	const float fMaster = pPlan->Master;
	if (pPlan->Bypass)
	{
		// L stays on L and R on R
		for (unsigned j = 0; j < n; j++)
		{
			pOutL[j] = pInL[j] * fMaster;
			pOutR[j] = pInR[j] * fMaster;
		}
		return;
	}

	// IN and OUT are blocks with L and R ports: input and output are always stereo (L on L, R on R)
	static float InL[PL_MAX_BLOCK], InR[PL_MAX_BLOCK];
	const float fIn = 1.0f + pPlan->InTrim;		// guitar profile trim, before all the blocks
	for (unsigned j = 0; j < n; j++)
	{
		InL[j] = pInL[j] * fIn;
		InR[j] = pInR[j] * fIn;
	}
	int nMono = 0;
	if (pPlan->NCores > 1 && s_nMcCores > 1 && s_nMcFaults == 0)
	{
		RunNetMc (pPlan, InL, InR, pOutL, pOutR, &nMono, n);
	}
	else
	{
		RunNet (pPlan, InL, InR, 0, pOutL, pOutR, &nMono, n);
	}
	for (unsigned j = 0; j < n; j++)
	{
		pOutL[j] *= fMaster;
		pOutR[j] *= fMaster;
	}
}

// ---- code ----

void GraphGhostInit (TGhost *pG)
{
	pG->NamLive = 1;
	pG->Kill = 0;
	pG->RampTotal = GH_RAMP_FRAMES;
	pG->RampLeft = GH_RAMP_FRAMES;
	pG->KillLeft = GH_KILL_FRAMES;
	pG->Quiet = 0;
	pG->Age = 0;
	pG->State = GH_RUN;
}

void GraphGhostProcess (TGhost *pG, const float *pInL, const float *pInR, float *pOutL, float *pOutR, unsigned n)
{
	for (unsigned j = 0; j < n; j++)
	{
		pOutL[j] = 0.0f;
		pOutR[j] = 0.0f;
	}
	if (pG->State != GH_RUN)
	{
		return;
	}
	TPlan *pPlan = &pG->Plan;
	if (pPlan->Bypass || n > PL_MAX_BLOCK)
	{
		pG->State = GH_DONE;
		return;
	}

	// input with the fade (then zero)
	static float GInL[PL_MAX_BLOCK], GInR[PL_MAX_BLOCK];
	for (unsigned j = 0; j < n; j++)
	{
		float g = 0.0f;
		if (pG->RampLeft > 0)
		{
			g = (float) pG->RampLeft / (float) pG->RampTotal;
			pG->RampLeft--;
		}
		GInL[j] = pInL[j] * g * (1.0f + pPlan->InTrim);
		GInR[j] = pInR[j] * g * (1.0f + pPlan->InTrim);
	}

	int nMono = 0;
	s_nGh = 1;
	if (pPlan->NCores > 1 && s_nMcCores > 1 && s_nMcFaults == 0)
	{
		RunNetMc (pPlan, GInL, GInR, pOutL, pOutR, &nMono, n);		// same distribution over the cores as the old plan: the NAMs stay where they were (no cache migration)
	}
	else
	{
		RunNet (pPlan, GInL, GInR, 0, pOutL, pOutR, &nMono, n);
	}
	s_nGh = 0;

	if (pG->RampLeft == 0 && pG->NamLive)
	{
		// from here the input is zero: the NAMs fall silent (and their instances become free again for another preset)
		for (unsigned i = 0; i < PL_MAX_NODES; i++)
		{
			if (pPlan->Node[i].Kind == NodeNam)
			{
				pPlan->Node[i].pInst = 0;
			}
		}
		pG->NamLive = 0;
	}

	const float fMaster = pPlan->Master;
	float fPeak = 0.0f;
	for (unsigned j = 0; j < n; j++)
	{
		float g = fMaster;
		if (pG->Kill)
		{
			g *= (float) pG->KillLeft / (float) GH_KILL_FRAMES;
			if (pG->KillLeft > 0)
			{
				pG->KillLeft--;
			}
		}
		pOutL[j] *= g;
		pOutR[j] *= g;
		const float a = pOutL[j] < 0.0f ? -pOutL[j] : pOutL[j];
		const float b = pOutR[j] < 0.0f ? -pOutR[j] : pOutR[j];
		fPeak = a > fPeak ? a : fPeak;
		fPeak = b > fPeak ? b : fPeak;
	}

	pG->Age += n;
	if (pG->RampLeft == 0 && fPeak < GH_QUIET_PEAK)
	{
		pG->Quiet += n;
	}
	else
	{
		pG->Quiet = 0;
	}
	if ((pG->Kill && pG->KillLeft == 0) || pG->Quiet >= GH_QUIET_FRAMES || pG->Age >= GH_MAX_FRAMES)
	{
		pG->NamLive = 0;
		pG->State = GH_DONE;
	}
}