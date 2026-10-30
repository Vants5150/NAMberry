#!/usr/bin/env python3
# Plays the NAMberry's side of the display protocol on a PC / Pi, forwarding the requests to the real board over its web interface.
# It lets a display be developed and tried through any USB serial port without changing the board's firmware. What you do on the display acts on the real board.
# usage: bridge_namberry.py SERIAL_PORT BOARD_IP [BAUD] [SECONDS]
#   "Q <id> G <path> [query]" -> GET http://BOARD_IP<path>?<query>;   "Q <id> P <path> <form>" -> POST with the page token (t=...&pg=1&) added, as the board does.
#   The old firmware does not filter GET /fx: with k=<effect> or l=1 this script takes the whole answer and cuts it, as the new firmware does.
import json, re, sys, time, urllib.request, serial

port, host = sys.argv[1], sys.argv[2]
baud = int(sys.argv[3]) if len(sys.argv) > 3 else 460800
run_s = float(sys.argv[4]) if len(sys.argv) > 4 else 3600
base = "http://" + host
s = serial.Serial(port, baud, timeout=0.05, write_timeout=5)
token = None
fx_all = None

def get_token():
    global token
    t = urllib.request.urlopen(base + "/t.js", timeout=5).read().decode()
    token = re.search(r'NB_T="([a-z0-9]+)"', t).group(1)

def http(method, path, query, form):
    global fx_all, token
    if method == "G":
        if path == "/fx" and ("k=" in query or "l=1" in query):
            if fx_all is None:
                fx_all = json.loads(urllib.request.urlopen(base + "/fx", timeout=10).read())
            if query.startswith("k="):
                body = json.dumps([d for d in fx_all if d["fx"] == query[2:]]).encode()
            else:
                body = json.dumps([{"fx": d["fx"], "title": d["title"], "hid": d["hid"]} for d in fx_all]).encode()
            return 200, body
        url = base + path + ("?" + query if query else "")
        try:
            r = urllib.request.urlopen(url, timeout=10)
            return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, b""
    if token is None:
        get_token()
    data = ("t=" + token + "&pg=1&" + form).encode()
    r = urllib.request.urlopen(urllib.request.Request(base + path, data=data), timeout=10)
    return r.status, r.read()

def send_reply(rid, status, body):
    s.write(("A %s %d %d\n" % (rid, status, len(body))).encode())
    pos = 0
    while pos < len(body):
        end = min(len(body), pos + 4096)
        s.write(body[pos:end]); s.flush()
        pos = end
        if pos < len(body):                       # one 'K' from the display after each block but the last
            t0 = time.time(); ack = b""
            while time.time() - t0 < 1.5 and ack != b"K":
                ack = s.read(1)
            if ack != b"K":
                print("no ACK after %d of %d bytes" % (pos, len(body)), flush=True)
                return

print("bridge: %s at %d baud <-> %s" % (port, baud, base), flush=True)
buf = b""
t_end = time.time() + run_s
while time.time() < t_end:
    buf += s.read(512)
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        parts = line.decode(errors="replace").strip().split(" ", 4)
        if len(parts) < 4 or parts[0] != "Q":
            continue
        rid, method, path = parts[1], parts[2], parts[3]
        rest = parts[4] if len(parts) > 4 else ""
        t0 = time.time()
        try:
            st, body = http(method, path, rest, rest)
        except Exception as e:
            print("request failed:", method, path, rest[:40], e, flush=True)
            st, body = 500, b""
        send_reply(rid, st, body)
        print("%s %s %s -> %d, %d bytes, %d ms" % (method, path, rest[:30], st, len(body), (time.time() - t0) * 1000), flush=True)
