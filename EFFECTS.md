# Effects included in NAMberry

Every effect is a block you can place on the pedalboard grid. They fall into five groups. **Please read the description of each effect**: "circuit model" means the pedal's schematic was turned into equations; it does NOT mean the result has been checked against recordings of the real pedal unless the text says so. The header of each source file (`fx_*.cpp`) lists every source and every assumption in full.

Cost figures and exact ranges can be read from the web page; controls below are generated from the firmware's own description (`/fx`).

## 1. Circuit models of real pedals and units

Built with the netlist engine in `ckt.h` (nodal analysis with the DK method and Newton iterations for the nonlinear parts) or, where noted, with numerical models of the parts that are not simple circuits.

### Tube Screamer (TS808 / TS9 / TS10 / TS9DX)

*Block name: ADELScreamerTS - Overdrive.*

Circuit model: input buffer, non-inverting clipping stage with diodes in the feedback, tone network, output buffer; each capacitor integrated with the trapezoidal rule and the clipping loop solved with Newton iterations. Sources: ElectroSmash TS808 analysis, PedalPCB tone-stack thread, m0xpd, model comparisons (Spicy Pedals, Mad Bean, Aion FX, Analog Man for the TS9DX modes). Small-signal response compared with nine TONE3000 captures of a real TS9: 0.42 dB rms. The +4.5 dB level offset of those captures was deliberately not copied. Assumed: diode parameters, Drive curve, op-amp swing.


| Control | Range | What it does |
|---|---|---|
| Model | choice | Pedal model: TS808, TS9, TS10 (minimal differences among them: output stage) or TS9DX with its Mode knob (TS9, +, Hot, Turbo: they change the diodes and the bass capacitor). |
| Drive | 0 to 10 | Like the pedal's knob (500k logarithmic potentiometer): more gain and more clipping, with the mid hump at about 720 Hz. |
| Tone | 0 to 10 | Like the pedal's knob: dark on the left (second-order low-pass), bright on the right (up to about +15 dB above 700 Hz-4 kHz). |
| Level | 0 to 10 | Like the pedal's knob (100k linear potentiometer): output level. |

### OD TS (simple overdrive block)

*Block name: ADEL OD TS - Overdrive.*

Simpler Tube Screamer-style block: a mid-hump drive curve plus tone and level, not a circuit model. The ScreamerTS block above is the circuit model.


| Control | Range | What it does |
|---|---|---|
| Drive | 0 to 100 % | How hard the clipping is pushed, with the mid hump typical of the Tube Screamer. |
| Tone | 0 to 100 % | Low-pass: 0 = dark, 100 = bright. |
| Level | -24 to 12 dB | Output level. |

### RAT (LM308 / OP07 / LED)

*Block name: ADELRatReal - Distortion.*

Circuit model: op-amp as an integrator inside the exact feedback network, slew-rate and rail limits, diode node with the Filter network solved per sample, optional 4x oversampling. Sources: ElectroSmash RAT analysis. Fitted to 14 TONE3000 captures of a RAT 2 (residual 1.49 dB rms): LM308 gain-bandwidth 0.252 MHz, minimum Distortion resistance 639 ohm, potentiometer curves. The OP07 variant is not fitted.


| Control | Range | What it does |
|---|---|---|
| Model | choice | Op-amp and diodes: LM308 with 1N914 diodes (original), OP07 (recent RAT 2, slightly narrower bandwidth) or LM308 with red LEDs. |
| Distortion | 0 to 10 | Like the pedal's knob (100K-A potentiometer in the feedback): gain from 1 to over 2000, but limited by the op-amp's bandwidth and the diodes. |
| Filter | 0 to 10 | Like the pedal's knob (100K-A potentiometer): passive low-pass filter, from about 32 kHz (fully left) to about 475 Hz (fully right): higher = darker. |
| Volume | 0 to 10 | Like the pedal's knob (100K-A potentiometer): output level. |
| Quality | choice | High: the circuit runs at 4 times the sampling rate (less aliasing than hard clipping, about 0.24 ms more delay and more CPU); Normal: normal rate, no delay. |

### Big Muff Pi (five versions)

*Block name: ADELBigMuffReal - Fuzz / sustainer.*

Netlist model of three historical versions (1972 Triangle, 1974 Ram's Head, 1976 V3) plus the 1975 schematic and the Little Big Muff. Sources: The Big Muff Pi Page (Kit Rae), ToneHome EH schematics, ElectroSmash. Transistor betas are typical values (the original parts have no data); real units vary a lot.


| Control | Range | What it does |
|---|---|---|
| Sustain | 0 to 10 | Like the pedal's knob (100k linear potentiometer): how much signal enters the two clipping stages, that is gain and sustain. |
| Tone | 0 to 10 | Like the pedal's knob (100k linear potentiometer between a low-pass and a high-pass): dark on the left, bright on the right, in the middle the mids are scooped at about 1 kHz. |
| Volume | 0 to 10 | Like the pedal's knob (100k linear potentiometer): output level. |
| Version | choice | Historical version of the circuit, each from its schematic: V3 Ram's Head 1976 (BC239), V2 Ram's Head 1974 (FS36999), V1 Triangle 1972 (PNP FS37000), EH schematic of 15 October 1975 (old model, 2N5088) and Little Big Muff (no Sustain: fixed gain). Component values, filters and tone change. |

### Fuzz Face (germanium)

*Block name: ADELFuzzFaceReal - Fuzz.*

Netlist model with two AC128 germanium PNP transistors (Ebers-Moll with the SPICE internal resistances). Sources: ElectroSmash Fuzz Face analysis. Early effect, leakage and beta drop are not modelled. The source can be ideal or an 8 kohm resistor imitating the pickup load (the Fuzz Face depends heavily on it).


| Control | Range | What it does |
|---|---|---|
| Fuzz | 0 to 10 | Like the pedal's knob (1k linear potentiometer as Q2's emitter resistor, with the 20 uF capacitor): on the left a lot of feedback and little gain, on the right the full gain; with the guitar's volume low it cleans up. |
| Volume | 0 to 10 | Like the pedal's knob (500k logarithmic potentiometer): output level. |
| Transistors | choice | Pair of germanium AC128s: Vintage = beta 85 and 120 (the ElectroSmash case); Hot = beta 105 and 170 (more compressed sound, extrapolated values). |
| Source | choice | The Fuzz Face has an input impedance of only 0.5-9 kohm and responds a lot to the guitar's: Buffered = ideal source; Guitar = 8 kohm series resistor that imitates the pickup load (inductance not modelled). |

### Rangemaster Treble Booster

*Block name: ADELRangemasterReal - Germanium treble booster.*

Netlist model with an OC44-type germanium transistor. Source: ElectroSmash Dallas Rangemaster analysis. Is and beta from the published operating points; not modelled: Early effect, leakage.


| Control | Range | What it does |
|---|---|---|
| Range | 0 to 10 | Like the pedal's knob (10k logarithmic potentiometer on the collector): how much signal comes out, from nothing to about 36 dB of gain at the treble. |
| Bias (R2) | 40 to 200 kohm | Resistor R2 that biases the base (68k in the original): it is the common modification to set the operating point with germanium transistors of different beta; the bias changes the clipping and the response. |
| Input cap | choice | Input capacitor C1: 5 nF is the original (it cuts below about 2.6 kHz); 10 and 15 nF and 47 and 100 nF are the popular modifications that widen the band towards the bass. |
| Transistor | choice | Germanium transistor: OC44 with beta 100 (ElectroSmash's value) or a hotter one with beta 150. |
| Source | choice | The Rangemaster has an input impedance of 10-12 kohm and its sound depends on the pickup load: Buffered = ideal source; Guitar = 8 kohm series resistor (pickup inductance not modelled). |

### Tone Bender MkII

*Block name: ADELToneBenderReal - Fuzz.*

Netlist model of the three-transistor germanium circuit, with the two documented versions (OC81D and OC75). Sources: Guitar PCB schematic, voltages measured on original units (PedalPCB thread). Bias varies greatly between real units.


| Control | Range | What it does |
|---|---|---|
| Attack | 0 to 10 | Like the pedal's knob (1k linear potentiometer on Q3's emitter with a 4.7 uF capacitor): higher = more gain and sustain. |
| Level | 0 to 10 | Like the pedal's knob (100k logarithmic potentiometer): output level. |
| Version | choice | The two documented versions of the MkII: with OC81D (late units, 100k on Q1's base and on Q2's collector) or with OC75 (10k and 47k); the germanium transistors have strong leakage and betas calibrated on the voltages measured on original units. |
| Q3 load | 1 to 40 kohm | Load resistance of Q3's collector (8.2k in the original; modern ones use a 20k trimmer): it changes the bias, the level and the response; 7 V on the collector is the common target. |
| Source | choice | The pedal is sensitive to the guitar's impedance: Buffered = ideal source; Guitar = 8 kohm series resistor (pickup inductance not modelled). |

### Fuzz Factory

*Block name: ADELFuzzFactoryReal - Fuzz with gate and oscillation.*

Netlist model of a published schematic (2019, author not indicated) with five controls (Gate, Comp, Drive, Stab, Volume) and two AC128 pair types. The directions of the potentiometers are assumptions (the schematic does not say).


| Control | Range | What it does |
|---|---|---|
| Gate | 0 to 10 | 10k (linear) potentiometer RV4 in series with R5 on Q2's collector: clockwise (assumed) more resistance, Q3 more off: weak notes are cut and the sound spits. |
| Comp | 0 to 10 | 10k (linear) RV3 as the resistance between the supply and Q3's emitter: clockwise (assumed) less resistance. |
| Drive | 0 to 10 | 10k (linear) RV2: the wiper goes to C4 towards the supply and grounds Q3's emitter node for the signal; clockwise (assumed) closer to the node. |
| Stab | 0 to 10 | 5k (linear) RV1 in series with the transistors' supply: clockwise (assumed) 0 ohm and full voltage; towards the left the voltage drops, the circuit becomes unstable and noisy. |
| Volume | 0 to 10 | 5k (linear) RV5 after R6, as in the schematic. |
| Transistors | choice | Pair of germanium AC128s (Q2 and Q3): Vintage = beta 85 and 120 (the ElectroSmash case for the Fuzz Face); Hot = beta 105 and 170. |
| Source | choice | Buffered = ideal source; Guitar = 8 kohm series resistor that imitates the pickup load (inductance not modelled). |

### DS-1 (1994 circuit)

*Block name: ADELDS1Real - Distortion.*

Netlist model of the DS1-A circuit with an op-amp in place of the original TA7136AP: buffer, transistor booster, op-amp stage, diode clipper, tone/level network. Source: ElectroSmash DS-1 analysis. The 1978 TA7136AP version is not modelled.


| Control | Range | What it does |
|---|---|---|
| Dist | 0 to 10 | Like the pedal's knob (100kB linear potentiometer in the op-amp's feedback): op-amp gain from 1 to 22 (26.5 dB) after the 35 dB transistor booster; the diodes to ground limit the signal to about +-0.6 V. |
| Tone | 0 to 10 | Like the pedal's knob (20kB linear potentiometer between a low-pass and a high-pass): dark on the left, bright on the right, in the middle the mids are scooped at about 650 Hz. |
| Level | 0 to 10 | Like the pedal's knob (100kB linear potentiometer): output level. |

### Guv'nor

*Block name: ADELGuvnorReal - Distortion.*

Netlist model with two op-amps, LED clipping (or 1N4148 / germanium alternatives) and the passive Bass/Mids/Treble stack. Source: ElectroSmash Marshall Guv'nor analysis. Potentiometers assumed linear.


| Control | Range | What it does |
|---|---|---|
| Gain | 0 to 10 | Like the pedal's knob (100k potentiometer in the feedback of the first TL072): gain from 1 to 46 (33 dB), followed by a second 36 dB stage and the LEDs: up to about 70 dB in all. |
| Bass | 0 to 10 | Like the pedal's knob (10k potentiometer of the passive tone stack). |
| Mids | 0 to 10 | Like the pedal's knob (10k potentiometer of the passive tone stack). |
| Treble | 0 to 10 | Like the pedal's knob (10k potentiometer of the passive tone stack). |
| Level | 0 to 10 | Like the pedal's knob (100k potentiometer): output level. |
| Clipping | choice | Clipping diodes to ground: red LEDs like the original (clipping at about 1.8 V, more headroom and volume), 1N4148 silicon (0.7 V) or germanium (0.3 V), the alternatives described by ElectroSmash. |

### Klon Centaur

*Block name: ADELKlonReal - Overdrive.*

Netlist model: buffer, two-section gain pot with the frequency-dependent network, germanium clipping, summer with the dry path, active treble shelf, output. Source: ElectroSmash Klon analysis. Diode choices: 1N34A, 1N4148 or red LED.


| Control | Range | What it does |
|---|---|---|
| Gain | 0 to 10 | Like the pedal's knob (100k dual potentiometer): the gain of the op-amp stage rises (up to about 40 dB) and the parallel network falls; at zero only the filtered clean signal passes. |
| Treble | 0 to 10 | Like the pedal's knob (10k potentiometer in the active shelving): above about 400 Hz it cuts down to -8 dB or boosts up to +18 dB. |
| Output | 0 to 10 | Like the pedal's knob (10k potentiometer): output level. |
| Clipping | choice | Clipping diodes to ground: 1N34A germanium like the original (Vf 0.35 V), or 1N4148 silicon or red LEDs, the popular modifications. |

### OCD (v2)

*Block name: ADELOCDReal - Overdrive.*

Three small netlist circuits per channel (input op-amp, MOSFET + germanium clipping stage, second op-amp and output). Source: Giampiccolo et al., DAFx26, Figure 2 and Tables 1-2. Op-amp type and potentiometer tapers assumed.


| Control | Range | What it does |
|---|---|---|
| Drive | 0 to 100 % | Drive potentiometer (R5, 0-500k in the feedback of the first op-amp: gain from about 19 to 47 dB). |
| Tone | 0 to 100 % | Tone potentiometer (R14 in series with C8): treble cut at 0, brighter towards 100. |
| Level | -24 to 12 dB | Output gain of the block, after the circuit. |
| Switch | choice | High-peak/low-peak switch of the output stage: R13 (22k) in parallel with R12 (closed, as in the published analysis) or not. |
| Volume | 0 to 10 | Volume potentiometer (R15, 500k). |

### Wah (Cry Baby GCB-95 / Vox V847)

*Block name: ADELWahReal - Wah.*

Netlist model with the 500 mH inductor, the input buffer option and the "vocal" and sweep-range modifications. Source: ElectroSmash GCB-95 and V847 analyses. The pedal-position law was calibrated on their published peaks (750 Hz at half travel).


| Control | Range | What it does |
|---|---|---|
| Pedal | 0 to 10 | Pedal position (suited to MIDI control): 0 = heel (peak at about 400 Hz), 5 = half (about 750 Hz), 10 = toe (about 1.4 kHz); the peak rises up to 18-22 dB. |
| Q | choice | The 33k resistor R7 sets the width of the peak: 39k, 68k or 100k make the sound more vocal ("vocal mod" modification). |
| Sweep | choice | Capacitor C2 shifts the whole sweep range: 100 nF lowers it (modification for bass players), 1 nF raises it. |
| Input buffer | choice | The Cry Baby has had an input buffer since 1991 (impedance 1 Mohm); the Vox V847 and the first Cry Babys do not: 69.5 kohm load on the guitar, less bass. |

### Dyna Comp (MXR 102)

*Block name: ADELDynaCompReal - Compressor.*

Netlist model: CA3080 OTA, transistor envelope detector, output stage, plus the documented attack/detector modifications. Source: ElectroSmash analysis, CA3080 datasheet. Potentiometer tapers assumed.


| Control | Range | What it does |
|---|---|---|
| Sensitivity | 0 to 10 | Like the pedal's knob (500k rheostat on the OTA's bias current, taper assumed logarithmic): clockwise more gain and more compression. |
| Output | 0 to 10 | Like the pedal's knob (50k potentiometer after the pedal, taper assumed logarithmic). |
| OTA output cap | choice | Documented modification: C5 from 1 nF to 470 or 100 pF or removed = more treble. |
| Detector | choice | Documented modification: disconnecting C7 the detector is half-wave (more ripple on the low notes). |
| Output cap | choice | Documented modification: C9 from 50 nF to 1 uF = more bass. |
| R13 | choice | Documented modification (ElectroSmash's "attack mod"): R13 from 150k to 10k. |

### Phase 90 (Script logo)

*Block name: ADELPhase90Real - Phaser.*

Four JFET all-pass stages as a netlist; the Schmitt-trigger LFO is computed numerically from its circuit equations, and its excursion on the JFET gates is the one MEASURED by Eichas et al. (DAFx-14). Sources: ElectroSmash, the DAFx-14 paper. The JFET resistance law is an approximation; the audio circuit is updated every 64 samples.


| Control | Range | What it does |
|---|---|---|
| Speed | 0 to 10 | Like the pedal's knob: speed of the Schmitt-trigger LFO (R36 from 470k to 10k with a geometric law, the minimum is not in the source): from about 0.13 to 6 Hz. |
| Bias | 0 to 10 | Bias trimmer (250k with R20 1M towards R22 3.9M): it shifts the duration and speed of the triangle; it does not change the excursion, which is the one measured by Eichas et al. |
| Feedback | choice | R28 24k from the output to the fourth stage: absent in the 1974 Script version, on the second stage in the Block versions; mid hump and more gain. |
| JFET cutoff | choice | Pinch-off voltage of the 2N5952 (from -1.3 to -3.5 V according to the datasheet; the gate measured by Eichas et al. goes from -2.8 to -1.5 V): it changes the excursion of the notches. Idss 6 mA assumed. |

### Uni-Vibe

*Block name: ADELUniVibeReal - Phase / chorus / vibrato.*

Grey-box model after Darabundit et al. (DAFx-19): four all-pass stages swept by one lamp and four LDRs, Chorus or Vibrato output; values from Table 1 of the paper and from Geofex. The lamp/LDR transfer and dynamics are assumed (the measured wavetables are not published) and documented in the file header.


| Control | Range | What it does |
|---|---|---|
| Speed | 0.1 to 7.6 Hz | Speed of the lamp's LFO: the foot pedal of the original goes from about 0 to 7.6 Hz. |
| Intensity | 0 to 100 % | LFO voltage across the lamp: how far the four LDRs, and so the notches, sweep (the original knob goes from 1 to 9 plus minimum and maximum). |
| Mode | choice | Chorus = last stage mixed evenly with the dry signal (moving notches); Vibrato = last stage only (pitch-like throb). |
| Volume | 0 to 10 | Output volume (the original knob). |

### Octaver (Boss OC-2)

*Block name: ADELOctaverReal - Octave down.*

Built from the factory schematic: tracking filter, peak detectors and comparators driving a latch, two divide-by-two flip-flops, JFET choppers, smoothing filters, three-level mixer. Like the original it can mistrack; a better tracker is planned as a separate option. Several small values are assumed (listed in the file header).


| Control | Range | What it does |
|---|---|---|
| Octave 1 | 0 to 100 % | Level of the octave below (OCT 1 potentiometer, 100k linear). |
| Direct | 0 to 100 % | Level of the original sound (DIRECT LEVEL potentiometer). |
| Level | -24 to 12 dB | Output gain of the block, after the circuit. |
| Octave 2 | 0 to 100 % | Level of the two octaves below (OCT 2 potentiometer). |

### Flanger (MXR M-117)

*Block name: ADELFlanger117Real - Flanger.*

Input pre-emphasis, limiter with REGEN feedback as a netlist; 1024-stage BBD as an interpolated delay; output filters and mixer; LFO and clock control from the circuit formulas. Source: MXR service schematic 117-4001 rev. 106, clock range from forum measurements on two units. Assumed: clock law (30 kHz to 960 kHz, exponential), wet/dry trim, BBD ideal. Regeneration may be weaker than the real pedal.


| Control | Range | What it does |
|---|---|---|
| Speed | 0 to 10 | SPEED potentiometer (R52 500k in the LFO integrator): 0.09 Hz at 0 to 9.4 Hz at 10; no effect with Width at 0. |
| Width | 0 to 10 | WIDTH potentiometer: how much of the LFO triangle reaches the clock (0 = only Manual). |
| Manual | 0 to 10 | MANUAL potentiometer: clock from 30 kHz (delay 17 ms) at 0 to 960 kHz (0.53 ms) at 10. |
| Regen | 0 to 10 | REGEN potentiometer: feedback of the delayed signal (0 = none). |

### Flanger Micro (MXR Micro Flanger)

*Block name: ADELFlangerMicroReal - Flanger.*

Netlist for the audio path, numerical LFO, current mirror, clock oscillator and 512-stage BBD. Source: MXR schematic 152-4001 rev. 105 (low-resolution scan). Assumed: supply, transistor betas, BBD polarity (parameter).


| Control | Range | What it does |
|---|---|---|
| Rate | 0 to 10 | Like the RATE knob (R32 500k linear, assumed): from the LFO circuit the cycle lasts from about 1.6 s (fully clockwise) to about 7.3 s (fully counterclockwise). |
| Regen | 0 to 10 | Like the REGEN knob (R9 500k): how much of the delayed signal returns to the inverting input of the input amplifier, through C16 5 nF which removes the bass. |
| BBD output polarity | choice | The SAD512D's polarity is not documented in the schematic: Normal = delayed in phase (the feedback is in antiphase), Inverted = delayed in antiphase (feedback in phase, comb with a null at frequency 0). |

### Chorus CE-1

*Block name: ADELChorusCE1Real - Chorus / vibrato.*

Chorus mode only: input amplifier, BBD filters and output network as a netlist; BBD delay and LFO numerical. Sources: Boss ET-10C service schematic and 1976 manual. The delay excursion is an estimate (Sweep parameter); Vibrato mode is not modelled.


| Control | Range | What it does |
|---|---|---|
| Chorus Intensity | 0 to 10 | Like the CE-1's knob in chorus mode: it sets the LFO's SPEED (from about 0.6 to 4.3 Hz, from the integrator's circuit) as the manual's note says. |
| Level | 0 to 10 | VR11 50k logarithmic before the input amplifier (gain 48). |
| Input sensitivity | choice | Input switch: Low = 80 mV against 10 mV, that is -18 dB (read from the manual's specifications). |
| Output | choice | Like the CE-1's two jacks: Mono sums dry and effect; Stereo puts the dry on the left and the effect on the right. |
| Sweep | 0 to 5.63 ms | Delay excursion in chorus (the short end stays at 2.56 ms). It is NOT a knob of the CE-1: the schematic does not allow working out how far the LFO moves the clock in chorus, and with the whole range of the legend (5.63 ms) the effect was too strong (+-142 cent at Intensity 10); 1.8 ms is the documented CE-2 excursion (ASSUMED). |

### Chorus CE-2

*Block name: ADELChorusCE2Real - Chorus.*

Netlist for the filters, numerical BBD (MN3007) and MN3101 clock, LFO from its circuit. Source: ElectroSmash CE-2 analysis, MN3101 datasheet. The real delay was not measured.


| Control | Range | What it does |
|---|---|---|
| Rate | 0 to 10 | Like the pedal's knob (100k linear, assumed): from the center downwards it reduces the square wave that drives the LFO's integrator; from about 0.3 Hz to 3.5 Hz (ElectroSmash formula). |
| Depth | 0 to 10 | Like the pedal's knob (100k linear, assumed): amplitude of the triangle that modulates the BBD clock and therefore the delay (about 2.8-4.8 ms at maximum; delay values from assumptions about the MN3101). |

### Dimension D (Roland SDD-320)

*Block name: ADELDimensionDReal - Chorus / ensemble.*

Reconstructed from the 1979 service note: four BBDs per channel, antiphase LFOs, the six real button combinations, Sallen-Key wet filter derived from the component values. The same-side wet shares are estimates and the NE570 compander and BBD distortion are not modelled. Values read from a low-quality scan.


| Control | Range | What it does |
|---|---|---|
| Mode | choice | Like the SDD-320's buttons: 1 = delay 8-12 ms, LFO 0.25 Hz; 2 = 5-10 ms, 0.25 Hz; 3 = 6-9 ms, 0.5 Hz; button 4 adds the wet part of the same side (on the original 4 alone equals 3+4). The six real combinations. |
| Input | choice | Like the original's STEREO/MONO switch: Mono sums the two inputs. |

### Analog Delay (Ibanez AD9)

*Block name: ADELAnalogDelayAD9Real - BBD analog delay.*

Netlist for the linear parts, numerical NE570/NE571 compander (datasheet equations), 4096-stage BBD at its clock; both documented component versions. Sources: ToneHome AD9 schematic and service manual. Two feedback calibration constants are assumed so that the loop reaches the self-oscillation limit as the manual says.


| Control | Range | What it does |
|---|---|---|
| Delay Time | 0 to 10 | Like the pedal's knob: the BBD clock goes from about 85 kHz (fully counterclockwise, about 24 ms) to 6.82 kHz (fully clockwise, about 300 ms): these are the two extremes of the service manual's adjustment; the law between the two is exponential (assumed). |
| Repeat | 0 to 10 | Like the REPEAT knob (100KB): how much of the delayed signal returns to the input. |
| Delay Level | 0 to 10 | Like the DELAY LEVEL knob (100KB): level of the delayed signal in the mixer. |
| Version | choice | The schematic says the AD9 exists in two versions: the values of the pre-emphasis, of the filters, of the mixer and of the compander capacitors (the ones in brackets) change. |
| Repeat trim (VR105) | 0.1 to 20 k | 20k trimmer VR105 in series with the feedback: in the manual it is adjusted so that the repeats last over 10 seconds at maximum Repeat. |

### Optical LA (LA-2A style)

*Block name: ADELOpticalLAReal - Optical compressor.*

BEHAVIOURAL model (not a circuit): 3-band detector, two-stage photocell with memory, fitted by Nelder-Mead on SignalTrain LA2A recordings of a real unit (0.5-0.6 dB error on music). Valid between Peak Reduction 65 and 80; outside is extrapolation. Tube and transformer distortion are not modelled.


| Control | Range | What it does |
|---|---|---|
| Peak Reduction | 0 to 100 | Like the pedal's knob: gain of the sidechain, that is threshold and amount of compression together (the threshold drops by about 0.4 dB per point). Measured on a real LA-2A between 65 and 80, outside that range it is an extrapolation. |
| Mode | choice | Compress: softer curve; Limit: higher ratio, like the LA-2A's switch. |
| Gain | -12 to 24 dB | Output level after the compression (it does not affect the compression, like the LA-2A's Gain knob). |
| Input | -24 to 24 dB | Input level before everything (the LA-2A does not have it: it is used to bring the guitar to the right level for the threshold). |
| Link | choice | On: the two channels compress together (two LA-2As linked in stereo); Off: each channel on its own. |
| Gain Reduction | 0 to 40 dB | Current gain reduction (with Link On the larger of the two channels, with Link Off that of the left channel). |
| Peak Reduction | 0 to 100 | Peak Reduction of the right channel (valid only with Link Off; with Link On the right channel follows the left one). |
| Mode | choice | Compress / Limit of the right channel (valid only with Link Off). |
| Gain | -12 to 24 dB | Gain of the right channel (valid only with Link Off). |
| Gain Reduction | 0 to 40 dB | Current gain reduction of the right channel. |

### Tone Stack (Fender / Marshall)

*Block name: ADELToneStackReal - Passive tone network.*

Netlist of the Bass/Mid/Treble network with the values of five amplifiers (Bassman 5F6-A, Twin Reverb, Princeton, JTM45, JCM800). Sources: Yeh and Smith DAFx-06, Faust tonestacks.lib values (Guitarix). The Level recovery gain and the source impedance are my choices.


| Control | Range | What it does |
|---|---|---|
| Bass | 0 to 10 | Bass potentiometer of the real circuit (exponential taper: at half travel the contribution is about 19%). |
| Mid | 0 to 10 | Mid potentiometer (linear). |
| Treble | 0 to 10 | Treble potentiometer (linear). |
| Amp | choice | Component values of the amplifier: the network is the same, resistors and capacitors change (source: Guitarix/faustlibraries library). |
| Source | choice | Impedance of what drives the network: ideal, or 38 kohm (12AX7 plate, estimate) as in the Fender stages; a higher impedance adds loss and moves the notch. |
| Level | -12 to 36 dB | Recovery of the gain lost by the passive network (15-20 dB), as the following stage would do. My choice: it is not part of the circuit. |

## 2. Engines from the ADEL plugin family

General-purpose DSP engines originally written as PiPedal plugins and ported unchanged.

### ADELphase

Stereo polarity / delay / all-pass alignment tool: measures correlation and delay between L and R, useful after an L/R split to align two microphones.


| Control | Range | What it does |
|---|---|---|
| Lane | choice | Mono version only, for the Mix split: put one instance per lane, one on Lane A and the other on Lane B. They exchange the signal and the Verdict appears on both. |
| stale | 0 to 1 | Red lamp: on = you changed something after the last measurement (Verdict Run sweep), run the sweep again. |
| Verdict | 0 to 8 | Outcome of the analysis: Waiting = no signal, OK = do not touch anything, Invert = invert the polarity, Delay A / Delay B = delay lane A (L) or B (R), Invert+Delay = both, Unrelated = uncorrelated lanes (the phase does not matter), Phase Rot = delay and polarity are fine but one band is still out of phase: use Phase Rotation as indicated in Set Stages and Set Freq (Lane mode only). In Lane mode it is measured after the correction: OK = you solved it. |
| Loss | -60 to 0 dB | How much level you lose by summing the lanes as they are compared with the aligned sum (0 = perfect, below -1 dB it is worth intervening). |
| Conf | 0 to 100 | Reliability of the measurement (%). Below 15 the suggestion is not updated. |
| Delay | -2 to 2 ms | Delay value to set (in Lane mode: on this lane) to align the two paths. Delay and inversion are always suggested on the same lane: the other ADELphase can be removed. |
| Invert | 0 to 1 | 1 = it is worth turning Invert Polarity on (in Lane mode: on this lane). |
| Stages | 0 to 4 | Phase Rotation stages to set on this lane (Verdict Phase Rot). In the other cases it shows the setting in force. |
| Freq | 0 to 24 | Phase Rotation frequency to set on this lane (All-pass Freq), rounded to the nearest value of the list. - = rotation off. In the other cases it shows the setting in force. |
| Invert Polarity | 0 to 1 | Inverts the polarity (180 degrees) of the R path (or of this lane). |
| Delay | -2 to 2 ms | Positive = delays R with respect to L; negative = delays L. In the mono version it delays this lane (positive values only). |
| Trim | -12 to 12 dB | Level of the R path (or of this lane): the cancellation also depends on the balance of the levels. |
| All-pass Stages | choice | Number of all-pass filters: each one rotates the phase by 90 degrees at the chosen frequency (0 = off). |
| All-pass Freq | 50 to 4000 Hz | Center frequency of the phase rotation. |
| In % | -100 to 100 | Correlation of the two inputs at zero delay (%): close to 100 they add up, close to -100 they cancel. |
| Out % | -100 to 100 | Correlation of the two paths after the correction (%). |
| Lag | -96 to 96 | By how many samples R (lane B) is delayed with respect to L (lane A). |
| Lat | 0 to 16 | Fixed latency in samples. |
| Output Mode | choice | Passthrough: L and R stay separate. Sum to Both: average of L and R on both outputs. Sum to Left: average on L (stereo only). |
| Auto Align | 0 to 1 | Delay and polarity follow the measurement automatically (stereo only). A signal is needed on both paths. |

### ADELverb / ADELverb mono

Reverb. Mono-lane version has a single output and no Width control.


| Control | Range | What it does |
|---|---|---|
| Mix | 0 to 100 % | Amount of reverb relative to the dry signal. |
| Mix Mode | choice | Keep Dry: the dry stays at full level and the reverb is added (the volume never drops). Crossfade: more Mix, less dry (like a wet/dry mixer). |
| Wet Only | 0 to 1 | Output with the reverb only (no dry signal): for a lane dedicated to the wet. In this mode Mix does not matter. |
| Output Level | -24 to 12 dB | Overall output level. |
| Pre-delay | 0 to 250 ms | Delay before the start of the reverb. |
| Decay | 0.2 to 30 s | Decay time (RT60) of the mid frequencies. |
| Size | 0.5 to 2 | Size of the room (tail and early reflections). |
| Room Shape | choice | Shape of the room: it changes the early reflections and the scale of the tail. |
| Distance | 0 to 100 % | Near or far source: fewer early reflections and a darker tail. |
| Late Delay | 0 to 100 ms | Delay between the early reflections and the start of the tail. |
| Early Reflections | 0 to 100 % | Level of the early reflections. |
| Diffusion | 0 to 100 % | Density of the reverb's attack. |
| Bass Multiply | 0.3 to 2 | Multiplier of the decay below the bass crossover. |
| Bass Crossover | 60 to 1000 Hz | Frequency that separates bass and mids in the decay. |
| Treble Decay | 0.1 to 1.5 | Multiplier of the decay above the treble crossover. |
| Treble Crossover | 800 to 10000 Hz | Frequency that separates mids and treble in the decay. |
| Mod Depth | 0 to 24 | Depth of the delay modulation (samples at 48 kHz). |
| Mod Rate | 0.05 to 3 Hz | Speed of the modulation. |
| Width | 0 to 100 % | Stereo width of the reverb. |
| Low Cut | 20 to 600 Hz | High-pass on the reverb (12 dB/octave). |
| High Cut | 1000 to 20000 Hz | Low-pass on the reverb (12 dB/octave). |
| EQ Low | -12 to 12 dB | Bass shelf at 250 Hz. |
| EQ Mid Freq | 200 to 6000 Hz | Frequency of the central peak. |
| EQ Mid | -12 to 12 dB | Gain of the center peak. |
| EQ High | -12 to 12 dB | High shelf at 4 kHz. |
| Spring | 0 to 100 % | Blends in the spring (with its 'boing') in place of the room tail. |
| Tuned Tail | 0 to 100 % | The tail resonates on the notes of a chord. |
| Tuned Root | choice | Root note of the chord. |
| Tuned Chord | choice | Chord type. |
| Bloom | 0 to 100 % | The reverb's input opens slowly after every note (swell). |
| Bloom Time | 50 to 3000 ms | Opening time of the bloom. |
| Gate | 0 to 100 % | Cuts the tail after the hold time (gated reverb). |
| Gate Hold | 50 to 1500 ms | Time the gate stays open after the attack. |
| Gate Release | 10 to 500 ms | Closing time of the gate. |
| Ducking | 0 to 100 % | Lowers the reverb while you play and lets it rise when you stop. |
| Duck Release | 50 to 3000 ms | Time over which the reverb rises. |
| Vintage | 0 to 100 % | Quantization in the feedback, like the digital converters of the 1980s. |
| Drive | 0 to 100 % | Soft saturation inside the feedback. |
| Freeze | 0 to 1 | Freezes the tail forever. |
| Morph | 0 to 100 % | Fades from your settings towards the chosen character. |
| Morph Target | choice | Character towards which the Morph fades. |

### ADELdelay / ADELdelay mono

Stereo delay (ping-pong, tap spread); the mono version uses one delay line.


| Control | Range | What it does |
|---|---|---|
| Mix | 0 to 100 % | Level of the repeats relative to the dry signal. |
| Wet Only | 0 to 1 | Output with the repeats only (no dry signal): for a lane dedicated to the wet. The level is set with Level. |
| Level | -24 to 12 dB | Output level of the repeats. |
| Feedback | 0 to 110 % | Amount of repeats. Above 100% the delay goes into self-oscillation (limited). |
| Time | 20 to 2000 ms | Base tempo of the delay (the duration of a quarter note). |
| Rhythm | choice | Rhythmic subdivision applied to the base tempo. |
| Glide | 10 to 2000 ms | Time over which the delay reaches the new tempo (tape effect). |
| Ping-Pong | 0 to 100 % | Bounce of the repeats between left and right. |
| R Time | 50 to 200 % | Time of the right channel relative to the left one (widens the stereo). |
| Tap 2 Level | 0 to 100 % | Level of the second head (extra rhythmic repeat). |
| Tap 2 Time | 10 to 100 % | Time of the second head relative to the base tempo. |
| Tap 3 Level | 0 to 100 % | Level of the third head. |
| Tap 3 Time | 10 to 100 % | Time of the third head relative to the base tempo. |
| Tap Spread | 0 to 100 % | Stereo spread of the extra heads. |
| Mode | choice | Character of the delay: clean digital, tape or analog BBD. |
| Character | 0 to 100 % | Intensity of the character (tape aging, BBD darkening). |
| FB Low Cut | 20 to 1000 Hz | High-pass in the feedback. |
| FB High Cut | 1000 to 20000 Hz | Low-pass in the feedback: the repeats get darker. |
| Saturation | 0 to 100 % | Soft saturation in the feedback. |
| Diffusion | 0 to 100 % | Fades the repeats into a diffuse atmosphere. |
| Reverse | 0 to 100 % | Repeats played in reverse. |
| Mod Depth | 0 to 10 ms | Depth of the time modulation (chorus). |
| Mod Rate | 0.1 to 8 Hz | Speed of the modulation. |
| Wow | 0 to 100 % | Slow tape wobble. |
| Flutter | 0 to 100 % | Fast tape flutter. |
| Trem Depth | 0 to 100 % | Rhythmic tremolo on the repeats, locked to the delay's tempo. |
| Trem Shape | choice | Tremolo waveform. |
| Trem Rate | choice | Tremolo speed relative to the delay's tempo. |
| Ducking | 0 to 100 % | Lowers the repeats while you play and lets them rise when you stop. |
| Duck Release | 50 to 2000 ms | Time over which the repeats rise. |
| Hold | 0 to 1 | Freezes the repeats in an infinite loop. |

### ADELspring

Spring reverb.


| Control | Range | What it does |
|---|---|---|
| Mix | 0 to 100 % | Amount of reverb. |
| Mix Mode | choice | Keep Dry: the dry always stays at full level and the reverb is added (the sound does not get quieter). Crossfade: dry/reverb crossfade. |
| Wet Only | 0 to 1 | Reverb only, without dry (for sends and returns). |
| Out Level | -24 to 12 dB | Output level. |
| Tank | choice | Length of the springs: Small = short, fast springs, Medium = classic tank, Long = long springs with a more spaced echo. Changing it resets the tail. |
| Springs | choice | Number of springs in parallel (1-3): more springs = denser, wider tail. Changing it resets the tail. |
| Decay | 0.3 to 8 s | Duration of the tail (decay time at about 1 kHz). |
| Drip | 0 to 1 | Strength of the 'drip' chirp typical of springs: 0 = almost smooth tail, 1 = pronounced boing. |
| Damp | 0 to 1 | Damping of the high frequencies in the tail: higher = darker tail. |
| Dwell | 0 to 1 | How hard the signal drives the springs: beyond a certain point it saturates gently and the chirp becomes more evident (like turning the Dwell knob of a real reverb). |
| Shake | 0 to 1 | Knock on the tank: turn it on to make the springs sound (the crash of the shaken reverb). |
| Low Cut | 40 to 600 Hz | High-pass on the reverb: removes the bass from the tail. |
| High Cut | 1500 to 9000 Hz | Low-pass on the reverb: real springs do not have much treble. |
| Wobble | 0 to 1 | Slight random wobble of the springs' length: it makes the tail less mechanical. |

### ADELchorus

BBD-style chorus/vibrato inspired by the Boss CE-1, CE-2 and CH-1 (a general effect, not a circuit copy; see the Chorus CE-1/CE-2 blocks for those).


| Control | Range | What it does |
|---|---|---|
| Model | choice | Character inspired by the Boss choruses: CE-1 Chorus (warm, mono), CE-1 Vibrato (modulated signal only), CE-2 (lighter, narrow band), CH-1 Super (two stereo voices, more open). |
| Rate | 0.05 to 12 Hz | LFO speed. |
| Depth | 0 to 100 % | How much the delay is modulated (depth of the chorus or vibrato). |
| Mix | 0 to 100 % | Blends dry and modulated signal (constant power). Ignored by CE-1 Vibrato and by Wet Only. |
| Level | -24 to 12 dB | Output level. |
| Wet Only | 0 to 1 | Output with the modulated signal only (no dry): for sends and returns or for the vibrato. |
| Delay | 1 to 25 ms | Center delay around which the modulation works: shorter = tighter effect, longer = wider. |
| LFO Shape | choice | LFO shape: the triangle is that of analog choruses, the sine is softer. |
| Stereo Phase | 0 to 180 ° | LFO phase offset between the two voices (CH-1 Super only): 180 degrees = one voice rises while the other falls. |
| Width | 0 to 100 % | Stereo width (CH-1 Super only): 0 = mono. |
| Regeneration | 0 to 60 % | Returns part of the modulated signal to the delay's input (color closer to the flanger). |
| Low Cut | 20 to 1000 Hz | High-pass on the modulated signal: removes the bass from the chorus. |
| High Cut | 1000 to 16000 Hz | Low-pass on the modulated signal (like the CH-1's Filter button). |
| Character | 0 to 100 % | BBD bandwidth and saturation: higher = darker and more 'analog'. |
| BBD Noise | 0 to 100 % | Background noise of the BBD (very light). |

### ADELshifter

3-voice harmonizer for single-note guitar (with pitch tracking).


| Control | Range | What it does |
|---|---|---|
| Dry | -60 to 6 dB | Level of the original signal (at -60 dB it turns off). |
| Voices Level | -24 to 12 dB | Overall level of the voices. |
| Key | choice | Key for the Diatonic harmonies. |
| Scale | choice | Scale for the Diatonic harmonies. |
| Glide | 1 to 400 ms | Time over which a voice glides from one note to the next. |
| Tracking | choice | Fast: it follows immediately; Smooth: more stable on dirty notes. |
| Gate | -80 to -20 dB | Below this level the pitch is not searched. |
| Lowest Note | 40 to 120 Hz | Lowest note played (82 Hz = the guitar's low E; go lower for 7 strings or drop tunings). |
| V1 On | 0 to 1 | Turns voice 1 on. |
| V1 Mode | choice | Diatonic: the interval follows the scale. Chromatic: fixed semitones. |
| V1 Steps | -14 to 14 | Steps of the scale above (+) or below (-) the played note: +2 = third, +4 = fifth, +7 = octave. |
| V1 Semitones | -24 to 24 semitone12TET | Fixed semitones (Chromatic mode). |
| V1 Fine | -50 to 50 cent | Fine tuning in cents (microdetune). |
| V1 Level | -36 to 6 dB | Level of the voice. |
| V1 Pan | -100 to 100 | Stereo position. |
| V1 Delay | 0 to 60 ms | Delay of the voice (doubling effect). |
| V2 On | 0 to 1 | Turns voice 2 on. |
| V2 Mode | choice | Diatonic: the interval follows the scale. Chromatic: fixed semitones. |
| V2 Steps | -14 to 14 | Steps of the scale above (+) or below (-) the played note: +2 = third, +4 = fifth, +7 = octave. |
| V2 Semitones | -24 to 24 semitone12TET | Fixed semitones (Chromatic mode). |
| V2 Fine | -50 to 50 cent | Fine tuning in cents (microdetune). |
| V2 Level | -36 to 6 dB | Level of the voice. |
| V2 Pan | -100 to 100 | Stereo position. |
| V2 Delay | 0 to 60 ms | Delay of the voice (doubling effect). |
| V3 On | 0 to 1 | Turns voice 3 on. |
| V3 Mode | choice | Diatonic: the interval follows the scale. Chromatic: fixed semitones. |
| V3 Steps | -14 to 14 | Steps of the scale above (+) or below (-) the played note: +2 = third, +4 = fifth, +7 = octave. |
| V3 Semitones | -24 to 24 semitone12TET | Fixed semitones (Chromatic mode). |
| V3 Fine | -50 to 50 cent | Fine tuning in cents (microdetune). |
| V3 Level | -36 to 6 dB | Level of the voice. |
| V3 Pan | -100 to 100 | Stereo position. |
| V3 Delay | 0 to 60 ms | Delay of the voice (doubling effect). |
| Whammy | -24 to 24 semitone12TET | Shifts the pitch of all the voices (and of the signal if Whammy Dry is on). Controllable from MIDI. |
| Whammy Dry | 0 to 1 | The Whammy also shifts the original signal, like a whammy pedal. |
| Drift | 0 to 40 cent | Slow, random variation of each voice's pitch (effect of several recorded takes). |
| Pitch | 0 to 2000 Hz | Detected frequency. |
| Note | 0 to 127 | Detected MIDI note (60 = middle C). |
| Tracking OK | 0 to 1 | 1 when the pitch has been recognized. |

### ADELComp

Compressor.


| Control | Range | What it does |
|---|---|---|
| Input | -24 to 24 dB | Gain before the compressor. |
| Output | -24 to 12 dB | Output level. |
| Mix | 0 to 100 % | Parallel compression: blends dry and compressed signal. |
| Type | choice | Character: clean VCA, slow and smooth Opto, fast and gritty FET, smooth Vari-Mu. |
| Threshold | -60 to 0 dB | Level above which the compression starts. |
| Ratio | 1 to 100 | Compression ratio. Above 20 it behaves as a limiter. |
| Attack | 0.02 to 200 ms | Attack time. |
| Release | 5 to 2000 ms | Release time. |
| Auto Release | 0 to 100 % | Program-dependent release: slower after a prolonged compression. |
| Knee | 0 to 24 dB | Softness of the knee. |
| Makeup | -12 to 36 dB | Make-up gain. |
| Auto Makeup | 0 to 1 | Automatic gain compensation. |
| Detector | choice | Peak, RMS or hybrid detection. |
| RMS Window | 1 to 100 ms | RMS detector window. |
| SC High-Pass | 20 to 500 Hz | High-pass on the signal that drives the compression (the bass does not make it pump). |
| SC Shelf | -12 to 12 dB | Shelf on the treble in the sidechain (positive = more sensitive to sibilants). |
| Lookahead | 0 to 5 ms | Look-ahead: delays the audio to stop the peaks (adds latency). |
| Saturation | 0 to 100 % | Soft saturation at the output (harmonics). |
| Stereo Link | 0 to 100 % | How much the two channels share the reduction (stereo version only). |
| M/S Mode | 0 to 1 | Compresses Mid and Side instead of left and right (stereo version only). |
| Gate Threshold | -90 to -20 dB | Below this level the signal is attenuated (expander). -90 = off. |
| Gate Ratio | 1 to 10 | Expansion ratio below the threshold. |
| Gate Range | 0 to 80 dB | Maximum attenuation of the gate. |
| Gate Release | 10 to 1000 ms | Time over which the gate closes again. |
| Gain Reduction | 0 to 40 dB | Current gain reduction. |
| In Level | -60 to 6 dB | Input peak level (after Input). |
| Out Level | -60 to 6 dB | Output peak level. |
| Latency | 0 to 512 | Latency introduced in samples (Lookahead). |

### ADELequal

Guitar equalizer: low and high cut, bass and treble shelves, three parametric bands.


| Control | Range | What it does |
|---|---|---|
| Level | -24 to 24 dB | Final gain. |
| Low Cut | 20 to 400 Hz | High-pass: removes the rumble. At 20 Hz it is bypassed. |
| High Cut | 2000 to 20000 Hz | Low-pass: removes the harshness. At 20 kHz it is bypassed. |
| Low Freq | 40 to 400 Hz | Frequency of the bass shelf. |
| Low Gain | -15 to 15 dB | Bass shelf: below the frequency the bass rises or falls. |
| Freq 1 | 100 to 1000 Hz | Band 1 (body). |
| Gain 1 | -15 to 15 dB | Gain of band 1. |
| Q 1 | 0.3 to 5 | Width of band 1: high Q = narrow band. |
| Freq 2 | 300 to 4000 Hz | Band 2 (mids). |
| Gain 2 | -15 to 15 dB | Gain of band 2. |
| Q 2 | 0.3 to 5 | Width of band 2. |
| Freq 3 | 1000 to 8000 Hz | Band 3 (presence). |
| Gain 3 | -15 to 15 dB | Gain of band 3. |
| Q 3 | 0.3 to 5 | Width of band 3. |
| High Freq | 2000 to 12000 Hz | Frequency of the treble shelf. |
| High Gain | -15 to 15 dB | Treble shelf: above the frequency the treble rises or falls. |

### ADELSendSweep

Measurement source: plays a sweep, notes, chord or pink noise at the press of a button, optionally muting the output.


| Control | Range | What it does |
|---|---|---|
| Play | 0 to 1 | Trigger the sample: set the switch to ON (to repeat it, set it back to OFF and then ON). |
| Signal | choice | Repeated logarithmic sweep 80 Hz-8 kHz (5 s, recommended for measuring), guitar notes (6 s), chord (5 s) or pink noise (3 s). |
| Level | -40 to 0 dB | Peak level of the sample. |
| Mute Input | 0 to 1 | Mutes the guitar while the sample plays. |
| Progress | 0 to 100 % | Sample progress (0 = stopped). |
| Repeat | 0 to 1 | With Play on, it relaunches the sample by itself as soon as it ends, so it plays continuously while you adjust the parameters. To stop it, set Play to OFF. |
| Mute Output | 0 to 1 | Mutes the board's outputs while the sample plays: the measurement still sees the signal inside the chain, but nothing is heard. Turn it off to listen to the sample. |

## 3. Cabinet and routing

### Cab IR

Cabinet impulse-response convolution (NEON), mono or stereo IR loaded from the SD card (.air format, WAV import from the page).


| Control | Range | What it does |
|---|---|---|
| Level | -24 to 24 dB | Gain after the convolution: IRs have very different levels. |
| Mix | 0 to 100 % | Amount of signal with the IR: 100% = cabinet only, less = it mixes with the direct signal. |
| Low Cut | 20 to 500 Hz | High-pass after the IR: removes the rumble. |
| High Cut | 2000 to 20000 Hz | Low-pass after the IR: removes the harshness in the treble. |

### Selector

Routing helper.


| Control | Range | What it does |
|---|---|---|
| Branch | choice | Active branch = the block's output A, B or C (A at the top on the right side). The others stay muted. Double click on the block in the grid = next branch. |
| Outputs | choice | How many outputs the selector has: AB = two outputs, ABC = three outputs. |

## 4. NAM block

Runs Neural Amp Modeler captures converted to the compact `.adn` format (`nam2adn.py`). Only the A2 WaveNet architecture is supported, always at full quality. Input is calibrated from the capture's metadata and the interface's measured maximum level. No model is shipped with the project.

## 5. Simplified or inspired blocks (not circuit models)

These blocks were written from general knowledge of the effect type, **without** a schematic. They are useful but make no claim of fidelity, and several are marked "ToTest" in the page: they are candidates to be rebuilt from real sources or removed.

* **ADELTremoloVibrolux** (`tremolo`)
* **ADELTremoloBrownface** (`harmtrem`)
* **ADELAutoWhaMuTron** (`autowah`)
* **ADELNoiseGateDecimator** (`noisegate`)
* **ADELEchoPlex** (`tapeecho`)
* **ADELVolumeSwell** (`swell`)
* **ADELStereoWithener** (`widener`)
* **ADELVibrato** (`vibrato`)
* **ADELAutoPan** (`autopan`)
* **ADELSlicer** (`slicer`)
* **ADELGraphicEQ6** (`geq6`)
* **ADELLimiter** (`limiter`)
* **ADELAnalogDelayMemoryMan** (`analogdelay`)
* **ADELDuckingDelay** (`duckdelay`)
* **ADELSustainer** (`sustainer`)
* **ToTest - Rotary Speaker (Leslie)** (`rotary`)
* **ToTest - Clean Booster (EP)** (`booster`)
* **ToTest - Blues Driver (Boss BD-2)** (`bluesdrv`)
* **ToTest - Fuzz (Maestro FZ-1)** (`fz1`)
* **ToTest - Small Stone (EHX)** (`smallstone`)
* **ToTest - Woman Tone (Clapton)** (`womantone`)

Some of the "Real" names above started as blocks of this kind and were rebuilt from schematics; the ones still in this list have not been.

## Honest limits

* No effect has been compared with recordings of the real pedal unless stated (Tube Screamer, RAT and the optical compressor are the ones fitted to real data).
* Sample rate is 48 kHz; most circuit models run without oversampling, so hard clipping can alias.
* Potentiometer tapers are assumed linear or log where the source does not say.
