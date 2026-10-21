// test_ftools.cpp - PC tests of the 10 functions with a switch (features.cpp)
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ihost_shim -I<circle>/include -I. test_ftools.cpp ftools.cpp -o test_ftools
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "ftools.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define CHECKM(c, ...) do { if (!(c)) { printf ("FAILED line %d: %s -- ", __LINE__, #c); printf (__VA_ARGS__); printf ("\n"); fails++; } } while (0)

static const float FS = 48000.0f;

// sends nSamples samples: sine of frequency f and amplitude a (stereo, L = R unless otherwise stated) through ProcessIn and then ProcessOut in blocks of 4 frames; returns L and R at the output
struct Run { std::vector<float> L, R; };
static Run Go (CFeatures &F, float f, float a, unsigned n, float rPhaseShift = 0.0f, bool bIn = true, bool bOut = true, float lAmp = -1.0f)
{
	Run o; o.L.assign (n, 0.0f); o.R.assign (n, 0.0f);
	double ph = 0.0;
	for (unsigned b = 0; b < n / 4; b++)
	{
		float l[4], r[4];
		for (int j = 0; j < 4; j++)
		{
			const float s = (float) sin (ph);
			l[j] = (lAmp >= 0.0f ? lAmp : a) * s;
			r[j] = a * (float) sin (ph + rPhaseShift);
			ph += 6.283185307 * f / FS;
		}
		if (bIn) F.ProcessIn (l, r, 4);
		if (bOut) F.ProcessOut (l, r, 4);
		for (int j = 0; j < 4; j++) { o.L[b * 4 + j] = l[j]; o.R[b * 4 + j] = r[j]; }
	}
	return o;
}
static double Rms (const std::vector<float> &v, unsigned from, unsigned to)
{
	double s = 0; for (unsigned i = from; i < to; i++) s += (double) v[i] * v[i]; return sqrt (s / (to - from));
}

int main ()
{
	// ---- everything off at startup, signal unchanged bit for bit
	{
		CFeatures F (FS);
		for (unsigned i = 0; i < FT_COUNT; i++) CHECK (!F.On (i));
		CHECK (!F.Any ());
		float l[4] = {0.1f, -0.2f, 0.3f, -0.4f}, r[4] = {0.4f, 0.3f, -0.2f, 0.1f};
		float l0[4], r0[4]; memcpy (l0, l, sizeof l); memcpy (r0, r, sizeof r);
		F.ProcessIn (l, r, 4); F.ProcessOut (l, r, 4); F.Tick (1000);
		CHECK (memcmp (l, l0, sizeof l) == 0 && memcmp (r, r0, sizeof r) == 0);
		CHECK (CFeatures::IdByName ("tuner") == FtTuner && CFeatures::IdByName ("nonesiste") == -1 && strcmp (CFeatures::Name (FtAir), "air") == 0);
		CHECK (!F.SetParam ("nonesiste", 1.0f));
	}
	// ---- Hum Killer: 50 Hz and harmonics disappear, 1 kHz passes
	{
		CFeatures F (FS); F.SetOn (FtHum, TRUE);
		Run a = Go (F, 50.0f, 0.3f, 96000), b = Go (F, 150.0f, 0.3f, 96000), c = Go (F, 1000.0f, 0.3f, 48000), d = Go (F, 110.0f, 0.3f, 96000);
		const double g50 = 20 * log10 (Rms (a.L, 80000, 96000) / (0.3 / sqrt (2.0))), g150 = 20 * log10 (Rms (b.L, 80000, 96000) / (0.3 / sqrt (2.0)));
		const double g1k = 20 * log10 (Rms (c.L, 24000, 48000) / (0.3 / sqrt (2.0))), g110 = 20 * log10 (Rms (d.L, 80000, 96000) / (0.3 / sqrt (2.0)));
		CHECKM (g50 < -30 && g150 < -30 && fabs (g1k) < 0.3 && fabs (g110) < 1.5, "hum: 50 Hz %g dB, 150 Hz %g dB, 1 kHz %g dB, 110 Hz %g dB", g50, g150, g1k, g110);
		F.SetParam ("mains", 60.0f); F.SetOn (FtHum, TRUE);
		Run e = Go (F, 60.0f, 0.3f, 96000), f = Go (F, 50.0f, 0.3f, 96000);
		CHECKM (20 * log10 (Rms (e.L, 80000, 96000) / (0.3 / sqrt (2.0))) < -30 && fabs (20 * log10 (Rms (f.L, 80000, 96000) / (0.3 / sqrt (2.0)))) < 3.0, "hum 60 Hz");
	}
	// ---- Rumble Filter
	{
		CFeatures F (FS); F.SetOn (FtRumble, TRUE);
		Run a = Go (F, 20.0f, 0.3f, 96000), b = Go (F, 1000.0f, 0.3f, 48000), c = Go (F, 80.0f, 0.3f, 96000);
		const double g20 = 20 * log10 (Rms (a.L, 60000, 96000) / (0.3 / sqrt (2.0))), g1k = 20 * log10 (Rms (b.L, 24000, 48000) / (0.3 / sqrt (2.0))), g80 = 20 * log10 (Rms (c.L, 60000, 96000) / (0.3 / sqrt (2.0)));
		CHECKM (g20 < -12.0 && fabs (g1k) < 0.2 && g80 > -4.5 && g80 < -2.0, "rumble: 20 Hz %g dB, 80 Hz %g dB (expected about -3), 1 kHz %g dB", g20, g80, g1k);
	}
	// ---- Master Limiter: the peak does not exceed the ceiling; below the ceiling the signal passes
	{
		CFeatures F (FS); F.SetOn (FtLimit, TRUE); F.SetParam ("ceil", -6.0f);
		Run a = Go (F, 220.0f, 1.0f, 48000);
		double mx = 0; for (unsigned i = 0; i < 48000; i++) { mx = fmax (mx, fabs (a.L[i])); mx = fmax (mx, fabs (a.R[i])); }
		CHECKM (mx <= 0.5013 && mx > 0.3, "limiter: peak %g (ceiling 0.501)", mx);
		CHECKM (F.LimiterReductionDb () > 3.0, "limiter: reduction %g dB", F.LimiterReductionDb ());
		CFeatures G (FS); G.SetOn (FtLimit, TRUE); G.SetParam ("ceil", -6.0f);
		Run b = Go (G, 220.0f, 0.1f, 48000);
		CHECK (fabs (Rms (b.L, 4800, 48000) - 0.1 / sqrt (2.0)) < 0.001);
	}
	// ---- Mono Check: L and R become equal to the average
	{
		CFeatures F (FS); F.SetOn (FtMono, TRUE);
		float l[4] = {0.2f, 0.4f, -0.6f, 0.0f}, r[4] = {0.0f, -0.2f, 0.2f, 0.6f};
		F.ProcessOut (l, r, 4);
		CHECK (fabsf (l[0] - 0.1f) < 1e-6f && l[1] == r[1] && fabsf (l[2] + 0.2f) < 1e-6f && l[3] == r[3]);
		// out-of-phase signal: disappears (this is exactly how phase problems show up)
		Run a = Go (F, 440.0f, 0.3f, 48000, 3.14159265f);
		CHECK (Rms (a.L, 4800, 48000) < 0.001);
	}
	// ---- Air Band: +12 dB at 15 kHz, neutral at 200 Hz
	{
		CFeatures F (FS); F.SetOn (FtAir, TRUE); F.SetParam ("airdb", 12.0f);
		Run a = Go (F, 15000.0f, 0.1f, 48000), b = Go (F, 200.0f, 0.1f, 48000);
		const double g15 = 20 * log10 (Rms (a.L, 24000, 48000) / (0.1 / sqrt (2.0))), g200 = 20 * log10 (Rms (b.L, 24000, 48000) / (0.1 / sqrt (2.0)));
		CHECKM (g15 > 9.5 && g15 < 13.5 && fabs (g200) < 0.3, "air: 15 kHz %g dB, 200 Hz %g dB", g15, g200);
	}
	// ---- Console Glue: small signals stay almost linear, large ones are rounded off
	{
		CFeatures F (FS); F.SetOn (FtSat, TRUE); F.SetParam ("satdrive", 50.0f);
		Run a = Go (F, 440.0f, 0.02f, 48000), b = Go (F, 440.0f, 0.9f, 48000);
		const double ra = Rms (a.L, 4800, 48000) / (0.02 / sqrt (2.0)), rb = Rms (b.L, 4800, 48000) / (0.9 / sqrt (2.0));
		CHECKM (ra > 0.99 && rb < 0.85 && rb > 0.2, "sat: small gain %g, large %g", ra, rb);
	}
	// ---- Console Glue at 80%: at a normal output level with the NAM (peak 0.25 = -12 dBFS) it must be audible: at least 3 dB of peak reduction; at 0.02 it stays linear
	{
		CFeatures F (FS); F.SetOn (FtSat, TRUE); F.SetParam ("satdrive", 80.0f);
		Run a = Go (F, 440.0f, 0.25f, 48000), b = Go (F, 440.0f, 0.02f, 48000);
		const double ra = 20 * log10 (Rms (a.L, 4800, 48000) / (0.25 / sqrt (2.0))), rb = 20 * log10 (Rms (b.L, 4800, 48000) / (0.02 / sqrt (2.0)));
		CHECKM (ra < -3.0 && rb > -0.2, "sat 80%%: a -12 dBFS %g dB, a -34 dBFS %g dB", ra, rb);
	}
	// ---- Metronome: one click every 60/bpm seconds, accent on the first of four
	{
		CFeatures F (FS); F.SetOn (FtClick, TRUE); F.SetParam ("bpm", 120.0f); F.SetParam ("clickdb", -12.0f);
		Run a = Go (F, 100.0f, 0.0f, 96000, 0.0f, false, true);
		std::vector<unsigned> starts;
		for (unsigned i = 1; i < 96000; i++) if (fabs (a.L[i]) > 0.01 && fabs (a.L[i - 1]) <= 0.01 && (starts.empty () || i - starts.back () > 2000)) starts.push_back (i);
		CHECKM (starts.size () >= 4, "metronome: %zu clicks in 2 s at 120 bpm (expected 4)", starts.size ());
		if (starts.size () >= 3) CHECKM (starts[2] - starts[1] > 23000 && starts[2] - starts[1] < 25000, "interval %u samples (expected 24000)", starts[2] - starts[1]);
		double pk = 0; for (unsigned i = 0; i < 96000; i++) pk = fmax (pk, fabs (a.L[i]));
		CHECKM (pk > 0.2 && pk < 0.26, "click amplitude %g (expected about 0.25 = -12 dB)", pk);
	}
	// ---- Spectrum (RTA): a 1 kHz sine lights up the right band, at the right level
	{
		CFeatures F (FS); F.SetOn (FtRta, TRUE);
		unsigned nT = 0;
		double ph = 0;
		for (unsigned b = 0; b < 48000 / 4; b++)
		{
			float l[4], r[4];
			for (int j = 0; j < 4; j++) { l[j] = r[j] = 0.5f * (float) sin (ph); ph += 6.283185307 * 1000.0 / FS; }
			F.ProcessOut (l, r, 4);
			if ((b % 1200) == 1199) { nT += 100; F.Tick (nT); }
		}
		unsigned kb = 0; for (unsigned k = 1; k < FT_RTA_BANDS; k++) if (F.Rta ()[k] > F.Rta ()[kb]) kb = k;
		const float fc = CFeatures::RtaCenter (kb);
		CHECKM (fc > 890.0f && fc < 1130.0f, "rta: highest band at %g Hz (about 1 kHz expected)", fc);
		CHECKM (F.Rta ()[kb] > -9.0f && F.Rta ()[kb] < -2.0f, "rta: level %g dB (sine at 0.5 = -6 dB)", F.Rta ()[kb]);
		// the distant bands are far below
		CHECKM (F.Rta ()[0] < F.Rta ()[kb] - 25.0f && F.Rta ()[FT_RTA_BANDS - 1] < F.Rta ()[kb] - 25.0f, "rta: distant bands %g and %g", F.Rta ()[0], F.Rta ()[FT_RTA_BANDS - 1]);
		CHECK (fabsf (CFeatures::RtaCenter (0) - 63.0f) < 0.5f && CFeatures::RtaCenter (23) > 12000.0f && CFeatures::RtaCenter (23) < 13500.0f);
	}
	// ---- Tuner: guitar strings and offsets in cents
	{
		struct { float hz; int midi; const char *name; } T[6] = {{82.41f, 40, "E2"}, {110.0f, 45, "A2"}, {146.83f, 50, "D3"}, {196.0f, 55, "G3"}, {246.94f, 59, "B3"}, {329.63f, 64, "E4"}};
		for (int k = 0; k < 6; k++)
		{
			CFeatures F (FS); F.SetOn (FtTuner, TRUE); F.SetParam ("tmute", 0.0f);
			// the guitar has harmonics: fundamental plus second and third
			double ph = 0;
			unsigned nT = 0;
			for (unsigned b = 0; b < 48000 / 4; b++)
			{
				float l[4], r[4];
				for (int j = 0; j < 4; j++)
				{
					const float s = 0.3f * (float) sin (ph) + 0.15f * (float) sin (2 * ph) + 0.1f * (float) sin (3 * ph);
					l[j] = r[j] = s; ph += 6.283185307 * T[k].hz / FS;
				}
				F.ProcessIn (l, r, 4);
				if ((b % 1200) == 1199) { nT += 100; F.Tick (nT); }
			}
			char nm[8]; CFeatures::NoteName (F.TunerMidi (), nm);
			CHECKM (F.TunerValid () && F.TunerMidi () == T[k].midi && fabsf (F.TunerCents ()) < 12.0f && strcmp (nm, T[k].name) == 0,
				"tuner %s (%g Hz): valid %d, note %s (midi %d), cents %g, Hz %g", T[k].name, T[k].hz, F.TunerValid (), nm, F.TunerMidi (), F.TunerCents (), F.TunerHz ());
		}
		// offset: 110 Hz + 30 cents = 111.92 Hz
		CFeatures F (FS); F.SetOn (FtTuner, TRUE);
		double ph = 0; unsigned nT = 0;
		for (unsigned b = 0; b < 48000 / 4; b++)
		{
			float l[4], r[4];
			for (int j = 0; j < 4; j++) { l[j] = r[j] = 0.3f * (float) sin (ph) + 0.1f * (float) sin (2 * ph); ph += 6.283185307 * 111.92 / FS; }
			F.ProcessIn (l, r, 4);
			if ((b % 1200) == 1199) { nT += 100; F.Tick (nT); }
		}
		CHECKM (F.TunerValid () && F.TunerMidi () == 45 && F.TunerCents () > 20.0f && F.TunerCents () < 40.0f, "tuner +30 cents: midi %d cents %g", F.TunerMidi (), F.TunerCents ());
		// silence: no note
		CFeatures G (FS); G.SetOn (FtTuner, TRUE);
		unsigned nS = 0;
		for (unsigned b = 0; b < 48000 / 4; b++) { float l[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0}; G.ProcessIn (l, r, 4); if ((b % 1200) == 1199) { nS += 100; G.Tick (nS); } }
		CHECK (!G.TunerValid ());
		// output muting with the tuner on
		CFeatures H (FS); H.SetOn (FtTuner, TRUE);
		float l[4] = {0.3f, 0.3f, 0.3f, 0.3f}, r[4] = {0.3f, 0.3f, 0.3f, 0.3f};
		H.ProcessOut (l, r, 4);
		CHECK (l[0] == 0.0f && r[3] == 0.0f);
		H.SetParam ("tmute", 0.0f);
		float l2[4] = {0.3f, 0.3f, 0.3f, 0.3f}, r2[4] = {0.3f, 0.3f, 0.3f, 0.3f};
		H.ProcessOut (l2, r2, 4);
		CHECK (l2[0] == 0.3f);
	}
	// ---- Meters: loudness, peak, crest, correlation
	{
		// 1 kHz sine of amplitude 0.1 (-20 dBFS) on both channels: about -20 LUFS
		CFeatures F (FS); F.SetOn (FtMeter, TRUE);
		unsigned nT = 0;
		double ph = 0;
		for (unsigned b = 0; b < 96000 / 4; b++)
		{
			float l[4], r[4];
			for (int j = 0; j < 4; j++) { l[j] = r[j] = 0.1f * (float) sin (ph); ph += 6.283185307 * 1000.0 / FS; }
			F.ProcessOut (l, r, 4);
			if ((b % 1200) == 1199) { nT += 100; F.Tick (nT); }
		}
		CHECKM (fabs (F.LufsMomentary () + 20.0f) < 1.0f, "meter: %g LUFS (expected about -20)", F.LufsMomentary ());
		CHECKM (fabs (F.PeakDb () + 20.0f) < 0.7f, "meter: peak %g dB (expected -20)", F.PeakDb ());
		CHECKM (fabs (F.CrestDb () - 3.0f) < 1.0f, "meter: crest %g dB (3 expected for a sine)", F.CrestDb ());
		CHECKM (F.Correlation () > 0.98f, "meter: correlation %g (identical = 1)", F.Correlation ());
		// L = -R
		CFeatures G (FS); G.SetOn (FtMeter, TRUE);
		nT = 0; ph = 0;
		for (unsigned b = 0; b < 96000 / 4; b++)
		{
			float l[4], r[4];
			for (int j = 0; j < 4; j++) { l[j] = 0.1f * (float) sin (ph); r[j] = -l[j]; ph += 6.283185307 * 1000.0 / FS; }
			G.ProcessOut (l, r, 4);
			if ((b % 1200) == 1199) { nT += 100; G.Tick (nT); }
		}
		CHECKM (G.Correlation () < -0.98f, "meter: out-of-phase correlation %g", G.Correlation ());
		// silence: finite and very low values
		CFeatures H (FS); H.SetOn (FtMeter, TRUE);
		nT = 0;
		for (unsigned b = 0; b < 48000 / 4; b++) { float l[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0}; H.ProcessOut (l, r, 4); if ((b % 1200) == 1199) { nT += 100; H.Tick (nT); } }
		CHECK (H.LufsMomentary () < -100.0f && H.PeakDb () < -100.0f && std::isfinite (H.Correlation ()) && std::isfinite (H.CrestDb ()));
	}
	// ---- all on together, strong random signal: no non-finite values
	{
		CFeatures F (FS);
		for (unsigned i = 0; i < FT_COUNT; i++) F.SetOn (i, TRUE);
		F.SetParam ("tmute", 0.0f);
		unsigned seed = 7, nT = 0;
		bool ok = true;
		for (unsigned b = 0; b < 48000 / 4; b++)
		{
			float l[4], r[4];
			for (int j = 0; j < 4; j++) { seed = seed * 1664525u + 1013904223u; l[j] = ((int) (seed >> 8) / 8388608.0f - 1.0f); seed = seed * 1664525u + 1013904223u; r[j] = ((int) (seed >> 8) / 8388608.0f - 1.0f); }
			F.ProcessIn (l, r, 4);
			F.ProcessOut (l, r, 4);
			for (int j = 0; j < 4; j++) if (!std::isfinite (l[j]) || !std::isfinite (r[j]) || fabsf (l[j]) > 2.0f || fabsf (r[j]) > 2.0f) ok = false;
			if ((b % 1200) == 1199) { nT += 100; F.Tick (nT); }
		}
		CHECK (ok);
		for (unsigned k = 0; k < FT_RTA_BANDS; k++) CHECK (std::isfinite (F.Rta ()[k]));
	}
	// ---- saving: the parameters and the signal functions are kept, metronome/analysis/tuner restart off
	{
		CFeatures F (FS);
		F.SetOn (FtHum, TRUE); F.SetOn (FtAir, TRUE); F.SetOn (FtClick, TRUE); F.SetOn (FtTuner, TRUE); F.SetOn (FtRta, TRUE);
		F.SetParam ("mains", 60.0f); F.SetParam ("bpm", 133.0f); F.SetParam ("clickdb", -18.0f); F.SetParam ("airdb", 7.0f); F.SetParam ("satdrive", 65.0f); F.SetParam ("ceil", -3.0f); F.SetParam ("tmute", 0.0f);
		char buf[256];
		const unsigned n = F.Serialize (buf, sizeof buf);
		CHECK (n > 20 && n < 200);
		CFeatures G (FS);
		G.Parse (buf);
		CHECK (G.On (FtHum) && G.On (FtAir) && !G.On (FtClick) && !G.On (FtTuner) && !G.On (FtRta) && !G.On (FtRumble));
		CHECK (G.Param ("mains") == 60.0f && G.Param ("bpm") == 133.0f && G.Param ("clickdb") == -18.0f && G.Param ("airdb") == 7.0f && G.Param ("satdrive") == 65.0f && G.Param ("ceil") == -3.0f && G.Param ("tmute") == 0.0f);
		// corrupted text: it does not blow up, it turns nothing on
		CFeatures H (FS);
		H.Parse ("on=\nxx\n=5\nbpm=\nbpm=9999999999999\nmains=-3\nclickdb=zzz\non=1111111111111111111\n");
		CHECK (H.Param ("bpm") >= 40.0f && H.Param ("bpm") <= 240.0f);
		char nm[8]; CFeatures::NoteName (45, nm); CHECK (strcmp (nm, "A2") == 0);
		CFeatures::NoteName (61, nm); CHECK (strcmp (nm, "C#4") == 0);
		CFeatures::NoteName (11, nm); CHECK (strcmp (nm, "B-1") == 0);
	}

	printf ("\nRISULTATO: %s\n", fails ? "tests failed" : "all tests passed");
	return fails ? 1 : 0;
}
