// test_fancurve.cpp - Linux test of the fan curve (fancurve.h)
#include "fancurve.h"
#include <cstdio>

static int fails = 0;
static void check (bool c, const char *m)
{
	if (!c) { printf ("FAILED: %s\n", m); fails++; }
	else printf ("ok       %s\n", m);
}

int main ()
{
	// turn-on threshold 55, maximum 55%
	check (FanCurvePct (55000, 55, 55) == FAN_MIN_PCT, "at the turn-on threshold: minimum speed");
	check (FanCurvePct (50000, 55, 55) == FAN_MIN_PCT, "below the threshold (hysteresis): minimum speed, never zero");
	check (FanCurvePct (68000, 55, 55) == 55, "at FAN_CAP_AT_C degrees: the chosen maximum");
	check (FanCurvePct (75000, 55, 55) == 100 && FanCurvePct (90000, 55, 55) == 100, "from FAN_FULL_C degrees: 100%");
	unsigned prev = 0;
	bool mono = true, inrange = true;
	for (unsigned t = 30000; t <= 90000; t += 100)
	{
		unsigned p = FanCurvePct (t, 55, 55);
		if (p < prev) mono = false;
		if (p < FAN_MIN_PCT || p > 100) inrange = false;
		prev = p;
	}
	check (mono, "the speed never drops as the temperature rises");
	check (inrange, "always between the minimum and 100%");
	check (FanCurvePct (61500, 55, 55) > FAN_MIN_PCT && FanCurvePct (61500, 55, 55) < 55, "halfway through the quiet part: between minimum and maximum");
	unsigned q = FanCurvePct (71500, 55, 55);
	check (q > 55 && q < 100, "between FAN_CAP_AT_C and FAN_FULL_C: rises towards 100%");
	// maximum of 100% = full speed already at 68
	check (FanCurvePct (68000, 55, 100) == 100, "maximum 100%: full speed at FAN_CAP_AT_C");
	// a maximum chosen out of range is brought back within the limits
	check (FanCurvePct (68000, 55, 10) == FAN_MIN_PCT, "maximum too low: brought to the minimum");
	// turn-on threshold beyond FAN_CAP_AT_C (70 degrees): above the threshold the maximum
	check (FanCurvePct (71000, 70, 55) >= 55, "turn-on at 70 degrees: beyond FAN_CAP_AT_C the maximum is respected");
	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
