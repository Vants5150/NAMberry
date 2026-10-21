//
// mcprobe.h - multi-core feasibility test (only with -DMC_PROBE and with Circle compiled with ARM_ALLOW_MULTI_CORE):
// it measures how long a message takes to go from core 0 to core 1 and back, and the synchronization cost of a parallel job.
//
#ifndef _mcprobe_h
#define _mcprobe_h

#include <circle/multicore.h>
#include <circle/types.h>

class CMcProbe : public CMultiCoreSupport
{
public:
	CMcProbe (CMemorySystem *pMemorySystem);

	void Run (unsigned nCore) override;

	// to be called on core 0: writes into pOut the summary of the measurements (text, one line)
	void Measure (char *pOut, unsigned nSize);
};

#endif
