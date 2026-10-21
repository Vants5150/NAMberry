//
// fxmath.cpp - implementation (argument reduction + series, all in double)
//
#include "fxmath.h"

typedef unsigned long long u64_t;

namespace fxm {

static inline u64_t Bits (double x)
{
	union { double d; u64_t u; } v;
	v.d = x;
	return v.u;
}

static inline double FromBits (u64_t u)
{
	union { double d; u64_t u; } v;
	v.u = u;
	return v.d;
}

static const double kLn2 = 0.69314718055994530942;
static const double kInf = 1.0e308 * 10.0;

// memory of the exp and pow results: the engines recompute the same coefficients with the same arguments at every block
// (the reverb calls pow about 50 times per block). Same result bit for bit, but only the first time costs.
// The memory is used by the audio callback (IRQ, which interrupts the main loop on the same core), by the worker cores and by the main loop.
// FIXED on 2026-10-18: the previous version wrote (key = 0, value, key) without protection: if between the writing of the value and that of the key
// the callback or another core wrote the same entry, the key of one with the value of the other was left, and that wrong result was returned
// until the entry was overwritten (in the spring: a sample of -35 in the input saturator and a burst like a "laser gun").
// Now every entry has a sequence number: whoever writes must first reserve it (from even to odd, with an atomic operation) and at the end of the write makes it even again;
// whoever reads accepts the value only if the number is even and has not changed. If the entry is busy it recomputes without storing.
struct TExpEnt
{
	volatile unsigned Seq;
	volatile u64_t Key;
	volatile double Val;
};

struct TPowEnt
{
	volatile unsigned Seq;
	volatile u64_t KeyX, KeyY;
	volatile double Val;
};

static TExpEnt s_Exp[256];
static TPowEnt s_Pow[256];

static inline bool Claim (volatile unsigned *pSeq, unsigned *pOld)
{
	unsigned s = __atomic_load_n (pSeq, __ATOMIC_RELAXED);
	if (s & 1u)
	{
		return false;
	}
	if (!__atomic_compare_exchange_n (pSeq, &s, s + 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
	{
		return false;
	}
	*pOld = s;
	return true;
}

static double ExpCalc (double x);

double exp (double x)
{
	if (x == 0.0)
	{
		return 1.0;
	}
	const u64_t k = Bits (x);
	TExpEnt &e = s_Exp[(unsigned) ((k * 0x9E3779B97F4A7C15ULL) >> 56)];
	const unsigned s1 = __atomic_load_n (&e.Seq, __ATOMIC_ACQUIRE);
	if (!(s1 & 1u))
	{
		const u64_t kk = e.Key;
		const double v = e.Val;
		__atomic_thread_fence (__ATOMIC_ACQUIRE);
		if (kk == k && __atomic_load_n (&e.Seq, __ATOMIC_RELAXED) == s1)
		{
			return v;
		}
	}
	const double r = ExpCalc (x);
	unsigned nOld;
	if (Claim (&e.Seq, &nOld))
	{
		e.Key = k;
		e.Val = r;
		__atomic_store_n (&e.Seq, nOld + 2u, __ATOMIC_RELEASE);
	}
	return r;
}
static double ExpCalc (double x)
{
	if (x != x)
	{
		return x;
	}
	if (x > 709.7)
	{
		return kInf;
	}
	if (x < -745.0)
	{
		return 0.0;
	}
	const double kd = x * 1.4426950408889634074;
	const long k = (long) (kd + (kd >= 0.0 ? 0.5 : -0.5));
	const double dk = (double) k;
	const double r = (x - dk * 6.93147180369123816490e-01) - dk * 1.90821492927058770002e-10;
	double p = 1.0 / 6227020800.0;
	p = p * r + 1.0 / 479001600.0;
	p = p * r + 1.0 / 39916800.0;
	p = p * r + 1.0 / 3628800.0;
	p = p * r + 1.0 / 362880.0;
	p = p * r + 1.0 / 40320.0;
	p = p * r + 1.0 / 5040.0;
	p = p * r + 1.0 / 720.0;
	p = p * r + 1.0 / 120.0;
	p = p * r + 1.0 / 24.0;
	p = p * r + 1.0 / 6.0;
	p = p * r + 0.5;
	p = p * r + 1.0;
	p = p * r + 1.0;
	if (k > -1000 && k < 1000)
	{
		return p * FromBits ((u64_t) (k + 1023) << 52);
	}
	// near the limits: two steps
	const long k1 = k / 2, k2 = k - k1;
	return p * FromBits ((u64_t) (k1 + 1023) << 52) * FromBits ((u64_t) (k2 + 1023) << 52);
}

double expm1 (double x)
{
	if (x != x)
	{
		return x;
	}
	if (x > 0.35 || x < -0.35)
	{
		return exp (x) - 1.0;
	}
	// Taylor series: x + x^2/2 + ... up to x^14/14!
	double p = 1.0 / 87178291200.0;
	p = p * x + 1.0 / 6227020800.0;
	p = p * x + 1.0 / 479001600.0;
	p = p * x + 1.0 / 39916800.0;
	p = p * x + 1.0 / 3628800.0;
	p = p * x + 1.0 / 362880.0;
	p = p * x + 1.0 / 40320.0;
	p = p * x + 1.0 / 5040.0;
	p = p * x + 1.0 / 720.0;
	p = p * x + 1.0 / 120.0;
	p = p * x + 1.0 / 24.0;
	p = p * x + 1.0 / 6.0;
	p = p * x + 0.5;
	p = p * x + 1.0;
	return x * p;
}

double exp2 (double x)
{
	return exp (x * kLn2);
}

double log (double x)
{
	if (x != x)
	{
		return x;
	}
	if (x <= 0.0)
	{
		return x == 0.0 ? -kInf : (kInf - kInf);
	}
	if (x > 1.7e308)
	{
		return 709.78;
	}
	u64_t u = Bits (x);
	long e = (long) ((u >> 52) & 0x7FF);
	if (e == 0)
	{
		x *= 18014398509481984.0;		// 2^54 (subnormals)
		u = Bits (x);
		e = (long) ((u >> 52) & 0x7FF) - 54;
	}
	e -= 1023;
	u = (u & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;
	double m = FromBits (u);
	if (m > 1.4142135623730951)
	{
		m *= 0.5;
		e++;
	}
	const double s = (m - 1.0) / (m + 1.0), s2 = s * s;
	double p = 1.0 / 23.0;
	p = p * s2 + 1.0 / 21.0;
	p = p * s2 + 1.0 / 19.0;
	p = p * s2 + 1.0 / 17.0;
	p = p * s2 + 1.0 / 15.0;
	p = p * s2 + 1.0 / 13.0;
	p = p * s2 + 1.0 / 11.0;
	p = p * s2 + 1.0 / 9.0;
	p = p * s2 + 1.0 / 7.0;
	p = p * s2 + 1.0 / 5.0;
	p = p * s2 + 1.0 / 3.0;
	p = p * s2 + 1.0;
	const double de = (double) e;
	return (de * 6.93147180369123816490e-01) + (2.0 * s * p + de * 1.90821492927058770002e-10);
}

double log2 (double x)
{
	return log (x) * 1.4426950408889634074;
}

double log10 (double x)
{
	return log (x) * 0.43429448190325182765;
}

double log1p (double x)
{
	const double u = 1.0 + x;
	if (u == 1.0)
	{
		return x;
	}
	if (u <= 0.0)
	{
		return log (u);
	}
	return log (u) * x / (u - 1.0);
}

double pow (double x, double y)
{
	if (y == 0.0)
	{
		return 1.0;
	}
	if (x == 0.0)
	{
		return y > 0.0 ? 0.0 : kInf;
	}
	if (x > 0.0)
	{
		const u64_t kx = Bits (x), ky = Bits (y);
		TPowEnt &e = s_Pow[(unsigned) (((kx * 0x9E3779B97F4A7C15ULL) ^ (ky * 0xC2B2AE3D27D4EB4FULL)) >> 56)];
		const unsigned s1 = __atomic_load_n (&e.Seq, __ATOMIC_ACQUIRE);
		if (!(s1 & 1u))
		{
			const u64_t kax = e.KeyX, kay = e.KeyY;
			const double v = e.Val;
			__atomic_thread_fence (__ATOMIC_ACQUIRE);
			if (kax == kx && kay == ky && __atomic_load_n (&e.Seq, __ATOMIC_RELAXED) == s1)
			{
				return v;
			}
		}
		const double r = ExpCalc (y * log (x));
		unsigned nOld;
		if (Claim (&e.Seq, &nOld))
		{
			e.KeyX = kx;
			e.KeyY = ky;
			e.Val = r;
			__atomic_store_n (&e.Seq, nOld + 2u, __ATOMIC_RELEASE);
		}
		return r;
	}
	// negative base: integer exponent only
	const long n = (long) y;
	if ((double) n != y)
	{
		return kInf - kInf;
	}
	const double v = exp (y * log (-x));
	return (n & 1) ? -v : v;
}

static const double kPio2_1 = 1.57079632673412561417e+00;
static const double kPio2_1t = 6.07710050650619224932e-11;

// reduces x to r in [-pi/4, pi/4] and quadrant q (mod 4)
static inline double Reduce (double x, int *pQ)
{
	const double t = x * 0.63661977236758134308;
	const double qd = (double) (long) (t + (t >= 0.0 ? 0.5 : -0.5));
	*pQ = (int) ((long) qd & 3);
	return (x - qd * kPio2_1) - qd * kPio2_1t;
}

static inline double SinPoly (double r)
{
	const double r2 = r * r;
	double p = -1.0 / 355687428096000.0;		// 1/17!
	p = p * r2 + 1.0 / 1307674368000.0;		// 15!
	p = p * r2 - 1.0 / 6227020800.0;		// 13!
	p = p * r2 + 1.0 / 39916800.0;			// 11!
	p = p * r2 - 1.0 / 362880.0;			// 9!
	p = p * r2 + 1.0 / 5040.0;
	p = p * r2 - 1.0 / 120.0;
	p = p * r2 + 1.0 / 6.0;
	return r - r * r2 * p;
}

static inline double CosPoly (double r)
{
	const double r2 = r * r;
	double p = 1.0 / 20922789888000.0;		// 1/16!
	p = p * r2 - 1.0 / 87178291200.0;		// 14!
	p = p * r2 + 1.0 / 479001600.0;			// 12!
	p = p * r2 - 1.0 / 3628800.0;			// 10!
	p = p * r2 + 1.0 / 40320.0;			// 8!
	p = p * r2 - 1.0 / 720.0;
	p = p * r2 + 1.0 / 24.0;
	p = p * r2 - 0.5;
	return 1.0 + r2 * p;
}

double sin (double x)
{
	if (x != x)
	{
		return x;
	}
	int q;
	const double r = Reduce (x, &q);
	switch (q)
	{
	case 0:	return SinPoly (r);
	case 1:	return CosPoly (r);
	case 2:	return -SinPoly (r);
	default:	return -CosPoly (r);
	}
}

double cos (double x)
{
	if (x != x)
	{
		return x;
	}
	int q;
	const double r = Reduce (x, &q);
	switch (q)
	{
	case 0:	return CosPoly (r);
	case 1:	return -SinPoly (r);
	case 2:	return -CosPoly (r);
	default:	return SinPoly (r);
	}
}

double tan (double x)
{
	if (x != x)
	{
		return x;
	}
	int q;
	const double r = Reduce (x, &q);
	const double s = SinPoly (r), c = CosPoly (r);
	return (q & 1) ? -c / s : s / c;
}

double atan (double x)
{
	if (x != x)
	{
		return x;
	}
	const double kPi2 = 1.57079632679489661923;
	double a = x < 0.0 ? -x : x;
	double res;
	if (a > 1.0e18)
	{
		res = kPi2;
	}
	else
	{
		const bool bBig = a > 1.0;
		if (bBig)
		{
			a = 1.0 / a;
		}
		// two halvings of the angle: a -> a / (1 + sqrt(1 + a^2))
		for (int i = 0; i < 2; i++)
		{
			a = a / (1.0 + __builtin_sqrt (1.0 + a * a));
		}
		const double a2 = a * a;
		double p = -1.0 / 23.0;
		p = p * a2 + 1.0 / 21.0;
		p = p * a2 - 1.0 / 19.0;
		p = p * a2 + 1.0 / 17.0;
		p = p * a2 - 1.0 / 15.0;
		p = p * a2 + 1.0 / 13.0;
		p = p * a2 - 1.0 / 11.0;
		p = p * a2 + 1.0 / 9.0;
		p = p * a2 - 1.0 / 7.0;
		p = p * a2 + 1.0 / 5.0;
		p = p * a2 - 1.0 / 3.0;
		p = p * a2 + 1.0;
		res = 4.0 * a * p;
		if (bBig)
		{
			res = kPi2 - res;
		}
	}
	return x < 0.0 ? -res : res;
}
double atan2 (double y, double x)
{
	const double kPi = 3.14159265358979323846;
	if (x != x || y != y)
	{
		return x + y;
	}
	if (x == 0.0)
	{
		return y > 0.0 ? kPi / 2 : (y < 0.0 ? -kPi / 2 : 0.0);
	}
	const double a = atan (y / x);
	if (x > 0.0)
	{
		return a;
	}
	return y >= 0.0 ? a + kPi : a - kPi;
}

double fmod (double x, double y)
{
	if (y == 0.0 || x != x || y != y)
	{
		return kInf - kInf;
	}
	const double q = x / y;
	const double t = (double) (long long) q;		// truncation towards zero
	return x - t * y;
}

double tanh (double x)
{
	if (x != x)
	{
		return x;
	}
	const double a = x < 0.0 ? -x : x;
	if (a > 22.0)
	{
		return x < 0.0 ? -1.0 : 1.0;
	}
	const double em = expm1 (2.0 * x);
	return em / (em + 2.0);
}

}

// square root: the compiler sometimes calls it as a library function (non-finite cases, etc.); Circle has no libm
extern "C" double sqrt (double x)
{
	double r;
	asm ("fsqrt %d0, %d1" : "=w" (r) : "w" (x));
	return r;
}

extern "C" float sqrtf (float x)
{
	float r;
	asm ("fsqrt %s0, %s1" : "=w" (r) : "w" (x));
	return r;
}
