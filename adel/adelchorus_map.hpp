// Conversion from the LV2 control values to the engine parameters.
#pragma once
#include "adelchorus_ctl.hpp"
#include "adelchorus_dsp.hpp"

inline adelchorus::Params paramsFromControls(const float* v) {
    adelchorus::Params q;
    q.model = int(v[C_MODEL] + 0.5f);
    q.rateHz = v[C_RATE];
    q.depth = v[C_DEPTH] * 0.01f;
    q.mix = v[C_MIX] * 0.01f;
    q.levelDb = v[C_LEVEL];
    q.wetOnly = v[C_WETONLY] > 0.5f;
    q.delayMs = v[C_DELAY];
    q.shape = int(v[C_SHAPE] + 0.5f);
    q.spreadDeg = v[C_SPREAD];
    q.width = v[C_WIDTH] * 0.01f;
    q.feedback = v[C_FEEDBACK] * 0.01f;
    q.lowCut = v[C_LOWCUT];
    q.highCut = v[C_HIGHCUT];
    q.character = v[C_CHARACTER] * 0.01f;
    q.noise = v[C_NOISE] * 0.01f;
    return q;
}
