//
// fx_ds1.cpp - DS-1 Real: Boss DS-1 Distortion (DS1-A circuit, from 1994: op-amp in place of the TA7136AP), simulated from the NETLIST (ckt.h) with the physical pedal's controls (Dist, Tone, Level).
//
// Sources (2026-10-19): ElectroSmash, Boss DS-1 Distortion analysis (archived copy, https://electrosmash.com/boss-ds1-analysis), schematic and parts list: input buffer as an emitter follower
// (R1 1k, C1 47 nF, R2 470k towards 4.5 V, Q1 2SC2240, R3 10k); transistor booster (C2 0.47 uF with R4 100k towards 4.5 V, C3 47 nF with R5 100k to ground, Q2 with R8 10k collector load, R9 22 ohm emitter,
// feedback R7 470k with C4 250 pF; gain about 35 dB); op-amp stage: C5 68 nF, R10 100k, R39 47k, D8 1N414 protection, first half-op-amp as a buffer, second non-inverting with
// R11 100k, C7 100 pF, Distortion potentiometer VR1 100kB (with R13 4.7k and C8 0.47 uF to ground: gain from 1 to 22.3), then R14 2.2k, C9 0.47 uF, two 1N4148s in antiparallel towards 4.5 V and C10 10 nF to ground
// (cutoff at 7.2 kHz); passive Big Muff-style tone/level: R16 6.8k + C12 0.1 uF (low-pass), C11 22 nF + R15 2.2k and R17 6.8k (high-pass), VR3 Tone 20kB, VR2 Level 100kB, R18 10k; output buffer like
// the input one (C13 47 nF, R19/R20 1M, R21 10k, C14 1 uF, R22 1k, R23 100k); 9 V supply with D1 1N4004 against reversal, R24/R25 10k for the 4.5 V and C15 47 uF.
//
// Model: Ebers-Moll (Is 1e-14, beta 400, reverse 5) for the 2SC2240s, 1N4148 (Is 2.52 nA, n 1.752), 1N4004 (Is 14 nA, n 1.98), NJM2904L op-amp with gain-bandwidth product 0.7 MHz, slew rate
// 0.4 V/us (not modelled: at 48 kHz it would be 8 V per sample, that is never reached, and it would make the calculation unstable), swing +-3.3 V around the 4.25 V (the real data are those of the datasheets: not measured). The potentiometers are linear (B). The 1978 original TA7136AP is not modelled (it is not an
// op-amp and I do not have its schematic). Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kVt = 0.02585;
const double kIs = 1.0e-14, kBf = 400.0, kBr = 5.0;
const double kDIs = 2.52e-9, kDn = 1.752 * kVt;

void Build (Ck::TCkt &c, int *h, const float *pp)
{
	(void) pp;
	const int vbat = c.Node (), rail = c.Node (), vb = c.Node (), in = c.Node ();
	c.V (vbat, 0, 8.35);		// 9 V minus the drop of D1 1N4004 (about 0.65 V at ten or so mA)
	c.R (vbat, rail, 1.0);
	c.C (rail, 0, 100.0e-6);
	c.R (rail, vb, 10.0e3);
	c.R (vb, 0, 10.0e3);
	c.C (vb, 0, 47.0e-6);
	h[0] = c.V (in, 0, 0.0);
	// input buffer
	const int n1 = c.Node (), b1 = c.Node (), e1 = c.Node ();
	c.R (in, n1, 1.0e3);
	c.C (n1, b1, 47.0e-9);
	c.R (b1, vb, 470.0e3);
	c.Bjt (rail, b1, e1, kIs, kBf, kBr, kVt, false, true);
	c.R (e1, 0, 10.0e3);
	// booster a transistor
	const int n2 = c.Node (), b2 = c.Node (), c2 = c.Node (), e2 = c.Node ();
	c.C (e1, n2, 0.47e-6);
	c.R (n2, vb, 100.0e3);
	c.C (n2, b2, 47.0e-9);
	c.R (b2, 0, 100.0e3);
	c.Bjt (c2, b2, e2, kIs, kBf, kBr, kVt, false, false);
	c.R (rail, c2, 10.0e3);
	c.R (e2, 0, 22.0);
	c.R (c2, b2, 470.0e3);
	c.C (c2, b2, 250.0e-12);
	// op-amp
	const int na = c.Node (), ap = c.Node (), u1o = c.Node (), bn = c.Node (), x = c.Node (), u2o = c.Node (), gt = c.Node (), gc = c.Node ();
	c.C (c2, na, 68.0e-9);
	c.R (na, vb, 100.0e3);
	c.R (na, ap, 47.0e3);
	c.Opamp (ap, u1o, u1o, vb, 0.7e6, 5.0e3, 4.0e7, 3.3, 50.0);
	c.Opamp (u1o, bn, u2o, vb, 0.7e6, 5.0e3, 4.0e7, 3.3, 50.0);
	c.R (bn, x, 100.0e3);				// R11
	c.C (x, u2o, 100.0e-12);				// C7
	h[2] = c.R (u2o, x, 50.0e3);			// VR1 part between the output and the wiper
	h[3] = c.R (x, gt, 50.0e3);			// VR1 part between the wiper and R13
	c.R (gt, gc, 4.7e3);
	c.C (gc, 0, 0.47e-6);
	// diode clipping towards 4.5 V
	const int nr = c.Node (), d = c.Node ();
	c.R (u2o, nr, 2.2e3);
	c.C (nr, d, 0.47e-6);
	c.DiodePair (d, vb, kDIs, kDn);
	c.C (d, 0, 10.0e-9);
	// tone / level
	const int p = c.Node (), q = c.Node (), cr = c.Node (), tw = c.Node (), lw = c.Node (), lo = c.Node ();
	c.R (d, p, 6.8e3);
	c.C (p, vb, 0.1e-6);
	h[4] = c.R (p, tw, 10.0e3);
	h[5] = c.R (tw, q, 10.0e3);
	c.R (q, vb, 6.8e3);
	c.C (d, cr, 22.0e-9);
	c.R (cr, q, 2.2e3);
	h[6] = c.R (tw, lw, 50.0e3);
	h[7] = c.R (lw, vb, 50.0e3);
	c.R (lw, lo, 10.0e3);
	// output buffer
	const int b3 = c.Node (), e3 = c.Node (), o1 = c.Node (), out = c.Node ();
	c.C (lo, b3, 47.0e-9);
	c.R (b3, vb, 500.0e3);
	c.Bjt (rail, b3, e3, kIs, kBf, kBr, kVt, false, true);
	c.R (e3, 0, 10.0e3);
	c.C (e3, o1, 1.0e-6);
	c.R (o1, 0, 100.0e3);
	c.R (o1, out, 1.0e3);
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[1] = out;
	h[8] = c2; h[9] = u2o; h[10] = d; h[11] = tw; h[12] = vb;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	double dist = p[0] * 0.1, tone = p[1] * 0.1, lev = p[2] * 0.1;
	dist = dist < 0 ? 0 : (dist > 1 ? 1 : dist);
	tone = tone < 0 ? 0 : (tone > 1 ? 1 : tone);
	lev = lev < 0 ? 0 : (lev > 1 ? 1 : lev);
	// Distortion (100kB): clockwise the wiper goes towards R13, that is more feedback resistance and less return
	c.SetR (h[2], dist * 100.0e3 + 1.0); c.SetR (h[3], (1.0 - dist) * 100.0e3 + 1.0);
	// Tone (20kB): clockwise towards the high-pass (q)
	c.SetR (h[4], tone * 20.0e3 + 1.0); c.SetR (h[5], (1.0 - tone) * 20.0e3 + 1.0);
	// Level (100kB)
	c.SetR (h[6], (1.0 - lev) * 100.0e3 + 1.0); c.SetR (h[7], lev * 100.0e3 + 1.0);
}

const TPedal kPedal = {FxDs1, Build, Pots, 3, 0, 1.0f / CKT_VFS};

}

CFx *NewDs1 (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void Ds1BuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void Ds1PotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
