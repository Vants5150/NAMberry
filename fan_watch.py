import os
# Observes temperature, fan speed and CPU every N seconds (readings only: /therm and /status).
# Usage: python3 fan_watch.py [duration_s] [interval_s]
import json, sys, time, urllib.request
B = os.environ.get("NAMBERRY_URL", "http://namberry5.local")
dur = int(sys.argv[1]) if len(sys.argv) > 1 else 600
step = int(sys.argv[2]) if len(sys.argv) > 2 else 10

def get(p):
    for _ in range(4):
        try:
            return json.loads(urllib.request.urlopen(B + p + "?_=%d" % int(time.time() * 1000), timeout=8).read().decode())
        except Exception:
            time.sleep(0.7)
    return None

t0 = time.time()
rows = []
while time.time() - t0 < dur:
    th, st = get("/therm"), get("/status")
    if th and st:
        rows.append((time.time() - t0, th["temp"] / 10.0, th.get("fp"), th["mhz"][0], st["cpu"]["avg"], st["up"]))
        print("%4d s | %.1f C | fan %s%% | %d MHz | CPU %.1f%% | up %d" % rows[-1], flush=True)
    time.sleep(step)
if rows:
    T = [r[1] for r in rows]
    print("temperature: min %.1f avg %.1f max %.1f C | average fan %.0f%%" % (min(T), sum(T) / len(T), max(T), sum((r[2] or 0) for r in rows) / len(rows)))
