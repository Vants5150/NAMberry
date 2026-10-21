#!/usr/bin/env python3
# Common engine state (after the three spring blocks): board against PC
import struct, sys
def rd(n):
    b = open(n, "rb").read()
    return list(struct.unpack("<%df" % (len(b) // 4), b))
B = rd(sys.argv[1]); P = rd(sys.argv[2])
per = 8192 + 2 * 72 * 6 + 7
c0 = 1 + 3 * per
names = ["hp1[0]", "hp1[1]", "hp2[0]", "hp2[1]", "lp1[0]", "lp1[1]", "lp2[0]", "lp2[1]", "hpi1[0]", "hpi1[1]", "hpi2[0]", "hpi2[1]",
         "adaa.xprev(hi)", "adaa.xprev(lo)", "adaa.init", "shakeCnt", "prevShake", "lastTank", "lastSprings", "dryS", "mixS", "seed(lo16)", "seed(hi16)", "first"]
print("%-16s %-14s %-14s %s" % ("field", "board", "PC", ""))
for i, n in enumerate(names):
    b, p = B[c0 + i], P[c0 + i]
    print("%-16s %-14.7g %-14.7g %s" % (n, b, p, "<<< DIVERSO" if abs(b - p) > 1e-7 + 1e-5 * abs(b) else ""))
