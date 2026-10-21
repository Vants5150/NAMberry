import json, re, sys, urllib.request, urllib.parse

"""Installs the block presets listed below on a NAMberry board.

Usage:  python3 block_presets.py http://<board-address>          (dry run: prints what would be saved)
        python3 block_presets.py http://<board-address> apply    (saves them; existing presets with the same name are replaced)

The sources of every setting are in presets_fonti.txt; entries marked "(typical)" are starting points, not documented settings.
The board keeps at most 48 block presets.
"""
import json, re, sys, urllib.request, urllib.parse

if len(sys.argv) < 2:
    sys.exit(__doc__)
BOARD = sys.argv[1].rstrip("/")
APPLY = len(sys.argv) > 2 and sys.argv[2] == "apply"
defs = {e["fx"]: e for e in json.load(urllib.request.urlopen(BOARD + "/fx", timeout=20))}

# (name <= 31 chars, fx, overrides)
P = [
 # ZVEX Fuzz Factory manual, example settings in o'clock notation (7:00 = 0, 12:00 = 5, 5:00 = 10)
 ("Hi compression fuzz (ZVEX)", "fuzzfactory", dict(volume=3, gate=8, comp=2, drive=10, stab=10)),
 ("Velcro fuzz (ZVEX)", "fuzzfactory", dict(volume=3, gate=0, comp=10, drive=10, stab=7)),
 ("Hi octave fuzz (ZVEX)", "fuzzfactory", dict(volume=3, gate=7.5, comp=0, drive=0, stab=10)),
 ("Smooth fuzz (ZVEX)", "fuzzfactory", dict(volume=3, gate=0, comp=8, drive=10, stab=10)),
 # Guitar Chalk, Blur "Song 2": Distortion 10, Filter 6-7, Volume 5-6
 ("Song 2 (Guitar Chalk)", "ratts", dict(distortion=10, filter=6.5, volume=5.5)),
 # MusicRadar FX files via search summary: rhythm Volume 10:00 Drive 2 Tone 2; lead Volume 11:00 Drive 10 Tone 1 (0-10 -> block's 0-100)
 ("Rock rhythm (MusicRadar)", "ocd", dict(drive=20, tone=20, volume=3)),
 ("Lead (MusicRadar)", "ocd", dict(drive=100, tone=10, volume=4)),
 # TalkBass / Seymour Duncan forum posts on the OC-2
 ("Bass 10 o'clock (forum)", "octaver", dict(sub=30, oct2=30)),
 ("1x15 cab (TalkBass)", "octaver", dict(dry=75, sub=50, oct2=0)),
 ("2x15 cab (TalkBass)", "octaver", dict(dry=75, sub=33, oct2=0)),
 # jazzguitar.be forum generic suggestion Fuzz 8-10, Volume 5-7
 ("Purple Haze style (forum)", "fuzzface", dict(fuzz=9, volume=6)),
 # Toe Bender MkII review (a clone): Level 10:00, Attack minimum
 ("Gentle (Toe Bender review)", "bender", dict(attack=0, level=3)),
 ("Full fuzz (typical)", "bender", dict(attack=8, level=6)),
 # MXR M117R manual: start with all controls at 12:00; Width at zero = static, Manual alone sets the frequency
 ("Starting point (MXR manual)", "flanger", dict(rate=5, depth=5, manual=5, fb=5)),
 ("Static, Width 0 (MXR manual)", "flanger", dict(rate=5, depth=0, manual=5, fb=5)),
 # Gearank: more aggressive as LEVEL goes down and DIST up
 ("Aggressive (typical)", "ds1", dict(dist=8, tone=5, level=4)),
 # UA / MusicRadar LA-2A notes: Peak Reduction 40-60 % for vocals; bass: lower, Gain about 40 %
 ("Vocals PR 50 (UA tips)", "opticalla", dict(pr=50, mode=0, gain=0)),
 ("Bass low PR (UA tips)", "opticalla", dict(pr=30, mode=0, gain=2.4)),
 # German review of the Dyna Comp Deluxe: Output 2:00, Sensitivity 2:00 (the Deluxe is not the original)
 ("Country (Deluxe review)", "dynacomp", dict(sens=7, output=7)),
 # reviews: slow = gradual sweep, fast = rotary-like
 ("Slow sweep (typical)", "phase90", dict(speed=2.5)),
 ("Fast swirl (typical)", "phase90", dict(speed=7)),
 ("Depth up (typical)", "ce2", dict(rate=3, depth=8)),
 # guitarchalk Kravitz amp EQ (Super Lead): Bass 5 Mid 6 Treble 7
 ("Mid-forward (Kravitz amp EQ)", "guvnor", dict(gain=6, bass=5, mids=6, treble=7, level=5)),
 ("Plexi EQ (Kravitz amp)", "tonestack", dict(amp=3, bass=5, mid=6, treble=7)),
 ("Full boost (typical)", "rangemaster", dict(range=10)),
 ("Chorus, slow (typical)", "univibe", dict(rate=2.0, intensity=70, mode=0, volume=10)),
 ("Vibrato (typical)", "univibe", dict(rate=4.0, intensity=80, mode=1, volume=10)),
 ("Mode 1, wide (SDD-320)", "dimension", dict(mode=0)),
 ("Mode 3+4, tight (SDD-320)", "dimension", dict(mode=5)),
 ("Slapback (typical)", "ad9", dict(delay=2, repeat=1.5, level=3.5)),
 ("Ambient (typical)", "ad9", dict(delay=8, repeat=5, level=5)),
 ("Cocked wah (typical)", "crybaby", dict(pos=4)),
 ("Slow jet (typical)", "mflanger", dict(rate=2, regen=6)),
 ("Low gain boost (typical)", "centaur", dict(gain=1.5, treble=5, output=8)),
]

assert len(P) <= 37, len(P)
out = []
for name, fx, ov in P:
    assert len(name) <= 31, name
    e = defs[fx]
    keys = {p['k']: p for p in e['p']}
    for k in ov:
        assert k in keys, (fx, k)
        p = keys[k]
        assert p['min'] <= ov[k] <= p['max'], (name, k, ov[k], p['min'], p['max'])
    parts = []
    for p in e['p']:
        if p['k'] in ('gr', 'gr2'):
            continue
        v = ov.get(p['k'], p['def'])
        parts.append("%s=%s" % (p['k'], ('%.2f' % v).rstrip('0').rstrip('.') if v != int(v) else str(int(v))))
    data = "fx:%s|%s" % (fx, "|".join(parts))
    assert len(data) < 780, (name, len(data))
    out.append((name, data))

for n, d in out:
    print("%-32s %s" % (n, d))
print(len(out), "presets")

if APPLY:
    html = urllib.request.urlopen(BOARD + "/t.js", timeout=10).read().decode()
    tok = re.search(r'NB_T="([a-z0-9]+)"', html).group(1)
    bad = 0
    for n, d in out:
        body = urllib.parse.urlencode({"t": tok, "op": "save", "n": n, "d": d}).encode()
        r = json.load(urllib.request.urlopen(urllib.request.Request(BOARD + "/bp", data=body), timeout=10))
        if not r.get("ok"):
            bad += 1
            print("FAIL", n, r)
    print("saved", len(out) - bad, "failed", bad)
