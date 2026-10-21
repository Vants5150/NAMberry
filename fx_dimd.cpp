//
// fx_dimd.cpp - Dimension D Real: Roland SDD-320 Dimension D (1979), reconstructed from the service notes (schematic, table of adjustments, waveforms).
//
// Fonti (2026-10-19):
//  - Roland SDD-320 Service Note, 30 Oct 1979 (5-page PDF, link from the UAD thread "Dimension D and the all buttons down mode"): schematic on page 3, "Checking & Adjustment" on page 4.
//  - Page 4, "Checking output waveforms": delay excursion (from a to b) with a 40 Hz square wave at the input: mode 1 = 8-12 ms, mode 2 = 5-10 ms, mode 3 = 6-9 ms, mode 4 = 6-9 ms
//    (in mode 4 only the negative pulse "c" increases, that is the wet part is stronger).
//  - Page 3, next to the "DIMENSION MODE" block: triangular LFO wave, period 4 s for modes 1 and 2, 2 s for modes 3 and 4 (that is 0.25 Hz and 0.5 Hz), amplitude 6.87 Vpp (1, 2) and 4.4 Vpp (3, 4).
//  - UAD thread (engineers who measured the unit): buttons 1, 2, 3 exclude each other with priority 1 > 2 > 3 (they are three clock variation ranges of the BBDs), button 4 does not change
//    the delay but the gain of a filter in the wet path; with 4 alone you get mode 3+4. The real combinations are therefore six: 1, 1+4, 2, 2+4, 3, 3+4. Four MN3009 BBDs per channel
//    (256 stages each, 1024 stages in all), NE570 compander, the two channels' LFOs in antiphase, no other controls.
//  - Page 3 (schematic, components read from the scan): after the BBDs the wet path goes through a Sallen-Key filter (R137 = R138 = 12 kohm, C 5.6 nF in feedback, C 3.9 nF to ground) followed by an
//    emitter follower and by a three-section RC ladder (12 kohm with 2.7 nF, 4.7 nF and 330 pF); the emphasis before the BBDs (C108/R112, C109/R113 and 22 kohm feedback) and the de-emphasis after (R149-R151, C147-C148)
//    are the opposite of each other (overall flat gain); high-pass: C109 with R113 (15 Hz) and C145 with R147 (72 Hz). On the dry and wet mixing nodes (C and A) the schematic's annotation is +4 dBm for both.
//
// Model: fractional delay (cubic interpolation) driven by a triangular LFO, one per channel, in antiphase; the average delay and the excursion depend on the mode as above. The wet path's
// filter is derived from the component values (state space, trapezoidal rule, double precision). Output = half dry + half wet, where the wet of each output is mostly that of the OPPOSITE input
// (Gearspace thread on the Klark Teknik BBD-320 clone, post #436 by Synthbuilder, who measured an SDD-320 and a BBD-320: mode 1 medium depth, mode 2 large, modes 3 and 4 small; LFO 0.25 Hz in modes 1 and 2,
// 0.5 Hz in modes 3 and 4; mode 4 increases the small share of same-side wet; same delays 8-12, 5-10, 6-9 ms as the Roland table).
// NOT verified against recordings of the real unit (I have none): the values read from the low-quality scan (in particular the second capacitor of the Sallen-Key section and the 330 pF one) may have
// errors; the same-side wet shares (kSameLow, kSameHigh) are ESTIMATES (the sources only say "low" and "high"); the NE570 compander (noise reduction) and the BBDs' distortion are not modelled.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

// modes: minimum and maximum delay (ms), LFO period (s)
struct TMode
{
	float fMin, fMax, fHz;
};

const TMode kMode[3] = {{8.0f, 12.0f, 0.25f}, {5.0f, 10.0f, 0.25f}, {6.0f, 9.0f, 0.5f}};
const float kSameLow = 0.2f, kSameHigh = 0.7f;	// ESTIMATES: same-side wet share (low in modes 1-3, high with button 4): the thread only says "low" and "high"

const unsigned kBuf = 2048;			// samples of the ring (power of 2): 42 ms at 48 kHz

// linear system with N states discretized with the trapezoidal rule: x1 = Ad x0 + Bd (u0 + u1), output = x[out]
template <unsigned N> struct TLin
{
	double Ad[N][N], Bd[N], x[N];
	double u1;
	unsigned out;

	void Init (const double A[N][N], const double B[N], unsigned nOut, double h)
	{
		out = nOut;
		double M[N][2 * N];
		for (unsigned i = 0; i < N; i++)
		{
			for (unsigned j = 0; j < N; j++)
			{
				M[i][j] = (i == j ? 1.0 : 0.0) - 0.5 * h * A[i][j];
				M[i][N + j] = (i == j ? 1.0 : 0.0);
			}
		}
		for (unsigned c = 0; c < N; c++)		// Gauss-Jordan (small, well-conditioned matrices)
		{
			unsigned p = c;
			for (unsigned r = c + 1; r < N; r++)
			{
				if (M[r][c] * M[r][c] > M[p][c] * M[p][c]) p = r;
			}
			for (unsigned j = 0; j < 2 * N; j++) { const double t = M[c][j]; M[c][j] = M[p][j]; M[p][j] = t; }
			const double d = M[c][c];
			for (unsigned j = 0; j < 2 * N; j++) M[c][j] /= d;
			for (unsigned r = 0; r < N; r++)
			{
				if (r == c) continue;
				const double f = M[r][c];
				for (unsigned j = 0; j < 2 * N; j++) M[r][j] -= f * M[c][j];
			}
		}
		for (unsigned i = 0; i < N; i++)
		{
			Bd[i] = 0.0;
			for (unsigned j = 0; j < N; j++)
			{
				Ad[i][j] = 0.0;
				for (unsigned k = 0; k < N; k++)
				{
					Ad[i][j] += M[i][N + k] * ((k == j ? 1.0 : 0.0) + 0.5 * h * A[k][j]);
				}
				Bd[i] += M[i][N + j] * 0.5 * h * B[j];
			}
			x[i] = 0.0;
		}
		u1 = 0.0;
	}

	float Run (float u)
	{
		double n[N];
		const double s = (double) u + u1;
		for (unsigned i = 0; i < N; i++)
		{
			double a = Bd[i] * s;
			for (unsigned j = 0; j < N; j++) a += Ad[i][j] * x[j];
			n[i] = a;
		}
		for (unsigned i = 0; i < N; i++) x[i] = n[i];
		u1 = u;
		return (float) x[out];
	}
};

// first-order high-pass (bilinear)
struct THp
{
	float b, a, x1, y1;
	void Init (float fHz, float fRate)
	{
		const float w = 3.14159265f * fHz / fRate;
		const float t = DspSin (w) / DspCos (w);
		b = 1.0f / (1.0f + t);
		a = (1.0f - t) / (1.0f + t);
		x1 = y1 = 0.0f;
	}
	float Run (float x)
	{
		const float y = b * (x - x1) + a * y1;
		x1 = x; y1 = y;
		return y;
	}
};

// wet path of one channel: BBD (delay), the schematic's filters
struct TChan
{
	float buf[kBuf];
	unsigned w;
	TLin<2> sk;
	TLin<3> lad;
	THp hp1, hp2;
};

class CDimD : public CFx
{
public:
	CDimD (float fRate) : m_fRate (fRate), m_fPh (0.0f)
	{
		const double h = 1.0 / (double) fRate;
		const double R = 12e3, Cf = 5.6e-9, Cg = 3.9e-9;
		const double Ask[2][2] = {{0.0, 1.0 / (R * Cg)}, {-1.0 / (R * Cf), -1.0 / (R * Cf) - 1.0 / (R * Cf)}};
		const double Bsk[2] = {0.0, 1.0 / (R * Cf)};
		const double c1 = 2.7e-9, c2 = 4.7e-9, c3 = 330e-12;
		const double Al[3][3] = {{-2.0 / (R * c1), 1.0 / (R * c1), 0.0}, {1.0 / (R * c2), -2.0 / (R * c2), 1.0 / (R * c2)}, {0.0, 1.0 / (R * c3), -1.0 / (R * c3)}};
		const double Bl[3] = {1.0 / (R * c1), 0.0, 0.0};
		for (unsigned c = 0; c < 2; c++)
		{
			TChan &C = m_Ch[c];
			memset (C.buf, 0, sizeof C.buf);
			C.w = 0;
			C.sk.Init (Ask, Bsk, 0, h);
			C.lad.Init (Al, Bl, 2, h);
			C.hp1.Init (15.0f, fRate);
			C.hp2.Init (72.0f, fRate);
		}
		m_fMid = 10.0f; m_fHalf = 2.0f; m_fWet = kSameLow;
		const float xs = 1.0f / (fRate * 0.03f);		// time constant of 30 ms: 1 - exp(-xs) with a tiny xs
		m_fSm = xs - 0.5f * xs * xs;
	}

	unsigned Type (void) const { return FxDimD; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		// mode: 0 = 1, 1 = 1+4, 2 = 2, 3 = 2+4, 4 = 3, 5 = 3+4
		int mi = (int) (pP[0] + 0.5f);
		mi = mi < 0 ? 0 : (mi > 5 ? 5 : mi);
		const TMode &M = kMode[mi >> 1];
		const bool bMonoIn = pP[1] > 0.5f;
		if (*pMono)
		{
			memcpy (pR, pL, n * sizeof (float));
			*pMono = 0;
		}
		const float fTMid = 0.5f * (M.fMin + M.fMax), fTHalf = 0.5f * (M.fMax - M.fMin);
		const float fTWet = (mi & 1) ? kSameHigh : kSameLow;
		const float dPh = M.fHz / m_fRate;
		const float msToS = 0.001f * m_fRate;
		for (unsigned j = 0; j < n; j++)
		{
			m_fMid += m_fSm * (fTMid - m_fMid);
			m_fHalf += m_fSm * (fTHalf - m_fHalf);
			m_fWet += m_fSm * (fTWet - m_fWet);
			float x[2] = {pL[j], pR[j]};
			if (bMonoIn)
			{
				x[0] = x[1] = 0.5f * (x[0] + x[1]);
			}
			float wet[2];
			for (unsigned c = 0; c < 2; c++)
			{
				TChan &C = m_Ch[c];
				C.buf[C.w] = x[c];
				float ph = m_fPh + (c ? 0.5f : 0.0f);
				ph -= (float) (int) ph;
				const float tri = 4.0f * (ph < 0.5f ? 0.5f - ph : ph - 0.5f) - 1.0f;		// +1 at phase 0, -1 at phase 0.5
				const float d = (m_fMid + m_fHalf * tri) * msToS;
				const int di = (int) d;
				const float f = d - (float) di;
				// s(k) = sample with delay k; 4-point Lagrange between s(di) and s(di + 1)
				const unsigned m = kBuf - 1;
				const float sm1 = C.buf[(C.w - (unsigned) (di - 1)) & m], s0 = C.buf[(C.w - (unsigned) di) & m];
				const float s1 = C.buf[(C.w - (unsigned) (di + 1)) & m], s2 = C.buf[(C.w - (unsigned) (di + 2)) & m];
				const float cm1 = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
				const float c0 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
				const float c1 = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
				const float c2 = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);
				const float w = cm1 * sm1 + c0 * s0 + c1 * s1 + c2 * s2;
				wet[c] = C.hp2.Run (C.hp1.Run (C.lad.Run (C.sk.Run (w))));
				C.w = (C.w + 1) & m;
			}
			// most of the wet of an output comes from the OPPOSITE input, a small part (m_fWet, higher with button 4) from the same side
			pL[j] = 0.5f * x[0] + 0.5f * (wet[1] + m_fWet * wet[0]);
			pR[j] = 0.5f * x[1] + 0.5f * (wet[0] + m_fWet * wet[1]);
			m_fPh += dPh;
			m_fPh -= (float) (int) m_fPh;
		}
	}

private:
	float m_fRate, m_fPh, m_fMid, m_fHalf, m_fWet, m_fSm;
	TChan m_Ch[2];
};

}

CFx *NewDimD (float fRate)
{
	return new CDimD (fRate);
}
