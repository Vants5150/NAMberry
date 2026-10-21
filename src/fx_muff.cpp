//
// fx_muff.cpp - Big Muff Pi Real: Electro-Harmonix Big Muff Pi in three historical versions, simulated from the circuit's NETLIST (ckt.h) with the physical pedal's controls (Sustain, Tone, Volume).
//
// Sources (2026-10-19): The Big Muff Pi Page (Kit Rae), schematics traced from the real circuits (https://www.bigmuffpage.com/Big_Muff_Pi_versions_schematics_part1.html and _part2.html):
//  - "Version 3 BIG MUFF - V3 76#3" (1976, NPN BC239): R2 39k, R14 47k, R9 470k + C10 470 pF, R13 15k, R22 100; Sustain 100k linear with R23 1k; C5 0.1 uF, R19 10k; two clipping stages (R20/R16 100k,
//    feedback 470k + 470 pF and C6/C7 1 uF with two 1N914s in antiparallel, R18/R11 15k, R21/R10 150); C13 0.1 uF and R12 10k; Tone: R8 39k + C8 10 nF, C9 4 nF + R5 22k; output R7 430k, R3 100k, R6 15k, R4 3.3k.
//  - "Version 2 BIG MUFF - RAM'S HEAD 74#3" (1974-75, NPN FS36999): R2 33k, C1 10 uF, R14 100k, R9 470k + 220 pF, R13 12k, R22 100, R23 820, C4/C5/C6/C7/C3 0.12 uF, R19/R12 10k, clipping stages with
//    100k/470k + 680 pF and 12k, 150; Tone: R8 33k + C8 6.8 nF, C9 4 nF + R5 33k; output R7 390k, R3 100k, R6 12k, R4 3.3k, C2 10 uF.
//  - "Version 1 BIG MUFF - TRIANGLE 72#2" (1972-73, PNP FS37000, positive to ground): R2 33k, C1 0.1 uF, R14 82k, R9 390k + 560 pF, R13 22k, R22 82, R23 820, C4/C5/C13/C3/C2 0.1 uF, R19/R12 8.2k, stages of
//    clipping with 82k/390k + 560 pF, C6/C7 0.05 uF, R18 12k and R11 22k, 150 and 82; Tone: R8 39k + 10 nF, C9 4 nF + R5 39k; output R7 390k, R3 100k, R6 12k, R4 2.7k.
//  - ToneHome (https://www.tonehome.de/electro-harmonix/big-muff-pi/ and .../little-big-muff-pi/), two EH reference schematics downloaded by the user on 2026-10-19: "Big Muff Pi, old model, Oct. 15/75" (version 3
//    of the enum) and "Little Big Muff Pi" (version 4, no Sustain): the values read are in the comments of the kVer table. In the 1975 schematic Q2 has no resistor from the base to ground: in the model it self-biases
//    from R15 with the collector at about 1.3 V (close to saturation), as drawn; Q3's emitter (R21) is not marked and is 100 ohm by assumption.
//  - ElectroSmash, Big Muff Pi analysis (archived copy, https://electrosmash.com/big-muff-pi-analysis), for the V3: gain of the two clipping stages 23.4 dB and 25.2 dB at maximum, Tone scoop at 1 kHz.
//
// Model: for every transistor Ebers-Moll (Is 1e-14, beta 400 for the V3 and 300 for the others, reverse 5), 1N914 diodes (Is 2.52 nA, n 1.752), no parasitic capacitance of the transistors. Beta is a typical
// value (FS36999 and FS37000 have no data: company markings): in real pedals it varies a lot from one unit to another and shifts the bias. All the potentiometers are 100k linear as in the schematics.
//
#include "fxckt.h"

namespace
{

const double kVt = 0.02585;
const double kIs = 1.0e-14, kBr = 5.0;
const double kDIs = 2.52e-9, kDn = 1.752 * kVt;
const double kPot = 100.0e3;

struct TVer
{
	bool pnp;
	double bf;
	double c1, r2, r14, r9, c10, r13, r22, c4, r23, c5, r19;			// input and Q4 stage, Sustain
	double r20, r17, c12, c6, r18, r21, c13, r12;				// Q3
	double r16, r15, c11, c7, r11, r10;						// Q2
	double r5, c9, r8, c8, c3;							// Tone
	double r7, r3, r6, r4, c2;							// Q1 and output
};

const TVer kVer[5] =
{
	// V3 1976
	{false, 400.0, 1.0e-6, 39e3, 47e3, 470e3, 470e-12, 15e3, 100.0, 1.0e-6, 1e3, 0.1e-6, 10e3,
	 100e3, 470e3, 470e-12, 1.0e-6, 15e3, 150.0, 0.1e-6, 10e3,
	 100e3, 470e3, 470e-12, 1.0e-6, 15e3, 150.0,
	 22e3, 4e-9, 39e3, 10e-9, 0.1e-6,
	 430e3, 100e3, 15e3, 3.3e3, 0.1e-6},
	// V2 Ram's Head 1974
	{false, 300.0, 10.0e-6, 33e3, 100e3, 470e3, 220e-12, 12e3, 100.0, 0.12e-6, 820.0, 0.12e-6, 10e3,
	 100e3, 470e3, 680e-12, 0.12e-6, 12e3, 150.0, 10.0e-6, 10e3,
	 100e3, 470e3, 680e-12, 0.12e-6, 12e3, 150.0,
	 33e3, 4e-9, 33e3, 6.8e-9, 0.12e-6,
	 390e3, 100e3, 12e3, 3.3e3, 10.0e-6},
	// V1 Triangle 1972 (PNP, positive to ground)
	{true, 300.0, 0.1e-6, 33e3, 82e3, 390e3, 560e-12, 22e3, 82.0, 0.1e-6, 820.0, 0.1e-6, 8.2e3,
	 82e3, 390e3, 560e-12, 0.05e-6, 12e3, 150.0, 0.1e-6, 8.2e3,
	 82e3, 390e3, 560e-12, 0.05e-6, 22e3, 82.0,
	 39e3, 4e-9, 39e3, 10e-9, 0.1e-6,
	 390e3, 100e3, 12e3, 2.7e3, 0.1e-6},
	// EH "old model" 15 October 1975 (EH3003 reference schematic on tonehome.de: R2 39k, C1 1 uF, R14 100k, R9 470k + C10 500 pF, R13 15k, R22 100, C4 1 uF, Sustain 100k with R1 1k, C5 0.1 uF and R19 22k,
	// Q3: R20 100k, R17 470k + C12 500 pF, C6 0.1 uF with the diodes, R18 15k, emitter R21 not marked (assumed 100 ohm like R10 and R22), C3 1 uF and R12 8.2k, Q2 without a resistor from the base to ground (self-biased by R15 470k),
	// C11 500 pF, C7 0.1 uF, R11 15k, R10 100; Tone R8 39k + C8 10 nF, C9 4 nF + R5 22k; output R7 390k, R3 100k, C3 0.1 uF, R6 10k, R4 2.2k, C2 1 uF; 2N5088 or BC239C)
	{false, 400.0, 1.0e-6, 39e3, 100e3, 470e3, 500e-12, 15e3, 100.0, 1.0e-6, 1e3, 0.1e-6, 22e3,
	 100e3, 470e3, 500e-12, 0.1e-6, 15e3, 100.0, 1.0e-6, 8.2e3,
	 1.0e12, 470e3, 500e-12, 0.1e-6, 15e3, 100.0,
	 22e3, 4e-9, 39e3, 10e-9, 0.1e-6,
	 390e3, 100e3, 10e3, 2.2e3, 1.0e-6},
	// Little Big Muff (EH reference schematic on tonehome.de, read upside down: input C 10 uF, R 33k, base with 43k to ground, 100 ohm emitter, 8.2k collector, 470k + 560 pF; coupling 0.1 uF and
	// 10k in series; clipping 1: base 100k, emitter 150, collector 10k, 470k + 560 pF and 0.1 uF with the diodes; 0.1 uF and 8.2k in series; clipping 2: base 100k, 150, collector 15k; Tone 39k + 10 nF, 4 nF + 22k, 100k;
	// output 430k and 100k to the base, 6.8k emitter, 15k collector, 0.1 uF; no Sustain control: fixed gain; Volume 100k)
	{false, 400.0, 10.0e-6, 33e3, 43e3, 470e3, 560e-12, 8.2e3, 100.0, 0.1e-6, 1e6, 1.0e-3, 10e3,
	 100e3, 470e3, 560e-12, 0.1e-6, 10e3, 150.0, 0.1e-6, 8.2e3,
	 100e3, 470e3, 560e-12, 0.1e-6, 15e3, 150.0,
	 22e3, 4e-9, 39e3, 10e-9, 0.1e-6,
	 430e3, 100e3, 15e3, 6.8e3, 0.1e-6}
};

void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int vi = (int) (pp[3] + 0.5f);
	vi = vi < 0 ? 0 : (vi > 4 ? 4 : vi);
	const TVer &V = kVer[vi];
	const bool pn = V.pnp;
	const int vcc = c.Node (), rail = c.Node (), in = c.Node ();
	c.V (vcc, 0, pn ? -9.0 : 9.0);
	h[0] = c.V (in, 0, 0.0);
	c.R (vcc, rail, 100.0);
	c.C (rail, 0, 430.0e-6);
	// Q4: input stage
	const int n1 = c.Node (), b4 = c.Node (), c4 = c.Node (), e4 = c.Node (), s = c.Node (), w = c.Node (), g1 = c.Node ();
	c.C (in, n1, V.c1);
	c.R (n1, b4, V.r2);
	c.R (b4, 0, V.r14);
	c.Bjt (c4, b4, e4, kIs, V.bf, kBr, kVt, pn, true);
	c.R (e4, 0, V.r22);
	c.R (rail, c4, V.r13);
	c.R (c4, b4, V.r9);
	c.C (c4, b4, V.c10);
	c.C (c4, s, V.c4);
	// Sustain 100k linear, R23
	h[2] = c.R (s, w, 50.0e3);
	h[3] = c.R (w, g1, 50.0e3);
	c.R (g1, 0, V.r23);
	// Q3: first clipping
	const int n5 = c.Node (), b3 = c.Node (), c3 = c.Node (), e3 = c.Node (), d3 = c.Node ();
	c.C (w, n5, V.c5);
	c.R (n5, b3, V.r19);
	c.R (b3, 0, V.r20);
	c.Bjt (c3, b3, e3, kIs, V.bf, kBr, kVt, pn, true);
	c.R (e3, 0, V.r21);
	c.R (rail, c3, V.r18);
	c.R (c3, b3, V.r17);
	c.C (c3, b3, V.c12);
	c.C (c3, d3, V.c6);
	c.DiodePair (d3, b3, kDIs, kDn);
	// Q2: second clipping
	const int n13 = c.Node (), b2 = c.Node (), c2 = c.Node (), e2 = c.Node (), d2 = c.Node ();
	c.C (c3, n13, V.c13);
	c.R (n13, b2, V.r12);
	c.R (b2, 0, V.r16);
	c.Bjt (c2, b2, e2, kIs, V.bf, kBr, kVt, pn, true);
	c.R (e2, 0, V.r10);
	c.R (rail, c2, V.r11);
	c.R (c2, b2, V.r15);
	c.C (c2, b2, V.c11);
	c.C (c2, d2, V.c7);
	c.DiodePair (d2, b2, kDIs, kDn);
	// Tone: R8 + C8 (low-pass), C9 + R5 (high-pass), 100k potentiometer between the two
	const int tl = c.Node (), th = c.Node (), tw = c.Node ();
	c.R (c2, tl, V.r8);
	c.C (tl, 0, V.c8);
	c.C (c2, th, V.c9);
	c.R (th, 0, V.r5);
	h[4] = c.R (tl, tw, 50.0e3);
	h[5] = c.R (tw, th, 50.0e3);
	// Q1: output stage
	const int b1 = c.Node (), c1 = c.Node (), e1 = c.Node (), vo = c.Node (), wv = c.Node ();
	c.C (tw, b1, V.c3);
	c.R (rail, b1, V.r7);
	c.R (b1, 0, V.r3);
	c.Bjt (c1, b1, e1, kIs, V.bf, kBr, kVt, pn, true);
	c.R (e1, 0, V.r4);
	c.R (rail, c1, V.r6);
	c.C (c1, vo, V.c2);
	h[6] = c.R (vo, wv, 50.0e3);
	h[7] = c.R (wv, 0, 50.0e3);
	c.R (wv, 0, 1.0e6);		// amplifier input
	c.SetOut (wv);
	h[1] = wv;
	h[8] = c4; h[9] = c3; h[10] = c2; h[11] = c1;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double s = p[0] * 0.1, t = p[1] * 0.1, v = p[2] * 0.1;
	s = s < 0 ? 0 : (s > 1 ? 1 : s);
	t = t < 0 ? 0 : (t > 1 ? 1 : t);
	v = v < 0 ? 0 : (v > 1 ? 1 : v);
	// Sustain: clockwise the wiper goes towards the signal; the Little Big Muff (version 4) does not have the control: the signal passes straight through
	if (p[3] > 3.5f) { c.SetR (h[2], 1.0); c.SetR (h[3], 1.0e9); }
	else { c.SetR (h[2], (1.0 - s) * kPot + 1.0); c.SetR (h[3], s * kPot + 1.0); }
	// Tone: clockwise towards the high-pass (th), to the left towards the low-pass (tl)
	c.SetR (h[4], t * kPot + 1.0); c.SetR (h[5], (1.0 - t) * kPot + 1.0);
	c.SetR (h[6], (1.0 - v) * kPot + 1.0); c.SetR (h[7], v * kPot + 1.0);
}

const TPedal kPedal = {FxMuff, Build, Pots, 4, 1u << 3, 1.0f / CKT_VFS};

}

CFx *NewMuff (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

// for tests: internal nodes of the circuit (p[3] = version)
void MuffBuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void MuffPotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
