// test_idle.js - node test of the automatic pause of the page's updates (no requests without activity for 60 s)
// uso: node test_idle.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
const a = src.indexOf("let lastAct=Date.now();");
const b = src.indexOf("async function mtick()");
if (a < 0 || b < 0) throw new Error("block not found");
const block = src.slice(a, b);
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }

let now = 1000000;
const handlers = {};
const sub = { dataset: { idle: "1" } };
const win = { addEventListener: (ev, fn) => { handlers[ev] = fn; } };
const doc = { getElementById: id => id == "sub" ? sub : null };
const realNow = Date.now;
Date.now = () => now;
const api = new Function("window", "document", block + ";return {idleNow, touch:()=>lastAct};")(win, doc);
check(!api.idleNow(), "just opened: not paused");
now += 59000;
check(!api.idleNow(), "after 59 s without activity: not yet paused");
now += 2000;
check(api.idleNow(), "after 61 s without activity: paused");
handlers["pointerdown"]();
check(!api.idleNow() && !sub.dataset.idle, "one touch: resumes and removes the pause label");
now += 61000;
check(api.idleNow(), "inactive again: paused");
["pointerdown", "keydown", "wheel", "touchstart", "mousemove"].forEach(e => {
  now += 61000;
  handlers[e]();
  if (api.idleNow()) { console.log("FAILED: event", e, "does not resume"); fails++; }
});
check(true, "all five events resume");
Date.now = realNow;

// needM: /m only if something shows it
const ln = src.split("\n").find(l => l.startsWith("const needM="));
if (!ln) throw new Error("needM not found");
let sweep = false, nobm = false;
const mk = (MT, FXO, SWL) => new Function("MT", "FXO", "SWL", "document", ln + ";return needM;")(MT, FXO, SWL, { querySelector: () => (sweep ? {} : null), body: { classList: { contains: c => c == "nobm" && nobm } } });
const bar = (on, kind) => ({ kind: kind || "i", bars: [{ m: { isConnected: on } }] });
check(!mk([], [], [])(), "nothing in view: no /m request");
check(mk([bar(true)], [], [])(), "a level bar in view: /m needed");
check(!mk([bar(false)], [], [])(), "bar no longer on the page: not needed");
check(mk([], [{ val: { isConnected: true } }], [])(), "measured value of an effect in view: needed");
check(!mk([], [{ val: { isConnected: false } }], [])(), "measured value no longer in view: not needed");
check(mk([], [], [{ isConnected: true }])(), "indicator in view: needed");
sweep = true;
check(mk([], [], [])(), "Sweep block in the grid: needed (playback indicator on the tile)");
sweep = false;
check(mk([bar(true, "n")], [], [])(), "indicator of a block in view: /m needed");
nobm = true;
check(!mk([bar(true, "n")], [], [])(), "indicators of hidden blocks: /m not needed");
check(mk([bar(true, "n"), bar(true, "i")], [], [])(), "indicators of hidden blocks but IN/OUT in view: /m needed");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
