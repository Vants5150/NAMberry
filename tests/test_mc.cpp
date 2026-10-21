// test_mc.cpp - multi-core tests of the graph engine on Linux, with threads in place of cores: the result with several cores must be bit-for-bit IDENTICAL to the single-core one,
// for random networks (volume, pan, routing) assigned at random to 2-4 cores; and the NAM scheduling (independent branches are split, serial chains stay on core 0).
// g++ -std=c++17 -O1 -pthread -Ihost_shim -I<circle>/include -I. test_mc.cpp board.cpp graph.cpp fx*.cpp midi.cpp ... -o test_mc
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <thread>
#include "board.h"
#include "graph.h"
#include "fx.h"
#include "fx_defs.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define OK(e) do { const char *r = (e); if (r) { printf("unexpected error line %d: %s\n", __LINE__, r); fails++; } } while (0)

static unsigned long long s_Rng = 88172645463325252ULL;
static unsigned Rnd (unsigned n)
{
	s_Rng ^= s_Rng << 13;
	s_Rng ^= s_Rng >> 7;
	s_Rng ^= s_Rng << 17;
	return (unsigned) (s_Rng % n);
}

static TPlan P1, P2;
static adelnam::Instance *pI[BD_MAX_NAM] = {0};

int main ()
{
	// ---- NAM scheduling
	CBoard::SetCores (2);
	{
		// 4 parallel branches IN -> NAM -> OUT: they are split among the cores
		static CBoard B;
		B.SetMode (2);
		B.Reset ();
		int n[4];
		for (int i = 0; i < 4; i++)
		{
			OK (B.NetAdd ("nam", i, 3, "x.adn", &n[i]));
			OK (B.NetLink (BD_NET_IN, n[i], 1, 0, 0.0f));
			OK (B.NetLink (n[i], BD_NET_OUT, 0, 1, 0.0f));
		}
		B.Compile (&P1, pI, 1.0f, FALSE);
		unsigned nOn1 = 0;
		for (int i = 0; i < 4; i++)
		{
			nOn1 += P1.Core[n[i]] == 1;
		}
		printf ("4 parallel NAMs on 2 cores: %u on core 1, NCores %u, estimated load %u.%u %%\n", nOn1, P1.NCores, B.EstimateLoad10 () / 10, B.EstimateLoad10 () % 10);
		CHECK (P1.NCores == 2 && nOn1 == 2);
		CHECK (B.EstimateLoad10 () == 7 + 2 * 215);			// two NAMs per core: 43 % + 0.7 %
	}
	{
		// serial chain IN -> NAM -> NAM -> OUT: everything on core 0
		static CBoard B;
		B.SetMode (2);
		B.Reset ();
		int a, b;
		OK (B.NetAdd ("nam", 0, 2, "x.adn", &a));
		OK (B.NetAdd ("nam", 0, 5, "x.adn", &b));
		OK (B.NetLink (BD_NET_IN, a, 1, 0, 0.0f));
		OK (B.NetLink (a, b, 0, 0, 0.0f));
		OK (B.NetLink (b, BD_NET_OUT, 0, 1, 0.0f));
		B.Compile (&P1, pI, 1.0f, FALSE);
		CHECK (P1.NCores == 1 && P1.Core[a] == 0 && P1.Core[b] == 0);
		CHECK (B.EstimateLoad10 () == 7 + 2 * 215);
	}
	{
		// with 3 cores and 6 branches: two NAMs per core; the 75% limit allows 6 NAMs
		CBoard::SetCores (3);
		static CBoard B;
		B.SetMode (2);
		B.Reset ();
		int n[6];
		for (int i = 0; i < 6; i++)
		{
			OK (B.NetAdd ("nam", i, 3, "x.adn", &n[i]));
			OK (B.NetLink (BD_NET_IN, n[i], 1, 0, 0.0f));
			OK (B.NetLink (n[i], BD_NET_OUT, 0, 1, 0.0f));
		}
		B.Compile (&P1, pI, 1.0f, FALSE);
		unsigned c[3] = {0, 0, 0};
		for (int i = 0; i < 6; i++)
		{
			c[P1.Core[n[i]]]++;
		}
		CHECK (P1.NCores == 3 && c[0] == 2 && c[1] == 2 && c[2] == 2);
		CBoard::SetCores (2);
	}

	// ---- identical results: random volume/pan/routing networks, assigned at random to the cores, with 3 threads in place of the cores
	CBoard::SetCores (1);
	std::thread W1 (GraphWorker, 1u), W2 (GraphWorker, 2u), W3 (GraphWorker, 3u);
	W1.detach ();
	W2.detach ();
	W3.detach ();
	GraphMcEnable (4);
	GraphMcSetTimeout (270000000u);		// 5 s: on the PC the threads can be suspended by the system, the test must not mistake that for a fault

	unsigned nNets = 0;
	for (int trial = 0; trial < 300; trial++)
	{
		static CBoard B;
		B.SetMode (2);
		B.Reset ();
		int ids[20], nId = 0;
		const char *Types[] = {"vol", "rout", "pan"};
		const unsigned nBlocks = 3 + Rnd (10);
		for (unsigned i = 0; i < nBlocks; i++)
		{
			if (!B.NetAdd (Types[Rnd (3)], Rnd (6), 1 + Rnd (8), "", &ids[nId]))
			{
				B.SetValue (ids[nId], "vol", (float) Rnd (20) - 12.0f);
				B.SetValue (ids[nId], "pan", (float) Rnd (200) - 100.0f);
				nId++;
			}
		}
		for (int k = 0; k < 25; k++)
		{
			const int a = Rnd (nId + 1) - 1, b = Rnd (nId + 1) - 1;
			B.NetLink (a < 0 ? BD_NET_IN : ids[a], b < 0 ? BD_NET_OUT : ids[b], a < 0 ? 1 + Rnd (2) : Rnd (3), b < 0 ? 1 + Rnd (2) : Rnd (3), (float) Rnd (12) - 8.0f);
		}
		B.Compile (&P1, pI, 1.0f, FALSE);
		memcpy (&P2, &P1, sizeof P1);
		const unsigned nCores = 2 + Rnd (3);
		for (int i = 0; i < nId; i++)
		{
			P2.Core[ids[i]] = (u8) Rnd (nCores);
		}
		P2.NCores = (u8) nCores;
		nNets++;

		float inL[8], inR[8], o1L[8], o1R[8], o2L[8], o2R[8];
		for (int blk = 0; blk < 60; blk++)
		{
			for (int j = 0; j < 8; j++)
			{
				inL[j] = 0.3f * sinf ((float) (blk * 8 + j) * 0.21f);
				inR[j] = 0.2f * cosf ((float) (blk * 8 + j) * 0.37f);
			}
			GraphResetSmoothing ();
			P1.NCores = 1;
			GraphProcess (&P1, inL, inR, o1L, o1R, 8);
			GraphResetSmoothing ();
			GraphProcess (&P2, inL, inR, o2L, o2R, 8);
			if (memcmp (o1L, o2L, sizeof o1L) != 0 || memcmp (o1R, o2R, sizeof o1R) != 0)
			{
				printf ("network %d block %d: different output with %u cores\n", trial, blk, nCores);
				fails++;
				break;
			}
		}
	}
	printf ("%u random networks, blocks with several cores: %u, faults: %u\n", nNets, GraphMcBlocks (), GraphMcFaults ());
	CHECK (GraphMcFaults () == 0);
	CHECK (GraphMcBlocks () > 0);


	// ---- real effects (reverbs, delay, chorus, compressor, springs, ...) on different cores: same outputs bit for bit.
	// Each network has two sets of identical instances: one for the single-core run, one for the multi-core run.
	{
		const char *FxNames[] = {"reverb", "delay", "phaser", "adelcomp", "adelverb", "adeldelay", "adelchorus", "adelspring", "adelshifter", "adelequal", "adeldelaym", "adelverbm",
					 "tremolo", "tapeecho", "rotary", "flanger", "octaver", "gate", "autowah", "slicer", "duck", "geq", "dynacomp", "limiter", "vol", "pan"};
		const unsigned nNames = sizeof FxNames / sizeof FxNames[0];
		unsigned nFxNets = 0;
		for (int trial = 0; trial < 120; trial++)
		{
			static CBoard B;
			B.SetMode (2);
			B.Reset ();
			int ids[20], nId = 0;
			const unsigned nBlocks = 3 + Rnd (7);
			for (unsigned i = 0; i < nBlocks; i++)
			{
				if (!B.NetAdd (FxNames[Rnd (nNames)], Rnd (6), 1 + Rnd (8), "", &ids[nId]))
				{
					nId++;
				}
			}
			if (nId < 3)
			{
				continue;
			}
			for (int k = 0; k < 20; k++)
			{
				const int a = Rnd (nId + 1) - 1, b = Rnd (nId + 1) - 1;
				B.NetLink (a < 0 ? BD_NET_IN : ids[a], b < 0 ? BD_NET_OUT : ids[b], a < 0 ? 1 + Rnd (2) : Rnd (3), b < 0 ? 1 + Rnd (2) : Rnd (3), (float) Rnd (8) - 4.0f);
			}
			CFx *pFxA[BD_MAX_BLOCKS] = {0}, *pFxB[BD_MAX_BLOCKS] = {0};
			for (int i = 0; i < nId; i++)
			{
				const unsigned nType = B.Block (ids[i]).Type;
				if (nType >= FxFirst && nType <= FxLast)
				{
					pFxA[ids[i]] = FxCreate (nType, 48000.0f);
					pFxB[ids[i]] = FxCreate (nType, 48000.0f);
				}
			}
			B.Compile (&P1, pI, 1.0f, FALSE, pFxA);
			B.Compile (&P2, pI, 1.0f, FALSE, pFxB);
			const unsigned nCores = 2 + Rnd (3);
			for (int i = 0; i < nId; i++)
			{
				P2.Core[ids[i]] = (u8) Rnd (nCores);
			}
			P2.NCores = (u8) nCores;
			P1.NCores = 1;
			nFxNets++;
			float inL[8], inR[8], o1L[8], o1R[8], o2L[8], o2R[8];
			for (int blk = 0; blk < 120; blk++)
			{
				for (int j = 0; j < 8; j++)
				{
					inL[j] = 0.3f * sinf ((float) (blk * 8 + j) * 0.11f);
					inR[j] = 0.2f * cosf ((float) (blk * 8 + j) * 0.29f);
				}
				GraphResetSmoothing ();
				GraphProcess (&P1, inL, inR, o1L, o1R, 8);
				GraphResetSmoothing ();
				GraphProcess (&P2, inL, inR, o2L, o2R, 8);
				if (memcmp (o1L, o2L, sizeof o1L) != 0 || memcmp (o1R, o2R, sizeof o1R) != 0)
				{
					printf ("network with effects %d block %d: different output with %u cores\n", trial, blk, nCores);
					fails++;
					break;
				}
			}
		}
		printf ("%u networks with real effects, faults: %u\n", nFxNets, GraphMcFaults ());
		CHECK (nFxNets > 50);
		CHECK (GraphMcFaults () == 0);
	}

	// ---- scheduling: two parallel reverbs are split; ADELphase stays on core 0
	CBoard::SetCores (2);
	{
		static CBoard B;
		B.SetMode (2);
		B.Reset ();
		int a, b, c;
		OK (B.NetAdd ("adelverb", 1, 3, "", &a));
		OK (B.NetAdd ("adelverb", 3, 3, "", &b));
		OK (B.NetAdd ("adelphase", 5, 3, "", &c));
		OK (B.NetLink (BD_NET_IN, a, 1, 0, 0.0f));
		OK (B.NetLink (BD_NET_IN, b, 1, 0, 0.0f));
		OK (B.NetLink (BD_NET_IN, c, 1, 0, 0.0f));
		OK (B.NetLink (a, BD_NET_OUT, 0, 1, 0.0f));
		OK (B.NetLink (b, BD_NET_OUT, 0, 1, 0.0f));
		OK (B.NetLink (c, BD_NET_OUT, 0, 1, 0.0f));
		B.Compile (&P1, pI, 1.0f, FALSE);
		printf ("2 reverbs + ADELphase on 2 cores: cores %u %u %u\n", P1.Core[a], P1.Core[b], P1.Core[c]);
		CHECK (P1.Core[a] != P1.Core[b] && P1.Core[c] == 0);
	}
	CBoard::SetCores (1);
	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	fflush (stdout);
	_Exit (fails ? 1 : 0);
}
