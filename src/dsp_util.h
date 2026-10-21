//
// dsp_util.h - numeric functions without libm
//
#ifndef _dsp_util_h
#define _dsp_util_h

#include <circle/types.h>

static inline float DspSqrtF (float x)
{
	float r;
	asm ("fsqrt %s0, %s1" : "=w" (r) : "w" (x));
	return r;
}

// 10^(dB/20) = 2^(dB * log2(10)/20), Taylor series for the fractional part
static inline float DbToLin (float fDb)
{
	float x = fDb * 0.16609640474f;
	int n = (int) x;
	if ((float) n > x)
	{
		n--;
	}
	const float k = (x - (float) n) * 0.69314718056f;
	float term = 1.0f, sum = 1.0f;
	for (int i = 1; i <= 12; i++)
	{
		term *= k / (float) i;
		sum += term;
	}
	union { float f; u32 u; } s;
	s.u = (u32) (n + 127) << 23;
	return sum * s.f;
}

// sine and cosine for angles between 0 and pi (Taylor series, error < 1e-6 in the interval)
static inline float DspSin (float x)
{
	const float x2 = x * x;
	float term = x, sum = x;
	for (int i = 1; i <= 8; i++)
	{
		term *= -x2 / (float) ((2 * i) * (2 * i + 1));
		sum += term;
	}
	return sum;
}

static inline float DspCos (float x)
{
	const float x2 = x * x;
	float term = 1.0f, sum = 1.0f;
	for (int i = 1; i <= 8; i++)
	{
		term *= -x2 / (float) ((2 * i - 1) * (2 * i));
		sum += term;
	}
	return sum;
}

#endif
