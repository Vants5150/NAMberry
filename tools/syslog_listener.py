#!/usr/bin/env python3
"""Minimal UDP syslog receiver for the board's log (RFC 5424 over UDP). Appends every message with date and sender.

usage: syslog_listener.py [port [file]]     (defaults: 5514, circle-syslog.log; the port is LOG_SERVER_PORT in src/config.h)
The board sends to the address set as SYSLOG_SERVER_IP in config_local.h.
"""
import socket, sys, time

port = int(sys.argv[1]) if len(sys.argv) > 1 else 5514
out = sys.argv[2] if len(sys.argv) > 2 else "circle-syslog.log"
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", port))
with open(out, "a", buffering=1) as f:
    f.write("# started %s port %d\n" % (time.strftime("%F %T"), port))
    while True:
        data, addr = s.recvfrom(4096)
        txt = data.decode("utf-8", "replace").rstrip("\r\n")
        f.write("%s %s %s\n" % (time.strftime("%F %T"), addr[0], txt))
