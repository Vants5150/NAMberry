//
// fx_wah.cpp - Wah Real: Dunlop Cry Baby GCB-95 (Vox V847), simulated from the NETLIST (ckt.h) with the physical pedal's control (pedal position) and the documented options (input buffer,
// "vocal mod", sweep range).
//
// Sources (2026-10-19): ElectroSmash, Dunlop CryBaby GCB-95 analysis (archived copy, https://electrosmash.com/crybaby-gcb-95) and Vox V847 analysis: MPSA13 Darlington input buffer (Cin1 10 nF,
// Rin1 2.2M, Rin2 1.8M, Rin3 1k, Rin4 10k, Cin2 22 pF); active filter: R1 68k, C1 10 nF, Q1 MPSA18 with R2 1.5k, R3 22k, R4 390, feedback R6 470k + C5 0.22 uF, inductor L1 500 mH in parallel with R7 33k
// ("Q" corner), C3 4.7 uF and R8 82k to ground, C2 10 nF from the circuit to Q2's emitter; pedal potentiometer VR1 100k logarithmic, C4 0.22 uF, MPSA18 output stage with R5 470k, R9 1k, R10 10k;
// 9 V supply (9.1 V zener); the pedal's output is upstream of VR1 (after C5), as ElectroSmash writes; operating points: collector Q0 8.6 V, emitter 3 V, collector Q1 4.9 V, emitter 0.06 V, L1 node 0.6 V, Q2 base 4.3 V; response: peak from 450 Hz to 1.6 kHz
// (750 Hz at half), up to +18 dB. Modifications: R7 33k -> 39k, 68k, 100k ("vocal mod"), C2 0.01 uF -> 0.1 uF or 1 nF (sweep range). Without the input buffer (V847 and Cry Baby before 1991) the input impedance
// is 69.5 kohm.
//
// Model: Ebers-Moll (Is 1e-14, beta 5000 for the MPSA13, 800 for the MPSA18s, reverse 5), ideal 500 mH inductor (without saturation or series resistance); pedal position -> wiper-to-ground resistance (fraction of 100k): 0.015 + 0.985 e^(-3.3 x), calibrated on ElectroSmash's peaks (750 Hz at half travel). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kVt = 0.02585;
const double kIs = 1.0e-14, kBr = 5.0;
const double kQ[4] = {33.0e3, 39.0e3, 68.0e3, 100.0e3};
const double kC2[3] = {10.0e-9, 100.0e-9, 1.0e-9};

float Taper (float x, float a)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return (Ck::Exp (a * x) - 1.0f) / (Ck::Exp (a) - 1.0f);
}

// p: 0 = pedal position (0 heel, 10 toe), 1 = Q (R7), 2 = range (C2), 3 = input buffer
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int c2i = (int) (pp[2] + 0.5f);
	c2i = c2i < 0 ? 0 : (c2i > 2 ? 2 : c2i);
	const bool bBuf = pp[3] > 0.5f;
	const int v9 = c.Node (), in = c.Node ();
	c.V (v9, 0, 9.0);
	h[0] = c.V (in, 0, 0.0);
	// input buffer
	const int q0b = c.Node (), q0c = c.Node (), q0e = c.Node ();
	int src = in;
	if (bBuf)
	{
		c.C (in, q0b, 10.0e-9);
		c.R (q0b, 0, 2.2e6);
		c.R (q0b, q0c, 1.8e6);
		c.C (q0b, 0, 22.0e-12);
		c.R (v9, q0c, 1.0e3);					// Rin3
		c.Bjt (q0c, q0b, q0e, 1.0e-20, 5000.0, kBr, kVt, false, true);		// Darlington: two junctions in series (Vbe about 1 V: beyond that the exponential is not linear in the engine)
		c.R (q0e, 0, 10.0e3);
		src = q0e;
	}
	// active filter
	const int n1 = c.Node (), b1 = c.Node (), c1 = c.Node (), e1 = c.Node (), f = c.Node (), bn = c.Node (), vt = c.Node (), wp = c.Node (), b2 = c.Node (), e2 = c.Node (), c2n = c.Node ();
	c.R (src, n1, 68.0e3);					// R1
	c.C (n1, b1, 10.0e-9);					// C1
	c.R (b1, bn, 1.5e3);					// R2
	c.Bjt (c1, b1, e1, kIs, 800.0, kBr, kVt, false, true);
	c.R (v9, c1, 22.0e3);					// R3
	c.R (e1, 0, 390.0);					// R4
	c.R (c1, f, 470.0e3);					// R6
	c.Ind (f, bn, 0.5);					// L1
	h[2] = c.R (f, bn, 33.0e3);				// R7
	c.C (f, 0, 4.7e-6);					// C3
	c.R (f, 0, 82.0e3);					// R8
	c.C (c1, vt, 0.22e-6);					// C5
	h[3] = c.R (vt, wp, 50.0e3);				// VR1: from the top to the wiper
	h[4] = c.R (wp, 0, 50.0e3);				// VR1: from the wiper to ground
	// output stage
	c.C (wp, b2, 0.22e-6);					// C4
	c.R (c1, b2, 470.0e3);					// R5
	c.Bjt (c2n, b2, e2, kIs, 800.0, kBr, kVt, false, true);
	c.R (v9, c2n, 1.0e3);					// R9
	c.R (e2, 0, 10.0e3);					// R10
	c.C (e2, bn, kC2[c2i]);					// C2
	c.R (vt, 0, 1.0e6);					// amplifier input: the output jack is taken upstream of the pedal's potentiometer (after C5), so the level does not depend on the position
	c.SetOut (vt);
	h[1] = vt;
	h[5] = c1; h[6] = f; h[7] = b2; h[8] = b1; h[9] = e1; h[10] = q0e; h[11] = q0c;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double x = p[0] * 0.1;
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	// pedal: at the heel the wiper is towards the top (maximum gain, 450 Hz), at the toe towards ground (minimum gain, 1.6 kHz): logarithmic VR1
	// wiper-to-ground resistance as a fraction of 100k: 1 at the heel, about 0.2 at half travel (750 Hz) and about 0.05 at the toe (1.5 kHz): exponential curve calibrated on ElectroSmash's peaks
	const double frac = 0.015 + 0.985 * (double) Ck::Exp (-3.3 * x);
	c.SetR (h[3], (1.0 - frac) * 100.0e3 + 1.0); c.SetR (h[4], frac * 100.0e3 + 1.0);
	int qi = (int) (p[1] + 0.5f);
	qi = qi < 0 ? 0 : (qi > 3 ? 3 : qi);
	c.SetR (h[2], kQ[qi]);
}

const TPedal kPedal = {FxCry, Build, Pots, 4, (1u << 2) | (1u << 3), 1.0f / CKT_VFS};

}

CFx *NewWah (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void WahBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void WahPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
