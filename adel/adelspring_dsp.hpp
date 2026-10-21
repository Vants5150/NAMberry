// ADELspring: spring reverb (amplifier-style tank).
// Model: from 1 to 3 springs in parallel; each spring is a loop with
//   - chain of "stretched" all-pass filters (Parker / Valimaki): low frequencies are delayed more than high ones, so an impulse
//     becomes a descending chirp (the "drip" / "boing"), which repeats at every round of the loop
//   - delay line (spring round-trip time), loop loss with damping of the highs (decay)
//   - slight random wobble of the length
// Before the springs: input saturation (Dwell), soft and with ADAA anti-aliasing. After: pickup band (high-pass and low-pass).
// The dry passes identical (no latency). Mix: "Keep Dry" (fixed dry, constant volume) or "Crossfade".
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dspkit.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace adelspring {

static const int kMaxSprings = 3;
static const int kMaxStages = 72;
static const int kStretch = 6;      // each stage uses z^-6: chirp transition at fs/12 = 4 kHz

struct Params {
    float mix = 0.35f;        // 0..1
    int mixMode = 0;          // 0 = Keep Dry, 1 = Crossfade
    bool wetOnly = false;
    float outLevelDb = 0.f;
    int tank = 1;             // 0 small, 1 medium, 2 long
    int springs = 3;          // 1..3
    float decay = 2.2f;       // s (nominal RT60 at about 1 kHz)
    float drip = 0.6f;        // 0..1: chirp strength
    float dwell = 0.35f;      // 0..1: input drive (saturation)
    float damp = 0.4f;        // 0..1: damping of the highs in the loop
    float lowCut = 150.f;
    float highCut = 4500.f;
    float wobble = 0.2f;      // 0..1
    bool shake = false;       // rising edge: hit on the tank
};

class Tank {
public:
    void init(double fs) {
        fs_ = float(fs);
        monoOut_ = false;
        clear();
    }
    void setMonoOut(bool m) { monoOut_ = m; }

    // Complete state as a float vector (for diagnosis: freeze the state on the board and replay it on the PC). Each spring: loop (8192), xh, yh, lp, hpx, hpy, wob, a, w, n%6.
    static size_t stateFloats() { return 1 + size_t(kMaxSprings) * (8192 + 2 * size_t(kMaxStages) * kStretch + 7) + 24; }
    void exportState(float* o) const {
        size_t k = 0;
        o[k++] = 1.f;
        for (int s = 0; s < kMaxSprings; ++s) {
            const Spring& sp = sp_[s];
            for (int i = 0; i < 8192; ++i) o[k++] = sp.buf[size_t(i)];
            for (int m = 0; m < kMaxStages; ++m) for (int j = 0; j < kStretch; ++j) o[k++] = sp.xh[m][j];
            for (int m = 0; m < kMaxStages; ++m) for (int j = 0; j < kStretch; ++j) o[k++] = sp.yh[m][j];
            o[k++] = sp.lp; o[k++] = sp.hpx; o[k++] = sp.hpy; o[k++] = sp.wob; o[k++] = sp.a;
            o[k++] = float(sp.w & 8191u); o[k++] = float(sp.n % uint32_t(kStretch));
        }
        for (int c = 0; c < 2; ++c) o[k++] = hp1_[c];
        for (int c = 0; c < 2; ++c) o[k++] = hp2_[c];
        for (int c = 0; c < 2; ++c) o[k++] = lp1_[c];
        for (int c = 0; c < 2; ++c) o[k++] = lp2_[c];
        for (int c = 0; c < 2; ++c) o[k++] = hpi1_[c];
        for (int c = 0; c < 2; ++c) o[k++] = hpi2_[c];
        const float hi = float(adaa_.xprev);
        o[k++] = hi; o[k++] = float(adaa_.xprev - double(hi)); o[k++] = adaa_.init ? 1.f : 0.f;
        o[k++] = float(shakeCnt_); o[k++] = prevShake_ ? 1.f : 0.f; o[k++] = float(lastTank_); o[k++] = float(lastSprings_);
        o[k++] = dryS_; o[k++] = mixS_;
        o[k++] = float(seed_ & 0xFFFFu); o[k++] = float(seed_ >> 16);
        o[k++] = first_ ? 1.f : 0.f;
    }
    void importState(const float* in) {
        size_t k = 1;
        for (int s = 0; s < kMaxSprings; ++s) {
            Spring& sp = sp_[s];
            sp.buf.assign(8192, 0.f); sp.mask = 8191;
            for (int i = 0; i < 8192; ++i) sp.buf[size_t(i)] = in[k++];
            for (int m = 0; m < kMaxStages; ++m) for (int j = 0; j < kStretch; ++j) sp.xh[m][j] = in[k++];
            for (int m = 0; m < kMaxStages; ++m) for (int j = 0; j < kStretch; ++j) sp.yh[m][j] = in[k++];
            sp.lp = in[k++]; sp.hpx = in[k++]; sp.hpy = in[k++]; sp.wob = in[k++]; sp.a = in[k++];
            sp.w = uint32_t(in[k++]); sp.n = uint32_t(in[k++]);
        }
        for (int c = 0; c < 2; ++c) hp1_[c] = in[k++];
        for (int c = 0; c < 2; ++c) hp2_[c] = in[k++];
        for (int c = 0; c < 2; ++c) lp1_[c] = in[k++];
        for (int c = 0; c < 2; ++c) lp2_[c] = in[k++];
        for (int c = 0; c < 2; ++c) hpi1_[c] = in[k++];
        for (int c = 0; c < 2; ++c) hpi2_[c] = in[k++];
        const float hi = in[k++], lo = in[k++];
        adaa_.xprev = double(hi) + double(lo); adaa_.init = in[k++] > 0.5f;
        shakeCnt_ = int(in[k++]); prevShake_ = in[k++] > 0.5f; lastTank_ = int(in[k++]); lastSprings_ = int(in[k++]);
        dryS_ = in[k++]; mixS_ = in[k++];
        seed_ = uint32_t(in[k++]); seed_ |= uint32_t(in[k++]) << 16;
        first_ = in[k++] > 0.5f;
    }

#ifdef ADELSPRING_DEBUG
    // DIAGNOSIS (only with ADELSPRING_DEBUG): at the first sample written into the loop with |v| > 1 while the input is small a snapshot of the state is frozen.
    // It re-arms after 100 ms below 0.2. It is read from the page (the block's /m?b=<id>) through CFx::GetOutputs.
    struct Dbg {
        float events = 0, calls = 0, spr = -1;
        float x = 0, xd = 0, hp = 0, v = 0, a = 0, wob = 0, d = 0, yo = 0, g = 0;
        float firstStage = -1, firstXn = 0, firstYn = 0;
        float maxXh = 0, maxXhAt = -1, maxYh = 0, maxYhAt = -1;
        float bufMax = 0, bufMaxAt = -1, fpcrBits = -1, n = 0, w = 0;
        float maxIn = 0, maxOutV = 0;
        float ev2 = 0, yo2 = 0, d2 = 0, w2 = 0, bufMax2 = 0, bufMaxAt2 = -1, readIdx2 = 0, spr2 = -1, v2 = 0, x2 = 0, bufAround = 0;
    } dbg;
    // sample-by-sample record of the last 4096 samples: 0 = input x, 1 = saturator output xd, 2 = loop read (spring 0), 3 = hp feedback (spring 0), 4 = write v (spring 0)
    static const int kDbgN = 4096;
    float dbgR_[5][4096];
    uint32_t dbgPos_ = 0;
    void exportDbg(float* o) const { for (int r = 0; r < 5; ++r) for (int i = 0; i < kDbgN; ++i) o[r * kDbgN + i] = dbgR_[r][(dbgPos_ + uint32_t(i)) & 4095u]; }
    int dbgQuiet_ = 0;
    bool dbgArmed_ = true, dbgArmed2_ = true;
    int dbgQuiet2_ = 0;
#endif
    void clear() {
        for (int s = 0; s < kMaxSprings; ++s) {
            sp_[s].buf.assign(8192, 0.f);
            sp_[s].mask = 8191;
            sp_[s].w = 0;
            sp_[s].n = 0;
            sp_[s].lp = sp_[s].hpx = sp_[s].hpy = sp_[s].wob = 0.f;
            std::memset(sp_[s].xh, 0, sizeof(sp_[s].xh));
            std::memset(sp_[s].yh, 0, sizeof(sp_[s].yh));
            sp_[s].a = -0.6f;
        }
        for (int c = 0; c < 2; ++c) {
            hp1_[c] = hp2_[c] = lp1_[c] = lp2_[c] = 0.f;
            hpi1_[c] = hpi2_[c] = 0.f;
        }
        adaa_.reset();
        shakeCnt_ = 0;
        prevShake_ = false;
        lastTank_ = -1;
        lastSprings_ = -1;
        dryS_ = 1.f;
        mixS_ = 0.f;
        first_ = true;
    }

    // n samples: mono input; mono output (outL) or stereo (outL/outR)
    void process(const float* in, float* outL, float* outR, uint32_t n, const Params& p) {
#if defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        asm volatile("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));   // flush to zero
#ifdef ADELSPRING_DEBUG
        { uint64_t f2; asm volatile("mrs %0, fpcr" : "=r"(f2)); dbg.fpcrBits = float((f2 >> 22) & 0xF); dbg.calls += 1.f; }
#endif
#elif defined(__x86_64__) || defined(__i386__)
        const unsigned csr = _mm_getcsr();
        _mm_setcsr(csr | 0x8040);
#endif
        const int tank = std::max(0, std::min(2, p.tank));
        const int S = std::max(1, std::min(kMaxSprings, p.springs));
        if (tank != lastTank_ || S != lastSprings_) {          // change of tank or of number of springs: the loops are zeroed
            const bool f = first_;
            clearSprings();
            lastTank_ = tank;
            lastSprings_ = S;
            first_ = f;
        }
        static const int kStages[3] = {40, 56, 72};
        static const float kLenScale[3] = {0.72f, 1.0f, 1.32f};
        static const int kBaseLen[kMaxSprings] = {1470, 1697, 1999};
        static const float kAFactor[kMaxSprings] = {1.00f, 1.04f, 0.95f};
        const int M = kStages[tank];
        const float rate = fs_ / 48000.f;
        const float drip = clampf(p.drip, 0.f, 1.f);
        const float damp = clampf(p.damp, 0.f, 1.f);
        const float dampC = 0.90f - 0.78f * damp;                  // coefficient of the low-pass in the loop
        const float rt = clampf(p.decay, 0.3f, 8.f);
        float g[kMaxSprings], aT[kMaxSprings], len[kMaxSprings];
        for (int s = 0; s < S; ++s) {
            len[s] = std::max(64.f, float(kBaseLen[s]) * kLenScale[tank] * rate);
            aT[s] = -(0.45f + 0.30f * drip) * kAFactor[s];
            // effective round-trip time: line delay + group delay of the chain at about 600 Hz
            const float w600 = 6.2831853f * 600.f / fs_;
            const float a = aT[s];
            const float tauStage = float(kStretch) * (1.f - a * a) / (1.f + 2.f * a * std::cos(float(kStretch) * w600) + a * a);
            const float loop = (len[s] + float(M) * tauStage) / fs_;
            g[s] = std::pow(10.f, -3.f * loop / (rt * 1.22f));   // 1.22: compensates the loss added by the damping of the highs
            g[s] = std::min(g[s], 0.985f);
        }
        const bool keepDry = p.mixMode == 0 && !p.wetOnly;
        const float mixT = p.wetOnly ? 1.f : clampf(p.mix, 0.f, 1.f) * (keepDry ? kKeepDryBoost : 1.f);
        const float dryT = p.wetOnly ? 0.f : (keepDry ? 1.f : 1.f - clampf(p.mix, 0.f, 1.f));
        const float outG = std::pow(10.f, clampf(p.outLevelDb, -24.f, 12.f) / 20.f);
        const float dwell = clampf(p.dwell, 0.f, 1.f);
        const float kd = 1.f + 6.f * dwell * dwell;                // slope of the saturation
        const float makeup = std::sqrt(kd);
        const float wob = clampf(p.wobble, 0.f, 1.f);
        const float hpC = 1.f - std::exp(-6.2831853f * clampf(p.lowCut, 20.f, 1000.f) / fs_);
        const float lpC = 1.f - std::exp(-6.2831853f * clampf(p.highCut, 1000.f, 12000.f) / fs_);
        if (first_) {
            dryS_ = dryT;
            mixS_ = mixT;
            for (int s = 0; s < kMaxSprings; ++s) sp_[s].a = aT[std::min(s, S - 1)];
            first_ = false;
        }
        if (p.shake && !prevShake_) shakeCnt_ = int(0.030f * fs_);
        prevShake_ = p.shake;
        const float sm = 1.f - std::exp(-1.f / (0.005f * fs_));
        const float ss = 1.f - std::exp(-1.f / (0.04f * fs_));
        for (uint32_t t = 0; t < n; ++t) {
            const float x = in[t];
            dryS_ += sm * (dryT - dryS_);
            mixS_ += sm * (mixT - mixS_);
            float xs = x;
            if (shakeCnt_ > 0) {
                xs += 0.5f * noise() * float(shakeCnt_) / (0.030f * fs_);
                --shakeCnt_;
            }
            const float xd = adaa_.process(xs, kd, 0.0) * makeup * 0.6f;   // input saturation (Dwell)
            float y[kMaxSprings] = {0.f, 0.f, 0.f};
            for (int s = 0; s < S; ++s) {
                Spring& sp = sp_[s];
                sp.a += ss * (aT[s] - sp.a);
                // wobble: very slow filtered noise that modulates the length
                sp.wob += 4e-5f * (noise() - sp.wob);
                const float d = len[s] + wob * 3.f * clampf(sp.wob * 140.f, -1.f, 1.f);
                float yo = dspkit::readLagrange8(sp.buf.data(), sp.mask, sp.w, d);
                sp.lp += dampC * (yo - sp.lp);
                float fb = sp.lp;
                // DC blocker in the loop
                const float hp = fb - sp.hpx + 0.995f * sp.hpy;
                sp.hpx = fb;
                sp.hpy = hp;
                float v = xd + g[s] * hp;
                const int idx = int(sp.n % uint32_t(kStretch));
                const float a = sp.a;
#ifdef ADELSPRING_DEBUG
                int dbgFirst = -1; float dbgFx = 0.f, dbgFy = 0.f;
#endif
                for (int m = 0; m < M; ++m) {                    // chain of stretched all-passes: the chirp
                    const float xn = v;
                    const float yn = a * xn + sp.xh[m][idx] - a * sp.yh[m][idx];
                    sp.xh[m][idx] = xn;
                    sp.yh[m][idx] = yn;
                    v = yn;
#ifdef ADELSPRING_DEBUG
                    if (dbgFirst < 0 && (yn > 1.f || yn < -1.f)) { dbgFirst = m; dbgFx = xn; dbgFy = yn; }
#endif
                }
#ifdef ADELSPRING_DEBUG
                {
                    const float av = v < 0.f ? -v : v, ax = x < 0.f ? -x : x;
                    if (ax > dbg.maxIn) dbg.maxIn = ax;
                    if (av > dbg.maxOutV) dbg.maxOutV = av;
                    if (dbgArmed_ && av > 1.f && ax < 0.05f) {
                        dbgArmed_ = false; dbgQuiet_ = 0;
                        dbg.events += 1.f; dbg.spr = float(s); dbg.x = x; dbg.xd = xd; dbg.hp = hp; dbg.v = v; dbg.a = a; dbg.wob = sp.wob; dbg.d = d; dbg.g = g[s];
                        dbg.firstStage = float(dbgFirst); dbg.firstXn = dbgFx; dbg.firstYn = dbgFy; dbg.n = float(sp.n); dbg.w = float(sp.w & 0xFFFFu);
                        float mx = 0.f, my = 0.f; int ix = -1, iy = -1;
                        for (int m = 0; m < M; ++m) for (int k = 0; k < kStretch; ++k) {
                            const float ax2 = sp.xh[m][k] < 0.f ? -sp.xh[m][k] : sp.xh[m][k], ay2 = sp.yh[m][k] < 0.f ? -sp.yh[m][k] : sp.yh[m][k];
                            if (ax2 > mx) { mx = ax2; ix = m; }
                            if (ay2 > my) { my = ay2; iy = m; }
                        }
                        dbg.maxXh = mx; dbg.maxXhAt = float(ix); dbg.maxYh = my; dbg.maxYhAt = float(iy);
                        float bm = 0.f; int bi = -1;
                        for (uint32_t i2 = 0; i2 <= sp.mask; ++i2) { const float q = sp.buf[i2] < 0.f ? -sp.buf[i2] : sp.buf[i2]; if (q > bm) { bm = q; bi = int(i2); } }
                        dbg.bufMax = bm; dbg.bufMaxAt = float(bi);
                    } else if (!dbgArmed_) {
                        if (av < 0.2f) { if (++dbgQuiet_ > 4800) dbgArmed_ = true; } else dbgQuiet_ = 0;
                    }
                }
#endif
                ++sp.n;
                sp.buf[sp.w & sp.mask] = v;
                ++sp.w;
                y[s] = yo;
#ifdef ADELSPRING_DEBUG
                if (s == 0) { const uint32_t dp = dbgPos_ & 4095u; dbgR_[2][dp] = yo; dbgR_[3][dp] = hp; dbgR_[4][dp] = v; }
#endif
#ifdef ADELSPRING_DEBUG
                {
                    const float ay = yo < 0.f ? -yo : yo, ax = x < 0.f ? -x : x;
                    if (dbgArmed2_ && ay > 1.f && ax < 0.05f) {
                        dbgArmed2_ = false; dbgQuiet2_ = 0;
                        dbg.ev2 += 1.f; dbg.yo2 = yo; dbg.d2 = d; dbg.w2 = float(sp.w & 0xFFFFu); dbg.spr2 = float(s); dbg.v2 = v; dbg.x2 = x;
                        float bm = 0.f; int bi = -1;
                        for (uint32_t i2 = 0; i2 <= sp.mask; ++i2) { const float q = sp.buf[i2] < 0.f ? -sp.buf[i2] : sp.buf[i2]; if (q > bm) { bm = q; bi = int(i2); } }
                        dbg.bufMax2 = bm; dbg.bufMaxAt2 = float(bi);
                        dbg.readIdx2 = float((sp.w - uint32_t(int32_t(d)) - 1u) & sp.mask);
                        // how many values in the loop exceed 0.5 (a localized disturbance has few, an oscillation of the whole spring many)
                        unsigned cnt = 0; for (uint32_t i2 = 0; i2 <= sp.mask; ++i2) { const float q = sp.buf[i2] < 0.f ? -sp.buf[i2] : sp.buf[i2]; if (q > 0.5f) ++cnt; }
                        dbg.bufAround = float(cnt);
                    } else if (!dbgArmed2_) {
                        if (ay < 0.2f) { if (++dbgQuiet2_ > 4800) dbgArmed2_ = true; } else dbgQuiet2_ = 0;
                    }
                }
#endif
            }
#ifdef ADELSPRING_DEBUG
            { const uint32_t dp = dbgPos_ & 4095u; dbgR_[0][dp] = x; dbgR_[1][dp] = xd; ++dbgPos_; }
#endif
            float wl, wr;
            if (S == 1) { wl = wr = y[0]; }
            else if (S == 2) { wl = y[0]; wr = y[1]; }
            else { wl = 0.85f * y[0] + 0.5f * y[2]; wr = 0.85f * y[1] + 0.5f * y[2]; }
            wl *= kWetGain;
            wr *= kWetGain;
            wl = pickup(0, wl, hpC, lpC);
            wr = pickup(1, wr, hpC, lpC);
            if (monoOut_) {
                outL[t] = outG * (dryS_ * x + mixS_ * 0.70710678f * (wl + wr));
            } else {
                outL[t] = outG * (dryS_ * x + mixS_ * wl);
                outR[t] = outG * (dryS_ * x + mixS_ * wr);
            }
        }
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(csr);
#endif
    }

private:
    struct Spring {
        std::vector<float> buf;
        uint32_t mask = 8191, w = 0, n = 0;
        float lp = 0.f, hpx = 0.f, hpy = 0.f, wob = 0.f, a = -0.6f;
        float xh[kMaxStages][kStretch];
        float yh[kMaxStages][kStretch];
    };

    // level calibration: the reverb at Mix 100% with Keep Dry comes close to the dry level
    static constexpr float kWetGain = 0.7f;
    static constexpr float kKeepDryBoost = 1.0f;

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    float noise() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return float(int32_t(seed_)) * (1.f / 2147483648.f);
    }

    void clearSprings() {
        for (int s = 0; s < kMaxSprings; ++s) {
            std::fill(sp_[s].buf.begin(), sp_[s].buf.end(), 0.f);
            sp_[s].w = 0;
            sp_[s].n = 0;
            sp_[s].lp = sp_[s].hpx = sp_[s].hpy = sp_[s].wob = 0.f;
            std::memset(sp_[s].xh, 0, sizeof(sp_[s].xh));
            std::memset(sp_[s].yh, 0, sizeof(sp_[s].yh));
        }
    }

    // pickup band: second-order high-pass and low-pass (two real poles)
    float pickup(int c, float x, float hpC, float lpC) {
        hp1_[c] += hpC * (x - hp1_[c]);
        float y = x - hp1_[c];
        hp2_[c] += hpC * (y - hp2_[c]);
        y -= hp2_[c];
        lp1_[c] += lpC * (y - lp1_[c]);
        lp2_[c] += lpC * (lp1_[c] - lp2_[c]);
        return lp2_[c];
    }

    float fs_ = 48000.f;
    bool monoOut_ = false, first_ = true, prevShake_ = false;
    Spring sp_[kMaxSprings];
    float hp1_[2] = {}, hp2_[2] = {}, lp1_[2] = {}, lp2_[2] = {}, hpi1_[2] = {}, hpi2_[2] = {};
    dspkit::AdaaTanh adaa_;
    int shakeCnt_ = 0, lastTank_ = -1, lastSprings_ = -1;
    float dryS_ = 1.f, mixS_ = 0.f;
    uint32_t seed_ = 2463534242u;
};

}  // namespace adelspring
