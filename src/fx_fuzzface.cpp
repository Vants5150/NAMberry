//
// fx_fuzzface.cpp - Fuzz Face Real: Dallas-Arbiter germanium Fuzz Face (PNP, positive ground), simulated from the NETLIST (ckt.h) with the pedal's controls (Fuzz, Volume).
//
// Fonti (2026-10-19):
//  - ElectroSmash, Fuzz Face analysis (archived copy, https://electrosmash.com/fuzz-face): original schematic and values: C1 2.2 uF, C2 20 uF, C3 0.01 uF, R1 33k, R2 470, R3 8.2k, R4 100k (feedback
//    from Q2's emitter to Q1's base), Fuzz 1k linear (with C2 from the wiper to ground), Volume 500k logarithmic, two AC128s; operating points (Q1: base -0.2 V, collector -0.7 V; Q2: emitter -0.5 V,
//    collector -4.5 V; output node -8.7 V) with Q1 beta 85 and Q2 beta 120 and Fuzz at half; PSpice models of the AC128 (IS 85.8 nA with beta 85 and 120.8 nA with beta 120, VAF 102 V, BR 20, RB 173 ohm,
//    RE 20 ohm, RC 60 ohm); gain of the first stage 18.6 dB, of Q2 19.5 dB, of the pedal -5.5 dB at maximum; input impedance from 5.2 to 8.4 kohm depending on the Fuzz position.
//    Choice of the transistors: "the best one has low beta in the first stage (70-80) and high in the second (110-130); for a more compressed sound 90-120 and 150-190".
//
// Model: Ebers-Moll with the two junctions and the internal base, emitter and collector resistances of the SPICE model; the Early effect, the leakage current and the beta drop at high current (IKF 10 mA)
// are not modelled. Temperature and battery (internal resistance) do not vary. The source is ideal ("Buffered") or an 8 kohm resistor that imitates the pickup load: the Fuzz Face depends
// a lot on the guitar's impedance (pickup inductance not modelled). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kPotFuzz = 1.0e3, kPotVol = 500.0e3;

void Build (Ck::TCkt &c, int *h, const float *pp)
{
	const bool bHot = pp[2] > 0.5f;
	// Is grows with beta in the ElectroSmash model (IS 85.8 nA at beta 85, 120.8 nA at beta 120)
	const double bf1 = bHot ? 105.0 : 85.0, bf2 = bHot ? 170.0 : 120.0;
	const double is1 = bf1 * 1.0e-9 * (bHot ? 1.0 : 1.0095), is2 = bf2 * 1.0e-9 * (bHot ? 1.0 : 1.0067);
	const bool bGtr = pp[3] > 0.5f;
	const int vee = c.Node (), ix = c.Node (), in = c.Node ();
	c.V (vee, 0, -9.0);
	h[0] = c.V (ix, 0, 0.0);
	c.R (ix, in, bGtr ? 8.0e3 : 1.0);
	const int b1 = c.Node (), c1 = c.Node (), e2 = c.Node (), wp = c.Node (), c2 = c.Node (), nout = c.Node (), vo = c.Node (), wv = c.Node ();
	c.C (in, b1, 2.2e-6);
	AddBjt (c, c1, b1, 0, is1, bf1, 20.0, 173.0, 20.0, 60.0, true, false);
	c.R (vee, c1, 33.0e3);
	AddBjt (c, c2, c1, e2, is2, bf2, 20.0, 173.0, 20.0, 60.0, true, false);
	c.R (c2, nout, 8.2e3);
	c.R (nout, vee, 470.0);
	c.R (e2, b1, 100.0e3);
	h[2] = c.R (e2, wp, 500.0);
	h[3] = c.R (wp, 0, 500.0);
	c.C (wp, 0, 20.0e-6);
	c.C (nout, vo, 0.01e-6);
	h[4] = c.R (vo, wv, 250.0e3);
	h[5] = c.R (wv, 0, 250.0e3);
	c.R (wv, 0, 1.0e6);
	c.SetOut (wv);
	h[1] = wv;
	h[6] = c1; h[7] = c2; h[8] = e2; h[9] = b1; h[10] = nout; h[11] = in;
}

float Taper (float x, float a)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return (Ck::Exp (a * x) - 1.0f) / (Ck::Exp (a) - 1.0f);
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double f = p[0] * 0.1, v = p[1] * 0.1;
	f = f < 0 ? 0 : (f > 1 ? 1 : f);
	v = v < 0 ? 0 : (v > 1 ? 1 : v);
	// Fuzz clockwise: the wiper rises towards the emitter (the 20 uF capacitor gradually short-circuits the resistance)
	c.SetR (h[2], (1.0 - f) * kPotFuzz + 0.5); c.SetR (h[3], f * kPotFuzz + 0.5);
	// Logarithmic Volume: the wiper-to-ground section follows the A curve
	const double t = (double) Taper ((float) v, 2.77f);
	c.SetR (h[4], (1.0 - t) * kPotVol + 1.0); c.SetR (h[5], t * kPotVol + 1.0);
}

const TPedal kPedal = {FxFuzzFace, Build, Pots, 4, (1u << 2) | (1u << 3), 1.0f / CKT_VFS};

}

CFx *NewFuzzFace (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void FuzzBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void FuzzPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
