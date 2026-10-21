// Conversion from the LV2 control values to the engine parameters.
#pragma once
#include "adelsendsweep_ctl.hpp"
#include "adelsendsweep_dsp.hpp"

inline adelsendsweep::Params paramsFromControls(const float* v) {
    adelsendsweep::Params q;
    q.trigger = v[C_PLAY] > 0.5f;
    q.signal = int(v[C_SIGNAL] + 0.5f);
    q.levelDb = v[C_LEVEL];
    q.mute = v[C_MUTE] > 0.5f;
    return q;
}
