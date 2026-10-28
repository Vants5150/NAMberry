#!/usr/bin/env python3
# Plays the NAMberry side of the serial link on the Pi to measure how fast the display accepts data.
# usage: fake_namberry.py PORT CHUNK [TOTAL]   - waits for "Q <id> G ..." and answers with TOTAL filler bytes in writes of CHUNK bytes.
import sys, time, serial

port, chunk = sys.argv[1], int(sys.argv[2])
total = int(sys.argv[3]) if len(sys.argv) > 3 else 109806
s = serial.Serial(port, 115200, timeout=0.2, write_timeout=30)	# default DTR/RTS: with RTS low the display sends nothing
print("waiting for a request on", port, flush=True)
s.reset_input_buffer()				# requests queued before we opened belong to a request the display has already given up
buf = b""
line = None
while True:
    buf += s.read(256)
    reqs = [l.decode(errors="replace").strip() for l in buf.split(b"\n")[:-1]]
    reqs = [l for l in reqs if l.startswith("Q ")]
    if reqs:
        time.sleep(0.4)				# a newer request may be on its way: answer the latest one, the only one the display still waits for
        buf += s.read(4096)
        reqs = [l.decode(errors="replace").strip() for l in buf.split(b"\n")[:-1]]
        line = [l for l in reqs if l.startswith("Q ")][-1]
        break
rid = line.split()[1]
print("request:", line, flush=True)
s.write(("A %s 200 %d\n" % (rid, total)).encode())
body = (b"x" * total)
t0 = time.time()
sent = 0
last = t0
BLOCK = 4096				# same flow control as the board: after each block (but the last) wait for a 'K' from the display
acks = 0
while sent < total:
    end = min(total, sent + BLOCK)
    while sent < end:
        n = s.write(body[sent:min(end, sent + chunk)])
        s.flush()
        sent += n
    if sent < total:
        t1 = time.time()
        ack = b""
        while time.time() - t1 < 1.5 and ack != b"K":
            ack = s.read(1)
        if ack != b"K":
            print("NO ACK after %d bytes" % sent, flush=True)
            sys.exit(1)
        acks += 1
    now = time.time()
    if now - last >= 2:
        print("sent %d bytes in %.1f s = %.0f B/s" % (sent, now - t0, sent / (now - t0)), flush=True)
        last = now
dt = time.time() - t0
print("done: %d bytes in %.1f s = %.0f B/s (chunk %d)" % (sent, dt, sent / dt, chunk), flush=True)
