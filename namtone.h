//
// namtone.h - Bass / Mid / Treble tone control of the NAM block: three biquads (RBJ) after the model, in the same design as the ToTest "Tone Stack" block:
// bass shelf at 110 Hz, mid peak at 650 Hz (Q 0.7), treble shelf at 3200 Hz. At 0 dB the band is off (no cost and no change to the signal).
// The coefficients are computed in the main loop (Compile) and live in the node's P[] floats; the audio callback only uses them.
//
#ifndef _namtone_h
#define _namtone_h

#include <circle/types.h>
#include "dsp_util.h"

#define NT_STAGES	3
#define NT_COEF		(NT_STAGES * 5)		// b0 b1 b2 a1 a2 for each band (already divided by a0)
#define NT_MAX_DB	12.0f

static inline float NtClamp (float x)
{
	return x < -NT_MAX_DB ? -NT_MAX_DB : (x > NT_MAX_DB ? NT_MAX_DB : x);
}

static inline void NtSet (float *pC, unsigned s, float b0, float b1, float b2, float a0, float a1, float a2)
{
	const float r = 1.0f / a0;
	pC[s * 5 + 0] = b0 * r;
	pC[s * 5 + 1] = b1 * r;
	pC[s * 5 + 2] = b2 * r;
	pC[s * 5 + 3] = a1 * r;
	pC[s * 5 + 4] = a2 * r;
}

static inline void NtPeak (float *pC, unsigned s, float f, float g, float q, float fRate)
{
	const float A = DbToLin (g * 0.5f), w0 = 6.2831853f * f / fRate, cw = DspCos (w0), al = DspSin (w0) / (2.0f * q);
	NtSet (pC, s, 1.0f + al * A, -2.0f * cw, 1.0f - al * A, 1.0f + al / A, -2.0f * cw, 1.0f - al / A);
}

static inline void NtShelf (float *pC, unsigned s, boolean bHigh, float f, float g, float fRate)
{
	const float A = DbToLin (g * 0.5f), sA = DbToLin (g * 0.25f), w0 = 6.2831853f * f / fRate, cw = DspCos (w0);
	const float al2 = DspSin (w0) * 0.70710678f * 2.0f * sA;
	if (!bHigh)
	{
		NtSet (pC, s, A * ((A + 1.0f) - (A - 1.0f) * cw + al2), 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw), A * ((A + 1.0f) - (A - 1.0f) * cw - al2),
		       (A + 1.0f) + (A - 1.0f) * cw + al2, -2.0f * ((A - 1.0f) + (A + 1.0f) * cw), (A + 1.0f) + (A - 1.0f) * cw - al2);
	}
	else
	{
		NtSet (pC, s, A * ((A + 1.0f) + (A - 1.0f) * cw + al2), -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw), A * ((A + 1.0f) + (A - 1.0f) * cw - al2),
		       (A + 1.0f) - (A - 1.0f) * cw + al2, 2.0f * ((A - 1.0f) - (A + 1.0f) * cw), (A + 1.0f) - (A - 1.0f) * cw - al2);
	}
}

// computes the coefficients in pC[NT_COEF]; returns the mask of the active bands (bit 0 bass, 1 mid, 2 treble; 0 = all flat)
static inline u8 NamToneSetup (float *pC, float fBass, float fMid, float fTreble, float fRate)
{
	const float gb = NtClamp (fBass), gm = NtClamp (fMid), gt = NtClamp (fTreble);
	u8 nMask = 0;
	if (gb > 0.05f || gb < -0.05f)
	{
		NtShelf (pC, 0, FALSE, 110.0f, gb, fRate);
		nMask |= 1;
	}
	if (gm > 0.05f || gm < -0.05f)
	{
		NtPeak (pC, 1, 650.0f, gm, 0.7f, fRate);
		nMask |= 2;
	}
	if (gt > 0.05f || gt < -0.05f)
	{
		NtShelf (pC, 2, TRUE, 3200.0f, gt, fRate);
		nMask |= 4;
	}
	return nMask;
}

// filters n samples in place; z[3][2] is the state of the three biquads (zeroed for the bands that are off: turning them on again makes no jump)
static inline void NamToneRun (const float *pC, u8 nMask, float z[NT_STAGES][2], float *p, unsigned n)
{
	for (unsigned s = 0; s < NT_STAGES; s++)
	{
		if (!(nMask & (1u << s)))
		{
			z[s][0] = z[s][1] = 0.0f;
			continue;
		}
		const float b0 = pC[s * 5], b1 = pC[s * 5 + 1], b2 = pC[s * 5 + 2], a1 = pC[s * 5 + 3], a2 = pC[s * 5 + 4];
		float z1 = z[s][0], z2 = z[s][1];
		for (unsigned j = 0; j < n; j++)
		{
			const float x = p[j];
			const float y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			p[j] = y;
		}
		z[s][0] = z1;
		z[s][1] = z2;
	}
}

#endif
