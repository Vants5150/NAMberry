// ADELshifter: 3-voice harmonizer for monophonic guitar.
//  - pitch detection with YIN (de Cheveigne & Kawahara 2002) on a decimated signal, with confirmation and memory
//    (2026-10: attack detector that limits the window to the new note, continuity of the sounding note, more agreeing hops for octave leaps)
//  - time-domain pitch shifting with splices synchronized to the period (PSOLA/"splicing" style):
//    the read point runs at rate rho and, when the delay leaves the interval, jumps by a whole number
//    of periods with a short fade. This way the waveform stays coherent and the latency is 1-3 periods.
//  - diatonic harmonies: the interval follows the chosen scale (thirds, fifths, etc.), with hysteresis on the note
//  - Whammy, Drift, delay and pan per voice.
// Public techniques, no third-party code.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dspkit.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace adelshifter {

struct VoiceParams {
    bool on = false;
    int mode = 0;         // 0 Diatonic, 1 Chromatic
    int steps = 2;
    float semi = 0.f;
    float fineCents = 0.f;
    float levelDb = -3.f;
    float pan = 0.f;      // -1..1
    float delayMs = 0.f;
};

struct Params {
    float dryDb = 0.f, levelDb = 0.f;
    int key = 0, scale = 0;
    float glideMs = 20.f;
    int tracking = 1;     // 0 Fast, 1 Normal, 2 Smooth
    float gateDb = -55.f;
    float lowHz = 70.f;
    VoiceParams v[3];
    float whammy = 0.f;
    bool whammyDry = false;
    float driftCents = 6.f;
};

// ------------------------------------------------------------------------------------------
// Pitch detection (YIN)
// ------------------------------------------------------------------------------------------
class Yin {
public:
    static constexpr int HOP = 64;      // decimated samples (5.33 ms at 48 kHz)
    static constexpr int W = 384;       // analysis window (decimated)
    static constexpr int MAXTAU = 340;

    void init(double fs) {
        fs_ = fs;
        reset();
    }

    void reset() {
        for (int i = 0; i < 2048; ++i) ring_[i] = 0.f;
        nd_ = 0;
        hop_ = 0;
        busy_ = false;
        a1_ = a2_ = b1_ = b2_ = 0.f;
        ph1_ = ph2_ = 0;
        voiced_ = false;
        hz_ = 0.f;
        hasPending_ = false;
        pendingHz_ = 0.f;
        unvoicedHops_ = 0;
        confirm_ = 0;
        rms_ = 0.f;
        conf_ = 0.f;
        hopsSinceOnset_ = 1000;
        onsetNd_ = 0;
        ageD_ = 100000;
        wc_ = W;
    }

    // called at every sample; returns true when an analysis has been done (every 256 samples)
    bool push(float x, float lowHz, float gateLin, int tracking) {
        bool done = false;
        if (busy_) done = slice();
        float y = 0.25f * (x + 2.f * a1_ + a2_);
        a2_ = a1_;
        a1_ = x;
        if ((ph1_++ & 1) != 0) return done;
        float z = 0.25f * (y + 2.f * b1_ + b2_);
        b2_ = b1_;
        b1_ = y;
        if ((ph2_++ & 1) != 0) return done;
        ring_[nd_ & 2047] = z;
        ++nd_;
        if (++hop_ < HOP) return done;
        hop_ = 0;
        if (analyse(lowHz, gateLin, tracking)) return true;
        return done;
    }

    bool voiced() const { return voiced_; }
    float hz() const { return hz_; }
    float rms() const { return rms_; }
    float confidence() const { return conf_; }

private:
    // start of the analysis: true if it is already finished (signal below the gate), false if the calculation continues in slices
    bool analyse(float lowHz, float gateLin, int tracking) {
        const float fsd = float(fs_ / 4.0);
        const int tauMax = std::min(MAXTAU, int(fsd / std::max(30.f, lowHz)) + 1);
        const int tauMin = std::max(6, int(fsd / 1500.f));
        // signal energy (last 256 decimated samples)
        float e = 0.f;
        for (int j = 0; j < 256; ++j) {
            float v = ring_[(nd_ - 1 - j) & 2047];
            e += v * v;
        }
        rms_ = std::sqrt(e / 256.f);
        // attack detector: energy of the last hop against the one before it (a picked note rises by several dB in a few ms)
        float eL = 0.f, eP = 0.f, hL = 0.f, hP = 0.f;
        for (int j = 0; j < HOP; ++j) {
            float a = ring_[(nd_ - 1 - j) & 2047], b = ring_[(nd_ - 1 - HOP - j) & 2047];
            float a1 = ring_[(nd_ - 2 - j) & 2047], b1 = ring_[(nd_ - 2 - HOP - j) & 2047];
            eL += a * a;
            eP += b * b;
            hL += (a - a1) * (a - a1);      // high-frequency part: the pick noise of a new note
            hP += (b - b1) * (b - b1);
        }
        if (hopsSinceOnset_ < 1000) ++hopsSinceOnset_;
        if (eL > gateLin * gateLin * float(HOP) * 2.25f && (eL > 2.5f * eP || hL > 3.5f * hP) && hopsSinceOnset_ >= 3) {
            hopsSinceOnset_ = 0;
            onsetNd_ = nd_ - uint32_t(HOP) - 6u;
        }
        ageD_ = hopsSinceOnset_ < 24 ? int(nd_ - onsetNd_) : 100000;
        const float thr = tracking == 0 ? 0.18f : (tracking == 1 ? 0.15f : 0.12f);
        const float alpha = tracking == 0 ? 0.6f : (tracking == 1 ? 0.4f : 0.25f);
        thr_ = thr; alpha_ = alpha; tracking_ = tracking; tauMax_ = tauMax; tauMin_ = tauMin; fsd_ = fsd; nds_ = nd_;
        // right after an attack only the samples of the new note may be used: window and lags are limited by its age
        wc_ = W;
        if (ageD_ < W + tauMax) {
            const int tm = std::min(tauMax, ageD_ / 2);
            if (tm < tauMin + 6) {
                // not enough of the new note yet: keep the previous state
                conf_ = 0.f;
                return true;
            }
            tauMax_ = tm;
            wc_ = std::min(W, ageD_ - tm);
        }
        conf_ = 0.f;
        if (rms_ >= gateLin && nd_ > uint32_t(W + MAXTAU + 8)) {
            busy_ = true;
            tau_ = 1;
            return false;
        }
        finish(false);
        return true;
    }

    // one slice of the difference function (12 lags); true when the analysis is finished
    bool slice() {
        const int tauMax = tauMax_;
        for (int k = 0; k < 12 && tau_ <= tauMax; ++k, ++tau_) {
            float s = 0.f;
            for (int j = 0; j < wc_; ++j) {
                float a = ring_[(nds_ - 1 - j) & 2047], b = ring_[(nds_ - 1 - j - tau_) & 2047];
                float df = a - b;
                s += df * df;
            }
            d_[tau_] = s * (float(W) / float(wc_));
        }
        if (tau_ <= tauMax) return false;
        busy_ = false;
        finish(true);
        return true;
    }

    // refines a lag with a parabola on the normalised difference function
    float refine(const float* dn, int best, int tauMax) const {
        if (best <= 1 || best >= tauMax) return float(best);
        float s0 = dn[best - 1], s1 = dn[best], s2 = dn[best + 1];
        float den = 2.f * (s0 - 2.f * s1 + s2);
        float shift = std::fabs(den) > 1e-9f ? (s0 - s2) / den : 0.f;
        shift = std::max(-0.5f, std::min(0.5f, shift));
        return float(best) + shift;
    }

    void finish(bool computed) {
        const float fsd = fsd_, thr = thr_, alpha = alpha_;
        const int tauMax = tauMax_, tauMin = tauMin_;
        float* d = d_;
        float* dn = dn_;
        (void) d;
        bool found = false;
        float cand = 0.f;
        auto semis = [](float a, float b) { return 12.f * std::log2(a / b); };
        const bool recentOnset = hopsSinceOnset_ < 10;      // a new note started in the last 53 ms
        float heldM = 1.f;                                  // how well the period of the note that was sounding still fits
        if (computed) {
            dn[0] = 1.f;
            float run = 0.f;
            for (int tau = 1; tau <= tauMax; ++tau) {
                run += d[tau];
                dn[tau] = run > 1e-12f ? d[tau] * float(tau) / run : 1.f;
            }
            int best = -1;
            for (int tau = tauMin; tau < tauMax; ++tau) {
                if (dn[tau] < thr) {
                    while (tau + 1 < tauMax && dn[tau + 1] < dn[tau]) ++tau;
                    best = tau;
                    break;
                }
            }
            if (best < 0) {
                float m = 1e9f;
                for (int tau = tauMin; tau < tauMax; ++tau)
                    if (dn[tau] < m) { m = dn[tau]; best = tau; }
                if (m > 0.30f) best = -1;
            }
            if (best > 1 && best < tauMax) {
                const float tauF = refine(dn, best, tauMax);
                conf_ = 1.f - dn[best];
                // right after an attack only a few periods of the new note are available: ask for a clearer minimum
                const float need = ageD_ < W + tauMax ? 0.78f : 0.72f;
                if (conf_ >= need) {
                    cand = fsd / tauF;
                    found = true;
                }
            }
            // continuity: a note that is already sounding keeps its pitch while its own period still fits, so an octave
            // error (the half period looks almost as good) or a noisy hop does not move it; right after an attack this is disabled
            if (voiced_ && hz_ > 0.f && !recentOnset_) {
                const float tp = fsd / hz_;
                int lo = std::max(tauMin, int(tp * 0.96f)), hi = std::min(tauMax - 1, int(tp * 1.04f) + 1);
                int bi = -1;
                float m = 1e9f;
                for (int tau = lo; tau <= hi; ++tau)
                    if (dn[tau] < m) { m = dn[tau]; bi = tau; }
                heldM = m;
                const float contThr = std::max(0.28f, thr * 1.8f);
                if (bi > 1 && bi < tauMax - 1 && m < contThr && (!found || std::fabs(semis(cand, hz_)) >= 0.9f)) {
                    cand = fsd / refine(dn, bi, tauMax);
                    found = true;
                    conf_ = 1.f - m;
                }
            }
        }
        recentOnset_ = recentOnset;
        if (!found) {
            ++unvoicedHops_;
            hasPending_ = false;
            confirm_ = 0;
            if (unvoicedHops_ * 5.33f > 180.f) voiced_ = false;  // 180 ms memory
            return;
        }
        unvoicedHops_ = 0;
        if (!voiced_) {
            if (hasPending_ && std::fabs(semis(cand, pendingHz_)) < 0.8f) {
                voiced_ = true;
                hz_ = cand;
                hasPending_ = false;
            } else {
                pendingHz_ = cand;
                hasPending_ = true;
            }
            return;
        }
        float dfs = semis(cand, hz_);
        if (std::fabs(dfs) < 0.3f) {
            hz_ += alpha * (cand - hz_);
            hasPending_ = false;
            confirm_ = 0;
        } else {
            // jump: a new note needs agreeing candidates (fewer right after an attack); an octave jump of a note that is
            // sounding without a new attack needs many more, because it is usually a wrong choice between two harmonics
            // a leap of an octave or more without a new attack is almost always a wrong choice between harmonics
            const bool oct = std::fabs(dfs) > 11.0f;
            const int more = tracking_ == 0 ? 0 : (tracking_ == 1 ? 1 : 2);
            int need = recentOnset ? (oct ? 3 : 2) : (oct ? 8 + 2 * more : 3 + more);
            // between 0.3 and 0.9 semitones the candidate is usually a window that straddles two notes: it must also agree with itself
            if (std::fabs(dfs) < 0.9f && !oct) need = std::max(2, std::min(need, 3));
            // the old period no longer fits at all (legato change: hammer-on, pull-off, slide): follow quickly
            if (heldM > 0.45f) need = std::min(need, oct ? 4 : 2);
            if (hasPending_ && std::fabs(semis(cand, pendingHz_)) < 0.4f) {
                if (++confirm_ >= need - 1) {
                    hz_ = cand;
                    hasPending_ = false;
                    confirm_ = 0;
                }
            } else {
                pendingHz_ = cand;
                hasPending_ = true;
                confirm_ = 0;
            }
        }
    }

    double fs_ = 48000.0;
    float d_[MAXTAU + 2] = {}, dn_[MAXTAU + 2] = {};
    float ring_[2048];
    uint32_t nd_ = 0;
    int hop_ = 0;
    float a1_ = 0, a2_ = 0, b1_ = 0, b2_ = 0;
    uint32_t ph1_ = 0, ph2_ = 0;
    bool voiced_ = false, hasPending_ = false, recentOnset_ = false;
    float hz_ = 0, pendingHz_ = 0, rms_ = 0, conf_ = 0;
    int unvoicedHops_ = 0, confirm_ = 0;
    bool busy_ = false;
    int tau_ = 1, tauMax_ = 0, tauMin_ = 0, tracking_ = 1, wc_ = W, hopsSinceOnset_ = 1000, ageD_ = 100000;
    uint32_t nds_ = 0, onsetNd_ = 0;
    float thr_ = 0, alpha_ = 0, fsd_ = 0;
};

// ------------------------------------------------------------------------------------------
// Pitch shifting with splices synchronized to the period
// ------------------------------------------------------------------------------------------
struct Shifter {
    float d = 0.f, dOld = 0.f, a = 0.f, aInc = 0.f;
    bool fading = false, init = false;

    void reset() { init = false; fading = false; }

    // buf/mask/w: input buffer (w = number of samples written); rho = frequency ratio; P = period in samples
    float process(const float* buf, uint32_t mask, uint32_t w, float rho, float P) {
        P = std::max(40.f, std::min(1100.f, P));
        const float F = std::max(48.f, std::min(480.f, 0.5f * P));
        // minimum delay: during the fade (F samples) the "old" read approaches the present by F*(rho-1):
        // as long as it stays >= 8 it never reads into the future. The allowed interval is one period wide (jump of a single period).
        const float dMin = 8.f + F * std::max(1.f, rho - 1.f);
        const float dTarget = dMin + 0.5f * P;
        const float dMax = dMin + P;
        if (!init) { d = dTarget; init = true; fading = false; }
        const float drift = 1.f - rho;
        d += drift;
        if (fading) dOld += drift;
        if (!fading) {
            if (d < dMin) {
                float m = std::ceil((dTarget - d) / P);
                dOld = d;
                d += m * P;
                fading = true; a = 0.f; aInc = 1.f / F;
            } else if (d > dMax) {
                float m = std::ceil((d - dTarget) / P);
                dOld = d;
                d -= m * P;
                fading = true; a = 0.f; aInc = 1.f / F;
            }
        }
        if (d < 8.f || d > 7000.f) { d = dTarget; fading = false; }
        float out;
        if (fading) {
            float o1 = dspkit::readLagrange8(buf, mask, w, std::max(8.f, std::min(7000.f, dOld)));
            float o2 = dspkit::readLagrange8(buf, mask, w, d);
            out = (1.f - a) * o1 + a * o2;
            a += aInc;
            if (a >= 1.f) fading = false;
        } else {
            out = dspkit::readLagrange8(buf, mask, w, d);
        }
        return out;
    }
};

// ------------------------------------------------------------------------------------------
// Scale
// ------------------------------------------------------------------------------------------
struct ScaleDef { int n; int s[8]; };
static const ScaleDef kScales[11] = {
    {7, {0, 2, 4, 5, 7, 9, 11, 0}},   // Major
    {7, {0, 2, 3, 5, 7, 8, 10, 0}},   // Natural Minor
    {7, {0, 2, 3, 5, 7, 8, 11, 0}},   // Harmonic Minor
    {7, {0, 2, 3, 5, 7, 9, 11, 0}},   // Melodic Minor
    {7, {0, 2, 3, 5, 7, 9, 10, 0}},   // Dorian
    {7, {0, 1, 3, 5, 7, 8, 10, 0}},   // Phrygian
    {7, {0, 2, 4, 6, 7, 9, 11, 0}},   // Lydian
    {7, {0, 2, 4, 5, 7, 9, 10, 0}},   // Mixolydian
    {5, {0, 2, 4, 7, 9, 0, 0, 0}},    // Major Pentatonic
    {5, {0, 3, 5, 7, 10, 0, 0, 0}},   // Minor Pentatonic
    {6, {0, 3, 5, 6, 7, 10, 0, 0}}};  // Blues

// interval in semitones to apply to the "midi" note to obtain the note "steps" degrees above in the scale
inline int diatonicInterval(int midi, int steps, int key, int scale) {
    const ScaleDef& sc = kScales[std::max(0, std::min(10, scale))];
    int pc = ((midi - key) % 12 + 12) % 12;
    int idx = 0;
    for (int i = 0; i < sc.n; ++i)
        if (sc.s[i] <= pc) idx = i;
    int total = idx + steps;
    int oct = total >= 0 ? total / sc.n : -((-total + sc.n - 1) / sc.n);
    int deg = total - oct * sc.n;
    int targetRel = sc.s[deg] + 12 * oct;
    return targetRel - pc;
}

// ------------------------------------------------------------------------------------------
// Harmonizer
// ------------------------------------------------------------------------------------------
class Harmonizer {
public:
    static constexpr uint32_t BUF = 16384;
    static constexpr uint32_t DLY = 8192;

    void init(double fs) {
        fs_ = fs;
        buf_.assign(BUF, 0.f);
        for (int v = 0; v < 3; ++v) vd_[v].assign(DLY, 0.f);
        yin_.init(fs);
        clear();
    }

    void clear() {
        std::fill(buf_.begin(), buf_.end(), 0.f);
        w_ = 0;
        for (int v = 0; v < 3; ++v) {
            std::fill(vd_[v].begin(), vd_[v].end(), 0.f);
            vw_[v] = 0;
            sh_[v].reset();
            s_[v] = 0.f;
            en_[v] = 0.f;
            rw1_[v] = rw2_[v] = 0.f;
            iv_[v] = 0;
        }
        shMain_.reset();
        sW_ = 0.f;
        wd_ = 0.f;
        yin_.reset();
        P_ = 480.f;
        hasNote_ = false;
        nearest_ = 60;
        midiF_ = 60.f;
        rng_ = 0x2545F491u;
        first_ = true;
    }

    void process(const float* in, float* outL, float* outR, uint32_t n, const Params& p) {
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
        const float gateLin = std::pow(10.f, std::max(-80.f, std::min(-20.f, p.gateDb)) / 20.f);
        const float dryG = p.dryDb <= -59.f ? 0.f : std::pow(10.f, std::min(6.f, p.dryDb) / 20.f);
        const float lvG = std::pow(10.f, std::max(-24.f, std::min(12.f, p.levelDb)) / 20.f);
        const float glideC = 1.f - std::exp(-1.f / (std::max(1.f, p.glideMs) * 1e-3f * fs));
        const float whC = 1.f - std::exp(-1.f / (0.008f * fs));
        const float whammy = std::max(-24.f, std::min(24.f, p.whammy));
        float vg[3], vL[3], vR[3], vDly[3], vTgtFixed[3];
        bool vOn[3];
        for (int v = 0; v < 3; ++v) {
            const VoiceParams& q = p.v[v];
            vOn[v] = q.on;
            vg[v] = std::pow(10.f, std::max(-36.f, std::min(6.f, q.levelDb)) / 20.f);
            float ang = (std::max(-1.f, std::min(1.f, q.pan)) + 1.f) * 0.5f * 1.5707963f;
            vL[v] = std::cos(ang);
            vR[v] = std::sin(ang);
            vDly[v] = std::max(0.f, std::min(60.f, q.delayMs)) * 1e-3f * fs;
            vTgtFixed[v] = q.fineCents / 100.f;
        }
        // if key/scale/steps change: recompute the intervals
        if (first_ || p.key != lastKey_ || p.scale != lastScale_) {
            for (int v = 0; v < 3; ++v) lastSteps_[v] = 1000;
            lastKey_ = p.key;
            lastScale_ = p.scale;
            first_ = false;
        }
        const float driftA = 6.2831853f * 0.4f / fs;

        for (uint32_t t = 0; t < n; ++t) {
            const float x = in[t];
            buf_[w_ & (BUF - 1)] = x;
            ++w_;
            if (yin_.push(x, p.lowHz, gateLin, p.tracking)) {
                if (yin_.voiced()) {
                    float hz = yin_.hz();
                    P_ += 0.5f * (fs / hz - P_);
                    midiF_ = 69.f + 12.f * std::log2(hz / 440.f);
                    if (!hasNote_ || std::fabs(midiF_ - float(nearest_)) > 0.65f) {
                        nearest_ = int(std::lround(midiF_));
                        hasNote_ = true;
                        for (int v = 0; v < 3; ++v) lastSteps_[v] = 1000;
                    }
                }
            }
            // diatonic intervals (recomputed only when something changes)
            for (int v = 0; v < 3; ++v) {
                const VoiceParams& q = p.v[v];
                if (q.mode == 0) {
                    if (lastSteps_[v] != q.steps) {
                        iv_[v] = hasNote_ ? diatonicInterval(nearest_, q.steps, p.key, p.scale) : 0;
                        lastSteps_[v] = q.steps;
                    }
                } else {
                    iv_[v] = int(std::lround(q.semi));
                }
            }
            float oL = 0.f, oR = 0.f;
            for (int v = 0; v < 3; ++v) {
                en_[v] += 0.002f * ((vOn[v] ? 1.f : 0.f) - en_[v]);
                if (en_[v] < 0.0005f && !vOn[v]) { sh_[v].reset(); continue; }
                // slow random drift (two filter stages)
                rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                float nz = float(int32_t(rng_)) * (1.f / 2147483648.f);
                rw1_[v] += driftA * 0.4f * (nz - rw1_[v]);
                rw2_[v] += driftA * 0.4f * (rw1_[v] - rw2_[v]);
                float drift = std::max(-1.f, std::min(1.f, rw2_[v] * 330.f)) * p.driftCents * 0.01f;
                const VoiceParams& q = p.v[v];
                float target = float(iv_[v]) + vTgtFixed[v] + whammy + drift;
                if (q.mode == 0 && !hasNote_) target = vTgtFixed[v] + whammy + drift;
                s_[v] += glideC * (target - s_[v]);
                float rho = std::exp2(s_[v] * (1.f / 12.f));
                float y = sh_[v].process(buf_.data(), BUF - 1, w_, rho, P_);
                // voice delay
                vd_[v][vw_[v] & (DLY - 1)] = y;
                ++vw_[v];
                if (vDly[v] > 1.f) {
                    float dd = vDly[v];
                    uint32_t i0 = vw_[v] - 1u - uint32_t(dd);
                    float f = dd - std::floor(dd);
                    float a = vd_[v][i0 & (DLY - 1)], b = vd_[v][(i0 - 1) & (DLY - 1)];
                    y = a + f * (b - a);
                }
                y *= vg[v] * en_[v];
                oL += y * vL[v];
                oR += y * vR[v];
            }
            // direct path, possibly with Whammy (pedal)
            wd_ += 0.003f * ((p.whammyDry ? 1.f : 0.f) - wd_);
            float dry = x;
            if (wd_ > 0.0005f || p.whammyDry) {
                sW_ += whC * (whammy - sW_);
                float ym = shMain_.process(buf_.data(), BUF - 1, w_, std::exp2(sW_ * (1.f / 12.f)), P_);
                dry = (1.f - wd_) * x + wd_ * ym;
            } else {
                shMain_.reset();
                sW_ = whammy;
            }
            outL[t] = dryG * dry + lvG * oL;
            outR[t] = dryG * dry + lvG * oR;
        }
        pitchOut_ = yin_.voiced() ? yin_.hz() : 0.f;
        noteOut_ = yin_.voiced() ? midiF_ : 0.f;
        voicedOut_ = yin_.voiced() ? 1.f : 0.f;
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(csr);
#endif
    }

    float pitchHz() const { return pitchOut_; }
    float noteMidi() const { return noteOut_; }
    float voiced() const { return voicedOut_; }
    float period() const { return P_; }

private:
    double fs_ = 48000.0;
    std::vector<float> buf_;
    uint32_t w_ = 0;
    std::vector<float> vd_[3];
    uint32_t vw_[3] = {};
    Yin yin_;
    Shifter sh_[3], shMain_;
    float s_[3] = {}, en_[3] = {}, rw1_[3] = {}, rw2_[3] = {};
    int iv_[3] = {};
    int lastSteps_[3] = {1000, 1000, 1000};
    int lastKey_ = -1, lastScale_ = -1;
    float sW_ = 0.f, wd_ = 0.f, P_ = 480.f;
    bool hasNote_ = false;
    int nearest_ = 60;
    float midiF_ = 60.f;
    float pitchOut_ = 0.f, noteOut_ = 0.f, voicedOut_ = 0.f;
    uint32_t rng_ = 1;
    bool first_ = true;
};

}  // namespace adelshifter
