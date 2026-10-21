//
// fx_rangemaster.cpp - Rangemaster Real: Dallas Rangemaster Treble Booster (1960s, OC44 germanium PNP, positive ground), simulated from the NETLIST (ckt.h) with the pedal's control (Range).
//
// Sources (2026-10-19): ElectroSmash, Dallas Rangemaster Treble Booster analysis (archived copy, https://electrosmash.com/dallas-rangemaster): schematic: C1 5 nF at the input, R1 470k towards the negative
// rail and R2 68k towards the positive (base at +8 V), OC44 with emitter at +8.2 V through R3 3.9k with C3 47 uF in parallel, collector (+2 V relative to the negative rail) on the
// 10k logarithmic potentiometer RV (Range) whose wiper goes to C2 10 nF and to the output; 9 V supply with C4 47 uF; operating points: base +8 V, emitter +8.2 V, collector +2 V relative to the
// negative rail's ground; gain gm x Rc = 0.008 x 10k = 80 (38 dB); popular modifications: C1 of 5, 10, 15, 47, 100 nF and adjustable R2 (100-200k) for the bias; "the sound depends a lot on the
// bias and on the transistor's beta", input impedance 10-12 kohm that interacts with the pickups.
//
// Model: Ebers-Moll with the two junctions and the internal resistances of the AC128 SPICE model (ElectroSmash, Fuzz Face page): RB 173, RE 20, RC 60 ohm; Is 87 nA (from Vbe = 0.2 V at 0.2 mA, as
// in their operating points), beta 100 (ElectroSmash's value) or 150. Early effect, leakage and beta drop at high current not modelled; ideal source or 8 kohm (pickup load, inductance
// not modelled). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kPotRv = 10.0e3;
const double kCap[5] = {5.0e-9, 10.0e-9, 15.0e-9, 47.0e-9, 100.0e-9};

float Taper (float x, float a)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return (Ck::Exp (a * x) - 1.0f) / (Ck::Exp (a) - 1.0f);
}

// p: 0 = range (0-10), 1 = bias R2 (kohm), 2 = input capacitor (index), 3 = transistor, 4 = source
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int ci = (int) (pp[2] + 0.5f);
	ci = ci < 0 ? 0 : (ci > 4 ? 4 : ci);
	const bool bHot = pp[3] > 0.5f;
	const int vee = c.Node (), ix = c.Node (), in = c.Node (), b = c.Node (), e = c.Node (), col = c.Node (), wp = c.Node (), out = c.Node ();
	c.V (vee, 0, -9.0);
	h[0] = c.V (ix, 0, 0.0);
	h[6] = c.R (ix, in, 1.0);
	c.C (in, b, kCap[ci]);
	c.R (vee, b, 470.0e3);
	h[2] = c.R (b, 0, 68.0e3);
	AddBjt (c, col, b, e, 87.0e-9 * (bHot ? 1.5 : 1.0), bHot ? 150.0 : 100.0, 20.0, 173.0, 20.0, 60.0, true, false);
	c.R (e, 0, 3.9e3);
	c.C (e, 0, 47.0e-6);
	h[3] = c.R (col, wp, 5.0e3);
	h[4] = c.R (wp, vee, 5.0e3);
	c.C (wp, out, 10.0e-9);
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[1] = out;
	h[7] = col; h[8] = e; h[9] = b; h[10] = in;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double x = p[0] * 0.1;
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	// Range: 10k logarithmic potentiometer between the collector and the rail; clockwise the wiper goes towards the collector
	const double t = (double) Taper ((float) x, 2.77f);
	c.SetR (h[3], (1.0 - t) * kPotRv + 1.0); c.SetR (h[4], t * kPotRv + 1.0);
	double r2 = p[1] * 1.0e3;
	r2 = r2 < 10.0e3 ? 10.0e3 : (r2 > 300.0e3 ? 300.0e3 : r2);
	c.SetR (h[2], r2);
	c.SetR (h[6], p[4] > 0.5f ? 8.0e3 : 1.0);
}

const TPedal kPedal = {FxRange, Build, Pots, 5, (1u << 2) | (1u << 3), 1.0f / CKT_VFS};

}

CFx *NewRangemaster (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void RangeBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void RangePotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
