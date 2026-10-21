// ADELverb v0.3: 16-line FDN reverb, mono input, stereo output.
// Public techniques, no third-party code:
//  - FDN with Householder matrix (Jot & Chaigne 1991), three-band decay per line
//  - all-pass diffusers inside the feedback and at the input (Schroeder / Dattorro 1997)
//  - cubic interpolation, sinusoidal modulation + random drift
//  - early reflections from a rectangular room, image method (Allen & Berkley 1979), 5 room shapes
//  - Spring (dispersive all-pass chains, Parker/Valimaki),
//    Tuned tail (tuned comb filters), Bloom, Gate, Ducking, Freeze, Drive, Vintage (quantization in the loop),
//    3-band EQ (RBJ biquads), Distance, Morph
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dspkit.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace adelverb {

struct Params {
    float mix = 0.30f;           // 0..1
    int mixMode = 0;             // 0 = Keep Dry (fixed dry, reverb added), 1 = Crossfade (dry drops with the Mix)
    bool wetOnly = false;        // output = reverb only (no dry signal); Mix does not count
    float outLevelDb = 0.f;      // overall output level
    float predelayMs = 22.f;
    float decay = 2.8f;          // s
    float size = 1.0f;           // 0.5..2
    int shape = 2;               // 0 Small Room, 1 Studio, 2 Hall, 3 Cathedral, 4 Cave
    float distance = 0.f;        // 0..1
    float lateDelayMs = 12.f;
    float er = 0.35f;            // 0..1
    float diffusion = 0.7f;      // 0..1
    float bass = 1.0f;
    float bassXover = 300.f;
    float treble = 0.45f;
    float trebleXover = 2500.f;
    float modDepth = 8.0f;       // samples at 48 kHz
    float modRate = 0.60f;       // Hz
    float lowCut = 120.f;
    float highCut = 9000.f;
    float eqLow = 0.f;           // dB, shelf a 250 Hz
    float eqMidFreq = 1000.f;
    float eqMid = 0.f;           // dB, peak
    float eqHigh = 0.f;          // dB, shelf a 4 kHz
    float width = 1.0f;          // 0..1
    float spring = 0.f;          // 0..1
    float tuned = 0.f;           // 0..1
    int tunedRoot = 0;           // 0..11 (C..B)
    int tunedChord = 0;          // 0 Octave, 1 Fifth, 2 Major, 3 Minor, 4 Sus4, 5 Maj7
    float bloom = 0.f;           // 0..1
    float bloomMs = 600.f;
    float gate = 0.f;            // 0..1
    float gateHoldMs = 400.f;
    float gateRelMs = 80.f;
    float duck = 0.0f;           // 0..1
    float duckRelease = 400.f;
    float vintage = 0.f;         // 0..1
    float drive = 0.0f;          // 0..1
    float freeze = 0.0f;         // 0 o 1
    float morph = 0.f;           // 0..1
    int morphTarget = 0;         // 0 Plate, 1 Dark Hall, 2 Ambient Wash, 3 Tight Room, 4 Cathedral
};

class Reverb {
public:
    static constexpr int N = 16;
    static constexpr int K = 32;
    static constexpr int ROOMS = 5;
    static constexpr int NSPRING_STAGES = 48;
    static constexpr int NCOMB = 6;

    void init(double sampleRate) {
        fs_ = sampleRate;
        scale_ = float(fs_ / 48000.0);
        for (int i = 0; i < N; ++i) {
            base_[i] = float(nextPrime(int(1000.0 * std::pow(3.6, double(i) / (N - 1)))));
            uint32_t need = uint32_t(base_[i] * 3.0f * scale_) + 64;
            uint32_t sz = 1;
            while (sz < need) sz <<= 1;
            line_[i].assign(sz, 0.f);
            mask_[i] = sz - 1;
            apLen_[i] = std::max(8, int(std::lround(nextPrime(90 + 14 * i) * scale_)));
            apBuf_[i].assign(apLen_[i], 0.f);
        }
        const int dl[4] = {229, 172, 611, 447};
        for (int k = 0; k < 4; ++k) inBuf_[k].assign(std::max(8, int(std::lround(dl[k] * scale_))), 0.f);
        uint32_t need = uint32_t(0.95 * fs_) + 8;
        uint32_t sz = 1;
        while (sz < need) sz <<= 1;
        pd_.assign(sz, 0.f);
        pdMask_ = sz - 1;
        buildRooms();
        // springs
        const int springLen[2] = {1297, 1511};
        const float springA[2] = {-0.60f, -0.57f};
        for (int s = 0; s < 2; ++s) {
            int len = std::max(64, int(std::lround(springLen[s] * scale_)));
            sz = 1;
            while (sz < uint32_t(len + 8)) sz <<= 1;
            spr_[s].buf.assign(sz, 0.f);
            spr_[s].mask = sz - 1;
            spr_[s].len = len;
            spr_[s].a = springA[s];
            spr_[s].dstage = std::max(1, int(std::lround(5 * scale_)));
            for (int m = 0; m < NSPRING_STAGES; ++m) {
                spr_[s].xh[m].assign(spr_[s].dstage, 0.f);
                spr_[s].yh[m].assign(spr_[s].dstage, 0.f);
            }
        }
        // tuned combs: maximum delay for 55 Hz
        sz = 1;
        while (sz < uint32_t(fs_ / 50.0) + 16) sz <<= 1;
        for (int ch = 0; ch < 2; ++ch)
            for (int c = 0; c < NCOMB; ++c) {
                comb_[ch][c].assign(sz, 0.f);
            }
        combMask_ = sz - 1;
        clear();
    }

    void clear() {
        for (int i = 0; i < N; ++i) {
            std::fill(line_[i].begin(), line_[i].end(), 0.f);
            std::fill(apBuf_[i].begin(), apBuf_[i].end(), 0.f);
            apI_[i] = 0;
            w_[i] = 0;
            lp1_[i] = lp2_[i] = 0.f;
            z_[i] = 0.f;
            phase_[i] = float(i) * 6.2831853f / N;
            sP_[i] = std::sin(phase_[i]);
            cP_[i] = std::cos(phase_[i]);
            rw_[i] = rw1_[i] = 0.f;
            adaa_[i].reset();
        }
        for (auto& b : inBuf_) std::fill(b.begin(), b.end(), 0.f);
        for (int k = 0; k < 4; ++k) inI_[k] = 0;
        std::fill(pd_.begin(), pd_.end(), 0.f);
        pdW_ = 0;
        for (int s = 0; s < 2; ++s) {
            std::fill(spr_[s].buf.begin(), spr_[s].buf.end(), 0.f);
            spr_[s].w = 0;
            spr_[s].n = 0;
            spr_[s].lp = 0.f;
            for (int m = 0; m < NSPRING_STAGES; ++m) {
                std::fill(spr_[s].xh[m].begin(), spr_[s].xh[m].end(), 0.f);
                std::fill(spr_[s].yh[m].begin(), spr_[s].yh[m].end(), 0.f);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int c = 0; c < NCOMB; ++c) {
                std::fill(comb_[ch][c].begin(), comb_[ch][c].end(), 0.f);
                combLp_[ch][c] = 0.f;
            }
        combW_ = 0;
        for (int ch = 0; ch < 2; ++ch) {
            l1_[ch] = l2_[ch] = h1_[ch] = h2_[ch] = 0.f;
            for (int b = 0; b < 3; ++b) eqS_[ch][b][0] = eqS_[ch][b][1] = 0.f;
        }
        rng_ = 0x9E3779B9u;
        env_ = envF_ = envS_ = 0.f;
        sinceOnset_ = 1u << 30;
        gateTimer_ = 0;
        gateG_ = 1.f;
        first_ = true;
    }

    // mono output: writes only outL (outR is not needed)
    void setMonoOut(bool m) { monoOut_ = m; }

    void process(const float* in, float* outL, float* outR, uint32_t n, const Params& pin) {
#if defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        uint64_t fpcrNew = fpcr | (1ull << 24);  // FZ: flush denormals to zero
        asm volatile("msr fpcr, %0" ::"r"(fpcrNew));
#elif defined(__x86_64__) || defined(__i386__)
        unsigned csr = _mm_getcsr();
        _mm_setcsr(csr | 0x8040);
#endif
        const Params p = applyMorph(pin);
        const float fs = float(fs_);
        const int shape = std::min(ROOMS - 1, std::max(0, p.shape));
        static const float shapeMul[ROOMS] = {0.55f, 0.8f, 1.0f, 1.5f, 1.9f};
        const float sizeUser = clampf(p.size, 0.5f, 2.0f);
        const float sizeEff = clampf(sizeUser * shapeMul[shape], 0.4f, 3.0f);
        for (int i = 0; i < N; ++i) {
            lenTgt_[i] = std::max(64.f, base_[i] * sizeEff * scale_);
            if (first_) lenCur_[i] = lenTgt_[i];
        }
        for (int k = 0; k < K; ++k) {
            erTgtL_[k] = std::max(1.f, sizeUser * erDelayL_[shape][k] * scale_);
            erTgtR_[k] = std::max(1.f, sizeUser * erDelayR_[shape][k] * scale_);
            erGTgtL_[k] = erGainL_[shape][k];
            erGTgtR_[k] = erGainR_[shape][k];
            if (first_) {
                erCurL_[k] = erTgtL_[k]; erCurR_[k] = erTgtR_[k];
                erGCurL_[k] = erGTgtL_[k]; erGCurR_[k] = erGTgtR_[k];
            }
        }
        const float dist = clampf(p.distance, 0.f, 1.f);
        const float decay = clampf(p.decay, 0.1f, 60.f);
        const float rtLo = std::max(0.05f, decay * clampf(p.bass, 0.2f, 3.f));
        const float rtMid = decay;
        const float rtHi = std::max(0.05f, decay * clampf(p.treble, 0.05f, 3.f) * (1.f - 0.35f * dist));
        float fx1 = clampf(p.bassXover, 40.f, 0.2f * fs);
        float fx2 = clampf(p.trebleXover, fx1 * 1.5f, 0.45f * fs);
        const float c1 = std::exp(-6.2831853f * fx1 / fs);
        const float c2 = std::exp(-6.2831853f * fx2 / fs);
        const float hcFreq = clampf(p.highCut, 500.f, 0.45f * fs) * (1.f - 0.6f * dist);
        const float hcC = std::exp(-6.2831853f * hcFreq / fs);
        const float lcC = std::exp(-6.2831853f * clampf(p.lowCut, 5.f, 2000.f) / fs);
        const float diff = clampf(p.diffusion, 0.f, 1.f);
        const float gIn = 0.35f + 0.40f * diff;
        const float gAp = 0.30f + 0.38f * diff;
        const float comp = 0.35f / std::sqrt(std::max(0.4f, decay));
        const float depth = std::max(0.f, p.modDepth) * scale_;
        if (!haveInc_ || p.modRate != incKey_) {
            for (int i = 0; i < N; ++i) {
                const float inc = 6.2831853f * p.modRate * (0.7f + 0.05f * i) / fs;
                cdK_[i] = std::cos(inc);
                sdK_[i] = std::sin(inc);
            }
            incKey_ = p.modRate;
            haveInc_ = true;
        }
        for (int i = 0; i < N; ++i) {  // renormalization of the oscillator (once per block)
            const float r = 1.5f - 0.5f * (sP_[i] * sP_[i] + cP_[i] * cP_[i]);
            sP_[i] *= r; cP_[i] *= r;
        }
        // Mix: mode 0 "Keep Dry" = the dry stays at full level and the reverb is added on top (the volume does not drop);
        // the reverb is raised by 6 dB so that Mix 100% is close to the dry level. Mode 1 = crossfade.
        const bool keepDry = p.mixMode == 0 && !p.wetOnly;
        const float mixT = p.wetOnly ? 1.f : clampf(p.mix, 0.f, 1.f) * (keepDry ? 2.0f : 1.0f);
        const float dryT = p.wetOnly ? 0.f : (keepDry ? 1.f : 1.f - clampf(p.mix, 0.f, 1.f));
        const float outG = std::pow(10.f, clampf(p.outLevelDb, -24.f, 12.f) / 20.f);
        const float erT = clampf(p.er, 0.f, 1.f) * 0.35f * (1.f - 0.75f * dist);
        const float widthT = clampf(p.width, 0.f, 1.f);
        const float pdT = clampf(p.predelayMs, 0.f, 250.f) * 1e-3f * fs;
        const float lateT = clampf(p.lateDelayMs, 0.f, 100.f) * 1e-3f * fs;
        const float freezeT = p.freeze > 0.5f ? 1.f : 0.f;
        const float driveT = clampf(p.drive, 0.f, 1.f);
        const float duckT = clampf(p.duck, 0.f, 1.f);
        const float relC = std::exp(-1.f / (std::max(10.f, p.duckRelease) * 1e-3f * fs));
        const float springT = clampf(p.spring, 0.f, 1.f);
        const float tunedT = clampf(p.tuned, 0.f, 1.f);
        const float bloomT = clampf(p.bloom, 0.f, 1.f);
        const float bloomSamples = std::max(10.f, p.bloomMs * 1e-3f * fs);
        const float gateT = clampf(p.gate, 0.f, 1.f);
        const uint32_t holdSamples = uint32_t(std::max(5.f, p.gateHoldMs) * 1e-3f * fs);
        const float gateRelC = std::exp(-1.f / (std::max(5.f, p.gateRelMs) * 1e-3f * fs));
        const float vintT = clampf(p.vintage, 0.f, 1.f);
        // springs: feedback gain from the decay time
        float springG[2];
        for (int s = 0; s < 2; ++s) {
            float loopT = float(spr_[s].len + 330 * scale_) / fs;
            springG[s] = std::pow(10.f, -3.f * loopT / std::min(4.5f, std::max(0.3f, decay * 0.9f)));
        }
        // tuned combs: frequencies of the chord
        if (tunedT > 0.001f) setupCombs(p.tunedRoot, p.tunedChord);
        // EQ: recomputes the biquads
        const bool eqOn = std::fabs(p.eqLow) > 0.05f || std::fabs(p.eqMid) > 0.05f || std::fabs(p.eqHigh) > 0.05f;
        if (eqOn) setupEq(p.eqLow, p.eqMidFreq, p.eqMid, p.eqHigh);
        if (first_) {
            mix_ = mixT; dryS_ = dryT; erG_ = erT; width_ = widthT; pdCur_ = pdT; lateCur_ = lateT;
            freeze_ = freezeT; drive_ = driveT; duck_ = duckT;
            spring_ = springT; tuned_ = tunedT; bloom_ = bloomT; gate_ = gateT; vint_ = vintT;
            first_ = false;
        }
        const float fz = freeze_;
        float gLo[N], gMid[N], gHi[N];
        {
            bool same = haveG_ && rtLo == kLo_ && rtMid == kMid_ && rtHi == kHi_ && fz == kFz_;
            for (int i = 0; i < N && same; ++i) same = lenCur_[i] == kLen_[i];
            if (!same) {
                for (int i = 0; i < N; ++i) {
                    float L = lenCur_[i] + float(apLen_[i]);
                    float a = std::pow(10.f, -3.f * L / (fs * rtLo));
                    float b = std::pow(10.f, -3.f * L / (fs * rtMid));
                    float c = std::pow(10.f, -3.f * L / (fs * rtHi));
                    const float hold = 0.99996f;
                    gLoC_[i] = a + fz * (hold - a);
                    gMidC_[i] = b + fz * (hold - b);
                    gHiC_[i] = c + fz * (hold - c);
                    kLen_[i] = lenCur_[i];
                }
                kLo_ = rtLo; kMid_ = rtMid; kHi_ = rtHi; kFz_ = fz;
                haveG_ = true;
            }
            for (int i = 0; i < N; ++i) { gLo[i] = gLoC_[i]; gMid[i] = gMidC_[i]; gHi[i] = gHiC_[i]; }
        }
        static const float sIn[N] = {1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1};
        float sL[N], sR[N];
        for (int i = 0; i < N; ++i) {
            sL[i] = (__builtin_popcount(i & 5) & 1) ? -1.f : 1.f;
            sR[i] = (__builtin_popcount(i & 9) & 1) ? -1.f : 1.f;
        }
        bool erMoving = false;  // are the early reflections still sliding towards the new target?
        for (int k = 0; k < K && !erMoving; ++k)
            erMoving = erCurL_[k] != erTgtL_[k] || erCurR_[k] != erTgtR_[k] || erGCurL_[k] != erGTgtL_[k] || erGCurR_[k] != erGTgtR_[k];
        const float sm = 0.002f, slewLen = 0.0015f, slewPd = 0.25f, slewEr = 0.04f, slewG = 0.0005f;
        const float outScale = comp * 1.0f;
        const float inj = 0.25f;
        const float onsetMin = 0.004f;
        const float slowC = 1.f / (0.08f * fs);

        for (uint32_t t = 0; t < n; ++t) {
            mix_ += sm * (mixT - mix_);
            dryS_ += sm * (dryT - dryS_);
            erG_ += sm * (erT - erG_);
            width_ += sm * (widthT - width_);
            freeze_ += 0.0004f * (freezeT - freeze_);
            drive_ += sm * (driveT - drive_);
            duck_ += sm * (duckT - duck_);
            spring_ += sm * (springT - spring_);
            tuned_ += sm * (tunedT - tuned_);
            bloom_ += sm * (bloomT - bloom_);
            gate_ += sm * (gateT - gate_);
            vint_ += sm * (vintT - vint_);
            pdCur_ += clampf(pdT - pdCur_, -slewPd, slewPd);
            lateCur_ += clampf(lateT - lateCur_, -slewPd, slewPd);

            const float x = in[t];
            float ax = std::fabs(x);
            // ducking
            env_ = ax > env_ ? env_ + 0.05f * (ax - env_) : env_ * relC;
            float duckAmt = clampf(env_ * 6.f, 0.f, 1.f);
            float wetG = 1.f - duck_ * std::sqrt(duckAmt);
            // note attack detection (bloom and gate)
            envF_ = ax > envF_ ? envF_ + 0.3f * (ax - envF_) : envF_ * 0.9995f;
            envS_ += slowC * (ax - envS_);
            if (sinceOnset_ < (1u << 30)) ++sinceOnset_;
            if (envF_ > onsetMin && envF_ > 3.f * envS_ + 0.002f && sinceOnset_ > uint32_t(0.1f * fs)) {
                sinceOnset_ = 0;
                gateTimer_ = holdSamples;
            }
            if (bloom_ > 0.002f) {
                float r = std::min(1.f, float(sinceOnset_) / bloomSamples);
                float e = r * r * (3.f - 2.f * r);
                wetG *= (1.f - bloom_) + bloom_ * e;
            }
            if (gate_ > 0.002f) {
                if (gateTimer_ > 0) { --gateTimer_; gateG_ += 0.02f * (1.f - gateG_); }
                else gateG_ *= gateRelC;
                wetG *= 1.f - gate_ * (1.f - gateG_);
            } else {
                gateG_ = 1.f;
            }

            pd_[pdW_ & pdMask_] = x;
            float xl = readCubic(pd_.data(), pdMask_, pdW_, std::max(1.f, pdCur_ + lateCur_));
            float erl = 0.f, err = 0.f;
            for (int k = 0; k < K; ++k) {
                if (erMoving) {
                    erCurL_[k] += clampf(erTgtL_[k] - erCurL_[k], -slewEr, slewEr);
                    erCurR_[k] += clampf(erTgtR_[k] - erCurR_[k], -slewEr, slewEr);
                    erGCurL_[k] += clampf(erGTgtL_[k] - erGCurL_[k], -slewG, slewG);
                    erGCurR_[k] += clampf(erGTgtR_[k] - erGCurR_[k], -slewG, slewG);
                }
                erl += erGCurL_[k] * readCubic(pd_.data(), pdMask_, pdW_, std::max(1.f, pdCur_ + erCurL_[k]));
                err += erGCurR_[k] * readCubic(pd_.data(), pdMask_, pdW_, std::max(1.f, pdCur_ + erCurR_[k]));
            }
            ++pdW_;
            // input diffusion
            float v = xl;
            for (int k = 0; k < 4; ++k) {
                std::vector<float>& b = inBuf_[k];
                float d = b[inI_[k]];
                float u = v - gIn * d;
                b[inI_[k]] = u;
                if (++inI_[k] >= int(b.size())) inI_[k] = 0;
                v = d + gIn * u;
            }
            // springs (in parallel with the FDN)
            float spL = 0.f, spR = 0.f;
            if (spring_ > 0.002f) {
                spL = springProcess(spr_[0], xl * 0.5f, springG[0]);
                spR = springProcess(spr_[1], xl * 0.5f, springG[1]);
            }
            v *= (1.f - freeze_);
            float ys[N];
            float s = 0.f;
            const float vk = vint_ > 0.002f ? (std::pow(2.f, 23.f - 11.f * vint_)) : 0.f;  // 24 -> 13 bit
            for (int i = 0; i < N; ++i) {
                rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                float nz = float(int32_t(rng_)) * (1.f / 2147483648.f);
                rw1_[i] += 6.5e-5f * (nz - rw1_[i]);
                rw_[i] += 6.5e-5f * (rw1_[i] - rw_[i]);
                float dl = lenTgt_[i] - lenCur_[i];
                lenCur_[i] += clampf(dl, -slewLen, slewLen);
                const float sn = sP_[i] * cdK_[i] + cP_[i] * sdK_[i];
                const float cn = cP_[i] * cdK_[i] - sP_[i] * sdK_[i];
                sP_[i] = sn; cP_[i] = cn;
                float m = 0.65f * sn + 0.35f * clampf(rw_[i] * 220.f, -1.f, 1.f);
                float dly = lenCur_[i] + depth * m;
                float y = readCubic(line_[i].data(), mask_[i], w_[i], dly);
                std::vector<float>& ab = apBuf_[i];
                float da = ab[apI_[i]];
                float ua = y - gAp * da;
                ab[apI_[i]] = ua;
                if (++apI_[i] >= apLen_[i]) apI_[i] = 0;
                y = da + gAp * ua;
                ys[i] = y;
                lp1_[i] = (1.f - c1) * y + c1 * lp1_[i];
                lp2_[i] = (1.f - c2) * y + c2 * lp2_[i];
                float z = gHi[i] * (y - lp2_[i]) + gMid[i] * (lp2_[i] - lp1_[i]) + gLo[i] * lp1_[i];
                if (drive_ > 0.0005f) z = adaa_[i].process(z, 10.0 * drive_);  // saturator with ADAA anti-aliasing
                else adaa_[i].reset();
                if (vk > 0.f) {
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    float d1 = float(int32_t(rng_)) * (1.f / 4294967296.f);
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    float d2 = float(int32_t(rng_)) * (1.f / 4294967296.f);
                    z = std::floor(z * vk + 0.5f + d1 + d2) / vk;
                }
                z_[i] = z;
                s += z;
            }
            const float mean = 2.f * s / N;
            for (int i = 0; i < N; ++i) {
                line_[i][w_[i] & mask_[i]] = (z_[i] - mean) + inj * sIn[i] * v;
                ++w_[i];
            }
            float wl = 0.f, wr = 0.f;
            for (int i = 0; i < N; ++i) {
                wl += sL[i] * ys[i];
                wr += sR[i] * ys[i];
            }
            wl *= outScale;
            wr *= outScale;
            if (spring_ > 0.002f) {
                wl = (1.f - spring_) * wl + spring_ * spL * 1.6f;
                wr = (1.f - spring_) * wr + spring_ * spR * 1.6f;
            }
            wl += erG_ * erl;
            wr += erG_ * err;
            // tuned tail
            if (tuned_ > 0.002f) {
                float cl = 0.f, cr = 0.f;
                for (int c = 0; c < NCOMB; ++c) {
                    cl += combProcess(0, c, wl);
                    cr += combProcess(1, c, wr);
                }
                ++combW_;
                wl += tuned_ * cl * 0.9f;
                wr += tuned_ * cr * 0.9f;
            }
            float mid = 0.5f * (wl + wr);
            float sid = 0.5f * (wl - wr) * width_;
            wl = mid + sid;
            wr = mid - sid;
            if (eqOn) {
                wl = eqRun(0, wl);
                wr = eqRun(1, wr);
            }
            l1_[0] = (1.f - lcC) * wl + lcC * l1_[0];
            wl -= l1_[0];
            l2_[0] = (1.f - lcC) * wl + lcC * l2_[0];
            wl -= l2_[0];
            l1_[1] = (1.f - lcC) * wr + lcC * l1_[1];
            wr -= l1_[1];
            l2_[1] = (1.f - lcC) * wr + lcC * l2_[1];
            wr -= l2_[1];
            h1_[0] = (1.f - hcC) * wl + hcC * h1_[0];
            h2_[0] = (1.f - hcC) * h1_[0] + hcC * h2_[0];
            h1_[1] = (1.f - hcC) * wr + hcC * h1_[1];
            h2_[1] = (1.f - hcC) * h1_[1] + hcC * h2_[1];
            if (monoOut_) {
                // mono output: the dry signal stays as it is, the reverb's two (decorrelated) channels are summed with 1/sqrt(2)
                // so the reverb level stays that of one channel
                outL[t] = outG * (dryS_ * x + mix_ * wetG * 0.70710678f * (h2_[0] + h2_[1]));
            } else {
                outL[t] = outG * (dryS_ * x + mix_ * wetG * h2_[0]);
                outR[t] = outG * (dryS_ * x + mix_ * wetG * h2_[1]);
            }
        }
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(csr);
#endif
    }

private:
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static int nextPrime(int n) {
        for (;; ++n) {
            if (n < 2) continue;
            bool p = true;
            for (int d = 2; d * d <= n; ++d)
                if (n % d == 0) { p = false; break; }
            if (p) return n;
        }
    }

    static inline float readCubic(const float* b, uint32_t mask, uint32_t w, float dly) {
        float dfl = std::floor(dly);
        float f = 1.f - (dly - dfl);
        uint32_t i = w - uint32_t(int32_t(dfl)) - 1u;
        float y0 = b[(i - 1) & mask], y1 = b[i & mask], y2 = b[(i + 1) & mask], y3 = b[(i + 2) & mask];
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // ---- morph ----
    struct Target {
        float decay, size, treble, bass, diffusion, modDepth, er, late, predelay, lowCut, highCut;
    };

    Params applyMorph(const Params& p) const {
        static const Target tg[5] = {
            // decay size treble bass diffusion mod  er   late pre  lowcut highcut
            {1.9f, 0.7f, 0.80f, 0.9f, 0.85f, 6.f, 0.0f, 0.f, 4.f, 150.f, 14000.f},   // Plate
            {4.5f, 1.4f, 0.30f, 1.1f, 0.70f, 8.f, 0.3f, 20.f, 30.f, 100.f, 6000.f},   // Dark Hall
            {9.0f, 2.0f, 0.50f, 1.0f, 0.80f, 14.f, 0.1f, 40.f, 40.f, 140.f, 8000.f},  // Ambient Wash
            {0.7f, 0.6f, 0.60f, 0.9f, 0.60f, 4.f, 0.6f, 4.f, 2.f, 120.f, 10000.f},    // Tight Room
            {8.0f, 2.0f, 0.55f, 1.3f, 0.75f, 8.f, 0.4f, 40.f, 45.f, 100.f, 7500.f}};  // Cathedral
        float m = clampf(p.morph, 0.f, 1.f);
        if (m <= 0.0005f) return p;
        const Target& t = tg[std::min(4, std::max(0, p.morphTarget))];
        auto lin = [m](float a, float b) { return a + m * (b - a); };
        auto lg = [m](float a, float b) { return std::exp(std::log(std::max(1e-3f, a)) + m * (std::log(std::max(1e-3f, b)) - std::log(std::max(1e-3f, a)))); };
        Params q = p;
        q.decay = lg(p.decay, t.decay);
        q.size = lin(p.size, t.size);
        q.treble = lin(p.treble, t.treble);
        q.bass = lin(p.bass, t.bass);
        q.diffusion = lin(p.diffusion, t.diffusion);
        q.modDepth = lin(p.modDepth, t.modDepth);
        q.er = lin(p.er, t.er);
        q.lateDelayMs = lin(p.lateDelayMs, t.late);
        q.predelayMs = lin(p.predelayMs, t.predelay);
        q.lowCut = lg(p.lowCut, t.lowCut);
        q.highCut = lg(p.highCut, t.highCut);
        return q;
    }

    // ---- rooms (image method) ----
    void buildRooms() {
        struct R { double W, D, H, beta; };
        static const R rooms[ROOMS] = {
            {4.0, 5.0, 2.6, 0.75},    // Small Room
            {6.0, 8.0, 3.0, 0.80},    // Studio
            {7.0, 9.0, 3.5, 0.85},    // Hall
            {20.0, 40.0, 18.0, 0.88}, // Cathedral
            {15.0, 30.0, 10.0, 0.90}  // Cave
        };
        const double c = 343.0;
        for (int r = 0; r < ROOMS; ++r) {
            const double W = rooms[r].W, D = rooms[r].D, H = rooms[r].H, beta = rooms[r].beta;
            const double sx = 0.35 * W, sy = 0.20 * D, sz = 1.5;
            const double lx = 0.60 * W, ly = 0.55 * D, lz = 1.6;
            for (int ear = 0; ear < 2; ++ear) {
                double ex = lx + (ear == 0 ? -0.09 : 0.09);
                struct Img { double d; int order; };
                std::vector<Img> imgs;
                double d0 = std::sqrt((sx - ex) * (sx - ex) + (sy - ly) * (sy - ly) + (sz - lz) * (sz - lz));
                for (int nx = -1; nx <= 1; ++nx)
                    for (int qx = 0; qx <= 1; ++qx)
                        for (int ny = -1; ny <= 1; ++ny)
                            for (int qy = 0; qy <= 1; ++qy)
                                for (int nz = -1; nz <= 1; ++nz)
                                    for (int qz = 0; qz <= 1; ++qz) {
                                        int order = std::abs(nx - qx) + std::abs(nx) + std::abs(ny - qy) + std::abs(ny) +
                                                    std::abs(nz - qz) + std::abs(nz);
                                        if (order < 1 || order > 3) continue;
                                        double ix = (1 - 2 * qx) * sx + 2 * nx * W;
                                        double iy = (1 - 2 * qy) * sy + 2 * ny * D;
                                        double iz = (1 - 2 * qz) * sz + 2 * nz * H;
                                        double d = std::sqrt((ix - ex) * (ix - ex) + (iy - ly) * (iy - ly) + (iz - lz) * (iz - lz));
                                        imgs.push_back({d, order});
                                    }
                std::sort(imgs.begin(), imgs.end(), [](const Img& a, const Img& b) { return a.d < b.d; });
                for (int k = 0; k < K && k < int(imgs.size()); ++k) {
                    float del = float((imgs[k].d - d0) / c * 48000.0);
                    float g = float(std::pow(beta, imgs[k].order) * d0 / imgs[k].d);
                    if (ear == 0) { erDelayL_[r][k] = del; erGainL_[r][k] = g; }
                    else { erDelayR_[r][k] = del; erGainR_[r][k] = g; }
                }
            }
        }
    }

    // ---- spring ----
    struct Spring {
        std::vector<float> buf;
        uint32_t mask = 0, w = 0;
        int len = 0, dstage = 5;
        uint32_t n = 0;
        float a = -0.6f, lp = 0.f;
        std::vector<float> xh[NSPRING_STAGES], yh[NSPRING_STAGES];
    };

    float springProcess(Spring& s, float in, float g) {
        float y = readCubic(s.buf.data(), s.mask, s.w, float(s.len));
        s.lp += 0.62f * (y - s.lp);
        float v = s.lp;
        int idx = int(s.n % uint32_t(s.dstage));
        const float a = s.a;
        for (int m = 0; m < NSPRING_STAGES; ++m) {
            float xn = v;
            float yn = a * xn + s.xh[m][idx] - a * s.yh[m][idx];
            s.xh[m][idx] = xn;
            s.yh[m][idx] = yn;
            v = yn;
        }
        ++s.n;
        s.buf[s.w & s.mask] = in + g * v;
        ++s.w;
        return y;
    }

    // ---- tuned combs ----
    void setupCombs(int root, int chord) {
        static const int chords[6][NCOMB] = {{0, 12, 24, 0, 12, 24}, {0, 7, 12, 19, 24, 7},
                                             {0, 4, 7, 12, 16, 19}, {0, 3, 7, 12, 15, 19},
                                             {0, 5, 7, 12, 17, 19}, {0, 4, 7, 11, 12, 16}};
        const int* ch = chords[std::min(5, std::max(0, chord))];
        for (int c = 0; c < NCOMB; ++c) {
            float f = 130.81f * std::pow(2.f, float(std::min(11, std::max(0, root)) + ch[c]) / 12.f);
            combDelay_[c] = float(fs_) / f;
        }
    }

    float combProcess(int ch, int c, float x) {
        const float g = 0.986f;
        float y = readCubic(comb_[ch][c].data(), combMask_, combW_, combDelay_[c]);
        combLp_[ch][c] += 0.5f * (y - combLp_[ch][c]);
        float v = x * 0.18f + g * combLp_[ch][c];
        comb_[ch][c][combW_ & combMask_] = v;
        return v * (1.f - g) * 6.0f;
    }

    // ---- 3-band EQ (RBJ biquads) ----
    void setupEq(float lowDb, float midFreq, float midDb, float highDb) {
        const float fs = float(fs_);
        auto coef = [&](int band, int type, float f, float dB, float Q) {
            float A = std::pow(10.f, dB / 40.f);
            float w0 = 6.2831853f * std::min(f, 0.45f * fs) / fs;
            float cw = std::cos(w0), sw = std::sin(w0);
            float b0, b1, b2, a0, a1, a2;
            if (type == 1) {  // peaking
                float al = sw / (2.f * Q);
                b0 = 1.f + al * A; b1 = -2.f * cw; b2 = 1.f - al * A;
                a0 = 1.f + al / A; a1 = -2.f * cw; a2 = 1.f - al / A;
            } else {
                float al = sw / 2.f * std::sqrt(2.f);  // S = 1
                float sq = 2.f * std::sqrt(A) * al;
                if (type == 0) {  // low shelf
                    b0 = A * ((A + 1.f) - (A - 1.f) * cw + sq);
                    b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cw);
                    b2 = A * ((A + 1.f) - (A - 1.f) * cw - sq);
                    a0 = (A + 1.f) + (A - 1.f) * cw + sq;
                    a1 = -2.f * ((A - 1.f) + (A + 1.f) * cw);
                    a2 = (A + 1.f) + (A - 1.f) * cw - sq;
                } else {  // high shelf
                    b0 = A * ((A + 1.f) + (A - 1.f) * cw + sq);
                    b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw);
                    b2 = A * ((A + 1.f) + (A - 1.f) * cw - sq);
                    a0 = (A + 1.f) - (A - 1.f) * cw + sq;
                    a1 = 2.f * ((A - 1.f) - (A + 1.f) * cw);
                    a2 = (A + 1.f) - (A - 1.f) * cw - sq;
                }
            }
            float r = 1.f / a0;
            eqC_[band][0] = b0 * r; eqC_[band][1] = b1 * r; eqC_[band][2] = b2 * r;
            eqC_[band][3] = a1 * r; eqC_[band][4] = a2 * r;
        };
        coef(0, 0, 250.f, clampf(lowDb, -18.f, 18.f), 0.7f);
        coef(1, 1, clampf(midFreq, 100.f, 8000.f), clampf(midDb, -18.f, 18.f), 0.9f);
        coef(2, 2, 4000.f, clampf(highDb, -18.f, 18.f), 0.7f);
    }

    float eqRun(int ch, float x) {
        for (int b = 0; b < 3; ++b) {
            const float* c = eqC_[b];
            float y = c[0] * x + eqS_[ch][b][0];
            eqS_[ch][b][0] = c[1] * x - c[3] * y + eqS_[ch][b][1];
            eqS_[ch][b][1] = c[2] * x - c[4] * y;
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0;
    float scale_ = 1.f;
    float base_[N] = {};
    std::vector<float> line_[N];
    uint32_t mask_[N] = {};
    uint32_t w_[N] = {};
    float lenCur_[N] = {}, lenTgt_[N] = {};
    float lp1_[N] = {}, lp2_[N] = {}, z_[N] = {}, phase_[N] = {}, rw_[N] = {}, rw1_[N] = {};
    float sP_[N] = {}, cP_[N] = {}, cdK_[N] = {}, sdK_[N] = {}, incKey_ = 0.f;
    bool haveInc_ = false;
    float gLoC_[N] = {}, gMidC_[N] = {}, gHiC_[N] = {}, kLen_[N] = {}, kLo_ = 0.f, kMid_ = 0.f, kHi_ = 0.f, kFz_ = 0.f;
    bool haveG_ = false;
    std::vector<float> apBuf_[N];
    int apLen_[N] = {};
    int apI_[N] = {};
    std::vector<float> inBuf_[4];
    int inI_[4] = {};
    std::vector<float> pd_;
    uint32_t pdMask_ = 0, pdW_ = 0;
    float erDelayL_[ROOMS][K] = {}, erDelayR_[ROOMS][K] = {}, erGainL_[ROOMS][K] = {}, erGainR_[ROOMS][K] = {};
    float erCurL_[K] = {}, erCurR_[K] = {}, erTgtL_[K] = {}, erTgtR_[K] = {};
    float erGCurL_[K] = {}, erGCurR_[K] = {}, erGTgtL_[K] = {}, erGTgtR_[K] = {};
    float l1_[2] = {}, l2_[2] = {}, h1_[2] = {}, h2_[2] = {};
    bool monoOut_ = false;
    float dryS_ = 1.f;
    float mix_ = 0, erG_ = 0, width_ = 1, pdCur_ = 0, lateCur_ = 0, freeze_ = 0, drive_ = 0, duck_ = 0, env_ = 0;
    dspkit::AdaaRational adaa_[N];
    float spring_ = 0, tuned_ = 0, bloom_ = 0, gate_ = 0, vint_ = 0;
    float envF_ = 0, envS_ = 0, gateG_ = 1.f;
    uint32_t sinceOnset_ = 1u << 30, gateTimer_ = 0;
    // springs
    Spring spr_[2];
    // comb
    std::vector<float> comb_[2][NCOMB];
    uint32_t combMask_ = 0, combW_ = 0;
    float combDelay_[NCOMB] = {};
    float combLp_[2][NCOMB] = {};
    // EQ
    float eqC_[3][5] = {};
    float eqS_[2][3][2] = {};
    uint32_t rng_ = 1;
    bool first_ = true;
};

}  // namespace adelverb
