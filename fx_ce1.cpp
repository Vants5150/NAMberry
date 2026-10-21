//
// fx_ce1.cpp - Chorus CE-1 Real: Boss CE-1 Chorus Ensemble (1976, ET-10C module), CHORUS mode: input amplifier, input and output filters of the MN3002 BBD (512 stages) and output network simulated from the
// NETLIST (ckt.h); BBD delay line and LFO simulated numerically with the formulas of their circuit.
//
// Sources (2026-10-19, supplied by the user from https://www.tonehome.de/boss/ce-1-chorus-ens/): "ET-10C" service schematic (boss_ce1_chorus.pdf) and 1976 owner's manual (Boss_CE1_1976_OM.pdf).
//  - Input: S1 high/low sensitivity (80 mV / 10 mV), VR11 Level 50KA, R8 10K, IC-1 TA7136P non-inverting with R11 470K and R12 10K + C07 0.47 uF to ground (gain 1 + 470/10 = 48); supply +-13 V.
//  - Prefilter: R13 22K in parallel with C06 0.0068 uF, R5 10K to ground (shelf: -10 dB below about 1 kHz), R14 and R16 10K with C12 0.0047 uF (bootstrap on Q2's emitter) and C11 0.001 uF (Q2 follower, R18 10K);
//    C14 0.1 uF, R19 6.2K and R21 100K towards 4 V on pin 3 of the MN3002 (R19 has the decimal point but its first digit is hard to read).
//  - BBD MN3002 with 512 stages: delay = 512 / (2 fclock); clock from a discrete multivibrator (Q3-Q6) driven by Q8 and Q9; waveform legend "(G) 5 us - 16 us" on the clock pins.
//  - Post-filter: Q7 (follower, R40 10K), R41 22K with C26 0.0047 uF, R42 6.8K with C28 0.001 uF and R43 15K with C29 0.001 uF (bootstrap towards the output), R44 15K with C27 470 pF, Q10 (R46 10K).
//  - Outputs: dry from IC-1 via R47 12K, C31 0.47 uF, R48 1.6K, R52 2.3K, R51 4.7K and R91 470; effect from Q10 via C30 0.47 uF, R45 2.2K, R50 100K, R90 2.2K, R54 4.7K and R91 470; MONO jack = the two together.
//    Legend levels at 440 Hz: C 4 Vpp, D 4 Vpp, F 750 mVpp, M 240 mVpp, N 350 mVpp.
//  - LFO (IC-4, uPC1458): Schmitt trigger (R80 180K and R81 220K, output +-13.5 V as "J 27 Vpp") and integrator (C43 0.062 uF); in chorus mode the integrator's input current goes through R62 100K,
//    VR12 Chorus Intensity 10KB with R65 1.5K to ground, R57 100K and R76 18K: the Intensity knob sets the SPEED (confirmed by the manual's note: "if the chorus intensity knob does not change the speed of the
//    blinking, the switch is in vibrato"); frequency = slope / (4 Vthreshold) with Vthreshold = 13.5 R80 / R81: about 0.56 to 4.3 Hz.
//
// Delay excursion (2026-10-19): the model used to sweep the whole 5-16 us range of the legend even in chorus mode (delay 2.56-8.19 ms, 5.6 ms peak-to-peak): at Intensity 10 the maximum pitch
// deviation of the wet branch was +-142 cent (measured from the phase at 1 kHz; CE-2 at maximum: +-51 with the same method) and the user found it unusable. In chorus the LFO signal (23 Vpp triangle, legend K) reaches the clock through a fixed divider
// (R79 4.7K, R78 6.2K), but from the drawing I cannot work out how far it moves the multivibrator: Sweep parameter, default 1.8 ms (like the documented CE-2 excursion, ASSUMED), with the short end
// fixed at 2.56 ms (100 kHz clock = 5 us half-period); 5.63 ms restores the previous full excursion.
//
// Model and ASSUMPTIONS: IC-1 as an op-amp (typical gain 92 dB, TA7136AP datasheet; gain-bandwidth product 1 MHz ASSUMED, +-12.5 V); ideal transistor followers; BBD = delay line with
// Lagrange interpolation (no sampling, compression or clock noise); BBD gain (0.40) calibrated on the legend, which is made of 440 Hz square waves (from IC-1 4 Vpp to F 750 mVpp); clock frequency linear in the control voltage and
// the whole 5-16 us range (clock period 2 x) swept by the LFO (ASSUMED: I do not know how the legend refers to the excursion); VIBRATO mode not modelled (sine oscillator with a different network, to be analyzed);
// low sensitivity = -18 dB (80/10 mV ratio); switching JFETs (Q11-Q13) ideal; R76 read as 18K. Not verified against recordings of the real pedal.
//
#include "fxckt.h"

namespace
{

const double kGBbd = 0.40;				// BBD gain (-8 dB): calibrated with the legend (440 Hz square waves: IC-1 -> D unchanged, F at 750 mVpp out of 4 Vpp), see test_ce1
const double kSat = 13.5, kRth = 180.0e3 / 220.0e3;	// Schmitt output +-13.5 V; threshold = kSat R80 / R81

float Taper (float x, float a)
{
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return (Ck::Exp (a * x) - 1.0f) / (Ck::Exp (a) - 1.0f);
}

void Op (Ck::TCkt &c, int inp, int inn, int out)
{
	c.Gc (0, out, inp, inn, 1.0);
	c.R (out, 0, 1.0e6);
}

// A: input -> prefilter -> BBD input; h[0] generator, h[1] BBD input (output), h[2] Level top R, h[3] bottom R, h[4] IC-1 output
void BuildA (Ck::TCkt &c, int *h)
{
	const int ix = c.Node (), in = c.Node (), wp = c.Node (), p = c.Node (), m = c.Node (), o = c.Node (), k = c.Node ();
	const int n1 = c.Node (), n2 = c.Node (), n3 = c.Node (), ao = c.Node (), bi = c.Node ();
	h[0] = c.V (ix, 0, 0.0);
	c.R (ix, in, 1.0);
	h[2] = c.R (in, wp, 25.0e3);
	h[3] = c.R (wp, 0, 25.0e3);
	c.R (wp, p, 10.0e3);					// R8
	c.Opamp (p, m, o, 0, 1.0e6, 5.0e3, 4.0e7, 12.5, 100.0);
	c.R (m, k, 10.0e3); c.C (k, 0, 0.47e-6);		// R12, C07
	c.R (m, o, 470.0e3);					// R11
	c.R (o, n1, 22.0e3); c.C (o, n1, 6.8e-9);		// R13, C06
	c.R (n1, 0, 10.0e3);					// R5
	c.R (n1, n2, 10.0e3);					// R14
	c.C (n2, ao, 4.7e-9);					// C12 (bootstrap)
	c.R (n2, n3, 10.0e3);					// R16
	c.C (n3, 0, 1.0e-9);					// C11
	Op (c, n3, ao, ao);					// Q2
	c.R (ao, 0, 10.0e3);					// R18
	c.C (ao, bi, 0.1e-6);					// C14
	c.R (bi, 0, 6.2e3); c.R (bi, 0, 100.0e3);		// R19, R21
	c.SetOut (bi);
	h[1] = bi; h[4] = o; h[5] = ao;
}

// C: BBD output -> post-filter; h[0] generator, h[1] output (Q10 emitter)
void BuildC (Ck::TCkt &c, int *h)
{
	const int s = c.Node (), a1 = c.Node (), a2 = c.Node (), a3 = c.Node (), a4 = c.Node (), f = c.Node ();
	h[0] = c.V (s, 0, 0.0);
	c.R (s, a1, 22.0e3); c.C (a1, 0, 4.7e-9);		// R41, C26
	c.R (a1, a2, 6.8e3); c.C (a2, f, 1.0e-9);		// R42, C28
	c.R (a2, a3, 15.0e3); c.C (a3, f, 1.0e-9);		// R43, C29
	c.R (a3, a4, 15.0e3); c.C (a4, 0, 470.0e-12);		// R44, C27
	Op (c, a4, f, f);					// Q10
	c.R (f, 0, 10.0e3);					// R46
	c.SetOut (f);
	h[1] = f;
}

// B: output network; h[0] dry generator, h[1] wet generator, h[2] dry output (jack M), h[3] wet output (jack N); with bMono the two jacks are joined
void BuildB (Ck::TCkt &c, int *h, bool bMono)
{
	const int vd = c.Node (), vw = c.Node (), d1 = c.Node (), d2 = c.Node (), d3 = c.Node (), w1 = c.Node (), w2 = c.Node (), w3 = c.Node (), w4 = c.Node (), od = c.Node ();
	const int ow = bMono ? od : c.Node ();
	h[0] = c.V (vd, 0, 0.0);
	h[1] = c.V (vw, 0, 0.0);
	c.R (vd, d1, 12.0e3); c.C (d1, d2, 0.47e-6);		// R47, C31
	c.R (d2, 0, 1.6e3);					// R48
	c.R (d2, d3, 2.3e3);					// R52
	c.R (d3, 0, 4.7e3);					// R51
	c.R (d3, od, 470.0);					// R91
	c.C (vw, w1, 0.47e-6);					// C30
	c.R (w1, w2, 2.2e3);					// R45
	c.R (w2, 0, 100.0e3);					// R50
	c.R (w2, w3, 2.2e3);					// R90
	c.R (w3, 0, 4.7e3);					// R54
	c.R (w3, ow, 470.0);					// R91
	c.R (od, 0, 1.0e6);
	if (!bMono) c.R (ow, 0, 1.0e6);
	(void) w4;
	c.SetOut (od);
	h[2] = od; h[3] = ow;
}

const unsigned kBuf = 1024;

class CCe1 : public CFx
{
public:
	CCe1 (float fRate) : m_fRate (fRate), m_bInit (false), m_nMode (0), m_fPh (0.0), m_fFreq (1.0), m_fKey (-1.0f)
	{
		m_nW = 0;
		for (unsigned i = 0; i < kBuf; i++) m_Buf[i] = 0.0f;
	}

	unsigned Type (void) const { return FxCe1; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		const int nMode = pP[3] > 0.5f ? 1 : 0;				// 0 mono, 1 stereo
		if (!m_bInit || nMode != m_nMode)
		{
			m_nMode = nMode;
			Restart ();
			m_bInit = true;
		}
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
			*pMono = 0;
		}
		if (pP[0] != m_fKey)
		{
			m_fKey = pP[0];
			m_fFreq = LfoHz (pP[0] * 0.1);
		}
		double lv = pP[1] * 0.1;
		lv = lv < 0 ? 0 : (lv > 1 ? 1 : lv);
		const double t = (double) Taper ((float) lv, 2.77f);
		m_A.SetR (m_hA[2], (1.0 - t) * 50.0e3 + 1.0); m_A.SetR (m_hA[3], t * 50.0e3 + 1.0);
		const double dt = 1.0 / (double) m_fRate, vfs = (double) CKT_VFS, og = 1.0 / (double) CKT_VFS;
		const double sens = pP[2] > 0.5f ? 0.125 : 1.0;			// low: -18 dB (80 mV against 10 mV)
		double sweep = (double) pP[4];
		sweep = sweep < 0.0 ? 0.0 : (sweep > 5.63 ? 5.63 : sweep);	// delay excursion in ms (ASSUMED, see the header)
		const double fLo = 256.0 / (2.56 + sweep) * 1e3;			// slowest clock reached by the LFO: 100 kHz = 2.56 ms; 31.25 kHz = 8.19 ms (full excursion)
		for (unsigned j = 0; j < n; j++)
		{
			m_fPh += m_fFreq * dt;
			if (m_fPh >= 1.0) m_fPh -= 1.0;
			const double u = m_fPh < 0.5 ? 2.0 * m_fPh : 2.0 - 2.0 * m_fPh;		// triangle 0..1
			const double fcp = fLo + (100.0e3 - fLo) * u;				// clock: period from 32 to 10 us (half-period from 16 to 5 us)
			double d = 256.0 / fcp * (double) m_fRate;					// delay = 512 / (2 fcp)
			if (d < 4.0) d = 4.0;
			if (d > (double) kBuf - 8.0) d = (double) kBuf - 8.0;
			const double x = 0.5 * ((double) pL[j] + (double) pR[j]) * vfs * sens;
			m_A.Run (m_hA[0], x);
			double vi = m_A.Probe (m_hA[1]), dry = m_A.Probe (m_hA[4]);
			if (!(vi > -80.0 && vi < 80.0 && dry > -80.0 && dry < 80.0)) { Restart (); vi = dry = 0.0; }
			m_Buf[m_nW] = (float) (vi * kGBbd);
			double rp = (double) m_nW - d;
			while (rp < 0.0) rp += (double) kBuf;
			const unsigned i1 = (unsigned) rp, i0 = (i1 + kBuf - 1) % kBuf, i2 = (i1 + 1) % kBuf, i3 = (i1 + 2) % kBuf;
			const double f = rp - (double) i1;
			const double y0 = m_Buf[i0], y1 = m_Buf[i1], y2 = m_Buf[i2], y3 = m_Buf[i3];
			const double bb = y1 + 0.5 * f * (y2 - y0 + f * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + f * (3.0 * (y1 - y2) + y3 - y0)));
			m_nW = (m_nW + 1) % kBuf;
			const double wet = m_C.Run (m_hC[0], bb);
			m_B.SetV (m_hB[1], wet);
			double yd = m_B.Run (m_hB[0], dry);
			double yw = m_nMode ? m_B.Probe (m_hB[3]) : yd;
			if (!(yd > -50.0 && yd < 50.0 && yw > -50.0 && yw < 50.0)) { Restart (); yd = yw = 0.0; }
			pL[j] = (float) (yd * og);
			pR[j] = (float) (yw * og);
		}
	}

	// LFO frequency for Intensity 0..1: k = 1 takes VR12's wiper towards node 19 (R62), k = 0 towards node 18 (R65)
	static double LfoHz (double k)
	{
		k = k < 0 ? 0 : (k > 1 ? 1 : k);
		const double R62 = 100.0e3, Rpot = 10.0e3, R65 = 1.5e3, Rl = 100.0e3 + 18.0e3;		// Rl = R57 + R76 towards the integrator's virtual ground
		const double A = (1.0 - k) * Rpot + 1.0, B = k * Rpot + 1.0;				// 19 - wiper, wiper - 18
		const double g62 = 1.0 / R62, gA = 1.0 / A, gB = 1.0 / B, g65 = 1.0 / R65, gl = 1.0 / Rl;
		// unknowns x = [V19, V10 (wiper), V18]; the Schmitt output is +kSat on R62
		double M[3][4] = {
			{ g62 + gA, -gA, 0.0, g62 * kSat },
			{ -gA, gA + gB + gl, -gB, 0.0 },
			{ 0.0, -gB, gB + g65, 0.0 }};
		for (int c = 0; c < 3; c++)
		{
			for (int r = c + 1; r < 3; r++)
			{
				const double f = M[r][c] / M[c][c];
				for (int q = c; q < 4; q++) M[r][q] -= f * M[c][q];
			}
		}
		double x[3];
		for (int r = 2; r >= 0; r--)
		{
			double s = M[r][3];
			for (int q = r + 1; q < 3; q++) s -= M[r][q] * x[q];
			x[r] = s / M[r][r];
		}
		const double slope = x[1] / (Rl * 0.062e-6);				// slope (V/s) at the integrator's output: current / C43
		return slope / (4.0 * kSat * kRth);					// half period = 2 Vthreshold / slope
	}
private:
	void Restart (void)
	{
		m_A.Clear (); BuildA (m_A, m_hA); m_A.SetStep (1.0 / (double) m_fRate); m_A.Start ();
		m_C.Clear (); BuildC (m_C, m_hC); m_C.SetStep (1.0 / (double) m_fRate); m_C.Start ();
		m_B.Clear (); BuildB (m_B, m_hB, m_nMode == 0); m_B.SetStep (1.0 / (double) m_fRate); m_B.Start ();
	}

	float m_fRate;
	bool m_bInit;
	int m_nMode;
	double m_fPh, m_fFreq;
	float m_fKey;
	unsigned m_nW;
	float m_Buf[kBuf];
	int m_hA[6], m_hB[6], m_hC[6];
	Ck::TCkt m_A, m_B, m_C;
};

}

CFx *NewCe1 (float fRate)
{
	return new CCe1 (fRate);
}

void Ce1BuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &b, int *hb, Ck::TCkt &c, int *hc, bool bMono) { BuildA (a, ha); BuildB (b, hb, bMono); BuildC (c, hc); }
double Ce1LfoHzForTest (double k) { return CCe1::LfoHz (k); }
