//
// fx_tonestack.cpp - Tone Stack Real: the passive Bass / Mid / Treble network of the Fenders and Marshalls (circuit of the '59 Bassman 5F6-A), simulated from the NETLIST (ckt.h) with the component values of five amplifiers.
//
// Fonti (2026-10-19):
//  - Topology: derived from the formula of the first coefficient of the transfer function's numerator, b1 = t C1 R1 + m C3 R3 + l (C1 R2 + C2 R2) + C1 R3 + C2 R3 (D. T. Yeh, J. O. Smith,
//    "Discretization of the '59 Fender Bassman Tone Stack", DAFx-06, in the Faust code tonestacks.lib of the faustlibraries library, LGPL 2.1 license, values from the Guitarix project); the network: C1 from the input to the
//    top of the treble potentiometer (R1), whose wiper is the output; the bottom of R1 goes to capacitor C2 (bass) and to the series of the bass potentiometer (R2, l * R2) and the mid one (R3, to ground);
//    R4 ("slope" resistor) from the input to the common node of C2 and C3; C3 (mid) from the wiper node of R3. Statement by the author (https://ccrma.stanford.edu/~dtyeh/tonestack/): "most
//    tone stacks (Fender, Marshall, Vox) are the same circuit with different values".
//  - Values (R1 treble, R2 bass, R3 mid, R4, C1, C2, C3): Bassman 5F6-A 250k 1M 25k 56k 250 pF 20 nF 20 nF; Twin Reverb 250k 250k 10k 100k 120 pF 100 nF 47 nF; Princeton 250k 250k 4.8k 100k 250 pF 100 nF 47 nF;
//    JTM45 250k 1M 25k 33k 270 pF 22 nF 22 nF; JCM800 220k 1M 22k 33k 470 pF 22 nF 22 nF (faustlibraries, tonestacks.lib, https://faustlibraries.grame.fr/libs/tonestacks/).
//  - Taper: bass exponential, l = e^((B-1) 3.4) with B in 0..1; mid and treble linear (same source).
//
// Model: linear circuit (no nonlinear ports), 1 Mohm load (volume potentiometer and the following stage's grid); ideal source or 38 kohm (100k plate resistor in
// parallel with the 12AX7's internal resistance, about 62k: my estimate, not in the source). The network loses 15-20 dB: the Level parameter recovers it as the following stage would (default +16 dB, my choice, not
// measured). Not verified against recordings of the real amplifier. The circuit reproduces b1 within 1% (test_tonestack); R4 does not appear in b1, so its position (from the input to the common node of C2 and C3,
// as in the Fender schematic) is not confirmed by that formula and remains to be checked against the curves of a tone stack calculator or a capture. Mid notch with Mid 0, Bass and Treble 10:
// Bassman 612 Hz, Twin 418 Hz, Princeton 285 Hz, JTM45 740 Hz, JCM800 612 Hz (computed from the model, not compared with sources).
//
#include "fxckt.h"

namespace
{

// R1 treble, R2 bass, R3 mid, R4, C1, C2, C3
struct TAmp { double r1, r2, r3, r4, c1, c2, c3; };
const TAmp kAmp[5] = {
	{250.0e3, 1.0e6, 25.0e3, 56.0e3, 250.0e-12, 20.0e-9, 20.0e-9},
	{250.0e3, 250.0e3, 10.0e3, 100.0e3, 120.0e-12, 100.0e-9, 47.0e-9},
	{250.0e3, 250.0e3, 4.8e3, 100.0e3, 250.0e-12, 100.0e-9, 47.0e-9},
	{250.0e3, 1.0e6, 25.0e3, 33.0e3, 270.0e-12, 22.0e-9, 22.0e-9},
	{220.0e3, 1.0e6, 22.0e3, 33.0e3, 470.0e-12, 22.0e-9, 22.0e-9}};

// p: 0 = bass, 1 = mid, 2 = treble (0..10), 3 = amplifier, 4 = source, 5 = Level (dB)
// handles: 0 input, 1 output, 2 source, 3 R1 top, 4 R1 bottom, 5 R2 series, 6 R3 top, 7 R3 bottom, 8 and 9 measurement nodes
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int ai = (int) (pp[3] + 0.5f);
	ai = ai < 0 ? 0 : (ai > 4 ? 4 : ai);
	const TAmp &A = kAmp[ai];
	const int ix = c.Node (), in = c.Node (), a = c.Node (), w = c.Node (), b = c.Node (), d = c.Node (), x = c.Node (), y = c.Node ();
	h[0] = c.V (ix, 0, 0.0);
	h[2] = c.R (ix, in, 1.0);
	c.C (in, a, A.c1);
	h[3] = c.R (a, w, 0.5 * A.r1);			// (1 - t) R1
	h[4] = c.R (w, b, 0.5 * A.r1);			// t R1
	c.R (in, d, A.r4);
	c.C (d, b, A.c2);
	h[5] = c.R (b, x, 0.5 * A.r2);			// l R2 (the remaining part of the potentiometer is not connected)
	h[6] = c.R (x, y, 0.5 * A.r3);			// (1 - m) R3
	h[7] = c.R (y, 0, 0.5 * A.r3);			// m R3
	c.C (d, y, A.c3);
	h[10] = c.R (w, 0, 1.0e6);				// load
	c.SetOut (w);
	h[1] = w;
	h[8] = b; h[9] = y;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	int ai = (int) (p[3] + 0.5f);
	ai = ai < 0 ? 0 : (ai > 4 ? 4 : ai);
	const TAmp &A = kAmp[ai];
	double bs = p[0] * 0.1, md = p[1] * 0.1, tr = p[2] * 0.1;
	bs = bs < 0 ? 0 : (bs > 1 ? 1 : bs); md = md < 0 ? 0 : (md > 1 ? 1 : md); tr = tr < 0 ? 0 : (tr > 1 ? 1 : tr);
	const double l = (double) Ck::Exp ((float) ((bs - 1.0) * 3.4));
	c.SetR (h[3], (1.0 - tr) * A.r1 + 1.0); c.SetR (h[4], tr * A.r1 + 1.0);
	c.SetR (h[5], l * A.r2 + 1.0);
	c.SetR (h[6], (1.0 - md) * A.r3 + 1.0); c.SetR (h[7], md * A.r3 + 1.0);
	c.SetR (h[2], p[4] > 0.5f ? 38.0e3 : 1.0);
}

const TPedal kPedal = {FxToneStack, Build, Pots, 6, (1u << 3), 1.0f / CKT_VFS, 6};

}

CFx *NewToneStack (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void ToneStackBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void ToneStackPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
