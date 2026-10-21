// ADELSendSweep: test signal generator for measurements (for example with ADELphase Align).
// A button starts a sample: synthetic guitar notes (Karplus-Strong), a chord or a burst of pink noise.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace adelsendsweep {

struct Params {
    bool trigger = false;
    int signal = 0;          // 0 = sweep, 1 = notes, 2 = chord, 3 = pink noise
    float levelDb = -12.f;   // peak of the generated signal
    bool mute = true;        // mutes the input while the sample plays
};

struct Ev {
    float t, f0;
};

static const Ev kNotes[] = {{0.0f, 82.4f}, {0.7f, 110.0f}, {1.4f, 146.8f}, {2.1f, 196.0f},
                            {2.8f, 246.9f}, {3.5f, 329.6f}, {4.2f, 110.0f}, {4.9f, 196.0f}};
static const float kNotesLen = 6.2f;
static const Ev kChord[] = {{0.00f, 82.4f}, {0.025f, 123.5f}, {0.05f, 164.8f}, {0.075f, 207.7f},
                            {1.60f, 82.4f}, {1.625f, 123.5f}, {1.65f, 164.8f}, {1.675f, 207.7f},
                            {3.20f, 110.0f}, {3.225f, 164.8f}, {3.25f, 220.0f}, {3.275f, 277.2f}};
static const float kChordLen = 5.2f;
static const float kNoiseLen = 3.0f;
static const float kSweepP = 0.5f, kSweepF1 = 80.f, kSweepF2 = 8000.f, kSweepLen = 5.0f;

class Probe {
public:
    void init(double rate) {
        fs_ = float(rate);
        clear();
    }
    void clear() {
        for (auto& v : v_) v.on = false;
        playing_ = false;
        prev_ = false;
        pos_ = 0;
        total_ = 1;
        evNext_ = 0;
        seed_ = 12345u;
        b0_ = b1_ = b2_ = 0.f;
    }

    void process(const float* in, float* out, uint32_t n, const Params& q) {
        if (q.trigger && !prev_) start(q.signal);
        prev_ = q.trigger;
        const float g = std::pow(10.f, q.levelDb / 20.f) * 1.5f;     // 1.5: the real Karplus-Strong peaks stay below 0.66
        for (uint32_t i = 0; i < n; ++i) {
            float s = 0.f;
            if (playing_) {
                const float t = float(pos_) / fs_;
                if (sig_ == 0) {
                    s = sweep(t) * (g / 1.5f);
                } else if (sig_ == 3) {
                    s = pink() * noiseFade(t) * g * 0.39f;
                } else {
                    const Ev* ev = sig_ == 1 ? kNotes : kChord;
                    const int ne = sig_ == 1 ? int(sizeof(kNotes) / sizeof(Ev)) : int(sizeof(kChord) / sizeof(Ev));
                    while (evNext_ < ne && t >= ev[evNext_].t) pluck(ev[evNext_++].f0);
                    for (auto& v : v_) {
                        if (!v.on) continue;
                        const int j = v.idx;
                        const int j2 = j + 1 == v.N ? 0 : j + 1;
                        const float y = v.buf[j];
                        v.buf[j] = 0.4985f * (v.buf[j] + v.buf[j2]);
                        v.idx = j2;
                        s += y;
                        if (--v.left <= 0) v.on = false;
                    }
                    s *= g * (sig_ == 1 ? 0.6f : 0.33f);
                }
                if (++pos_ >= total_) playing_ = false;
            }
            out[i] = (playing_ && q.mute ? 0.f : in[i]) + s;
        }
    }

    float progressPct() const { return playing_ ? 100.f * float(pos_) / float(total_) : 0.f; }
    bool playing() const { return playing_; }

private:
    struct Voice {
        bool on = false;
        int N = 0, idx = 0, left = 0;
        float buf[1024];
    };

    inline float rnd() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return float(int32_t(seed_)) * (1.f / 2147483648.f);
    }
    inline float pink() {         // Paul Kellet's filter (economical 3-pole version)
        const float w = rnd();
        b0_ = 0.99765f * b0_ + w * 0.0990460f;
        b1_ = 0.96300f * b1_ + w * 0.2965164f;
        b2_ = 0.57000f * b2_ + w * 1.0526913f;
        return (b0_ + b1_ + b2_ + w * 0.1848f) * 0.25f;
    }
    inline float sweep(float t) const {        // repeated exponential sweep: 80 Hz -> 8 kHz every 0.5 s
        const double u = std::fmod(double(t), double(kSweepP));
        const double L = std::log(double(kSweepF2) / double(kSweepF1));
        const double ph = 6.283185307179586 * double(kSweepF1) * double(kSweepP) / L * (std::exp(u * L / double(kSweepP)) - 1.0);
        const double fd = 0.005;
        const double env = std::min(1.0, std::min(u / fd, (double(kSweepP) - u) / fd));
        return float(std::sin(ph) * std::max(0.0, env));
    }
    inline float noiseFade(float t) const {
        const float f = 0.05f;
        if (t < f) return t / f;
        if (t > kNoiseLen - f) return std::max(0.f, (kNoiseLen - t) / f);
        return 1.f;
    }
    void start(int sig) {
        sig_ = sig < 0 ? 0 : (sig > 3 ? 3 : sig);
        pos_ = 0;
        evNext_ = 0;
        for (auto& v : v_) v.on = false;
        total_ = uint32_t((sig_ == 0 ? kSweepLen : (sig_ == 1 ? kNotesLen : (sig_ == 2 ? kChordLen : kNoiseLen))) * fs_);
        b0_ = b1_ = b2_ = 0.f;
        playing_ = true;
    }
    void pluck(float f0) {
        Voice* v = nullptr;
        for (auto& x : v_)
            if (!x.on) { v = &x; break; }
        if (!v) v = &v_[0];
        v->N = std::max(8, std::min(1000, int(fs_ / f0 + 0.5f)));
        float pk = 1e-6f;
        for (int i = 0; i < v->N; ++i) { v->buf[i] = rnd(); pk = std::max(pk, std::fabs(v->buf[i])); }
        for (int i = 0; i < v->N; ++i) v->buf[i] /= pk;
        v->idx = 0;
        v->left = int(1.1f * fs_);
        v->on = true;
    }

    float fs_ = 48000.f;
    Voice v_[6];
    bool playing_ = false, prev_ = false;
    int sig_ = 0, evNext_ = 0;
    uint32_t pos_ = 0, total_ = 1, seed_ = 12345u;
    float b0_ = 0.f, b1_ = 0.f, b2_ = 0.f;
};

}  // namespace adelsendsweep
