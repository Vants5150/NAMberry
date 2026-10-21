// test_lbl.js - node test of the label under the block (lbl): NAM = model name, Cab IR = IR name, the others as before
// uso: node test_lbl.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf("function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
const lbl = new Function("nmOf", fnSrc("lbl") + ";return lbl;")(n => n.fx == "cabir" ? "Cab IR" : "Reverb");
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }
check(lbl({ t: "nam", loaded: true, name: "VX 304 NR Drive" }) == "VX 304 NR Drive", "NAM loaded: model name");
check(lbl({ t: "nam", loaded: false, status: "loading" }) == "loading", "NAM not loaded: status");
check(lbl({ t: "fx", fx: "cabir", file: "AMA_MRSH_4X12_G12H_25W_Pulsonic_V3.air" }) == "AMA_MRSH_4X12_G12H_25W_Pulsonic_V3", "Cab IR: IR name without .air");
check(lbl({ t: "fx", fx: "cabir", file: "Test.AIR" }) == "Test", "Cab IR: uppercase extension");
check(lbl({ t: "fx", fx: "cabir", file: "" }) == "Cab IR", "Cab IR without a file: type name");
check(lbl({ t: "fx", fx: "adelverb" }) == "Reverb", "other effects: type name");
check(lbl({ t: "vol", vol: -3, rt: 0 }) == "Vol -3.0 dB", "volume unchanged");
console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
