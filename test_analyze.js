// test_analyze.js - node test of the preset analysis (analyzeNet) of the page, with fake networks
// uso: node test_analyze.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf("function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
function lineStarting(re) {
  const m = src.split("\n").find(l => re.test(l));
  if (!m) throw new Error("line not found: " + re);
  return m;
}
const code = [
  lineStarting(/^const esc=/),
  lineStarting(/^const XFX=/),
  lineStarting(/^const lsGet=/), lineStarting(/^const lsSet=/), lineStarting(/^const anDest=/),
  lineStarting(/^const IOE=/),
  lineStarting(/^const isIo=/),
  lineStarting(/^const netCells=/),
  lineStarting(/^const netNode=/),
  lineStarting(/^const ndName=/),
  lineStarting(/^const nf=/),
  fnSrc("netAll"), fnSrc("netAnc"), fnSrc("phFind"), fnSrc("netMono"), fnSrc("netGainEstimate"), fnSrc("anLab"), fnSrc("analyzeNet"),
].join("\n");
const EQ0 = () => [0, 20, 20000, 100, 0, 250, 0, 1, 800, 0, 1, 3000, 0, 1, 5000, 0];
let S, fails = 0;
const env = new Function("S", "fxStereoOut", "fxMixIdx", "nmOf", "localStorage", "let anOn=false;" + code + ";return analyzeNet();");
function run(cells, links, dest) {
  const store = { nb_dest: dest || "amp" };
  const ls = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = v; } };
  S = { board: { mode: 2, net: { cells: cells.map(c => ({ r: c.r, c: c.c, b: [c.b] })), links, io: [[2, 0], [3, 0], [2, 9], [3, 9]] } } };
  return env(S, () => false, () => -1, b => b.t + b.id, ls);
}
const nam = (id, o) => Object.assign({ id, on: 1, t: "nam", in: 0, out: 0, file: "x", name: "NAM" + id, loaded: true }, o || {});
const fx = (id, name, v, o) => Object.assign({ id, on: 1, t: "fx", fx: name, v: v || [] }, o || {});
const has = (res, s) => res.some(r => r.title.indexOf(s) >= 0 || r.why.indexOf(s) >= 0 || r.fix.indexOf(s) >= 0);
function check(c, msg) { if (!c) { console.log("FAILED:", msg); fails++; } else console.log("ok      ", msg); }
// A: NAM with input +8, no cabinet
let r = run([{ r: 2, c: 2, b: nam(0, { in: 8 }) }], [[-1, 0, 1, 0, 0], [0, -2, 0, 1, 0], [0, -2, 0, 2, 0]], "mix");
check(has(r, "no cabinet"), "A: reports the lack of a cabinet or equalizer after the NAM");
check(has(r, "very high input"), "A: reports that the NAM input is too high");
// B: TS -> NAM -> EQ with everything open and a boost at 350 Hz and at 3.5 kHz
const eq = EQ0(); eq[6] = 4; eq[12] = 5; eq[15] = 7; eq[4] = 7; eq[3] = 120;
r = run([{ r: 2, c: 1, b: fx(0, "tsdrive", [40, 50, 0]) }, { r: 2, c: 3, b: nam(1, { in: 4 }) }, { r: 2, c: 5, b: fx(2, "adelequal", eq) }],
        [[-1, 0, 1, 0, 0], [0, 1, 0, 0, 0], [1, 2, 0, 0, 0], [2, -2, 0, 1, 0], [2, -2, 0, 2, 0]], "mix");
check(has(r, "Low Cut off"), "B: Low Cut off");
check(has(r, "High Cut open"), "B: High Cut open after a distorted sound");
check(has(r, "200-500 Hz zone"), "B: boost in the 200-500 Hz zone");
check(has(r, "between 2.5 and 5 kHz"), "B: boost aspro 2,5-5 kHz");
check(has(r, "bass shelf"), "B: strong bass shelf");
check(has(r, "treble shelf"), "B: strong treble shelf after distortion");
check(has(r, "Noise Gate"), "B: recommends the noise gate with high gain");
// C: chain in order
const eqOk = EQ0(); eqOk[1] = 90; eqOk[2] = 7000;
r = run([{ r: 2, c: 1, b: fx(0, "noisegate", [-55, 1, 120, 60, 20]) }, { r: 2, c: 3, b: nam(1) }, { r: 2, c: 5, b: fx(2, "adelequal", eqOk) }],
        [[-1, 0, 1, 0, 0], [0, 1, 0, 0, 0], [1, 2, 0, 0, 0], [2, -2, 0, 1, 0], [2, -2, 0, 2, 0]], "mix");
check(!r.some(x => x.sev != "info" && /Low Cut|High Cut|200-500|tra 2,5|shelf|cabinet|ingresso molto|uscita alta/.test(x.title)), "C: correct chain with no sound warnings (" + r.map(x => x.title).join(" | ") + ")");
// C2: with the Cab IR in order
r = run([{ r: 2, c: 3, b: nam(0) }, { r: 2, c: 5, b: fx(1, "cabir", [0, 100, 90, 7000]) }], [[-1, 0, 1, 0, 0], [0, 1, 0, 0, 0], [1, -2, 0, 1, 0], [1, -2, 0, 2, 0]], "mix");
check(!has(r, "no cabinet") && !r.some(x => /High Cut|Low Cut/.test(x.title)), "C2: NAM + Cab IR with correct cuts: no warning (" + r.map(x => x.title).join(" | ") + ")");
// C3: Cab IR con high cut aperto
r = run([{ r: 2, c: 3, b: nam(0) }, { r: 2, c: 5, b: fx(1, "cabir", [0, 100, 30, 14000]) }], [[-1, 0, 1, 0, 0], [0, 1, 0, 0, 0], [1, -2, 0, 1, 0], [1, -2, 0, 2, 0]], "mix");
check(has(r, "Low Cut a 30") && has(r, "High Cut a 14000"), "C3: Cab IR with open cuts reported");
// D: two NAMs in parallel -> message with the comb formula
r = run([{ r: 1, c: 2, b: nam(0) }, { r: 3, c: 2, b: nam(1) }, { r: 2, c: 5, b: { id: 2, on: 1, t: "vol", rt: 1, vol: 0 } }],
        [[-1, 0, 1, 0, 0], [-1, 1, 1, 0, 0], [0, 2, 0, 0, 0], [1, 2, 0, 0, 0], [2, -2, 0, 1, 0], [2, -2, 0, 2, 0]]);
check(has(r, "1/(2D)"), "D: parallel paths with the comb filter formula");
const dd = r.find(x => /Parallel paths/.test(x.title));
check(dd && dd.act && dd.act.kind === "phase" && /Measure the phase/.test(dd.fix) && !/insert an ADELphase/.test(dd.fix), "D: the warning offers \"Measure the phase\" (probe and ADELphase added automatically) instead of asking to do the measurement by hand");
// F: with a power amp and a cabinet after the output (default setting) no advice on cabinet, treble cut and equalization
r = run([{ r: 2, c: 2, b: nam(0, { in: 8 }) }], [[-1, 0, 1, 0, 0], [0, -2, 0, 1, 0], [0, -2, 0, 2, 0]]);
check(!has(r, "no cabinet") && has(r, "very high input"), "F: with a power amp and a cabinet the amp without an IR is not reported, but the too-high input is");
const eq2 = EQ0(); eq2[6] = 4; eq2[15] = 7;
r = run([{ r: 2, c: 1, b: fx(0, "tsdrive", [40, 50, 0]) }, { r: 2, c: 3, b: nam(1, { in: 4 }) }, { r: 2, c: 5, b: fx(2, "adelequal", eq2) }],
        [[-1, 0, 1, 0, 0], [0, 1, 0, 0, 0], [1, 2, 0, 0, 0], [2, -2, 0, 1, 0], [2, -2, 0, 2, 0]]);
check(!has(r, "Low Cut off") && !has(r, "High Cut open") && !has(r, "treble shelf") && has(r, "Noise Gate"), " F: with a power amp and a cabinet no EQ advice, but the noise gate stays");
// E: empty grid and disconnected block
r = run([], []);
check(has(r, "empty"), "E: griglia vuota");
r = run([{ r: 2, c: 3, b: nam(0) }], []);
check(has(r, "No input connected") && has(r, "receives no signal"), "E: inputs/outputs and disconnected block");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
