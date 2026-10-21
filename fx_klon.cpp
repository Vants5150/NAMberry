//
// fx_klon.cpp - Klon Real: Klon Centaur (Bill Finnegan), simulated from the NETLIST (ckt.h) with the physical pedal's controls (Gain, Treble, Output) and the choice of the clipping diodes.
//
// Sources (2026-10-19): ElectroSmash, Klon Centaur analysis (archived copy, https://electrosmash.com/klon-centaur-analysis), schematic and values: TL072 input buffer (R1 10k, C1 0.1 uF, R2 1M);
// gain stage: C3 0.1 uF, C5 68 nF in parallel with R6 10k, Gain potentiometer 100k (two sections: the first with wiper at 4.5 V and R10 2k, R11 15k with C7 82 nF and R12 422k with C8 390 pF in feedback,
// maximum gain about 40 dB at 1 kHz) and the second with R5 5.1k + C4 68 nF, R8 1.5k, C6 390 nF + R9 1k and R15 22k + C11 2.2 nF, R17 27k, R18 12k + C12 27 nF; network 1: R7 1.5k, C16 1 uF, R19 15k;
// stage output: C9 1 uF, R13 1k, two germanium 1N34As to ground (Vf 0.35 V), C10 1 uF and R16 47k; TL072 summer with R20 392k and C13 820 pF, powered at +16.2 V and -8.6 V (MAX1044);
// tone: active shelving R22 100k, R24 100k, R21 1.8k, R23 4.7k, Treble potentiometer 10k, C14 3.9 nF; output: C15 4.7 uF, R25 560, Output 10k, and the direct signal (bypass) that goes through C2 4.7 uF, R3 100k,
// R4 560 and R243 68k into the output node together with R28 100k.
//
// Model: TL072 as a macromodel (gain-bandwidth product 3 MHz; the first at 9 V with rails at +-3 V around the 4.5 V, the summer and the tone at 16.2 V / -8.6 V with rails at +10.2 and -11.6 V); 1N34A with
// Is 30 nA and n 1.3 (Vf 0.35 V); alternatives: 1N4148 and red LED (the popular modifications). The potentiometers are considered linear ("b" in the schematic). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kVt = 0.02585;

void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int di = (int) (pp[3] + 0.5f);
	di = di < 0 ? 0 : (di > 2 ? 2 : di);
	const int v9 = c.Node (), vb = c.Node (), in = c.Node ();
	c.V (v9, 0, 9.0);
	c.R (v9, vb, 27.0e3);
	c.R (vb, 0, 27.0e3);
	c.C (vb, 0, 47.0e-6);
	h[0] = c.V (in, 0, 0.0);
	// input buffer
	const int n1 = c.Node (), b1 = c.Node (), o1 = c.Node ();
	c.R (in, n1, 10.0e3);
	c.C (n1, b1, 0.1e-6);
	c.R (b1, vb, 1.0e6);
	c.Opamp (b1, o1, o1, vb, 3.0e6, 2.0e4, 4.0e7, 3.0, 50.0);
	// network 2 (input): R5 // C4 towards node s, then the Gain potentiometer (second section)
	const int s = c.Node (), w = c.Node (), q = c.Node ();
	c.R (o1, s, 5.1e3);
	c.C (o1, s, 68.0e-9);
	c.R (s, vb, 1.5e3);					// R8
	c.C (s, q, 390.0e-9);					// C6
	c.R (q, vb, 1.0e3);					// R9
	h[2] = c.R (s, w, 50.0e3);				// RV-GAIN_b: from the top to the wiper
	h[3] = c.R (w, vb, 50.0e3);				// RV-GAIN_b: from the wiper to 4.5 V
	// gain stage
	const int a = c.Node (), b = c.Node (), g = c.Node (), f = c.Node (), o2 = c.Node (), pw = c.Node ();
	c.C (o1, a, 0.1e-6);					// C3
	c.C (a, b, 68.0e-9);					// C5
	c.R (a, b, 10.0e3);					// R6
	h[4] = c.R (b, vb, 50.0e3);				// RV-GAIN_a: from the top (b) to the wiper (4.5 V)
	h[5] = c.R (vb, pw, 50.0e3);				// RV-GAIN_a: from the wiper to the bottom
	c.R (pw, g, 2.0e3);					// R10
	c.R (g, f, 15.0e3);					// R11
	c.C (g, f, 82.0e-9);					// C7
	c.Opamp (b, f, o2, vb, 3.0e6, 2.0e4, 4.0e7, 3.0, 50.0);
	c.R (f, o2, 422.0e3);					// R12
	c.C (f, o2, 390.0e-12);					// C8
	// stage output: C9, R13, diodes to ground, C10
	const int c9o = c.Node (), d = c.Node (), c10r = c.Node ();
	c.C (o2, c9o, 1.0e-6);
	c.R (c9o, d, 1.0e3);
	if (di == 0) c.DiodePair (d, 0, 3.0e-8, 1.3 * kVt);
	else if (di == 1) c.DiodePair (d, 0, 2.52e-9, 1.752 * kVt);
	else c.DiodePair (d, 0, 1.0e-18, 1.9 * kVt);
	c.C (d, c10r, 1.0e-6);
	// network 1: from node a (after C3) with R7, C16 to ground and R19 towards the summer
	const int n7 = c.Node (), X = c.Node ();
	c.R (a, n7, 1.5e3);
	c.C (n7, 0, 1.0e-6);
	c.R (n7, X, 15.0e3);
	// network 2 (output): from wiper w towards node c10r (C11 + R15) and towards the summer (R17 // C12 + R18)
	const int p15 = c.Node (), p18 = c.Node ();
	c.C (w, p15, 2.2e-9);
	c.R (p15, c10r, 22.0e3);
	c.R (w, X, 27.0e3);
	c.C (w, p18, 27.0e-9);
	c.R (p18, X, 12.0e3);
	c.R (c10r, X, 47.0e3);					// R16
	// summer: inverting TL072 powered at +16.2 / -8.6 V
	const int o3 = c.Node ();
	c.Opamp (vb, X, o3, vb, 3.0e6, 2.0e4, 4.0e7, 10.2, 50.0, 11.6);
	c.R (X, o3, 392.0e3);					// R20
	c.C (X, o3, 820.0e-12);					// C13
	// tone: active shelving
	const int m = c.Node (), o4 = c.Node (), pbot = c.Node (), pwp = c.Node (), ptop = c.Node (), cw = c.Node ();
	c.R (o3, m, 100.0e3);					// R22
	c.R (o3, pbot, 1.8e3);					// R21
	h[6] = c.R (pbot, pwp, 5.0e3);				// RV2 Treble: from the bottom to the wiper
	h[7] = c.R (pwp, ptop, 5.0e3);				// RV2: from the wiper to the top
	c.R (ptop, o4, 4.7e3);					// R23
	c.C (pwp, cw, 3.9e-9);					// C14
	c.R (cw, m, 1.0);
	c.R (m, o4, 100.0e3);					// R24
	c.Opamp (vb, m, o4, vb, 3.0e6, 2.0e4, 4.0e7, 10.2, 50.0, 11.6);
	// output: C15, R25, Output 10k; direct signal with the "anti-pop" resistors
	const int n15 = c.Node (), lt = c.Node (), lw = c.Node (), out = c.Node ();
	c.C (o4, n15, 4.7e-6);
	c.R (n15, lt, 560.0);
	h[8] = c.R (lt, lw, 5.0e3);				// RV3 Output: from the top to the wiper
	h[9] = c.R (lw, 0, 5.0e3);
	c.R (lw, out, 1.0);					// SW1 closed: wiper on the output
	const int dn = c.Node (), dr = c.Node ();
	c.C (o1, dn, 4.7e-6);					// C2
	c.R (dn, vb, 100.0e3);					// R3
	c.R (dn, dr, 560.0);					// R4
	c.R (dr, out, 68.0e3);					// R243
	c.R (out, 0, 100.0e3);					// R28
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[1] = out;
	h[11] = o2; h[12] = d; h[13] = o3; h[14] = o4; h[15] = vb;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double g = p[0] * 0.1, tr = p[1] * 0.1, ou = p[2] * 0.1;
	g = g < 0 ? 0 : (g > 1 ? 1 : g); tr = tr < 0 ? 0 : (tr > 1 ? 1 : tr); ou = ou < 0 ? 0 : (ou > 1 ? 1 : ou);
	// Gain (two sections on the same shaft): clockwise in the first the wiper goes towards the bottom (less resistance to ground: more gain), in the second towards 4.5 V (network 2 weaker)
	c.SetR (h[4], g * 100.0e3 + 1.0); c.SetR (h[5], (1.0 - g) * 100.0e3 + 1.0);
	c.SetR (h[2], g * 100.0e3 + 1.0); c.SetR (h[3], (1.0 - g) * 100.0e3 + 1.0);
	// Treble: clockwise the wiper goes towards the bottom (input side): more treble
	c.SetR (h[6], (1.0 - tr) * 10.0e3 + 1.0); c.SetR (h[7], tr * 10.0e3 + 1.0);
	// Output: clockwise the wiper goes towards the top
	c.SetR (h[8], (1.0 - ou) * 10.0e3 + 1.0); c.SetR (h[9], ou * 10.0e3 + 1.0);
}

const TPedal kPedal = {FxCentaur, Build, Pots, 4, 1u << 3, 1.0f / CKT_VFS};

}

CFx *NewKlon (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void KlonBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void KlonPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
