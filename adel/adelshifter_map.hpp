// Conversion from the LV2 control values to the engine parameters.
#pragma once
#include "adelshifter_ctl.hpp"
#include "adelshifter_dsp.hpp"

inline adelshifter::Params paramsFromControls(const float* v) {
    adelshifter::Params q;
    q.dryDb = v[C_DRY];
    q.levelDb = v[C_LEVEL];
    q.key = int(v[C_KEY] + 0.5f);
    q.scale = int(v[C_SCALE] + 0.5f);
    q.glideMs = v[C_GLIDE];
    q.tracking = int(v[C_TRACKING] + 0.5f);
    q.gateDb = v[C_GATE];
    q.lowHz = v[C_LOWHZ];
    for (int i = 0; i < 3; ++i) {
        const int b = C_V1ON + 8 * i;
        adelshifter::VoiceParams& vp = q.v[i];
        vp.on = v[b + 0] > 0.5f;
        vp.mode = int(v[b + 1] + 0.5f);
        vp.steps = int(std::lround(v[b + 2]));
        vp.semi = v[b + 3];
        vp.fineCents = v[b + 4];
        vp.levelDb = v[b + 5];
        vp.pan = v[b + 6] * 0.01f;
        vp.delayMs = v[b + 7];
    }
    q.whammy = v[C_WHAMMY];
    q.whammyDry = v[C_WHAMMYDRY] > 0.5f;
    q.driftCents = v[C_DRIFT];
    return q;
}
