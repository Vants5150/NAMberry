// test_net_fuzz.cpp - on Linux, with AddressSanitizer and UndefinedBehaviorSanitizer: random operations on the network document (even with out-of-range arguments),
// plan recompilation and audio processing after every step, saving and re-reading. Looks for out-of-bounds writes, uninitialized reads, overflows.
// g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Ihost_shim -I<circle>/include -I. -Iadel test_net_fuzz.cpp board.cpp graph.cpp fx*.cpp -o test_net_fuzz
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "params.h"

static unsigned long long s_Rng = 88172645463325252ULL;
static unsigned Rnd (void)
{
	s_Rng ^= s_Rng << 13;
	s_Rng ^= s_Rng >> 7;
	s_Rng ^= s_Rng << 17;
	return (unsigned) (s_Rng >> 11);
}

static int RndI (int lo, int hi) { return lo + (int) (Rnd () % (unsigned) (hi - lo + 1)); }

// comparison of the saved texts: equal row by row; in the 'd' rows the numbers may differ by a hundredth (the extended 'e' row keeps more decimals:
// a value like 1.2049 is written as 1.20 in the 'd' row but is re-read with three decimals and rewritten as 1.21)
static boolean SameText (const char *p1, const char *p2)
{
	while (*p1 && *p2)
	{
		const char *e1 = strchr (p1, '\n'), *e2 = strchr (p2, '\n');
		const size_t l1 = e1 ? (size_t) (e1 - p1) : strlen (p1), l2 = e2 ? (size_t) (e2 - p2) : strlen (p2);
		if (l1 != l2 || strncmp (p1, p2, l1) != 0)
		{
			if (p1[0] != 'd' || p2[0] != 'd')
			{
				return FALSE;
			}
			char b1[512], b2[512];
			if (l1 >= sizeof b1 || l2 >= sizeof b2)
			{
				return FALSE;
			}
			memcpy (b1, p1, l1); b1[l1] = 0;
			memcpy (b2, p2, l2); b2[l2] = 0;
			char *s1, *s2;
			char *t1 = strtok_r (b1, " ", &s1), *t2 = strtok_r (b2, " ", &s2);
			while (t1 && t2)
			{
				if (strcmp (t1, t2) != 0)
				{
					char *q1, *q2;
					const double v1 = strtod (t1, &q1), v2 = strtod (t2, &q2);
					if (*q1 || *q2 || fabs (v1 - v2) > 0.0101)
					{
						return FALSE;
					}
				}
				t1 = strtok_r (0, " ", &s1);
				t2 = strtok_r (0, " ", &s2);
			}
			if (t1 || t2)
			{
				return FALSE;
			}
		}
		p1 += l1 + (e1 ? 1 : 0);
		p2 += l2 + (e2 ? 1 : 0);
	}
	return *p1 == 0 && *p2 == 0;
}

int main (int argc, char **argv)
{
	const long nOps = argc > 1 ? atol (argv[1]) : 200000;
	if (argc > 2)
	{
		s_Rng ^= (unsigned long long) atol (argv[2]) * 0x9E3779B97F4A7C15ULL;
	}
	static CBoard B[2];
	B[0].SetMode (2);
	B[1].SetMode (1);
	B[0].Reset ();
	static TPlan P;
	adelnam::Instance *pI[BD_MAX_NAM] = {0};
	static char Text[65536];
	static CBoard Back;
	Back.SetMode (2);
	const char *Types[] = {"vol", "rout", "pan", "nam", "split", "xx", "adelverb", "adeldelay", "adelcomp", "adelequal", "adelphase", "adelsendsweep", "adelshifter", "cabir", "phaser"};
	float inL[PL_MAX_BLOCK], inR[PL_MAX_BLOCK], oL[PL_MAX_BLOCK], oR[PL_MAX_BLOCK];
	for (unsigned j = 0; j < PL_MAX_BLOCK; j++)
	{
		inL[j] = 0.3f * sinf ((float) j * 0.2f);
		inR[j] = 0.2f * cosf ((float) j * 0.3f);
	}
	long nOk = 0, nErr = 0, nRound = 0;
	for (long i = 0; i < nOps; i++)
	{
		CBoard &G = B[0];
		int a = RndI (-3, 45), b = RndI (-3, 45);
		const int op = RndI (0, 18);
		const char *e = 0;
		switch (op)
		{
		case 0:
		case 1:
			e = G.NetAdd (Types[RndI (0, 14)], RndI (-2, 8), RndI (-2, 12), "x.adn", &a);
			break;
		case 2:
			e = G.NetRemove (a);
			break;
		case 3:
			e = G.NetMove (a, RndI (-2, 8), RndI (-2, 12));
			break;
		case 4:
		case 5:
		case 6:
			e = G.NetLink (RndI (-3, 42) , RndI (-3, 42), RndI (-1, 3), RndI (-1, 3), (float) RndI (-100, 40));
			break;
		case 7:
			e = G.NetUnlink (RndI (-3, 42), RndI (-3, 42), RndI (-1, 3), RndI (-1, 3));
			break;
		case 8:
			e = G.NetMoveIo (RndI (-1, 4), RndI (-2, 8), RndI (-2, 12));
			break;
		case 9:
			e = G.NetRetype (a, Types[RndI (0, 14)], "x.adn");
			break;
		case 10:
		{
			const char *Keys[] = {"vol", "pan", "on", "in", "out", "mix", "xx"};
			e = G.SetValue (a, Keys[RndI (0, 6)], (float) RndI (-150, 150) * 0.7f);
			break;
		}
		case 11:
			e = G.SetFile (a, "y.adn");
			break;
		case 12:		// MIDI: a block's CC (as the page does: only existing blocks)
			if (a >= 0 && a < (int) BD_MAX_BLOCKS && G.Block ((unsigned) a).Used)
			{
				if (!G.Midi ().SetBlockCc (RndI (20, 130), a)) e = "rejected";
			}
			else e = "no such block";
			break;
		case 13:		// MIDI: source and link of a controller
			if (a >= 0 && a < (int) BD_MAX_BLOCKS && G.Block ((unsigned) a).Used)
			{
				TMidiCtl &Ct = G.Midi ().Ctl ((unsigned) RndI (0, 2));
				Ct.Cc = RndI (0, 1) ? RndI (32, 127) : 0;
				TMidiLink &L = Ct.Link[RndI (0, 2)];
				L.Block = a;
				L.Param = RndI (0, 40);
				L.MinPct = (float) RndI (0, 100);
				L.MaxPct = (float) RndI (0, 100);
			}
			else e = "no such block";
			break;
		case 14:		// MIDI: normalized parameter
			e = G.SetParamNorm (a, (unsigned) RndI (0, 45), (float) RndI (-20, 120) * 0.01f);
			break;
		case 15:		// MIDI: any message in the map (even with learn active) and application of the linked parameters
		{
			TMidiMsg Mm;
			Mm.Type = (u8) RndI (0, 9);
			Mm.Chan = (u8) RndI (0, 15);
			Mm.A = (u8) RndI (0, 127);
			Mm.B = (u8) RndI (0, 127);
			G.Midi ().SetChannel ((unsigned) RndI (0, 16));
			TMidiAct Act[8];
			const unsigned na = G.Midi ().Handle (Mm, Act, 8);
			for (unsigned k = 0; k < na; k++)
			{
				if (Act[k].Type == ActParam) G.SetParamNorm (Act[k].A, (unsigned) Act[k].B, Act[k].F);
				else if (Act[k].Type == ActClassEnable) { const int id = G.MidiFindClass ((unsigned) Act[k].A); if (id >= 0) G.SetValue (id, "on", Act[k].B ? 1.0f : 0.0f); }
				else if (Act[k].Type == ActBlockEnable) G.SetValue (Act[k].A, "on", Act[k].B ? 1.0f : 0.0f);
			}
			G.MidiClassMask ();
			break;
		}
		case 16:
			if (RndI (0, 1)) G.Midi ().LearnController (RndI (0, 2));
			else if (a >= 0 && a < (int) BD_MAX_BLOCKS && G.Block ((unsigned) a).Used) G.Midi ().LearnBlock (a);		// like the page: only existing blocks
			break;
		case 17:
			G.MarkEdit ();
			break;
		default:
			// saving and re-reading: a valid layout must re-read identical
			{
				const unsigned n = G.Serialize (Text, sizeof Text);
				if (n >= sizeof Text - 2)
				{
					printf ("text too long: %u\n", n);
					return 1;
				}
				static char Text2[65536];
				if (!Back.Parse (Text))
				{
					printf ("operation %ld: the saved text does not re-read\n", i);
					return 1;
				}
				Back.Serialize (Text2, sizeof Text2);
				if (!SameText (Text, Text2))
				{
					printf ("operation %ld: saving and re-reading differ\n", i);
					{
						const char *p1 = Text, *p2 = Text2;
						while (*p1 && *p2)
						{
							const char *e1 = strchr (p1, '\n'), *e2 = strchr (p2, '\n');
							const size_t l1 = e1 ? (size_t) (e1 - p1) : strlen (p1), l2 = e2 ? (size_t) (e2 - p2) : strlen (p2);
							if (l1 != l2 || strncmp (p1, p2, l1) != 0)
							{
								printf ("before: %.*s\nafter : %.*s\n", (int) l1, p1, (int) l2, p2);
								int cc0 = 0, id0 = 0;
								if (sscanf (p1, "x %d %d", &cc0, &id0) == 2 && id0 >= 0 && id0 < (int) BD_MAX_BLOCKS)
								{
									printf ("block %d: used in G = %d, in the re-read = %d; CC in the re-read: %u\n", id0, (int) G.Block ((unsigned) id0).Used, (int) Back.Block ((unsigned) id0).Used, Back.Midi ().BlockCcCount ());
								}
								break;
							}
							p1 += l1 + (e1 ? 1 : 0);
							p2 += l2 + (e2 ? 1 : 0);
						}
					}
					return 1;
				}
				nRound++;
			}
			break;
		}
		if (e)
		{
			nErr++;
		}
		else
		{
			nOk++;
		}
		// the layout must stay valid and the plan must compile and process without leaving the limits
		G.Compile (&P, pI, 1.0f, FALSE);
		GraphProcess (&P, inL, inR, oL, oR, 4 + (unsigned) (i % 60));
		for (unsigned j = 0; j < 4; j++)
		{
			if (!(oL[j] == oL[j]) || !(oR[j] == oR[j]))
			{
				printf ("operation %ld: NaN output\n", i);
				return 1;
			}
		}
		if (i % 5000 == 4999 && Rnd () % 3 == 0)
		{
			G.Reset ();
		}
	}
	printf ("operations %ld: accepted %ld, refused %ld, saves re-read %ld\n", nOps, nOk, nErr, nRound);
	printf ("RESULT: no sanitizer errors\n");
	return 0;
}
