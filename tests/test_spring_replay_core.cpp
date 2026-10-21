// test_spring_replay_core.cpp - ADELspring engine compiled as on the board (fxshim/fxmath math, -O3) with C functions to replay on the PC a state frozen on the board
// g++ -std=c++17 -O3 -I. -Ifxshim -Iadel -fno-threadsafe-statics -nostdinc++ -c test_spring_replay_core.cpp
#define ADELSPRING_DEBUG 1
#include "adelspring_map.hpp"

extern "C" {

void *rp_new (float fs)
{
	adelspring::Tank *p = new adelspring::Tank;
	p->init ((double) fs);
	p->setMonoOut (false);
	return p;
}

unsigned rp_state_floats (void)
{
	return (unsigned) adelspring::Tank::stateFloats ();
}

void rp_import (void *p, const float *pState)
{
	((adelspring::Tank *) p)->importState (pState);
}

// n samples (<= 64); params = the 14 controls
void rp_process (void *p, const float *pIn, float *pL, float *pR, unsigned n, const float *pParams)
{
	((adelspring::Tank *) p)->process (pIn, pL, pR, n, paramsFromControls (pParams));
}

void rp_export_dbg (void *p, float *pOut)
{
	((adelspring::Tank *) p)->exportDbg (pOut);
}

void rp_export (void *p, float *pOut)
{
	((adelspring::Tank *) p)->exportState (pOut);
}

float rp_max_out_v (void *p)
{
	return ((adelspring::Tank *) p)->dbg.maxOutV;
}

float rp_events (void *p)
{
	return ((adelspring::Tank *) p)->dbg.events;
}

}
