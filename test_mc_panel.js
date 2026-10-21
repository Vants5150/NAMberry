// test_mc_panel.js - node test of the page's "Processor cores" card (mcCard) with a fake DOM
// uso: node test_mc_panel.js control_page.h
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
const code = [line(/^const el=/), line(/^function btn\(/), line(/^const mcText=/), fnSrc("mcCard")].join("\n");

class N {
  constructor(t) { this.tag = t; this.children = []; this._html = ""; this.dataset = {}; this.style = {}; this.className = ""; this.value = ""; this.textContent = ""; }
  appendChild(c) { this.children.push(c); return c; }
  set innerHTML(v) { this._html = v; if (v === "") this.children = []; }
  get innerHTML() { return this._html; }
  text() { return (this._html || "") + " " + (this.textContent || "") + " " + this.children.map(c => (c instanceof N ? c.text() : String(c.t))).join(" "); }
  find(pred, out = []) { if (pred(this)) out.push(this); this.children.forEach(c => c instanceof N && c.find(pred, out)); return out; }
}
const doc = { createElement: t => new N(t) };
const sent = [], alerts = [];
const make = MCx => {
  sent.length = 0; alerts.length = 0;
  const f = new Function("document", "MCC", "g", "alert", "let mcEl=null,mcStat=null;" + code + ";return mcCard;");
  return f(doc, MCx, o => { sent.push(o); return Promise.resolve({ ok: 1 }); }, m => alerts.push(m));
};
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
(async () => {
  check(make(null)() === null, "without data the card is not there");
  const card = make({ avail: 1, cfg: 0, cores: 1, blocks: 0, faults: 0 });
  const c = card();
  const t = c.text();
  check(/Processor cores/.test(t) && /Cores in use: 1/.test(t) && /faults: 0/.test(t), "title and status: 1 core in use, no faults");
  const sel = c.find(n => n.tag == "select")[0];
  check(sel && sel.children.length == 4 && sel.value == "0", "choice between 0 and 3 worker cores, now 0");
  const bs = c.find(n => n.tag == "button");
  check(bs.length == 1, "Save button");
  sel.value = "2"; await bs[0].onclick();
  check(sent.length == 1 && sent[0].op == "mcset" && sent[0].v == 2, "saving sends mcset with 2");
  check(card() === c, "the card stays the same");
  const c2 = make({ avail: 1, cfg: 2, cores: 3, blocks: 12345, faults: 1 })();
  check(/Cores in use: 3/.test(c2.text()) && /12345/.test(c2.text()) && /faults: 1/.test(c2.text()), "with 3 cores: blocks and faults shown");
  const c3 = make({ avail: 0, cfg: 0, cores: 1, blocks: 0, faults: 0 })();
  check(/not built with multi-core support/.test(c3.text()) && c3.find(n => n.tag == "select").length == 0, "without support: warning and no choice");
  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})();
