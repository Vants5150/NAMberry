#!/usr/bin/env python3
"""Converts a NAM A2 model (.nam, JSON) into the compact binary .adn format read by the ADELnam engine (Linux and Circle).

ALWAYS quality 1.0: from a SlimmableContainer the largest submodel is taken (highest max_value), never the small ones.
Usage: nam2adn.py model.nam [model.adn]

Formato .adn v1 (little endian, tutto a 4 byte):
  char magic[4] = "ADN1"; u32 version = 1; u32 channels; u32 nLayers; u32 headKernel; u32 reserved = 0
  f32 leakySlope; f32 sampleRate; f32 headScale; f32 loudness; f32 inputLevelDbu; f32 outputLevelDbu   (NaN if absent)
  char name[64]  (model name, UTF-8, zero terminated)
  u32 kernel[nLayers]; u32 dilation[nLayers]
  f32 rechannel[C]
  per layer: f32 convW[K*C*C] (index k*C*C + j*C + i, i = output, j = input), f32 convB[C], f32 mixin[C],
              f32 l1w[C*C] (index j*C + i), f32 l1b[C]
  f32 headW[headKernel*C] (index k*C + b), f32 headB
  u32 crc32 of everything that precedes
"""
import json, math, struct, sys, zlib

LEAKY = 0.01


def fail(msg):
    sys.exit("nam2adn: " + msg)


def load_submodel(d):
    arch = d.get("architecture")
    if arch == "SlimmableContainer":
        subs = d["config"]["submodels"]
        sub = max(subs, key=lambda s: s["max_value"])        # quality 1.0: the largest submodel
        return sub["model"]
    if arch == "WaveNet":
        return d
    fail("unsupported architecture: %s" % arch)


def convert(path, out):
    d = json.load(open(path))
    md = d.get("metadata", {})
    m = load_submodel(d)
    if m.get("architecture") != "WaveNet":
        fail("the submodel is not a WaveNet")
    cfg = m["config"]
    layers = cfg["layers"]
    if len(layers) != 1:
        fail("expected a single layer array, found %d" % len(layers))
    L = layers[0]
    C = L["channels"]
    n = len(L["kernel_sizes"])
    if n != len(L["dilations"]) or n != len(L["activation"]):
        fail("lists of different length")
    if L["bottleneck"] != C or L["input_size"] != 1 or L["condition_size"] != 1:
        fail("not the A2 form (bottleneck/input/condition)")
    for a in L["activation"]:
        if a.get("type") != "LeakyReLU" or abs(a.get("negative_slope", 0) - LEAKY) > 1e-9:
            fail("activation is not LeakyReLU(0.01): %s" % a)
    if set(L["gating_mode"]) != {"none"} or any(s is not None for s in L["secondary_activation"]):
        fail("unsupported gating")
    if L["head1x1"].get("active") or not L["layer1x1"].get("active") or L["layer1x1"].get("groups", 1) != 1:
        fail("head1x1/layer1x1 not in the A2 form")
    if L["head"].get("out_channels") != 1 or not L["head"].get("bias", False):
        fail("head not in the A2 form")
    for key, v in L.items():
        if key.endswith("_film") and isinstance(v, dict) and v.get("active"):
            fail("FiLM active: %s" % key)
    if cfg.get("head") is not None:
        fail("post-stack head not supported")
    HK = L["head"]["kernel_size"]
    K = L["kernel_sizes"]
    D = L["dilations"]
    w = list(m["weights"])
    expect = C + sum(k * C * C + C + C + C * C + C for k in K) + HK * C + 1 + 1
    if len(w) != expect:
        fail("weights count %d, expected %d" % (len(w), expect))

    pos = 0

    def take(count):
        nonlocal pos
        r = w[pos:pos + count]
        pos += count
        return r

    payload = []
    payload += take(C)                                   # rechannel (C x 1), order: i in C
    for k in K:
        # Conv1D C -> C, read order: for i (output): for j (input): for tap
        raw = take(C * C * k)
        conv = [0.0] * (k * C * C)
        idx = 0
        for i in range(C):
            for j in range(C):
                for t in range(k):
                    conv[t * C * C + j * C + i] = raw[idx]
                    idx += 1
        payload += conv
        payload += take(C)                               # convB
        payload += take(C)                               # mixin (1 -> C), no bias
        raw = take(C * C)                                # layer1x1: for i (output): for j (input)
        l1 = [0.0] * (C * C)
        idx = 0
        for i in range(C):
            for j in range(C):
                l1[j * C + i] = raw[idx]
                idx += 1
        payload += l1
        payload += take(C)                               # l1b
    raw = take(HK * C)                                   # head: for j (channel): for tap
    head = [0.0] * (HK * C)
    idx = 0
    for j in range(C):
        for t in range(HK):
            head[t * C + j] = raw[idx]
            idx += 1
    payload += head
    payload += take(1)                                   # headB
    head_scale = take(1)[0]
    assert pos == len(w)

    def num(x):
        return float(x) if x is not None else float("nan")

    name = str(md.get("name") or path.split("/")[-1].rsplit(".", 1)[0]).encode("utf-8", "replace")[:63]
    name = name + b"\0" * (64 - len(name))                  # model name, 64 bytes with terminator
    hdr = b"ADN1" + struct.pack("<5I", 1, C, n, HK, 0)
    hdr += struct.pack("<6f", LEAKY, float(d.get("sample_rate", 48000.0)), head_scale,
                       num(md.get("loudness")), num(md.get("input_level_dbu")), num(md.get("output_level_dbu")))
    hdr += name
    hdr += struct.pack("<%dI" % n, *K) + struct.pack("<%dI" % n, *D)
    body = hdr + struct.pack("<%df" % len(payload), *payload)
    body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    open(out, "wb").write(body)
    print("%s -> %s: channels %d, layers %d, head %d, weights %d, %d bytes, head_scale %.6f, loudness %s, input %s dBu, output %s dBu" % (
        path.split("/")[-1], out.split("/")[-1], C, n, HK, len(w), len(body), head_scale,
        md.get("loudness"), md.get("input_level_dbu"), md.get("output_level_dbu")))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".adn"
    convert(src, dst)
