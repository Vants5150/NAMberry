// test_tap.js - node test of the page's tap tempo (tapTempo): series, average of the last five taps, pause, limits
// usage: node test_tap.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf("function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
const near = (a, b, e) => Math.abs(a - b) <= e;
const tapTempo = new Function(fnSrc("tapTempo") + ";return tapTempo;")();
let s = [], r;
r = tapTempo(s, 1000); s = r.taps; check(r.bpm == 0, "one tap: no tempo yet");
r = tapTempo(s, 1500); s = r.taps; check(near(r.bpm, 120, 1e-9), "two taps 500 ms apart: 120 bpm");
r = tapTempo(s, 2000); s = r.taps; check(near(r.bpm, 120, 1e-9), "three taps: still 120 bpm");
// the last five taps only: a slower beat replaces the old one
for (const t of [2600, 3200, 3800, 4400]) { r = tapTempo(s, t); s = r.taps; }
check(s.length == 5 && near(r.bpm, 100, 1e-9), "only the last five taps count (taps 600 ms apart: 100 bpm)");
// a pause over 2.5 s starts a new series
r = tapTempo(s, 4400 + 2501); s = r.taps; check(r.bpm == 0 && s.length == 1, "a pause over 2.5 s starts a new series");
r = tapTempo(s, 4400 + 2501 + 400); s = r.taps; check(near(r.bpm, 150, 1e-9), "the new series: 400 ms apart = 150 bpm");
// not a pause: 2.5 s exactly continues the series
r = tapTempo([0], 2500); check(near(r.bpm, 24, 1) === false && r.bpm == 30, "2.5 s apart continues the series and is limited to 30 bpm");
r = tapTempo([0], 100); check(r.bpm == 300, "100 ms apart is limited to 300 bpm");
check(tapTempo([], 5).taps.length == 1, "the first tap starts the series");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
