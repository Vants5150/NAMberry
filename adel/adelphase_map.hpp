// Conversion from the LV2 control values to the engine parameters.
#pragma once
#include "adelphase_ctl.hpp"
#include "adelphase_dsp.hpp"

inline adelphase::Params paramsFromControls(const float* v) {
    adelphase::Params q;
    q.invert = v[C_INVERT] > 0.5f;
    q.delayMs = v[C_DELAY];
    q.trimDb = v[C_TRIM];
    q.apStages = int(v[C_APSTAGES] + 0.5f);
    q.apFreq = v[C_APFREQ];
    q.mode = int(v[C_MODE] + 0.5f);
    q.autoAlign = v[C_AUTO] > 0.5f;
    q.role = int(v[C_LANE] + 0.5f);
    return q;
}
