//
// fx_sag.cpp - ADELSag: power-supply sag of a guitar amplifier, as an optional block.
//
// What it models. In a class AB amplifier the current drawn from the high-voltage supply grows with the output level. The supply has a source
// resistance (rectifier + transformer winding), so the rail drops under load and recovers when the load goes away. With the rail lower, the amplifier
// can swing less: on loud notes the output is compressed and "blooms" back. The block reproduces that on the SIGNAL OF THE AMPLIFIER (put it right
// after the NAM block): a load detector -> two cascaded supply stages (plate rail, then the slower screen/preamp filter) -> gain.
//
// Sources read on 2026-11-16 (facts used here; nothing else is claimed):
//  - Aiken Amplification, "What is Sag?" (https://www.aikenamps.com/index.php/what-is-sag): sag = drop of the supply voltage under large transient
//    demands; causes are the rectifier (tube drop varies with the current; a solid-state rectifier can emulate it with a series resistance of about
//    100 ohm), the transformer winding (50 - 300 ohm for a typical B+ winding), the filter capacitors; the lower supply lowers the output power in
//    opposition to the larger input signal (acts like a compressor) and reduces the headroom; the screen is a filtered version of the supply, so it sags
//    too; class A amplifiers do not sag (the current at full power equals the current at idle), class AB ones do; full-wave rectification gives a
//    ripple at 120 Hz on 60 Hz mains (100 Hz on 50 Hz), which if not filtered enough modulates the signal ("ghost notes").
//  - ampbooks.com, "Power supply" (https://www.ampbooks.com/mobile/dsp/power-supply/): worked Fender Deluxe 5E3 example with a 5Y3GT: the load goes from
//    84 mA to 138 mA and the plate supply from 360 V to 328 V (effective source resistance about 593 ohm, a 9% drop); under a power-chord load the plate
//    supply falls at about 2.88 V/ms (0.8%/ms) and the screen supply, behind two RC filters (5 kohm in the screen filter), at about 0.5 V/ms.
//
// Supply presets: five real situations, with the parameters derived from the numbers below (Depth = total gain drop D at full power, from the rail drop r
// at full power: D = 1 - (1 - r)^1.5, because g = vA * sqrt(vB) and vB settles to vA; plate time constant = source resistance x reservoir capacitance;
// screen/preamp stage time = the slow dropping resistor x its capacitor):
//  1. Fender 5E3 Deluxe, 5Y3GT rectifier (ampbooks.com, worked example): 360 V -> 328 V for a load of 84 -> 138 mA, source resistance 593 ohm, first
//     capacitor 16 uF: r = 8.9%, D = 13.0%, tau = 593 ohm x 16 uF = 9.5 ms. Screen stage: the 5 kohm filter resistor of that page with a capacitor of 16 uF
//     ASSUMED (the page does not give it): 80 ms.
//  2. The same 5E3 with silicon diodes: DERIVED from the same page's transformer data (Hammond 290BX: 112.5 ohm secondary = 56 ohm per phase, primary
//     3.261 ohm, 351 V per phase from 120 V): source resistance = 56.25 + 3.261 x (351/120)^2 = 84 ohm, open-circuit 410 V (the page's Thevenin value, ASSUMED
//     equal for the diode case); it is a LOWER bound (a capacitor-input rectifier looks like more than the winding resistance): r = 1.1%, D = 1.7%, tau = 1.3 ms.
//  3. The 5E3 with diodes and a 100 ohm series resistor, the usual way to emulate a tube rectifier (Aiken: "typically around 100 ohms"; GroupDIY: 47 - 100 ohm):
//     source resistance 184 ohm: r = 2.5%, D = 3.8%, tau = 2.9 ms. Compared with preset 1 it shows how much the 5Y3 adds beyond 100 ohm.
//  4. Marshall 100 W "Super Lead", versions 6 / 7A (1967-68, silicon bridges and voltage doubler; Rob Jaarsma / roef.folk.ntnu.no): plate voltage 460 V which "drops
//     80-90 V at full distortion": r = 18.5%, D = 26.4%. Mains reservoir 50 uF (two 100 uF in series); the phase-inverter node is fed through 8.2 kohm + 10 kohm with
//     32 uF: 580 ms. The plate time constant (25 ms = about 500 ohm x 50 uF) rests on an ASSUMED source resistance: the page gives no currents.
//  5. Vox AC30 (JMI, GZ34 / 5AR4, B+ about 320 V; two 16 uF or 32 uF smoothing capacitors and a 10 H choke are reported): idle current 47.5 mA per tube; a
//     measured +25% at full drive (Dick Denney, 100 -> 125 mA per pair, as quoted on the forum of the search; Aiken measures a swing 47.5 -> 138 mA peak): the
//     current swing is about 50 mA in total. The source resistance (250 ohm), the capacitor (32 uF) and the preamp-node time (60 ms) are ASSUMED (no source
//     found gives them): r = 3.9%, D = 5.8%, tau = 8 ms.
//
// Values set by design (not taken from a source): the exponent 1/2 of the screen stage in the gain, the load detector
// time (4 ms), the idle share of the ripple (30%), the factor 0.08 for class A, and every value marked ASSUMED above.
//
// Gain law: g = vA * sqrt(vB) where vA is the plate rail (fraction of its idle value) and vB the screen/preamp rail, which follows vA through a slower
// low-pass (a cascaded RC filter). Depth is the total gain drop at full power: vA(steady) = (1 - depth)^(2/3). With a quiet signal the load is at
// idle level, the rails stay at 1 and the block is transparent (bit-exact when Depth = 0 and Ripple = 0).
// Not modelled: the extra distortion of a lower rail (the NAM already contains the clipping of the nominal rail), a different current law of the
// rectifier tube (a linear source resistance is used), the real output-stage current waveform (a rectified average is used).
//
// Load: u = (rectified average of max(|L|, |R|)) / (2/pi * 10^(Level/20)): 1 means the amplifier is at full power (a sine of peak = Level). One supply
// for the two channels (a stereo block shares one rail).
//
#include "fx.h"
#include "dsp_util.h"
#include <circle/util.h>

namespace
{

enum { PChar = 0, PDepth, PAttack, PRecovery, PLevel, PClass, PRipple, PMains, POutRail = 8, POutGr = 9, NParams = 8 };

struct TPreset { float fDepth, fAttack, fRecovery, fBloom; };	// % at full power, ms, ms, ms (screen stage)
const unsigned kNPresets = 5;			// the next choice is Custom
const TPreset kPreset[kNPresets] =
{
	{13.0f, 9.5f, 9.5f, 80.0f},		// Fender 5E3 Deluxe, 5Y3GT rectifier
	{1.7f, 1.3f, 1.3f, 80.0f},		// Fender 5E3, silicon diodes
	{3.8f, 2.9f, 2.9f, 80.0f},		// Fender 5E3, diodes + 100 ohm series
	{26.4f, 25.0f, 25.0f, 580.0f},		// Marshall 100 W Super Lead (1967-68), silicon bridges, 50 uF
	{5.8f, 8.0f, 8.0f, 60.0f}		// Vox AC30, GZ34 / 5AR4 (estimated)
};

const float kDetectorS = 0.004f;		// load detector time
const float kScreenExp = 0.5f;		// the screen/preamp rail counts with this exponent in the gain (sqrt)
const float kRippleIdle = 0.3f;		// share of the ripple that is present at idle
const float kClassA = 0.08f;			// load swing of a class A power stage, relative to class AB

// fast log2 (error < 3e-5) and exp
inline float FastLog2 (float x)
{
	union { float f; u32 u; } v;
	v.f = x;
	const int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
	const float m = v.f - 1.0f;
	return (float) e + m * (1.4418258f + m * (-0.708682101f + m * (0.415421949f + m * (-0.194422672f + m * 0.0458855267f))));
}

inline float Ex (float x)
{
	x = x < -80.0f ? -80.0f : (x > 80.0f ? 80.0f : x);
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

// value between lo and hi, a NaN becomes lo
inline float Clamp (float x, float lo, float hi)
{
	return !(x >= lo) ? lo : (x > hi ? hi : x);
}

class CSag : public CFx
{
public:
	CSag (float fRate) : m_fRate (fRate)
	{
		m_fE = 0.0f;
		m_fVa = m_fVb = 1.0f;
		m_fC = 1.0f;
		m_fS = 0.0f;
		m_nRenorm = 0;
		m_fGain = 1.0f;
		m_bValid = false;
		for (unsigned i = 0; i < NParams; i++)
		{
			m_fP[i] = 0.0f;
		}
		m_fKe = 1.0f - Ex (-1.0f / (fRate * kDetectorS));
		Configure (0);
	}

	unsigned Type (void) const { return FxSag; }

	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP)
	{
		Update (pP);
		const unsigned nCh = *pMono ? 1 : 2;
		for (unsigned j = 0; j < n; j++)
		{
			float a = pL[j] < 0.0f ? -pL[j] : pL[j];
			if (nCh > 1)
			{
				const float b = pR[j] < 0.0f ? -pR[j] : pR[j];
				if (b > a)
				{
					a = b;
				}
			}
			if (!(a < 16.0f))
			{
				a = (a == a) ? 16.0f : 0.0f;		// +-inf or huge -> 16, NaN -> 0 (the state never becomes NaN)
			}
			m_fE += (a - m_fE) * m_fKe;
			if (m_fE < 1e-18f)
			{
				m_fE = 0.0f;
			}
			float u = m_fE * m_fInvRef;
			u = u > 1.5f ? 1.5f : u;
			const float vt = 1.0f - m_fDrop * (u * m_fClassK);		// rail the load pulls towards
			m_fVa += (vt - m_fVa) * (vt < m_fVa ? m_fKAttack : m_fKRecovery);
			m_fVb += (m_fVa - m_fVb) * m_fKBloom;
			float g = m_fVa;
			if (m_fVb != 1.0f)
			{
				g *= kScreenExp == 0.5f ? DspSqrtF (m_fVb) : m_fVb;
			}
			if (m_fRipple > 0.0f)
			{
				// quadrature oscillator at twice the mains frequency
				const float c = m_fC * m_fCosT - m_fS * m_fSinT;
				m_fS = m_fS * m_fCosT + m_fC * m_fSinT;
				m_fC = c;
				if (++m_nRenorm >= 2048)
				{
					m_nRenorm = 0;
					const float m = m_fC * m_fC + m_fS * m_fS;
					const float k = 0.5f * (3.0f - m);
					m_fC *= k;
					m_fS *= k;
				}
				g *= 1.0f + m_fRipple * (kRippleIdle + (1.0f - kRippleIdle) * (u > 1.0f ? 1.0f : u)) * m_fS;
			}
			m_fGain = g;
			pL[j] *= g;
			if (nCh > 1)
			{
				pR[j] *= g;
			}
		}
		if (nCh == 1)
		{
			for (unsigned j = 0; j < n; j++)
			{
				pR[j] = pL[j];
			}
		}
	}

	void GetOutputs (float *pOut) const
	{
		for (unsigned i = 0; i < FX_MAX_P; i++)
		{
			pOut[i] = 0.0f;
		}
		pOut[POutRail] = m_fVa * 100.0f;
		const float g = m_fGain > 1e-6f ? m_fGain : 1e-6f;
		pOut[POutGr] = -6.0205999f * FastLog2 (g);
	}

private:
	// coefficients that depend on the parameters (only when they change)
	void Update (const float *pP)
	{
		bool bSame = m_bValid;
		for (unsigned i = 0; i < NParams && bSame; i++)
		{
			bSame = pP[i] == m_fP[i];
		}
		if (bSame)
		{
			return;
		}
		for (unsigned i = 0; i < NParams; i++)
		{
			m_fP[i] = pP[i];
		}
		m_bValid = true;
		Configure (pP);
	}

	void Configure (const float *pP)
	{
		float p[NParams] = {0.0f, 10.0f, 8.0f, 40.0f, -9.0f, 0.0f, 0.3f, 0.0f};
		if (pP)
		{
			for (unsigned i = 0; i < NParams; i++)
			{
				p[i] = pP[i];
			}
		}
		const int nChar = (int) Clamp (p[PChar], 0.0f, (float) kNPresets + 0.49f);
		float fDepth, fAttack, fRecovery, fBloom;
		if (nChar < (int) kNPresets)
		{
			fDepth = kPreset[nChar].fDepth; fAttack = kPreset[nChar].fAttack; fRecovery = kPreset[nChar].fRecovery; fBloom = kPreset[nChar].fBloom;
		}
		else
		{
			fDepth = Clamp (p[PDepth], 0.0f, 30.0f);
			fAttack = Clamp (p[PAttack], 2.0f, 50.0f);
			fRecovery = Clamp (p[PRecovery], 10.0f, 300.0f);
			fBloom = 3.0f * fRecovery;
		}
		// depth = total gain drop at full power; vA(steady) = (1 - depth)^(2/3)
		const float fKeep = 1.0f - fDepth * 0.01f;
		const float fVFull = fDepth <= 0.0f ? 1.0f : Ex ((2.0f / 3.0f) * 0.69314718f * FastLog2 (fKeep));
		m_fDrop = 1.0f - fVFull;
		m_fKAttack = 1.0f - Ex (-1.0f / (m_fRate * fAttack * 0.001f));
		m_fKRecovery = 1.0f - Ex (-1.0f / (m_fRate * fRecovery * 0.001f));
		m_fKBloom = 1.0f - Ex (-1.0f / (m_fRate * fBloom * 0.001f));
		m_fInvRef = 1.0f / (DbToLin (Clamp (p[PLevel], -30.0f, 0.0f)) * 0.63661977f);
		m_fClassK = p[PClass] > 0.5f ? kClassA : 1.0f;
		m_fRipple = Clamp (p[PRipple], 0.0f, 3.0f) * 0.01f;
		const float fMains = p[PMains] > 0.5f ? 60.0f : 50.0f;
		const float th = 6.2831853f * 2.0f * fMains / m_fRate;		// 0..pi for the sample rates in use
		m_fCosT = DspCos (th);
		m_fSinT = DspSin (th);
	}

	float m_fRate;
	float m_fP[NParams];
	bool m_bValid;
	float m_fKe, m_fInvRef, m_fDrop, m_fClassK, m_fKAttack, m_fKRecovery, m_fKBloom, m_fRipple, m_fCosT, m_fSinT;
	float m_fE, m_fVa, m_fVb, m_fC, m_fS, m_fGain;
	unsigned m_nRenorm;
};

}

CFx *NewSag (float fRate)
{
	return new CSag (fRate);
}
