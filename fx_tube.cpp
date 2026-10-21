//
// fx_tube.cpp - Tube Screamer: model of the CIRCUIT (not an invented curve), with the physical pedal's controls (Drive, Tone, Level and, for the TS9DX, Mode).
//
// Sources (2026-10-18; the component values are those reported by the sources, where the sources disagree it is written):
//  - ElectroSmash, TS808 analysis (https://electrosmash.mas-effects.com/tube-screamer-analysis): input buffer as an emitter follower (C1 0.02 uF, R2 510k, Zin 446k),
//    non-inverting clipping stage (R5 10k towards the bias, C2 1 uF, R4 4k7 + C3 0.047 uF to ground => high-pass at 721 Hz, feedback 51k + Drive 500k logarithmic with C4 51 pF in
//    parallel, two antiparallel MA150/1N4148/1N914 diodes in the feedback: gain from about 12 to 118), tone (R 1k + C 0.22 uF = low-pass at 723 Hz, network R8 220 ohm + C6 0.22 uF
//    from the wiper of a 20k potentiometer connected between the + and - inputs of the second op-amp), Level 100k, output buffer (RB 100 ohm, C9 10 uF, RC 10k), 9 V supply
//    con bias a 4,5 V.
//  - PedalPCB, "Tone Stacks Part 3" (https://forum.pedalpcb.com/threads/tone-stacks-part-3-tube-screamer-hm-2-gyrators.8557/): tone topology (with Tone at minimum the R7+C6 network is in parallel
//    with C5, op-amp at gain 1, low-pass at about 400 Hz; at maximum the network is in the feedback with R10: gain 1 + R10/R7 = 5.55, that is +14.9 dB from 725 Hz to 4 kHz).
//  - m0xpd (https://m0xpd.blogspot.com/2012/11/analysis-of-tube-screamer.html): 220 nF in parallel with 10k on the second op-amp's + input; 220 nF + 220 ohm network between the wiper and ground.
//  - Differences among the models: Wikipedia (https://en.wikipedia.org/wiki/Ibanez_Tube_Screamer), Spicy Pedals (https://spicypedals.com/the-ultimate-tube-screamer-showdown-ts808-vs-ts9-vs-ts10/),
//    Mad Bean (https://www.madbeanpedals.com/forum/index.php?topic=32695.0), Aion FX (https://aionfx.com/project/stratus-classic-overdrive/): TS808 -> TS9: in the output stage 100 ohm and 10k
//    become 470 ohm and 100k (and the op-amp; "no real effect on the sound", the output impedance changes); TS10: 220 ohm in series with the clipping op-amp's input (no tonal
//    effect). TS9DX (Analog Man, https://analogman.com/ts9dx.htm): same circuit as the TS9; the Mode knob changes diodes and capacitor: "+" = two diodes in series for each direction and
//    capacitor about 5 times; "Hot" = red LEDs and about 20 times; "Turbo" = diodes practically removed and about 40 times (electrolytic). Here the capacitor is C3 (the one that sets the 720 Hz): 0.047 uF x5 = 0.22 uF,
//    x20 = 1 uF, x40 = 2.2 uF (standard values: it is my reading, see "Uncertain").
//
// Uncertain / approximated (not found in the sources): diode parameters (1N914: Is 2.52 nA, n 1.752 from standard SPICE; red LED: Is 1e-18, n 1.9), maximum swing of the op-amp (3.5 V peak),
// Drive curve (logarithmic with 15% at half travel), additional resistance of the LEDs in Hot mode (ignored), amplifier load (1 Mohm),
// level: 0 dBFS = 1.305 V peak (0 dBFS = +1.5 dBu, Pisound calibration at Gain 0).
//
// Comparison with real captures (2026-10-18, TONE3000 "Ibanez TS9 Tube Screamer" by studioampcaptures, 9 models with declared Drive/Tone/Level; the knob labels are the author's and cannot
// be verified): the small-signal response of the 3 captures at Drive 0 follows this circuit within 0.42 dB rms (40 Hz - 10 kHz) with only 4 free parameters; the position of the Tone wiper
// derived from the captures (Tone 6, 7, 9 -> 0.88, 0.92, 0.998 towards the + input) corresponds to an inverse logarithmic curve e^(3 x): it is the curve used here (the Drive: 2 -> 2.6%, 7 -> 30% of the 500k, consistent
// with the 3.47 curve). The capture has a constant gain of about +4.5 dB above this circuit: a passive+op-amp circuit cannot give more than the computed gain, so I attribute it to the
// capture's level calibration (input_level_dbu 16, output_level_dbu 18 of the metadata) and do NOT copy it.
//
// Calculation: every capacitor is integrated with the trapezoidal rule at 48 kHz; the clipping feedback (resistance + C4 + diodes) is solved sample by sample with Newton's method;
// the tone is a second-order filter derived from the circuit's exact transfer function for the wiper position (bilinear transform).
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

inline float Clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Absf (float v) { return v < 0.0f ? -v : v; }

// fast e^x (relative error about 1e-6): 2^(x log2 e) with a series for the fractional part
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

inline float Tanhf (float x)
{
	if (x > 3.0f) return 1.0f;
	if (x < -3.0f) return -1.0f;
	const float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ---- components (TS808, ElectroSmash) ----
const float kOutTrim = 0.40f;			// fixed -8 dB at the output (NOT part of the circuit): the NAM in the board comes out normalized at about -19/-23 dBFS RMS, the real circuit (medium Level) comes out 6-11 dB louder; measured 2026-10-18 with loud_ts9.py, at Drive 7 Tone 7 Level 7 the two levels coincide
const float kVfs = 1.305f;			// peak volts per digital full scale
const float kC1 = 0.02e-6f, kRin = 446.0e3f;	// input buffer: high-pass at 17.8 Hz
const float kC2 = 1.0e-6f, kR5 = 10.0e3f;	// clipping stage coupling: 15.9 Hz
const float kR4 = 4.7e3f;			// clipping branch to ground (with C3)
const float kRf = 51.0e3f, kC4 = 51.0e-12f;	// feedback (with the Drive in series with Rf) and capacitor in parallel
const float kPotDrive = 500.0e3f;
const float kR9 = 1.0e3f, kC5 = 0.22e-6f, kRb = 10.0e3f;	// tone input: R 1k, C 0.22 uF, bias 10k
const float kR10 = 1.0e3f, kR7 = 220.0f, kC6 = 0.22e-6f;	// tone feedback and wiper network
const float kPotTone = 20.0e3f;
const float kC9 = 10.0e-6f, kRload = 1.0e6f;	// output
const float kVsat = 3.5f;			// op-amp swing (approximate)
const float kVt = 0.02585f;

struct TModel
{
	float C3;		// capacitor to ground of the clipping
	float Is, nVt;		// diode (Is = 0: no diode); nVt = m * n * Vt (m diodes in series per direction)
	float RB, RC;		// output stage
};

const TModel kModels[7] =
{
	{0.047e-6f, 2.52e-9f, 1.752f * kVt, 100.0f, 10.0e3f},			// TS808
	{0.047e-6f, 2.52e-9f, 1.752f * kVt, 470.0f, 100.0e3f},			// TS9
	{0.047e-6f, 2.52e-9f, 1.752f * kVt, 470.0f, 100.0e3f},			// TS10 (same sound: 220 ohm in series with the input, no effect)
	{0.047e-6f, 2.52e-9f, 1.752f * kVt, 470.0f, 100.0e3f},			// TS9DX, Mode TS9
	{0.22e-6f, 2.52e-9f, 2.0f * 1.752f * kVt, 470.0f, 100.0e3f},		// TS9DX, Mode +
	{1.0e-6f, 1.0e-18f, 1.9f * kVt, 470.0f, 100.0e3f},			// TS9DX, Hot mode (red LEDs)
	{2.2e-6f, 0.0f, 1.0f, 470.0f, 100.0e3f}					// TS9DX, Turbo mode (no diodes)
};

// potentiometer curve: R(x)/Rmax = (e^(a x) - 1) / (e^a - 1)
inline float Taper (float x, float a)
{
	x = Clampf (x, 0.0f, 1.0f);
	if (a < 0.001f) return x;
	return (Ex (a * x) - 1.0f) / (Ex (a) - 1.0f);
}

// Tone wiper position (fraction of 20k towards the + input): inverse logarithmic "G" curve; beyond 0.8 of travel the remaining part falls linearly to zero (fitted to the real captures:
// at Tone 9 the capture is brighter than the exponential alone gives)
inline float TonePos (float x)
{
	x = Clampf (x, 0.0f, 1.0f);
	float q = Taper (1.0f - x, 3.0f);
	if (x > 0.8f)
	{
		q *= (1.0f - x) * 5.0f;
	}
	return 1.0f - q;
}

struct TChan
{
	float hp1x, hp1y, hp2x, hp2y;		// input high-pass
	float vC3, igPrev;			// capacitor to ground of the clipping
	float vfPrev, iCPrev;			// voltage across the feedback and current in its capacitor
	float z1, z2;				// tone filter
	float ox, oy;				// output high-pass
};

class CTubeScreamer : public CFx
{
public:
	CTubeScreamer (float fRate) : m_fRate (fRate), m_fT (1.0f / fRate), m_nModel (-1), m_bInit (false)
	{
		for (unsigned c = 0; c < 2; c++)
		{
			ClearChan (m_Ch[c]);
		}
		m_fDrive = m_fTone = m_fLevel = 0.5f;
	}

	unsigned Type (void) const { return FxTubeScreamer; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		int nModel = (int) (pP[0] + 0.5f);
		nModel = nModel < 0 ? 0 : (nModel > 6 ? 6 : nModel);
		const float fDriveT = Clampf (pP[1], 0.0f, 10.0f) * 0.1f;
		const float fToneT = Clampf (pP[2], 0.0f, 10.0f) * 0.1f;
		const float fLevelT = Clampf (pP[3], 0.0f, 10.0f) * 0.1f;
		if (!m_bInit)
		{
			m_fDrive = fDriveT; m_fTone = fToneT; m_fLevel = fLevelT;
			m_bInit = true;
		}
		// the knobs follow with a time constant of about 10 ms (no jumps when they are moved)
		const float a = 1.0f - Ex (-(float) n * m_fT / 0.010f);
		m_fDrive += a * (fDriveT - m_fDrive);
		m_fTone += a * (fToneT - m_fTone);
		m_fLevel += a * (fLevelT - m_fLevel);
		Setup (nModel);

		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned c = 0; c < nCh; c++)
		{
			float *p = c ? pR : pL;
			TChan &S = m_Ch[c];
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
	static void ClearChan (TChan &S)
	{
		S.hp1x = S.hp1y = S.hp2x = S.hp2y = 0.0f;
		S.vC3 = S.igPrev = S.vfPrev = S.iCPrev = 0.0f;
		S.z1 = S.z2 = 0.0f;
		S.ox = S.oy = 0.0f;
	}

	// coefficients that depend on model and knobs (per block)
	void Setup (int nModel)
	{
		const TModel &M = kModels[nModel];
		const float T = m_fT;
		if (nModel != m_nModel)
		{
			m_nModel = nModel;
			m_fToneLast = -1.0f;
			const float kg = T / (2.0f * M.C3 * kR4);
			m_fKg = kg;
			m_fKc = T / (2.0f * M.C3);
			m_fIs = M.Is;
			m_fInvNVt = M.Is > 0.0f ? 1.0f / M.nVt : 0.0f;
			const float RshL = M.RC * kRload / (M.RC + kRload);
			m_fGout = RshL / (M.RB + RshL);
			m_fAoHp = 1.0f / (1.0f + 6.2831853f * (1.0f / (6.2831853f * RshL * kC9)) * T);
			m_fA1 = 1.0f / (1.0f + 6.2831853f * (1.0f / (6.2831853f * kRin * kC1)) * T);
			m_fA2 = 1.0f / (1.0f + 6.2831853f * (1.0f / (6.2831853f * kR5 * kC2)) * T);
		}
		// Drive: 500k logarithmic potentiometer in series with 51k, with C4 in parallel
		const float Rtot = kRf + kPotDrive * Taper (m_fDrive, 3.47f);
		m_fG0 = 1.0f / Rtot + 2.0f * kC4 / T;
		m_fC4k = 2.0f * kC4 / T;
		// Tone: second-order filter from the exact transfer function for the wiper position p
		if (Absf (m_fTone - m_fToneLast) > 0.0005f)
		{
			m_fToneLast = m_fTone;
			ToneCoef (TonePos (m_fTone));
		}
		m_fLvl = Clampf (m_fLevel, 0.0f, 1.0f);		// Level: 100k linear potentiometer
	}

	// H(s) = (g + s C6 (1 + g R7 + R10/Rwy)) / ((1 + R9/Rb + s R9 C5)(g + s C6 (1 + g R7)) + s R9 C6 / Rxw), with g = 1/Rxw + 1/Rwy; bilinear transform
	void ToneCoef (float p)
	{
		const double Rxw = p * (double) kPotTone < 1.0 ? 1.0 : p * (double) kPotTone;
		const double Rwy = (1.0 - p) * (double) kPotTone < 1.0 ? 1.0 : (1.0 - p) * (double) kPotTone;
		const double g = 1.0 / Rxw + 1.0 / Rwy;
		const double a0 = 1.0 + (double) kR9 / kRb;
		const double d1 = (double) kC6 * (1.0 + g * kR7);
		const double n0 = g, n1 = (double) kC6 * (1.0 + g * kR7 + (double) kR10 / Rwy);
		const double e0 = a0 * g;
		const double e1 = a0 * d1 + (double) kR9 * kC5 * g + (double) kR9 * kC6 / Rxw;
		const double e2 = (double) kR9 * kC5 * d1;
		const double K = 2.0 / (double) m_fT;
		const double b0 = n0 + n1 * K, b1 = 2.0 * n0, b2 = n0 - n1 * K;
		const double q0 = e0 + e1 * K + e2 * K * K, q1 = 2.0 * e0 - 2.0 * e2 * K * K, q2 = e0 - e1 * K + e2 * K * K;
		m_fB0 = (float) (b0 / q0); m_fB1 = (float) (b1 / q0); m_fB2 = (float) (b2 / q0);
		m_fQ1 = (float) (q1 / q0); m_fQ2 = (float) (q2 / q0);
	}

	// solves the clipping feedback: F(v) = v G0 - Ih + I_diodes(v) - ig = 0 (Newton's method, starting from the previous solution)
	float SolveFeedback (TChan &S, float ig)
	{
		const float Ih = m_fC4k * S.vfPrev + S.iCPrev;
		float v = S.vfPrev;
		if (m_fIs <= 0.0f)
		{
			return (ig + Ih) / m_fG0;
		}
		const float k2is = 2.0f * m_fIs;
		for (int it = 0; it < 8; it++)
		{
			const float e = Ex (v * m_fInvNVt), ei = 1.0f / e;
			const float F = v * m_fG0 - Ih + k2is * 0.5f * (e - ei) - ig;
			const float dF = m_fG0 + k2is * m_fInvNVt * 0.5f * (e + ei);
			float dv = F / dF;
			dv = Clampf (dv, -0.5f, 0.5f);
			v -= dv;
			if (Absf (dv) < 1.0e-6f)
			{
				break;
			}
		}
		return v;
	}

	float Sample (TChan &S, float x)
	{
		// input: volts, high-pass of the buffer (17.8 Hz) and of the coupling (15.9 Hz)
		const float xv = x * kVfs;
		S.hp1y = m_fA1 * (S.hp1y + xv - S.hp1x);
		S.hp1x = xv;
		S.hp2y = m_fA2 * (S.hp2y + S.hp1y - S.hp2x);
		S.hp2x = S.hp1y;
		const float u = S.hp2y;

		// branch to ground (R4 + C3): current ig
		const float vC3 = (S.vC3 + m_fKg * u * 1.0f + m_fKc * S.igPrev) / (1.0f + m_fKg);
		const float ig = (u - vC3) / kR4;
		S.vC3 = vC3;
		S.igPrev = ig;

		// feedback: series resistor + C4 + diodes
		const float vf = SolveFeedback (S, ig);
		S.iCPrev = m_fC4k * (vf - S.vfPrev) - S.iCPrev;
		S.vfPrev = vf;
		const float v1 = u + vf;
		const float vo = kVsat * Tanhf (v1 / kVsat);		// op-amp swing

		// tone (second order, transposed direct form II)
		const float yt = m_fB0 * vo + S.z1;
		S.z1 = m_fB1 * vo - m_fQ1 * yt + S.z2;
		S.z2 = m_fB2 * vo - m_fQ2 * yt;

		// Level, output buffer (divider with the load and high-pass of C9)
		const float vl = yt * m_fLvl;
		S.oy = m_fAoHp * (S.oy + vl - S.ox);
		S.ox = vl;
		return S.oy * m_fGout * kOutTrim / kVfs;
	}

	float m_fRate, m_fT;
	int m_nModel;
	bool m_bInit;
	float m_fDrive, m_fTone, m_fLevel, m_fToneLast, m_fLvl;
	float m_fKg, m_fKc, m_fIs, m_fInvNVt, m_fGout, m_fAoHp, m_fA1, m_fA2, m_fG0, m_fC4k;
	float m_fB0, m_fB1, m_fB2, m_fQ1, m_fQ2;
	TChan m_Ch[2];
};

}

CFx *NewTubeScreamer (float fRate)
{
	return new CTubeScreamer (fRate);
}
