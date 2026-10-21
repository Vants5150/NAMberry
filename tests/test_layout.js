// test_layout.js - node test of the linear reordering of the blocks (layoutNet): valid positions, links always towards the right, free columns for probe and ADELphase
// uso: node test_layout.js control_page.h
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
function fnSrc(name) {
  const i = src.indexOf("function " + name + "(");
  if (i < 0) throw new Error("function not found: " + name);
  let j = src.indexOf("{", i), d = 0, k = j;
  for (; k < src.length; k++) { if (src[k] == "{") d++; else if (src[k] == "}") { d--; if (!d) break; } }
  return src.slice(i, k + 1);
}
const layoutNet = new Function(fnSrc("layoutNet") + ";return layoutNet;")();
let fails = 0;
function check(c, m) { if (!c) { console.log("FAILED:", m); fails++; } else console.log("ok      ", m); }

function mk(ids, links) {
  const N = { rows: 6, cols: 12, cells: ids.map((id, k) => ({ r: k % 6, c: k % 12, b: [{ id }] })), io: [[2, 0], [3, 0], [2, 11], [3, 11]] };
  return { N, all: links.map(([f, t]) => ({ f, t, pf: 0, pt: 0, db: 0 })) };
}
function validate(lay, all, label) {
  const col = {}, seen = new Set();
  lay.blocks.forEach(b => { col[b[0]] = b[2]; const k = b[1] + "," + b[2]; if (seen.has(k)) check(false, label + ": duplicate cell"); seen.add(k); if (b[1] < 0 || b[1] > 5 || b[2] < 0 || b[2] > 11) check(false, label + ": outside the grid"); });
  lay.io.forEach(x => { const k = x[0] + "," + x[1]; if (seen.has(k)) check(false, label + ": duplicate IO cell"); seen.add(k); });
  col[-11] = lay.io[0][1]; col[-12] = lay.io[1][1]; col[-21] = lay.io[2][1]; col[-22] = lay.io[3][1];
  return all.every(l => col[l.t] >= col[l.f]);
}

// like preset aaa: IN L -> comp(3) -> 3 NAM (0,1,4) -> routing(2) -> delay(5) / verb(6) -> OUT L / OUT R
let t = mk([0, 1, 2, 3, 4, 5, 6], [[-11, 3], [3, 0], [3, 1], [3, 4], [0, 2], [1, 2], [4, 2], [2, 5], [2, 6], [5, -21], [6, -22]]);
let lay = layoutNet(t.N, t.all);
check(!lay.err, "aaa-type preset: reorderable");
check(validate(lay, t.all, "aaa"), "aaa-type preset: distinct cells, inside the grid, all links towards the right");
const cb = id => lay.blocks.find(b => b[0] == id);
check(cb(3)[2] == 1 && cb(0)[2] == 4 && cb(1)[2] == 4 && cb(4)[2] == 4 && cb(2)[2] == 7 && cb(5)[2] == 9 && cb(6)[2] == 9, "four levels spread over columns 1, 4, 7, 9 (two free columns where the arrows are most crowded)");
{
  const r = [cb(0)[1], cb(1)[1], cb(4)[1]].sort((a, b) => a - b);
  check(r[1] - r[0] >= 2 && r[2] - r[1] >= 2, "the three NAMs in the same column with a free row between one and the next");
  const d = [cb(5)[1], cb(6)[1]].sort((a, b) => a - b);
  check(d[1] - d[0] >= 2, "delay and reverb with a free row in between");
  check(Math.abs(lay.io[0][0] - lay.io[1][0]) >= 2 && Math.abs(lay.io[2][0] - lay.io[3][0]) >= 2, "Input L/R and Output L/R two rows apart");
}
check(lay.io[0][1] == 0 && lay.io[1][1] == 0 && lay.io[2][1] == 11 && lay.io[3][1] == 11, "inputs always in column 0 and outputs always in the last (11): the two extremes");
check(new Set([cb(0)[1], cb(1)[1], cb(4)[1]]).size == 3, "the three NAMs on three different rows");

// long chain: 4 levels fit with the free column, 8 only without
t = mk([0, 1, 2, 3, 4, 5, 6, 7], [[-11, 0], [0, 1], [1, 2], [2, 3], [3, 4], [4, 5], [5, 6], [6, 7], [7, -21]]);
lay = layoutNet(t.N, t.all);
check(!lay.err && validate(lay, t.all, "series8") && lay.blocks.every((b, k, a) => k == 0 || b[2] > a[k - 1][2]) && lay.blocks[0][2] == 1 && lay.blocks[7][2] == 9 && lay.io[2][1] == 11, "8 blocks in series: distinct, increasing columns from 1 to 9, output at 11");
t = mk([0, 1, 2, 3, 4, 5, 6, 7, 8], [[-11, 0], [0, 1], [1, 2], [2, 3], [3, 4], [4, 5], [5, 6], [6, 7], [7, 8], [8, -21]]);
lay = layoutNet(t.N, t.all);
check(!lay.err && validate(lay, t.all, "series9") && lay.blocks.map(b => b[2]).join() == "1,2,3,4,5,6,7,8,9", "9 blocks in series: columns 1..9, output at 11");
t = mk([0, 1, 2, 3, 4, 5, 6, 7, 8, 9], [[-11, 0], [0, 1], [1, 2], [2, 3], [3, 4], [4, 5], [5, 6], [6, 7], [7, 8], [8, 9], [9, -21]]);
check(!!layoutNet(t.N, t.all).err, "10 blocks in series: they do not fit, no reordering");

// too many blocks in the same level
t = mk([0, 1, 2, 3, 4, 5, 6], [[-11, 0], [-11, 1], [-11, 2], [-11, 3], [-11, 4], [-11, 5], [-11, 6]]);
check(!!layoutNet(t.N, t.all).err, "7 blocks in parallel: more than the 6 rows, no reordering");

// block with no input links and empty preset
t = mk([0, 1], [[-11, 0], [0, -21]]);
lay = layoutNet(t.N, t.all);
check(!lay.err && validate(lay, t.all, "orphan") && lay.blocks.find(b => b[0] == 1)[2] == lay.blocks.find(b => b[0] == 0)[2] && lay.blocks.find(b => b[0] == 1)[0] == 1, "block with no input: in level 1 like the others, no error");
t = mk([], []);
lay = layoutNet(t.N, t.all);
check(!lay.err && lay.blocks.length == 0, "empty preset: nothing to reorder, no error");

// the links and the set of blocks do not change: same ids
t = mk([0, 1, 2, 3, 4, 5, 6], [[-11, 3], [3, 0], [3, 1], [3, 4], [0, 2], [1, 2], [4, 2], [2, 5], [2, 6], [5, -21], [6, -22]]);
lay = layoutNet(t.N, t.all);
check(lay.blocks.map(b => b[0]).sort().join() == "0,1,2,3,4,5,6", "the same seven blocks, each once");

console.log(fails ? "\nRESULT: " + fails + " tests failed" : "\nRESULT: all tests passed");
process.exit(fails ? 1 : 0);
