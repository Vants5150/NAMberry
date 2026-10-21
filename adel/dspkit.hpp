// DSP tools shared by the ADEL plugins: 4x linear-phase oversampling, 8-point Lagrange interpolation,
// first-order "ADAA" anti-aliasing (Parker, Zavalishin, Le Bivic 2016) for latency-free saturators.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace dspkit {

// ---- 4x oversampling with linear-phase Kaiser FIR. Total latency (up + down) = exactly 27 samples at the base rate ----
class Os4 {
public:
    static constexpr int T = 112;   // length of the 4x FIR
    static constexpr int P = T / 4; // coefficients per phase
    static constexpr int LATENCY = 27;

    Os4() { design(); clear(); }

    void clear() {
        for (int i = 0; i < 32; ++i) x_[i] = 0.f;
        for (int i = 0; i < 128; ++i) z_[i] = 0.f;
        px_ = pz_ = 0;
    }

    // nonlin: callable float -> float object applied to the 4 oversampled samples
    template <class F>
    float process(float in, F nonlin) {
        px_ = (px_ + 1) & 31;
        x_[px_] = in;
        for (int p = 0; p < 4; ++p) {
            float y = 0.f;
            for (int j = 0; j < P; ++j) y += h_[4 * j + p] * x_[(px_ - j) & 31];
            pz_ = (pz_ + 1) & 127;
            z_[pz_] = nonlin(4.f * y);
        }
        float out = 0.f;
        for (int k = 0; k < T; ++k) out += h_[k] * z_[(pz_ - k) & 127];
        return out;
    }

private:
    static double bessel0(double x) {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 40; ++k) {
            t *= (x / (2.0 * k)) * (x / (2.0 * k));
            s += t;
        }
        return s;
    }

    void design() {
        const double beta = 7.5, fc = 0.125;  // cutoff at 24 kHz with 4x frequency = 192 kHz
        const double c = (T - 1) / 2.0;
        double sum = 0.0;
        for (int n = 0; n < T; ++n) {
            double m = n - c;
            double sinc = std::fabs(m) < 1e-9 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * m) / (M_PI * m);
            double r = 2.0 * n / (T - 1) - 1.0;
            double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel0(beta);
            double v = sinc * w;
            tmp_[n] = v;
            sum += v;
        }
        for (int n = 0; n < T; ++n) h_[n] = float(tmp_[n] / sum);
    }

    float h_[T];
    double tmp_[T];
    float x_[32];
    float z_[128];
    uint32_t px_ = 0, pz_ = 0;
};

// ---- 8-point Lagrange read from a circular buffer: reads the sample written "dly" samples ago (dly >= 5) ----
static inline float readLagrange8(const float* b, uint32_t mask, uint32_t w, float dly) {
    static const float invD[8] = {-1.f / 5040.f, 1.f / 720.f, -1.f / 240.f, 1.f / 144.f,
                                  -1.f / 144.f, 1.f / 240.f, -1.f / 720.f, 1.f / 5040.f};
    float dfl = std::floor(dly);
    float t = 1.f - (dly - dfl);
    uint32_t i = w - uint32_t(int32_t(dfl)) - 1u;
    float y[8];
    for (int m = 0; m < 8; ++m) y[m] = b[(i + uint32_t(m) - 3u) & mask];
    float d[8];
    for (int m = 0; m < 8; ++m) d[m] = t - float(m - 3);
    float pre[9], suf[9];
    pre[0] = 1.f;
    for (int m = 0; m < 8; ++m) pre[m + 1] = pre[m] * d[m];
    suf[8] = 1.f;
    for (int m = 7; m >= 0; --m) suf[m] = suf[m + 1] * d[m];
    float out = 0.f;
    for (int j = 0; j < 8; ++j) out += y[j] * pre[j] * suf[j + 1] * invD[j];
    return out;
}

// ---- first-order ADAA: y = (F(x) - F(x_prev)) / (x - x_prev), with F the antiderivative of the saturator.
// The parameters may vary slowly: F(x_prev) is recomputed with the current parameters (no spikes). ----
static inline double lncosh(double x) {
    double a = std::fabs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
}

// saturator y = (tanh(k(x+b)) - tanh(k b)) / k
struct AdaaTanh {
    double xprev = 0.0;
    bool init = false;
    static inline double F(double x, double k, double bias) {
        return lncosh(k * (x + bias)) / (k * k) - std::tanh(k * bias) / k * x;
    }
    static inline double f(double x, double k, double bias) { return (std::tanh(k * (x + bias)) - std::tanh(k * bias)) / k; }
    inline float process(float xin, double k, double bias) {
        double x = xin, y;
        if (!init) { y = f(x, k, bias); init = true; }
        else {
            double dx = x - xprev;
            if (std::fabs(dx) < 1e-6) y = f(0.5 * (x + xprev), k, bias);
            else y = (F(x, k, bias) - F(xprev, k, bias)) / dx;
        }
        xprev = x;
        return float(y);
    }
    void reset() { init = false; xprev = 0.0; }
};

// rational saturator y = x / (1 + a|x|)
struct AdaaRational {
    double xprev = 0.0;
    bool init = false;
    static inline double F(double x, double a) {
        double ax = std::fabs(x);
        return a > 1e-6 ? ax / a - std::log1p(a * ax) / (a * a) : 0.5 * x * x;
    }
    static inline double f(double x, double a) { return x / (1.0 + a * std::fabs(x)); }
    inline float process(float xin, double a) {
        double x = xin, y;
        if (!init) { y = f(x, a); init = true; }
        else {
            double dx = x - xprev;
            if (std::fabs(dx) < 1e-6) y = f(0.5 * (x + xprev), a);
            else y = (F(x, a) - F(xprev, a)) / dx;
        }
        xprev = x;
        return float(y);
    }
    void reset() { init = false; xprev = 0.0; }
};

}  // namespace dspkit
