//
// fx_univibe.cpp - Uni-Vibe Real: Shin-ei / Univox Uni-Vibe (late 1960s), four all-pass phase stages swept by one lamp and four light-dependent resistors, Chorus or Vibrato output.
//
// Sources (2026-10-21):
//  - C. Darabundit, R. Wedelich, P. Bischoff, "Digital Grey Box Model of the Uni-Vibe Effects Pedal", DAFx-19, Birmingham 2019 (https://ccrma.stanford.edu/~champ/files/DAFx2019_paper_31.pdf):
//    phase stage = superposition of the inverting leg (collector, gain -beta) and the non-inverting leg (emitter, gain alpha) with the block capacitor CDC (equations 3-10, 14-18):
//    H(s) = (alpha kappa_e wo - beta (kappa_c wo + s)) / (wo + s), kappa_c = Cp / (Cp + CDC), kappa_e = CDC / (Cp + CDC), wo = (CDC + Cp) / (R0 Cp CDC), R0 = LDR + R6;
//    Table 1 (measured on an original unit): Cp 0.015 uF / 0.22 uF / 470 pF / 0.0047 uF, alpha 1.01 / 0.98 / 0.97 / 0.95, beta 1.11 / 1.09 / 1.10 / 1.09, LDR average 405 / 233 / 290 / 240 kohm,
//    minimum 12.7 / 6.86 / 7.69 / 6.22 kohm, maximum 2.79 / 2.59 / 3.32 / 4.16 Mohm; R0 = 409.7 kohm for the mean of LDR1 (Figure 6) gives R6 = 4.7 kohm; the Speed goes from 0 to about 7.6 Hz
//    (toe to heel); the Intensity scales the LFO voltage across the lamp; Chorus mixes the output of the last stage evenly with the "dry" signal of the first, Vibrato sends only the last stage;
//    the emitter followers clip asymmetrically (tanh with a bias and a scale chosen by ear in the paper), followed by a DC blocker.
//  - R. G. Keen, "The Technology of the Univibe", Geofex (http://www.geofex.com/article_folders/univibe/univtech.htm): input 22k + 22k and 47k to ground, preamp as a discrete op-amp with gain
//    1 + 3.9k / 1.2k (about 4), phase stages with the block capacitor of 1 uF in series with the LDR, 15 V supply with the base of each stage at about 4.8 V (emitter 3.4 V, collector 11.6 V: about 3.4 V
//    of peak headroom), output mixer: dry and wet each through 100k to the Chorus contact, Vibrato through 47k / 220k, volume 100k; lamp 28 V / 40 mA (cold resistance just over 100 ohm), thermal
//    time constant that averages the current at high speeds; the LFO amplitude grows with the speed (explicitly: a first-order compensation of the falling response of the bulb).
//
// What is NOT in the sources and is ASSUMED here (not verified against recordings of a real unit):
//  - the shape of the lamp -> LDR transfer: the wavetables measured in the paper are not published. Here the brightness x (0 = dark, 1 = very bright) sets the LDR as R = Rmin^x Rmax^(1 - x)
//    (logarithmic between the measured minimum and maximum of Table 1) and the static point x_c is the one that gives the average resistance of Table 1 (0.36-0.44);
//  - lamp: power (1 + m sin)^2 through a first-order thermal filter of 20 ms, brightness x = x_c P for P < 1 and x_c + (1 - x_c)(P - 1) / 3 above (P = 4 is the full-modulation peak: "almost dark to very bright");
//  - LDR dynamics: first order, 8 ms when the light increases (resistance falls) and 30 ms when it decreases (the paper only says that turn-on and turn-off times differ);
//  - LFO amplitude m = Intensity (0.6 + 0.4 Speed / 7.6) (rises with the speed as Geofex describes, the law is mine), sinusoidal;
//  - clipping: asymmetric tanh with a 3.4 V scale (Geofex headroom) and a bias of 0.1 of it (the paper does not give its values) in the preamp and in every stage; DC blocker at 10 Hz at the output;
//  - the effect is mono like the original: a stereo input is processed by two copies that share the lamp and the LDRs.
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

const double kPi = 3.14159265358979323846;
const double kR6 = 4.7e3, kCdc = 1.0e-6;
const double kVfs = 1.305;			// 0 dBFS = +1.5 dBu = 1.305 V peak (same calibration as the other REAL pedals)
const double kVh = 3.4;				// peak headroom of the stages (Geofex)

// Table 1 of Darabundit et al.: Cp, alpha, beta; ln of the minimum and maximum LDR resistance; static brightness giving the average resistance
struct TStageDef
{
	double cp, alpha, beta, lnRmin, lnRmax, xc;
};

const TStageDef kSt[4] = {
	{15.0e-9, 1.01, 1.11, 9.449357, 14.841552, 0.3579},
	{220.0e-9, 0.98, 1.09, 8.833463, 14.767168, 0.4059},
	{470.0e-12, 0.97, 1.10, 8.947676, 15.015475, 0.4018},
	{4.7e-9, 0.95, 1.09, 8.735525, 15.241026, 0.4385}};

inline double Exp (double x)
{
	return (double) DbToLin ((float) (x < -80.0 ? -80.0 : (x > 80.0 ? 80.0 : x)) * 8.685889638f);
}

inline float Tanhf (float x)
{
	if (x > 3.0f) return 1.0f;
	if (x < -3.0f) return -1.0f;
	const float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// sine of a phase p in [0, 1)
inline float Sin1 (float p)
{
	float x = 3.14159265f - p * 6.2831853f;
	const float ax = x < 0.0f ? -x : x;
	const float y = 1.27323954f * x - 0.40528473f * x * ax;
	const float ay = y < 0.0f ? -y : y;
	return 0.225f * (y * ay - y) + y;
}

// tan of an angle in [0, 1.4] radians
inline double Tan (double x)
{
	const double p = x / 6.283185307179586;
	const double c = (double) Sin1 ((float) (p + 0.25 - (p + 0.25 >= 1.0 ? 1.0 : 0.0)));
	return (double) Sin1 ((float) p) / (c < 1e-3 ? 1e-3 : c);
}

// asymmetric soft clipping of an emitter follower (volts): tanh with a bias, DC removed
inline float Clip (float v)
{
	const float b = 0.1f;
	return (float) kVh * (Tanhf (v * (1.0f / (float) kVh) + b) - Tanhf (b));
}

// one phase stage: H(s) = (n0 - beta s) / (wo + s) by the bilinear transform
struct TStage
{
	double b0, b1, a1;
	float x1, y1;

	void Reset (void) { x1 = y1 = 0.0f; b0 = 1.0; b1 = 0.0; a1 = 0.0; }

	// rLdr: resistance of the LDR (ohm)
	void Coef (const TStageDef &d, double rLdr, double fs)
	{
		const double c = 2.0 * fs;
		const double r0 = rLdr + kR6;
		const double ke = kCdc / (d.cp + kCdc), kc = d.cp / (d.cp + kCdc);
		double wo = (1.0 / r0) * (kCdc + d.cp) / (d.cp * kCdc);
		double q = wo / c;
		if (q > 1.35) q = 1.35;
		wo = c * Tan (q);					// pre-warping: the centre frequency stays where the analog circuit has it
		const double n0 = (d.alpha * ke - d.beta * kc) * wo;
		const double den = wo + c;
		b0 = (n0 - d.beta * c) / den;
		b1 = (n0 + d.beta * c) / den;
		a1 = (wo - c) / den;
	}

	inline float Run (float x)
	{
		const float y = (float) b0 * x + (float) b1 * x1 - (float) a1 * y1;
		x1 = x;
		y1 = y;
		return y;
	}
};

class CUniVibe : public CFx
{
public:
	CUniVibe (float fRate) : m_fRate (fRate)
	{
		m_fPh = 0.0f;
		m_fPow = 1.0f;
		m_nCtl = 0;
		for (unsigned s = 0; s < 4; s++)
		{
			m_fXl[s] = (float) kSt[s].xc;
			m_dR[s] = Exp (kSt[s].xc * kSt[s].lnRmin + (1.0 - kSt[s].xc) * kSt[s].lnRmax);
			for (unsigned c = 0; c < 2; c++)
			{
				m_Ch[c].st[s].Reset ();
				m_Ch[c].st[s].Coef (kSt[s], m_dR[s], (double) fRate);
			}
		}
		for (unsigned c = 0; c < 2; c++)
		{
			m_Ch[c].hx = m_Ch[c].hy = 0.0f;
		}
		m_fSmVol = 1.0f;
	}

	unsigned Type (void) const { return FxUniVibe; }

	// LDR resistances (ohm) of the four stages, for the tests
	void Resistances (double *pR) const { for (unsigned s = 0; s < 4; s++) pR[s] = m_dR[s]; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		float fSpeed = pP[0], fInt = pP[1] * 0.01f, fVol = pP[3];
		fSpeed = fSpeed < 0.0f ? 0.0f : (fSpeed > 7.6f ? 7.6f : fSpeed);
		fInt = fInt < 0.0f ? 0.0f : (fInt > 1.0f ? 1.0f : fInt);
		fVol = fVol < 0.0f ? 0.0f : (fVol > 10.0f ? 10.0f : fVol);
		const boolean bVib = pP[2] > 0.5f;
		const float fVolG = (fVol * 0.1f) * (fVol * 0.1f);
		const unsigned nCh = *pMono ? 1 : 2;
		const float fPreG = 0.6812f * 4.25f;				// input divider 47k / (22k + 47k) and discrete op-amp 1 + 3.9k / 1.2k
		const float fMixC = 1.0f / 3.0f, fMixV = 0.594f;		// Chorus: (dry + wet) / 3 (100k / 100k into the 100k volume); Vibrato: 47k / 220k divider into the 100k volume
		const float fToV = (float) kVfs, fToS = 1.0f / (float) kVfs;
		const float dPh = fSpeed / m_fRate;
		const float aOn = 1.0f - (float) Exp (-8.0 / (0.008 * (double) m_fRate)), aOff = 1.0f - (float) Exp (-8.0 / (0.030 * (double) m_fRate));		// per update of 8 samples
		const float aTh = 1.0f - (float) Exp (-1.0 / (0.020 * (double) m_fRate));
		const float mAmp = fInt * (0.6f + 0.4f * fSpeed * (1.0f / 7.6f));
		const float aDc = 1.0f - (float) Exp (-6.283185307 * 10.0 / (double) m_fRate);
		const float aVol = 1.0f - (float) Exp (-1.0 / (0.01 * (double) m_fRate));
		for (unsigned j = 0; j < n; j++)
		{
			// lamp: power (1 + m sin)^2 through the thermal filter, brightness of each LDR (they share the lamp and have their own static point)
			const float l = Sin1 (m_fPh);
			const float d = 1.0f + mAmp * l;
			m_fPow += aTh * (d * d - m_fPow);
			m_fPh += dPh;
			if (m_fPh >= 1.0f) m_fPh -= 1.0f;
			if ((m_nCtl++ & 7) == 0)
			{
				for (unsigned s = 0; s < 4; s++)
				{
					const double xc = kSt[s].xc;
					const double P = (double) m_fPow;
					double xt = P < 1.0 ? xc * P : xc + (1.0 - xc) * (P - 1.0) / 3.0;
					xt = xt < 0.0 ? 0.0 : (xt > 1.0 ? 1.0 : xt);
					m_fXl[s] += (xt > (double) m_fXl[s] ? aOn : aOff) * ((float) xt - m_fXl[s]);		// the LDR follows the brightness with its own time constant (updated every 8 samples)
					m_dR[s] = Exp ((double) m_fXl[s] * kSt[s].lnRmin + (1.0 - (double) m_fXl[s]) * kSt[s].lnRmax);
					for (unsigned c = 0; c < 2; c++)
					{
						m_Ch[c].st[s].Coef (kSt[s], m_dR[s], (double) m_fRate);
					}
				}
			}
			m_fSmVol += aVol * (fVolG - m_fSmVol);
			for (unsigned c = 0; c < nCh; c++)
			{
				float *p = c ? pR : pL;
				TChan &C = m_Ch[c];
				float v = p[j] * fToV * fPreG;
				float dry = Clip (v);					// emitter of the preamp's third transistor (dry signal and input of stage 1)
				float s = dry;
				for (unsigned k = 0; k < 4; k++)
				{
					s = C.st[k].Run (Clip (s));
				}
				const float jn = bVib ? fMixV * s : fMixC * (dry + s);
				const float o = jn * m_fSmVol;
				C.hy += aDc * (o - C.hy);					// DC blocker (output coupling)
				p[j] = (o - C.hy) * fToS;
			}
		}
		if (*pMono)
		{
			for (unsigned j = 0; j < n; j++) pR[j] = pL[j];
		}
	}

private:
	struct TChan
	{
		TStage st[4];
		float hx, hy;
	};
	float m_fRate, m_fPh, m_fPow, m_fSmVol;
	float m_fXl[4];
	double m_dR[4];
	unsigned m_nCtl;
	TChan m_Ch[2];
};

}

CFx *NewUniVibe (float fRate)
{
	return new CUniVibe (fRate);
}

// hooks for the tests: one phase stage alone with a fixed LDR, and the current LDR resistances of an instance
void UniVibeStageForTest (unsigned nStage, double dRldr, double dFs, const float *pIn, float *pOut, unsigned n)
{
	TStage st;
	st.Reset ();
	st.Coef (kSt[nStage], dRldr, dFs);
	for (unsigned i = 0; i < n; i++)
	{
		pOut[i] = st.Run (pIn[i]);
	}
}

void UniVibeResForTest (CFx *pFx, double *pR)
{
	static_cast<CUniVibe *> (pFx)->Resistances (pR);
}
