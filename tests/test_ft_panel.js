// test_ft_panel.js - node test of the page's "Functions" panel (ftFill, ftRta, ftTuner, ftMeters) with a fake DOM
// uso: node test_ft_panel.js control_page.h
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
const a = src.indexOf("const FTDEF=["), b = src.indexOf("\n];", a) + 3;
const ftdef = src.slice(a, b);
const code = [line(/^const esc=/), line(/^const el=/), line(/^function btn\(/), fnSrc("switchEl"), ftdef, line(/^const ftSend=/), fnSrc("ftRta"), fnSrc("ftTuner"), fnSrc("ftMeters"), fnSrc("ftFill")].join("\n");

class N {
  constructor(t) { this.tag = t; this.children = []; this._html = ""; this.dataset = {}; this.style = {}; this.className = ""; this.classList = { add() {} }; this.checked = false; }
  appendChild(c) { this.children.push(c); return c; }
  set innerHTML(v) { this._html = v; if (v === "") this.children = []; }
  get innerHTML() { return this._html; }
  text() { return (this._html || "") + " " + this.children.map(c => (c instanceof N ? c.text() : String(c.t))).join(" "); }
  find(pred, out = []) { if (pred(this)) out.push(this); this.children.forEach(c => c instanceof N && c.find(pred, out)); return out; }
}
const doc = { createElement: t => new N(t), createTextNode: t => ({ t }), activeElement: null, getElementById: () => null };
const sent = [];
const run = FTx => {
  sent.length = 0;
  const f = new Function("document", "FT", "g", "build", "upd", "ftOn", "ftStop", "ftLoad", "recent", "alert", "$", code + ";let ftOn_=0;const c=el('div','card');ftFill(c);return c;");
  return f(doc, FTx, o => { sent.push(o); return Promise.resolve({ ok: 1 }); }, () => {}, () => {}, true, () => {}, async () => {}, () => false, () => {}, () => null);
};
const base = { on: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], names: ["hum", "rumble", "limit", "mono", "air", "sat", "click", "rta", "tuner", "meter"], mains: 50, bpm: 100, clickdb: -24, airdb: 4, satdrive: 30, ceil: -1, tmute: 1,
  gr: 0, lufs: -120, peak: -120, crest: 0, corr: 0, tun: null, rta: Array(24).fill(-100) };
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
let c;
try { c = run(base); } catch (e) { console.log("ECCEZIONE:", e.stack); process.exit(1); }
let t = c.text();
["Hum Killer", "Rumble Filter", "Master Limiter", "Mono Check", "Air Band", "Console Glue", "Metronome", "Spectrum", "Tuner", "Meters"].forEach(n => check(t.includes(n), "row " + n));
check(/all off at startup/.test(t), "warning: all off at startup");
const sws = c.find(n => n.tag == "input" && n.type == "checkbox" && n.dataset !== undefined && false);
check(c.children.filter(r => r.className == "ftrow").length == 10, "ten rows with a switch");
check(!/ftb/.test(t) && !/ftnote/.test(t) && !/Loudness \(momentanea\)/.test(t), "with everything off no display");
// switch: changes the state and sends the right operation
const row = c.children.filter(r => r.className == "ftrow")[8];		// tuner
const label = row.children[0];		// switchEl: label con input
const cb = label.children[0];
cb.checked = true; cb.onchange();
check(sent.length == 1 && sent[0].op == "feat" && sent[0].k == "tuner" && sent[0].v == 1, "the tuner switch sends op feat tuner 1");
sent.length = 0;
// numeric parameter: the metronome's bpm
const rowC = c.children.filter(r => r.className == "ftrow")[6];
const inp = rowC.find(n => n.tag == "input" && n.type == "number")[0];
inp.value = "140"; inp.onchange();
check(sent.length == 1 && sent[0].k == "bpm" && sent[0].v == 140, "metronome bpm sent");
// with spectrum, tuner and meters on
const on = { ...base, on: [0, 0, 1, 0, 0, 0, 0, 1, 1, 1], gr: 3.2,
  rta: Array.from({ length: 24 }, (_, k) => -60 + k), tun: { hz: 82.4, note: "E2", cents: -7 }, lufs: -18.3, peak: -6.1, crest: 12.4, corr: 0.82 };
const c2 = run(on); const t2 = c2.text();
check((c2.find(n => /ftb/.test(n._html || "")).length > 0) && (t2.match(/class="ftb"/g) || []).length >= 0, "spettro presente");
const rtaNode = c2.find(n => n.className == "ftrta")[0];
check(rtaNode && (rtaNode._html.match(/class="ftb"/g) || []).length == 24, "24 spectrum bars");
check(/E2/.test(t2) && /-7 cent/.test(t2) && /82.4 Hz/.test(t2) && /Flat/.test(t2), "tuner: note, cents, Hz and indication");
check(/-18.3 LUFS/.test(t2) && /-6.1 dBFS/.test(t2) && /12.4 dB/.test(t2) && /0.82/.test(t2), "meters");
check(/reduction 3.2 dB/.test(t2), "limiter reduction");
// tuner without signal and tuner in tune
const c3 = run({ ...on, tun: null }); check(/no signal recognized/.test(c3.text()), "tuner without signal");
const c4 = run({ ...on, tun: { hz: 110.0, note: "A2", cents: 2 } }); check(/In tune/.test(c4.text()), "tuner: in tune");
// panel without data
const c5 = run(null); check(/does not respond/.test(c5.text()), "panel without data");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
