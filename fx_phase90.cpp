//
// fx_phase90.cpp - Phase 90 Real: MXR Phase 90 Script Logo (1974, four op-amp phase shifters with JFETs as variable resistors, Schmitt-trigger LFO with integrator, PNP transistor mixer),
// simulated from the NETLIST (ckt.h) for the audio part and from a numerical LFO with the equations of its circuit, with the Speed knob and the documented options (R28 feedback, JFET pinch-off voltage).
//
// Sources (2026-10-19): ElectroSmash, MXR Phase 90 analysis (archived copy, https://electrosmash.com/mxr-phase-90): 9 V supply with 5.1 V zener (Vref) and 250k trimmer with R20 1M for the bias voltage
// Vbias; input buffer: R3 10k, C5 10 nF, R14 470k to Vref, follower (TL061); four identical stages: R 10k from the input to the inverting input, R 10k feedback, C 47 nF
// from the input to the non-inverting one, R 24k between the non-inverting one and Vref with the 2N5952 JFET (drain and source) in parallel, whose gate is the LFO node; R28 24k (Block Logo, later versions) from the output
// of the fourth stage to the inverting input of the second (elsewhere in the other variants); mixer: R16 150k (dry) and R8 150k (wet) on the base of the 2N4125 PNP, R7 150k feedback from the collector, R4 56k to ground,
// emitter to Vref, C2 47 nF and R2 150k at the output; LFO: op-amp with R19 150k towards Vref, R21 470k positive feedback, R24 150k and C7 10 nF on the inverting input, the triangle node is R36
// (speed, 470k) from the op-amp's output with C10 15 uF to ground and R22 3.9M towards Vbias. The JFET's resistance is linear for the triode region: rDS = 1 / (2 beta (Vgs - Vp)), with pinch-off voltage
// between -1.3 and -3.5 V (ElectroSmash) and rDS from a few hundred ohms to a few Mohm. Formulas: stage = all-pass, notch at f = tan(phi / 2) / (2 pi R C) with R 24k and C 47 nF: 58.5 Hz and 340.8 Hz (ElectroSmash).
//
//  - F. Eichas, M. Fink, M. Holters, U. Zoelzer, "Physical Modeling of the MXR Phase 90 Guitar Effect Pedal", DAFx-14, Erlangen 2014: same method (nodal DK, ideal op-amps), circuit of a kit
//    (Musikding) measured component by component and with the four JFETs matched; Vlift (zener) about 5 V; the measured and simulated working range of the gate is Vgs from -2.8 V to -1.5 V (Vds from -2 to 2 V),
//    independent of Vds and Ids; the LFO is described as a triangle with slightly curved edges and a falling edge a bit longer than the rising one; the feedback (R15 in their schematic, R28 here) goes
//    to the second stage; maximum of the sweep visible in the spectrograms about 6 kHz.
//
// Model: linear op-amps with finite gain (5000, no saturation at the rails: guitar dynamics does not reach them), Ebers-Moll PNP (Is 1e-14, beta 200 from ElectroSmash), ideal Vref; numerical
// LFO: op-amp output between 0.3 V and 7.7 V (ASSUMED: not documented; the TL061 at 9 V is not rail-to-rail), Schmitt thresholds Vref x 0.758 + Vo x 0.242, triangle node that charges towards
// (Vo / R36 + Vbias / R22) / (1 / R36 + 1 / R22) with time constant C10 / (1 / R36 + 1 / R22); the SHAPE and the speed of the LFO come from here, while the EXCURSION of the gate is the one MEASURED by Eichas et
// al.: the node voltage (from 3.94 to 5.73 V with these thresholds) is linearly remapped onto Vgs from -2.8 to -1.5 V (with the schematic's thresholds the gate would go from -1.2 to +0.6 V and the notches would be between 4 and 16 kHz,
// impossible: the role of the bias trimmer is not explained by ElectroSmash); Speed: R36 from 470k to 10k with a geometric law (ASSUMED: the potentiometer's minimum is not in the source); JFET: Idss 6 mA
// (average value of the 2N5952, Central Semiconductor) and pinch-off voltage -2.4, -3.0 (default: below -2.8 V the channel is closed as in their range) or -3.5 V: the precise values of their JFET table
// are not published (reading tables), so the rDS(Vgs) relation is an approximation; the four JFETs are equal (matched). The audio circuit is updated every 64 samples. Not verified against
// recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kVref = 5.1, kVhi = 7.7, kVlo = 0.3, kR22 = 3.9e6, kC10 = 15.0e-6, kRmax = 470.0e3, kRmin = 10.0e3;
const double kVp[3] = {-2.4, -3.0, -3.5};
const double kVgsLo = -2.8, kVgsHi = -1.5;		// gate range measured by Eichas et al. (DAFx-14)
const double kIdss = 6.0e-3;		// 2N5952: IDSS 4-8 mA, VGS(off) from -1.3 to -3.5 V, rDS(on) at most 300 ohm (Central Semiconductor, 2N5949-2N5953 series): average value

// linear op-amp: current g (V+ - V-) into the output node, 1 Mohm load to ground
void Op (Ck::TCkt &c, int inp, int inn, int out)
{
	c.Gc (0, out, inp, inn, 1.0);
	c.R (out, 0, 1.0e6);
}

// p: 0 = speed, 1 = bias, 2 = R28 feedback (0 absent, 1..4 = arrival stage), 3 = pinch-off voltage (index)
void Build (Ck::TCkt &c, int *h, const float *pp)
{
	int fb = (int) (pp[2] + 0.5f);
	fb = fb < 0 ? 0 : (fb > 4 ? 4 : fb);
	const int vr = c.Node (), ix = c.Node (), in = c.Node (), n3 = c.Node (), pb = c.Node (), ob = c.Node ();
	c.V (vr, 0, kVref);
	h[0] = c.V (ix, 0, 0.0);
	c.R (ix, in, 1.0);
	c.R (in, n3, 10.0e3); c.C (n3, pb, 10.0e-9);
	c.R (pb, vr, 470.0e3);
	Op (c, pb, ob, ob);							// follower
	int s = ob;
	int sum[4], outn[4];
	for (int k = 0; k < 4; k++)
	{
		const int m = c.Node (), o = c.Node (), p = c.Node ();
		c.R (s, m, 10.0e3);
		c.R (m, o, 10.0e3);
		c.C (s, p, 47.0e-9);
		c.R (p, vr, 24.0e3);
		h[2 + k] = c.R (p, vr, 1.0e9);					// JFET
		Op (c, p, m, o);
		sum[k] = m; outn[k] = o;
		s = o;
	}
	if (fb > 0) c.R (outn[3], sum[fb - 1], 24.0e3);				// R28
	// mixer
	const int bm = c.Node (), qc = c.Node (), ow = c.Node ();
	c.R (ob, bm, 150.0e3);
	c.R (outn[3], bm, 150.0e3);
	c.R (bm, qc, 150.0e3);
	c.Bjt (qc, bm, vr, 1.0e-14, 200.0, 5.0, 0.02585, true, false);
	c.R (qc, 0, 56.0e3);
	c.C (qc, ow, 47.0e-9);
	c.R (ow, 0, 150.0e3);
	c.R (ow, 0, 1.0e6);
	c.SetOut (ow);
	h[1] = ow;
	h[6] = outn[0]; h[7] = outn[1]; h[8] = outn[2]; h[9] = outn[3]; h[10] = ob; h[11] = qc; h[12] = bm;
}

void Pots (Ck::TCkt &c, const int *h, const float *p)
{
	(void) c; (void) h; (void) p;		// everything goes through Mod: the JFETs' resistance depends on the LFO
}

// LFO: st[0] = voltage of the triangle node, st[1] = 1 if the op-amp's output is high, st[2] = started
void Mod (Ck::TCkt &c, const int *h, const float *p, double *st, unsigned n, double dt)
{
	double sp = p[0] * 0.1, bs = p[1] * 0.1;
	sp = sp < 0 ? 0 : (sp > 1 ? 1 : sp); bs = bs < 0 ? 0 : (bs > 1 ? 1 : bs);
	int vi = (int) (p[3] + 0.5f);
	vi = vi < 0 ? 0 : (vi > 2 ? 2 : vi);
	if (st[2] == 0.0) { st[2] = 1.0; st[0] = 4.8; st[1] = 1.0; }
	const double r36 = kRmax * (double) Ck::Exp ((float) (sp * -3.8501));	// kRmin / kRmax = e^-3,85
	const double vbias = kVref * bs;
	const double gs = 1.0 / r36 + 1.0 / kR22, tau = kC10 / gs;
	if (n)
	{
		const double Vo = st[1] > 0.5 ? kVhi : kVlo;
		const double vt = (Vo / r36 + vbias / kR22) / gs;
		const double k = (double) Ck::Exp ((float) (-(double) n * dt / tau));
		st[0] = vt + (st[0] - vt) * k;
		const double vpHi = kVref * 0.758 + kVhi * 0.242, vpLo = kVref * 0.758 + kVlo * 0.242;
		if (st[1] > 0.5 && st[0] >= vpHi) st[1] = 0.0;
		else if (st[1] < 0.5 && st[0] <= vpLo) st[1] = 1.0;
	}
	// JFET: the excursion of the triangle node (between the two Schmitt thresholds) is remapped onto the gate range measured by Eichas et al.
	const double tLo = kVref * 0.758 + kVlo * 0.242, tHi = kVref * 0.758 + kVhi * 0.242;
	double u = (st[0] - tLo) / (tHi - tLo);
	u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
	const double vgs = kVgsLo + (kVgsHi - kVgsLo) * u;
	const double vp = kVp[vi], beta = kIdss / (vp * vp);
	double rds = vgs <= vp + 1.0e-3 ? 1.0e9 : 1.0 / (2.0 * beta * (vgs - vp));
	if (rds > 1.0e9) rds = 1.0e9;
	for (int k = 0; k < 4; k++) c.SetR (h[2 + k], rds);
}

const TPedal kPedal = {FxPhase90, Build, Pots, 4, (1u << 2), 1.0f / CKT_VFS, 0, 64, Mod};

}

CFx *NewPhase90 (float fRate)
{
	return new CCktFx (&kPedal, fRate);
}

void Phase90BuildForTest (Ck::TCkt &c, int *h, const float *p) { Build (c, h, p); }
void Phase90PotsForTest (Ck::TCkt &c, const int *h, const float *p) { Pots (c, h, p); }
void Phase90ModForTest (Ck::TCkt &c, const int *h, const float *p, double *st, unsigned n, double dt) { Mod (c, h, p, st, n, dt); }
