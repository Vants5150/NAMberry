//
// cabir.h - cabinet IR: direct convolution with NEON (no latency beyond the sample, no FFT).
// History of each channel in a double buffer (the last T outputs are contiguous), impulse response reversed and padded to multiples of 16.
// Cost: T multiply-adds per sample and per channel (T = 4096: about 0.12 us/sample on the Pi 5 CPU).
//
#ifndef _cabir_h
#define _cabir_h

#include <arm_neon.h>

#ifndef CABIR_MAX_TAPS
#define CABIR_MAX_TAPS	8192
#endif

// dot product of n elements (n a multiple of 16) of two float vectors
static inline float CabDot (const float *a, const float *b, unsigned n)
{
	float32x4_t s0 = vdupq_n_f32 (0.0f), s1 = s0, s2 = s0, s3 = s0;
	for (unsigned i = 0; i < n; i += 16)
	{
		s0 = vfmaq_f32 (s0, vld1q_f32 (a + i), vld1q_f32 (b + i));
		s1 = vfmaq_f32 (s1, vld1q_f32 (a + i + 4), vld1q_f32 (b + i + 4));
		s2 = vfmaq_f32 (s2, vld1q_f32 (a + i + 8), vld1q_f32 (b + i + 8));
		s3 = vfmaq_f32 (s3, vld1q_f32 (a + i + 12), vld1q_f32 (b + i + 12));
	}
	return vaddvq_f32 (vaddq_f32 (vaddq_f32 (s0, s1), vaddq_f32 (s2, s3)));
}

// one convolution channel: ir[0..T-1] (T a multiple of 16, already reversed: ir[0] is the oldest sample of the window)
struct TCabChannel
{
	float *pHist;		// 2 * T samples
	unsigned nWrite;	// 0..T-1

	void Reset (unsigned nTaps)
	{
		for (unsigned i = 0; i < 2 * nTaps; i++)
		{
			pHist[i] = 0.0f;
		}
		nWrite = 0;
	}

	inline float Step (float x, const float *pIrRev, unsigned nTaps)
	{
		pHist[nWrite] = x;
		pHist[nWrite + nTaps] = x;
		const float y = CabDot (pIrRev, pHist + nWrite + 1, nTaps);
		if (++nWrite >= nTaps)
		{
			nWrite = 0;
		}
		return y;
	}
};

#endif
