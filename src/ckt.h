//
// ckt.h - circuit engine for the REAL pedals: simulation from the NETLIST with the DK method (linear part precomputed, only the nonlinear ports solved with Newton at every sample).
//
// Elements: resistors (also variable: potentiometers), capacitors (trapezoidal rule), voltage sources (input, supply), controlled sources, diodes (single or two in
// antiparallel), bipolar transistors (Ebers-Moll, NPN and PNP, optional base-collector junction), op-amps (macromodel: transconductance with dominant pole, current limit = slew rate,
// output limit to the rails, output resistance).
// The DC operating point is computed at startup (capacitors open, supply ramped to steady state in 24 steps) and the capacitors start from it.
//
// The system is M x = rhs - Q j(p): x = unknowns (node voltages and source currents), p = voltages of the nonlinear ports (node differences), j = port currents.
// With Minv precomputed, only the quantities needed at every sample are kept (voltages of the capacitors, of the ports, of the output): z = z0 - Wz j with z0 linear in the capacitor memories and
// in the sources. F(p) = p - p0 + K j(p) = 0 is solved with Newton (m x m matrix, m = number of ports).
//
#ifndef CKT_ENGINE_H
#define CKT_ENGINE_H

#ifdef CKT_DEBUG
#include <cstdio>
static int g_ckDbg = 0;
#endif
namespace Ck
{

const int MAXU = 52;		// unknowns (nodes and voltage sources)
const int MAXP = 20;		// nonlinear ports
const int MAXC = 32;		// capacitors
const int MAXR = 72;		// resistors
const int MAXV = 4;		// voltage sources
const int MAXG = 12;		// voltage-controlled conductances (VCCS)
const int MAXT = 6;		// injections per port
const int MAXZ = MAXC + MAXP + 1;

typedef unsigned long long U64;

inline double Exp (double x)
{
	if (x > 700.0) x = 700.0;
	if (x < -700.0) x = -700.0;
	const double t = x * 1.4426950408889634;
	const int n = (int) (t + (t >= 0 ? 0.5 : -0.5));		// rounded: |r| <= 0.3466
	const double r = (t - (double) n) * 0.6931471805599453;
	const double p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720 + r * (1.0 / 5040)))))));		// relative error < 6e-9
	union { double d; U64 u; } s;
	s.u = (U64) (n + 1023) << 52;
	return p * s.d;
}

// bounded exponential: above 40 it becomes linear (stable Newton)
inline double ExpL (double x, double *dx)
{
	if (x > 40.0)
	{
		const double e = 2.3538526683702e17;		// e^40
		*dx = e;
		return e * (1.0 + (x - 40.0));
	}
	const double e = Exp (x);
	*dx = e;
	return e;
}

inline double Tanh (double x)
{
	if (x > 20.0) return 1.0;
	if (x < -20.0) return -1.0;
	const double e = Exp (2.0 * x);
	return (e - 1.0) / (e + 1.0);
}

enum PortType { PtDiode = 0, PtClamp = 1, PtGm = 2, PtBjt = 3, PtPair = 4, PtMos = 5 };

struct TPort
{
	int a, b;			// the port voltage is V(a) - V(b)
	int type;
	double c0, c1, c2, c3;		// diode: Is, nVt; clamp: Ic, e, positive Vs, negative Vs; Gm: Imax, gm; BJT: Is, Vt; MOSFET diode: kn, Vth, lambda, smoothing (V)
	double sgn;			// BJT: +1 NPN, -1 PNP
	int nInj;
	int injN[MAXT];
	double injC[MAXT];
};

// the heavy functions must not be copied inline into every pedal (each is 10-15 KB and the kernel has a 2 MB limit): a single shared copy
#if defined(__clang__)
#define CK_NOINLINE __attribute__((noinline))
#else
#define CK_NOINLINE __attribute__((noinline, noclone))
#endif

class TCkt
{
public:
	TCkt (void) : nn (0), nu (0), nr (0), ncap (0), nv (0), ng (0), np (0), nog (0), nOut (0), m_bDirty (false), m_dT (1.0 / 48000.0), m_nIt (0), m_nz (0), ncapLast (0), m_nFail (0), m_fRes (0.0), m_yLast (0.0)
	{
		for (int i = 0; i < MAXP; i++) { pTr[i] = pDc[i] = 0.0; lastJ[i] = 0.0; }
		for (int i = 0; i < MAXC; i++) lastHist[i] = 0.0;
		oc.on = false; oc.port = oc.ns = oc.np = oc.r1 = oc.r2 = 0;
	}

	// brings the object back to the initial state without building a temporary: the object is large (over 100 KB) and "K = TCkt ()" would put a copy on a task's stack and duplicate the copy code in every pedal
	CK_NOINLINE void Clear (void)
	{
		nn = nu = nr = ncap = nv = ng = np = nog = nOut = 0;
		m_bDirty = false; m_dT = 1.0 / 48000.0; m_nIt = 0; m_nz = 0; ncapLast = 0; m_nFail = 0; m_fRes = 0.0; m_yLast = 0.0;
		for (int i = 0; i < MAXP; i++) { pTr[i] = pDc[i] = 0.0; lastJ[i] = 0.0; }
		for (int i = 0; i < MAXC; i++) lastHist[i] = 0.0;
		oc.on = false; oc.port = oc.ns = oc.np = oc.r1 = oc.r2 = 0;
	}

	// ---- construction ----
	int Node (void) { return ++nn; }
	int R (int a, int b, double ohm) { rr[nr].a = a; rr[nr].b = b; rr[nr].v = ohm; return nr++; }
	void SetR (int id, double ohm) { if (ohm < 1e-3) ohm = 1e-3; if (rr[id].v != ohm) { rr[id].v = ohm; m_bDirty = true; } }
	int C (int a, int b, double f) { cc[ncap].a = a; cc[ncap].b = b; cc[ncap].v = f; cc[ncap].hist = 0.0; cc[ncap].be = false; cc[ncap].ind = false; return ncap++; }
	// inductor (henry): trapezoidal rule, a short circuit in DC (1 mohm); in the memories it has the opposite sign to the capacitors
	int Ind (int a, int b, double henry) { const int i = C (a, b, henry); cc[i].ind = true; return i; }
	// capacitor with backward Euler (damps the modes much faster than the step, where the trapezoidal rule oscillates at Nyquist): for the internal pole of the op-amps
	int Cbe (int a, int b, double f) { const int i = C (a, b, f); cc[i].be = true; return i; }
	int V (int p, int n, double val) { vv[nv].p = p; vv[nv].n = n; vv[nv].val = val; return nv++; }
	void SetV (int id, double val) { vv[id].val = val; }
	void Gc (int a, int b, int cp, int cn, double g) { gg[ng].a = a; gg[ng].b = b; gg[ng].cp = cp; gg[ng].cn = cn; gg[ng].g = g; ng++; }
	void Diode (int a, int b, double Is, double nVt) { TPort &P = NewPort (a, b, PtDiode); P.c0 = Is; P.c1 = nVt; Inj (P, a, 1.0); Inj (P, b, -1.0); }
	// two antiparallel diodes (a -> b and b -> a) in a single element: i = Is (e^(v/nVt) - e^(-v/nVt))
	void DiodePair (int a, int b, double Is, double nVt) { TPort &P = NewPort (a, b, PtPair); P.c0 = Is; P.c1 = nVt; Inj (P, a, 1.0); Inj (P, b, -1.0); }
	// diode-connected N-channel MOSFET (gate tied to the drain): the current flows from a (drain and gate) to b (source), i = kn / 2 s^2 (1 + lambda v) with s = smooth max (v - Vth, 0); the body diode is not included
	void MosDiode (int a, int b, double kn, double vth, double lambda) { TPort &P = NewPort (a, b, PtMos); P.c0 = kn; P.c1 = vth; P.c2 = lambda; P.c3 = 0.05; Inj (P, a, 1.0); Inj (P, b, -1.0); }
	// transistor: c, b, e; Is, forward and reverse beta, Vt; with simple = true the base-collector junction is missing (for transistors that never saturate: one port less)
	void Bjt (int c, int b, int e, double Is, double bf, double br, double vt, bool pnp, bool simple = false)
	{
		const double s = pnp ? -1.0 : 1.0;
		TPort &P1 = NewPort (b, e, PtBjt);
		P1.c0 = Is; P1.c1 = vt; P1.sgn = s;
		Inj (P1, c, s); Inj (P1, b, s / bf); Inj (P1, e, -s * (1.0 + 1.0 / bf));
		if (simple) return;
		TPort &P2 = NewPort (b, c, PtBjt);
		P2.c0 = Is; P2.c1 = vt; P2.sgn = s;
		Inj (P2, c, -s * (1.0 + 1.0 / br)); Inj (P2, b, s / br); Inj (P2, e, s);
	}
	// op-amp: inp, inn, output; gain-bandwidth product (Hz), DC gain, slew rate (V/s), swing (peak V around vref), output resistance
	int Opamp (int inp, int inn, int out, int vref, double gbw, double a0, double sr, double vsat, double rout, double vsatNeg = -1.0)
	{
		const int x = Node ();
		const double cx = 1.0e-10, gm = 6.283185307179586 * gbw * cx, rx = a0 / gm, imax = sr * cx;
		R (x, 0, rx);
		C (x, 0, cx);
		TPort &P = NewPort (inp, inn, PtGm);
		P.c0 = imax; P.c1 = gm; Inj (P, x, -1.0);
		TPort &K = NewPort (x, vref, PtClamp);
		K.c0 = 0.01; K.c1 = 0.02; K.c2 = vsat - 0.02; K.c3 = (vsatNeg < 0.0 ? vsat : vsatNeg) - 0.02; Inj (K, x, 1.0); Inj (K, vref, -1.0);
		// one-sided output: current into the output node = (Vo - Vx) / rout
		og[nog].o = out; og[nog].x = x; og[nog].g = 1.0 / rout; nog++;
		return x;
	}
	// algebraic op-amp (no internal state, a single port): output = vref + vsat tanh(A vd / vsat) through rout; it has no gain-bandwidth pole: fine when the
	// closed-loop bandwidth is far above 20 kHz or is limited elsewhere (otherwise use Opamp)
	void OpampA (int inp, int inn, int out, int vref, double a, double vsat, double rout)
	{
		R (out, vref, rout);
		TPort &P = NewPort (inp, inn, PtGm);
		P.c0 = vsat / rout; P.c1 = a / rout;
		Inj (P, out, -1.0); Inj (P, vref, 1.0);
	}
	// OTA (CA3080): the current into the output node is i = Iabc tanh(Vd / (2 Vt)) with Vd = V(inp) - V(inn); Gm port with Imax = Iabc and gm = Iabc / (2 Vt) (RCA, datasheet)
	int Ota (int inp, int inn, int out, double iabc)
	{
		TPort &P = NewPort (inp, inn, PtGm);
		P.c0 = iabc; P.c1 = iabc * 19.34;
		Inj (P, out, -1.0);
		return np - 1;
	}
	// the Iabc current follows the current in the resistors r1 + r2 in series between two nodes (nodeSrc towards the bias pin nodePin): updated after every sample, hence with a one-sample delay
	void OtaControl (int port, int nodeSrc, int nodePin, int r1, int r2) { oc.on = true; oc.port = port; oc.ns = nodeSrc; oc.np = nodePin; oc.r1 = r1; oc.r2 = r2; }
	void SetOut (int node) { nOut = node; }
	void SetStep (double dT) { m_dT = dT; }

	// ---- startup: DC operating point, then the transient matrices ----
	CK_NOINLINE bool Start (void)
	{
		bool ok = StartCore ();
		if (oc.on) for (int k = 0; k < 6; k++) { OtaUpdate (); ok = StartCore () && ok; }		// the Iabc current depends on the operating point: fixed-point iteration
		return ok;
	}
	void OtaUpdate (void)
	{
		if (!oc.on) return;
		double i = (Probe (oc.ns) - Probe (oc.np)) / (rr[oc.r1].v + rr[oc.r2].v);
		if (i < 1.0e-7) i = 1.0e-7;
		if (i > 2.0e-3) i = 2.0e-3;
		pt[oc.port].c0 = i; pt[oc.port].c1 = i * 19.34;
	}
	CK_NOINLINE bool StartCore (void)
	{
		nu = nn + nv;
		for (int i = 0; i < ncap; i++) cc[i].hist = 0.0;
		// DC: capacitors open, supply stepped
		BuildMatrices (true);
		double target[MAXV];
		for (int k = 0; k < nv; k++) target[k] = vv[k].val;
		bool ok = true;
		for (int s = 1; s <= 24; s++)
		{
			for (int k = 0; k < nv; k++) vv[k].val = target[k] * (double) s / 24.0;
			ok = SolveFull () && ok;
		}
		for (int k = 0; k < nv; k++) vv[k].val = target[k];
		// the capacitors start from the DC voltage: i = 0 and therefore hist = gc * vc
		for (int i = 0; i < ncap; i++)
		{
			const double vc = Xv (cc[i].a) - Xv (cc[i].b);
			cc[i].hist = cc[i].ind ? (1.0e3 + Gcap (i)) * vc : Gcap (i) * vc;		// inductor: i = DC current and hist = i + g v
			lastHist[i] = cc[i].hist;
		}
		for (int k = 0; k < np; k++) { pTr[k] = pDc[k]; lastJ[k] = jDc[k]; }
		ncapLast = ncap;
		Rebuild ();
		return ok;
	}

	// voltage of a node in the last solved sample (computed on request)
	double Probe (int n) const
	{
		if (n == 0) return 0.0;
		double s = 0.0;
		const int i = n - 1;
		for (int c = 0; c < ncapLast; c++)
		{
			const double h = cc[c].ind ? -lastHist[c] : lastHist[c];
			if (cc[c].a) s += Minv[i][cc[c].a - 1] * h;
			if (cc[c].b) s -= Minv[i][cc[c].b - 1] * h;
		}
		for (int k = 0; k < nv; k++) s += Minv[i][nn + k] * vv[k].val;
		for (int k = 0; k < np; k++) s -= W[i][k] * lastJ[k];
		return s;
	}

	// one sample: the input voltage is that of generator vIn
	CK_NOINLINE double Run (int vIn, double u)
	{
		vv[vIn].val = u;
		if (m_bDirty) { Rebuild (); m_bDirty = false; }
		const double y = SolveFast ();
		if (oc.on) OtaUpdate ();
		return y;
	}

	int Ports (void) const { return np; }
	int Unknowns (void) const { return nn + nv; }
	int Caps (void) const { return ncap; }
	int Iters (void) const { return m_nIt; }
	int Fails (void) const { return m_nFail; }

private:
	struct TR { int a, b; double v; };
	struct TC { int a, b; double v, hist; bool be, ind; };
	// companion conductance: capacitor 2C/h (trapezoidal) or C/h (Euler); inductor h/(2L)
	double Gcap (int i) const { return cc[i].ind ? m_dT / (2.0 * cc[i].v) : (cc[i].be ? 1.0 : 2.0) * cc[i].v / m_dT; }
	struct TV { int p, n; double val; };
	struct TG { int a, b, cp, cn; double g; };
	struct TO { int o, x; double g; };

	TPort &NewPort (int a, int b, int t) { TPort &P = pt[np++]; P.a = a; P.b = b; P.type = t; P.c0 = P.c1 = P.c2 = P.c3 = 0; P.sgn = 1; P.nInj = 0; return P; }
	void Inj (TPort &P, int node, double c) { if (node == 0) return; P.injN[P.nInj] = node; P.injC[P.nInj] = c; P.nInj++; }

	double Xv (int n) const { return n == 0 ? 0.0 : x[n - 1]; }

	void Stamp (double M[MAXU][MAXU], int a, int b, double g)
	{
		if (a) M[a - 1][a - 1] += g;
		if (b) M[b - 1][b - 1] += g;
		if (a && b) { M[a - 1][b - 1] -= g; M[b - 1][a - 1] -= g; }
	}

	// matrices: M, then Minv (Gauss-Jordan), W = Minv Q, K = Pm W
	void BuildMatrices (bool bDc)
	{
		// the matrix is built directly where the inverse will end up and is inverted in place: no static workspace (it was 65 KB of BSS in the kernel)
		double (*M)[MAXU] = bDc ? MinvDc : Minv;
		for (int i = 0; i < nu; i++) for (int j = 0; j < nu; j++) M[i][j] = 0.0;
		for (int i = 0; i < nn; i++) M[i][i] += 1.0e-12;
		for (int i = 0; i < nr; i++) Stamp (M, rr[i].a, rr[i].b, 1.0 / rr[i].v);
		for (int i = 0; i < ncap; i++)
		{
			if (!bDc) Stamp (M, cc[i].a, cc[i].b, Gcap (i));
			else if (cc[i].ind) Stamp (M, cc[i].a, cc[i].b, 1.0e3);
		}
		for (int k = 0; k < nv; k++)
		{
			const int r = nn + k;
			if (vv[k].p) { M[vv[k].p - 1][r] += 1.0; M[r][vv[k].p - 1] += 1.0; }
			if (vv[k].n) { M[vv[k].n - 1][r] -= 1.0; M[r][vv[k].n - 1] -= 1.0; }
		}
		for (int i = 0; i < ng; i++)
		{
			const TG &G = gg[i];
			if (G.a) { if (G.cp) M[G.a - 1][G.cp - 1] += G.g; if (G.cn) M[G.a - 1][G.cn - 1] -= G.g; }
			if (G.b) { if (G.cp) M[G.b - 1][G.cp - 1] -= G.g; if (G.cn) M[G.b - 1][G.cn - 1] += G.g; }
		}
		for (int i = 0; i < nog; i++)
		{
			if (og[i].o) { M[og[i].o - 1][og[i].o - 1] += og[i].g; M[og[i].o - 1][og[i].x - 1] -= og[i].g; }
		}
		double (*Inv)[MAXU] = M;
		// in-place Gauss-Jordan with partial pivoting: column c, zeroed by the elimination, holds column c of the identity matrix, which is transformed into the inverse; the row swaps
		// are undone at the end by swapping the columns in reverse order
		int piv[MAXU];
		for (int c = 0; c < nu; c++)
		{
			int p = c;
			double best = M[c][c] < 0 ? -M[c][c] : M[c][c];
			for (int r = c + 1; r < nu; r++) { const double v = M[r][c] < 0 ? -M[r][c] : M[r][c]; if (v > best) { best = v; p = r; } }
			piv[c] = p;
			if (p != c) for (int j = 0; j < nu; j++) { const double t = M[c][j]; M[c][j] = M[p][j]; M[p][j] = t; }
			const double d = 1.0 / M[c][c];
			M[c][c] = 1.0;
			for (int j = 0; j < nu; j++) M[c][j] *= d;
			for (int r = 0; r < nu; r++)
			{
				if (r == c) continue;
				const double f = M[r][c];
				if (f == 0.0) continue;
				M[r][c] = 0.0;
				for (int j = 0; j < nu; j++) M[r][j] -= f * M[c][j];
			}
		}
		for (int c = nu - 1; c >= 0; c--)
		{
			if (piv[c] == c) continue;
			for (int r = 0; r < nu; r++) { const double t = M[r][c]; M[r][c] = M[r][piv[c]]; M[r][piv[c]] = t; }
		}
		double (*Wm)[MAXP] = bDc ? WDc : W;
		double (*Km)[MAXP] = bDc ? KDc : K;
		for (int i = 0; i < nu; i++)
		{
			for (int k = 0; k < np; k++)
			{
				double s = 0.0;
				for (int t = 0; t < pt[k].nInj; t++) s += Inv[i][pt[k].injN[t] - 1] * pt[k].injC[t];
				Wm[i][k] = s;
			}
		}
		for (int k = 0; k < np; k++)
		{
			for (int l = 0; l < np; l++)
			{
				double s = 0.0;
				if (pt[k].a) s += Wm[pt[k].a - 1][l];
				if (pt[k].b) s -= Wm[pt[k].b - 1][l];
				Km[k][l] = s;
			}
		}
	}

	// reduced transient matrices: rows z = [capacitor voltages, port voltages, output]
	void Rebuild (void)
	{
		BuildMatrices (false);
		m_nz = ncap + np + 1;
		for (int r = 0; r < m_nz; r++)
		{
			double row[MAXU];
			int a = 0, b = 0;
			if (r < ncap) { a = cc[r].a; b = cc[r].b; }
			else if (r < ncap + np) { a = pt[r - ncap].a; b = pt[r - ncap].b; }
			else { a = nOut; b = 0; }
			for (int i = 0; i < nu; i++)
			{
				double s = 0.0;
				if (a) s += Minv[a - 1][i];
				if (b) s -= Minv[b - 1][i];
				row[i] = s;
			}
			for (int c = 0; c < ncap; c++)
			{
				double s = 0.0;
				if (cc[c].a) s += row[cc[c].a - 1];
				if (cc[c].b) s -= row[cc[c].b - 1];
				Zh[r][c] = cc[c].ind ? -s : s;
			}
			for (int k = 0; k < nv; k++) Zv[r][k] = row[nn + k];
			for (int k = 0; k < np; k++)
			{
				double s = 0.0;
				for (int t = 0; t < pt[k].nInj; t++) s += row[pt[k].injN[t] - 1] * pt[k].injC[t];
				Wz[r][k] = s;
			}
		}
	}

	// current and derivative of a port (the current is the one entering the device in the direction a -> b)
	void Dev (const TPort &P, double v, double *i, double *d) const
	{
		switch (P.type)
		{
		case PtDiode:
		{
			double de;
			const double e = ExpL (v / P.c1, &de);
			*i = P.c0 * (e - 1.0);
			*d = P.c0 * de / P.c1;
			break;
		}
		case PtPair:
		{
			double d1, d2;
			const double e1 = ExpL (v / P.c1, &d1), e2 = ExpL (-v / P.c1, &d2);
			*i = P.c0 * (e1 - e2);
			*d = P.c0 * (d1 + d2) / P.c1;
			break;
		}
		case PtBjt:
		{
			double de;
			const double e = ExpL (P.sgn * v / P.c1, &de);
			*i = P.c0 * (e - 1.0);
			*d = P.sgn * P.c0 * de / P.c1;
			break;
		}
		case PtMos:
		{
			// square law above the threshold with a smooth knee (e = c3): s = (z + sqrt (z^2 + e^2)) / 2
			const double z = v - P.c1, q = __builtin_sqrt (z * z + P.c3 * P.c3), s = 0.5 * (z + q), ds = 0.5 * (1.0 + z / q);
			*i = 0.5 * P.c0 * s * s * (1.0 + P.c2 * v);
			*d = P.c0 * s * ds * (1.0 + P.c2 * v) + 0.5 * P.c0 * s * s * P.c2;
			break;
		}
		case PtClamp:
		{
			// smooth limiting to the rails: i = G (r(v - Vs+) - r(-v - Vs-)) with r(z) = (z + sqrt(z^2 + e^2)) / 2 (derivative between 0 and 1: well conditioned for Newton)
			const double z1 = v - P.c2, z2 = -v - P.c3, s1 = __builtin_sqrt (z1 * z1 + P.c1 * P.c1), s2 = __builtin_sqrt (z2 * z2 + P.c1 * P.c1);
			*i = P.c0 * 0.5 * ((z1 + s1) - (z2 + s2));
			*d = P.c0 * 0.5 * ((1.0 + z1 / s1) + (1.0 + z2 / s2));
			break;
		}
		default:	// PtGm
		{
			const double t = Tanh (P.c1 * v / P.c0);
			*i = P.c0 * t;
			*d = P.c1 * (1.0 - t * t);
			break;
		}
		}
	}

	// residual F(p) = p - p0 + K j(p) (sign changed) and port currents/derivatives; returns max |F|
	double Resid (const double *p, const double *p0, double (*Km)[MAXP], double *j, double *dj, double *F)
	{
		for (int k = 0; k < np; k++) Dev (pt[k], p[k], &j[k], &dj[k]);
		double mx = 0.0;
		for (int k = 0; k < np; k++)
		{
			double s = p[k] - p0[k];
			for (int l = 0; l < np; l++) s += Km[k][l] * j[l];
			F[k] = -s;
			const double a = s < 0 ? -s : s;
			if (a > mx) mx = a;
		}
		return mx;
	}

	// Newton on the ports with a damped step (the step is halved until the residual drops): p (in/out), linear p0, matrix K (np x np), final currents in j
	bool Newton (double *p, const double *p0, double (*Km)[MAXP], double *j)
	{
		double dj[MAXP], F[MAXP];
		m_nIt = 0;
		bool conv = false;
		double fmax = Resid (p, p0, Km, j, dj, F);
		if (fmax < 1.0e-8) return true;
		for (int it = 0; it < 60; it++)
		{
			m_nIt = it + 1;
			double J[MAXP][MAXP], dp[MAXP], Fs[MAXP];
			for (int k = 0; k < np; k++)
			{
				Fs[k] = F[k];
				for (int l = 0; l < np; l++) J[k][l] = Km[k][l] * dj[l] + (k == l ? 1.0 : 0.0);
			}
			for (int c = 0; c < np; c++)
			{
				int pr = c;
				double best = J[c][c] < 0 ? -J[c][c] : J[c][c];
				for (int r = c + 1; r < np; r++) { const double v = J[r][c] < 0 ? -J[r][c] : J[r][c]; if (v > best) { best = v; pr = r; } }
				if (pr != c) { for (int q = 0; q < np; q++) { const double t = J[c][q]; J[c][q] = J[pr][q]; J[pr][q] = t; } const double t = Fs[c]; Fs[c] = Fs[pr]; Fs[pr] = t; }
				const double dg = J[c][c];
				if (dg == 0.0) return false;
				for (int r = c + 1; r < np; r++)
				{
					const double f = J[r][c] / dg;
					if (f == 0.0) continue;
					for (int q = c; q < np; q++) J[r][q] -= f * J[c][q];
					Fs[r] -= f * Fs[c];
				}
			}
			for (int r = np - 1; r >= 0; r--)
			{
				double s = Fs[r];
				for (int q = r + 1; q < np; q++) s -= J[r][q] * dp[q];
				dp[r] = s / J[r][r];
			}
			double mx = 0.0;
			for (int k = 0; k < np; k++) { const double a = dp[k] < 0 ? -dp[k] : dp[k]; if (a > mx) mx = a; }
			// per-port step limit (like SPICE): only the growth in forward bias of the junctions is limited, reverse movements are free
			for (int k = 0; k < np; k++)
			{
				const TPort &P = pt[k];
				if (P.type != PtDiode && P.type != PtBjt && P.type != PtPair) continue;
				const double sg = P.type == PtBjt ? P.sgn : 1.0;
				const double lim = P.c1 * 14.0, inc = P.c1 * 3.0;
				double xo = sg * p[k], xn = xo + sg * dp[k];
				if (P.type == PtPair) { xo = xo < 0 ? -xo : xo; xn = sg * (p[k] + dp[k]); xn = xn < 0 ? -xn : xn; }
				if (P.type == PtClamp)
				{
					// excess beyond the threshold (the clamp conducts exponentially beyond positive or negative Vs)
					const double eo = p[k] - P.c2, eo2 = -p[k] - P.c3, en = p[k] + dp[k] - P.c2, en2 = -(p[k] + dp[k]) - P.c3;
					xo = eo > eo2 ? eo : eo2; xn = en > en2 ? en : en2;
					if (xn > xo && xn > -lim)
					{
						const double xl = (xo < -lim ? -lim : xo) + inc;
						if (xn > xl) { const double f = (xl - xo) / (xn - xo); dp[k] *= f; }
					}
					continue;
				}
				if (xn > xo && xn > lim)
				{
					const double xl = (xo < lim ? lim : xo) + inc;
					if (xn > xl)
					{
						const double f = (xl - xo) / (xn - xo);
						dp[k] *= f;
					}
				}
			}
			for (int k = 0; k < np; k++)
			{
				if (pt[k].type != PtGm) continue;
				const double vl = 8.0 * pt[k].c0 / pt[k].c1 + 6.0;		// with the output saturated the input difference can be several volts
				const double pn2 = p[k] + dp[k];
				if (pn2 > vl) dp[k] = vl - p[k]; else if (pn2 < -vl) dp[k] = -vl - p[k];
			}
			mx = 0.0;
			for (int k = 0; k < np; k++) { const double a = dp[k] < 0 ? -dp[k] : dp[k]; if (a > mx) mx = a; }
			double s = 1.0;
			double pn[MAXP], jn[MAXP], djn[MAXP], Fn[MAXP], fn = 0.0;
			for (int bt = 0; bt < 5; bt++)
			{
				for (int k = 0; k < np; k++) pn[k] = p[k] + s * dp[k];
				fn = Resid (pn, p0, Km, jn, djn, Fn);
				if (fn <= 2.0 * fmax || fn < 1.0e-8) break;		// a somewhat higher residual is also accepted (pure Newton), but not a jump that makes it explode
				s *= 0.5;
			}			for (int k = 0; k < np; k++) { p[k] = pn[k]; j[k] = jn[k]; dj[k] = djn[k]; F[k] = Fn[k]; }
			fmax = fn;
#ifdef CKT_DEBUG
			if (g_ckDbg && it >= 0 && it < 14) { printf ("  it %d: step %.3g s %.3g fmax %.3g\n     p:", it, mx, s, fmax); for (int k = 0; k < np; k++) printf (" %8.4f", p[k]); printf ("\n     F:"); for (int k = 0; k < np; k++) printf (" %8.4f", F[k]); printf ("\n    dp:"); for (int k = 0; k < np; k++) printf (" %8.4f", dp[k]); printf ("\n"); }
#endif
			if (mx * s < 5.0e-6 || fn < 1.0e-8) { conv = true; break; }
		}
		m_fRes = fmax;
		return conv;
	}
	// DC (used at startup): full solution with the capacitors open
	bool SolveFull (void)
	{
		double rhs[MAXU];
		for (int i = 0; i < nu; i++) rhs[i] = 0.0;
		for (int k = 0; k < nv; k++) rhs[nn + k] = vv[k].val;
		double x0[MAXU];
		for (int i = 0; i < nu; i++)
		{
			double s = 0.0;
			for (int j = 0; j < nu; j++) s += MinvDc[i][j] * rhs[j];
			x0[i] = s;
		}
		double p0[MAXP];
		for (int k = 0; k < np; k++) p0[k] = (pt[k].a ? x0[pt[k].a - 1] : 0.0) - (pt[k].b ? x0[pt[k].b - 1] : 0.0);
		const bool conv = Newton (pDc, p0, KDc, jDc);
		for (int i = 0; i < nu; i++)
		{
			double s = x0[i];
			for (int k = 0; k < np; k++) s -= WDc[i][k] * jDc[k];
			x[i] = s;
		}
		return conv;
	}

	// transient: only the reduced quantities
	double SolveFast (void)
	{
		double z0[MAXZ];
		for (int r = 0; r < m_nz; r++)
		{
			double s = 0.0;
			for (int c = 0; c < ncap; c++) s += Zh[r][c] * cc[c].hist;
			for (int k = 0; k < nv; k++) s += Zv[r][k] * vv[k].val;
			z0[r] = s;
		}
		double p0[MAXP], j[MAXP];
		for (int k = 0; k < np; k++) p0[k] = z0[ncap + k];
		double Kp[MAXP][MAXP];
		for (int k = 0; k < np; k++) for (int l = 0; l < np; l++) Kp[k][l] = Wz[ncap + k][l];
		for (int k = 0; k < np; k++) pSave[k] = pTr[k];
		{
			// initial estimate: the previous state or the one with frozen currents (p0 - K previous j), i.e. the better of the two
			double pg[MAXP], jg[MAXP], djg[MAXP], Fg[MAXP], jp[MAXP], djp[MAXP], Fp[MAXP];
			for (int k = 0; k < np; k++) { double s = p0[k]; for (int l = 0; l < np; l++) s -= Kp[k][l] * lastJ[l]; pg[k] = s; }
			const double fa = Resid (pTr, p0, Kp, jp, djp, Fp), fb = Resid (pg, p0, Kp, jg, djg, Fg);
			if (fb < fa) for (int k = 0; k < np; k++) pTr[k] = pg[k];
		}
		Newton (pTr, p0, Kp, j);
		if (!(m_fRes < 1.0e-3))
		{
			// non-convergence: go back to the last valid state and keep the last output (no advance of the capacitors)
			for (int k = 0; k < np; k++) pTr[k] = pSave[k];
			m_nFail++;
			return m_yLast;
		}
		for (int c = 0; c < ncap; c++)
		{
			double vc = z0[c];
			for (int l = 0; l < np; l++) vc -= Wz[c][l] * j[l];
			lastHist[c] = cc[c].hist;
			cc[c].hist = cc[c].ind ? 2.0 * Gcap (c) * vc + cc[c].hist : (cc[c].be ? Gcap (c) * vc : 2.0 * Gcap (c) * vc - cc[c].hist);
		}
		ncapLast = ncap;
		for (int l = 0; l < np; l++) lastJ[l] = j[l];
		double y = z0[m_nz - 1];
		for (int l = 0; l < np; l++) y -= Wz[m_nz - 1][l] * j[l];
		m_yLast = y;
		return y;
	}

	int nn, nu, nr, ncap, nv, ng, np, nog, nOut;
	bool m_bDirty;
	double m_dT;
	int m_nIt, m_nz, ncapLast, m_nFail;
	double m_fRes, m_yLast;
	double pSave[MAXP];
	TR rr[MAXR];
	TC cc[MAXC];
	TV vv[MAXV];
	TG gg[MAXG];
	TO og[4];
	TPort pt[MAXP];
	double Minv[MAXU][MAXU], MinvDc[MAXU][MAXU];
	double W[MAXU][MAXP], WDc[MAXU][MAXP];
	double K[MAXP][MAXP], KDc[MAXP][MAXP];
	double Zh[MAXZ][MAXC], Zv[MAXZ][MAXV], Wz[MAXZ][MAXP];
	double x[MAXU];
	double pTr[MAXP], pDc[MAXP], jDc[MAXP];
	double lastHist[MAXC], lastJ[MAXP];
	struct TOc { bool on; int port, ns, np, r1, r2; } oc;
};

}

#endif
