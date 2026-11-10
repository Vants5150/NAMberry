#!/usr/bin/env python3
"""Sends a new kernel to the board over Wi-Fi (TFTP), signed with HMAC-SHA256.

usage: ota_push.py kernel_2712.img --host BOARD_IP [--key ota.key] [--log circle-syslog.log]

Writes update.img, then update.sig (HMAC-SHA256 of the kernel, 64 hexadecimal characters). The board checks the signature with SD:/ota.key,
replaces kernel_2712.img (the old one stays as kernel_2712.bak) and reboots. The script follows the syslog file (see syslog_listener.py) to confirm.
The board accepts TFTP writes only from the address set as OTA_ALLOWED_IP in config_local.h.
"""
import argparse, hashlib, hmac, os, socket, struct, sys, time


def tftp_put(host, name, data, timeout=3.0, retries=6, port=69):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    wrq = b"\x00\x02" + name.encode() + b"\x00octet\x00"
    peer = None
    block = 0
    payload = data
    nblocks = len(payload) // 512 + 1                    # the last block is always shorter than 512 bytes (possibly empty)
    pkt = wrq
    dest = (host, port)
    while True:
        for attempt in range(retries):
            s.sendto(pkt, dest if peer is None else peer)
            try:
                resp, addr = s.recvfrom(1024)
            except socket.timeout:
                continue
            if peer is None:
                peer = addr
            elif addr != peer:
                continue
            op = struct.unpack("!H", resp[:2])[0]
            if op == 5:
                code = struct.unpack("!H", resp[2:4])[0]
                raise RuntimeError("TFTP error %d: %s" % (code, resp[4:-1].decode(errors="replace")))
            if op == 4 and struct.unpack("!H", resp[2:4])[0] == block:
                break
        else:
            raise RuntimeError("no answer from the board (%s, block %d)" % (name, block))
        block += 1
        if block > nblocks:
            return
        chunk = payload[(block - 1) * 512: block * 512]
        pkt = struct.pack("!HH", 3, block) + chunk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kernel")
    ap.add_argument("--host", required=True, help="IP address of the board")
    ap.add_argument("--key", default="ota.key", help="file with the 64 hexadecimal characters of the key (the same as SD:/ota.key)")
    ap.add_argument("--log", default="circle-syslog.log", help="syslog file written by syslog_listener.py")
    ap.add_argument("--wait", type=int, default=90, help="seconds to wait for the confirmation in the log")
    ap.add_argument("--port", type=int, default=69, help="TFTP port (69 normally; different only for tests)")
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    key = bytes.fromhex(open(a.key).read().strip())
    sig = hmac.new(key, kernel, hashlib.sha256).hexdigest()
    print("kernel %s: %d bytes, HMAC %s..." % (a.kernel, len(kernel), sig[:12]))

    try:
        t0 = os.path.getsize(a.log)
    except OSError:
        t0 = 0

    t = time.time()
    tftp_put(a.host, "update.img", kernel, port=a.port)
    print("update.img sent in %.1f s" % (time.time() - t))
    tftp_put(a.host, "update.sig", (sig + "\n").encode(), port=a.port)
    print("update.sig sent, waiting for the board...")

    deadline = time.time() + a.wait
    seen = set()
    replaced = False
    while time.time() < deadline:
        time.sleep(1)
        try:
            with open(a.log, "r", errors="replace") as f:
                f.seek(t0)
                new = f.read()
        except OSError:
            continue
        for line in new.splitlines():
            if "OTA:" in line and line not in seen:
                seen.add(line)
                print(line[line.find("OTA:"):])
                if any(w in line for w in ("refused", "missing or invalid", "cannot", "implausible", "read error")):
                    return 2
        replaced = replaced or "OTA: kernel replaced" in new
        if replaced and "started on Raspberry Pi" in new[new.find("OTA: kernel replaced"):]:
            print("The new kernel is running.")
            return 0
    print("no complete confirmation in the log within %d s" % a.wait)
    return 1


if __name__ == "__main__":
    sys.exit(main())
