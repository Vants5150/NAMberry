// test_newpreset.cpp - the command sequence the Live Display sends to build a simple preset (delete the blocks, place the input and output cells, add up to four blocks, link them in a chain),
// run on the CBoard of the firmware. The display sends the same operations as the web page does (op=del, op=mvio, op=add, op=lk).
// g++ -std=c++17 -Itests/host_shim -I../../../../include -Isrc -Isrc/adel tests/test_newpreset.cpp src/board.cpp src/graph.cpp src/fx*.cpp src/midi.cpp
// test of the command sequence of the new simple preset of the display (same calls the web requests make), on the CBoard of the NAMberry
#include <cstdio>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "params.h"
static int fails = 0;
#define OK(e) do { const char *err_ = (e); if (err_) { printf("unexpected error line %d: %s\n", __LINE__, err_); fails++; } } while (0)
static int Build (CBoard &B, const char *types[4], int in, int out)
{
	// 2. IO cells
	B.NetMoveIo (0, 2, 0); B.NetMoveIo (1, 3, 0); B.NetMoveIo (2, 2, 9); B.NetMoveIo (3, 3, 9);
	int prev = BD_NET_IN, pf = in == 0 ? 1 : 2, n = 0;
	for (int i = 0; i < 4; i++)
	{
		if (!types[i][0]) continue;
		int id = -1;
		OK (B.NetAdd (types[i], 2, 2 + n, "x.adn", &id));
		OK (B.NetLink (prev, id, pf, 0, 0.0f));
		prev = id; pf = 0; n++;
	}
	if (out != 1) OK (B.NetLink (prev, BD_NET_OUT, 0, 1, 0.0f));
	if (out != 0) OK (B.NetLink (prev, BD_NET_OUT, 0, 2, 0.0f));
	return n;
}
int main ()
{
	// start: a board like the live one (blocks in odd places, IO at other cells, a direct link)
	static CBoard B;
	B.SetMode (2);
	B.Reset ();
	int a, b, c;
	OK (B.NetMoveIo (2, 0, 9)); OK (B.NetMoveIo (3, 5, 9));
	OK (B.NetAdd ("noisegate", 2, 1, "", &a)); OK (B.NetAdd ("nam", 0, 6, "x.adn", &b)); OK (B.NetAdd ("adelverbm", 3, 8, "", &c));
	OK (B.NetLink (BD_NET_IN, a, 1, 0, 0.0f)); OK (B.NetLink (a, b, 0, 0, 0.0f)); OK (B.NetLink (b, c, 0, 0, 0.0f)); OK (B.NetLink (c, BD_NET_OUT, 0, 1, -6.0f));
	OK (B.NetLink (BD_NET_IN, BD_NET_OUT, 2, 2, 0.0f));
	// 1. remove all the blocks, then the direct links (the block ids 0..BD_MAX_BLOCKS-1)
	for (int i = 0; i < (int) BD_MAX_BLOCKS; i++) B.NetRemove (i);
	B.NetUnlink (BD_NET_IN, BD_NET_OUT, 2, 2);
	const char *t4[4] = {"noisegate", "nam", "tsdrive", "adelverbm"};
	int n = Build (B, t4, 0, 2);
	printf ("four blocks, input L, output L+R: %d blocks built, %d errors\n", n, fails);
	// again from this state with other choices
	for (int i = 0; i < (int) BD_MAX_BLOCKS; i++) B.NetRemove (i);
	const char *t2[4] = {"nam", "", "", "adeldelaym"};
	n = Build (B, t2, 1, 1);
	printf ("two blocks, input R, output R: %d blocks built, %d errors\n", n, fails);
	const char *t1[4] = {"", "", "", "adelverbm"};
	for (int i = 0; i < (int) BD_MAX_BLOCKS; i++) B.NetRemove (i);
	n = Build (B, t1, 0, 0);
	printf ("one block: %d built, %d errors\n", n, fails);
	printf (fails ? "FAILED\n" : "RESULT: all ok\n");
	return fails != 0;
}
