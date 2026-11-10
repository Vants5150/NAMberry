// bulk_upload.js - uploads the .nam models and the .wav / .air impulse responses of a folder to the NAMberry, with the SAME conversion and the SAME requests as the web page
// (the functions are taken from control_page.h, so there is one source of truth): .nam -> .adn (A2 models only), .wav -> .air, POST /up in 1380-byte pieces.
// usage: node bulk_upload.js control_page.h FOLDER BOARD_IP [dry]
//   dry: converts everything and reports (sizes, names, files that already exist, models that cannot be converted) without sending anything.
// A name that already exists in the board's library is NOT replaced: it is reported and skipped.
const fs = require("fs"), path = require("path");
const [pagePath, dir, host, mode] = process.argv.slice(2);
const dry = mode === "dry";
const src = fs.readFileSync(pagePath, "utf8");
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
const F = new Function("btoa", [lineSrc("const b64u="), lineSrc("const safeName="), fnSrc("crc32"), fnSrc("namToAdn"), fnSrc("wavToIr"), fnSrc("irToAir")].join("\n")
  + "\nreturn {b64u, safeName, namToAdn, wavToIr, irToAir};")(btoa);
const base = "http://" + host;
let T = "";

// The board's web server reads a POST from the first TCP segment: the headers and the body must go out together, as a browser does. Node's fetch sends them in two
// pieces and every request got "400 Bad Request", so the POST is made with http.request (the body is given to end ()).
const http = require("http");
function post(p, body) {
  return new Promise((res, rej) => {
    const r = http.request({ host, port: 80, path: p, method: "POST", agent: false, timeout: 10000,
      headers: { "Content-Type": "application/x-www-form-urlencoded", "Content-Length": Buffer.byteLength(body), "Connection": "close" } }, x => {
      let s = ""; x.on("data", d => s += d); x.on("end", () => { try { res(JSON.parse(s)); } catch (e) { rej(new Error("HTTP " + x.statusCode + ": " + s.slice(0, 60))); } });
    });
    r.on("timeout", () => r.destroy(new Error("timeout")));
    r.on("error", rej);
    r.end(body);
  });
}
async function api(p, o) {
  const q = new URLSearchParams();
  q.set("t", T); q.set("pg", "1");
  for (const k in o) q.set(k, String(o[k]));
  return post(p, q.toString());
}
async function upload(name, bytes) {
  const N = 1380;
  for (let o = 0; o < bytes.length; o += N) {
    let r;
    for (let attempt = 0; attempt < 3; attempt++) {
      try { r = await api("/up", { n: name, o, z: bytes.length, d: F.b64u(bytes.subarray(o, o + N)) }); break; }
      catch (e) { if (attempt == 2) throw e; await new Promise(x => setTimeout(x, 300)); }
    }
    if (!r.ok) throw new Error(r.err || "upload error");
  }
  // the board writes the file to the SD card and its web server can be slow to answer meanwhile: a failed or slow status read is not a failed upload, read it again
  for (let i = 0; i < 120; i++) {
    await new Promise(x => setTimeout(x, 400));
    try {
      const j = await (await fetch(base + "/status?_=" + Date.now(), { signal: AbortSignal.timeout(15000) })).json();
      if (j.upl && j.upl.n == name && j.upl.ok) { if (j.upl.ok == 1) return; throw new Error(j.upl.e || "rejected by the board"); }
    } catch (e) { if (e.message && !/fetch failed|aborted|timeout/i.test(e.message)) throw e; }
  }
  throw new Error("no confirmation from the board");
}

(async () => {
  T = (await (await fetch(base + "/t.js")).text()).match(/NB_T="([a-z0-9]+)"/)[1];
  const lib = await (await fetch(base + "/lib")).json();
  const haveAdn = new Set(lib.lib), haveAir = new Set(lib.ir);
  const files = fs.readdirSync(dir).filter(f => /\.(nam|wav|air)$/i.test(f)).sort();
  console.log("board library: " + lib.lib.length + " models, " + lib.ir.length + " IR;  files to look at: " + files.length + (dry ? "  (dry run: nothing is sent)" : ""));
  const seen = new Set();
  let ok = 0, skipped = 0, failed = 0, bytesTotal = 0;
  for (const f of files) {
    const stem = f.replace(/\.[^.]*$/, ""), name = F.safeName(stem);
    const isNam = /\.nam$/i.test(f), isIr = !isNam;
    const out = name + (isNam ? ".adn" : ".air");
    try {
      if ((isNam ? haveAdn : haveAir).has(out)) { console.log("SKIP exists   " + f + " -> " + out); skipped++; continue; }
      if (seen.has(out)) { console.log("SKIP same name inside this folder: " + f + " -> " + out); skipped++; continue; }
      seen.add(out);
      const buf = fs.readFileSync(path.join(dir, f));
      let bytes, info = "";
      if (isNam) bytes = F.namToAdn(buf.toString("utf8"), stem);
      else if (/\.air$/i.test(f)) bytes = new Uint8Array(buf);
      else { const r = F.irToAir(F.wavToIr(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength))); bytes = r.air; info = " (" + r.taps + " samples, " + r.ch + " ch, from " + r.sr + " Hz)"; }
      if (isNam && bytes.length > 262144) throw new Error("model too large (maximum 256 KB)");
      if (!dry) { await upload(out, bytes); await new Promise(x => setTimeout(x, 400)); }
      bytesTotal += bytes.length; ok++;
      console.log((dry ? "ok (not sent) " : "UPLOADED     ") + f + " -> " + out + "  " + bytes.length + " bytes" + info);
    } catch (e) {
      failed++;
      console.log("FAILED       " + f + " -> " + out + ": " + (e.message || e));
    }
  }
  console.log("\nresult: " + ok + (dry ? " ready" : " uploaded") + ", " + skipped + " skipped, " + failed + " failed, " + bytesTotal + " bytes");
})();
