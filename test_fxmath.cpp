// test_fxmath.cpp - compares fxmath with Linux's libm: maximum relative error per function
// g++ -std=c++17 -O2 -I. -Ifxshim test_fxmath.cpp fxshim/fxmath.cpp -o test_fxmath
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "fxmath.h"

static double rnd (double lo, double hi)
{
	return lo + (hi - lo) * ((double) rand () / (double) RAND_MAX);
}

static double rel (double got, double ref)
{
	if (ref == 0.0)
	{
		return fabs (got);
	}
	return fabs (got - ref) / fabs (ref);
}

int main ()
{
	int fails = 0;
	struct { const char *n; double (*f)(double); double (*r)(double); double lo, hi; bool bAbs; } T[] =
	{
		{"exp", fxm::exp, ::exp, -60, 60, false},
		{"expm1", fxm::expm1, ::expm1, -2, 2, false},
		{"exp2", fxm::exp2, ::exp2, -40, 40, false},
		{"log", fxm::log, ::log, 1e-9, 1e9, false},
		{"log2", fxm::log2, ::log2, 1e-9, 1e9, false},
		{"log10", fxm::log10, ::log10, 1e-9, 1e9, false},
		{"log1p", fxm::log1p, ::log1p, -0.9, 5, false},
		{"sin", fxm::sin, ::sin, -1000, 1000, true},
		{"cos", fxm::cos, ::cos, -1000, 1000, true},
		{"tan", fxm::tan, ::tan, -1.5, 1.5, false},
		{"atan", fxm::atan, ::atan, -50, 50, false},
		{"tanh", fxm::tanh, ::tanh, -10, 10, false},
	};
	for (auto &t : T)
	{
		double worst = 0;
		for (int i = 0; i < 200000; i++)
		{
			const double x = rnd (t.lo, t.hi);
			const double e = t.bAbs ? fabs (t.f (x) - t.r (x)) : rel (t.f (x), t.r (x));
			if (e > worst)
			{
				worst = e;
			}
		}
		printf ("%-6s maximum error %.2e\n", t.n, worst);
		if (worst > 5e-14)
		{
			printf ("  FAILED\n");
			fails++;
		}
	}
	double w1 = 0, w2 = 0;
	for (int i = 0; i < 200000; i++)
	{
		const double x = rnd (0.001, 1000), y = rnd (-5, 5);
		w1 = fmax (w1, rel (fxm::pow (x, y), ::pow (x, y)));
		const double a = rnd (-10, 10), b = rnd (-10, 10);
		w2 = fmax (w2, fabs (fxm::atan2 (a, b) - ::atan2 (a, b)));
	}
	printf ("pow    maximum error %.2e\natan2  maximum absolute error %.2e\n", w1, w2);
	if (w1 > 5e-13 || w2 > 5e-14)
	{
		printf ("  FAILED\n");
		fails++;
	}
	printf (fails ? "RISULTATO: %d FALLITE\n" : "RESULT: all tests passed\n", fails);
	return fails;
}
