// ADELcomp: mono/stereo compressor. Types VCA / Opto / FET / Vari-Mu, Peak / RMS / Hybrid detector,
// program-dependent release, knee, sidechain (high-pass + shelf), lookahead, parallel, auto makeup,
// saturation, stereo link and M/S, gate/expander. Public techniques (static curve with quadratic knee,
// ballistics in the dB domain), no third-party code.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dspkit.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace adelcomp {

struct Params {
    float inputDb = 0.f, outputDb = 0.f, mix = 1.f;
    int type = 0;                // 0 VCA, 1 Opto, 2 FET, 3 Vari-Mu
    float thresholdDb = -20.f, ratio = 4.f, attackMs = 10.f, releaseMs = 150.f, autoRel = 0.f, kneeDb = 6.f;
    float makeupDb = 0.f;
    bool autoMakeup = false;
    int detector = 0;            // 0 Peak, 1 RMS, 2 Hybrid
    float rmsMs = 15.f;
    float scHpf = 20.f, scShelfDb = 0.f;
    float lookMs = 0.f;
    float sat = 0.f;             // 0..1
    float link = 1.f;            // 0..1
    bool ms = false;
    float gateThrDb = -90.f, gateRatio = 3.f, gateRangeDb = 30.f, gateRelMs = 150.f;
};


// true peak estimate: interpolation at 1/4, 2/4 and 3/4 of a sample (8-point sinc with Hann window) between the two central samples
struct TruePeak {
    float h[8] = {};
    float c[3][8];
    TruePeak() {
        for (int p = 0; p < 3; ++p) {
            const float f = 0.25f * float(p + 1);
            float sum = 0.f;
            for (int j = 0; j < 8; ++j) {
                const float x = float(j) - 3.f - f;
                const float sn = std::fabs(x) < 1e-6f ? 1.f : std::sin(3.14159265f * x) / (3.14159265f * x);
                c[p][j] = sn * 0.5f * (1.f + std::cos(3.14159265f * x / 4.f));
                sum += c[p][j];
            }
            for (int j = 0; j < 8; ++j) c[p][j] /= sum;
        }
    }
    void reset() { for (int j = 0; j < 8; ++j) h[j] = 0.f; }
    // returns the maximum between the current sample and the peaks estimated between the samples 3-4 samples ago
    float push(float s) {
        for (int j = 0; j < 7; ++j) h[j] = h[j + 1];
        h[7] = s;
        float m = std::fabs(s);
        for (int p = 0; p < 3; ++p) {
            float a = 0.f;
            for (int j = 0; j < 8; ++j) a += c[p][j] * h[j];
            m = std::max(m, std::fabs(a));
        }
        return m;
    }
};

// 2x oversampling with polyphase IIR halfband filters (two 4th-order all-pass branches in total)
struct Os2 {
    struct Ap { float x1 = 0, y1 = 0; float run(float x, float a) { float y = a * (x - y1) + x1; x1 = x; y1 = y; return y; } };
    static constexpr float kA0 = 0.07986642623635751f, kA1 = 0.5453536510711322f;
    static constexpr float kB0 = 0.28382934487410993f, kB1 = 0.8344118914807379f;
    Ap ua0, ua1, ub0, ub1, da0, da1, db0, db1;
    float prevOdd = 0.f;
    void reset() { *this = Os2(); }
    void up(float x, float& e, float& o) { e = ua1.run(ua0.run(x, kA0), kA1); o = ub1.run(ub0.run(x, kB0), kB1); }
    float down(float e, float o) {
        const float a = da1.run(da0.run(e, kA0), kA1);
        const float b = db1.run(db0.run(prevOdd, kB0), kB1);
        prevOdd = o;
        return 0.5f * (a + b);
    }
};

class Comp {
public:
    void init(double sampleRate, int channels) {
        fs_ = sampleRate;
        nch_ = std::min(2, std::max(1, channels));
        uint32_t sz = 1;
        while (sz < uint32_t(0.012 * fs_) + 8) sz <<= 1;
        for (int c = 0; c < 2; ++c) {
            la_[c].assign(sz, 0.f);
        }
        laMask_ = sz - 1;
        clear();
    }

    void clear() {
        for (int c = 0; c < 2; ++c) {
            std::fill(la_[c].begin(), la_[c].end(), 0.f);
            adaa_[c].reset();
            tp_[c].reset();
            os_[c].reset();
            grSlow_[c] = 0.f;
            for (int k = 0; k < 4; ++k) sc_[c][k] = 0.f;
            msq_[c] = pk_[c] = 0.f;
            grEnv_[c] = gEnv_[c] = grAvg_[c] = 0.f;
        }
        laW_ = 0;
        meterGr_ = 0.f; meterIn_ = meterOut_ = 0.f;
    }

    // in/out: pointers to the channels (1 or 2). Returns nothing; the meters are read with the functions below.
    void process(const float* const* in, float* const* out, uint32_t n, const Params& p) {
#if defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        uint64_t fpcrNew = fpcr | (1ull << 24);
        asm volatile("msr fpcr, %0" ::"r"(fpcrNew));
#elif defined(__x86_64__) || defined(__i386__)
        unsigned csr = _mm_getcsr();
        _mm_setcsr(csr | 0x8040);
#endif
        const float fs = float(fs_);
        const int type = std::min(3, std::max(0, p.type));
        // effective parameters per type
        float atk = std::max(0.02f, p.attackMs), rel = std::max(2.f, p.releaseMs);
        float knee = clampf(p.kneeDb, 0.f, 24.f);
        float ar = clampf(p.autoRel, 0.f, 1.f);
        float ratio = clampf(p.ratio, 1.f, 100.f);
        float fetDrive = 0.f, muBias = 0.f;
        if (type == 1) {  // Opto
            atk = std::max(atk, 6.f);
            knee = std::max(knee, 10.f);
            ar = std::max(ar, 0.75f);
            ratio = 1.f + (ratio - 1.f) * 0.65f;
        } else if (type == 2) {  // FET
            knee = std::min(knee, 2.f);
            atk = std::max(0.02f, atk * 0.5f);
            rel = rel * 0.7f;
            fetDrive = 0.05f;
        } else if (type == 3) {  // Vari-Mu
            atk = std::max(atk, 12.f);
            knee = std::max(knee, 14.f);
            ar = std::max(ar, 0.5f);
            muBias = 0.03f;
        }
        const float aA = 1.f - std::exp(-1.f / (atk * 1e-3f * fs));
        const float aR0 = rel * 1e-3f * fs;
        const float gClose = 1.f - std::exp(-1.f / (std::max(10.f, p.gateRelMs) * 1e-3f * fs));
        const float gOpen = 1.f - std::exp(-1.f / (0.002f * fs));
        const float avgC = 1.f / (0.8f * fs);
        const float rmsC = 1.f / (std::max(1.f, p.rmsMs) * 1e-3f * fs);
        const float pkDecay = std::exp(-1.f / (0.005f * fs));
        const float aSlowA = 1.f - std::exp(-1.f / (0.12f * fs));
        const float aSlowR = 1.f - std::exp(-1.f / (2.5f * clampf(rel / 150.f, 0.3f, 3.f) * fs));
        const float inG = std::pow(10.f, clampf(p.inputDb, -48.f, 48.f) / 20.f);
        const float outG = std::pow(10.f, clampf(p.outputDb, -48.f, 24.f) / 20.f);
        float makeup = p.makeupDb;
        if (p.autoMakeup) makeup += std::max(0.f, -p.thresholdDb * (1.f - 1.f / ratio) * 0.5f);
        const float muG = std::pow(10.f, clampf(makeup, -24.f, 48.f) / 20.f);
        const float mix = clampf(p.mix, 0.f, 1.f);
        const float link = clampf(p.link, 0.f, 1.f);
        const bool msm = p.ms && nch_ == 2;
        const int la = std::min(int(laMask_) - 4, std::max(0, int(std::lround(clampf(p.lookMs, 0.f, 5.f) * 1e-3f * fs))));
        latency_ = float(la);
        const float satK = 1.f + 5.f * clampf(p.sat, 0.f, 1.f);
        const bool satOn = p.sat > 0.002f || fetDrive > 0.f || muBias > 0.f;
        setupSidechain(clampf(p.scHpf, 20.f, 500.f), clampf(p.scShelfDb, -12.f, 12.f));
        const bool hpOn = p.scHpf > 20.5f, shOn = std::fabs(p.scShelfDb) > 0.05f;
        const float T = p.thresholdDb;
        float peakGr = 0.f, peakIn = 0.f, peakOut = 0.f;

        for (uint32_t t = 0; t < n; ++t) {
            float x[2] = {0.f, 0.f};
            for (int c = 0; c < nch_; ++c) x[c] = in[c][t] * inG;
            if (msm) {
                float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]);
                x[0] = m; x[1] = s;
            }
            // sidechain and detection
            float lv[2] = {0.f, 0.f};
            for (int c = 0; c < nch_; ++c) {
                float s = x[c];
                if (shOn) s = biq(sh_, sc_[c] + 0, s);
                if (hpOn) s = biq(hp_, sc_[c] + 2, s);
                const float tpk = tp_[c].push(s);  // true peak (for Peak and Hybrid)
                float a = p.detector == 1 ? std::fabs(s) : tpk;
                pk_[c] = std::max(a, pk_[c] * pkDecay);
                msq_[c] += rmsC * (s * s - msq_[c]);
                float rms = std::sqrt(std::max(0.f, msq_[c]));
                lv[c] = p.detector == 0 ? a : (p.detector == 1 ? rms : 0.5f * (pk_[c] + rms));
                peakIn = std::max(peakIn, std::fabs(x[c]));
            }
            if (nch_ == 2) {
                float mx = std::max(lv[0], lv[1]);
                lv[0] = lv[0] + link * (mx - lv[0]);
                lv[1] = lv[1] + link * (mx - lv[1]);
            }
            float y[2] = {0.f, 0.f};
            for (int c = 0; c < nch_; ++c) {
                float xdb = 20.f * std::log10(lv[c] + 1e-9f);
                // static curve
                float over = xdb - T;
                float R = ratio;
                if (type == 3) {
                    float u = clampf((over + 0.5f * knee) / (knee + 12.f), 0.f, 1.f);
                    R = 1.f + (ratio - 1.f) * u * u * (3.f - 2.f * u);
                }
                float gr = grStatic(xdb, T, R, knee);
                // ballistics with program-dependent release
                float rel_eff = aR0 * (1.f - ar + ar * (0.2f + 1.8f * clampf(grAvg_[c] / 10.f, 0.f, 1.f)));
                float aR = 1.f - std::exp(-1.f / std::max(1.f, rel_eff));
                grEnv_[c] += (gr > grEnv_[c] ? aA : aR) * (gr - grEnv_[c]);
                grAvg_[c] += avgC * (grEnv_[c] - grAvg_[c]);
                // gate / expander
                float gt = 0.f;
                if (p.gateThrDb > -89.f && xdb < p.gateThrDb)
                    gt = std::min(clampf(p.gateRangeDb, 0.f, 80.f), (p.gateThrDb - xdb) * (std::max(1.f, p.gateRatio) - 1.f));
                gEnv_[c] += (gt > gEnv_[c] ? gClose : gOpen) * (gt - gEnv_[c]);
                float grNow = grEnv_[c];
                if (type == 1) {  // two-stage Opto: slow envelope that holds the release tail
                    grSlow_[c] += (gr > grSlow_[c] ? aSlowA : aSlowR) * (gr - grSlow_[c]);
                    grNow = std::max(grNow, 0.45f * grSlow_[c]);
                }
                float tot = grNow + gEnv_[c];
                peakGr = std::max(peakGr, tot);
                // delayed audio (lookahead)
                la_[c][laW_ & laMask_] = x[c];
                float xd = la_[c][(laW_ - uint32_t(la)) & laMask_];
                float g = std::pow(10.f, -tot / 20.f);
                float w = xd * g * muG;
                if (satOn) {
                    float k = satK + fetDrive * grEnv_[c];
                    float b = 0.1f * clampf(p.sat, 0.f, 1.f) + muBias;
                    float e, o;  // 2x oversampled saturator with ADAA
                    os_[c].up(w, e, o);
                    const float ve = adaa_[c].process(e, k, b);
                    const float vo = adaa_[c].process(o, k, b);
                    w = os_[c].down(ve, vo);
                } else {
                    adaa_[c].reset();
                    os_[c].reset();
                }
                y[c] = (1.f - mix) * xd + mix * w;
            }
            ++laW_;
            if (msm) {
                float l = y[0] + y[1], r = y[0] - y[1];
                y[0] = l; y[1] = r;
            }
            for (int c = 0; c < nch_; ++c) {
                float o = y[c] * outG;
                out[c][t] = o;
                peakOut = std::max(peakOut, std::fabs(o));
            }
        }
        // meter: simple per-block ballistics
        meterGr_ = std::max(peakGr, meterGr_ * 0.985f);
        meterIn_ = std::max(peakIn, meterIn_ * 0.985f);
        meterOut_ = std::max(peakOut, meterOut_ * 0.985f);
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(csr);
#endif
    }

    float grDb() const { return meterGr_; }
    float inDb() const { return 20.f * std::log10(meterIn_ + 1e-6f); }
    float outDb() const { return 20.f * std::log10(meterOut_ + 1e-6f); }
    float latencySamples() const { return latency_; }

private:
    struct Bq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static float grStatic(float x, float T, float R, float W) {
        float over = x - T;
        float k = 1.f - 1.f / R;
        if (W < 0.01f) return over > 0.f ? over * k : 0.f;
        if (2.f * over < -W) return 0.f;
        if (2.f * std::fabs(over) <= W) {
            float t = over + 0.5f * W;
            return k * t * t / (2.f * W);
        }
        return k * over;
    }

    static float biq(const Bq& q, float* z, float x) {
        float y = q.b0 * x + z[0];
        z[0] = q.b1 * x - q.a1 * y + z[1];
        z[1] = q.b2 * x - q.a2 * y;
        return y;
    }

    void setupSidechain(float hpf, float shelfDb) {
        const float fs = float(fs_);
        {  // 2nd-order Butterworth high-pass
            float w0 = 6.2831853f * hpf / fs, cw = std::cos(w0), al = std::sin(w0) / (2.f * 0.70710678f);
            float a0 = 1.f + al;
            hp_.b0 = (1.f + cw) * 0.5f / a0; hp_.b1 = -(1.f + cw) / a0; hp_.b2 = hp_.b0;
            hp_.a1 = -2.f * cw / a0; hp_.a2 = (1.f - al) / a0;
        }
        {  // high shelf at 2.5 kHz
            float A = std::pow(10.f, shelfDb / 40.f);
            float w0 = 6.2831853f * std::min(2500.f, 0.45f * fs) / fs, cw = std::cos(w0);
            float al = std::sin(w0) / 2.f * std::sqrt(2.f), sq = 2.f * std::sqrt(A) * al;
            float b0 = A * ((A + 1.f) + (A - 1.f) * cw + sq), b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw),
                  b2 = A * ((A + 1.f) + (A - 1.f) * cw - sq);
            float a0 = (A + 1.f) - (A - 1.f) * cw + sq, a1 = 2.f * ((A - 1.f) - (A + 1.f) * cw),
                  a2 = (A + 1.f) - (A - 1.f) * cw - sq;
            sh_.b0 = b0 / a0; sh_.b1 = b1 / a0; sh_.b2 = b2 / a0; sh_.a1 = a1 / a0; sh_.a2 = a2 / a0;
        }
    }

    double fs_ = 48000.0;
    int nch_ = 1;
    std::vector<float> la_[2];
    uint32_t laMask_ = 0, laW_ = 0;
    Bq hp_, sh_;
    dspkit::AdaaTanh adaa_[2];
    TruePeak tp_[2];
    Os2 os_[2];
    float grSlow_[2] = {};
    float sc_[2][4] = {};
    float msq_[2] = {}, pk_[2] = {}, grEnv_[2] = {}, gEnv_[2] = {}, grAvg_[2] = {};
    float meterGr_ = 0.f, meterIn_ = 0.f, meterOut_ = 0.f, latency_ = 0.f;
};

}  // namespace adelcomp
