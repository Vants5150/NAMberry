//
// fx_dynacomp.cpp - Dyna Comp Real: MXR 102 Dyna Comp (1972; common-emitter input buffer, CA3080 OTA, transistor envelope detector and output stage), simulated from the NETLIST (ckt.h) with the
// controls of the physical pedal (Output, Sensitivity) and the documented modifications.
//
// Sources (2026-10-19): ElectroSmash, MXR Dyna Comp analysis (archived copy, https://electrosmash.com/mxr-dyna-comp-analysis): 9 V supply with R15 56k / R16 22k divider and C11 10 uF for the 2.54 V
// reference; buffer: C1 10 nF, R1 10k, Q1 2N3904 with R2 1M at 2.54 V and R3 10k, C2 1 uF, R4 1M to ground; OTA: "-" input connected to the input node, "+" input through the 2k trimmer (at half travel) with
// R5 470k to +9 V, R6 1M to ground, R7 15k in parallel with C4 10 nF and C3 1 uF to ground; OTA output on R8 150k in parallel with C5 1 nF towards 2.54 V, then Q2 (R9 10k at the collector, R10 10k at the emitter);
// detector: C6 10 nF, R11 1M and D1 1N914 towards Q3's base, C7 10 nF, R12 1M and D2 towards Q4's base, the collectors of Q3 and Q4 on the node of C8 10 uF and R13 150k to +9 V, Q5 follower whose emitter
// goes, through R17 27k and sensitivity RV1 500k (rheostat), to pin 5 of the OTA (bias current Iabc); output: Q2's emitter, C9 50 nF, R14 10k and level RV2 50k. Modifications: C5 from 1 nF to 100-470 pF
// or removed; C7 disconnected (half-wave detector); C9 1 uF; R13 10k (ElectroSmash's "attack mod"). CA3080 datasheet (RCA / Harris, AN6668): i = Iabc tanh(Vd / 2 Vt), gm = Iabc / (2 Vt) = 19.3 Iabc.
//
// Model: Ebers-Moll (Is 6.7 fA, beta 300 from ElectroSmash, reverse 0.74: values from the standard SPICE library of the 2N3904, not verified), 1N914 diodes (Is 2.52 nA, n 1.752), OTA pin 5 as two diodes in series
// to ground (about 1.2 V), OTA with 1 Mohm inputs to ground (ElectroSmash's Zin), trimmer at half travel, Iabc updated at every sample (a one-sample delay, the detector is slow).
// Taper of the potentiometers not documented: assumed logarithmic (audio) for Output and Sensitivity. Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kIs = 6.7e-15, kBf = 300.0, kBr = 0.74, kVt = 0.02585;
const double kD1Is = 2.52e-9, kD1N = 1.752 * 0.02585;
const double kC5[4] = {1.0e-9, 470.0e-12, 100.0e-12, 1.0e-13};

float Taper (float x, float a)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return (Ck::Exp (a * x) - 1.0f) / (Ck::Exp (a) - 1.0f);
}

// p: 0 = sensitivity, 1 = output (0-10), 2 = C5 (index), 3 = detector (0 full wave, 1 half wave), 4 = C9 (0 50 nF, 1 1 uF), 5 = R13 (0 150k, 1 10k)
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int c5i = (int) (pp[2] + 0.5f);
	c5i = c5i < 0 ? 0 : (c5i > 3 ? 3 : c5i);
	const bool bHalf = pp[3] > 0.5f, bC9 = pp[4] > 0.5f, bR13 = pp[5] > 0.5f;
	const int v9 = c.Node (), vb = c.Node (), ix = c.Node (), in = c.Node (), n1x = c.Node (), b1 = c.Node (), e1 = c.Node (), n1 = c.Node ();
	c.V (v9, 0, 9.0);
	// reference 2.54 V
	c.R (v9, vb, 56.0e3); c.R (vb, 0, 22.0e3); c.C (vb, 0, 10.0e-6);
	// input buffer
	h[0] = c.V (ix, 0, 0.0);
	c.R (ix, in, 1.0);
	c.C (in, n1x, 10.0e-9); c.R (n1x, b1, 10.0e3);
	c.R (b1, vb, 1.0e6);
	c.Bjt (v9, b1, e1, kIs, kBf, kBr, kVt, false, true);
	c.R (e1, 0, 10.0e3);
	c.C (e1, n1, 1.0e-6);
	c.R (n1, 0, 1.0e6);						// R4
	// OTA
	const int wp = c.Node (), p = c.Node (), q7 = c.Node (), oo = c.Node ();
	c.R (n1, wp, 1.0e3); c.R (wp, p, 1.0e3);			// 2k trimmer at half travel
	c.R (v9, wp, 470.0e3);						// R5
	c.R (p, 0, 1.0e6);						// R6
	c.R (p, q7, 15.0e3); c.C (p, q7, 10.0e-9);			// R7 || C4
	c.C (q7, 0, 1.0e-6);						// C3
	c.R (n1, 0, 1.0e6); c.R (p, 0, 1.0e6);				// Zin of the two inputs
	const int port = c.Ota (p, n1, oo, 100.0e-6);
	c.R (oo, vb, 150.0e3); c.C (oo, vb, kC5[c5i]);			// R8 || C5
	// Q2: phase splitter
	const int c2 = c.Node (), e2 = c.Node ();
	c.Bjt (c2, oo, e2, kIs, kBf, kBr, kVt, false);
	c.R (v9, c2, 10.0e3); c.R (e2, 0, 10.0e3);			// R9, R10
	// envelope detector
	const int b3 = c.Node (), b4 = c.Node (), E = c.Node (), q5e = c.Node (), mid = c.Node (), p5 = c.Node (), m5 = c.Node ();
	c.C (c2, b3, 10.0e-9); c.R (b3, 0, 1.0e6); c.Diode (0, b3, kD1Is, kD1N);
	c.C (e2, b4, bHalf ? 1.0e-15 : 10.0e-9); c.R (b4, 0, 1.0e6); c.Diode (0, b4, kD1Is, kD1N);
	c.Bjt (E, b3, 0, kIs, kBf, kBr, kVt, false);
	c.Bjt (E, b4, 0, kIs, kBf, kBr, kVt, false);
	c.R (v9, E, bR13 ? 10.0e3 : 150.0e3);				// R13
	c.C (E, 0, 10.0e-6);						// C8
	c.Bjt (v9, E, q5e, kIs, kBf, kBr, kVt, false, true);	// Q5
	const int r17 = c.R (q5e, mid, 27.0e3);
	h[2] = c.R (mid, p5, 100.0e3);					// RV1 (rheostat)
	c.Diode (p5, m5, kD1Is, kD1N); c.Diode (m5, 0, kD1Is, kD1N);	// pin 5: two junctions to ground
	c.OtaControl (port, q5e, p5, r17, h[2]);
	h[3] = r17;
	// output
	const int o1 = c.Node (), o2 = c.Node (), w2 = c.Node ();
	c.C (e2, o1, bC9 ? 1.0e-6 : 50.0e-9);
	c.R (o1, o2, 10.0e3);						// R14
	h[4] = c.R (o2, w2, 25.0e3);
	h[5] = c.R (w2, 0, 25.0e3);
	c.R (w2, 0, 1.0e6);
	c.SetOut (w2);
	h[1] = w2;
	h[6] = port; h[7] = E; h[8] = n1; h[9] = p; h[10] = oo; h[11] = q5e; h[12] = p5; h[13] = e2; h[14] = vb; h[15] = c2;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double s = p[0] * 0.1, l = p[1] * 0.1;
	s = s < 0 ? 0 : (s > 1 ? 1 : s); l = l < 0 ? 0 : (l > 1 ? 1 : l);
	// Sensitivity: 500k rheostat, clockwise minimum resistance (more Iabc current, more gain and more compression)
	const double ts = (double) Taper ((float) s, 2.77f);
	c.SetR (h[2], (1.0 - ts) * 500.0e3 + 1.0);
	const double tl = (double) Taper ((float) l, 2.77f);
	c.SetR (h[4], (1.0 - tl) * 50.0e3 + 1.0); c.SetR (h[5], tl * 50.0e3 + 1.0);
}

const TPedal kPedal = {FxDynaComp, Build, Pots, 6, (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5), 1.0f / CKT_VFS, 0};

}

CFx *NewDynaComp (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void DynaCompBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void DynaCompPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
