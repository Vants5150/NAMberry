// Circle version of adel_probe_shm.hpp: on PiPedal the ADELSendSweep probe talks to ADELphase through the operating system's shared
// memory. On Circle there is no probe: the structure exists but "playing" always stays 0
// (the stereo version of ADELphase, the one used here, measures the input by itself anyway).
#pragma once
#include <atomic>
#include <cstdint>

namespace adelprobe_shm {

struct Shared {
    std::atomic<uint32_t> playing;
    std::atomic<uint32_t> beat;
};

inline Shared* get() {
    static Shared s;
    return &s;
}

}  // namespace adelprobe_shm
