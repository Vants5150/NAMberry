// test_delay_tap.cpp - Follow tap tempo in every delay: with the switch off the delay keeps its time whatever the master tempo is; with it on the
// quarter note of the master tempo (with the block's subdivision) sets the time; a change of tempo moves the delay; without a tempo the manual time applies.
// Run with tests/run_host_tests.sh
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "fx.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)
#define CHECKF(c, ...) do { char b_[260]; snprintf (b_, sizeof b_, __VA_ARGS__); CHECK (c, b_); } while (0)

static int Idx (unsigned nType, const char *pKey)
{
	const TFxDef *d = FxDefByType (nType);
	for (unsigned i = 0; d && i < d->nParams; i++) if (strcmp (d->pParams[i].Key, pKey) == 0) return (int) i;
	return -1;
}

struct Case { unsigned type; const char *name; const char *timeKey; float manual; const char *fbKey; const char *syncKey; };

// time (ms) of the first repeat of an impulse, with the given tempo, follow switch and subdivision; params set after the defaults
static double FirstEcho (const Case &c, float bpm, float follow, float sync, float fs = 48000.0f)
{
	float P[FX_MAX_P];
	const TFxDef *d = FxDefByType (c.type);
	for (unsigned i = 0; i < FX_MAX_P; i++) P[i] = i < d->nParams ? d->pParams[i].Def : 0.0f;
	P[Idx (c.type, c.timeKey)] = c.manual;
	if (c.fbKey) P[Idx (c.type, c.fbKey)] = 0.0f;
	const int iw = Idx (c.type, "wetonly"); if (iw >= 0) P[iw] = 1.0f;
	const int im = Idx (c.type, "mix"); if (im >= 0 && c.type != FxAdelDelay && c.type != FxAdelDelayM) P[im] = 100.0f;
	if (c.syncKey) P[Idx (c.type, c.syncKey)] = sync;
	const int ifo = Idx (c.type, c.type == FxAdelDelay || c.type == FxAdelDelayM ? "tapfollow" : "follow");
	if (ifo < 0) { printf ("FAILED: %s has no follow switch\n", c.name); fails++; return -2.0; }
	P[ifo] = follow;
	g_fFxTempoBpm = bpm;
	CFx *fx = FxCreate (c.type, fs);
	std::vector<float> out;
	const unsigned total = (unsigned) (fs * 2.5f);
	float L[4], R[4];
	int mono = 1;
	for (unsigned b = 0; b < total / 4; b++)
	{
		const unsigned pos = b * 4;
		for (int j = 0; j < 4; j++) { L[j] = (pos + j == (unsigned) (fs * 1.2f)) ? 0.5f : 0.0f; R[j] = L[j]; }
		mono = 1;
		fx->Process (L, R, 4, &mono, P);
		for (int j = 0; j < 4; j++) out.push_back(L[j]);
	}
	delete fx;
	const unsigned t0 = (unsigned) (fs * 1.2f);
	double best = 0; unsigned bi = 0;
	for (unsigned i = t0 + (unsigned) (fs * 0.015f); i < out.size (); i++) { const double a = fabs ((double) out[i]); if (a > best) { best = a; bi = i; } }
	return best < 1e-5 ? -1.0 : (double) (bi - t0) * 1000.0 / fs;
}

int main (void)
{
	const Case cases[] =
	{
		{FxAdelDelay, "ADELdelay", "time", 300.0f, "feedback", 0},
		{FxAdelDelayM, "ADELdelay mono", "time", 300.0f, "feedback", 0},
		{FxTapeEcho, "Tape Echo", "time", 300.0f, "repeats", "sync"},
		{FxAnalog, "Analog Delay", "time", 300.0f, "repeats", "sync"},
		{FxDuck, "Duck Delay", "time", 300.0f, "repeats", "sync"},
	};
	for (const Case &c : cases)
	{
		const double manual = FirstEcho (c, 0.0f, 0.0f, 0.0f);
		CHECKF (fabs (manual - 300.0) < 4.0, "%s: no tempo and switch off: first repeat at %.1f ms (set 300)", c.name, manual);
		const double offFast = FirstEcho (c, 150.0f, 0.0f, 3.0f);
		CHECKF (fabs (offFast - 300.0) < 4.0, "%s: switch off with a master tempo of 150 bpm: still %.1f ms", c.name, offFast);
		const double noTempo = FirstEcho (c, 0.0f, 1.0f, 0.0f);
		CHECKF (fabs (noTempo - 300.0) < 4.0, "%s: switch on without a master tempo: the manual %.1f ms applies", c.name, noTempo);
		const double q120 = FirstEcho (c, 120.0f, 1.0f, 0.0f);
		CHECKF (fabs (q120 - 500.0) < 5.0, "%s: switch on at 120 bpm: %.1f ms (quarter note 500)", c.name, q120);
		const double q150 = FirstEcho (c, 150.0f, 1.0f, 0.0f);
		CHECKF (fabs (q150 - 400.0) < 5.0, "%s: switch on at 150 bpm: %.1f ms (quarter note 400)", c.name, q150);
		if (c.syncKey)
		{
			const double e8 = FirstEcho (c, 120.0f, 1.0f, 3.0f);
			CHECKF (fabs (e8 - 250.0) < 5.0, "%s: switch on, subdivision 1/8 at 120 bpm: %.1f ms", c.name, e8);
		}
	}
	// ADELdelay: Rhythm applies to the followed quarter note (1/8 = half of it)
	{
		const Case &c = cases[0];
		float P[FX_MAX_P];
		const TFxDef *d = FxDefByType (c.type);
		for (unsigned i = 0; i < FX_MAX_P; i++) P[i] = i < d->nParams ? d->pParams[i].Def : 0.0f;
		P[Idx (c.type, "feedback")] = 0.0f; P[Idx (c.type, "wetonly")] = 1.0f; P[Idx (c.type, "tapfollow")] = 1.0f; P[Idx (c.type, "rhythm")] = 2.0f;		// 1/8
		g_fFxTempoBpm = 120.0f;
		CFx *fx = FxCreate (c.type, 48000.0f);
		std::vector<float> out; float L[4], R[4]; int mono;
		for (unsigned b = 0; b < 48000 * 25 / 40; b++) { for (int j = 0; j < 4; j++) { L[j] = (b * 4 + j == 57600) ? 0.5f : 0.0f; R[j] = L[j]; } mono = 1; fx->Process (L, R, 4, &mono, P); for (int j = 0; j < 4; j++) out.push_back (L[j]); }
		delete fx;
		double best = 0; unsigned bi = 0;
		for (unsigned i = 57600 + 720; i < out.size (); i++) if (fabs (out[i]) > best) { best = fabs (out[i]); bi = i; }
		CHECKF (fabs ((double) (bi - 57600) / 48.0 - 250.0) < 5.0, "ADELdelay: Rhythm 1/8 on the followed tempo (120 bpm): %.1f ms", (double) (bi - 57600) / 48.0);
	}
	// Analog AD9: the quarter note, halved to fit its range (24 to 300 ms)
	{
		const Case a = {FxAd9, "AD9", "delay", 5.0f, "repeat", 0};
		const double man = FirstEcho (a, 0.0f, 0.0f, 0.0f), ign = FirstEcho (a, 150.0f, 0.0f, 0.0f);
		CHECKF (fabs (man - ign) < 1.0, "AD9: switch off: the knob sets the time whatever the tempo (%.1f ms)", man);
		const double f120 = FirstEcho (a, 120.0f, 1.0f, 0.0f), f200 = FirstEcho (a, 200.0f, 1.0f, 0.0f);
		CHECKF (fabs (f120 - 250.0) < 20.0, "AD9: 120 bpm: the quarter note (500 ms) is halved to %.1f ms (250)", f120);
		CHECKF (fabs (f200 - 300.0) < 20.0, "AD9: 200 bpm: the quarter note %.1f ms is inside the range (300)", f200);
	}
	// every delay block has the switch, in the table, defaulting to off
	for (unsigned t : {(unsigned) FxAdelDelay, (unsigned) FxAdelDelayM, (unsigned) FxTapeEcho, (unsigned) FxAnalog, (unsigned) FxDuck, (unsigned) FxAd9})
	{
		const int i = Idx (t, (t == FxAdelDelay || t == FxAdelDelayM) ? "tapfollow" : "follow");
		const TFxDef *d = FxDefByType (t);
		CHECKF (i >= 0 && d->pParams[i].Kind == FxKindToggle && d->pParams[i].Def == 0.0f && i == (int) d->nParams - 1, "%s: Follow tap tempo is the last parameter, a switch, off by default", d->Title);
	}
	g_fFxTempoBpm = 0.0f;
	printf ("\nRESULT: %s (%d failures)\n", fails ? "FAILED" : "all ok", fails);
	return fails ? 1 : 0;
}
