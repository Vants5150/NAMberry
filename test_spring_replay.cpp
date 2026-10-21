// test_spring_replay.cpp - replays on the PC the state frozen on the board and the input that follows it: does the burst reappear?
// usage: test_spring_replay state.bin history.bin expected_index   (float32 little endian; history = input from the moment of the state onwards)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
extern "C" {
void *rp_new (float fs);
unsigned rp_state_floats (void);
void rp_import (void *p, const float *pState);
void rp_process (void *p, const float *pIn, float *pL, float *pR, unsigned n, const float *pParams);
float rp_max_out_v (void *p);
float rp_events (void *p);
void rp_export (void *p, float *pOut);
void rp_export_dbg (void *p, float *pOut);
}

static std::vector<float> ReadF (const char *name)
{
	std::vector<float> v;
	FILE *f = fopen (name, "rb");
	if (!f) { printf ("missing file: %s\n", name); exit (2); }
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	v.resize ((size_t) n / 4);
	if (fread (v.data (), 4, v.size (), f) != v.size ()) { printf ("incomplete read\n"); exit (2); }
	fclose (f);
	return v;
}

int main (int argc, char **argv)
{
	if (argc < 3) { printf ("usage: state.bin history.bin [expected_index]\n"); return 2; }
	std::vector<float> st = ReadF (argv[1]), hist = ReadF (argv[2]);
	const long expect = argc > 3 ? atol (argv[3]) : -1;
	if (st.size () != rp_state_floats ()) { printf ("state size %zu, expected %u\n", st.size (), rp_state_floats ()); return 2; }
	static const float P[14] = {35, 0, 0, 0, 1, 2, 2.2f, 0.6f, 0.4f, 0.35f, 0, 150, 4500, 0.2f};
	void *p = rp_new (48000.0f);
	rp_import (p, st.data ());
	double maxIn = 0;
	for (float x : hist) maxIn = fmax (maxIn, fabs (x));
	printf ("history: %zu samples, maximum input %g; burst expected (on the board) at sample %ld\n", hist.size (), maxIn, expect);
	double pk = 0; long first = -1;
	for (size_t i = 0; i + 4 <= hist.size (); i += 4)
	{
		float L[4], R[4];
		rp_process (p, &hist[i], L, R, 4, P);
		for (int j = 0; j < 4; j++)
		{
			const double a = fabs (L[j]) - fabs (hist[i + j]);
			pk = fmax (pk, fabs (a));
			if (first < 0 && fabs (a) > 0.05) first = (long) (i + j);
		}
	}
	printf ("PC: output peak %g, maximum written into the ring %g, engine events %g, first sample with |output| > 0.05: %ld\n", pk, rp_max_out_v (p), rp_events (p), first);
	if (argc > 4)
	{
		// comparison of the final state: board (file) against PC after replaying the same history from the same start
		std::vector<float> endB = ReadF (argv[4]), endP (st.size ());
		rp_export (p, endP.data ());
		{ FILE *fo = fopen ("spr_pc_end.bin", "wb"); if (fo) { fwrite (endP.data (), 4, endP.size (), fo); fclose (fo); } }
		size_t nd = 0;
		const size_t perSpring = 8192 + 2 * 72 * 6 + 7;
		auto name = [&](size_t i, char *buf) {
			if (i == 0) { sprintf (buf, "header"); return; }
			size_t k = i - 1;
			if (k < 3 * perSpring) {
				size_t s = k / perSpring, r = k % perSpring;
				if (r < 8192) sprintf (buf, "spring %zu ring[%zu]", s, r);
				else if (r < 8192 + 432) sprintf (buf, "spring %zu xh[stage %zu][%zu]", s, (r - 8192) / 6, (r - 8192) % 6);
				else if (r < 8192 + 864) sprintf (buf, "spring %zu yh[stage %zu][%zu]", s, (r - 8192 - 432) / 6, (r - 8192 - 432) % 6);
				else sprintf (buf, "spring %zu scalar %zu (lp,hpx,hpy,wob,a,w,n)", s, r - 8192 - 864);
			} else sprintf (buf, "common[%zu]", k - 3 * perSpring);
		};
		long runStart = -1; size_t runLen = 0; int shown = 0;
		for (size_t i = 0; i <= st.size (); i++)
		{
			const bool diff = i < st.size () && fabs (endB[i] - endP[i]) > 1e-7 + 1e-5 * fabs (endB[i]);
			if (diff) { nd++; if (runStart < 0) { runStart = (long) i; runLen = 0; } runLen++; }
			else if (runStart >= 0)
			{
				if (shown < 12) { char b1[96], b2[96]; name ((size_t) runStart, b1); name ((size_t) runStart + runLen - 1, b2); printf ("  difference: %zu consecutive values from %s to %s; first: board %g PC %g\n", runLen, b1, b2, endB[runStart], endP[runStart]); }
				shown++; runStart = -1;
			}
		}
		printf ("final state board/PC: %zu different values out of %zu (%d zones)\n", nd, st.size (), shown);
	}
	if (argc > 5)
	{
		// intermediate signals sample by sample (last 4096): where does the board first deviate from the PC's recalculation?
		std::vector<float> dB = ReadF (argv[5]), dP (5 * 4096);
		rp_export_dbg (p, dP.data ());
		static const char *nm[5] = {"input x", "saturator output xd", "ring read (spring 0)", "hp feedback (spring 0)", "write v (spring 0)"};
		for (int r = 0; r < 5; r++)
		{
			long firstD = -1; double maxD = 0; int nd2 = 0;
			for (int i = 0; i < 4096; i++)
			{
				const double a = dB[r * 4096 + i], b = dP[r * 4096 + i], d = fabs (a - b);
				if (d > 1e-7 + 1e-4 * fabs (a)) { if (firstD < 0) firstD = i; nd2++; }
				maxD = fmax (maxD, d);
			}
			printf ("  %-32s different %4d/4096, first at sample %ld (of the final 4096), maximum difference %g\n", nm[r], nd2, firstD, maxD);
			if (firstD >= 0)
				for (long i = firstD > 3 ? firstD - 3 : 0; i < firstD + 8 && i < 4096; i++)
					printf ("      %5ld board % .8g  PC % .8g\n", i, dB[r * 4096 + i], dP[r * 4096 + i]);
		}
	}	printf ("%s\n", first >= 0 ? "RESULT: the PC reproduces the burst" : "RESULT: the PC does NOT reproduce the burst");
	return 0;
}
