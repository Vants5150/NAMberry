// Conversion from the LV2 control values to the engine parameters.
#pragma once
#include "adelcomp_ctl.hpp"
#include "adelcomp_dsp.hpp"

inline adelcomp::Params paramsFromControls(const float* v) {
    adelcomp::Params q;
    q.inputDb = v[C_INPUT];
    q.outputDb = v[C_OUTPUT];
    q.mix = v[C_MIX] * 0.01f;
    q.type = int(v[C_TYPE] + 0.5f);
    q.thresholdDb = v[C_THRESHOLD];
    q.ratio = v[C_RATIO];
    q.attackMs = v[C_ATTACK];
    q.releaseMs = v[C_RELEASE];
    q.autoRel = v[C_AUTOREL] * 0.01f;
    q.kneeDb = v[C_KNEE];
    q.makeupDb = v[C_MAKEUP];
    q.autoMakeup = v[C_AUTOMAKEUP] > 0.5f;
    q.detector = int(v[C_DETECTOR] + 0.5f);
    q.rmsMs = v[C_RMSWIN];
    q.scHpf = v[C_SCHPF];
    q.scShelfDb = v[C_SCSHELF];
    q.lookMs = v[C_LOOKAHEAD];
    q.sat = v[C_SAT] * 0.01f;
    q.link = v[C_LINK] * 0.01f;
    q.ms = v[C_MSMODE] > 0.5f;
    q.gateThrDb = v[C_GATETHR];
    q.gateRatio = v[C_GATERATIO];
    q.gateRangeDb = v[C_GATERANGE];
    q.gateRelMs = v[C_GATEREL];
    return q;
}
