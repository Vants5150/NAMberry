// test_bp_poll.js - node test of bpSync (the saved block settings are re-read from /bpl only when "bv" of /status changes)
// usage: node test_bp_poll.js control_page.h
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

(async () => {
  let calls = [];
  let bp = { bv: 5, bp: [{ n: "A", d: "fx:x|k=1" }] };
  let fail = false;
  const fakeFetch = async url => { calls.push(url); if (fail) throw new Error("network"); return { json: async () => bp }; };
  const mk = new Function("fetch", "let BPC=null,BPV=-1;async " + fnSrc("bpSync") + ";return {bpSync,get:()=>[BPC,BPV]};")(fakeFetch);
  await mk.bpSync(5);
  check(calls.length == 1 && calls[0].startsWith("/bpl?"), "first time: reads /bpl");
  await mk.bpSync(5);
  await mk.bpSync(5);
  check(calls.length == 1, "same version: no new request");
  bp = { bv: 6, bp: [{ n: "A", d: "fx:x|k=1" }, { n: "B", d: "fx:y|k=2" }] };
  await mk.bpSync(6);
  check(calls.length == 2 && mk.get()[0].bp.length == 2 && mk.get()[1] == 6, "version changed: re-reads /bpl and updates the copy");
  fail = true;
  await mk.bpSync(7);
  check(mk.get()[0].bp.length == 2, "network error: keeps the previous list");
  const mk2 = new Function("fetch", "let BPC=null,BPV=-1;async " + fnSrc("bpSync") + ";return {bpSync,get:()=>[BPC,BPV]};")(fakeFetch);
  const n0 = calls.length;
  await mk2.bpSync(undefined);
  check(calls.length == n0 && mk2.get()[0].bp.length == 0, "status without bv: empty list, no request");
  await mk2.bpSync(1);
  check(mk2.get()[0].bp.length == 0, "network error at the first read: empty list");
  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})();