import numpy as np, sys, math, os
SR = 48000
DELAY = 0.016   # analysis window is 32 ms long: its result belongs to the time 16 ms earlier

def cents(a, b): return 1200 * math.log2(a / b)

def score(logpath, truth, verbose=False):
    rows = np.array([[float(a) for a in l.split()] for l in open(logpath)])
    t, v, hz, c, rms = rows.T
    n = len(t)
    tt = np.clip(((t - DELAY) * SR).astype(int), 0, len(truth) - 1)
    tr = truth[tt]
    # truth notes (segments of constant-ish pitch separated by silence or >80 cents jumps)
    sounding = tr > 0
    good = octv = wrong = unv = 0
    for i in range(n):
        if not sounding[i]: continue
        if rms[i] < 10 ** (-55 / 20):  # below the tracker's gate: not counted
            continue
        if not v[i] or hz[i] <= 0: unv += 1; continue
        d = cents(hz[i], tr[i])
        if abs(d) < 50: good += 1
        elif min(abs(abs(d) - 1200), abs(abs(d) - 2400)) < 100: octv += 1
        else: wrong += 1
    tot = good + octv + wrong + unv
    # onsets of the truth: sounding after silence, or pitch jump > 80 cents
    ons = []
    prev = 0.0
    for k in range(1, len(truth)):
        f = truth[k]
        if f > 0 and (prev == 0 or abs(cents(f, prev)) > 80): ons.append(k)
        prev = f
    lat = []; miss = 0
    for k in ons:
        tk = k / SR + DELAY
        got = None
        for i in np.where((t >= tk) & (t < tk + 0.4))[0]:
            if not v[i] or hz[i] <= 0: continue
            ti = int((t[i] - DELAY) * SR)
            if ti >= len(truth) or truth[ti] <= 0: continue
            if abs(cents(hz[i], truth[ti])) < 50 and abs(cents(truth[ti], truth[k])) < 80:
                got = t[i] - DELAY - k / SR; break
        if got is None: miss += 1
        else: lat.append(got)
    # reported note changes (more than 60 cents away from the previous voiced value after stabilising)
    changes = 0; short = 0; last = None; run = 0
    for i in range(n):
        if v[i] and hz[i] > 0:
            if last is None or abs(cents(hz[i], last)) > 60:
                if last is not None:
                    changes += 1
                    if run * 256 / SR < 0.05: short += 1
                last = hz[i]; run = 0
            run += 1
        else:
            last = None; run = 0
    return dict(good=100 * good / max(tot, 1), octv=100 * octv / max(tot, 1), wrong=100 * wrong / max(tot, 1), unv=100 * unv / max(tot, 1),
                onsets=len(ons), found=len(lat), miss=miss, lat_med=1000 * np.median(lat) if lat else -1, lat_p90=1000 * np.percentile(lat, 90) if lat else -1,
                changes=changes, short=short, frames=tot)

if __name__ == "__main__":
    synth, logs, tag = sys.argv[1], sys.argv[2], sys.argv[3]
    names = ["run6_picked", "run10_picked", "run6_legato", "bend", "palm_mute", "power_chords", "high_notes"]
    print("%-14s %7s %7s %7s %7s | %6s %5s %5s %8s %8s | %7s %5s" % (tag, "good%", "oct%", "wrong%", "unv%", "onsets", "found", "miss", "lat med", "lat p90", "changes", "short"))
    agg = []
    for nme in names:
        truth = np.load(os.path.join(synth, nme + "_truth.npy"))
        r = score(os.path.join(logs, tag + "_" + nme + ".txt"), truth)
        agg.append(r)
        print("%-14s %7.1f %7.1f %7.1f %7.1f | %6d %5d %5d %6.0f ms %6.0f ms | %7d %5d" % (nme, r["good"], r["octv"], r["wrong"], r["unv"], r["onsets"], r["found"], r["miss"], r["lat_med"], r["lat_p90"], r["changes"], r["short"]))
    print("%-14s %7.1f %7.1f %7.1f %7.1f | %6d %5d %5d %6s    %6s    | %7d %5d" % ("TOTAL", np.mean([a["good"] for a in agg]), np.mean([a["octv"] for a in agg]), np.mean([a["wrong"] for a in agg]), np.mean([a["unv"] for a in agg]),
          sum(a["onsets"] for a in agg), sum(a["found"] for a in agg), sum(a["miss"] for a in agg), "", "", sum(a["changes"] for a in agg), sum(a["short"] for a in agg)))
