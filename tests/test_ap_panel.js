// test_ap_panel.js - node test of the page's "Fallback network" card (apCard) with a fake DOM
// uso: node test_ap_panel.js control_page.h
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
const code = [line(/^const el=/), line(/^function btn\(/), line(/^const apOk=/), fnSrc("apCard")].join("\n");

class N {
  constructor(t) { this.tag = t; this.children = []; this._html = ""; this.dataset = {}; this.style = {}; this.className = ""; this.value = ""; }
  appendChild(c) { this.children.push(c); return c; }
  set innerHTML(v) { this._html = v; if (v === "") this.children = []; }
  get innerHTML() { return this._html; }
  text() { return (this._html || "") + " " + this.children.map(c => (c instanceof N ? c.text() : String(c.t))).join(" "); }
  find(pred, out = []) { if (pred(this)) out.push(this); this.children.forEach(c => c instanceof N && c.find(pred, out)); return out; }
}
const doc = { createElement: t => new N(t) };
const sent = [], alerts = [];
let confirmAnswer = true;
const make = APCx => {
  sent.length = 0; alerts.length = 0;
  const f = new Function("document", "APC", "g", "alert", "confirm", "state", "let apEl=null;" + code + ";return apCard;");
  const st = {};
  const ap = f(doc, APCx, o => { sent.push(o); return Promise.resolve({ ok: 1 }); }, m => alerts.push(m), () => confirmAnswer, st);
  return ap;
};
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
(async () => {
  const base = { active: 0, ssid: "NAMberry5", pass: "test-pass-1234", ch: 6, wait: 60 };
  // without data: no card
  check(make(null)() === null, "without data the card is not there");
  const ap = make({ ...base });
  const c = ap();
  const t = c.text();
  check(/Fallback network/.test(t) && /60 s/.test(t) && /192\.168\.4\.1/.test(t), "title, 60 s wait and address");
  check(!/You are connected to the board's fallback network/.test(t), "not active: no warning");
  const inputs = c.find(n => n.tag == "input");
  check(inputs.length == 2 && inputs[0].value == "NAMberry5" && inputs[1].value == "test-pass-1234", "name and password shown");
  const sel = c.find(n => n.tag == "select")[0];
  check(sel && sel.children.length == 13 && sel.value == "6", "channel 6 out of 13");
  const bs = c.find(n => n.tag == "button");
  check(bs.length == 2 && bs[0].innerHTML == "Save" && /Try the fallback network now/.test(bs[1].innerHTML), "Save and Try now buttons");
  // invalid values: no request
  inputs[0].value = "name with spaces"; await bs[0].onclick();
  check(sent.length == 0 && alerts.length == 1, "name with spaces rejected in the page");
  inputs[0].value = "Sala1"; inputs[1].value = "short"; await bs[0].onclick();
  check(sent.length == 0 && alerts.length == 2, "password too short rejected");
  // valid values
  inputs[1].value = "test-pass-5678"; sel.value = "11"; await bs[0].onclick();
  check(sent.length == 1 && sent[0].op == "apset" && sent[0].s == "Sala1" && sent[0].p == "test-pass-5678" && sent[0].c == 11, "saving sends apset with name, password and channel");
  // test: with confirmation it sends apnow, without it does not
  sent.length = 0; confirmAnswer = false; await bs[1].onclick();
  check(sent.length == 0, "without confirmation no reboot");
  confirmAnswer = true; await bs[1].onclick();
  check(sent.length == 1 && sent[0].op == "apnow", "with confirmation it sends apnow");
  // the card is remembered until the state changes
  check(ap() === c, "the card stays the same (the typed values are not lost)");
  // active: warning and button to go back home
  const ap2 = make({ ...base, active: 1 });
  const c2 = ap2(); const t2 = c2.text();
  check(/You are connected to the board's fallback network/.test(t2), "active: warning");
  const b2 = c2.find(n => n.tag == "button");
  check(b2.length == 2 && /Back to the home network/.test(b2[1].innerHTML) && !/Try the fallback network now/.test(c2.text()), "active: Back to the home network button");
  sent.length = 0; await b2[1].onclick();
  check(sent.length == 1 && sent[0].op == "reboot", "back home sends reboot");
  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})();
