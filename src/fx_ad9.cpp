//
// fx_ad9.cpp - Analog Delay AD9 Real: Ibanez AD9 (1980s, 4096-stage MN3205 BBD, MN3102 clock, NE571 compander), simulated from the NETLIST (ckt.h) for the linear parts (input and pre-emphasis with the repeat
// feedback, BBD input filter, output filter, mixer with de-emphasis), from a numerical compander with the NE570/NE571 datasheet equations and from a BBD sampled at its clock.
//
// Sources (2026-10-19, ToneHome https://www.tonehome.de/ibanez/9-series/ad9-analog-delay/ : Ibanez_AD9_Schematics.pdf, drawing by Dirk Hendrik 2007, and Ibanez_AD9_ServiceManual.pdf no. 007):
//  - Schematic and values: input C1 47 nF, R1 1k, T1 follower with R2 510k and R7 10k; pre-emphasis U1.A (4558) with C4 1 uF, R9 47k, R5 10k + C5 6.8 nF in parallel with R9, feedback R6 47k + C2 100 pF (0 dB up to
//    about 400 Hz and +15 dB above 2.3 kHz); C6 and compressor U2.A (NE571) with C17 0.47 uF (CRECT) and C18 100 pF; 3-pole low-pass filter R11 R13 R14 10k, C8 10 nF, C10 82 nF (bootstrap on T2's emitter) and
//    C11 330 pF; C12 10 uF and R16 100k towards the BBD's IN pin; output: R18 R19 R20 R21 R22 10k, C13 2.2 nF, C14 39 nF and C15 39 nF (bootstrap), C19 1 nF, C20 820 pF, T3 and T4 (R29 R30 10k), C21 10 uF; expander U2.B
//    with C30 0.47 uF and R38 3.3k in series with R3; repeats: C31 1 uF, REPEAT P2 100KB, R43 20k (VR105), C32 1 uF towards U1.A's summing input; DELAY LEVEL P3 100KB, C35 1 uF, R47 47k; mixer U1.B with feedback
//    R59 47k, C41 100 pF and R50 10k + C38 6.8 nF (complementary de-emphasis); the dry line comes from U1.A's output through C16 4.7 uF and R44 47k; output R65 470, C43 10 uF, R66 100k.
//    The schematic says: "The AD9 seems to exist in 2 versions; the values in brackets are those of the other version": R5 1k, C5 18 nF, R6 and R9 10k, C2 470 pF, C6 1 uF, C7 1 uF, C8 12 nF, C10 100 nF, C11 270 pF,
//    C15 27 nF, C19 820 pF, C20 470 pF, R47 10k, R50 1k, R59 10k, C38 18 nF, C41 470 pF, C17 and C30 0.22 uF: Version parameter.
//  - Service manual: block diagram (buffer, pre-emphasis, compressor, -18 dB/oct low-pass, BBD, -36 dB/oct low-pass, expander, mixer and de-emphasis) and clock adjustment: "Delay Time fully
//    clockwise: 6.82 kHz on CP1; fully counterclockwise: about 80-90 kHz"; delay = 4096 / (2 fclock): about 300 ms and about 24 ms.
//  - NE570/NE571 compander (ON Semiconductor datasheet): rectifier Ig = 2 mean|Vin| / R1 with R1 10k and averaging by R5 10k with CRECT (tau = 10k x CRECT), DG cell Iout = (Ig / I1) Iin with R2 20k and
//    I1 140 uA, expander gain 2 R3 mean|Vin| / (R1 R2 IB) with R3 20k (1.4286 per volt: gain 1 at 0 dBm = 775 mV), compressor = expander in the op-amp's feedback loop (2:1 in dB).
//
// Model and ASSUMPTIONS: ideal transistor followers; linear op-amps with finite gain; compressor and expander with gain 1 at 0 dBm (the expander has the +1.3 dB of R38 3k3 in series with R3 and the
// compressor is ideal); the compressor's rectifier senses the output (loop), the expander's the input; BBD = FIFO of 2048 samples (4096 stages / 2) sampled at its clock (sample-and-hold,
// with linear interpolation of the input), clock exponential in the knob (ASSUMED: only the two extremes are documented), unity gain, no clock noise; the repeat feedback is the
// literal one of the schematic (inverting loop: U1.A inverts, the rest does not: not verified); R43 (VR105) is the Repeat trim parameter; the JFET switches (T7, T8, T12) are closed; the filters and the potentiometers P2 and P3
// do not load each other. Not verified against recordings of the real pedal.
// CALIBRATION (2026-10-19): with the printed values and an ideal BBD the feedback self-oscillates at about 1.7 kHz at Repeat 10 even with VR105 at 20k (the output filter has a +9 dB peak and the compander squares the
// gains): see kBbdRs and kFbLoss, two ASSUMED constants (not from the schematic) set so that at Repeat 10 and VR105 10k the loop is at the limit, as the manual says. With version B (R6 10k) the loop is weaker.
//
#include "fxckt.h"

namespace
{

// Calibration ASSUMPTIONS (not in the schematic or in the manual): BBD output impedance in series with the filters (it damps the Q peak of the 3-pole filters: at 0 the output filter has +9 dB at 1.7 kHz) and feedback
// loss (the compander squares the gains, and the loop gain at Repeat 10 with VR105 20k from the printed schematic is far above 1, while the manual says VR105 is adjusted for repeats lasting
// over 10 s): chosen so that at Repeat 10 and VR105 10k the loop is at the limit of self-oscillation.
const double kBbdRs = 10.0e3;
const double kFbLoss = 0.045;

void Op (Ck::TCkt &c, int inp, int inn, int out)
{
	c.Gc (0, out, inp, inn, 1.0);
	c.R (out, 0, 1.0e6);
}

// values per version: 0 = the main ones, 1 = those in brackets
struct TVer
{
	double r5, c5, r6, r9, c2, c6, c8, c10, c11, c15, c19, c20, r47, r50, r59, c38, c41, cr;
};
const TVer kVer[2] = {
	{10.0e3, 6.8e-9, 47.0e3, 47.0e3, 100.0e-12, 10.0e-6, 10.0e-9, 82.0e-9, 330.0e-12, 39.0e-9, 1.0e-9, 820.0e-12, 47.0e3, 10.0e3, 47.0e3, 6.8e-9, 100.0e-12, 0.47e-6},
	{1.0e3, 18.0e-9, 10.0e3, 10.0e3, 470.0e-12, 1.0e-6, 12.0e-9, 100.0e-9, 270.0e-12, 27.0e-9, 820.0e-12, 470.0e-12, 10.0e3, 1.0e3, 10.0e3, 18.0e-9, 470.0e-12, 0.22e-6}};

// A: input -> buffer -> pre-emphasis with the feedback; h[0] input, h[1] U1.A output (pre-emphasized dry), h[2] feedback generator, h[3] P2 top, h[4] P2 bottom
void BuildA (Ck::TCkt &c, int *h, const TVer &V, double r43)
{
	const int ix = c.Node (), in = c.Node (), b1 = c.Node (), b = c.Node (), e1 = c.Node (), n9 = c.Node (), kk = c.Node (), m = c.Node (), o = c.Node ();
	const int fx = c.Node (), x2 = c.Node (), wp = c.Node (), f1 = c.Node ();
	h[0] = c.V (ix, 0, 0.0);
	c.R (ix, in, 1.0);
	c.C (in, b1, 47.0e-9); c.R (b1, b, 1.0e3);			// C1, R1
	c.R (b, 0, 510.0e3);						// R2
	Op (c, b, e1, e1);						// T1
	c.R (e1, 0, 10.0e3);						// R7
	c.C (e1, n9, 1.0e-6);						// C4
	c.R (n9, m, V.r9);						// R9
	c.R (n9, kk, V.r5); c.C (kk, m, V.c5);			// R5, C5
	c.R (m, o, V.r6); c.C (m, o, V.c2);				// R6, C2
	Op (c, 0, m, o);
	// feedback: expander -> C31 -> P2 -> R43 -> C32 -> summer
	h[2] = c.V (fx, 0, 0.0);
	c.C (fx, x2, 1.0e-6);						// C31
	h[3] = c.R (x2, wp, 50.0e3);					// P2 (top)
	h[4] = c.R (wp, 0, 50.0e3);					// P2 (bottom)
	c.R (wp, f1, r43); c.C (f1, m, 1.0e-6);			// R43 (VR105), C32
	c.SetOut (o);
	h[1] = o;
}

// B: compressor -> BBD input filter; h[0] generator, h[1] BBD input
void BuildB (Ck::TCkt &c, int *h, const TVer &V)
{
	const int s = c.Node (), n1 = c.Node (), n2 = c.Node (), n3 = c.Node (), e = c.Node (), bi = c.Node ();
	h[0] = c.V (s, 0, 0.0);
	c.R (s, n1, 10.0e3); c.C (n1, 0, V.c8);			// R11, C8
	c.R (n1, n2, 10.0e3); c.C (n2, e, V.c10);			// R13, C10
	c.R (n2, n3, 10.0e3); c.C (n3, 0, V.c11);			// R14, C11
	Op (c, n3, e, e);						// T2
	c.R (e, 0, 10.0e3);						// R15
	c.C (e, bi, 10.0e-6); c.R (bi, 0, 100.0e3);			// C12, R16
	c.SetOut (bi);
	h[1] = bi;
}

// C: BBD output -> filter -> expander input; h[0] generator, h[1] output
void BuildC (Ck::TCkt &c, int *h, const TVer &V)
{
	const int s = c.Node (), n1 = c.Node (), n2 = c.Node (), n3 = c.Node (), e3 = c.Node (), n4 = c.Node (), n5 = c.Node (), e4 = c.Node (), ei = c.Node ();
	h[0] = c.V (s, 0, 0.0);
	c.R (s, 0, 100.0e3);						// R26 / R28 (BBD loads)
	c.R (s, n1, 12.5e3 + kBbdRs);						// R17 (wiper at half) + R18
	c.C (n1, 0, 2.2e-9);						// C13
	c.R (n1, n2, 10.0e3); c.C (n2, e3, 39.0e-9);			// R19, C14
	c.R (n2, n3, 10.0e3); c.C (n3, 0, V.c19);			// R20, C19
	Op (c, n3, e3, e3);						// T3
	c.R (e3, 0, 10.0e3);						// R29
	c.R (e3, n4, 10.0e3); c.C (n4, e4, V.c15);			// R21, C15
	c.R (n4, n5, 10.0e3); c.C (n5, 0, V.c20);			// R22, C20
	Op (c, n5, e4, e4);						// T4
	c.R (e4, 0, 10.0e3);						// R30
	c.C (e4, ei, 10.0e-6); c.R (ei, 0, 6.7e3);			// C21 and the expander's inputs (10k // 20k)
	c.SetOut (ei);
	h[1] = ei;
}

// D: mixer: dry (from U1.A via C16 and R44) and wet (expander via C31, P3, C35, R47); h[0] dry, h[1] wet, h[2] P3 top, h[3] P3 bottom
void BuildD (Ck::TCkt &c, int *h, const TVer &V)
{
	const int vd = c.Node (), vw = c.Node (), d1 = c.Node (), x3 = c.Node (), wp = c.Node (), w1 = c.Node (), w2 = c.Node (), m = c.Node (), o = c.Node (), q = c.Node (), out = c.Node (), kk = c.Node ();
	h[0] = c.V (vd, 0, 0.0);
	h[1] = c.V (vw, 0, 0.0);
	c.C (vd, d1, 4.7e-6); c.R (d1, m, 47.0e3);			// C16, R44
	c.C (vw, x3, 1.0e-6);						// C31
	h[2] = c.R (x3, wp, 50.0e3);					// P3 (top)
	h[3] = c.R (wp, 0, 50.0e3);					// P3 (bottom)
	c.C (wp, w1, 1.0e-6); c.R (w1, m, V.r47);			// C35, R47
	c.R (m, o, V.r59); c.C (m, o, V.c41);				// R59, C41
	c.C (m, kk, V.c38); c.R (kk, o, V.r50);			// C38, R50
	Op (c, 0, m, o);
	c.R (o, q, 470.0);						// R65
	c.C (q, out, 10.0e-6); c.R (out, 0, 100.0e3);			// C43, R66
	c.R (out, 0, 1.0e6);
	c.SetOut (out);
	h[4] = out; (void) w2;
}

const double kK = 2.0 * 20.0e3 / (10.0e3 * 20.0e3 * 140.0e-6);		// 1.4286 per volt (NE570)
const unsigned kFifo = 2048;						// 4096 stages / 2

class CAd9 : public CFx
{
public:
	CAd9 (float fRate) : m_fRate (fRate), m_bInit (false), m_nVer (0), m_eC (0.0), m_eE (0.0), m_fFb (0.0), m_fPh (0.0), m_fOut (0.0), m_fLastIn (0.0), m_fPrev (0.0), m_nW (0), m_fR43 (-1.0f)
	{
		for (unsigned i = 0; i < kFifo; i++) m_Fifo[i] = 0.0f;
	}

	unsigned Type (void) const { return FxAd9; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		int ver = (int) (pP[3] + 0.5f);
		ver = ver < 0 ? 0 : (ver > 1 ? 1 : ver);
		double r43 = (double) pP[4] * 1.0e3;
		r43 = r43 < 100.0 ? 100.0 : (r43 > 20.0e3 ? 20.0e3 : r43);
		if (!m_bInit || ver != m_nVer || r43 != (double) m_fR43)
		{
			m_nVer = ver; m_fR43 = (float) r43;
			Restart ();
			m_bInit = true;
		}
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
			*pMono = 0;
		}
		double dl = pP[0] * 0.1, rp = pP[1] * 0.1, lv = pP[2] * 0.1;
		dl = dl < 0 ? 0 : (dl > 1 ? 1 : dl); rp = rp < 0 ? 0 : (rp > 1 ? 1 : rp); lv = lv < 0 ? 0 : (lv > 1 ? 1 : lv);
		m_A.SetR (m_hA[3], (1.0 - rp) * 100.0e3 + 1.0); m_A.SetR (m_hA[4], rp * 100.0e3 + 1.0);
		m_D.SetR (m_hD[2], (1.0 - lv) * 100.0e3 + 1.0); m_D.SetR (m_hD[3], lv * 100.0e3 + 1.0);
		// clock: from about 85 kHz (fully counterclockwise) to 6.82 kHz (fully clockwise), exponential law in the knob
		const double fclk = 85.0e3 * (double) Ck::Exp ((float) (dl * -2.5230));			// ln(6,82 / 85) = -2,523
		const double dt = 1.0 / (double) m_fRate, vfs = (double) CKT_VFS, og = 1.0 / (double) CKT_VFS;
		const double alpha = dt / (10.0e3 * kVer[m_nVer].cr);
		const double ticks = fclk * dt;				// clock periods per audio sample
		for (unsigned j = 0; j < n; j++)
		{
			const double x = 0.5 * ((double) pL[j] + (double) pR[j]) * vfs;
			m_A.SetV (m_hA[2], m_fFb);
			m_A.Run (m_hA[0], x);
			const double vo = m_A.Probe (m_hA[1]);
			// compressor (in the feedback loop: its output is the signal divided by k times the mean of the output)
			double ec = m_eC < 5.0e-4 ? 5.0e-4 : m_eC;
			double vc = vo / (kK * ec);
			if (vc > 3.5) vc = 3.5; else if (vc < -3.5) vc = -3.5;
			m_eC += alpha * ((vc < 0 ? -vc : vc) - m_eC);
			m_B.Run (m_hB[0], vc);
			const double vb = m_B.Probe (m_hB[1]);
			// BBD: the line is sampled by the clock; the input signal is linearly interpolated between two audio samples
			m_fPh += ticks;
			while (m_fPh >= 1.0)
			{
				m_fPh -= 1.0;
				const double fr = ticks > 0.0 ? 1.0 - m_fPh / ticks : 1.0;
				const double s = m_fPrev + (vb - m_fPrev) * (fr < 0 ? 0 : (fr > 1 ? 1 : fr));
				m_fOut = (double) m_Fifo[m_nW];			// the output is the oldest sample
				m_Fifo[m_nW] = (float) s;
				m_nW = (m_nW + 1) % kFifo;
			}
			m_fPrev = vb;
			const double wet = m_C.Run (m_hC[0], m_fOut);
			// expander: the mean is that of its input
			m_eE += alpha * ((wet < 0 ? -wet : wet) - m_eE);
			double xo = kK * 1.165 * m_eE * wet;			// +1.3 dB for R38 3k3 in series with R3
			if (xo > 3.5) xo = 3.5; else if (xo < -3.5) xo = -3.5;
			m_fFb = xo * kFbLoss;
			m_D.SetV (m_hD[1], xo);
			double y = m_D.Run (m_hD[0], vo);
			if (!(y > -50.0 && y < 50.0) || !(vo > -50.0 && vo < 50.0)) { Restart (); y = 0.0; m_fFb = 0.0; }
			pL[j] = pR[j] = (float) (y * og);
		}
	}

private:
	void Restart (void)
	{
		const TVer &V = kVer[m_nVer];
		m_A.Clear (); BuildA (m_A, m_hA, V, (double) m_fR43); m_A.SetStep (1.0 / (double) m_fRate); m_A.Start ();
		m_B.Clear (); BuildB (m_B, m_hB, V); m_B.SetStep (1.0 / (double) m_fRate); m_B.Start ();
		m_C.Clear (); BuildC (m_C, m_hC, V); m_C.SetStep (1.0 / (double) m_fRate); m_C.Start ();
		m_D.Clear (); BuildD (m_D, m_hD, V); m_D.SetStep (1.0 / (double) m_fRate); m_D.Start ();
		m_eC = m_eE = 0.0;
	}

	float m_fRate;
	bool m_bInit;
	int m_nVer;
	double m_eC, m_eE, m_fFb, m_fPh, m_fOut, m_fLastIn, m_fPrev;
	unsigned m_nW;
	float m_fR43;
	float m_Fifo[kFifo];
	int m_hA[6], m_hB[6], m_hC[6], m_hD[6];
	Ck::TCkt m_A, m_B, m_C, m_D;
};

}

CFx *NewAd9 (float fRate)
{
	return new CAd9 (fRate);
}

void Ad9BuildForTest (Ck::TCkt &a, int *ha, Ck::TCkt &b, int *hb, Ck::TCkt &c, int *hc, Ck::TCkt &d, int *hd, int ver, double r43) { BuildA (a, ha, kVer[ver], r43); BuildB (b, hb, kVer[ver]); BuildC (c, hc, kVer[ver]); BuildD (d, hd, kVer[ver]); }
