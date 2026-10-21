// test_warm.cpp - the warmed state saved and restored (WarmSave/WarmLoad) must give outputs IDENTICAL bit for bit to an instance warmed with Prewarm
// usage: g++ -std=c++17 -O2 -I. test_warm.cpp -o test_warm && ./test_warm model1.adn [model2.adn ...]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>
#include "adelnam_engine.hpp"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf ("FAILED: %s\n", m); fails++; } else printf ("ok       %s\n", m); } while (0)

int main (int argc, char **argv)
{
	for (int a = 1; a < argc; a++)
	{
		FILE *f = fopen (argv[a], "rb");
		if (!f) { printf ("missing file: %s\n", argv[a]); fails++; continue; }
		fseek (f, 0, SEEK_END);
		long n = ftell (f);
		fseek (f, 0, SEEK_SET);
		uint8_t *data = (uint8_t *) aligned_alloc (64, (n + 63) & ~63L);
		fread (data, 1, n, f);
		fclose (f);
		adelnam::Model M;
		const char *err = adelnam::ParseModel (data, (size_t) n, &M);
		CHECK (err == nullptr, argv[a]);
		if (err) continue;
		const int block = 4;
		size_t bytes = adelnam::Instance::StateBytes (M, block) + 64;
		uint8_t *m1 = (uint8_t *) aligned_alloc (64, (bytes + 63) & ~63UL), *m2 = (uint8_t *) aligned_alloc (64, (bytes + 63) & ~63UL);
		adelnam::Instance A, B;
		A.Init (&M, m1, bytes, block);
		auto t0 = std::chrono::steady_clock::now();
		A.Prewarm ();
		auto t1 = std::chrono::steady_clock::now();
		std::vector<float> snap (A.WarmFloats ());
		uint32_t pos[adelnam::kMaxLayers + 2];
		A.WarmSave (snap.data (), pos);
		B.Init (&M, m2, bytes, block);
		auto t2 = std::chrono::steady_clock::now();
		B.WarmLoad (snap.data (), pos);
		auto t3 = std::chrono::steady_clock::now();
		printf ("         warm-up %.2f ms, restore %.3f ms (%zu floats)\n", std::chrono::duration<double, std::milli> (t1 - t0).count (), std::chrono::duration<double, std::milli> (t3 - t2).count (), snap.size ());
		// same input signal: noise and square wave in blocks of 4 frames
		uint32_t seed = 99;
		bool same = true;
		for (int i = 0; i < 4000 && same; i++)
		{
			float in[4], oa[4], ob[4];
			for (int j = 0; j < 4; j++)
			{
				seed = seed * 1664525u + 1013904223u;
				in[j] = (((i * 4 + j) / 100) & 1 ? 0.15f : -0.15f) + ((float) (int) (seed >> 16) - 32768.0f) * (0.05f / 32768.0f);
			}
			A.Process (in, oa, 4);
			B.Process (in, ob, 4);
			if (memcmp (oa, ob, sizeof oa) != 0) same = false;
		}
		CHECK (same, "outputs identical bit for bit after the restore");
			// silence at the input must come out as zero (the model's DC offset is removed), with Prewarm and with WarmLoad
			{
				adelnam::Instance C, D;
				uint8_t *m3 = (uint8_t *) aligned_alloc (64, (bytes + 63) & ~63UL), *m4 = (uint8_t *) aligned_alloc (64, (bytes + 63) & ~63UL);
				C.Init (&M, m3, bytes, block); C.Prewarm ();
				D.Init (&M, m4, bytes, block); D.WarmLoad (snap.data (), pos);
				float z[4] = {0, 0, 0, 0}, oc[4], od[4];
				C.Process (z, oc, 4); D.Process (z, od, 4);
				CHECK (fabsf (oc[3]) < 1e-6f && fabsf (od[3]) < 1e-6f, "silence at the input = zero at the output (DC offset removed)");
				free (m3); free (m4);
			}
		free (data); free (m1); free (m2);
	}
	printf ("RESULT: %s (%d failed)\n", fails == 0 ? "OK" : "FAILED", fails);
	return fails != 0;
}
