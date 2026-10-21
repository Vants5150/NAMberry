// test_namcal.cpp - Linux test of the NAM input calibration (namcal.h)
#include "namcal.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
static void check (bool c, const char *m)
{
	if (!c) { printf ("FAILED: %s\n", m); fails++; }
	else printf ("ok       %s\n", m);
}
static bool near (float a, float b) { return std::fabs (a - b) < 1e-4f; }

int main ()
{
	// today's values: Pisound +1.5 dBu, A2 models with 15 dBu send, +6 offset = -7.5 dB (what is read in the log)
	check (near (NamInputDb (1.5f, 15.0f, true, 6.0f), -7.5f), "Pisound 1.5 dBu, send 15 dBu, offset +6: -7.5 dB (as today)");
	check (near (NamInputDb (1.5f, 15.0f, true, 0.0f), -13.5f), "same model without the offset (as in the guide): -13.5 dB");
	check (near (NamInputDb (1.5f, 1.5f, true, 0.0f), 0.0f), "send equal to the interface level: 0 dB");
	check (near (NamInputDb (1.5f, 15.0f, false, 6.0f), 0.0f), "model without a send level: 0 dB, the offset does not matter");
	// Pisound Gain raised by 10 dB: 0 dBFS corresponds to 10 dB less -> the value to enter drops by 10 and the input drops by 10
	check (near (NamInputDb (1.5f - 10.0f, 15.0f, true, 0.0f) - NamInputDb (1.5f, 15.0f, true, 0.0f), -10.0f), "Gain +10 dB: input 10 dB lower for the same model");
	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
