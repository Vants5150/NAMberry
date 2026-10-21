import json, re, sys, urllib.request, urllib.parse

"""Installs the block presets listed below on a NAMberry board.

Usage:  python3 block_presets.py http://<board-address>          (dry run: prints what would be saved)
        python3 block_presets.py http://<board-address> apply    (saves them; existing presets with the same name are replaced)

The sources of every setting are in presets_fonti.txt; entries marked "(typical)" are starting points, not documented settings.
The firmware keeps at most 256 block presets (older firmware: 48; with it, install only the first part).
"""
import json, re, sys, urllib.request, urllib.parse

if len(sys.argv) < 2:
    sys.exit(__doc__)
BOARD = sys.argv[1].rstrip("/")
APPLY = len(sys.argv) > 2 and sys.argv[2] == "apply"
ONLY = sys.argv[3] if len(sys.argv) > 3 else ""      # optional text: only presets whose name contains it
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

# Additional presets: 4 per effect. Name tags: (typical) = my choice from how the control is described; others name their source.
MORE = [
 # ---- ADEL delay (stereo) and mono: slapback 70-150 ms with one repeat (forum, EHX Slap-Back 45/65/100 ms); The Edge dotted 1/8 360 ms and 1/4 480 ms at 125 bpm (Total Guitar), low feedback, mix about equal (Premier Guitar)
 ("Delay slapback 100ms (forum)", "adeldelay", dict(time=100, feedback=10, mix=25, mode=1)),
 ("Delay dotted 360ms (Edge)", "adeldelay", dict(time=360, feedback=25, mix=50, mode=0)),
 ("Delay quarter 480ms (Edge)", "adeldelay", dict(time=480, feedback=30, mix=30, mode=0)),
 ("Delay ping-pong (typical)", "adeldelay", dict(time=380, feedback=35, mix=30, pingpong=60, mode=0)),
 ("Mono slapback 100ms (forum)", "adeldelaym", dict(time=100, feedback=10, mix=25, mode=1)),
 ("Mono dotted 360ms (Edge)", "adeldelaym", dict(time=360, feedback=25, mix=50, mode=0)),
 ("Mono BBD 300ms (typical)", "adeldelaym", dict(time=300, feedback=40, mix=30, mode=2)),
 ("Mono tape 450ms (typical)", "adeldelaym", dict(time=450, feedback=35, mix=28, mode=1, wow=20)),
 # ---- reverbs: room under 1 s, plate 1.5-2.5 s with 20-40 ms pre-delay, hall 2-4 s, cathedral pre-delay 50 ms or more, guitar mix about 15 % (mixing guides)
 ("Reverb room under 1s (guides)", "adelverb", dict(shape=0, decay=0.8, predelay=5, mix=15)),
 ("Reverb plate 2s (guides)", "adelverb", dict(shape=1, decay=2.0, predelay=30, mix=18, diffusion=85)),
 ("Reverb hall 3s (guides)", "adelverb", dict(shape=2, decay=3.0, predelay=30, mix=20)),
 ("Reverb cathedral 4.5s (guides)", "adelverb", dict(shape=3, decay=4.5, predelay=55, mix=15)),
 ("Mono reverb room (guides)", "adelverbm", dict(shape=0, decay=0.8, predelay=5, mix=15)),
 ("Mono reverb plate (guides)", "adelverbm", dict(shape=1, decay=2.0, predelay=30, mix=18, diffusion=85)),
 ("Mono reverb hall (guides)", "adelverbm", dict(shape=2, decay=3.0, predelay=30, mix=20)),
 ("Mono reverb cathedral (guides)", "adelverbm", dict(shape=3, decay=4.5, predelay=55, mix=15)),
 # ---- spring
 ("Spring small tank (typical)", "adelspring", dict(tank=0, springs=0, decay=1.2, mix=25)),
 ("Spring classic (typical)", "adelspring", dict(tank=1, springs=2, decay=2.2, mix=30)),
 ("Spring long surf (typical)", "adelspring", dict(tank=2, springs=2, decay=3.5, drip=0.8, mix=35, wobble=0.3)),
 ("Spring subtle (typical)", "adelspring", dict(tank=1, springs=1, decay=1.5, drip=0.4, mix=15)),
 # ---- chorus models
 ("Chorus CE-1 warm (typical)", "adelchorus", dict(model=0, rate=0.8, depth=60, mix=50)),
 ("Chorus vibrato wet (typical)", "adelchorus", dict(model=1, rate=5, depth=50, wetonly=1)),
 ("Chorus CE-2 light (typical)", "adelchorus", dict(model=2, rate=0.8, depth=55, mix=50)),
 ("Chorus CH-1 Super (typical)", "adelchorus", dict(model=3, rate=1.0, depth=60, spread=180, mix=50)),
 # ---- harmonizer
 ("Shifter octave up (typical)", "adelshifter", dict(v1on=1, v1mode=1, v1semi=12, v1level=-3, v2on=0, v3on=0)),
 ("Shifter fifth+octave (typical)", "adelshifter", dict(v1on=1, v1mode=1, v1semi=7, v1level=-4, v2on=1, v2mode=1, v2semi=12, v2level=-5, v3on=0)),
 ("Shifter diatonic 3rd (typical)", "adelshifter", dict(key=0, scale=0, v1on=1, v1mode=0, v1steps=2, v1level=-3, v2on=0, v3on=0)),
 ("Shifter octave down (typical)", "adelshifter", dict(v1on=1, v1mode=1, v1semi=-12, v1level=0, v1pan=0, v2on=0, v3on=0)),
 # ---- compressor: start ratio 3:1, attack 3-5 ms, release about 50 ms (guides); Fractal Audio forum named settings (that unit's scale)
 ("Comp clean 3:1 (guides)", "adelcomp", dict(type=0, threshold=-20, ratio=3, attack=5, release=50)),
 ("Comp country snappy (Fractal)", "adelcomp", dict(type=2, threshold=-22, ratio=6, attack=1, release=25)),
 ("Comp chunky (Fractal forum)", "adelcomp", dict(type=0, threshold=-22, ratio=5, attack=11, release=102)),
 ("Comp sustain opto (typical)", "adelcomp", dict(type=1, threshold=-28, ratio=10, attack=30, release=600)),
 # ---- equalizer: high-pass 80-120 Hz, low-pass 8-10 kHz, mud 300-500 Hz, fizz 5-6.5 kHz (Violet Recording, Fractal forum)
 ("EQ clean-up 100Hz-8k (guides)", "adelequal", dict(lowcut=100, highcut=8000)),
 ("EQ de-mud 400Hz (guides)", "adelequal", dict(lowcut=80, highcut=9000, f1=400, g1=-3, q1=1.2)),
 ("EQ de-fizz 5.5k (guides)", "adelequal", dict(lowcut=80, highcut=7000, f3=5500, g3=-3, q3=1.0)),
 ("EQ mid push 800Hz (typical)", "adelequal", dict(lowcut=80, f2=800, g2=3, q2=0.8, level=-1)),
 # ---- phase tool
 ("Phase invert polarity (tool)", "adelphase", dict(invert=1)),
 ("Phase delay 0.5ms (tool)", "adelphase", dict(delay=0.5)),
 ("Phase all-pass 180 (tool)", "adelphase", dict(apstages=2, apfreq=300)),
 # ---- probe signals
 ("Probe sweep (tool)", "adelsendsweep", dict(signal=0)),
 ("Probe guitar notes (tool)", "adelsendsweep", dict(signal=1)),
 ("Probe chord (tool)", "adelsendsweep", dict(signal=2)),
 ("Probe pink noise (tool)", "adelsendsweep", dict(signal=3)),
 # ---- cabinet filter
 ("Cab tight 80Hz-6k (typical)", "cabir", dict(lowcut=80, highcut=6000)),
 ("Cab open 60Hz-9k (typical)", "cabir", dict(lowcut=60, highcut=9000)),
 ("Cab dark 100Hz-4.5k (typical)", "cabir", dict(lowcut=100, highcut=4500)),
 ("Cab full 50Hz-12k (typical)", "cabir", dict(lowcut=50, highcut=12000)),
 # ---- simple overdrive
 ("OD TS boost (typical)", "tsdrive", dict(drive=15, tone=60, level=4)),
 ("OD TS crunch (typical)", "tsdrive", dict(drive=45, tone=50, level=0)),
 ("OD TS lead (typical)", "tsdrive", dict(drive=75, tone=55, level=-2)),
 ("OD TS dark rhythm (typical)", "tsdrive", dict(drive=55, tone=30, level=0)),
 # ---- tremolo (Vibrolux-style: speed hair under 4 to 8, intensity 0-3 on the amp's dial, Moog forum; no Hz measured)
 ("Trem slow 3Hz (typical)", "tremolo", dict(rate=3, depth=45, shape=0)),
 ("Trem surf 7Hz (typical)", "tremolo", dict(rate=7, depth=70, shape=0)),
 ("Trem choppy square (typical)", "tremolo", dict(rate=5, depth=80, shape=2)),
 ("Trem subtle 5Hz (typical)", "tremolo", dict(rate=5, depth=25, shape=1)),
 ("Harm trem slow (typical)", "harmtrem", dict(rate=3, depth=55, xover=700)),
 ("Harm trem fast (typical)", "harmtrem", dict(rate=7, depth=70, xover=700)),
 ("Harm trem wide (typical)", "harmtrem", dict(rate=4, depth=60, xover=1200)),
 ("Harm trem deep (typical)", "harmtrem", dict(rate=4, depth=85, xover=500)),
 # ---- envelope filter: Mu-Tron III notes (Guitar World, Premier Guitar): low-pass is the funk choice, Up = classic quack, Down = swoop, Lo range for rhythm
 ("Auto-wah funk up (Mu-Tron)", "autowah", dict(sens=65, lo=350, hi=2200, q=4, dir=0, mix=100)),
 ("Auto-wah high Q (Mu-Tron)", "autowah", dict(sens=65, lo=350, hi=2200, q=8, dir=0, mix=100)),
 ("Auto-wah down swoop (Mu-Tron)", "autowah", dict(sens=60, lo=350, hi=2200, q=4, dir=1, mix=100)),
 ("Auto-wah subtle (typical)", "autowah", dict(sens=40, lo=300, hi=1800, q=3, dir=0, mix=60)),
 # ---- noise gate (behaviour from gate guides: threshold just above the noise floor, hold above zero avoids chatter)
 ("Gate high gain (typical)", "noisegate", dict(thr=-45, attack=1, release=80, range=80, hold=30)),
 ("Gate medium (typical)", "noisegate", dict(thr=-55, attack=1, release=120, range=60, hold=20)),
 ("Gate clean (typical)", "noisegate", dict(thr=-70, attack=2, release=150, range=40, hold=20)),
 ("Gate hard chug (typical)", "noisegate", dict(thr=-40, attack=0.5, release=40, range=90, hold=10)),
 # ---- rotary: Leslie chorale about 0.8 Hz (40-50 rpm), tremolo 5.7-6.8 Hz (340-400 rpm) (Wikipedia, diyAudio)
 ("Rotary chorale slow (Leslie)", "rotary", dict(speed=0, depth=70, mix=100)),
 ("Rotary tremolo fast (Leslie)", "rotary", dict(speed=1, depth=70, mix=100)),
 ("Rotary slow blend (typical)", "rotary", dict(speed=0, depth=50, mix=60)),
 ("Rotary fast blend (typical)", "rotary", dict(speed=1, depth=60, mix=70)),
 # ---- tape echo
 ("Echo slapback (forum)", "tapeecho", dict(time=100, repeats=10, tone=50, wow=20, sat=30, mix=25)),
 ("Echo dotted 360ms (Edge)", "tapeecho", dict(time=360, repeats=25, mix=45)),
 ("Echo quarter 480ms (Edge)", "tapeecho", dict(time=480, repeats=30, mix=30)),
 ("Echo ambient (typical)", "tapeecho", dict(time=600, repeats=55, tone=40, mix=35)),
 # ---- clean booster
 ("Boost clean 6dB (typical)", "booster", dict(gain=6)),
 ("Boost push 12dB (typical)", "booster", dict(gain=12)),
 ("Boost bright (typical)", "booster", dict(gain=9, lowcut=120, warm=10, bright=60)),
 ("Boost warm (typical)", "booster", dict(gain=8, warm=50, bright=10)),
 # ---- volume swell
 ("Swell slow 1.2s (typical)", "swell", dict(attack=1200, release=600)),
 ("Swell medium (typical)", "swell", dict(attack=600, release=400)),
 ("Swell fast 200ms (typical)", "swell", dict(attack=200, release=300)),
 ("Swell long release (typical)", "swell", dict(attack=800, release=1500)),
 # ---- stereo widener
 ("Widener 120% (typical)", "widener", dict(width=120, haas=0)),
 ("Widener Haas 10ms (typical)", "widener", dict(width=100, haas=10)),
 ("Widener narrow 60% (typical)", "widener", dict(width=60, haas=0)),
 ("Widener wide+Haas (typical)", "widener", dict(width=160, haas=15)),
 # ---- vibrato
 ("Vibrato slow 4Hz (typical)", "vibrato", dict(rate=4, depth=40, shape=0)),
 ("Vibrato 6Hz deep (typical)", "vibrato", dict(rate=6, depth=70, shape=0)),
 ("Vibrato triangle (typical)", "vibrato", dict(rate=5, depth=50, shape=1)),
 ("Vibrato subtle (typical)", "vibrato", dict(rate=5.5, depth=20, shape=0)),
 # ---- other simplified drives
 ("BD-2 crunch (typical)", "bluesdrv", dict(gain=40, tone=50, level=-3)),
 ("BD-2 low-gain boost (typical)", "bluesdrv", dict(gain=15, tone=55, level=2)),
 ("BD-2 driven (typical)", "bluesdrv", dict(gain=70, tone=45, level=-4)),
 ("BD-2 bright (typical)", "bluesdrv", dict(gain=35, tone=70, level=-2)),
 ("FZ-1 mild (typical)", "fz1", dict(fuzz=35, tone=50, volume=-6)),
 ("FZ-1 full (typical)", "fz1", dict(fuzz=90, tone=50, volume=-8)),
 ("FZ-1 dark (typical)", "fz1", dict(fuzz=70, tone=25, volume=-6)),
 ("FZ-1 bright (typical)", "fz1", dict(fuzz=60, tone=75, volume=-8)),
 # ---- phaser, small
 ("Small Stone slow (typical)", "smallstone", dict(rate=0.4, color=0)),
 ("Small Stone fast (typical)", "smallstone", dict(rate=3, color=0)),
 ("Small Stone clr slow (typical)", "smallstone", dict(rate=0.5, color=1)),
 ("Small Stone clr fast (typical)", "smallstone", dict(rate=2.5, color=1)),
 # ---- auto-pan and slicer
 ("Autopan slow sine (typical)", "autopan", dict(rate=0.5, depth=70, wave=0)),
 ("Autopan 4Hz triangle (typical)", "autopan", dict(rate=4, depth=60, wave=1)),
 ("Autopan hard square (typical)", "autopan", dict(rate=2, depth=100, wave=2)),
 ("Autopan subtle (typical)", "autopan", dict(rate=1, depth=30, wave=0)),
 ("Slicer eighths (typical)", "slicer", dict(rate=2, pattern=0, depth=100, smooth=8)),
 ("Slicer gallop (typical)", "slicer", dict(rate=2, pattern=1, depth=100, smooth=8)),
 ("Slicer offbeat (typical)", "slicer", dict(rate=2, pattern=2, depth=100, smooth=8)),
 ("Slicer stutter (typical)", "slicer", dict(rate=3, pattern=3, depth=100, smooth=5)),
 # ---- woman tone (neck pickup, tone rolled off)
 ("Woman tone dark (typical)", "womantone", dict(tone=20, body=60, level=3)),
 ("Woman tone Clapton (typical)", "womantone", dict(tone=40, body=50, level=3)),
 ("Woman tone fat (typical)", "womantone", dict(tone=30, body=80, level=3)),
 ("Woman tone open (typical)", "womantone", dict(tone=60, body=40, level=2)),
 # ---- graphic EQ (mud 300-500 Hz, presence)
 ("GEQ mid scoop (typical)", "geq6", dict(g100=3, g200=0, g400=-4, g800=-4, g1600=0, g3200=3)),
 ("GEQ de-mud (guides)", "geq6", dict(g200=-2, g400=-4)),
 ("GEQ presence (typical)", "geq6", dict(g1600=2, g3200=4)),
 ("GEQ lead mid boost (typical)", "geq6", dict(g400=-2, g800=3, g1600=4)),
 # ---- limiter
 ("Limiter safety (typical)", "limiter", dict(thr=-3, release=150, ceiling=-0.5)),
 ("Limiter hard (typical)", "limiter", dict(thr=-12, release=80, ceiling=-1)),
 ("Limiter gentle (typical)", "limiter", dict(thr=-6, release=300, ceiling=-1)),
 ("Limiter peak catch (typical)", "limiter", dict(thr=-2, release=50, ceiling=-0.3)),
 # ---- Memory Man style analog delay and ducking delay
 ("Memory Man slap 90ms (typical)", "analogdelay", dict(time=90, repeats=15, mix=30, mod=20)),
 ("Memory Man ambient (typical)", "analogdelay", dict(time=550, repeats=55, mix=35, mod=40)),
 ("Memory Man 300ms (typical)", "analogdelay", dict(time=300, repeats=35, mix=35, mod=30)),
 ("Memory Man short mod (typical)", "analogdelay", dict(time=35, repeats=30, mix=45, mod=60)),
 ("Duck delay 380ms (typical)", "duckdelay", dict(time=380, repeats=40, duck=70, mix=40)),
 ("Duck dotted 360ms (Edge)", "duckdelay", dict(time=360, repeats=30, duck=60, mix=45)),
 ("Duck long 600ms (typical)", "duckdelay", dict(time=600, repeats=45, duck=75, mix=40)),
 ("Duck slap 110ms (typical)", "duckdelay", dict(time=110, repeats=15, duck=50, mix=35)),
 # ---- sustainer and selector
 ("Sustainer low (typical)", "sustainer", dict(sustain=25, level=-6, tone=50)),
 ("Sustainer mid (typical)", "sustainer", dict(sustain=50, level=-6, tone=50)),
 ("Sustainer max (typical)", "sustainer", dict(sustain=90, level=-9, tone=45)),
 ("Sustainer bright (typical)", "sustainer", dict(sustain=60, level=-6, tone=75)),
 ("Selector A of AB", "selector", dict(sel=0, outs=0)),
 ("Selector B of AB", "selector", dict(sel=1, outs=0)),
 ("Selector A of ABC", "selector", dict(sel=0, outs=1)),
 ("Selector C of ABC", "selector", dict(sel=2, outs=1)),
 # ---- more settings for the circuit-model pedals (to reach four each)
 ("Muff Triangle (typical)", "muff", dict(sustain=4, tone=5, volume=6, version=2)),
 ("Little Big Muff (typical)", "muff", dict(sustain=5, tone=5, volume=5, version=4)),
 ("Klon transparent (typical)", "centaur", dict(gain=3, treble=5, output=6)),
 ("Klon crunch (typical)", "centaur", dict(gain=6, treble=6, output=5)),
 ("Klon red LED boost (typical)", "centaur", dict(gain=2, treble=5, output=8, diodes=2)),
 ("Fuzz Face half (typical)", "fuzzface", dict(fuzz=6, volume=7)),
 ("Fuzz Face hot pair (typical)", "fuzzface", dict(fuzz=8, volume=5, transistors=1)),
 ("RAT LED (typical)", "ratts", dict(model=2, distortion=6, filter=5, volume=5)),
 ("RAT 2 OP07 (typical)", "ratts", dict(model=1, distortion=5, filter=4, volume=5)),
 ("RAT low gain (typical)", "ratts", dict(distortion=3, filter=3, volume=6)),
 ("OCD blues (typical)", "ocd", dict(drive=30, tone=50, volume=5)),
 ("OCD crunch (typical)", "ocd", dict(drive=60, tone=40, volume=4)),
 ("OC-2 synth oct1 (forum)", "octaver", dict(sub=90, dry=50, oct2=0)),
 ("Bender fat OC75 (typical)", "bender", dict(attack=6, level=5, version=1)),
 ("Bender cleanup (typical)", "bender", dict(attack=3, level=6, version=0)),
 ("Flanger slow jet (typical)", "flanger", dict(rate=2, depth=8, manual=5, fb=6)),
 ("Flanger fast subtle (typical)", "flanger", dict(rate=6, depth=4, manual=4, fb=3)),
 ("DS-1 all noon (Gearank test)", "ds1", dict(dist=5, tone=5, level=5)),
 ("DS-1 dark crunch (typical)", "ds1", dict(dist=6, tone=3, level=5)),
 ("DS-1 bright lead (typical)", "ds1", dict(dist=7, tone=7, level=4)),
 ("LA-2A limit mode (typical)", "opticalla", dict(pr=40, mode=1, gain=0)),
 ("LA-2A gentle (typical)", "opticalla", dict(pr=35, mode=0, gain=1)),
 ("Dyna Comp low (typical)", "dynacomp", dict(sens=3, output=6)),
 ("Dyna Comp funk (typical)", "dynacomp", dict(sens=7, output=6)),
 ("Dyna Comp sustain (typical)", "dynacomp", dict(sens=9, output=5)),
 ("Phase 90 Block mid (typical)", "phase90", dict(speed=5, feedback=2)),
 ("Phase 90 Block fast (typical)", "phase90", dict(speed=7, feedback=2)),
 ("CE-2 slow wide (typical)", "ce2", dict(rate=2, depth=7)),
 ("CE-2 fast shimmer (typical)", "ce2", dict(rate=7, depth=4)),
 ("CE-2 subtle (typical)", "ce2", dict(rate=3, depth=3)),
 ("Guv'nor bass-forward (typical)", "guvnor", dict(gain=8, bass=8, mids=4, treble=5, level=5)),
 ("Guv'nor crunch (typical)", "guvnor", dict(gain=4, bass=5, mids=6, treble=6, level=5)),
 ("Guv'nor lead (typical)", "guvnor", dict(gain=8, bass=5, mids=7, treble=6, level=6)),
 ("Twin Reverb clean (typical)", "tonestack", dict(amp=1, bass=4, mid=4, treble=6)),
 ("Bassman 5F6-A (typical)", "tonestack", dict(amp=0, bass=5, mid=5, treble=6)),
 ("JCM800 (typical)", "tonestack", dict(amp=4, bass=6, mid=7, treble=6)),
 ("Rangemaster 10nF (typical)", "rangemaster", dict(cap=1, range=8)),
 ("Rangemaster 47nF fat (typical)", "rangemaster", dict(cap=3, range=7)),
 ("Rangemaster hot (typical)", "rangemaster", dict(transistor=1, range=9)),
 ("Uni-Vibe throb (typical)", "univibe", dict(rate=3.5, intensity=90, mode=0, volume=10)),
 ("Uni-Vibe fast (typical)", "univibe", dict(rate=6, intensity=60, mode=0, volume=10)),
 ("Mode 2 (SDD-320)", "dimension", dict(mode=2)),
 ("Mode 2+4 (SDD-320)", "dimension", dict(mode=3)),
 ("AD9 doubling (typical)", "ad9", dict(delay=0.3, repeat=0.5, level=5)),
 ("AD9 long echo (typical)", "ad9", dict(delay=9, repeat=4, level=4)),
 ("Wah heel (typical)", "crybaby", dict(pos=0)),
 ("Wah toe (typical)", "crybaby", dict(pos=10)),
 ("Wah vocal mod (typical)", "crybaby", dict(pos=5, vocal=3)),
 ("Micro Flanger slow (typical)", "mflanger", dict(rate=1.5, regen=5)),
 ("Micro Flanger fast (typical)", "mflanger", dict(rate=6, regen=4)),
 ("Micro Flanger regen (typical)", "mflanger", dict(rate=3, regen=8)),
 ("CE-1 chorus (typical)", "ce1", dict(intensity=3, level=5)),
 ("CE-1 deep (typical)", "ce1", dict(intensity=7, level=5)),
 ("CE-1 slow wide (typical)", "ce1", dict(intensity=2, level=5, sweep=3.0)),
 ("CE-1 stereo out (typical)", "ce1", dict(intensity=4, level=5, output=1)),
 # ---- harmonizer presets made to try the pitch tracker (chromatic intervals follow the note you play; the last one is diatonic in Eb major, the key of a guitar tuned a half step down)
 ("Track 1 octave up", "adelshifter", dict(dry=-9, tracking=1, v1on=1, v1mode=1, v1semi=12, v1level=0, v1pan=0, v2on=0, v3on=0)),
 ("Track 2 octave down", "adelshifter", dict(dry=-9, tracking=1, v1on=1, v1mode=1, v1semi=-12, v1level=0, v1pan=0, v2on=0, v3on=0)),
 ("Track 3 fifth up", "adelshifter", dict(dry=-9, tracking=1, v1on=1, v1mode=1, v1semi=7, v1level=0, v1pan=0, v2on=0, v3on=0)),
 ("Track 4 fifth+octave up", "adelshifter", dict(dry=-9, tracking=1, v1on=1, v1mode=1, v1semi=7, v1level=-2, v1pan=-50, v2on=1, v2mode=1, v2semi=12, v2level=-2, v2pan=50, v3on=0)),
 ("Track 5 3rd+5th Eb major", "adelshifter", dict(dry=-6, tracking=1, key=3, scale=0, v1on=1, v1mode=0, v1steps=2, v1level=-2, v1pan=-50, v2on=1, v2mode=0, v2steps=4, v2level=-3, v2pan=50, v3on=0)),
]

P += MORE
assert len(P) <= 245, len(P)   # the board holds at most 256 and 11 more (the Tube Screamer, Big Muff, Klon and Fuzz Face settings of presets_fonti.txt) may already be saved
assert len({n for n, _, _ in P}) == len(P), 'duplicate preset names'
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
        if p.get('kind') == 3:      # measured outputs (meters) are not settings
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
        if ONLY and ONLY not in n:
            continue
        body = urllib.parse.urlencode({"t": tok, "op": "save", "n": n, "d": d}).encode()
        r = json.load(urllib.request.urlopen(urllib.request.Request(BOARD + "/bp", data=body), timeout=10))
        if not r.get("ok"):
            bad += 1
            print("FAIL", n, r)
    print("saved", len(out) - bad, "failed", bad)
