// test_midi_panel.js - node test of the page's MIDI panel (miFill, miParams...) with a fake DOM
// uso: node test_midi_panel.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf("function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
function line(re) { const m = src.split("\n").find(l => re.test(l)); if (!m) throw new Error("line not found: " + re); return m; }
const code = [
  line(/^const esc=/), line(/^const el=/), line(/^function btn\(/), line(/^const IOE=/), line(/^const isIo=/), line(/^const netCells=/), line(/^const MCLS=/),
  line(/^const lsGet=/), line(/^const lsSet=/), line(/^const miBlk=/), line(/^const miName=/), line(/^const miSend=/),
  fnSrc("miParams"), fnSrc("miFill"), fnSrc("anLab"),
].join("\n");

// DOM finto
class N {
  constructor(t) { this.tag = t; this.children = []; this._html = ""; this.dataset = {}; this.style = {}; this.className = ""; this.classList = { add() {} }; }
  appendChild(c) { this.children.push(c); return c; }
  set innerHTML(v) { this._html = v; if (v === "") this.children = []; }
  get innerHTML() { return this._html; }
  text() { return (this._html || "") + this.children.map(c => (c instanceof N ? c.text() : String(c.t))).join(" "); }
  contains() { return false; }
}
const doc = { createElement: t => new N(t), createTextNode: t => ({ t }), activeElement: null, getElementById: () => null };
const sent = [];
const run = (M, S, sel) => {
  sent.length = 0;
  const f = new Function("document", "S", "g", "fxd", "nmOf", "sel", "alert", "miLoad", "MID", "let MIDx=null;" + code + ";const c=el('div','card');miFill(c);return c;");
  return f(doc, S, o => { sent.push(o); return Promise.resolve({ ok: 1 }); }, k => ({ p: [{ k: "time", l: "Time", kind: 0 }, { k: "mix", l: "Mix", kind: 0 }, { k: "out", l: "Out", kind: 3 }] }), b => b.t + b.id, sel, () => {}, async () => {}, M);
};
const S = { board: { mode: 2, net: { cells: [
  { r: 2, c: 3, b: [{ id: 0, on: 1, t: "nam", in: 0, out: 0, name: "Amp", loaded: true }] },
  { r: 2, c: 6, b: [{ id: 1, on: 1, t: "fx", fx: "adelverb", v: [0, 0] }] }], links: [], io: [] } } };
const M = { chan: 0, echo: 1, bpm: 120, clk: 1, usb: 1, rx: 12, tx: 5, drop: 0, lc: 0, lid: 0,
  cls: Array.from({ length: 16 }, (_, i) => [i + 1, i == 2 ? 0 : (i == 4 ? 1 : -1), 1]),
  bcc: [[50, 1]], ctl: [{ cc: 40, l: [[0, 1, 0, 10, 90]] }, { cc: 0, l: [] }, { cc: 0, l: [] }],
  mon: [{ s: 2, d: "o", t: "CC 3 = 127 (can. 1)" }, { s: 1, d: "i", t: "CC 3 = 127 (can. 1)" }] };
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
let c;
try { c = run(M, S, 1); } catch (e) { console.log("ECCEZIONE:", e.stack); process.exit(1); }
const t = c.text();
check(/Fixed CCs/.test(t), "header of the fixed CCs");
check(/Drive/.test(t) && /Reverb/.test(t) && /Amp \(NAM 1\)/.test(t), "list of the functions");
check(/controller connected/.test(t) && /120.0 bpm/.test(t), "USB state and tempo");
check(/CC 50/.test(t) && /Controller 1/.test(t) && /Controller 3 \(global\)/.test(t), "block CCs and controllers");
check(/CC 3 = 127/.test(t), "message monitor");
// Flow: no network, the panel must not break
let c2;
try { c2 = run({ ...M, cls: M.cls.map(q => [q[0], -1, 0]), bcc: [], ctl: [{ cc: 0, l: [] }, { cc: 0, l: [] }, { cc: 0, l: [] }], mon: [] }, { board: { mode: 0, net: null } }, null); } catch (e) { console.log("ECCEZIONE Flow:", e.stack); process.exit(1); }
check(/only in ADELGrid/.test(c2.text()) && /No messages/.test(c2.text()), "panel in ADELFlow without errors");
// panel with the section 'the pedalboard does not respond'
const c3 = run(null, S, null);
check(/does not respond/.test(c3.text()), "panel without data");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
