//
// fx_bender.cpp - Tone Bender Real: Sola Sound / Vox / Colorsound Tone Bender MkII (1966-68, three PNP germanium transistors, positive ground), simulated from the NETLIST (ckt.h) with the controls of the physical
// pedal (Attack, Level) and the choice between the two documented versions (OC81D and OC75).
//
// Sources (2026-10-19):
//  - Schematic and values: P.U.T.B. Tonebender MkII PNP, Guitar PCB (https://guitarpcb.com/wp-content/uploads/2018/06/BD_PUTB.pdf), based on the Colorsound version: R1 1M and C1 10 nF to ground at the input, C2 4.7 uF,
//    R2 100k (Q1 base to ground), R3 10k (Q1 collector), C3 220 pF (Q1 Miller), C5 100 nF between Q1's collector and Q2's base, R5 100k feedback from Q2's base to Q3's emitter,
//    C6 220 pF, R4 100k (Q2 collector), Q3 directly coupled to Q2 with C7 220 pF, Q3 load (20k trimmer in place of the original's fixed 8.2k) and R6 470 ohm, output C9 10 nF and Level 100k log;
//    Attack 1k linear on Q3's emitter with C8 4.7 uF. Values of the original Sola Sound: R4 100k and R6 470 ohm; Fuzz Central: beta about 70 for Q1 and Q2 and 100 for Q3.
//  - Voltages measured on original units (PedalPCB thread "Tonebender Mk. II transistor clarification", https://forum.pedalpcb.com/threads/tonebender-mk-ii-transistor-clarification.28158/):
//    OC75 (10k on Q1's base and 47k on Q2's collector): battery -9.01 V, Q1 c -8.50 b -0.038, Q2 c -0.13 b -0.076, Q3 c -8.29 b -0.13 e -0.08;
//    OC81D (100k and 100k): battery -9.55 V, Q1 c -8.51 b -0.06, Q2 c -0.13 b -0.068, Q3 c -8.72 b -0.13 e -0.07. Germanium transistors have strong leakage: here Is of a few uA (not the 87 nA of the AC128).
//
// Model: Ebers-Moll with the two junctions (Is and beta calibrated on the measured voltages) and RB 100 ohm. Early effect, temperature and unit-to-unit variations are not modelled: in real pedals the
// bias changes a lot from one unit to another (the thread repeats it). Ideal source or 8 kohm (pickup load, inductance not modelled). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

// version: 0 = Sola/Vox OC81D, 1 = Vox OC75
struct TVer { double r2, r4, is[3], bf[3]; };
const TVer kVer[2] = {{100.0e3, 100.0e3, {1.0e-5, 1.0e-5, 1.0e-5}, {118.0, 65.0, 106.0}},
	{10.0e3, 47.0e3, {1.5e-5, 1.5e-5, 1.5e-5}, {100.0, 100.0, 100.0}}};

float Taper (float x, float a)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return (Ck::Exp (a * x) - 1.0f) / (Ck::Exp (a) - 1.0f);
}

// p: 0 = attack, 1 = level, 2 = version, 3 = Q3 load resistance (kohm), 4 = source
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int vi = (int) (pp[2] + 0.5f);
	vi = vi < 0 ? 0 : (vi > 1 ? 1 : vi);
	const TVer &V = kVer[vi];
	const int vee = c.Node (), ix = c.Node (), in = c.Node ();
	c.V (vee, 0, -9.0);
	h[0] = c.V (ix, 0, 0.0);
	h[6] = c.R (ix, in, 1.0);
	c.R (in, 0, 1.0e6);					// R1
	c.C (in, 0, 10.0e-9);					// C1
	const int b1 = c.Node (), c1 = c.Node (), b2 = c.Node (), c2 = c.Node (), e3 = c.Node (), c3 = c.Node (), t = c.Node (), wp = c.Node (), lw = c.Node (), vo = c.Node (), out = c.Node ();
	c.C (in, b1, 4.7e-6);					// C2
	h[7] = c.R (b1, 0, V.r2);				// R2
	AddBjt (c, c1, b1, 0, V.is[0], V.bf[0], 10.0, 100.0, 0.0, 0.0, true, false);	// Q1
	c.R (vee, c1, 10.0e3);					// R3
	c.C (b1, c1, 220.0e-12);					// C3
	c.C (c1, b2, 100.0e-9);					// C5
	c.R (b2, e3, 100.0e3);					// R5
	AddBjt (c, c2, b2, 0, V.is[1], V.bf[1], 10.0, 100.0, 0.0, 0.0, true, false);	// Q2
	h[8] = c.R (vee, c2, V.r4);				// R4
	c.C (b2, c2, 220.0e-12);					// C6
	AddBjt (c, c3, c2, e3, V.is[2], V.bf[2], 10.0, 100.0, 0.0, 0.0, true, false);	// Q3 (base = Q2 collector)
	c.C (c2, c3, 220.0e-12);					// C7
	h[9] = c.R (c3, t, 8.2e3);				// Q3 load (adjustable)
	c.R (t, vee, 470.0);					// R6
	// Attack 1k linear with C8 from the wiper to ground
	h[2] = c.R (e3, wp, 500.0);
	h[3] = c.R (wp, 0, 500.0);
	c.C (wp, 0, 4.7e-6);					// C8
	// output: C9 and Level 100k logarithmic
	c.C (t, vo, 10.0e-9);
	h[4] = c.R (vo, lw, 50.0e3);
	h[5] = c.R (lw, 0, 50.0e3);
	c.R (lw, out, 1.0);
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[1] = out;
	h[10] = c1; h[11] = c2; h[12] = c3; h[13] = b1; h[14] = b2; h[15] = e3;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double a = p[0] * 0.1, l = p[1] * 0.1;
	a = a < 0 ? 0 : (a > 1 ? 1 : a); l = l < 0 ? 0 : (l > 1 ? 1 : l);
	// Attack: clockwise the wiper goes towards the emitter (the 4.7 uF capacitor gradually short-circuits the resistance): more gain
	c.SetR (h[2], (1.0 - a) * 1000.0 + 0.5); c.SetR (h[3], a * 1000.0 + 0.5);
	const double t = (double) Taper ((float) l, 2.77f);
	c.SetR (h[4], (1.0 - t) * 100.0e3 + 1.0); c.SetR (h[5], t * 100.0e3 + 1.0);
	double rc = p[3] * 1.0e3;
	rc = rc < 1.0e3 ? 1.0e3 : (rc > 40.0e3 ? 40.0e3 : rc);
	c.SetR (h[9], rc);
	c.SetR (h[6], p[4] > 0.5f ? 8.0e3 : 1.0);
}

const TPedal kPedal = {FxBender, Build, Pots, 5, 1u << 2, 1.0f / CKT_VFS};

}

CFx *NewBender (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void BenderBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void BenderPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
