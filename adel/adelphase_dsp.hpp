// ADELphase: phase/time alignment between two paths (for example two NAMs in parallel)
// Tools: polarity, fractional delay (Lagrange8), phase rotation with all-pass filters, level trim,
// measurement of the correlation between the two paths, verdict (whether and how to touch the phase) and automatic alignment.
// Two measurement modes:
//  - stereo version (L/R split): sees the two lanes as L and R;
//  - mono version with Lane A / Lane B role (Mix split): the two instances, one per lane, exchange the (corrected) samples
//    through a bus shared in the process and the Lane B instance does the analysis.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "adel_probe_shm.hpp"
#include "dspkit.hpp"

namespace adelphase {

static const int kK = 96;           // maximum measurable delay: +-96 samples (+-2 ms at 48 kHz)
static const int kLags = 2 * kK + 1;
static const uint32_t kMask = 4095;
static const int kBase = 3;         // interpolator's minimum delay (samples): fixed latency

struct Params {
    bool invert = false;
    float delayMs = 0.f;     // positive = delays the R path (or the only path in the mono version); negative = delays L
    float apFreq = 300.f;    // all-pass frequency
    int apStages = 0;        // 0..4, each rotates the phase by -90 degrees at frequency apFreq
    float trimDb = 0.f;      // level of the R path
    int mode = 0;            // 0 = passthrough, 1 = sum on both, 2 = sum on L (stereo only)
    bool autoAlign = false;
    int role = 0;            // mono only: 0 = standalone, 1 = Lane A, 2 = Lane B
};

struct AllPass1 {
    float x1 = 0.f, y1 = 0.f;
    inline float process(float x, float a) {
        float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// ---- Bus shared between the Lane A / Lane B instances (same process, same copy of the library) ----
static const uint32_t kBusN = 16384;
static const uint32_t kBusMask = kBusN - 1;

struct Bus {
    float ring[2][kBusN];
    std::atomic<uint64_t> G{0};                  // global position (in frames) of the start of the next audio cycle
    std::atomic<uint64_t> flagG[2];              // the lane has opened the cycle that ends at this value and the other has not reached it yet
    std::atomic<uint64_t> end[2];                // frames completed by each lane
    std::atomic<float> d[2];                     // applied delay (samples)
    std::atomic<int> inv[2];                     // polarity inverted
    std::atomic<float> verdict{0.f}, loss{0.f}, conf{0.f}, corrIn{0.f}, lag{0.f};
    std::atomic<float> sugMs[2];
    std::atomic<float> sugInv[2];
    std::atomic<int> apS[2];                     // phase rotation applied (stages, frequency)
    std::atomic<float> apF[2];
    std::atomic<float> ctlDelay[2];              // control values (to notice that something changed after the measurement)
    std::atomic<float> ctlTrim[2];
    std::atomic<float> sugStages[2];
    std::atomic<float> sugFreq[2];
    Bus() {
        std::memset(ring, 0, sizeof(ring));
        for (int i = 0; i < 2; ++i) {
            flagG[i] = 0;
            end[i] = 0;
            d[i] = 0.f;
            inv[i] = 0;
            sugMs[i] = 0.f;
            sugInv[i] = 0.f;
            apS[i] = 0;
            apF[i] = 300.f;
            ctlDelay[i] = 0.f;
            ctlTrim[i] = 0.f;
            sugStages[i] = 0.f;
            sugFreq[i] = 0.f;
        }
    }
};
inline Bus& bus() {
    static Bus b;
    return b;
}

// ---- Estimator: cross-correlation between two paths, verdict and suggestions ----
class Estimator {
public:
    void init(float fs) {
        fs_ = fs;
        lam_ = std::exp(-2.f / (6.f * fs));        // measurement over 6 s, updated every 2 samples; stops when there is no signal
        lamS_ = std::exp(-1.f / (6.f * fs));       // same, updated at every sample (bands)
        static const float centers[kBands] = {141.f, 283.f, 566.f, 1131.f, 2263.f, 4525.f};
        for (int k = 0; k < kBands; ++k) {
            // second-order band-pass (RBJ, peak gain 0 dB), Q = 1.4
            const float w0 = 2.f * float(M_PI) * centers[k] / fs, al = std::sin(w0) / (2.f * 1.4f), a0 = 1.f + al;
            bd_[k].b0 = al / a0;
            bd_[k].b1 = 0.f;
            bd_[k].b2 = -al / a0;
            bd_[k].a1 = -2.f * std::cos(w0) / a0;
            bd_[k].a2 = (1.f - al) / a0;
            bd_[k].N = std::max(1, std::min(127, int(fs / (4.f * centers[k]) + 0.5f)));      // quarter period
            bd_[k].f = centers[k];
        }
        clear();
    }
    // the per-band analysis only makes sense if the lanes are measured AFTER the correction (Lane mode)
    void enableBands(bool e) { bands_ = e; }
    // bands only: skips the correlation on the delays (only the per-band phase analysis is needed, as for the rotation on the corrected outputs)
    void setBandsOnly(bool e) { bandsOnly_ = e; }
    void clear() {
        for (auto& b : bd_) {
            b.l1 = b.l2 = b.r1 = b.r2 = 0.f;
            std::memset(b.dly, 0, sizeof(b.dly));
            b.c0 = b.cq = b.ell = b.err = 0.f;
        }
        dpos_ = 0;
        std::memset(hL_, 0, sizeof(hL_));
        std::memset(hR_, 0, sizeof(hR_));
        std::memset(C_, 0, sizeof(C_));
        hp_ = 0;
        eL_ = eR_ = 1e-12f;
        sDA_ = sDB_ = sPA_ = sPB_ = 0.f;
        pxL_ = pxR_ = 0.f;
        parity_ = 0;
        silent_ = 1 << 30;
        pres_ = 0.f;
        verdict_ = loss_ = lag_ = conf_ = corrIn_ = 0.f;
        neg_ = false;
        valid_ = false;
    }

    // resets the measurement in progress (accumulators), keeping the last outcome until a new signal arrives
    void reset() {
        std::memset(C_, 0, sizeof(C_));
        eL_ = eR_ = 1e-12f;
        sDA_ = sDB_ = sPA_ = sPB_ = 0.f;
        for (auto& b : bd_) b.c0 = b.cq = b.ell = b.err = 0.f;
        silent_ = 0;
    }

    // one sample per path; the signal is pre-emphasized to sharpen the peak
    // dd = B's delay (samples) minus A's delay applied at that moment, pf = 1 if the polarities of A and B differ:
    // needed in Lane mode, where the measurement is made after the correction, to work back to the absolute value to set
    inline void feed(float xl, float xr, float dA = 0.f, float dB = 0.f, float pA = 0.f, float pB = 0.f) {
        // signal presence: after at least 1.5 s of silence a new signal starts a new measurement
        pres_ += 0.002f * (xl * xl + xr * xr - pres_);
        const bool present = pres_ > 1e-6f;
        if (present) {
            if (silent_ > int(1.5f * fs_)) {
                std::memset(C_, 0, sizeof(C_));
                eL_ = eR_ = 1e-12f;
                sDA_ = sDB_ = sPA_ = sPB_ = 0.f;
                for (auto& b : bd_) b.c0 = b.cq = b.ell = b.err = 0.f;
            }
            silent_ = 0;
        } else if (silent_ < (1 << 30)) {
            ++silent_;
        }
        if (bands_) {
            // relative phase per band: correlation at zero delay and with R delayed by a quarter of a period
            for (auto& b : bd_) {
                const float yl = b.b0 * xl + b.l1;
                b.l1 = b.b1 * xl - b.a1 * yl + b.l2;
                b.l2 = b.b2 * xl - b.a2 * yl;
                const float yr = b.b0 * xr + b.r1;
                b.r1 = b.b1 * xr - b.a1 * yr + b.r2;
                b.r2 = b.b2 * xr - b.a2 * yr;
                b.dly[dpos_] = yr;
                const float yrq = b.dly[(dpos_ - b.N) & 127];
                if (present) {
                    b.c0 = lamS_ * b.c0 + yl * yr;
                    b.cq = lamS_ * b.cq + yl * yrq;
                    b.ell = lamS_ * b.ell + yl * yl;
                    b.err = lamS_ * b.err + yr * yr;
                }
            }
            dpos_ = (dpos_ + 1) & 127;
        }
        if (bandsOnly_) return;
        const float pl = xl - pxL_, pr = xr - pxR_;
        pxL_ = xl;
        pxR_ = xr;
        hp_ = (hp_ + 1) & 255;
        hL_[hp_] = hL_[hp_ + 256] = pl;
        hR_[hp_] = hR_[hp_ + 256] = pr;
        if ((parity_ ^= 1) == 0 && present) {
            const float rd = hR_[hp_ + 256 - kK];       // R delayed by K: allows negative lags
            const float* bl = &hL_[hp_ + 256];
            for (int j = 0; j < kLags; ++j) C_[j] = lam_ * C_[j] + bl[-j] * rd;
            eL_ = lam_ * eL_ + pl * pl;
            eR_ = lam_ * eR_ + pr * pr;
            const float en = pl * pl + pr * pr;
            sDA_ = lam_ * sDA_ + dA * en;
            sDB_ = lam_ * sDB_ + dB * en;
            sPA_ = lam_ * sPA_ + pA * en;
            sPB_ = lam_ * sPB_ + pB * en;
        }
    }

    // average (weighted like the measurement) of the relative delay and of the polarity difference present during the measurement
    float meanDelayA() const { return sDA_ / (eL_ + eR_); }
    float meanDelayB() const { return sDB_ / (eL_ + eR_); }
    bool meanInvertA() const { return sPA_ / (eL_ + eR_) > 0.5f; }
    bool meanInvertB() const { return sPB_ / (eL_ + eR_) > 0.5f; }
    float meanDelayDiff() const { return meanDelayB() - meanDelayA(); }
    bool meanPolDiff() const { return meanInvertA() != meanInvertB(); }

    void update() {
        int jb = 0;
        float best = 0.f;
        for (int j = 0; j < kLags; ++j) {
            const float a = std::fabs(C_[j]);
            if (a > best) { best = a; jb = j; }
        }
        const float norm = std::sqrt(eL_ * eR_);
        const float conf = norm > 1e-9f ? best / norm : 0.f;
        const bool active = eL_ > 1e-9f && eR_ > 1e-9f;
        valid_ = false;
        if (active) conf_ = std::min(1.f, conf) * 100.f;     // when the signal ends the last outcome stays visible
        if (active && conf > 0.15f) {
            float off = 0.f;
            if (jb > 0 && jb < kLags - 1) {
                const float a = std::fabs(C_[jb - 1]), b = best, c = std::fabs(C_[jb + 1]);
                const float den = a - 2.f * b + c;
                if (den < -1e-20f) off = std::max(-0.5f, std::min(0.5f, 0.5f * (a - c) / den));
            }
            lag_ = float(jb - kK) + off;         // samples by which R lags behind L
            neg_ = C_[jb] < 0.f;
            valid_ = true;
        }
        // verdict: how much is lost by summing the two lanes as they are, compared with the best-aligned sum
        if (active) {
            const float tot = eL_ + eR_;
            const float now = tot + 2.f * C_[kK];
            const float bst = tot + 2.f * best;
            float loss = -60.f;
            if (bst > 1e-12f) loss = 10.f * std::log10(std::max(now, 1e-6f * tot) / bst);
            loss_ = std::max(-60.f, std::min(0.f, loss));
            const bool needDelay = std::fabs(lag_) >= 1.f;       // below 1 sample (0.02 ms) the delay is negligible
            if (conf < 0.15f) verdict_ = 6.f;
            else if (loss_ > -1.f) verdict_ = 1.f;
            else if (neg_ && needDelay) verdict_ = 5.f;
            else if (neg_) verdict_ = 2.f;
            else verdict_ = lag_ > 0.f ? 4.f : 3.f;      // R/B late (lag > 0): L/A is delayed (4); R/B early: R/B is delayed (3)
        }
        const float c0 = C_[kK];
        corrIn_ = norm > 1e-9f ? std::max(-1.f, std::min(1.f, c0 / norm)) * 100.f : 0.f;
    }

    float lag() const { return lag_; }
    bool negative() const { return neg_; }
    bool valid() const { return valid_; }
    float confidence() const { return conf_; }
    float verdict() const { return verdict_; }
    float loss() const { return loss_; }
    float corrIn() const { return corrIn_; }

    // diagnostics: one row with frequency, energy share, coherence and phase (degrees) of each band
    int bandReport(char* out, int cap) const {
        int n = 0;
        float tot = 1e-12f;
        for (const auto& b : bd_) tot += b.ell + b.err;
        for (const auto& b : bd_) {
            const float e = b.ell + b.err, nrm = std::sqrt(b.ell * b.err);
            float m = 0.f, ph = 0.f;
            if (nrm > 1e-12f) {
                const float r0 = b.c0 / nrm, rq = b.cq / nrm;
                m = std::sqrt(r0 * r0 + rq * rq);
                ph = std::atan2(-rq, r0) * 57.29578f;
            }
            n += std::snprintf(out + n, size_t(std::max(0, cap - n)), "%.0fHz[en %.0f%% m %.2f ph %.0f] ", b.f, 100.f * e / tot, m, ph);
            if (n >= cap) break;
        }
        return n;
    }

    // ---- Phase rotation (Lane mode only, lanes measured after the correction) ----
    // For each band: relative phase phi (> 0: B lags A in phase), coherence m and weight (energy x coherence).
    // The effect of n all-pass stages at a frequency f0 on A or on B is simulated across ALL the bands (an all-pass rotates by 90 degrees
    // at f0 but also delays the frequencies below) and the combination that most increases the coherent sum is proposed; the
    // rotation is suggested only if the improvement is clear.
    struct Rot {
        bool needed = false;
        int lane = 1, stages = 0;
        float freq = 300.f, gain = 0.f, cur = 0.f, best = 0.f;
    };
    Rot suggestRotation(int sA, float fA, int sB, float fB, bool onlyB = false) const {
        Rot r;
        if (!bands_) return r;
        float phi[kBands], w[kBands], fc[kBands];
        float tot = 1e-12f, wsum = 0.f;
        for (const auto& b : bd_) tot += b.ell + b.err;
        for (int k = 0; k < kBands; ++k) {
            const Band& b = bd_[k];
            const float e = b.ell + b.err, nrm = std::sqrt(b.ell * b.err);
            w[k] = 0.f;
            phi[k] = 0.f;
            fc[k] = b.f;
            if (e < 0.03f * tot || nrm < 1e-12f) continue;
            const float r0 = b.c0 / nrm, rq = b.cq / nrm;
            const float m = std::sqrt(r0 * r0 + rq * rq);
            if (m < 0.4f) continue;                         // uncorrelated lanes in this band: the phase does not matter
            phi[k] = std::atan2(-rq, r0);
            w[k] = e * m;
            wsum += w[k];
        }
        if (wsum <= 0.f) return r;
        for (int k = 0; k < kBands; ++k) w[k] /= wsum;
        auto D = [&](float f, float f0, int n) {           // phase delay (rad) of n all-pass stages with frequency f0, at frequency f
            if (n <= 0) return 0.f;
            const float tf = std::tan(float(M_PI) * f / fs_), t0 = std::tan(float(M_PI) * std::max(20.f, std::min(0.45f * fs_, f0)) / fs_);
            return float(n) * 2.f * std::atan(tf / t0);
        };
        float pre[kBands];
        float cur = 0.f;
        for (int k = 0; k < kBands; ++k) {
            pre[k] = phi[k] + D(fc[k], fA, sA) - D(fc[k], fB, sB);      // phase without the current rotation
            cur += w[k] * std::cos(phi[k]);
        }
        // best coherent sum per number of stages (0 = no rotation)
        float sN[5], fN[5];
        int lN[5];
        sN[0] = 0.f;
        for (int k = 0; k < kBands; ++k) sN[0] += w[k] * std::cos(pre[k]);
        lN[0] = 1;
        fN[0] = 300.f;
        for (int n = 1; n <= 4; ++n) {
            sN[n] = -2.f;
            lN[n] = 1;
            fN[n] = 300.f;
            for (int lane = onlyB ? 1 : 0; lane < 2; ++lane)
                for (int i = 0; i < 24; ++i) {
                    const float f0 = 100.f * std::pow(40.f, float(i) / 23.f);   // 100 .. 4000 Hz
                    float s = 0.f;
                    for (int k = 0; k < kBands; ++k) {
                        const float d = D(fc[k], f0, n);
                        s += w[k] * std::cos(lane == 0 ? pre[k] - d : pre[k] + d);   // on A: A lags more; on B: B lags more
                    }
                    if (s > sN[n]) { sN[n] = s; lN[n] = lane; fN[n] = f0; }
                }
        }
        float top = sN[0];
        for (int n = 1; n <= 4; ++n) top = std::max(top, sN[n]);
        // the solution with FEWER stages that achieves at least 80% of the maximum improvement is chosen
        const float target = cur + 0.8f * (top - cur);
        int pick = 0;
        for (int n = 0; n <= 4; ++n)
            if (sN[n] >= target - 1e-6f) { pick = n; break; }
        r.cur = cur;
        r.best = sN[pick];
        r.gain = sN[pick] - cur;
        // high threshold: the per-band phase of two different models is not very sharp, better not to suggest rotations for small
        // improvements (and even more so if a rotation is already set, to avoid it oscillating between nearby values)
        r.needed = r.gain >= ((sA > 0 || sB > 0) ? 0.35f : 0.25f);
        r.lane = lN[pick];
        r.stages = pick;
        r.freq = fN[pick];
        return r;
    }

    // ---- Stepwise calculation of the recommended rotation (on path B only): same results as suggestRotation(..., onlyB = true), but the work is
    // split into 9 steps, one per rotStep call. In the stereo version the calculation runs in the audio callback, which has 83 microseconds per block:
    // all at once costs up to about 150 microseconds on the board and causes a 190% CPU peak. In addition the arctangents depend only on band
    // and frequency (not on the number of stages): they are computed once and the angle multiples are obtained with a rotation.
    void rotStart(int sA, float fA, int sB, float fB) {
        job_.active = true;
        job_.stage = 0;
        job_.sA = sA; job_.fA = fA; job_.sB = sB; job_.fB = fB;
    }
    bool rotBusy() const { return job_.active; }
    bool rotStep(Rot& out) {
        RotJob& j = job_;
        if (!j.active) return false;
        const float pi = float(M_PI);
        if (j.stage == 0) {                                 // snapshot of the bands: from here on the work no longer depends on the incoming samples
            j.empty = false;
            if (!bands_) j.empty = true;
            float tot = 1e-12f, wsum = 0.f;
            for (const auto& b : bd_) tot += b.ell + b.err;
            for (int k = 0; k < kBands && !j.empty; ++k) {
                const Band& b = bd_[k];
                const float e = b.ell + b.err, nrm = std::sqrt(b.ell * b.err);
                j.w[k] = 0.f;
                j.phi[k] = 0.f;
                j.fc[k] = b.f;
                if (e < 0.03f * tot || nrm < 1e-12f) continue;
                const float r0 = b.c0 / nrm, rq = b.cq / nrm;
                const float m = std::sqrt(r0 * r0 + rq * rq);
                if (m < 0.4f) continue;
                j.phi[k] = std::atan2(-rq, r0);
                j.w[k] = e * m;
                wsum += j.w[k];
            }
            if (wsum <= 0.f) j.empty = true;
            if (j.empty) {                                   // nothing measurable: no rotation
                out = Rot();
                j.active = false;
                return true;
            }
            for (int k = 0; k < kBands; ++k) j.w[k] /= wsum;
            auto D = [&](float f, float f0, int n) {
                if (n <= 0) return 0.f;
                const float tf = std::tan(pi * f / fs_), t0 = std::tan(pi * std::max(20.f, std::min(0.45f * fs_, f0)) / fs_);
                return float(n) * 2.f * std::atan(tf / t0);
            };
            j.cur = 0.f;
            j.sN[0] = 0.f;
            for (int k = 0; k < kBands; ++k) {
                const float pre = j.phi[k] + D(j.fc[k], j.fA, j.sA) - D(j.fc[k], j.fB, j.sB);   // phase without the current rotation
                j.cp[k] = std::cos(pre);
                j.sp[k] = std::sin(pre);
                j.cur += j.w[k] * std::cos(j.phi[k]);
                j.sN[0] += j.w[k] * j.cp[k];
                j.tf[k] = std::tan(pi * j.fc[k] / fs_);
            }
            for (int i = 0; i < 24; ++i) {
                j.f0[i] = 100.f * std::pow(40.f, float(i) / 23.f);       // 100 .. 4000 Hz
                j.t0[i] = std::tan(pi * j.f0[i] / fs_);
            }
            j.fN[0] = 300.f;
            j.stage = 1;
            return false;
        }
        if (j.stage >= 1 && j.stage <= 3) {                 // angles of a single stage for eight frequencies at a time (bands x frequencies)
            const int i0 = (j.stage - 1) * 8;
            for (int i = i0; i < i0 + 8; ++i)
                for (int k = 0; k < kBands; ++k) {
                    const float d = 2.f * std::atan(j.tf[k] / j.t0[i]);
                    j.c1[k][i] = std::cos(d);
                    j.s1[k][i] = std::sin(d);
                    j.cn[k][i] = j.c1[k][i];                 // n = 1
                    j.sn[k][i] = j.s1[k][i];
                }
            ++j.stage;
            return false;
        }
        if (j.stage >= 4 && j.stage <= 7) {                 // score for n = 1..4 stages: weighted coherent sum cos(phase + n d), over all the frequencies
            const int n = j.stage - 3;
            j.sN[n] = -2.f;
            j.fN[n] = 300.f;
            for (int i = 0; i < 24; ++i) {
                float s = 0.f;
                for (int k = 0; k < kBands; ++k) s += j.w[k] * (j.cp[k] * j.cn[k][i] - j.sp[k] * j.sn[k][i]);
                if (s > j.sN[n]) { j.sN[n] = s; j.fN[n] = j.f0[i]; }
            }
            for (int i = 0; i < 24; ++i)                      // from n to n + 1: rotation of the angle
                for (int k = 0; k < kBands; ++k) {
                    const float c = j.cn[k][i] * j.c1[k][i] - j.sn[k][i] * j.s1[k][i];
                    const float s = j.sn[k][i] * j.c1[k][i] + j.cn[k][i] * j.s1[k][i];
                    j.cn[k][i] = c;
                    j.sn[k][i] = s;
                }
            ++j.stage;
            return false;
        }
        // step 8: choice (fewest stages that achieve at least 80% of the maximum improvement)
        Rot r;
        float top = j.sN[0];
        for (int n = 1; n <= 4; ++n) top = std::max(top, j.sN[n]);
        const float target = j.cur + 0.8f * (top - j.cur);
        int pick = 0;
        for (int n = 0; n <= 4; ++n)
            if (j.sN[n] >= target - 1e-6f) { pick = n; break; }
        r.cur = j.cur;
        r.best = j.sN[pick];
        r.gain = j.sN[pick] - j.cur;
        r.needed = r.gain >= ((j.sA > 0 || j.sB > 0) ? 0.35f : 0.25f);
        r.lane = 1;
        r.stages = pick;
        r.freq = j.fN[pick];
        out = r;
        j.active = false;
        return true;
    }

private:
    struct RotJob {
        bool active = false, empty = false;
        int stage = 0, sA = 0, sB = 0;
        float fA = 300.f, fB = 300.f, cur = 0.f;
        float phi[6], w[6], fc[6], cp[6], sp[6], tf[6], t0[24], f0[24];
        float c1[6][24], s1[6][24], cn[6][24], sn[6][24];
        float sN[5], fN[5];
    };
    RotJob job_;
    static const int kBands = 6;
    struct Band {
        float b0 = 0.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, f = 0.f;
        float l1 = 0.f, l2 = 0.f, r1 = 0.f, r2 = 0.f;
        float dly[128] = {};
        int N = 1;
        float c0 = 0.f, cq = 0.f, ell = 0.f, err = 0.f;
    };
    Band bd_[kBands];
    int dpos_ = 0;
    bool bands_ = false, bandsOnly_ = false;
    float lamS_ = 0.99999f;
    float fs_ = 48000.f, lam_ = 0.99999f;
    float hL_[512], hR_[512];
    uint32_t hp_ = 0;
    float C_[kLags];
    float eL_ = 1e-12f, eR_ = 1e-12f, pxL_ = 0.f, pxR_ = 0.f, sDA_ = 0.f, sDB_ = 0.f, sPA_ = 0.f, sPB_ = 0.f;
    int parity_ = 0, silent_ = 1 << 30;
    float pres_ = 0.f;
    float verdict_ = 0.f, loss_ = 0.f, lag_ = 0.f, conf_ = 0.f, corrIn_ = 0.f;
    bool neg_ = false, valid_ = false;
};

class Phase {
public:
    void init(double rate, int nch) {
        fs_ = float(rate);
        nch_ = nch;
        est_.init(fs_);
        estOut_.init(fs_);
        estOut_.enableBands(true);
        estOut_.setBandsOnly(true);
        clear();
    }
    // activation: in the mono version with Lane A/B role it registers on the shared bus
    void activate(int role) {
        clear();
        setRole(role);
    }
    // the role can also be changed while the audio is running: it (re)registers on the bus
    void setRole(int role) {
        role_ = (nch_ == 1 && role > 0) ? role : 0;
        if (role_) {
            Bus& b = bus();
            const int l = role_ - 1;
            b.flagG[l] = 0;
            b.end[l] = b.G.load();
            b.d[l] = 0.f;
            b.inv[l] = 0;
            est_.clear();
            est_.enableBands(role_ == 2);      // the per-band analysis is done by the Lane B instance (measures after the correction)
            ana_ = false;
        }
    }
    void clear() {
        for (int c = 0; c < 2; ++c) {
            std::memset(buf_[c], 0, sizeof(buf_[c]));
            for (auto& s : ap_[c]) s = AllPass1();
        }
        w_ = 0;
        est_.clear();
        estOut_.clear();
        rotSugStages_ = 0;
        rotSugFreq_ = 0.f;
        rotNeeded_ = false;
        rotWant_ = false;
        rotCount_ = 0;
        lastBeatO_ = 0;
        idleO_ = 1 << 30;
        probePrevO_ = false;
        dEff_ = 0.f;
        polG_ = 1.f;
        aCoef_ = 0.f;
        autoDelay_ = 0.f;
        autoPol_ = false;
        zLR_ = zLL_ = zRR_ = 1e-12f;
        corrOut_ = sugMs_ = sugPol_ = 0.f;
        first_ = true;
        ana_ = false;
        probePrev_ = false;
        haveSig_ = false;
        lastBeat_ = 0;
        idleFrames_ = 1 << 30;
        pos_ = 0;
    }

    void process(const float* const* in, float* const* out, uint32_t n, const Params& q) {
        const float trim = std::pow(10.f, q.trimDb / 20.f);
        const float lamZ = std::exp(-1.f / (0.3f * fs_));
        const float polTarget = ((q.autoAlign ? autoPol_ : false) != q.invert) ? -1.f : 1.f;
        float dTarget = q.autoAlign ? autoDelay_ : q.delayMs * 0.001f * fs_;
        dTarget = std::max(-float(kK), std::min(float(kK), dTarget));
        const float f0 = std::max(20.f, std::min(0.45f * fs_, q.apFreq));
        const float tn = std::tan(float(M_PI) * f0 / fs_);
        const float aT = (tn - 1.f) / (tn + 1.f);
        if (first_) { aCoef_ = aT; dEff_ = dTarget; polG_ = polTarget; first_ = false; }
        const float slew = 0.002f;     // delay samples per sample: smooth slide (no jumps)
        const float polSlew = 1.f / (0.005f * fs_);

        if (nch_ == 1 && q.role != role_) setRole(q.role);

        // bus: start of the cycle (global position) for the version with Lane role
        Bus* bp = nullptr;
        uint64_t start = 0;
        int lane = 0;
        if (nch_ == 1 && role_ > 0) {
            bp = &bus();
            lane = role_ - 1;
            const uint64_t g = bp->G.load(std::memory_order_acquire);
            if (bp->flagG[1 - lane].load(std::memory_order_acquire) == g && g != 0) {
                start = g - n;                                    // the other lane has already opened this cycle: I lock on
                bp->flagG[1 - lane].store(0, std::memory_order_release);
            } else {
                start = g;                                        // I open the cycle
                bp->G.store(g + n, std::memory_order_release);
                bp->flagG[lane].store(g + n, std::memory_order_release);
            }
        }

        // stereo version: per-band phase analysis on the ALREADY corrected outputs (delay, polarity, rotation in force);
        // it is fed ONLY while the sweep plays: without a probe in the chain nothing is done (the suggestion calculation costs up to
        // about 50 microseconds in one call and the callback has 83 per block)
        bool feedOut = false, endedOut = false;
        if (nch_ > 1) {
            adelprobe_shm::Shared* sh = adelprobe_shm::get();
            const uint32_t beat = sh->beat.load(std::memory_order_relaxed);
            if (beat != lastBeatO_) { lastBeatO_ = beat; idleO_ = 0; }
            else if (idleO_ < (1 << 30)) idleO_ += int(n);
            const bool alive = idleO_ < int(0.5f * fs_);
            const bool playing = alive && sh->playing.load(std::memory_order_relaxed) != 0;
            if (playing && !probePrevO_) estOut_.reset();      // new sweep: new measurement
            endedOut = probePrevO_ && !playing;                // end of the sweep: last calculation
            probePrevO_ = playing;
            feedOut = playing;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const float xl = in[0][i];
            const float xr = nch_ > 1 ? in[1][i] : in[0][i];
            if (nch_ > 1) est_.feed(xl, xr);       // stereo: measurement on the input (before the correction)
            // correction
            polG_ += std::max(-polSlew, std::min(polSlew, polTarget - polG_));
            aCoef_ += 0.001f * (aT - aCoef_);
            dEff_ += std::max(-slew, std::min(slew, dTarget - dEff_));
            float r = xr * trim * polG_;
            for (int s = 0; s < q.apStages && s < 4; ++s) r = ap_[1][s].process(r, aCoef_);
            float l = xl;
            // the mono version delays the only path; the stereo one delays R (d>0) or L (d<0)
            const float dR = std::max(0.f, dEff_);
            const float dL = nch_ > 1 ? std::max(0.f, -dEff_) : 0.f;
            if (nch_ > 1) {
                buf_[0][w_ & kMask] = l;
                buf_[1][w_ & kMask] = r;
            } else {
                buf_[1][w_ & kMask] = r;
            }
            ++w_;
            float oR = dspkit::readLagrange8(buf_[1], kMask, w_, float(kBase + 1) + dR);
            float oL = 0.f;
            if (nch_ > 1) oL = dspkit::readLagrange8(buf_[0], kMask, w_, float(kBase + 1) + dL);
            if (nch_ == 1) {
                out[0][i] = oR;
                if (bp) bp->ring[lane][(start + i) & kBusMask] = oR;
                continue;
            }
            // output correlation (corrected paths)
            zLR_ = lamZ * zLR_ + oL * oR;
            zLL_ = lamZ * zLL_ + oL * oL;
            zRR_ = lamZ * zRR_ + oR * oR;
            if (feedOut) estOut_.feed(oL, oR);
            if (q.mode == 0) {
                out[0][i] = oL;
                out[1][i] = oR;
            } else if (q.mode == 1) {
                const float s = 0.5f * (oL + oR);
                out[0][i] = s;
                out[1][i] = s;
            } else {
                out[0][i] = 0.5f * (oL + oR);
                out[1][i] = 0.f;
            }
        }

        if (nch_ > 1) {
            est_.update();
            if (est_.valid()) {
                autoDelay_ += 0.05f * (-est_.lag() - autoDelay_);      // the automatic alignment follows the measurement with a slow average
                autoPol_ = est_.negative();
                const bool ok = est_.verdict() == 1.f || est_.verdict() == 6.f;      // nothing to correct: no delay suggested
                sugMs_ = ok ? 0.f : -est_.lag() / fs_ * 1000.f;
                sugPol_ = (!ok && est_.negative()) ? 1.f : 0.f;
            }
            // rotation suggestion: starts at most once per second while the sweep plays and once at the end; the calculation is split into
            // 9 steps, one per call (a step costs a few microseconds instead of the roughly 150 of the whole calculation together)
            rotCount_ += n;
            if ((feedOut && rotCount_ >= uint32_t(1.0f * fs_)) || endedOut) rotWant_ = true;
            if (rotWant_ && !estOut_.rotBusy()) {
                rotWant_ = false;
                rotCount_ = 0;
                estOut_.rotStart(0, 300.f, q.apStages, q.apFreq);
            }
            if (estOut_.rotBusy()) {
                Estimator::Rot rot;
                if (estOut_.rotStep(rot)) {
                    rotNeeded_ = rot.needed;
                    rotSugStages_ = rot.needed ? rot.stages : q.apStages;
                    rotSugFreq_ = rot.needed ? rot.freq : (q.apStages > 0 ? std::max(20.f, std::min(4000.f, q.apFreq)) : 0.f);
                }
            }
            const float zn = std::sqrt(zLL_ * zRR_);
            corrOut_ = zn > 1e-9f ? std::max(-1.f, std::min(1.f, zLR_ / zn)) * 100.f : 0.f;
        } else if (bp) {
            laneUpdate(*bp, lane, start + n, n, q);
        }
    }

    float corrInPct() const { return nch_ > 1 ? est_.corrIn() : laneCorr_; }
    float corrOutPct() const { return nch_ > 1 ? corrOut_ : laneCorr_; }
    float lagSamples() const { return nch_ > 1 ? est_.lag() : laneLag_; }
    float suggestDelayMs() const { return nch_ > 1 ? sugMs_ : laneSugMs_; }
    float suggestPolarity() const { return nch_ > 1 ? sugPol_ : laneSugInv_; }
    float confidencePct() const { return nch_ > 1 ? est_.confidence() : laneConf_; }
    // stereo: verdict 7 (phase rotation) when delay and polarity are fine but the per-band analysis on the corrected outputs
    // shows that an all-pass rotation on the R path clearly improves the sum
    float verdict() const { return nch_ > 1 ? ((est_.verdict() == 1.f && rotNeeded_ && rotSugStages_ > 0) ? 7.f : est_.verdict()) : laneVerdict_; }
    float lossDb() const { return nch_ > 1 ? est_.loss() : laneLoss_; }
    float rotationScoreNow() const { return rotCur_; }       // diagnostics: current coherent sum and with the best rotation
    float rotationScoreBest() const { return rotBest_; }
    float suggestStages() const { return nch_ > 1 ? float(rotSugStages_) : laneSugStages_; }
    float suggestFreq() const { return nch_ > 1 ? rotSugFreq_ : laneSugFreq_; }
    float latencySamples() const { return float(kBase); }

private:
    // settings of the two lanes at the time of the measurement
    struct Sig {
        float delay[2] = {0.f, 0.f}, trim[2] = {0.f, 0.f}, apF[2] = {300.f, 300.f};
        int inv[2] = {0, 0}, apS[2] = {0, 0};
    };
    void captureSig(const Bus& b) {
        for (int l = 0; l < 2; ++l) {
            sig_.delay[l] = b.ctlDelay[l].load(std::memory_order_relaxed);
            sig_.trim[l] = b.ctlTrim[l].load(std::memory_order_relaxed);
            sig_.inv[l] = b.inv[l].load(std::memory_order_relaxed);
            sig_.apS[l] = b.apS[l].load(std::memory_order_relaxed);
            sig_.apF[l] = b.apF[l].load(std::memory_order_relaxed);
        }
        haveSig_ = true;
    }
    bool sigChanged(const Bus& b) const {
        for (int l = 0; l < 2; ++l) {
            if (std::fabs(sig_.delay[l] - b.ctlDelay[l].load(std::memory_order_relaxed)) > 0.004f) return true;
            if (std::fabs(sig_.trim[l] - b.ctlTrim[l].load(std::memory_order_relaxed)) > 0.04f) return true;
            if (sig_.inv[l] != b.inv[l].load(std::memory_order_relaxed)) return true;
            const int s = b.apS[l].load(std::memory_order_relaxed);
            if (sig_.apS[l] != s) return true;
            // the frequency only matters if the rotation is active
            if (s > 0 && std::fabs(sig_.apF[l] - b.apF[l].load(std::memory_order_relaxed)) > 0.005f * sig_.apF[l]) return true;
        }
        return false;
    }

    // Version with Lane role: publishes its own state and, in Lane B, analyzes the two lanes
    void laneUpdate(Bus& b, int lane, uint64_t endPos, uint32_t n, const Params& q) {
        b.end[lane].store(endPos, std::memory_order_release);
        b.d[lane].store(std::max(0.f, dEff_), std::memory_order_relaxed);
        b.inv[lane].store(q.invert ? 1 : 0, std::memory_order_relaxed);
        b.apS[lane].store(q.apStages, std::memory_order_relaxed);
        b.apF[lane].store(q.apFreq, std::memory_order_relaxed);
        b.ctlDelay[lane].store(q.delayMs, std::memory_order_relaxed);
        b.ctlTrim[lane].store(q.trimDb, std::memory_order_relaxed);
        if (role_ == 2) {
            const uint64_t P = std::min(b.end[0].load(std::memory_order_acquire), b.end[1].load(std::memory_order_acquire));
            if (!ana_ || P < pos_) { pos_ = P; ana_ = true; }
            if (P - pos_ > 8192) pos_ = P - 8192;
            // the two lanes are measured AFTER the correction: the correction in force during the measurement is recorded
            // to work back to the absolute value to set, which must not change if you move the controls after the measurement
            const float dA = b.d[0].load(std::memory_order_relaxed), dB = b.d[1].load(std::memory_order_relaxed);
            const bool iA = b.inv[0].load(std::memory_order_relaxed) != 0, iB = b.inv[1].load(std::memory_order_relaxed) != 0;
            // if there is an ADELSendSweep in the chain it measures only while the sweep plays (then the result stays fixed until
            // the next sweep); without ADELSendSweep it measures on the incoming signal
            adelprobe_shm::Shared* sh = adelprobe_shm::get();
            const uint32_t beat = sh->beat.load(std::memory_order_relaxed);
            if (beat != lastBeat_) { lastBeat_ = beat; idleFrames_ = 0; }
            else if (idleFrames_ < (1 << 30)) idleFrames_ += int(n);
            const bool probeAlive = idleFrames_ < int(0.5f * fs_);
            const bool probePlaying = probeAlive && sh->playing.load(std::memory_order_relaxed) != 0;
            if (probePlaying && !probePrev_) {
                est_.reset();
                for (int l = 0; l < 2; ++l) {          // phase rotation in force at the start of the measurement
                    measApS_[l] = b.apS[l].load(std::memory_order_relaxed);
                    measApF_[l] = b.apF[l].load(std::memory_order_relaxed);
                }
                captureSig(b);                          // settings in force: if they change after the measurement, the Verdict warns
            }
            probePrev_ = probePlaying;
            if (!probeAlive || probePlaying)
                for (uint64_t k = pos_; k < P; ++k) est_.feed(b.ring[0][k & kBusMask], b.ring[1][k & kBusMask], dA, dB, iA ? 1.f : 0.f, iB ? 1.f : 0.f);
            pos_ = P;
            est_.update();
            // Verdict OK but one band stays out of phase: the Estimator::suggestRotation simulation says whether a phase rotation
            // clearly improves the sum (and which) -> Verdict "Phase Rot"
            float vOut = est_.verdict();
            Estimator::Rot rot;
            if (vOut == 1.f) {
                rot = est_.suggestRotation(measApS_[0], measApF_[0], measApS_[1], measApF_[1]);
                if (rot.needed) vOut = 7.f;
            }
            rotCur_ = rot.cur;
            rotBest_ = rot.best;
            if (est_.valid()) {
                const float maxMs = 2.f;
                const float v = vOut;
                // Clear correction to have (B's delay minus A's delay, in samples, and relative inversion):
                //  - Verdict OK / uncorrelated lanes: the equivalent of the settings in force during the measurement
                //    (so that a marginal correlation peak does not suggest useless corrections)
                //  - otherwise: the absolute value that aligns the lanes (measured AFTER the correction, worked back to before)
                float net;
                bool inv;
                if (v == 1.f || v == 6.f || v == 7.f) {       // 7 = delay/polarity fine, only the phase rotation is missing
                    net = est_.meanDelayDiff();
                    inv = est_.meanPolDiff();
                } else {
                    net = -(est_.lag() - est_.meanDelayDiff());     // B's delay relative to A without corrections = lagPre
                    inv = est_.negative() != est_.meanPolDiff();
                }
                // everything on ONE lane: the one that receives the delay (net > 0 delays B, net < 0 delays A); with no delay, B.
                // This way the other ADELphase can always be removed.
                const bool delayNeeded = std::fabs(net) >= 1.f;       // below 1 sample (0.02 ms) it is negligible
                const int target = (delayNeeded && net < 0.f) ? 0 : 1;
                for (int l = 0; l < 2; ++l) {
                    const bool on = l == target;
                    b.sugMs[l].store(on && delayNeeded ? std::min(maxMs, std::fabs(net) / fs_ * 1000.f) : 0.f, std::memory_order_relaxed);
                    b.sugInv[l].store(on && inv ? 1.f : 0.f, std::memory_order_relaxed);
                }
                // phase rotation: with Verdict "Phase Rot" the absolute value (stages, frequency) goes on ONE lane and the other goes
                // to Off; in the other cases the settings in force during the measurement are shown
                for (int l = 0; l < 2; ++l) {
                    float st = float(measApS_[l]), fr = measApF_[l];
                    if (v == 7.f) {
                        const bool on = l == rot.lane && rot.stages > 0;
                        st = on ? float(rot.stages) : 0.f;
                        fr = on ? rot.freq : 300.f;
                    }
                    b.sugStages[l].store(st, std::memory_order_relaxed);
                    b.sugFreq[l].store(fr, std::memory_order_relaxed);
                }
                b.lag.store(est_.lag(), std::memory_order_relaxed);
            }
            // if something was changed after the measurement (delay, inversion, trim, phase rotation) the outcome is no longer valid
            const float vPub = (haveSig_ && sigChanged(b)) ? 8.f : vOut;
            b.verdict.store(vPub, std::memory_order_relaxed);
            b.loss.store(est_.loss(), std::memory_order_relaxed);
            b.conf.store(est_.confidence(), std::memory_order_relaxed);
            b.corrIn.store(est_.corrIn(), std::memory_order_relaxed);
        }
        laneVerdict_ = b.verdict.load(std::memory_order_relaxed);
        laneLoss_ = b.loss.load(std::memory_order_relaxed);
        laneConf_ = b.conf.load(std::memory_order_relaxed);
        laneCorr_ = b.corrIn.load(std::memory_order_relaxed);
        laneLag_ = b.lag.load(std::memory_order_relaxed);
        laneSugMs_ = b.sugMs[lane].load(std::memory_order_relaxed);
        laneSugInv_ = b.sugInv[lane].load(std::memory_order_relaxed);
        laneSugStages_ = b.sugStages[lane].load(std::memory_order_relaxed);
        laneSugFreq_ = b.sugFreq[lane].load(std::memory_order_relaxed);
    }

    float fs_ = 48000.f;
    int nch_ = 2, role_ = 0;
    float buf_[2][4096];
    uint32_t w_ = 0;
    AllPass1 ap_[2][4];
    Estimator est_, estOut_;
    int rotSugStages_ = 0;
    float rotSugFreq_ = 0.f;
    bool rotNeeded_ = false, probePrevO_ = false, rotWant_ = false;
    uint32_t rotCount_ = 0, lastBeatO_ = 0;
    int idleO_ = 1 << 30;
    float dEff_ = 0.f, polG_ = 1.f, aCoef_ = 0.f, autoDelay_ = 0.f;
    bool autoPol_ = false, first_ = true;
    float zLR_ = 1e-12f, zLL_ = 1e-12f, zRR_ = 1e-12f;
    float corrOut_ = 0.f, sugMs_ = 0.f, sugPol_ = 0.f;
    bool ana_ = false, probePrev_ = false;
    uint64_t pos_ = 0;
    uint32_t lastBeat_ = 0;
    int idleFrames_ = 1 << 30;
    float laneVerdict_ = 0.f, laneLoss_ = 0.f, laneConf_ = 0.f, laneCorr_ = 0.f, laneLag_ = 0.f, laneSugMs_ = 0.f, laneSugInv_ = 0.f;
    float laneSugStages_ = 0.f, laneSugFreq_ = 0.f, rotCur_ = 0.f, rotBest_ = 0.f;
    Sig sig_;
    bool haveSig_ = false;
    int measApS_[2] = {0, 0};
    float measApF_[2] = {300.f, 300.f};
};

}  // namespace adelphase
