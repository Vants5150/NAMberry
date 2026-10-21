//
// fx_guvnor.cpp - Guv'nor Real: Marshall The Guv'nor (1988), simulated from the NETLIST (ckt.h) with the physical pedal's controls (Gain, Bass, Mids, Treble, Level) and the choice of the clipping diodes.
//
// Sources (2026-10-19): ElectroSmash, Marshall The Guvnor analysis (archived copy, https://electrosmash.com/marshall-guvnor-analysis), schematic and values: 9 V supply with two 47k for the 4.5 V and C14 10 uF;
// input with R1 2.2M to ground, C1 9.6 nF and R2 1M towards 4.5 V; first TL072 non-inverting with feedback from the Gain potentiometer VR1 100k in parallel with C2 120 pF and return to ground with R3 2.2k + C3 100 nF
// (gain from 1 to 46.45, that is up to 33.3 dB); second TL072 inverting: C4 220 nF, R4 10k, C5 100 nF at the input, feedback R5 680k with C6 220 pF (gain -68, 36 dB), output C7 220 nF, R6 1k and two red
// LEDs in antiparallel to ground (Vf 1.8-2 V); passive tone stack with three 10k potentiometers: R7 1.5k, C9 4.7 nF, C8 100 nF, R8 680, VR2 Bass, R9 680, C10 220 nF, VR3 Mids with R10 100 and C11 10 nF,
// VR4 Treble, C12 68 nF; output: VR5 Level 100k, R11 22k and C13 470 pF. Total gain about 70 dB, the tone stack attenuates by 10-20 dB.
//
// Model: TL072 as a macromodel (gain-bandwidth product 3 MHz, rails at +-3 V around the 4.5 V as for a 9 V supply); red LED Is 1e-18, n 1.9 (like the RAT with LEDs), 1N4148 and a
// germanium (Vf 0.3 V) as alternatives documented by ElectroSmash. Potentiometers considered linear (the taper is not indicated). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kVt = 0.02585;

void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int di = (int) (pp[5] + 0.5f);
	di = di < 0 ? 0 : (di > 2 ? 2 : di);
	const int v9 = c.Node (), vb = c.Node (), in = c.Node ();
	c.V (v9, 0, 9.0);
	c.R (v9, vb, 47.0e3);
	c.R (vb, 0, 47.0e3);
	c.C (vb, 0, 10.0e-6);
	h[0] = c.V (in, 0, 0.0);
	c.R (in, 0, 2.2e6);					// R1
	// first stage: non-inverting
	const int ip = c.Node (), m = c.Node (), n3 = c.Node (), o1 = c.Node ();
	c.C (in, ip, 9.6e-9);
	c.R (ip, vb, 1.0e6);
	c.R (m, n3, 2.2e3);
	c.C (n3, 0, 100.0e-9);
	c.Opamp (ip, m, o1, vb, 3.0e6, 2.0e4, 4.0e7, 3.0, 50.0);
	h[2] = c.R (m, o1, 50.0e3);				// VR1: feedback resistance = x
	c.C (m, o1, 120.0e-12);					// C2
	// second stage: inverting
	const int a = c.Node (), n6 = c.Node (), o2 = c.Node (), n4 = c.Node ();
	c.C (o1, n4, 220.0e-9);
	c.R (n4, a, 10.0e3);
	c.C (a, n6, 100.0e-9);
	c.R (a, 0, 1.0e9);					// node a is isolated in DC: a very high resistance (capacitor leakage) keeps it defined
	c.Opamp (vb, n6, o2, vb, 3.0e6, 2.0e4, 4.0e7, 3.0, 50.0);
	c.R (n6, o2, 680.0e3);
	c.C (n6, o2, 220.0e-12);
	// clipping to ground
	const int d1 = c.Node (), tin = c.Node ();
	c.C (o2, d1, 220.0e-9);
	c.R (d1, tin, 1.0e3);
	if (di == 0) c.DiodePair (tin, 0, 1.0e-18, 1.9 * kVt);
	else if (di == 1) c.DiodePair (tin, 0, 2.52e-9, 1.752 * kVt);
	else c.DiodePair (tin, 0, 1.3e-7, 1.3 * kVt);
	// tone stack
	const int P = c.Node (), M = c.Node (), T = c.Node (), B = c.Node (), vw = c.Node (), v3 = c.Node (), v3b = c.Node (), tw = c.Node ();
	c.C (tin, T, 4.7e-9);					// C9
	c.R (tin, P, 1.5e3);					// R7
	c.R (P, M, 680.0);					// R9
	c.C (P, B, 100.0e-9);					// C8
	c.R (B, 0, 680.0);					// R8
	h[3] = c.R (M, B, 5.0e3);				// VR2 Bass: between M and the wiper (B)
	h[4] = c.R (B, 0, 5.0e3);				// VR2: between the wiper and ground
	c.C (P, vw, 220.0e-9);					// C10
	h[5] = c.R (vw, v3, 5.0e3);				// VR3 Mids: from the wiper to the top (R10, C11)
	h[6] = c.R (vw, 0, 5.0e3);				// VR3: from the wiper to ground
	c.R (v3, v3b, 100.0);					// R10
	c.C (v3b, T, 10.0e-9);					// C11
	h[7] = c.R (T, tw, 5.0e3);				// VR4 Treble: from the top T to the wiper
	h[8] = c.R (tw, M, 5.0e3);				// VR4: from the wiper to M
	c.C (M, 0, 68.0e-9);					// C12
	// level and output
	const int lw = c.Node (), o = c.Node (), out = c.Node ();
	h[9] = c.R (tw, lw, 50.0e3);				// VR5 Level 100k: from the top to the wiper
	h[10] = c.R (lw, 0, 50.0e3);
	c.R (lw, o, 22.0e3);					// R11
	c.C (o, 0, 470.0e-12);					// C13
	c.R (o, out, 1.0);
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[1] = out;
	h[11] = o1; h[12] = o2; h[13] = tin; h[14] = vb; h[15] = tw;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double g = p[0] * 0.1, bs = p[1] * 0.1, md = p[2] * 0.1, tr = p[3] * 0.1, lv = p[4] * 0.1;
	g = g < 0 ? 0 : (g > 1 ? 1 : g); bs = bs < 0 ? 0 : (bs > 1 ? 1 : bs); md = md < 0 ? 0 : (md > 1 ? 1 : md);
	tr = tr < 0 ? 0 : (tr > 1 ? 1 : tr); lv = lv < 0 ? 0 : (lv > 1 ? 1 : lv);
	c.SetR (h[2], g * 100.0e3 + 1.0);
	// Bass: clockwise the wiper goes towards ground (C8 and R8 load the highs: more bass, verified in testing)
	c.SetR (h[3], bs * 10.0e3 + 1.0); c.SetR (h[4], (1.0 - bs) * 10.0e3 + 1.0);
	// Mids: clockwise the wiper goes towards the top (R10, C11): more mids
	c.SetR (h[5], (1.0 - md) * 10.0e3 + 1.0); c.SetR (h[6], md * 10.0e3 + 1.0);
	// Treble: clockwise the wiper goes towards the top T (C9)
	c.SetR (h[7], (1.0 - tr) * 10.0e3 + 1.0); c.SetR (h[8], tr * 10.0e3 + 1.0);
	c.SetR (h[9], (1.0 - lv) * 100.0e3 + 1.0); c.SetR (h[10], lv * 100.0e3 + 1.0);
}

const TPedal kPedal = {FxGuv, Build, Pots, 6, 1u << 5, 1.0f / CKT_VFS};

}

CFx *NewGuvnor (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void GuvBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void GuvPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
