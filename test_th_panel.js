// test_th_panel.js - node test of the page's "Temperature and fan" card (thCard) with a fake DOM
// uso: node test_th_panel.js control_page.h
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
const code = [line(/^const el=/), line(/^function btn\(/), line(/^const thText=/), fnSrc("thCard"), line(/^const ccText=/), fnSrc("concertCard")].join("\n");
/* ccEl/ccStat are declared by the test container (in the page they are global "let"s)*/

class N {
  constructor(t) { this.tag = t; this.children = []; this._html = ""; this.dataset = {}; this.style = {}; this.className = ""; this.value = ""; this.checked = false; this.textContent = ""; }
  appendChild(c) { this.children.push(c); return c; }
  set innerHTML(v) { this._html = v; if (v === "") this.children = []; }
  get innerHTML() { return this._html; }
  text() { return (this._html || "") + " " + (this.textContent || "") + " " + this.children.map(c => (c instanceof N ? c.text() : String(c.t))).join(" "); }
  find(pred, out = []) { if (pred(this)) out.push(this); this.children.forEach(c => c instanceof N && c.find(pred, out)); return out; }
}
const doc = { createElement: t => new N(t), createTextNode: t => ({ t }) };
const sent = [], alerts = [];
let confirmAnswer = true;
const make = (THCx, which) => {
  sent.length = 0; alerts.length = 0;
  const f = new Function("document", "THC", "g", "alert", "confirm", "let thEl=null,thStat=null,ccEl=null,ccStat=null;" + code + ";return " + (which || "thCard") + ";");
  return f(doc, THCx, o => { sent.push(o); return Promise.resolve({ ok: 1 }); }, m => alerts.push(m), () => confirmAnswer);
};
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
(async () => {
  const base = { temp: 523, mhz: [1500, 1500, 2400], fan: 0, fanctl: 0, on: 55, off: 47, boost: 0 };
  check(make(null)() === null, "without data the card is not there");
  const card = make({ ...base });
  const c = card();
  const t = c.text();
  check(/Temperature and fan/.test(t) && /52\.3/.test(t) && /1500 MHz/.test(t) && /managed by the system/.test(t), "title, temperature 52.3 and fan managed by the system");
  const inputs = c.find(n => n.tag == "input");
  const cbs = inputs.filter(n => n.type == "checkbox"), nums = inputs.filter(n => n.type == "number");
  check(cbs.length == 2 && nums.length == 3 && nums[0].value == 55 && nums[1].value == 47 && nums[2].value == 55 && !cbs[0].checked && !cbs[1].checked, "two boxes, thresholds 55 and 47, maximum speed 55, everything off");
  const bs = c.find(n => n.tag == "button");
  check(bs.length == 1 && bs[0].innerHTML == "Save", "Save button");
  // invalid thresholds: no request
  nums[0].value = "55"; nums[1].value = "54"; await bs[0].onclick();
  check(sent.length == 0 && alerts.length == 1, "turn-off too close to turn-on rejected in the page");
  nums[0].value = "90"; nums[1].value = "47"; await bs[0].onclick();
  check(sent.length == 0 && alerts.length == 2, "turn-on above 80 degrees refused");
  // valid values
  nums[0].value = "62"; nums[1].value = "50"; nums[2].value = "20"; await bs[0].onclick();
  check(sent.length == 0 && alerts.length == 3, "maximum speed below 35% rejected");
  cbs[0].checked = true; cbs[1].checked = true; nums[2].value = "50"; await bs[0].onclick();
  check(sent.length == 1 && sent[0].op == "thset" && sent[0].fc == 1 && sent[0].on == 62 && sent[0].off == 50 && sent[0].mx == 50 && sent[0].bo == 1, "saving sends thset with control, thresholds, maximum speed and frequency");
  check(card() === c, "the card stays the same (the typed values are not lost)");
  // fan controlled and on
  const c2 = make({ ...base, fanctl: 1, fan: 1, fp: 42, mx: 55, temp: 581 })();
  check(/fan at 42%/.test(c2.text()) && /58\.1/.test(c2.text()), "fan controlled: at 42% at 58.1");
  const c3 = make({ ...base, fanctl: 1, fan: 0, fp: 0, mx: 55, temp: 480 })();
  check(/fan off/.test(c3.text()), "fan controlled and stopped: off");
  // concert mode
  check(make(null, "concertCard")() === null, "concert: without data the card is not there");
  const cc = make({ ...base, da: 3 }, "concertCard")();
  check(/Concert mode/.test(cc.text()) && /DMA alarms\): 3/.test(cc.text()), "concert: title and number of recorded alarms");
  const cb = cc.find(n => n.tag == "button");
  check(cb.length == 1 || cb.length === undefined, "concert: a button");
  confirmAnswer = false; await cb[0].onclick();
  check(sent.length == 0, "concert: without confirmation it sends nothing");
  confirmAnswer = true; await cb[0].onclick();
  check(sent.length == 1 && sent[0].op == "concert", "concert: with confirmation it sends op concert");
  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})();
