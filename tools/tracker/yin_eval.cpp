// yin_eval.cpp - runs the ADELshifter pitch detector (adelshifter::Yin) over a recorded DI WAV (24-bit stereo) and prints what it reports.
// usage: yin_eval file.wav [channel 0|1] [tracking 0|1|2] [lowHz] [gate_dBFS]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "adelshifter_dsp.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const int chan = argc > 2 ? atoi(argv[2]) : 0;
    const int tracking = argc > 3 ? atoi(argv[3]) : 1;
    const float lowHz = argc > 4 ? (float) atof(argv[4]) : 70.f;
    const float gateDb = argc > 5 ? (float) atof(argv[5]) : -55.f;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 44, SEEK_SET);
    std::vector<unsigned char> b(n - 44);
    if (fread(b.data(), 1, b.size(), f) != b.size()) return 3;
    fclose(f);
    const size_t frames = b.size() / 6;
    std::vector<float> x(frames);
    for (size_t i = 0; i < frames; i++) {
        const unsigned char* p = &b[i * 6 + 3 * chan];
        int v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000) v -= 1 << 24;
        x[i] = (float) v / 8388608.f;
    }
    adelshifter::Yin yin;
    yin.init(48000.0);
    const float gateLin = std::pow(10.f, gateDb / 20.f);
    // log one line per 100 ms: time, voiced, hz, confidence, rms
    size_t nextLog = 0;
    int analyses = 0, voicedN = 0;
    double sumHz = 0;
    for (size_t i = 0; i < frames; i++) {
        bool done = yin.push(x[i], lowHz, gateLin, tracking);
        if (done) analyses++;
        if (done) {
            if (yin.voiced()) voicedN++;
            printf("%.4f %d %.3f %.3f %.6f\n", i / 48000.0, yin.voiced() ? 1 : 0, yin.hz(), yin.confidence(), yin.rms());
        }
    }
    fprintf(stderr, "analyses %d, logged voiced %d\n", analyses, voicedN);
    return 0;
}
