//
// fancurve.h - fan speed curve (percentage) as a function of temperature: arithmetic only, tested on Linux (test_fancurve.cpp)
//
#ifndef _fancurve_h
#define _fancurve_h

#include "config.h"

// nTempMc: thousandths of a degree; nOnC: turn-on threshold in degrees; nMaxPct: maximum speed in the quiet part (up to FAN_CAP_AT_C degrees).
// FAN_MIN_PCT at the turn-on threshold, rises linearly up to nMaxPct at FAN_CAP_AT_C, then rises linearly up to 100% at FAN_FULL_C (protection).
static inline unsigned FanCurvePct (unsigned nTempMc, unsigned nOnC, unsigned nMaxPct)
{
	const unsigned on = nOnC * 1000, cap = FAN_CAP_AT_C * 1000, full = FAN_FULL_C * 1000;
	if (nMaxPct < FAN_MIN_PCT)
	{
		nMaxPct = FAN_MIN_PCT;
	}
	if (nMaxPct > 100)
	{
		nMaxPct = 100;
	}
	if (nTempMc >= full)
	{
		return 100;
	}
	if (nTempMc >= cap)
	{
		return nMaxPct + (100 - nMaxPct) * (nTempMc - cap) / (full - cap);
	}
	if (nTempMc <= on)
	{
		return FAN_MIN_PCT;
	}
	if (cap <= on)
	{
		return nMaxPct;
	}
	return FAN_MIN_PCT + (nMaxPct - FAN_MIN_PCT) * (nTempMc - on) / (cap - on);
}

#endif
