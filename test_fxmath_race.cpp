// test_fxmath_race.cpp - the exp/pow results cache (fxmath.cpp) must never return the value of another argument, even with several threads writing colliding entries.
// It also demonstrates the flaw of the old protocol (key = 0, value, key): with that one the test MUST fail.
// g++ -std=c++17 -O2 -pthread -Ifxshim test_fxmath_race.cpp fxmath.cpp -o test_fxmath_race
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <thread>
#include <atomic>
#include "fxmath.h"

typedef unsigned long long u64;
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)

// ---- the old protocol, copied as it was (only to show the flaw) ----
struct OldEnt { volatile u64 Key; volatile double Val; };
static OldEnt s_Old[256];
static u64 Bits (double x) { u64 u; memcpy (&u, &x, 8); return u; }
static double OldExp (double x, double (*calc) (double))
{
	const u64 k = Bits (x);
	OldEnt &e = s_Old[(unsigned) ((k * 0x9E3779B97F4A7C15ULL) >> 56)];
	if (e.Key == k)
	{
		const double v = e.Val;
		asm volatile ("dmb ish" ::: "memory");
		if (e.Key == k) return v;
	}
	const double r = calc (x);
	e.Key = 0;
	asm volatile ("dmb ish" ::: "memory");
	e.Val = r;
	asm volatile ("dmb ish" ::: "memory");
	e.Key = k;
	return r;
}
static double Ref (double x) { return std::exp (x); }

int main ()
{
	// 1000 different arguments on 256 entries: many collisions
	std::vector<double> args;
	for (int i = 0; i < 1000; i++) args.push_back (-0.0001 * (i + 1) - 0.00003 * (i % 7));
	std::vector<double> expect;
	for (double a : args) expect.push_back (fxm::exp (a));		// first pass on a single thread: correct values
	for (size_t i = 0; i < args.size (); i++)
		if (fabs (expect[i] - Ref (args[i])) > 1e-12 * fabs (expect[i])) { printf ("wrong reference value\n"); return 2; }

	const int nThreads = 4;
	const long nIter = 6000000;
	std::atomic<long> badNew (0), badOld (0);
	auto work = [&] (int id) {
		unsigned s = 12345u + (unsigned) id * 7919u;
		for (long n = 0; n < nIter; n++)
		{
			s = s * 1664525u + 1013904223u;
			const size_t i = (s >> 8) % args.size ();
			if (fxm::exp (args[i]) != expect[i]) badNew++;
			if (OldExp (args[i], Ref) != expect[i]) badOld++;
		}
	};
	std::vector<std::thread> th;
	for (int i = 0; i < nThreads; i++) th.emplace_back (work, i);
	for (auto &t : th) t.join ();
	printf ("wrong results on %ld calls: new protocol %ld, old protocol %ld\n", (long) nThreads * nIter, badNew.load (), badOld.load ());
	CHECK (badNew.load () == 0, "new protocol: no wrong result with 4 colliding threads");
	CHECK (badOld.load () > 0, "old protocol: the flaw shows (the test catches the problem)");

	// pow: same test
	std::vector<double> bx, by, pe;
	for (int i = 0; i < 600; i++) { bx.push_back (1.0 + 0.013 * i); by.push_back (0.5 + 0.007 * (i % 11)); }
	for (size_t i = 0; i < bx.size (); i++) pe.push_back (fxm::pow (bx[i], by[i]));
	std::atomic<long> badPow (0);
	auto workP = [&] (int id) {
		unsigned s = 777u + (unsigned) id * 104729u;
		for (long n = 0; n < nIter; n++)
		{
			s = s * 1664525u + 1013904223u;
			const size_t i = (s >> 8) % bx.size ();
			if (fxm::pow (bx[i], by[i]) != pe[i]) badPow++;
		}
	};
	std::vector<std::thread> th2;
	for (int i = 0; i < nThreads; i++) th2.emplace_back (workP, i);
	for (auto &t : th2) t.join ();
	printf ("pow: wrong results %ld of %ld\n", badPow.load (), (long) nThreads * nIter);
	CHECK (badPow.load () == 0, "pow: no wrong result with 4 colliding threads");

	printf ("RESULT: %s (%d failed)\n", fails == 0 ? "OK" : "FAILED", fails);
	return fails != 0;
}
