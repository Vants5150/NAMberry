// test_parsefloat.cpp - the text written with FormatFixed must re-read with ParseFloat and rewrite identical (otherwise the presets "drift" by a hundredth)
#include <cstdio>
#include <cstring>
#include <cmath>
#include "params.h"

int main ()
{
	int bad = 0, total = 0;
	for (int dec = 1; dec <= 2; dec++)
	{
		for (long k = -20000; k <= 20000; k++)
		{
			const double scale = dec == 1 ? 10.0 : 100.0;
			const float v = (float) ((double) k / scale);
			char a[32], b[32];
			FormatFixed (a, v, (unsigned) dec);
			const char *p = a;
			const float r = ParseFloat (p);
			FormatFixed (b, r, (unsigned) dec);
			total++;
			if (strcmp (a, b) != 0)
			{
				if (bad < 12) printf ("dec %d: value %.6f -> \"%s\" -> %.9f -> \"%s\"\n", dec, (double) v, a, (double) r, b);
				bad++;
			}
		}
	}
	printf ("%d cases, %d different\n", total, bad);
	return bad ? 1 : 0;
}
