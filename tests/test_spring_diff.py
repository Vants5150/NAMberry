#!/usr/bin/env python3
# Comparison between the board's final state (spr_end.bin) and the PC's after the replay (spr_pc_end.bin), per ring and per time (age of the sample from the write pointer)
import struct, sys
def rd(n):
    b = open(n, "rb").read()
    return list(struct.unpack("<%df" % (len(b) // 4), b))
B = rd(sys.argv[1]); P = rd(sys.argv[2])
per = 8192 + 2 * 72 * 6 + 7
print("float:", len(B), len(P))
for s in range(3):
    base = 1 + s * per
    w = int(B[base + 8192 + 864 + 5]); wp = int(P[base + 8192 + 864 + 5])
    print("spring %d: write pointer board %d PC %d" % (s, w, wp))
    ring_b = B[base:base + 8192]; ring_p = P[base:base + 8192]
    diffs = []
    for i in range(8192):
        age = (w - 1 - i) % 8192
        d = abs(ring_b[i] - ring_p[i])
        if d > 1e-7 + 1e-5 * abs(ring_b[i]):
            diffs.append((age, ring_b[i], ring_p[i]))
    diffs.sort(reverse=True)
    print("  loop: %d different values (of 8192)" % len(diffs))
    if diffs:
        print("  oldest: age %d | most recent: age %d" % (diffs[0][0], diffs[-1][0]))
        for a, b, p in diffs[:14]:
            print("    age %5d samples: board % .6g  PC % .6g" % (a, b, p))
        ages = [a for a, b, p in diffs]
        print("  consecutive ages? minimum/maximum distance between nearby differences:", min(ages[i] - ages[i + 1] for i in range(len(ages) - 1)) if len(ages) > 1 else "-", max(ages[i] - ages[i + 1] for i in range(len(ages) - 1)) if len(ages) > 1 else "-")
    xb = B[base + 8192:base + 8192 + 432]; xp = P[base + 8192:base + 8192 + 432]
    yb = B[base + 8192 + 432:base + 8192 + 864]; yp = P[base + 8192 + 432:base + 8192 + 864]
    nx = sum(1 for a, b in zip(xb, xp) if abs(a - b) > 1e-7 + 1e-5 * abs(a))
    ny = sum(1 for a, b in zip(yb, yp) if abs(a - b) > 1e-7 + 1e-5 * abs(a))
    print("  chain: xh different %d/432, yh different %d/432; max|xh| board %.4g PC %.4g; max|yh| board %.4g PC %.4g" % (nx, ny, max(map(abs, xb)), max(map(abs, xp)), max(map(abs, yb)), max(map(abs, yp))))
    sc = ["lp", "hpx", "hpy", "wob", "a", "w", "n%6"]
    print("  board scalars:", " ".join("%s=%.5g" % (n, v) for n, v in zip(sc, B[base + 8192 + 864:base + per])))
    print("  PC scalars:    ", " ".join("%s=%.5g" % (n, v) for n, v in zip(sc, P[base + 8192 + 864:base + per])))
