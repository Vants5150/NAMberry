// test_probe.js - node test of phProbe: the phase measurement remembered in the browser is valid only if its blocks still exist in the current preset
// uso: node test_probe.js control_page.h
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

const store = {};
const lsGet = (k, d) => (k in store ? store[k] : d);
const lsSet = (k, v) => { store[k] = v; };
let cells = [], S = { pc: "aaa" };
const netCells = () => cells;
const phProbe = new Function("lsGet", "lsSet", "netCells", "S", fnSrc("phProbe") + ";return phProbe;")(lsGet, lsSet, netCells, S);
const cell = (id, t, fx) => ({ b: [{ id, t, fx }] });

cells = [cell(0, "nam"), cell(1, "nam"), cell(2, "vol")];
store.nb_probe = JSON.stringify({ S: 7, P: 8, inLinks: [], mergeLinks: [] });
check(phProbe() === null && store.nb_probe === "", "leftover from another preset (blocks 7 and 8 do not exist): discarded and deleted");

cells = [cell(0, "nam"), cell(7, "fx", "adelsendsweep"), cell(8, "fx", "adelphase")];
store.nb_probe = JSON.stringify({ S: 7, P: 8, pc: "aaa", inLinks: [], mergeLinks: [] });
check(phProbe() && phProbe().S == 7, "probe and ADELphase present, same preset: the measurement is valid");

store.nb_probe = JSON.stringify({ S: 7, P: 8, pc: "other", inLinks: [], mergeLinks: [] });
check(phProbe() === null, "same number of blocks but different preset: discarded");

cells = [cell(0, "nam"), cell(7, "fx", "adelsendsweep")];
store.nb_probe = JSON.stringify({ S: 7, P: -1, pc: "aaa", inLinks: [], mergeLinks: [] });
check(phProbe() && phProbe().P == -1, "only the probe (P = -1): valid");

cells = [cell(0, "nam"), cell(7, "vol")];
store.nb_probe = JSON.stringify({ S: 7, P: -1, inLinks: [], mergeLinks: [] });
check(phProbe() === null, "at number 7 there is a block of another type: discarded (a user's block is not removed)");

store.nb_probe = "not json";
check(phProbe() === null, "unreadable value: no measurement");
store.nb_probe = "";
check(phProbe() === null, "empty: no measurement");

console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
