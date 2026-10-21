// Conversion from the LV2 control values (as on the knobs) to the engine parameters. Used by the plugin, the test bench and the test host.
#pragma once
#include "adelverb_dsp.hpp"
#include "adelverb_ports.hpp"

inline adelverb::Params paramsFromControls(const float* v) {
    adelverb::Params q;
    q.mix = v[P_MIX] * 0.01f;
    q.mixMode = int(v[P_MIXMODE] + 0.5f);
    q.wetOnly = v[P_WETONLY] > 0.5f;
    q.outLevelDb = v[P_OUTLEVEL];
    q.predelayMs = v[P_PREDELAY];
    q.decay = v[P_DECAY];
    q.size = v[P_SIZE];
    q.shape = int(v[P_SHAPE] + 0.5f);
    q.distance = v[P_DISTANCE] * 0.01f;
    q.lateDelayMs = v[P_LATE];
    q.er = v[P_ER] * 0.01f;
    q.diffusion = v[P_DIFFUSION] * 0.01f;
    q.bass = v[P_BASS];
    q.bassXover = v[P_BASSX];
    q.treble = v[P_TREBLE];
    q.trebleXover = v[P_TREBLEX];
    q.modDepth = v[P_MODDEPTH];
    q.modRate = v[P_MODRATE];
    q.width = v[P_WIDTH] * 0.01f;
    q.lowCut = v[P_LOWCUT];
    q.highCut = v[P_HIGHCUT];
    q.eqLow = v[P_EQLOW];
    q.eqMidFreq = v[P_EQMIDF];
    q.eqMid = v[P_EQMID];
    q.eqHigh = v[P_EQHIGH];
    q.spring = v[P_SPRING] * 0.01f;
    q.tuned = v[P_TUNED] * 0.01f;
    q.tunedRoot = int(v[P_TUNEDROOT] + 0.5f);
    q.tunedChord = int(v[P_TUNEDCHORD] + 0.5f);
    q.bloom = v[P_BLOOM] * 0.01f;
    q.bloomMs = v[P_BLOOMTIME];
    q.gate = v[P_GATE] * 0.01f;
    q.gateHoldMs = v[P_GATEHOLD];
    q.gateRelMs = v[P_GATEREL];
    q.duck = v[P_DUCK] * 0.01f;
    q.duckRelease = v[P_DUCKREL];
    q.vintage = v[P_VINTAGE] * 0.01f;
    q.drive = v[P_DRIVE] * 0.01f;
    q.freeze = v[P_FREEZE];
    q.morph = v[P_MORPH] * 0.01f;
    q.morphTarget = int(v[P_MORPHTARGET] + 0.5f);
    return q;
}
