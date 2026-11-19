// test_dblclick.js - node test of the double click on a block tile (dblAction) and of the order of the "add block" menu (addOpts)
// usage: node test_dblclick.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf("function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
function lineSrc(prefix) {
  const i = src.indexOf(prefix);
  if (i < 0) throw new Error("line not found: " + prefix);
  return src.slice(i, src.indexOf("\n", i));
}
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
const dblAction = new Function(fnSrc("dblAction") + ";return dblAction;")();
// Selector with two outputs (v = [branch, outputs]: outputs 0 = AB, 1 = ABC)
let a = dblAction({ id: 7, fx: "selector" }, { v: [0, 0], on: 1 });
check(a.op == "set" && a.id == 7 && a.k == "sel" && a.v == 1, "Selector AB: A -> B");
a = dblAction({ id: 7, fx: "selector" }, { v: [1, 0], on: 1 });
check(a.k == "sel" && a.v == 0, "Selector AB: B -> A");
for (const [from, to] of [[0, 1], [1, 2], [2, 0]]) {
  a = dblAction({ id: 7, fx: "selector" }, { v: [from, 1], on: 1 });
  check(a.k == "sel" && a.v == to, "Selector ABC: " + "ABC"[from] + " -> " + "ABC"[to]);
}
a = dblAction({ id: 7, fx: "selector" }, { v: [1, 1], on: 0 });
check(a.k == "sel" && a.v == 2, "a Selector that is off still goes to the next output (it never switches on or off)");
a = dblAction({ id: 3, fx: "adelverb" }, { v: [], on: 1 });
check(a.k == "on" && a.v == 0, "other blocks: on -> off");
a = dblAction({ id: 3, fx: "adelverb" }, { v: [], on: 0 });
check(a.k == "on" && a.v == 1, "other blocks: off -> on");
a = dblAction({ id: 4, t: "nam" }, { v: [], on: 1 });
check(a.k == "on" && a.v == 0, "NAM block: on -> off");
// the Selector tile never looks switched off; other blocks do when they are off
const isOff = new Function(fnSrc("isOff") + ";return isOff;")();
check(isOff({ fx: "selector", on: 0 }) === false && isOff({ fx: "selector", on: 1 }) === false, "a Selector tile is never shown as off");
check(isOff({ fx: "adelverb", on: 0 }) === true && isOff({ fx: "adelverb", on: 1 }) === false, "other blocks: off is shown as off");
// the editor of a Selector has no on/off switch
const ed = src.slice(src.indexOf("function editor(info)"), src.indexOf("function editor(info)") + 3000);
check(/if\(n\.fx!="selector"\)\{const sw=switchEl/.test(ed), "the editor creates the on/off switch for every block but the Selector");
// add block menu: NAM, Volume, Routing, Pan, Selector first; the Selector is not repeated in the effects list
const FXD = [{ fx: "adelverb", title: "ADELVerb (Reverb)" }, { fx: "selector", title: "Selector" }, { fx: "adeldelay", title: "ADELDelay" }, { fx: "phaser", title: "Phaser (old)", hid: 1 }, { fx: "tsdrive", title: "ToTest - TS Drive" }];
const addOpts = new Function("FXD", fnSrc("fxSortKey") + ";" + lineSrc("const addOpts=") + ";return addOpts;")(FXD);
const o = addOpts(0);
check(o.slice(0, 5).map(x => x[1]).join("|") == "NAM|Volume|Routing (routing only)|Pan (stereo)|Selector", "the first five entries are NAM, Volume, Routing, Pan, Selector");
check(o[4][0] == "selector", "the Selector entry creates a selector block");
check(o.filter(x => x[0] == "selector").length == 1, "the Selector appears once");
check(o.slice(5).map(x => x[0]).join(",") == "adeldelay,adelverb,tsdrive", "effects follow in alphabetical order, ToTest last, hidden ones left out");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
