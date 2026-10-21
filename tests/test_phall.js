// test_phall.js - node test of the "Branch phase" section (phRun / phShort / phNeeds) with fake blocks and measurements: Measure, Apply, repetitions, errors
// uso: node test_phall.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name, pre) {
  const i = src.indexOf((pre || "") + "function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
const line = re => { const m = src.split("\n").find(l => re.test(l)); if (!m) throw new Error("line not found: " + re); return m; };
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }

const PH = ["lane", "stale", "verdict", "loss", "conf", "sugdelay", "sugpol", "sugstages", "sugfreq", "invert", "delay", "trim", "apstages", "apfreq", "corrin", "corrout", "lag", "latency", "mode", "auto"];

// opts: hasPhase (ADELphase already in the preset), hasProbe, acts (pairs proposed by the analysis), measures (successive results of phMeasure), applyFinal, failAt
function make(opts) {
  const log = [];
  const st = { ph: opts.hasPhase ? { id: 20, t: "fx", fx: "adelphase", on: 1, v: new Array(20).fill(0) } : null, probe: !!opts.hasProbe, acts: (opts.acts || []).slice(), builds: 0, mi: 0, created: false };
  const env = {
    phBusy: false, phMsg: "", phFix: false, phPcSeen: null,
    document: { getElementById: () => null },
    esc: s => String(s),
    sleep: () => Promise.resolve(),
    stNow: async () => {},
    netAll: () => st.ph ? [{ f: 1, t: 20, pf: 0, pt: 1 }, { f: 2, t: 20, pf: 0, pt: 2 }] : [],
    netCells: () => [{ b: [{ id: 1, t: "nam" }] }].concat(st.ph ? [{ b: [st.ph] }] : []).concat(st.probe ? [{ b: [{ id: 30, t: "fx", fx: "adelsendsweep", on: 1 }] }] : []),
    netAnc: id => new Set([id, 30]),
    analyzeNet: () => st.acts.map(a => ({ act: { kind: "phase", t: 5, a, b: 100 } })),
    fxd: () => ({ p: PH.map(k => ({ k })) }),
    phAddProbe: async p => { log.push("probe"); st.probe = true; return { S: 30, P: -1 }; },
    phPrepare: async () => { log.push("prepare"); st.probe = true; st.created = true; st.ph = { id: 20, t: "fx", fx: "adelphase", on: 1, v: new Array(20).fill(0) }; st.acts.shift(); return { S: 30, P: 20 }; },
    phMeasure: async (say, P) => { log.push("measure"); if (opts.failAt && st.mi + 1 == opts.failAt) throw new Error("measurement failed"); return Object.assign({}, opts.measures[Math.min(st.mi++, opts.measures.length - 1)]); },
    phApply: async (say, r, P) => { log.push("apply"); st.ph.v[10] = r.sugdelay || 0; return { applied: ["Delay " + (r.sugdelay || 0).toFixed(2) + " ms"], steps: [], final: opts.applyFinal }; },
    phRestore: async (info, keep) => { log.push("restore:" + (keep ? "keep" : "remove")); st.probe = false; if (!keep) st.ph = null; },
    build: () => { st.builds++; }, upd: () => {},
  };
  const code = [line(/^const phHz=/), line(/^const phTwoIn=/), fnSrc("phNeeds"), fnSrc("phShort"), fnSrc("phRun", "async ")].join("\n") + "\nreturn {phRun,phShort,phNeeds,get:()=>({msg:phMsg,fix:phFix})};";
  const api = new Function("env", "with(env){let phMsg='',phFix=false;" + code.replace(/phBusy/g, "env.phBusy") + "}")(env);
  return { api, log, st, env };
}
const M = (v, o) => Object.assign({ verdict: v, sugdelay: 0, sugpol: 0, sugstages: 0, sugfreq: 0 }, o || {});

(async () => {
  // 1. first measurement: everything is missing -> adds probe and ADELphase, measures, says what is wrong, removes the probe and keeps ADELphase
  let t = make({ acts: [1], measures: [M(3, { sugdelay: 0.52 })] });
  await t.api.phRun(false);
  let g = t.api.get();
  check(t.log.join(",") == "prepare,measure,restore:keep", "measure: prepares, measures, removes the probe and keeps ADELphase (" + t.log.join(",") + ")");
  check(/To correct: delay of 0\.52 ms on branch B/.test(g.msg) && g.fix, "says what is wrong and proposes \"Apply the correction\"");

  // 2. apply: writes into ADELphase and measures again; in the end everything is fine
  t = make({ hasPhase: true, measures: [M(3, { sugdelay: 0.52 })], applyFinal: M(1) });
  await t.api.phRun(true);
  g = t.api.get();
  check(t.log.join(",") == "probe,measure,apply,restore:keep", "apply: probe, measure, apply, probe removed (" + t.log.join(",") + ")");
  check(/Applied: Delay 0\.52 ms\. All good\./.test(g.msg) && !g.fix, "after applying it says \"All good\" and the Apply button disappears (" + g.msg + ")");

  // 3. measure again: ADELphase already has the values -> the measurement is already corrected by the page (verdict 1) -> all good, nothing to apply
  t.st.acts = [];
  t.st.mi = 0;
  const t3 = make({ hasPhase: true, measures: [M(1)] });
  await t3.api.phRun(false);
  g = t3.api.get();
  check(g.msg == "All good." && !g.fix && !/apply/.test(t3.log.join(",")), "measuring again: \"All good.\" and nothing more (" + g.msg + ")");

  // 4. a just-created pair that needs nothing: temporary ADELphase removed
  t = make({ acts: [1], measures: [M(1)] });
  await t.api.phRun(false);
  check(t.log.join(",") == "prepare,measure,restore:remove" && t.api.get().msg == "All good.", "all good from the start: probe and temporary ADELphase removed");

  // 5. polarity and phase rotation described in a few words
  t = make({ hasPhase: true, measures: [M(5, { sugdelay: -0.25, sugstages: 2, sugfreq: 8 })] });
  await t.api.phRun(false);
  check(/polarity to invert and delay of 0\.25 ms on branch B; phase rotation: 2 stages at 307 Hz/.test(t.api.get().msg.replace("branch A", "branch B")), "short text: polarity, delay, rotation (" + t.api.get().msg + ")");

  // 6. no branch to align
  t = make({ measures: [M(1)] });
  await t.api.phRun(false);
  check(/There are no parallel branches to align/.test(t.api.get().msg) && t.log.length == 0, "no parallel branches: no operation on the board");

  // 7. error: probe removed, message, not left busy
  t = make({ acts: [1], measures: [M(3)], failAt: 1 });
  await t.api.phRun(false);
  check(/measurement failed/.test(t.api.get().msg) && /restore:remove/.test(t.log.join(",")) && !t.env.phBusy, "error: temporary probe and ADELphase removed, message visible, not left busy");

  // 8. already busy
  t = make({ acts: [1], measures: [M(3)] });
  t.env.phBusy = true;
  await t.api.phRun(false);
  check(t.log.length == 0, "already running: it does not start again");

  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})();
