// test_lib_poll.js - node test of libSync (the library re-reads /lib only when "lv" changes) and of ndFull (NAM model name in the Links table)
// uso: node test_lib_poll.js control_page.h
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

(async () => {
  let calls = [];
  let lib = { lv: 111, lib: ["a.adn"], ir: ["x.air"], libinf: [""] };
  const fakeFetch = async url => { calls.push(url); return { json: async () => lib }; };
  const mk = new Function("fetch", "let LIBC=null,LIBV=-1;async " + fnSrc("libSync") + ";return {libSync,get:()=>[LIBC,LIBV]};")(fakeFetch);
  await mk.libSync(111);
  check(calls.length == 1 && calls[0].startsWith("/lib?"), "first time: reads /lib");
  await mk.libSync(111);
  await mk.libSync(111);
  check(calls.length == 1, "same version: no new request");
  lib = { lv: 222, lib: ["a.adn", "b.adn"], ir: [], libinf: ["", ""] };
  await mk.libSync(222);
  check(calls.length == 2 && mk.get()[0].lib.length == 2 && mk.get()[1] == 222, "version changed: re-reads /lib and updates the copy");

  // ndFull: NAM with model name, other blocks and input/output unchanged
  const nodes = { 5: { id: 5, t: "nam", loaded: true, name: "VX 304 NR Drive" }, 6: { id: 6, t: "nam", loaded: false, status: "" }, 7: { id: 7, t: "fx", fx: "adelverb" } };
  const ndName = id => id < 0 ? "Input" : nodes[id].t == "nam" ? "NAM" : "Reverb";
  const lbl = new Function(fnSrc("lbl") + ";return lbl;")();
  const ndFull = new Function("isIo", "netNode", "ndName", "lbl", lineSrc("const ndFull=") + ";return ndFull;")(id => id < 0, id => nodes[id] || null, ndName, lbl);
  check(ndFull(5) == "NAM - VX 304 NR Drive", "NAM loaded: NAM - model name");
  check(ndFull(6) == "NAM", "NAM with neither name nor status: just NAM");
  check(ndFull(7) == "Reverb", "effect: name unchanged");
  check(ndFull(-11) == "Input", "input/output: name unchanged");
  console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
  process.exit(fails ? 1 : 0);
})();
