// ADELchorus: analog delay-line (BBD) chorus / vibrato inspired by the Boss choruses, mono input, stereo output.
// Four characters (models): CE-1 Chorus, CE-1 Vibrato (modulated signal only), CE-2, CH-1 Super (two voices, stereo, filters).
// Public techniques, no third-party code: delay line modulated by a triangular or sine LFO, 8-point Lagrange interpolation
// (dspkit), band-limited like a BBD's filters, soft saturation with ADAA anti-aliasing, optional background noise.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dspkit.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace adelchorus {

struct Params {
    int model = 2;              // 0 CE-1 Chorus, 1 CE-1 Vibrato, 2 CE-2, 3 CH-1 Super
    float rateHz = 0.8f;
    float depth = 0.6f;         // 0..1
    float mix = 0.5f;           // 0..1
    float levelDb = 0.f;
    bool wetOnly = false;
    float delayMs = 7.f;        // center delay
    int shape = 0;              // 0 triangle, 1 sine
    float spreadDeg = 180.f;    // LFO phase offset of the second voice (degrees)
    float width = 1.f;          // 0 = mono, 1 = full stereo
    float feedback = 0.f;       // 0..0.6
    float lowCut = 100.f, highCut = 8000.f;
    float character = 0.4f;     // 0..1: BBD bandwidth and saturation
    float noise = 0.f;          // 0..1: BBD hiss
};

// character of the four models
struct Voicing {
    float bwIn;         // Hz, input filter (BBD anti-alias)
    float depthMs;      // maximum delay excursion (ms) at Depth 100%
    float sat;          // base amount of saturation
    bool twoVoices;     // second voice with LFO out of phase (stereo)
    bool vibrato;       // modulated signal only
};
static const Voicing kVoicing[4] = {
    {7000.f, 3.5f, 0.55f, false, false},   // CE-1 Chorus
    {7000.f, 3.0f, 0.55f, false, true},    // CE-1 Vibrato
    {6000.f, 2.5f, 0.35f, false, false},   // CE-2
    {10000.f, 3.0f, 0.20f, true, false},   // CH-1 Super
};

class Chorus {
public:
    void init(double rate) {
        fs_ = float(rate);
        uint32_t sz = 1;
        while (sz < uint32_t(0.06 * fs_) + 16) sz <<= 1;
        buf_.assign(sz, 0.f);
        mask_ = sz - 1;
        clear();
    }
    void setMonoOut(bool m) { monoOut_ = m; }

    void clear() {
        std::fill(buf_.begin(), buf_.end(), 0.f);
        w_ = 0;
        ph_ = 0.f;
        adaa_.reset();
        for (int v = 0; v < 2; ++v) {
            hp_[v] = Bq();
            lp_[v] = Bq();
            lp2_[v] = Bq();
        }
        inLp_ = Bq();
        fbState_ = 0.f;
        seed_ = 12345u;
        first_ = true;
        lastKey_[0] = -1.f;
    }

    // in: mono; can outL/outR coincide with in? NO (in is read before the outputs only sample by sample, but the mono
    // version writes both): use distinct buffers
    void process(const float* in, float* outL, float* outR, uint32_t n, const Params& p) {
#if defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        uint64_t fpcrNew = fpcr | (1ull << 24);  // FZ: flush denormals to zero
        asm volatile("msr fpcr, %0" ::"r"(fpcrNew));
#elif defined(__x86_64__) || defined(__i386__)
        unsigned csr = _mm_getcsr();
        _mm_setcsr(csr | 0x8040);
#endif
        const int model = std::min(3, std::max(0, p.model));
        const Voicing& vc = kVoicing[model];
        const bool wetOnly = p.wetOnly || vc.vibrato;
        setup(p, vc);

        const float depthT = clampf(p.depth, 0.f, 1.f) * vc.depthMs * 1e-3f * fs_;     // samples
        const float delayT = std::max(1.0f, p.delayMs) * 1e-3f * fs_;
        const float mixT = wetOnly ? 1.f : clampf(p.mix, 0.f, 1.f);
        const float dryT = wetOnly ? 0.f : std::sqrt(std::max(0.f, 1.f - mixT));
        const float wetT = wetOnly ? 1.f : std::sqrt(mixT);
        const float outG = std::pow(10.f, clampf(p.levelDb, -24.f, 12.f) / 20.f);
        const float fb = clampf(p.feedback, 0.f, 0.6f);
        const float widthT = vc.twoVoices ? clampf(p.width, 0.f, 1.f) : 0.f;
        const float spread = clampf(p.spreadDeg, 0.f, 180.f) / 360.f;
        const float inc = clampf(p.rateHz, 0.02f, 20.f) / fs_;
        const float satK = 1.f + 1.5f * (vc.sat * (0.4f + 1.2f * clampf(p.character, 0.f, 1.f)));
        const float noiseA = clampf(p.noise, 0.f, 1.f) * 4e-4f;
        if (first_) {
            delay_ = delayT; depth_ = depthT; dry_ = dryT; wet_ = wetT; width_ = widthT;
            first_ = false;
        }
        const float sm = 0.0008f;

        for (uint32_t i = 0; i < n; ++i) {
            delay_ += sm * (delayT - delay_);
            depth_ += sm * (depthT - depth_);
            dry_ += sm * (dryT - dry_);
            wet_ += sm * (wetT - wet_);
            width_ += sm * (widthT - width_);

            const float x = in[i];
            // BBD input: anti-alias filter, hiss, saturation, regeneration
            float y = biq(inLp_, x) + noiseA * rnd() + fb * fbState_;
            y = adaa_.process(y, satK, 0.0);
            buf_[w_ & mask_] = y;
            ++w_;

            // LFO of the two voices
            ph_ += inc;
            if (ph_ >= 1.f) ph_ -= 1.f;
            float pb = ph_ + spread;
            if (pb >= 1.f) pb -= 1.f;
            const float la = lfo(ph_, p.shape), lb = lfo(pb, p.shape);

            const float dA = std::max(6.f, delay_ + depth_ * la);
            float sA = dspkit::readLagrange8(buf_.data(), mask_, w_, dA);
            sA = biq(lp2_[0], biq(lp_[0], biq(hp_[0], sA)));
            float sB = sA;
            if (vc.twoVoices) {
                const float dB = std::max(6.f, delay_ + depth_ * lb);
                sB = dspkit::readLagrange8(buf_.data(), mask_, w_, dB);
                sB = biq(lp2_[1], biq(lp_[1], biq(hp_[1], sB)));
            }
            fbState_ = 0.5f * (sA + sB);
            // mid and side: stereo width
            const float m = 0.5f * (sA + sB), s = 0.5f * (sA - sB);
            const float wl = m + width_ * s, wr = m - width_ * s;
            float l = (dry_ * x + wet_ * wl) * outG;
            float r = (dry_ * x + wet_ * wr) * outG;
            if (monoOut_) {
                l = r = 0.5f * (l + r);
            }
            outL[i] = l;
            outR[i] = r;
        }
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(csr);
#endif
    }

private:
    struct Bq {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static float biq(Bq& q, float x) {
        const float y = q.b0 * x + q.z1;
        q.z1 = q.b1 * x - q.a1 * y + q.z2;
        q.z2 = q.b2 * x - q.a2 * y;
        return y;
    }

    static void lowpass(Bq& q, float f, float fs) {
        const float w0 = 6.2831853f * f / fs, cw = std::cos(w0), al = std::sin(w0) / (2.f * 0.70710678f);
        const float a0 = 1.f + al;
        q.b0 = (1.f - cw) * 0.5f / a0; q.b1 = (1.f - cw) / a0; q.b2 = q.b0;
        q.a1 = -2.f * cw / a0; q.a2 = (1.f - al) / a0;
    }
    static void highpass(Bq& q, float f, float fs) {
        const float w0 = 6.2831853f * f / fs, cw = std::cos(w0), al = std::sin(w0) / (2.f * 0.70710678f);
        const float a0 = 1.f + al;
        q.b0 = (1.f + cw) * 0.5f / a0; q.b1 = -(1.f + cw) / a0; q.b2 = q.b0;
        q.a1 = -2.f * cw / a0; q.a2 = (1.f - al) / a0;
    }

    // the coefficients are recomputed only when a parameter that determines them changes
    void setup(const Params& p, const Voicing& vc) {
        const float key[5] = {float(p.model), p.lowCut, p.highCut, p.character, 0.f};
        bool same = lastKey_[0] >= 0.f;
        for (int k = 0; k < 4 && same; ++k) same = key[k] == lastKey_[k];
        if (same) return;
        for (int k = 0; k < 4; ++k) lastKey_[k] = key[k];
        const float c = clampf(p.character, 0.f, 1.f);
        lowpass(inLp_, std::min(0.45f * fs_, vc.bwIn * (1.f - 0.35f * c)), fs_);
        for (int v = 0; v < 2; ++v) {
            highpass(hp_[v], clampf(p.lowCut, 20.f, 1000.f), fs_);
            lowpass(lp_[v], clampf(p.highCut, 1000.f, 0.45f * fs_), fs_);
            lowpass(lp2_[v], std::min(0.45f * fs_, 14000.f - 9000.f * c), fs_);      // Character: BBD treble loss
        }
    }

    static float lfo(float ph, int shape) {
        if (shape == 1) return std::sin(6.2831853f * ph);
        return 4.f * std::fabs(ph - 0.5f) - 1.f;
    }
    float rnd() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return float(int32_t(seed_)) * (1.f / 2147483648.f);
    }

    float fs_ = 48000.f;
    std::vector<float> buf_;
    uint32_t mask_ = 0, w_ = 0, seed_ = 12345u;
    float ph_ = 0.f, fbState_ = 0.f;
    float delay_ = 0.f, depth_ = 0.f, dry_ = 1.f, wet_ = 0.f, width_ = 0.f;
    bool first_ = true, monoOut_ = false;
    Bq inLp_, hp_[2], lp_[2], lp2_[2];
    float lastKey_[5] = {-1.f, 0.f, 0.f, 0.f, 0.f};
    dspkit::AdaaTanh adaa_;
};

}  // namespace adelchorus
