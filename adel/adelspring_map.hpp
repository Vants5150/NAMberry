// Conversion from the LV2 control values to the engine parameters.
#pragma once
#include "adelspring_ctl.hpp"
#include "adelspring_dsp.hpp"

inline adelspring::Params paramsFromControls(const float* v) {
    adelspring::Params q;
    q.mix = v[C_MIX] * 0.01f;
    q.mixMode = int(v[C_MIXMODE] + 0.5f);
    q.wetOnly = v[C_WETONLY] > 0.5f;
    q.outLevelDb = v[C_OUTLEVEL];
    q.tank = int(v[C_TANK] + 0.5f);
    q.springs = int(v[C_SPRINGS] + 0.5f) + 1;
    q.decay = v[C_DECAY];
    q.drip = v[C_DRIP];
    q.damp = v[C_DAMP];
    q.dwell = v[C_DWELL];
    q.shake = v[C_SHAKE] > 0.5f;
    q.lowCut = v[C_LOWCUT];
    q.highCut = v[C_HIGHCUT];
    q.wobble = v[C_WOBBLE];
    return q;
}
