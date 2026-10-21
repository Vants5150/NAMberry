import os
# PWM fan test (test image fanvariant.sh): speed ramp with the user present.
# Usage: python3 fan_ramp.py [seconds_per_step] [comma-separated percentages]
# It does not touch the pedalboard. The test ends by itself (300 s without commands or 70 C); at the end it sends fandt v=-1.
import re, json, sys, time, urllib.request, urllib.parse
B = os.environ.get("NAMBERRY_URL", "http://namberry5.local")
t = re.search(r'NB_T="([a-z0-9]+)"', urllib.request.urlopen(B + "/t.js").read().decode()).group(1)

def g(**kw):
    d = urllib.parse.urlencode(dict(t=t, **kw)).encode()
    for _ in range(6):
        try:
            return json.loads(urllib.request.urlopen(urllib.request.Request(B + "/g", data=d), timeout=10).read().decode())
        except Exception:
            time.sleep(0.5)

def therm():
    for _ in range(6):
        try:
            return json.loads(urllib.request.urlopen(B + "/therm?_=%d" % int(time.time() * 1000), timeout=10).read().decode())
        except Exception:
            time.sleep(0.5)

step = int(sys.argv[1]) if len(sys.argv) > 1 else 12
levels = [int(x) for x in sys.argv[2].split(",")] if len(sys.argv) > 2 else [100, 75, 55, 40, 30, 25, 20, 15, 10, 0]
r = g(op="fandt", v=levels[0])
print("start:", r)
if not r or not r.get("ok"):
    sys.exit("image without the fan test")
try:
    for p in levels:
        g(op="fandt", v=p)
        t0 = time.time()
        print("--- fan at %d%%: listen" % p, flush=True)
        while time.time() - t0 < step:
            time.sleep(4)
            x = therm()
            g(op="fandt", v=p)                  # renews the command
            print("   %d s | %.1f C | duty reported %s" % (time.time() - t0, x["temp"] / 10.0, x.get("duty")), flush=True)
finally:
    print("end of test:", g(op="fandt", v=-1))
