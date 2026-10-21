// ADELnam engine: lean WaveNet A2 engine (quality 1.0), no STL and no dynamic allocation.
// Runs identically on Linux and on Circle (bare metal). Models use the .adn format (see nam2adn.py).
// Checked against NeuralAudio (AdnCompare). One instance = one lane; the state memory is supplied by the caller.
#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(__ARM_NEON) && !defined(ADELNAM_NO_NEON)
#include <arm_neon.h>
#define ADELNAM_HAVE_NEON 1
#endif

namespace adelnam {

static const int kMaxLayers = 32;

// model data (they point inside the memory block of the .adn file, which must stay valid and 4-byte aligned)
struct Model {
	uint32_t channels = 0, nLayers = 0, headKernel = 0;
	float leakySlope = 0.01f, sampleRate = 48000.f, headScale = 1.f;
	float loudness = 0.f, inLevelDbu = 0.f, outLevelDbu = 0.f;
	uint32_t kernel[kMaxLayers], dilation[kMaxLayers];
	const float* rechannel = nullptr;
	const float* convW[kMaxLayers];
	const float* convB[kMaxLayers];
	const float* mixin[kMaxLayers];
	const float* l1w[kMaxLayers];
	const float* l1b[kMaxLayers];
	const float* headW = nullptr;
	float headB = 0.f;
	uint32_t lookback = 0;       // samples of history needed (receptive field - 1)
	char name[64];               // model name (from the .adn file)
};

inline uint32_t Crc32(const uint8_t* p, size_t n) {
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

// returns nullptr if ok, otherwise a string with the error
// verifyCrc = false: the data come from a copy already verified in memory (the bit-by-bit CRC check of a 50 KB file costs about 1.5 ms)
inline const char* ParseModel(const uint8_t* d, size_t size, Model* m, bool verifyCrc = true) {
	if (size < 64 || ((uintptr_t) d & 3)) return "data too short or not aligned";
	if (__builtin_memcmp(d, "ADN1", 4)) return "wrong magic";
	uint32_t crc;
	__builtin_memcpy(&crc, d + size - 4, 4);
	if (verifyCrc && Crc32(d, size - 4) != crc) return "wrong CRC";
	const uint32_t* u = (const uint32_t*) (d + 4);
	if (u[0] != 1) return "unsupported version";
	m->channels = u[1];
	m->nLayers = u[2];
	m->headKernel = u[3];
	if (m->channels < 1 || m->channels > 16 || m->nLayers < 1 || m->nLayers > (uint32_t) kMaxLayers || m->headKernel < 1) return "invalid header";
	const float* f = (const float*) (d + 24);      // after magic (4) and 5 u32 (20 bytes)
	m->leakySlope = f[0];
	m->sampleRate = f[1];
	m->headScale = f[2];
	m->loudness = f[3];
	m->inLevelDbu = f[4];
	m->outLevelDbu = f[5];
	__builtin_memcpy(m->name, f + 6, 64);
	m->name[63] = 0;
	const uint32_t* kd = (const uint32_t*) (f + 6 + 16);       // after the 6 floats and the name (64 bytes)
	const uint32_t C = m->channels;
	m->lookback = m->headKernel - 1;
	for (uint32_t i = 0; i < m->nLayers; i++) {
		m->kernel[i] = kd[i];
		m->dilation[i] = kd[m->nLayers + i];
		if (m->kernel[i] < 1 || m->dilation[i] < 1) return "invalid layer";
		m->lookback += (m->kernel[i] - 1) * m->dilation[i];
	}
	const float* p = (const float*) (kd + 2 * m->nLayers);
	const float* end = (const float*) (d + size - 4);
	m->rechannel = p;
	p += C;
	for (uint32_t i = 0; i < m->nLayers; i++) {
		m->convW[i] = p;  p += m->kernel[i] * C * C;
		m->convB[i] = p;  p += C;
		m->mixin[i] = p;  p += C;
		m->l1w[i] = p;    p += C * C;
		m->l1b[i] = p;    p += C;
	}
	m->headW = p;  p += m->headKernel * C;
	m->headB = *p; p += 1;
	if (p != end) return "inconsistent weights size";
	return nullptr;
}

inline uint32_t NextPow2(uint32_t v) { uint32_t p = 1; while (p < v) p <<= 1; return p; }

// state of an instance. The memory (StateBytes) is supplied by the caller, 16-byte aligned.
class Instance {
public:
	static size_t StateBytes(const Model& m, int maxBlock) {
		size_t cols = 0;
		for (uint32_t i = 0; i < m.nLayers; i++) cols += NextPow2((m.kernel[i] - 1) * m.dilation[i] + (uint32_t) maxBlock);
		cols += NextPow2(m.headKernel - 1 + (uint32_t) maxBlock);
		size_t floats = cols * m.channels + 3 * (size_t) maxBlock * m.channels + (size_t) maxBlock * 2;
		return floats * sizeof(float) + 64;
	}

	// returns false if the memory is not enough
	bool Init(const Model* m, void* mem, size_t memBytes, int maxBlock) {
		if (memBytes < StateBytes(*m, maxBlock)) return false;
		m_ = m;
		C_ = (int) m->channels;
		maxBlock_ = maxBlock;
		uintptr_t a = ((uintptr_t) mem + 15) & ~(uintptr_t) 15;
		float* p = (float*) a;
		for (uint32_t i = 0; i < m->nLayers; i++) {
			uint32_t r = NextPow2((m->kernel[i] - 1) * m->dilation[i] + (uint32_t) maxBlock);
			ring_[i] = p;
			mask_[i] = r - 1;
			pos_[i] = 0;
			p += (size_t) r * C_;
		}
		uint32_t hr = NextPow2(m->headKernel - 1 + (uint32_t) maxBlock);
		headRing_ = p;
		headMask_ = hr - 1;
		headPos_ = 0;
		dc_ = 0.0f;
		p += (size_t) hr * C_;
		layerIn_ = p;  p += (size_t) maxBlock * C_;
		headSum_ = p;  p += (size_t) maxBlock * C_;
		z_ = p;        p += (size_t) maxBlock * C_;
		cond_ = p;     p += maxBlock;
		out_ = p;      p += maxBlock;
		Reset();
		return true;
	}

	void Reset() {
		size_t total = 0;
		for (uint32_t i = 0; i < m_->nLayers; i++) total += (size_t) (mask_[i] + 1) * C_;
		total += (size_t) (headMask_ + 1) * C_;
		for (uint32_t i = 0; i < m_->nLayers; i++) { __builtin_memset(ring_[i], 0, (size_t) (mask_[i] + 1) * C_ * sizeof(float)); pos_[i] = 0; }
		__builtin_memset(headRing_, 0, (size_t) (headMask_ + 1) * C_ * sizeof(float));
		headPos_ = 0;
		(void) total;
	}

	// warms the state up with silence (same length as NeuralAudio's prewarm: the receptive field)
	void Prewarm() {
		float zeros[64] = {0}, out[64];
		int n = (int) m_->lookback + 1;
		dc_ = 0.0f;
		float last = 0.0f;
		while (n > 0) {
			int b = n < 64 ? n : 64;
			if (b > maxBlock_) b = maxBlock_;
			Process(zeros, out, b);
			last = out[b - 1];
			n -= b;
		}
		dc_ = last;		// model output with silent input (the model's DC offset): from here on it is subtracted, so silence comes out as zero
	}

	int PrewarmSamples() const { return (int) m_->lookback + 1; }

	// The state after Init + Prewarm (silent input) depends only on the model: instead of recomputing it at every load (40-50 ms per model on the Pi 5)
	// it can be saved once and restored with a memory copy. WarmFloats = how many floats are needed; pos = nLayers + 2 values (write positions of the rings, plus the DC offset).
	size_t WarmFloats() const {
		size_t t = 0;
		for (uint32_t i = 0; i < m_->nLayers; i++) t += (size_t) (mask_[i] + 1) * C_;
		return t + (size_t) (headMask_ + 1) * C_;
	}
	void WarmSave(float* dst, uint32_t* pos) const {
		for (uint32_t i = 0; i < m_->nLayers; i++) {
			const size_t n = (size_t) (mask_[i] + 1) * C_;
			__builtin_memcpy(dst, ring_[i], n * sizeof(float));
			dst += n;
			pos[i] = pos_[i];
		}
		__builtin_memcpy(dst, headRing_, (size_t) (headMask_ + 1) * C_ * sizeof(float));
		pos[m_->nLayers] = headPos_;
		__builtin_memcpy(&pos[m_->nLayers + 1], &dc_, sizeof dc_);		// pos has nLayers + 2 slots
	}
	void WarmLoad(const float* src, const uint32_t* pos) {
		for (uint32_t i = 0; i < m_->nLayers; i++) {
			const size_t n = (size_t) (mask_[i] + 1) * C_;
			__builtin_memcpy(ring_[i], src, n * sizeof(float));
			src += n;
			pos_[i] = pos[i];
		}
		__builtin_memcpy(headRing_, src, (size_t) (headMask_ + 1) * C_ * sizeof(float));
		headPos_ = pos[m_->nLayers];
		__builtin_memcpy(&dc_, &pos[m_->nLayers + 1], sizeof dc_);
	}

	// processes n samples (n <= maxBlock)
	void Process(const float* in, float* out, int n) {
		while (n > maxBlock_) {
			Process(in, out, maxBlock_);
			in += maxBlock_;
			out += maxBlock_;
			n -= maxBlock_;
		}
		const int C = C_;
		for (int f = 0; f < n; f++) {
			const float x = in[f];
			cond_[f] = x;
			for (int c = 0; c < C; c++) layerIn_[f * C + c] = m_->rechannel[c] * x;
		}
		__builtin_memset(headSum_, 0, (size_t) n * C * sizeof(float));
		for (uint32_t li = 0; li < m_->nLayers; li++) Layer(li, n);
		Head(out, n);
		if (dc_ != 0.0f) for (int f = 0; f < n; f++) out[f] -= dc_;
	}

private:
#ifdef ADELNAM_HAVE_NEON
	// ---- NEON path for 8 channels -------------------------------------------------------------------------------
	// Convolution of T frames together: the weights of each tap are loaded once per T frames; two sets of accumulators
	// (even/odd taps) break the dependency chain of the FMAs.
	// Weights: column j of a tap = 8 contiguous outputs (index j*8 + i).
#define ADN_STEP(JJ, XV, LANE)                                                                                    \
	{                                                                                                             \
		const float32x4_t wl = vld1q_f32(wk + (JJ) * 8), wh = vld1q_f32(wk + (JJ) * 8 + 4);                       \
		for (int t = 0; t < T; t++) {                                                                             \
			A[t][0] = vfmaq_laneq_f32(A[t][0], wl, XV[t], LANE);                                                  \
			A[t][1] = vfmaq_laneq_f32(A[t][1], wh, XV[t], LANE);                                                  \
		}                                                                                                         \
	}

	template <int T>
	inline void Tile8(uint32_t li, int f0, uint32_t pos, uint32_t mask, int K, int D) {
		const float* ring = ring_[li];
		const float* W = m_->convW[li];
		float32x4_t acc[2][T][2];
		for (int s = 0; s < 2; s++)
			for (int t = 0; t < T; t++) { acc[s][t][0] = vdupq_n_f32(0.f); acc[s][t][1] = vdupq_n_f32(0.f); }
		for (int k = 0; k < K; k++) {
			const float* wk = W + (size_t) k * 64;
			float32x4_t xl[T], xh[T];
			for (int t = 0; t < T; t++) {
				const uint32_t col = (pos + (uint32_t) (f0 + t) - (uint32_t) ((K - 1 - k) * D)) & mask;
				xl[t] = vld1q_f32(ring + (size_t) col * 8);
				xh[t] = vld1q_f32(ring + (size_t) col * 8 + 4);
			}
			float32x4_t(*A)[2] = acc[k & 1];
			ADN_STEP(0, xl, 0) ADN_STEP(1, xl, 1) ADN_STEP(2, xl, 2) ADN_STEP(3, xl, 3)
			ADN_STEP(4, xh, 0) ADN_STEP(5, xh, 1) ADN_STEP(6, xh, 2) ADN_STEP(7, xh, 3)
		}
		const float32x4_t cbl = vld1q_f32(m_->convB[li]), cbh = vld1q_f32(m_->convB[li] + 4);
		const float32x4_t mxl = vld1q_f32(m_->mixin[li]), mxh = vld1q_f32(m_->mixin[li] + 4);
		const float32x4_t lbl = vld1q_f32(m_->l1b[li]), lbh = vld1q_f32(m_->l1b[li] + 4);
		const float* lw = m_->l1w[li];
		const float slope = m_->leakySlope;
		float32x4_t zl[T], zh[T];
		for (int t = 0; t < T; t++) {
			const float cf = cond_[f0 + t];
			float32x4_t a = vaddq_f32(vaddq_f32(acc[0][t][0], acc[1][t][0]), cbl);
			float32x4_t b = vaddq_f32(vaddq_f32(acc[0][t][1], acc[1][t][1]), cbh);
			a = vfmaq_n_f32(a, mxl, cf);
			b = vfmaq_n_f32(b, mxh, cf);
			zl[t] = vmaxq_f32(a, vmulq_n_f32(a, slope));           // LeakyReLU = max(z, slope*z)
			zh[t] = vmaxq_f32(b, vmulq_n_f32(b, slope));
		}
		float32x4_t yl[T], yh[T];
		for (int t = 0; t < T; t++) { yl[t] = lbl; yh[t] = lbh; }
#define ADN_L1(JJ, ZV, LANE)                                                                                      \
	{                                                                                                             \
		const float32x4_t wl = vld1q_f32(lw + (JJ) * 8), wh = vld1q_f32(lw + (JJ) * 8 + 4);                       \
		for (int t = 0; t < T; t++) {                                                                             \
			yl[t] = vfmaq_laneq_f32(yl[t], wl, ZV[t], LANE);                                                      \
			yh[t] = vfmaq_laneq_f32(yh[t], wh, ZV[t], LANE);                                                      \
		}                                                                                                         \
	}
		ADN_L1(0, zl, 0) ADN_L1(1, zl, 1) ADN_L1(2, zl, 2) ADN_L1(3, zl, 3)
		ADN_L1(4, zh, 0) ADN_L1(5, zh, 1) ADN_L1(6, zh, 2) ADN_L1(7, zh, 3)
#undef ADN_L1
		for (int t = 0; t < T; t++) {
			float* hs = headSum_ + (size_t) (f0 + t) * 8;
			vst1q_f32(hs, vaddq_f32(vld1q_f32(hs), zl[t]));
			vst1q_f32(hs + 4, vaddq_f32(vld1q_f32(hs + 4), zh[t]));
			float* li_ = layerIn_ + (size_t) (f0 + t) * 8;
			vst1q_f32(li_, vaddq_f32(vld1q_f32(li_), yl[t]));
			vst1q_f32(li_ + 4, vaddq_f32(vld1q_f32(li_ + 4), yh[t]));
		}
	}
#undef ADN_STEP

	void LayerNeon8(uint32_t li, int n) {
		const int K = (int) m_->kernel[li];
		const int D = (int) m_->dilation[li];
		float* ring = ring_[li];
		const uint32_t mask = mask_[li];
		const uint32_t pos = pos_[li];
		for (int f = 0; f < n; f++) {
			float* dst = ring + (size_t) ((pos + (uint32_t) f) & mask) * 8;
			vst1q_f32(dst, vld1q_f32(layerIn_ + (size_t) f * 8));
			vst1q_f32(dst + 4, vld1q_f32(layerIn_ + (size_t) f * 8 + 4));
		}
		int f = 0;
		for (; f + 4 <= n; f += 4) Tile8<4>(li, f, pos, mask, K, D);
		for (; f + 2 <= n; f += 2) Tile8<2>(li, f, pos, mask, K, D);
		for (; f < n; f++) Tile8<1>(li, f, pos, mask, K, D);
		pos_[li] = pos + (uint32_t) n;
	}

	void HeadNeon8(float* out, int n) {
		const int HK = (int) m_->headKernel;
		for (int f = 0; f < n; f++) {
			float* dst = headRing_ + (size_t) ((headPos_ + (uint32_t) f) & headMask_) * 8;
			vst1q_f32(dst, vld1q_f32(headSum_ + (size_t) f * 8));
			vst1q_f32(dst + 4, vld1q_f32(headSum_ + (size_t) f * 8 + 4));
		}
		for (int f = 0; f < n; f++) {
			float32x4_t a0 = vdupq_n_f32(0.f), a1 = vdupq_n_f32(0.f), a2 = vdupq_n_f32(0.f), a3 = vdupq_n_f32(0.f);
			for (int k = 0; k < HK; k += 2) {
				const uint32_t c0 = (headPos_ + (uint32_t) f - (uint32_t) (HK - 1 - k)) & headMask_;
				const uint32_t c1 = (headPos_ + (uint32_t) f - (uint32_t) (HK - 2 - k)) & headMask_;
				const float* s0 = headRing_ + (size_t) c0 * 8;
				const float* s1 = headRing_ + (size_t) c1 * 8;
				const float* w0 = m_->headW + (size_t) k * 8;
				a0 = vfmaq_f32(a0, vld1q_f32(w0), vld1q_f32(s0));
				a1 = vfmaq_f32(a1, vld1q_f32(w0 + 4), vld1q_f32(s0 + 4));
				a2 = vfmaq_f32(a2, vld1q_f32(w0 + 8), vld1q_f32(s1));
				a3 = vfmaq_f32(a3, vld1q_f32(w0 + 12), vld1q_f32(s1 + 4));
			}
			const float y = m_->headB + vaddvq_f32(vaddq_f32(vaddq_f32(a0, a1), vaddq_f32(a2, a3)));
			out[f] = y * m_->headScale;
		}
		headPos_ += (uint32_t) n;
	}
#endif

	void Layer(uint32_t li, int n) {
#ifdef ADELNAM_HAVE_NEON
		if (C_ == 8) { LayerNeon8(li, n); return; }
#endif
		const int C = C_;
		const int K = (int) m_->kernel[li];
		const int D = (int) m_->dilation[li];
		float* ring = ring_[li];
		const uint32_t mask = mask_[li];
		uint32_t pos = pos_[li];
		for (int f = 0; f < n; f++) __builtin_memcpy(ring + (size_t) ((pos + f) & mask) * C, layerIn_ + (size_t) f * C, C * sizeof(float));
		const float* W = m_->convW[li];
		const float* b = m_->convB[li];
		const float* mx = m_->mixin[li];
		const float* lw = m_->l1w[li];
		const float* lb = m_->l1b[li];
		const float slope = m_->leakySlope;
		for (int f = 0; f < n; f++) {
			float z[16];
			const float cf = cond_[f];
			for (int i = 0; i < C; i++) z[i] = b[i] + mx[i] * cf;
			for (int k = 0; k < K; k++) {
				const uint32_t col = (pos + (uint32_t) f - (uint32_t) ((K - 1 - k) * D)) & mask;
				const float* src = ring + (size_t) col * C;
				const float* wk = W + (size_t) k * C * C;
				for (int j = 0; j < C; j++) {
					const float xj = src[j];
					for (int i = 0; i < C; i++) z[i] += wk[j * C + i] * xj;
				}
			}
			for (int i = 0; i < C; i++) z[i] = z[i] >= 0.f ? z[i] : z[i] * slope;
			float* hs = headSum_ + (size_t) f * C;
			float* li_ = layerIn_ + (size_t) f * C;
			for (int i = 0; i < C; i++) {
				hs[i] += z[i];
				float s = lb[i];
				for (int j = 0; j < C; j++) s += lw[j * C + i] * z[j];
				li_[i] += s;
			}
		}
		pos_[li] = pos + (uint32_t) n;
	}

	void Head(float* out, int n) {
#ifdef ADELNAM_HAVE_NEON
		if (C_ == 8 && (m_->headKernel & 1) == 0) { HeadNeon8(out, n); return; }
#endif
		const int C = C_;
		const int HK = (int) m_->headKernel;
		for (int f = 0; f < n; f++) __builtin_memcpy(headRing_ + (size_t) ((headPos_ + f) & headMask_) * C, headSum_ + (size_t) f * C, C * sizeof(float));
		for (int f = 0; f < n; f++) {
			float y = m_->headB;
			for (int k = 0; k < HK; k++) {
				const uint32_t col = (headPos_ + (uint32_t) f - (uint32_t) (HK - 1 - k)) & headMask_;
				const float* src = headRing_ + (size_t) col * C;
				const float* wk = m_->headW + (size_t) k * C;
				for (int j = 0; j < C; j++) y += wk[j] * src[j];
			}
			out[f] = y * m_->headScale;
		}
		headPos_ += (uint32_t) n;
	}

	const Model* m_ = nullptr;
	int C_ = 0, maxBlock_ = 0;
	float* ring_[kMaxLayers];
	uint32_t mask_[kMaxLayers], pos_[kMaxLayers];
	float* headRing_ = nullptr;
	uint32_t headMask_ = 0, headPos_ = 0;
	float dc_ = 0.0f;
	float *layerIn_ = nullptr, *headSum_ = nullptr, *z_ = nullptr, *cond_ = nullptr, *out_ = nullptr;
};

}  // namespace adelnam
