"""Synthetic plucked-guitar test signals with a known pitch timeline (truth), written as 24-bit stereo 48 kHz WAV (left = guitar).
Each scenario returns (signal, truth) where truth[i] = f0 in Hz sounding at sample i (0 = silence)."""
import numpy as np, struct, os, sys
SR = 48000
rng = np.random.default_rng(7)

def partials_amp(f0):
    # measured shape of a low string: fundamental -4 dB, 2nd -5, 3rd 0 (strongest), 4th -1, 5th -12 ...; higher notes: weaker upper partials
    base = np.array([0.63, 0.56, 1.0, 0.89, 0.25, 0.05, 0.04, 0.03, 0.02, 0.015])
    if f0 > 200:
        base = np.array([1.0, 0.7, 0.35, 0.2, 0.1, 0.05, 0.03, 0.02, 0.01, 0.01])
    return base

def note(f0, dur, level=0.08, attack=True, tau=1.2, tail=0.05):
    n = int((dur + tail) * SR); t = np.arange(n) / SR
    x = np.zeros(n); a = partials_amp(f0)
    for k in range(1, len(a) + 1):
        if f0 * k > SR / 2.2: break
        dec = np.exp(-t / (tau / (1 + 0.6 * (k - 1))))
        x += a[k - 1] * dec * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 6.28) * (1 if attack else 0))
    x *= level / max(np.max(np.abs(x)), 1e-9)
    if attack:
        nz = rng.normal(size=int(0.008 * SR)) * level * 0.5 * np.exp(-np.arange(int(0.008 * SR)) / (0.003 * SR))
        x[:len(nz)] += nz
    env_off = np.ones(n); fade = int(tail * SR); env_off[-fade:] = np.linspace(1, 0, fade)
    return x * env_off

def freq(midi): return 440.0 * 2 ** ((midi - 69) / 12.0)

def place(sig, truth, x, f0, start, dur):
    i = int(start * SR); j = min(len(sig), i + len(x)); sig[i:j] += x[:j - i]
    k = min(len(truth), i + int(dur * SR)); truth[i:k] = f0

def total(sec): return np.zeros(int(sec * SR)), np.zeros(int(sec * SR))

MAJOR = [0, 2, 4, 5, 7, 9, 11]
def scale_notes(root_midi, octaves=2):
    out = []
    for o in range(octaves):
        for s in MAJOR: out.append(root_midi + 12 * o + s)
    out.append(root_midi + 12 * octaves)
    return out

def scen_run(rate, name, root=39, octaves=2, picked=True, tau=1.2):
    ns = scale_notes(root, octaves); ns = ns + ns[-2::-1]       # up and down
    dur = 1.0 / rate; sig, truth = total(1.0 + len(ns) * dur + 1.5)
    for i, m in enumerate(ns):
        f = freq(m); st = 0.5 + i * dur
        if picked or i == 0:
            place(sig, truth, note(f, dur * 0.97 + (1.2 if i == len(ns) - 1 else 0), tau=tau), f, st, dur * 0.97 + (0.0 if i < len(ns) - 1 else 1.0))
        else:
            # legato: same string keeps ringing, pitch changes without a new attack (hammer-on / pull-off): continuous phase, no noise burst
            x = note(f, dur, attack=False, tau=tau)
            place(sig, truth, x, f, st, dur)
    return sig + rng.normal(size=len(sig)) * 10 ** (-72 / 20), truth

def scen_bend():
    sig, truth = total(8.0)
    f0 = freq(55); t = np.arange(int(7 * SR)) / SR
    # glide up 2 semitones in 0.4 s, hold, glide back
    semis = np.interp(t, [0, 0.5, 0.9, 3.0, 3.4, 7.0], [0, 0, 2, 2, 0, 0]); inst = f0 * 2 ** (semis / 12)
    ph = 2 * np.pi * np.cumsum(inst) / SR; a = partials_amp(f0); x = np.zeros(len(t))
    for k in range(1, len(a) + 1):
        x += a[k - 1] * np.exp(-t / (1.8 / (1 + 0.5 * (k - 1)))) * np.sin(k * ph)
    x *= 0.08 / np.max(np.abs(x))
    sig[int(0.5 * SR):int(0.5 * SR) + len(x)] += x
    truth[int(0.5 * SR):int(0.5 * SR) + len(x)] = inst
    return sig + rng.normal(size=len(sig)) * 10 ** (-72 / 20), truth

def scen_palm():
    sig, truth = total(6.0)
    f = freq(40)
    for i in range(36):
        st = 0.5 + i * 0.125
        x = note(f, 0.1, tau=0.07, level=0.12)
        place(sig, truth, x, f, st, 0.09)
    return sig + rng.normal(size=len(sig)) * 10 ** (-72 / 20), truth

def scen_power():
    # power chords: root + fifth (+ octave): truth = the root note (what an octaver/harmonizer should follow)
    sig, truth = total(14.0); roots = [39, 41, 43, 44, 46, 41, 39]
    for i, m in enumerate(roots):
        st = 0.5 + i * 1.8; f = freq(m)
        x = note(f, 1.7, tau=1.0) + note(f * 1.4983, 1.7, tau=1.0) * 0.8 + note(f * 2, 1.7, tau=1.0) * 0.7
        place(sig, truth, x, f, st, 1.6)
    return sig + rng.normal(size=len(sig)) * 10 ** (-72 / 20), truth

def scen_high():
    return scen_run(5, "high", root=64, octaves=2, picked=True, tau=0.9)

SCEN = {
    "run6_picked": lambda: scen_run(6, "r6"),
    "run10_picked": lambda: scen_run(10, "r10"),
    "run6_legato": lambda: scen_run(6, "l6", picked=False),
    "bend": scen_bend,
    "palm_mute": scen_palm,
    "power_chords": scen_power,
    "high_notes": scen_high,
}

def write_wav(path, sig):
    sig = np.clip(sig, -1, 1)
    v = (sig * 8388607).astype(np.int32)
    out = np.zeros((len(v), 6), dtype=np.uint8)
    for b in range(3):
        out[:, b] = (v >> (8 * b)) & 0xFF
    data = out.tobytes()
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, SR, SR * 6, 6, 24) + b"data" + struct.pack("<I", len(data))
    open(path, "wb").write(hdr + data)

if __name__ == "__main__":
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    for name, fn in SCEN.items():
        sig, truth = fn()
        write_wav(os.path.join(out, name + ".wav"), sig)
        np.save(os.path.join(out, name + "_truth.npy"), truth)
        print(name, "%.1f s" % (len(sig) / SR), "peak %.1f dBFS" % (20 * np.log10(np.max(np.abs(sig)))))
