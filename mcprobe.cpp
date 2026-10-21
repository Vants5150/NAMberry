//
// mcprobe.cpp - see mcprobe.h
//
#include "mcprobe.h"
#include <circle/string.h>
#include <circle/util.h>

struct TMcShared
{
	alignas (64) volatile u32 Req;
	alignas (64) volatile u32 Ack;
	alignas (64) volatile u32 Work;
	alignas (64) volatile u32 Alive;
};
static TMcShared s_Mc;

static inline u64 Ticks (void)
{
	u64 n;
	asm volatile ("isb; mrs %0, cntvct_el0" : "=r" (n));
	return n;
}

// fake work: n iterations of multiply-add in floating point with a dependency (it cannot be optimized away)
static void Burn (u32 n)
{
	volatile float acc = 1.0f;
	for (u32 i = 0; i < n; i++)
	{
		acc = acc * 0.9999f + 1.0f;
	}
}

CMcProbe::CMcProbe (CMemorySystem *pMemorySystem)
:	CMultiCoreSupport (pMemorySystem)
{
}

void CMcProbe::Run (unsigned nCore)
{
	if (nCore != 1)
	{
		for (;;)
		{
			asm volatile ("wfe");
		}
	}

	__atomic_store_n (&s_Mc.Alive, 1, __ATOMIC_RELEASE);
	u32 nLast = 0;
	for (;;)
	{
		u32 nReq;
		while ((nReq = __atomic_load_n (&s_Mc.Req, __ATOMIC_ACQUIRE)) == nLast)
		{
		}
		nLast = nReq;
		Burn (__atomic_load_n (&s_Mc.Work, __ATOMIC_RELAXED));
		__atomic_store_n (&s_Mc.Ack, nReq, __ATOMIC_RELEASE);
	}
}

void CMcProbe::Measure (char *pOut, unsigned nSize)
{
	u64 nFreq;
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (nFreq));
	const unsigned nNsPerTick1000 = (unsigned) (1000000000000ULL / nFreq);		// ns * 1000 per tick

	if (!__atomic_load_n (&s_Mc.Alive, __ATOMIC_ACQUIRE))
	{
		CString S ("core 1 not active");
		strncpy (pOut, (const char *) S, nSize - 1);
		pOut[nSize - 1] = 0;
		return;
	}

	static u32 nSeq = 0;
	// 1) round trip with no work
	const unsigned N = 5000;
	u64 nMin = ~0ULL, nMax = 0, nSum = 0;
	for (unsigned i = 0; i < N; i++)
	{
		s_Mc.Work = 0;
		const u32 nS = ++nSeq;
		const u64 t0 = Ticks ();
		__atomic_store_n (&s_Mc.Req, nS, __ATOMIC_RELEASE);
		while (__atomic_load_n (&s_Mc.Ack, __ATOMIC_ACQUIRE) != nS)
		{
		}
		const u64 d = Ticks () - t0;
		nSum += d;
		if (d < nMin) nMin = d;
		if (d > nMax) nMax = d;
	}
	const unsigned nRtMin = (unsigned) (nMin * nNsPerTick1000 / 1000);
	const unsigned nRtAvg = (unsigned) (nSum / N * nNsPerTick1000 / 1000);
	const unsigned nRtMax = (unsigned) (nMax * nNsPerTick1000 / 1000);

	// 2) parallel work: W iterations on core 1 and the same on core 0; the time alone on core 0 is the reference
	const u32 W = 2000;
	u64 nAlone = ~0ULL;
	for (unsigned i = 0; i < 200; i++)
	{
		const u64 t0 = Ticks ();
		Burn (W);
		const u64 d = Ticks () - t0;
		if (d < nAlone) nAlone = d;
	}
	u64 nParMin = ~0ULL, nParSum = 0, nParMax = 0;
	const unsigned M = 2000;
	for (unsigned i = 0; i < M; i++)
	{
		s_Mc.Work = W;
		const u32 nS = ++nSeq;
		const u64 t0 = Ticks ();
		__atomic_store_n (&s_Mc.Req, nS, __ATOMIC_RELEASE);
		Burn (W);
		while (__atomic_load_n (&s_Mc.Ack, __ATOMIC_ACQUIRE) != nS)
		{
		}
		const u64 d = Ticks () - t0;
		nParSum += d;
		if (d < nParMin) nParMin = d;
		if (d > nParMax) nParMax = d;
	}
	const unsigned nAloneNs = (unsigned) (nAlone * nNsPerTick1000 / 1000);
	const unsigned nParMinNs = (unsigned) (nParMin * nNsPerTick1000 / 1000);
	const unsigned nParAvgNs = (unsigned) (nParSum / M * nNsPerTick1000 / 1000);
	const unsigned nParMaxNs = (unsigned) (nParMax * nNsPerTick1000 / 1000);

	CString S;
	S.Format ("round trip ns: min %u mean %u max %u | work %u iterations: alone %u ns; in parallel (2 cores) min %u mean %u max %u ns",
		  nRtMin, nRtAvg, nRtMax, (unsigned) W, nAloneNs, nParMinNs, nParAvgNs, nParMaxNs);
	strncpy (pOut, (const char *) S, nSize - 1);
	pOut[nSize - 1] = 0;
}
