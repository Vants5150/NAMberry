//
// main.cpp - Pisound + Wi-Fi test on Circle
//
#include "kernel.h"
#include <circle/startup.h>
#include <circle/actled.h>
#include <circle/memio.h>
#include <circle/bcm2712.h>
#include <circle/timer.h>

// State of the RP1's DMA controller found at startup (it is written to the log by CKernel::Run)
unsigned g_nDmaChenAtBoot = 0;
unsigned g_DmaDarAtBoot[8];
unsigned g_DmaLlpAtBoot[8];
unsigned g_nDmaResetOk = 0;

// The RP1's DMA controller is a separate chip and is NOT reset by the Pi's reboot: the audio channels of the previous kernel
// keep running and keep writing into the memory that the new kernel is already using. It must be stopped immediately, before
// anything else (the complete reset is then done by Circle's driver when it creates the first channel).
static void QuiesceRp1Dma (void)
{
	const uintptr nBase = ARM_DMA_RP1_BASE;
	g_nDmaChenAtBoot = read32 (nBase + 0x018);			// DMAC_CHEN
	for (unsigned i = 0; i < 8; i++)
	{
		const uintptr nChan = nBase + 0x100 + i * 0x100;		// COMMON_REG_LEN + i * CHAN_REG_LEN
		g_DmaDarAtBoot[i] = read32 (nChan + 0x008);			// CH_DAR (low part)
		g_DmaLlpAtBoot[i] = read32 (nChan + 0x028);			// CH_LLP (low part)
	}

	write32 (nBase + 0x058, 1);					// DMAC_RESET
	for (int nRetries = 1000; nRetries > 0; nRetries--)
	{
		if (!read32 (nBase + 0x058))
		{
			g_nDmaResetOk = 1;
			break;
		}
		CTimer::SimpleusDelay (1);
	}
}

int main (void)
{
	// cannot return here because some destructors used in CKernel are not implemented

	QuiesceRp1Dma ();

	// The object is large (pedalboard, audio plans, model library): on the heap, not on the 128 KB boot stack
	CKernel *pKernel = new CKernel;
	CKernel &Kernel = *pKernel;
	if (!Kernel.Initialize ())
	{
		// error signal in Initialize: 3 fast blinks and 1 second of pause, forever
		CActLED Led;
		for (;;)
		{
			Led.Blink (3, 100, 100);
			CTimer::SimpleMsDelay (1000);
		}
	}

	TShutdownMode ShutdownMode = Kernel.Run ();

	switch (ShutdownMode)
	{
	case ShutdownReboot:
		reboot ();
		return EXIT_REBOOT;

	case ShutdownHalt:
	default:
		halt ();
		return EXIT_HALT;
	}
}
