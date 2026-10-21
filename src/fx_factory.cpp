//
// fx_factory.cpp - Fuzz Factory Real: Zvex Fuzz Factory in the version of the schematic "FUZZ FACTORY schematic 12.02.2019" supplied by the user (author not indicated), simulated from the NETLIST (ckt.h) with the five
// controls (Gate, Comp, Drive, Stab, Volume) and the choice between the two types of AC128 pair.
//
// Source (2026-10-19): image of the schematic and of the parts list sent by the user in the chat (not saved as files; the PCB drawing is on the same page): +9 V supply with C2 47 uF
// and D1 1N400X (reverse polarity protection, does not conduct); RV1 STAB B5k in series with the supply; Q1 2N3904 with collector on R3 10k, R2 220k bias from the collector to the base, C1 10 uF and R1
// 1M at the input, emitter to ground; C3 100 nF from Q1's collector to Q2's base; Q2 and Q3 AC128 (germanium PNP, emitters towards the positive): Q2 with emitter on the supply, collector on R5 470 ohm and
// RV4 GATE B10k to ground and to Q3's base; Q3 with emitter on node D, collector on R6 5k1 and RV5 VOLUME B5k to ground; feedback R4 47k from Q2's base to node D; node D is connected to the supply by RV3
// COMP B10k (rheostat) and by RV2 DRIVE B10k, whose wiper goes to C4 10 uF towards the supply (ground path for the signal); output from RV5's wiper with C5 10 uF and R7 220k to ground. All the potentiometers are
// "B" (linear).
//
// Model: Ebers-Moll with the two junctions; AC128 with the ElectroSmash parameters (like the Fuzz Face: RB 173, RE 20, RC 60 ohm, IS 85.8 nA with beta 85 for Q2 and 120.8 nA with beta 120 for Q3; "Hot" = beta 105 and 170);
// 2N3904 Is 6.7 fA, beta 300, reverse 0.74 (standard SPICE library, not verified); Early effect, leakage and beta drop not modelled. ASSUMPTIONS on the direction of the potentiometers (the schematic does not say): Stab
// clockwise = minimum series resistance (more voltage), Gate clockwise = RV4 at maximum (Q3 more off: more "gate"), Comp clockwise = RV3 at minimum, Drive clockwise = wiper towards node D
// (less resistance between node D and C4: the node is more grounded for the signal). Ideal source or 8 kohm (pickup load). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

// p: 0 = gate, 1 = comp, 2 = drive, 3 = stab, 4 = volume (0-10), 5 = transistor (0 vintage, 1 hot), 6 = source (0 ideal, 1 guitar 8k)
// handles: 0 generator, 1 output, 2 stab, 3 gate, 4 comp, 5 drive (D -> wiper), 6 drive (wiper -> supply), 7 volume top, 8 volume bottom, 9 source,
// 10 Vs, 11 Q1 collector, 12 Q2 base, 13 Q2 collector, 14 node D, 15 Q3 collector
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	const bool bHot = pp[5] > 0.5f;
	const double bf2 = bHot ? 105.0 : 85.0, bf3 = bHot ? 170.0 : 120.0;
	const double is2 = bf2 * 1.0e-9 * (bHot ? 1.0 : 1.0095), is3 = bf3 * 1.0e-9 * (bHot ? 1.0 : 1.0067);
	const int v9 = c.Node (), vs = c.Node (), ix = c.Node (), in = c.Node (), b1 = c.Node (), a = c.Node (), b2 = c.Node ();
	const int cc = c.Node (), g = c.Node (), d = c.Node (), e3 = c.Node (), vt = c.Node (), wv = c.Node (), vo = c.Node (), w = c.Node ();
	c.V (v9, 0, 9.0);
	h[2] = c.R (v9, vs, 1.0);					// RV1 STAB (rheostat)
	h[0] = c.V (ix, 0, 0.0);
	h[9] = c.R (ix, in, 1.0);
	c.C (in, b1, 10.0e-6); c.R (in, 0, 1.0e6);			// C1, R1
	c.Bjt (a, b1, 0, 6.7e-15, 300.0, 0.74, 0.02585, false, false);	// Q1 2N3904
	c.R (vs, a, 10.0e3);						// R3
	c.R (a, b1, 220.0e3);						// R2
	c.C (a, b2, 100.0e-9);						// C3
	AddBjt (c, cc, b2, vs, is2, bf2, 20.0, 173.0, 20.0, 60.0, true, false);	// Q2: base b2, collector cc, emitter on the supply
	c.R (b2, d, 47.0e3);						// R4
	c.R (cc, g, 470.0);						// R5
	h[3] = c.R (g, 0, 1.0);					// RV4 GATE
	AddBjt (c, e3, cc, d, is3, bf3, 20.0, 173.0, 20.0, 60.0, true, false);	// Q3: base dc, collector e3, emitter d
	h[4] = c.R (vs, d, 1.0);					// RV3 COMP (rheostat)
	h[5] = c.R (d, w, 5.0e3);					// RV2 DRIVE: from node D to the wiper
	h[6] = c.R (w, vs, 5.0e3);					// RV2: from the wiper to the supply
	c.C (w, vs, 10.0e-6);						// C4
	c.R (e3, vt, 5.1e3);						// R6
	h[7] = c.R (vt, wv, 2.5e3);					// RV5 VOLUME
	h[8] = c.R (wv, 0, 2.5e3);
	c.C (wv, vo, 10.0e-6);						// C5
	c.R (vo, 0, 220.0e3);						// R7
	c.R (vo, 0, 1.0e6);						// load
	c.SetOut (vo);
	h[1] = vo;
	h[10] = vs; h[11] = a; h[12] = b2; h[13] = cc; h[14] = d; h[15] = e3;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double gt = p[0] * 0.1, cm = p[1] * 0.1, dr = p[2] * 0.1, st = p[3] * 0.1, vl = p[4] * 0.1;
	gt = gt < 0 ? 0 : (gt > 1 ? 1 : gt); cm = cm < 0 ? 0 : (cm > 1 ? 1 : cm); dr = dr < 0 ? 0 : (dr > 1 ? 1 : dr);
	st = st < 0 ? 0 : (st > 1 ? 1 : st); vl = vl < 0 ? 0 : (vl > 1 ? 1 : vl);
	c.SetR (h[2], 5.0e3 * (1.0 - st) + 0.5);
	c.SetR (h[3], 10.0e3 * gt + 0.5);
	c.SetR (h[4], 10.0e3 * (1.0 - cm) + 0.5);
	c.SetR (h[5], 10.0e3 * (1.0 - dr) + 0.5); c.SetR (h[6], 10.0e3 * dr + 0.5);
	c.SetR (h[7], 5.0e3 * (1.0 - vl) + 0.5); c.SetR (h[8], 5.0e3 * vl + 0.5);
	c.SetR (h[9], p[6] > 0.5f ? 8.0e3 : 1.0);
}

const TPedal kPedal = {FxFactory, Build, Pots, 7, (1u << 5), 1.0f / CKT_VFS};

}

CFx *NewFuzzFactory (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void FactoryBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void FactoryPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
