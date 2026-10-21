// test_phase_flow.js - node test of "Prepare the measurement", "Measure the phase", "Apply" and of the restore, with a simulated board (same link rules: only to the right or vertical, no cycles)
// uso: node test_phase_flow.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf((src.indexOf("async function " + name + "(") >= 0 ? "async function " : "function ") + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
const line = re => { const m = src.split("\n").find(l => re.test(l)); if (!m) throw new Error("line not found: " + re); return m; };
const code = [
  line(/^const IOE=/), line(/^const isIo=/), line(/^const nf=/), line(/^const netCells=/), line(/^const lkArgs=/), line(/^const colOf=/), line(/^const rowOfId=/), line(/^const lsGet=/), line(/^const lsSet=/),
  line(/^const PHV=/), line(/^const phSet=/), line(/^const phHz=/),
  fnSrc("netAll"), fnSrc("netAnc"), fnSrc("phFind"), fnSrc("phRead"), fnSrc("phHtml"), fnSrc("phMeasure"), fnSrc("phDelayDone"), fnSrc("phApply"), fnSrc("phPrepare"), fnSrc("phAddProbe"), fnSrc("phRestore"),
].join("\n");

const PH_KEYS = [["lane", 1], ["stale", 3], ["verdict", 3], ["loss", 3], ["conf", 3], ["sugdelay", 3], ["sugpol", 3], ["sugstages", 3], ["sugfreq", 3], ["invert", 2], ["delay", 0], ["trim", 0], ["apstages", 1], ["apfreq", 0], ["corrin", 3], ["corrout", 3], ["lag", 3], ["latency", 3], ["mode", 1], ["auto", 2]];

function makeBoard(cells, links) {
  const B = { cells: cells.map(c => ({ ...c, b: { ...c.b } })), links: links.map(l => l.slice()), io: [[2, 0], [3, 0], [2, 9], [3, 9]], next: 100, calls: [], failAt: -1, nlk: 0, sets: [] };
  B.state = () => ({ board: { mode: 2, net: { cells: B.cells.map(c => ({ r: c.r, c: c.c, b: [c.b] })), links: B.links.map(l => l.slice()), io: B.io } } });
  const col = (id, pf) => id == -1 ? B.io[pf == 2 ? 1 : 0][1] : (id == -2 ? B.io[pf == 2 ? 3 : 2][1] : (B.cells.find(c => c.b.id == id) || { c: -99 }).c);
  const reach = (a, b) => { const seen = new Set(), st = [a]; while (st.length) { const n = st.pop(); if (n == b) return true; if (seen.has(n)) continue; seen.add(n); B.links.forEach(l => { if (l[0] == n && l[1] >= 0) st.push(l[1]); }); } return false; };
  B.g = async o => {
    B.calls.push(o.op);
    if (o.op == "add") {
      const occ = B.cells.some(c => c.r == o.r && c.c == o.p) || B.io.some(q => q[0] == o.r && q[1] == o.p);
      if (occ || o.r < 0 || o.r > 5 || o.p < 0 || o.p > 9) return { ok: 0, err: "the cell is occupied" };
      const id = B.next++;
      B.cells.push({ r: o.r, c: o.p, b: { id, on: 1, t: "fx", fx: o.k, v: new Array(20).fill(0) } });
      return { ok: 1, id };
    }
    if (o.op == "del") {
      if (!B.cells.some(c => c.b.id == o.id)) return { ok: 0, err: "no such block" };
      B.cells = B.cells.filter(c => c.b.id != o.id);
      B.links = B.links.filter(l => l[0] != o.id && l[1] != o.id);
      return { ok: 1 };
    }
    if (o.op == "lk") {
      B.nlk++;
      if (B.failAt >= 0 && B.nlk == B.failAt) return { ok: 0, err: "link refused (test)" };
      const fr = +o.fr, to = +o.to;
      if (col(to, o.pt) < col(fr, o.pf)) return { ok: 0, err: "the link goes to the left" };
      if (fr >= 0 && to >= 0 && (fr == to || reach(to, fr))) return { ok: 0, err: "cycle" };
      const i = B.links.findIndex(l => l[0] == fr && l[1] == to && l[2] == +o.pf && l[3] == +o.pt);
      if (i >= 0) B.links[i][4] = +o.v; else B.links.push([fr, to, +o.pf, +o.pt, +o.v]);
      return { ok: 1 };
    }
    if (o.op == "ul") {
      const n = B.links.length;
      B.links = B.links.filter(l => !(l[0] == +o.fr && l[1] == +o.to && l[2] == +o.pf && l[3] == +o.pt));
      return n == B.links.length ? { ok: 0, err: "no such link" } : { ok: 1 };
    }
    if (o.op == "set") {
      B.sets.push([o.id, o.k, o.v]);
      const cell = B.cells.find(c => c.b.id == +o.id), ix = PH_KEYS.findIndex(q => q[0] == o.k);
      if (cell && cell.b.v && ix >= 0) cell.b.v[ix] = +o.v;		// the block keeps the value (like the board), so the page re-reads it
      return { ok: 1 };
    }
    return { ok: 0, err: "op?" };
  };
  return B;
}
const sortL = L => L.map(l => l.join(",")).sort().join("|");
const NAM = (id, name) => ({ id, on: 1, t: "nam", in: 0, out: 0, name, loaded: true });
const baseCells = () => [{ r: 1, c: 3, b: NAM(0, "A") }, { r: 5, c: 3, b: NAM(1, "B") }, { r: 3, c: 8, b: { id: 2, on: 1, t: "vol", rt: 1, vol: 0 } }];
const baseLinks = () => [[-1, 0, 1, 0, 0], [-1, 1, 1, 0, -3], [0, 2, 0, 0, 0], [1, 2, 0, 0, -1.5], [2, -2, 0, 1, 0], [2, -2, 0, 2, 0]];

function env(B, fetchSeq) {
  const store = {};
  const ls = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = v; } };
  const S = B.state();
  const refreshS = () => { Object.assign(S, B.state()); };
  const fxd = k => ({ p: PH_KEYS.map(([k2, kind]) => ({ k: k2, kind })) });
  let fi = 0;
  const fetchMock = async url => ({ json: async () => ({ f: (fetchSeq[Math.min(fi++, fetchSeq.length - 1)] || []) }) });
  const f = new Function("S", "g", "fxd", "fetch", "localStorage", "refreshS", "const sleep=()=>Promise.resolve();const stNow=async()=>{refreshS()};" + code + ";return {phPrepare,phAddProbe,phRestore,phFind,phMeasure,phApply,phHtml,netAnc,store:null};");
  const api = f(S, B.g, fxd, fetchMock, ls, refreshS);
  api.refresh = refreshS;
  api.store = store;
  return api;
}
let fails = 0;
const check = (c, m) => { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); };
const meas = (verdict, extra) => { const f = new Array(20).fill(0); f[2] = verdict; Object.assign(f, extra || {}); return f; };

(async () => {
  // 1. prepare and cancel
  let B = makeBoard(baseCells(), baseLinks());
  let api = env(B, []);
  const l0 = sortL(B.links), nc0 = B.cells.length;
  const info = await api.phPrepare(2, 0, 1);
  const S_ = B.cells.find(c => c.b.id == info.S), P_ = B.cells.find(c => c.b.id == info.P);
  check(S_ && S_.b.fx == "adelsendsweep" && P_ && P_.b.fx == "adelphase", "probe and ADELphase added");
  check(S_.c == 2 && P_.c == 7, "probe between the input and the NAMs (column " + S_.c + "), ADELphase between the NAMs and the sum (column " + P_.c + ")");
  const has = (f, t, pf, pt) => B.links.some(l => l[0] == f && l[1] == t && l[2] == pf && l[3] == pt);
  check(has(-1, info.S, 1, 0) && has(info.S, 0, 0, 0) && has(info.S, 1, 0, 0), "input -> probe -> the two NAMs");
  check(!has(-1, 0, 1, 0) && !has(-1, 1, 1, 0) && !has(0, 2, 0, 0) && !has(1, 2, 0, 0), "the direct links have been removed");
  check(has(0, info.P, 0, 1) && has(1, info.P, 0, 2) && has(info.P, 2, 0, 0), "NAM A to the L port, NAM B to the R port, ADELphase towards the sum");
  check(B.links.find(l => l[0] == 1 && l[1] == info.P)[4] == -1.5 && B.links.find(l => l[0] == info.S && l[1] == 1)[4] == -3, "the original levels are preserved");
  api.refresh();
  const f1 = api.phFind();
  check(f1.p && f1.s && f1.p.id == info.P && f1.s.id == info.S, "phFind finds the probe and ADELphase pair");
  check(JSON.parse(api.store.nb_probe).S == info.S, "the restore is remembered in the browser");
  await api.phRestore(info, false);
  check(sortL(B.links) == l0 && B.cells.length == nc0 && api.store.nb_probe === "", "on cancel, links and blocks return identical to before");

  // 2. remove the probe and keep ADELphase
  B = makeBoard(baseCells(), baseLinks()); api = env(B, []);
  const i2 = await api.phPrepare(2, 0, 1);
  await api.phRestore(i2, true);
  check(!B.cells.some(c => c.b.id == i2.S) && B.cells.some(c => c.b.id == i2.P), "probe removed, ADELphase stays");
  check(has(-1, 0, 1, 0) || B.links.some(l => l[0] == -1 && l[1] == 0 && l[2] == 1 && l[3] == 0), "probe removed, the links from the input come back");
  check(B.links.some(l => l[0] == 0 && l[1] == i2.P) && B.links.some(l => l[0] == i2.P && l[1] == 2), "ADELphase's links stay");

  // 3. error halfway: everything returns as it was
  B = makeBoard(baseCells(), baseLinks()); api = env(B, []);
  const l3 = sortL(B.links), c3 = B.cells.length;
  B.failAt = 5;
  let err = null;
  try { await api.phPrepare(2, 0, 1); } catch (e) { err = e; }
  check(err && /refused/.test(err.message), "a link refused halfway interrupts the preparation (" + (err && err.message) + ")");
  check(sortL(B.links) == l3 && B.cells.length == c3, "after the error the layout is identical to the previous one");

  // 4. no free cell
  const full = baseCells();
  for (let r = 0; r < 6; r++) for (let c = 1; c < 9; c++) if (!full.some(x => x.r == r && x.c == c)) full.push({ r, c, b: { id: 200 + r * 10 + c, on: 1, t: "vol", vol: 0 } });
  B = makeBoard(full, baseLinks()); api = env(B, []);
  err = null;
  try { await api.phPrepare(2, 0, 1); } catch (e) { err = e; }
  check(err && /free cell/.test(err.message) && sortL(B.links) == sortL(baseLinks()), "without free cells: clear error and nothing modified");

  // 5. two independent paths
  B = makeBoard(baseCells(), [[-1, 0, 1, 0, 0], [-1, 1, 2, 0, 0], [0, 2, 0, 0, 0], [1, 2, 0, 0, 0], [2, -2, 0, 1, 0]]); api = env(B, []);
  err = null;
  try { await api.phPrepare(2, 0, 1); } catch (e) { err = e; }
  check(err && /same input/.test(err.message), "paths starting from different inputs: no need to align");

  // 6. measurement and delay correction
  const W = (n, extra) => meas(0, Object.assign({ 1: 1 }, extra));
  B = makeBoard(baseCells(), baseLinks());
  api = env(B, [W(), W(), W(), W(), W(), meas(3, { 1: 0, 5: 0.52, 4: 91, 14: 30, 15: -20, 16: 25, 3: -6 }), meas(1, { 1: 0, 4: 95, 14: 80, 15: 90, 3: 0 })]);
  const i6 = await api.phPrepare(2, 0, 1); api.refresh();
  const prog = [];
  let r = await api.phMeasure(h => prog.push(h));
  check(Math.round(r.verdict) == 3 && Math.abs(r.sugdelay - 0.52) < 1e-6 && prog.length >= 5, "measure: verdict \"Delay branch B\" of 0.52 ms after a few seconds (" + prog.length + " letture)");
  const html = api.phHtml(r);
  check(/Delay branch B/.test(html) && /0\.52 ms/.test(html) && /91%/.test(html), "the result text is clear");
  const pl = B.sets.filter(s => s[1] == "play").map(s => s[2]);
  check(pl[0] == 0 && pl[1] == 1 && pl[pl.length - 1] == 0, "the probe starts and at the end is put back to Play OFF (" + pl.join(",") + ")");
  B.sets = [];
  let o = await api.phApply(() => {}, r);
  check(!B.sets.some(s => s[1] == "auto" && s[2] == 1), "apply: does not turn on Auto Align (the value would stay inside the engine)");
  check(B.sets.some(s => s[0] == i6.P && s[1] == "delay" && Math.abs(s[2] - 0.52) < 1e-9) && B.sets.some(s => s[0] == i6.P && s[1] == "invert" && s[2] == 0) && B.sets.some(s => s[0] == i6.P && s[1] == "auto" && s[2] == 0),
        "apply: writes Delay 0.52 ms and Invert Polarity 0 into ADELphase's knobs (Auto Align off)");
  check(o.steps.length == 1 && Math.round(o.final.verdict) == 1 && /Phase OK/.test(api.phHtml(o.final)), "after the correction the measurement says \"Phase OK\"");
  {
    // measuring again: the probe always sees the same delay at the input, but the knobs already have the suggested values -> "nothing to correct"
    const A2 = env(B, [W(), W(), W(), W(), W(), meas(3, { 1: 0, 5: 0.52, 4: 91, 14: 30, 15: 90, 3: -6 })]);
    A2.refresh();
    const r2 = await A2.phMeasure(() => {});
    check(Math.round(r2.verdict) == 1 && r2.done && /Nothing to correct/.test(A2.phHtml(r2)) && /Delay and Invert Polarity knobs/.test(A2.phHtml(r2)), "measuring again after the correction: \"Nothing to correct\", values already in ADELphase");
  }
  check(!B.sets.some(s => s[1] == "apstages"), "without a recommended rotation it does not touch the all-passes");
  {
    // second tap on "Apply the correction" with the old result (verdict 3): it rewrites nothing and says everything is fine
    B.sets = [];
    const o2 = await api.phApply(() => {}, r);
    check(B.sets.length == 0 && o2.applied.length == 0 && Math.round(o2.final.verdict) == 1 && o2.final.done, "second \"Apply\": no writes, everything fine");
  }

  // 7. rotation only: the delay is fine but the per-band phase is not (2 stages recommended at index 8 = 307 Hz)
  B = makeBoard(baseCells(), baseLinks());
  api = env(B, [W(), W(), W(), W(), W(), meas(1, { 1: 0, 4: 90, 14: 95, 15: 76, 18: 0, 6: 0, 7: 2, 8: 8 }), meas(1, { 1: 0, 4: 92, 14: 95, 15: 99, 7: 2, 8: 8 })]);
  const i7 = await api.phPrepare(2, 0, 1); api.refresh();
  r = await api.phMeasure(() => {});
  check(Math.round(r.sugstages) == 2 && Math.round(r.sugfreq) == 8, "measure: ADELphase recommends 2 stages at frequency index 8");
  check(/2 stages at 307 Hz/.test(api.phHtml(r)), "the text says \"2 stages at 307 Hz\" (" + (api.phHtml(r).match(/\d stages at \d+ Hz/) || ["?"])[0] + ")");
  B.sets = [];
  o = await api.phApply(() => {}, r);
  check(!B.sets.some(s => s[1] == "auto"), "with delay and polarity fine it does not turn on Auto Align");
  check(B.sets.some(s => s[0] == i7.P && s[1] == "apstages" && s[2] == 2) && B.sets.some(s => s[0] == i7.P && s[1] == "apfreq" && Math.abs(s[2] - 307) <= 1), "sets 2 all-pass stages at 307 Hz on ADELphase (" + JSON.stringify(B.sets.filter(s => /^ap/.test(s[1]))) + ")");
  check(o.steps.length == 1 && /After the phase rotation/.test(o.steps[0].tag) && Math.round(o.final.corrout) == 99, "and measures again: output correlation 99%");
  check(o.applied.length == 1 && /rotation/.test(o.applied[0]), "the summary lists only the rotation");

  // 8. delay and rotation together: first Auto Align, then the rotation, with a measurement after each step
  B = makeBoard(baseCells(), baseLinks());
  api = env(B, [W(), W(), W(), W(), W(), meas(3, { 1: 0, 5: 0.25, 4: 85, 14: 20, 15: -5 }), meas(1, { 1: 0, 4: 90, 15: 75, 7: 3, 8: 12 }), meas(1, { 1: 0, 4: 93, 15: 98, 7: 3, 8: 12 })]);
  const i8 = await api.phPrepare(2, 0, 1); api.refresh();
  r = await api.phMeasure(() => {});
  B.sets = [];
  o = await api.phApply(() => {}, r);
  const order = B.sets.filter(s => s[1] == "auto" || s[1] == "apstages" || s[1] == "apfreq").map(s => s[1]);
  check(order.join(",") == "auto,apstages,apfreq", "order: Auto Align, then the all-passes (" + order.join(",") + ")");
  check(o.steps.length == 2 && Math.round(o.final.corrout) == 98, "two confirmation measurements, final correlation 98%");

  // 9. rotation already set as recommended: it does not repeat it
  B = makeBoard(baseCells(), baseLinks());
  api = env(B, [W(), W(), W(), W(), W(), meas(1, { 1: 0, 4: 92, 15: 99, 7: 2, 8: 8 })]);
  const i9 = await api.phPrepare(2, 0, 1); api.refresh();
  const ph9 = B.cells.find(c => c.b.id == i9.P).b;
  ph9.v[12] = 2; ph9.v[13] = 307;
  api.refresh();
  r = await api.phMeasure(() => {});
  B.sets = [];
  o = await api.phApply(() => {}, r);
  check(!B.sets.some(s => /^ap/.test(s[1])) && o.applied.length == 0, "if the recommended rotation is already set nothing changes");

  // 10. ADELphase already present (as in the user's current layout): only the probe is added and then removed
  const withP = baseCells().concat([{ r: 3, c: 7, b: { id: 4, on: 1, t: "fx", fx: "adelphase", v: new Array(20).fill(0) } }]);
  const linksP = [[-1, 0, 1, 0, 0], [-1, 1, 1, 0, 0], [0, 4, 0, 1, 0], [1, 4, 0, 2, 0], [4, 2, 0, 2, 0], [2, -2, 0, 1, 0]];
  B = makeBoard(withP, linksP); api = env(B, []);
  const l10 = sortL(B.links);
  const i10 = await api.phAddProbe({ id: 4 });
  const S10 = B.cells.find(c => c.b.id == i10.S);
  check(S10 && S10.b.fx == "adelsendsweep" && S10.c >= 0 && S10.c <= 3 && i10.P == -1, "probe added (column " + S10.c + ") without touching ADELphase");
  check(B.links.some(l => l[0] == -1 && l[1] == i10.S) && B.links.some(l => l[0] == i10.S && l[1] == 0) && B.links.some(l => l[0] == i10.S && l[1] == 1) && !B.links.some(l => l[0] == -1 && l[1] == 0), "input -> probe -> the two NAMs");
  check(B.links.some(l => l[0] == 0 && l[1] == 4 && l[3] == 1) && B.links.some(l => l[0] == 1 && l[1] == 4 && l[3] == 2) && B.links.some(l => l[0] == 4 && l[1] == 2 && l[3] == 2), "ADELphase's links do not change");
  api.refresh();
  const f10 = api.phFind();
  check(f10.p && f10.s && f10.p.id == 4 && f10.s.id == i10.S, "now phFind sees ADELphase and the probe");
  await api.phRestore(i10, true);
  check(sortL(B.links) == l10 && B.cells.length == withP.length, "removes the probe and the layout returns identical to before");
  // error halfway: everything returns as it was
  B = makeBoard(withP, linksP); api = env(B, []); B.failAt = 2;
  err = null;
  try { await api.phAddProbe({ id: 4 }); } catch (e) { err = e; }
  check(err && sortL(B.links) == sortL(linksP) && B.cells.length == withP.length, "error halfway: layout identical to the previous one (" + (err && err.message) + ")");
  // without two paths on the same input: clear error
  B = makeBoard(withP, [[-1, 0, 1, 0, 0], [-1, 1, 2, 0, 0], [0, 4, 0, 1, 0], [1, 4, 0, 2, 0], [4, 2, 0, 2, 0], [2, -2, 0, 1, 0]]); api = env(B, []);
  err = null;
  try { await api.phAddProbe({ id: 4 }); } catch (e) { err = e; }
  check(err && /same input/.test(err.message), "different inputs: clear error (" + (err && err.message) + ")");
  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})().catch(e => { console.log("ECCEZIONE:", e.stack); process.exit(1); });
