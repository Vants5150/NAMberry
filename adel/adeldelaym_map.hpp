// Conversion from the LV2 control values (as on the knobs) to the engine parameters.
#pragma once
#include "adeldelay_ctl.hpp"
#include "adeldelaym_dsp.hpp"

inline adeldelaym::Params paramsFromControlsM(const float* v) {
    adeldelaym::Params q;
    q.mix = v[C_MIX] * 0.01f;
    q.wetOnly = v[C_WETONLY] > 0.5f;
    q.levelDb = v[C_LEVEL];
    q.feedback = v[C_FEEDBACK] * 0.01f;
    q.timeMs = v[C_TIME];
    q.rhythm = int(v[C_RHYTHM] + 0.5f);
    q.glideMs = v[C_GLIDE];
    q.pingpong = v[C_PINGPONG] * 0.01f;
    q.rOffset = v[C_ROFFSET] * 0.01f;
    q.tap2 = v[C_TAP2] * 0.01f;
    q.tap2r = v[C_TAP2R] * 0.01f;
    q.tap3 = v[C_TAP3] * 0.01f;
    q.tap3r = v[C_TAP3R] * 0.01f;
    q.tapSpread = v[C_TAPSPREAD] * 0.01f;
    q.mode = int(v[C_MODE] + 0.5f);
    q.character = v[C_CHARACTER] * 0.01f;
    q.fbLow = v[C_FBLOW];
    q.fbHigh = v[C_FBHIGH];
    q.drive = v[C_DRIVE] * 0.01f;
    q.diffusion = v[C_DIFFUSION] * 0.01f;
    q.reverse = v[C_REVERSE] * 0.01f;
    q.modDepthMs = v[C_MODDEPTH];
    q.modRate = v[C_MODRATE];
    q.wow = v[C_WOW] * 0.01f;
    q.flutter = v[C_FLUTTER] * 0.01f;
    q.tremDepth = v[C_TREMDEPTH] * 0.01f;
    q.tremShape = int(v[C_TREMSHAPE] + 0.5f);
    q.tremDiv = int(v[C_TREMDIV] + 0.5f);
    q.duck = v[C_DUCK] * 0.01f;
    q.duckRelMs = v[C_DUCKREL];
    q.hold = v[C_HOLD];
    return q;
}
