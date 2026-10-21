//
// fx_rat.cpp - RAT (ProCo): model of the CIRCUIT with the physical pedal's controls (Distortion, Filter, Volume) and three variants (LM308, OP07, LED).
//
// Fonti (2026-10-18):
//  - ElectroSmash, analysis of the ProCo RAT (https://electrosmash.mas-effects.com/proco-rat; the original electrosmash.com does not open from here): input C1 22 nF with high-pass at 7.2 Hz, R3 1k and C2 1 nF; NON-inverting
//    clipping stage with LM308 (compensation C3 30 pF): feedback = Distortion potentiometer 100K-A in parallel with C4 100 pF, towards ground (- input) R4 47 ohm + C5 2.2 uF in parallel with
//    R5 560 ohm + C6 4.7 uF (gain 1 + Rd / (R4 // R5): from 1 to 2305 = 67 dB; corners at 1539 Hz and 60 Hz); C7 4.7 uF and R6 1k lead to the two 1N914 diodes in antiparallel to ground; passive Tone filter:
//    R7 1.5k + Filter 100K-A in series, C8 3.3 nF to ground (cutoff from 475 Hz to 32 kHz); JFET buffer (2N5458) with gain 1; Volume 100K-A. LM308 slew rate 0.3 V/us.
//  - Variants: the original has the LM308; more recent "RAT 2" the OP07 (same source); LEDs in place of the diodes as in the builders' common modification (here two red LEDs: parameters as for the Tube Screamer).
//
// Fitted to real captures (RAT2 by outmodedelectronics, TONE3000, 14 models with input and output at 11.5 dBu; Nelder-Mead on the complete model, 294 points, residual 1.49 dB rms): gain-bandwidth product
// of the LM308 0.252 MHz (the analysis says "at 10 kHz no more than 30 dB"), minimum resistance of the Distortion potentiometer 639 ohm, curves of the three potentiometers: Distortion a = 3.47, Filter a = 4.04 (R = (e^(a x) - 1) /
// (e^a - 1)); Volume 2.77 (not determinable: a single position in the captures); output excursion 4.1 V. Not verified: the OP07 variant (0.6 MHz bandwidth from the datasheet, not from captures).
//
// Calculation: the op-amp is an integrator (A = wt / s) inside the exact feedback network: a 4-state linear system (C5, C6, C4 and output) discretized with the trapezoidal rule, with the output limited
// by the slew rate and the supply rails; after C7 and R6 the diode node with the Tone filter is solved sample by sample with Newton's method. With Quality = High everything runs at 4 times the
// sampling rate (48-tap FIR filters, total delay 0.24 ms) to reduce the aliasing of the hard clipping.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

// values fitted to the real captures: global because the PC tests vary them (in the firmware they stay these)
float g_RatGbw[3] = {0.252e6f, 0.6e6f, 0.252e6f};	// gain-bandwidth product (Hz): LM308 with 30 pF, OP07, LM308
float g_RatVsat[3] = {4.1f, 3.6f, 4.1f};	// maximum output excursion (peak V)
float g_RatTaperD = 3.47f, g_RatTaperT = 4.04f, g_RatTaperV = 2.77f;	// potentiometer curves: R(x) = (e^(a x) - 1) / (e^a - 1)
float g_RatOutTrim = 0.575f;		// -4.8 dB: -1.8 dB from the captures (the circuit comes out a bit lower than the captures) and a fixed -3 dB (NOT part of the circuit) because the NAM in the board comes out normalized at about -16.5 dBFS RMS, the circuit 2-4 dB louder
float g_RatRdMin = 639.0f;			// minimum resistance of the Distortion potentiometer (ohm)
float g_RatSr = 0.3e6f;				// slew rate (V/s): 0.3 V/us for LM308 and OP07

namespace
{

const float kVfs = 1.305f;			// peak volts per digital full scale (0 dBFS = +1.5 dBu)
const float kVt = 0.02585f;
const double kC1 = 22.0e-9, kR1 = 1.0e6;	// input: high-pass at 7.2 Hz
const double kR4 = 47.0, kC5 = 2.2e-6, kR5 = 560.0, kC6 = 4.7e-6, kC4 = 100.0e-12;
const double kPotD = 100.0e3;
const double kC7 = 4.7e-6, kR6 = 1.0e3;
const double kR7 = 1.5e3, kPotT = 100.0e3, kC8 = 3.3e-9;

// low-pass filter of the resamplers (4x): linear-phase FIR, 48 taps, Kaiser beta 8, cutoff at 22.8 kHz; DC gain 1
#define OS_N 4
#define FIR_N 48
const float kFir[FIR_N] = {-3.06513681e-05f, -8.31105685e-05f, -6.680543e-05f, 0.000152354317f, 0.000592695302f, 0.000975700567f, 0.000740626359f, -0.000581041168f, -0.0027523456f, -0.00445275799f, -0.00369803194f, 0.000798904392f, 0.00798356721f, 0.0138220219f, 0.0127794299f, 0.00133004913f, -0.0182191363f, -0.0362418022f, -0.038577437f, -0.0131996468f, 0.0423044449f, 0.116770435f, 0.18803025f, 0.231622287f, 0.231622287f, 0.18803025f, 0.116770435f, 0.0423044449f, -0.0131996468f, -0.038577437f, -0.0362418022f, -0.0182191363f, 0.00133004913f, 0.0127794299f, 0.0138220219f, 0.00798356721f, 0.000798904392f, -0.00369803194f, -0.00445275799f, -0.0027523456f, -0.000581041168f, 0.000740626359f, 0.000975700567f, 0.000592695302f, 0.000152354317f, -6.680543e-05f, -8.31105685e-05f, -3.06513681e-05f};

struct TDiode { float Is, nVt; };
const TDiode kDiodes[3] = {{2.52e-9f, 1.752f * kVt}, {2.52e-9f, 1.752f * kVt}, {1.0e-18f, 1.9f * kVt}};

inline float Clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double Clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Absf (float v) { return v < 0.0f ? -v : v; }

inline float Ex (float x)
{
	x = Clampf (x, -80.0f, 80.0f);
	const float t = x * 1.44269504f;
	int n = (int) t;
	if ((float) n > t)
	{
		n--;
	}
	const float r = (t - (float) n) * 0.69314718f;
	const float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f + r * (0.041666668f + r * (0.0083333338f + r * (0.0013888889f + r * 0.0001984127f))))));
	union { float f; u32 u; } s;
	s.u = (u32) (n + 127) << 23;
	return p * s.f;
}

inline float Taper (float x, float a)
{
	x = Clampf (x, 0.0f, 1.0f);
	if (a < 0.001f) return x;
	return (Ex (a * x) - 1.0f) / (Ex (a) - 1.0f);
}

struct TRatChan
{
	double x[4];			// C5, C6, voltage across Rd // C4, op-amp output
	double uPrev;
	float hpx, hpy;			// input high-pass (at the normal sampling rate)
	float vC7, i7, vC8, j8, vx;	// C7 coupling, filter capacitor, diode node
	float xh[FIR_N / OS_N];		// input: latest samples (resampler)
	float yh[FIR_N];		// core output: latest samples at 4x
	unsigned yp;
};

class CRatTS : public CFx
{
public:
	CRatTS (float fRate) : m_fRate (fRate), m_nModel (-1), m_nOsCur (0), m_bInit (false)
	{
		for (unsigned c = 0; c < 2; c++)
		{
			ClearChan (m_Ch[c]);
		}
		m_fDist = m_fFilt = m_fVol = 0.5f;
		m_fDistLast = -1.0f;
		m_fAHp = 1.0f / (1.0f + 6.2831853f * (float) (1.0 / (6.2831853 * kR1 * kC1)) / fRate);
	}

	unsigned Type (void) const { return FxRatTS; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		int nModel = (int) (pP[0] + 0.5f);
		nModel = nModel < 0 ? 0 : (nModel > 2 ? 2 : nModel);
		const float fDistT = Clampf (pP[1], 0.0f, 10.0f) * 0.1f;
		const float fFiltT = Clampf (pP[2], 0.0f, 10.0f) * 0.1f;
		const float fVolT = Clampf (pP[3], 0.0f, 10.0f) * 0.1f;
		const int nOs = pP[4] > 0.5f ? OS_N : 1;
		if (!m_bInit)
		{
			m_fDist = fDistT; m_fFilt = fFiltT; m_fVol = fVolT;
			m_bInit = true;
		}
		const float a = 1.0f - Ex (-(float) n / m_fRate / 0.010f);
		m_fDist += a * (fDistT - m_fDist);
		m_fFilt += a * (fFiltT - m_fFilt);
		m_fVol += a * (fVolT - m_fVol);
		Setup (nModel, nOs);

		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			TRatChan &S = m_Ch[c];
			for (unsigned j = 0; j < n; j++)
			{
				p[j] = Sample (S, p[j]);
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
		}
	}

private:
	static void ClearChan (TRatChan &S)
	{
		S.x[0] = S.x[1] = S.x[2] = S.x[3] = 0.0;
		S.uPrev = 0.0;
		S.hpx = S.hpy = 0.0f;
		S.vC7 = S.i7 = S.vC8 = S.j8 = S.vx = 0.0f;
		for (unsigned i = 0; i < FIR_N / OS_N; i++) S.xh[i] = 0.0f;
		for (unsigned i = 0; i < FIR_N; i++) S.yh[i] = 0.0f;
		S.yp = 0;
	}

	// solves M y = v (4 x 4, Gaussian elimination with pivoting)
	static void Solve4 (double M[4][4], double V[4][5], int nCols)
	{
		for (int c = 0; c < 4; c++)
		{
			int p = c;
			for (int r = c + 1; r < 4; r++)
			{
				if ((M[r][c] < 0 ? -M[r][c] : M[r][c]) > (M[p][c] < 0 ? -M[p][c] : M[p][c])) p = r;
			}
			if (p != c)
			{
				for (int k = 0; k < 4; k++) { const double t = M[c][k]; M[c][k] = M[p][k]; M[p][k] = t; }
				for (int k = 0; k < nCols; k++) { const double t = V[c][k]; V[c][k] = V[p][k]; V[p][k] = t; }
			}
			const double d = M[c][c];
			for (int r = c + 1; r < 4; r++)
			{
				const double f = M[r][c] / d;
				for (int k = c; k < 4; k++) M[r][k] -= f * M[c][k];
				for (int k = 0; k < nCols; k++) V[r][k] -= f * V[c][k];
			}
		}
		for (int k = 0; k < nCols; k++)
		{
			for (int r = 3; r >= 0; r--)
			{
				double s = V[r][k];
				for (int q = r + 1; q < 4; q++) s -= M[r][q] * V[q][k];
				V[r][k] = s / M[r][r];
			}
		}
	}

	void Setup (int nModel, int nOs)
	{
		const double T = 1.0 / ((double) m_fRate * nOs);		// core step
		const bool bNew = nModel != m_nModel || nOs != m_nOsCur;
		if (bNew)
		{
			m_nModel = nModel;
			m_nOsCur = nOs;
			m_fDistLast = -1.0f;
			m_fIs = kDiodes[nModel].Is;
			m_fInvNVt = 1.0f / kDiodes[nModel].nVt;
			const double g7 = 2.0 * kC7 / T;
			m_fRs = (float) (kR6 + 1.0 / g7);
			m_fG7 = (float) g7;
			m_fG8 = (float) (2.0 * kC8 / T);
			m_fSlew = (float) ((double) g_RatSr * T);
		}
		const double wt = 6.283185307179586 * (double) g_RatGbw[nModel];
		m_fVsat = g_RatVsat[nModel];
		// Distortion: 100K-A
		if (Absf (m_fDist - m_fDistLast) > 0.0005f || m_fDistLast < 0.0f)
		{
			m_fDistLast = m_fDist;
			const double Rd = (double) g_RatRdMin + (kPotD - (double) g_RatRdMin) * (double) Taper (m_fDist, g_RatTaperD);
			const double t1 = kR4 * kC5, t2 = kR5 * kC6;
			double A[4][4] = {
				{-1.0 / t1, 0.0, -1.0 / t1, 1.0 / t1},
				{0.0, -1.0 / t2, -1.0 / t2, 1.0 / t2},
				{-1.0 / (kR4 * kC4), -1.0 / (kR5 * kC4), -(1.0 / kR4 + 1.0 / kR5 + 1.0 / Rd) / kC4, (1.0 / kR4 + 1.0 / kR5) / kC4},
				{0.0, 0.0, wt, -wt}};
			const double B[4] = {0.0, 0.0, 0.0, wt};
			// trapezoidal rule: (I - h/2 A) x1 = (I + h/2 A) x0 + h/2 B (u0 + u1)  =>  Ad = (I - h/2 A)^-1 (I + h/2 A), Bd = (I - h/2 A)^-1 h/2 B
			double M[4][4], V[4][5];
			for (int r = 0; r < 4; r++)
			{
				for (int c = 0; c < 4; c++)
				{
					M[r][c] = (r == c ? 1.0 : 0.0) - 0.5 * T * A[r][c];
					V[r][c] = (r == c ? 1.0 : 0.0) + 0.5 * T * A[r][c];
				}
				V[r][4] = 0.5 * T * B[r];
			}
			Solve4 (M, V, 5);
			for (int r = 0; r < 4; r++)
			{
				for (int c = 0; c < 4; c++) m_Ad[r][c] = V[r][c];
				m_Bd[r] = V[r][4];
			}
		}
		// Filter: R7 + 100K-A, with C8 to ground
		const double Rt = kR7 + kPotT * (double) Taper (m_fFilt, g_RatTaperT);
		m_fGt = (float) (1.0 / (Rt + 1.0 / (2.0 * kC8 / T)));
		m_fVolR = Taper (m_fVol, g_RatTaperV);
	}

	// diode node: (Vout - ve7 - Vx) / Rs = 2 Is sinh(Vx / nVt) + Gt (Vx - ve8)
	float SolveNode (TRatChan &S, float vOut, float ve7, float ve8)
	{
		float v = S.vx;
		const float k2is = 2.0f * m_fIs;
		for (int it = 0; it < 10; it++)
		{
			const float e = Ex (v * m_fInvNVt), ei = 1.0f / e;
			const float F = (vOut - ve7 - v) / m_fRs - k2is * 0.5f * (e - ei) - m_fGt * (v - ve8);
			const float dF = -1.0f / m_fRs - k2is * m_fInvNVt * 0.5f * (e + ei) - m_fGt;
			float dv = F / dF;
			dv = Clampf (dv, -0.3f, 0.3f);
			v -= dv;
			if (Absf (dv) < 1.0e-6f)
			{
				break;
			}
		}
		return v;
	}

	// one core step (at the effective sampling rate): u in volts, returns the volts before the Volume
	float Core (TRatChan &S, double u)
	{
		// op-amp in its feedback network: trapezoidal rule on the 4 states, output limited by the slew rate and the rails
		double xn[4];
		for (int r = 0; r < 4; r++)
		{
			xn[r] = m_Bd[r] * (S.uPrev + u);
			for (int c = 0; c < 4; c++) xn[r] += m_Ad[r][c] * S.x[c];
		}
		const double sl = (double) m_fSlew;
		xn[3] = S.x[3] + Clampd (xn[3] - S.x[3], -sl, sl);
		const double vs = (double) m_fVsat;
		xn[3] = Clampd (xn[3], -vs, vs);
		for (int r = 0; r < 4; r++) S.x[r] = xn[r];
		S.uPrev = u;
		const float vOut = (float) xn[3];

		// C7 + R6, diodes and Tone filter (C8)
		const float ve7 = S.vC7 + S.i7 / m_fG7;
		const float ve8 = S.vC8 + S.j8 / m_fG8;
		const float vx = SolveNode (S, vOut, ve7, ve8);
		S.vx = vx;
		S.i7 = (vOut - ve7 - vx) / m_fRs;
		S.vC7 = ve7 + S.i7 / m_fG7;
		S.j8 = m_fGt * (vx - ve8);
		S.vC8 = ve8 + S.j8 / m_fG8;
		return S.vC8;
	}

	float Sample (TRatChan &S, float x)
	{
		// input: volts, C1 high-pass
		const float xv = x * kVfs;
		S.hpy = m_fAHp * (S.hpy + xv - S.hpx);
		S.hpx = xv;
		float y;
		if (m_nOsCur == 1)
		{
			y = Core (S, (double) S.hpy);
		}
		else
		{
			// 4x: polyphase interpolation, core run 4 times, filter and decimation
			for (unsigned i = FIR_N / OS_N - 1; i > 0; i--) S.xh[i] = S.xh[i - 1];
			S.xh[0] = S.hpy;
			for (unsigned k = 0; k < OS_N; k++)
			{
				float up = 0.0f;
				for (unsigned j = 0; j < FIR_N / OS_N; j++) up += kFir[OS_N * j + k] * S.xh[j];
				const float c = Core (S, (double) (up * (float) OS_N));
				S.yp = (S.yp + 1) % FIR_N;
				S.yh[S.yp] = c;
			}
			y = 0.0f;
			for (unsigned i = 0; i < FIR_N; i++) y += kFir[i] * S.yh[(S.yp + FIR_N - i) % FIR_N];
		}
		// JFET buffer (gain 1) and Volume
		return y * m_fVolR * g_RatOutTrim / kVfs;
	}

	float m_fRate;
	int m_nModel, m_nOsCur;
	bool m_bInit;
	float m_fDist, m_fFilt, m_fVol, m_fDistLast;
	float m_fAHp, m_fIs, m_fInvNVt, m_fRs, m_fG7, m_fG8, m_fGt, m_fVolR, m_fVsat, m_fSlew;
	double m_Ad[4][4], m_Bd[4];
	TRatChan m_Ch[2];
};

}

CFx *NewRatTS (float fRate)
{
	return new CRatTS (fRate);
}
