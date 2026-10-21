// control_page.h - the web page (HTML + CSS + JS in a single file). "@@TOKEN@@" is replaced with the boot code.
// Included by control.cpp; no external resources. The pedalboard is a grid of linked blocks (ADELGrid): L/R inputs and outputs, blocks, links.
static const char s_Page[] = R"HTML(<!doctype html>
<html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NAMberry5</title>
<style>
:root{--u:2px;--g:4px;--w:8px;--bg:#f4f5f7;--card:#fff;--ink:#16181d;--mut:#6a7280;--line:#dfe3ea;--acc:#1f6feb;--ok:#1a9d55;--warn:#d97706;--bad:#d1242f}
@media(prefers-color-scheme:dark){:root{--bg:#101216;--card:#181b21;--ink:#e8eaee;--mut:#8b93a1;--line:#2a2f38;--acc:#4c8dff;--ok:#3fb97a;--warn:#f0a43a;--bad:#ff6b6b}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
.w{max-width:960px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px;display:flex;align-items:center;gap:8px}
.dot0{width:10px;height:10px;border-radius:50%;background:var(--bad)}.dot0.on{background:var(--ok)}
.sub{color:var(--mut);font-size:13px;margin-bottom:12px}
.tlbar{position:sticky;top:0;z-index:50;background:#d97706;color:#fff;text-align:center;padding:9px 12px;font-weight:600;border-radius:0 0 10px 10px}
.tlbar.bad{background:var(--bad);color:#fff}
.cfgb{margin-left:8px;font-size:12px;font-weight:600;padding:4px 10px;border-radius:14px;border:1px solid var(--line);background:var(--card);color:var(--ink);text-decoration:none;white-space:nowrap}
.cfgb.on{border-color:var(--acc);color:var(--acc)}
.tabs a{flex:1;text-align:center;padding:8px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--mut);text-decoration:none;font-weight:600}
.tabs a.on{color:var(--ink);border-color:var(--acc);box-shadow:inset 0 -3px 0 var(--acc)}
.chips{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px;margin-bottom:12px}
.chip{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:8px 10px}
.chip b{display:block;font-size:12px;color:var(--mut);font-weight:500}.chip span{font-size:18px;font-variant-numeric:tabular-nums}
.bar{height:6px;border-radius:3px;background:var(--line);margin-top:6px;overflow:hidden}.bar i{display:block;height:100%;width:0;background:var(--ok);transition:width .15s}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px;margin-bottom:12px}
.card h2{font-size:16px;margin:0;display:flex;justify-content:space-between;align-items:center;gap:8px}
.info{color:var(--mut);font-size:13px;margin:2px 0 4px;word-break:break-word}.info.bad{color:var(--warn)}
.row{display:grid;grid-template-columns:110px 1fr 64px;align-items:center;gap:8px;padding:5px 0}
.row label{color:var(--mut);font-size:13px}.row output{text-align:right;font-variant-numeric:tabular-nums;font-size:14px}
input[type=range]{width:100%;accent-color:var(--acc)}
select{width:100%;padding:7px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink);font:inherit;grid-column:2/4;min-width:0}
.row.fr2{grid-template-columns:110px 1fr auto}.row.fr2 select{grid-column:auto}
.sw{position:relative;width:46px;height:26px;flex:none}.sw input{opacity:0;width:100%;height:100%;margin:0;position:absolute;z-index:1}
.sw i{position:absolute;inset:0;background:var(--line);border-radius:13px;transition:.15s}.sw i:after{content:"";position:absolute;left:3px;top:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:.15s}
.sw input:checked+i{background:var(--ok)}.sw input:checked+i:after{left:23px}
.row.tog{grid-template-columns:110px 1fr}.row.tog .sw{justify-self:end}
.acts{display:flex;gap:4px}.ico,.mini{border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:8px;padding:5px 9px;font:inherit;cursor:pointer;line-height:1}
.ico.fv{color:var(--warn)}.ico:active,.mini:active{background:var(--line)}.mini.del{color:var(--bad)}
.grow{flex:1}
/* PiPedal-style pedalboard*/
.pb{background:#1b1c20;border-radius:12px;padding:18px 14px 46px;overflow-x:auto;color:#e6e6e6}
.pbi{display:inline-flex;align-items:center;min-width:100%;padding-right:6px;position:relative;isolation:isolate}
.pbi.lsvg .ln{visibility:hidden}
.pbi svg.lsv{position:absolute;left:0;top:0;overflow:visible;pointer-events:none;z-index:-1}
.ch{display:flex;align-items:center;flex:none}
.ln{width:44px;height:var(--u);background:#cfd2d8;flex:none}
.ln.fill{flex:1;width:auto;min-width:18px}
/* stereo signal = tube: 8 px gray with a 4 px empty inside (two 2 px lines); the empty parts are above all the grays, so corners and junctions match*/
.ln.st{height:var(--w);position:relative;z-index:1}
.ln.st::after{content:"";position:absolute;left:0;right:0;top:var(--u);height:var(--g);background:#1b1c20;z-index:2}
.ln.st.fkin::after{right:calc(-0.5*(var(--u) + var(--g)))}.ln.st.fkout::after{left:calc(-0.5*(var(--u) + var(--g)))}
.fr>.sl.st,.fr>.sr.st{height:var(--w);top:calc(50% - 0.5*var(--w));width:calc(22px + 0.5*(var(--u) + var(--g)));z-index:1}
.fr>.sl.st{left:calc(-0.5*(var(--u) + var(--g)))}.fr>.sr.st{right:calc(-0.5*(var(--u) + var(--g)))}
.fr>.sl.st::after,.fr>.sr.st::after{content:"";position:absolute;top:var(--u);height:var(--g);background:#1b1c20;z-index:2}
.fr>.sl.st::after{left:var(--u);right:0}.fr>.sr.st::after{left:0;right:var(--u)}
.fk.sti .fr::before,.fk.sto .fr::after{width:var(--w);z-index:1}
.fk.sti .fr::before{left:calc(-0.5*(var(--u) + var(--g)))}.fk.sto .fr::after{right:calc(-0.5*(var(--u) + var(--g)))}
.fk.sti .fr:first-child::before,.fk.sto .fr:first-child::after{top:calc(50% - 0.5*var(--w))}
.fk.sti .fr:last-child::before,.fk.sto .fr:last-child::after{bottom:calc(50% - 0.5*var(--w))}
.fr>.vl,.fr>.vr{display:none;position:absolute;width:var(--g);background:#1b1c20;top:0;bottom:-40px;z-index:2}
.fr>.vl{left:calc(0.5*(var(--u) - var(--g)))}.fr>.vr{right:calc(0.5*(var(--u) - var(--g)))}
.fk.sti .fr>.vl,.fk.sto .fr>.vr{display:block}
.fr:first-child>.vl,.fr:first-child>.vr{top:calc(50% - 0.5*var(--g))}
.fr:last-child>.vl,.fr:last-child>.vr{bottom:calc(50% - 0.5*var(--g))}
.fr>.ch{flex:1}
.end{width:12px;height:12px;border-radius:50%;background:#fff;flex:none;position:relative}
.bk.io{background:#2f3340;color:#e6e6e6;border:2px solid #4f5565}.bk.io:hover{border-color:#a78bfa}.bk.io svg{stroke:#e6e6e6}.bk.io .iot{position:absolute;top:2px;left:0;right:0;text-align:center;font-size:9px;line-height:1;color:#9aa0aa;font-weight:600;letter-spacing:.04em}.bk.io svg{margin-top:8px;width:22px;height:22px}
body.nobm .bk:not(.io) .mt{display:none}
.mt{position:absolute;top:4px;right:3px;bottom:4px;display:flex;gap:2px}
.mt.one i{width:5px}
.mt i{position:relative;width:3px;height:100%;background:linear-gradient(to top,#2fbf71 0,#2fbf71 70%,#f0c030 70%,#f0c030 90%,#ef4444 90%);border-radius:1px;overflow:hidden}
.mt i b{position:absolute;left:0;right:0;top:0;height:100%;background:#1b1c20}
.mt i u{position:absolute;left:0;right:0;height:1px;background:#fff;text-decoration:none}
.end{cursor:pointer}.end::before{content:"";position:absolute;inset:-9px}.end.sel{box-shadow:0 0 0 3px rgba(167,139,250,.6)}
.end small{position:absolute;top:18px;left:50%;transform:translateX(-50%);font-size:10px;color:#9aa0aa;white-space:nowrap}
.bk{position:relative;width:44px;height:44px;border-radius:7px;background:#ececec;display:flex;align-items:center;justify-content:center;cursor:pointer;flex:none;border:2px solid transparent;color:#222}
.bk svg{width:26px;height:26px;stroke:#222;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.bk{touch-action:none;user-select:none;-webkit-user-select:none}
/* tile colors by family: reverbs green, delays blue, overdrives and distortions orange*/
.gsel{display:flex;gap:6px;flex-wrap:wrap}.gbn{max-width:160px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.gbn.on{border-color:var(--acc);color:var(--acc);box-shadow:inset 0 -3px 0 var(--acc)}
.bk.cg{background:#cdeedb}.bk.cg svg{stroke:#17713f}
.bk.cb{background:#cfe3fb}.bk.cb svg{stroke:#1d56a6}
.bk.co{background:#fde0c2}.bk.co svg{stroke:#b4540a}
.bk.dragging{opacity:.3}
.gh{position:fixed;z-index:50;pointer-events:none;opacity:.92;transform:translate(-50%,-50%);box-shadow:0 6px 18px rgba(0,0,0,.5)}
.ch.dz{outline:2px dashed #a78bfa;outline-offset:5px;border-radius:6px}
#dm{position:fixed;z-index:40;width:4px;border-radius:2px;background:#a78bfa;pointer-events:none;display:none}
.prebar{display:flex;gap:6px;align-items:center;flex:1;min-width:0;margin-right:10px}
.psel{flex:1;min-width:0;position:relative}
.psel>button.pcur{border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--ink);width:100%;height:34px;display:flex;align-items:center;gap:8px;padding:0 10px;font-size:15px;font-weight:600;text-align:left}
.psel .pno,.pmenu .pno{flex:none;min-width:2ch;text-align:right;color:var(--mut);font-variant-numeric:tabular-nums;font-weight:600}
.psel .pn{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.psel svg,.pmenu svg{width:16px;height:16px;flex:none;stroke:currentColor;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.pmenu{color:var(--ink);position:fixed;z-index:50;max-height:50vh;overflow:auto;background:var(--card,#1b1c20);border:1px solid var(--line);border-radius:10px;padding:4px;overflow-x:hidden;box-shadow:0 6px 24px rgba(0,0,0,.5)}
.pmenu .po{display:flex;align-items:center;gap:8px;padding:8px 10px;border-radius:8px;cursor:pointer;font-size:15px}
.pmenu .po:hover,.pmenu .po.on{background:rgba(127,127,127,.22)}
.prebar select{flex:1;min-width:0;font-size:15px;font-weight:600;width:auto;grid-column:auto}
.prebar button{flex:none;display:flex;align-items:center;justify-content:center;width:34px;height:34px;padding:0}
.prebar button svg{width:18px;height:18px;stroke:currentColor;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.ph>button.gb{flex:none;display:flex;align-items:center;justify-content:center;width:34px;height:34px;padding:0}
.ph>button.gb+button.gb{margin-left:6px}
.ntb{display:flex;align-items:center;margin-bottom:14px}
.nt{padding-bottom:6px}
.ngd{position:relative;display:grid;grid-template-columns:repeat(12,54px);column-gap:16px;row-gap:22px;align-items:start;width:max-content;padding:8px 4px 30px}
.gcl{height:54px;display:flex;justify-content:center;align-items:flex-start;border-radius:8px;position:relative;z-index:2}
.gcl.dz{outline:2px dashed #a78bfa;outline-offset:-2px}
.gcl .gp{width:32px;height:32px;border:1px dashed #4a4d56;background:transparent;color:#6b6f7a;border-radius:7px;font-size:20px;cursor:pointer;padding:0}
.gcl .gp:hover{border-color:#a78bfa;color:#cfd2d8}
.gcl .bk{flex:none}
.gcl select.pbs{width:62px;grid-column:auto}
.gio{align-self:start;margin:0;background:#2a2c33;color:#cfd2d8;border:1px solid #3a3d46;border-radius:8px;height:44px;padding:0 4px;font-size:11px;font-weight:700;display:flex;flex-direction:column;align-items:center;justify-content:center;cursor:pointer;line-height:1.25;position:relative;z-index:2}
.gio b{font-weight:600;font-size:11px;color:#fff;white-space:nowrap}
.pt{position:absolute;width:11px;height:11px;border-radius:50%;background:#2b2d33;border:2px solid #cfd2d8;box-sizing:border-box;cursor:crosshair;z-index:5;touch-action:none}
.pt.in{left:-7px}.pt.out{right:-7px}
.pt.c{top:16px;width:12px;height:12px}.pt.l{top:3px}.pt.r{top:30px}
.pt.sa{top:3px}.pt.sb{top:16px;width:12px;height:12px}.pt.sc{top:30px}.pt[data-lb]::after{content:attr(data-lb);position:absolute;right:11px;top:-2px;font:700 8px/1 sans-serif;color:var(--mut);pointer-events:none}
.ngd .bk{width:32px;height:32px;border-radius:6px}.ngd .bk svg{width:18px;height:18px}.ngd .bk.io svg{margin-top:6px;width:15px;height:15px}
.ngd .bk .lb{top:36px;font-size:9.5px;width:72px}.ngd .bk .tg{top:-7px;right:-7px}
.ngd .pt.c{top:10px}.ngd .pt.l{top:0}.ngd .pt.r{top:21px}.ngd .pt.sa{top:0}.ngd .pt.sb{top:10px}.ngd .pt.sc{top:21px}
.pt:hover,.pt.hov{background:#a78bfa;border-color:#fff}
.lamp{display:inline-block;width:14px;height:14px;border-radius:50%;background:#3a3d46;border:1px solid #555;vertical-align:middle;transition:background .1s,box-shadow .1s}
.lamp.on.green{background:#3fb97a;border-color:#8be0b2;box-shadow:0 0 8px #3fb97a}.lamp.on.red{background:#ff4d4d;border-color:#ff9a9a;box-shadow:0 0 8px #ff4d4d}
.swlamp{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--mut);margin:4px 0}
@keyframes swp{0%,100%{box-shadow:0 0 0 2px rgba(63,185,122,.9)}50%{box-shadow:0 0 0 6px rgba(63,185,122,.25)}}
.bk.swplay{border-color:#3fb97a;animation:swp .9s infinite}
.ph>button.gb#anbtn{margin-left:14px}
.ph>button.gb#midibtn{margin-left:6px}
.mitab{border-collapse:collapse;width:100%;font-size:13px}.mitab th,.mitab td{text-align:left;padding:3px 8px;border-bottom:1px solid var(--line,#8884)}
.mictl{margin:8px 0;padding:8px;border:1px solid var(--line,#8884);border-radius:8px}
.mimon{max-height:220px;overflow:auto;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12.5px;border:1px solid var(--line,#8884);border-radius:8px;padding:6px 8px}
.ftrow{display:flex;gap:12px;align-items:flex-start;padding:8px 0;border-bottom:1px solid var(--line,#8884)}
.ftrow .fttx{flex:1;min-width:0}.ftrow .fttx .info{margin:2px 0 0}
.ftrow .ftpr{display:flex;flex-wrap:wrap;gap:8px;align-items:center;font-size:13px}
.ftrta{display:flex;align-items:flex-end;gap:2px;height:110px;margin:6px 0 20px;border:1px solid var(--line,#8884);border-radius:8px;padding:6px 6px 4px}
.ftb{flex:1;height:100%;position:relative;display:flex;align-items:flex-end}
.ftb i{display:block;width:100%;background:#a78bfa;border-radius:2px 2px 0 0;min-height:1px}
.ftb span{position:absolute;bottom:-16px;left:50%;transform:translateX(-50%);font-size:9px;color:var(--mut)}
.fttun{margin:8px 0;text-align:center}.ftnote{font-size:44px;font-weight:700}.ftnote.dim{opacity:.35}.ftcents{font-size:18px;margin:0 8px}
.ftbar{position:relative;height:16px;margin:8px auto;max-width:360px;border:1px solid var(--line,#8884);border-radius:8px}
.ftbar b{position:absolute;left:50%;top:-3px;bottom:-3px;width:2px;background:#8c8}.ftbar i{position:absolute;top:2px;bottom:2px;width:8px;margin-left:-4px;background:#a78bfa;border-radius:4px}
.ftok{color:#4c4;font-weight:700}
.ftmet{display:flex;flex-wrap:wrap;gap:14px;margin:8px 0}.ftmet div{min-width:130px}.ftmet small{display:block;color:var(--mut)}.ftmet b{font-size:20px}
.phbox{margin:8px 0;padding:8px;border:1px solid var(--line,#8884);border-radius:8px}.phres{margin:4px 0;font-size:13px}.phres .bad{color:#c33}.ani.ignd{opacity:.5}
.mimon .mii{opacity:.95}.mimon .mio{color:var(--acc,#2a7);opacity:.95}
.midi h4{margin:14px 0 6px;font-size:14px}
.card.an h3{margin:0 0 8px;display:flex;align-items:center}
.ani{border-left:4px solid var(--line);padding:6px 10px;margin:10px 0;border-radius:4px;background:rgba(127,127,127,.08)}
.ani.problem{border-color:var(--bad)}.ani.warning{border-color:var(--warn)}.ani.suggestion{border-color:var(--acc)}.ani.info{border-color:var(--mut)}
.ani p{margin:4px 0;font-size:13px}.ani i{font-style:normal;font-weight:600;color:var(--mut)}
.anids{display:flex;flex-wrap:wrap;gap:6px;margin-top:4px}
svg.nsv{position:absolute;left:0;top:0;pointer-events:none;overflow:visible;z-index:3}
svg.nsv .nlab{pointer-events:auto;cursor:pointer}
body.lkdrag svg.nsv .nlab{pointer-events:none}
.blkpop{position:fixed;z-index:70;display:flex;gap:12px;animation:trin .15s ease-out}
.blkpop button{width:44px;height:44px;border-radius:50%;border:2px solid #fff;background:#d33;color:#fff;display:flex;align-items:center;justify-content:center;box-shadow:0 4px 16px rgba(0,0,0,.5);cursor:pointer;padding:0}
.blkpop button.cp{background:#2a6fdb}
.blkpop button.ps{background:#198754}
.blkpop svg{width:24px;height:24px;fill:none;stroke:#fff;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;pointer-events:none}
.shman{display:flex;flex-direction:column;gap:10px;padding:6px 0}
.shman .shtop,.shman .shrow{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.shman .shrow .vn{min-width:5.5em;font-weight:600}
.shman button.shar{width:48px;height:48px;font-size:22px;line-height:1;border-radius:10px}
.shman .shval{min-width:13em;font-variant-numeric:tabular-nums}
.shman .shnote{font-size:15px}
.toastmini{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);z-index:80;background:#222;color:#fff;border-radius:10px;padding:10px 16px;font-size:13px;box-shadow:0 6px 20px rgba(0,0,0,.5);max-width:90vw}
@keyframes trin{from{transform:scale(.4);opacity:0}to{transform:scale(1);opacity:1}}
.lpop{position:fixed;z-index:60;background:var(--card);color:var(--ink);border:1px solid var(--line);border-radius:12px;padding:10px;box-shadow:0 8px 28px rgba(0,0,0,.4);display:flex;flex-direction:column;gap:8px;min-width:270px}
.lpop .lh{font-weight:700;font-size:13px}
.lpop .lr{display:flex;align-items:center;gap:6px}
.lpop .lr select{width:auto;grid-column:auto;padding:4px}
.lpop .lr input{width:70px;padding:4px;border-radius:6px;border:1px solid var(--line);background:var(--card);color:var(--ink)}
.lpop .lr .u,.lpop .lr .ar{color:var(--mut);font-size:12px}
.npan{margin-top:12px;border-top:1px solid var(--line);padding-top:10px}
.npan h4{margin:0 0 6px}
.nrow{display:flex;flex-wrap:wrap;align-items:center;gap:6px;margin:6px 0}
.nrow .nl{color:var(--mut);font-size:12px;min-width:110px}
.chip2{border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:14px;padding:4px 10px;font-size:12px;cursor:pointer}
.npan .hint,.hint{color:var(--mut);font-size:12px}
.ph>button.gb.on{border-color:var(--acc);color:var(--acc)}
.ph>button.gb svg{width:18px;height:18px;stroke:currentColor;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.prebar button:disabled{opacity:.35}
.card h2.ph{display:flex;align-items:center}
.tsel{font-size:16px;font-weight:700;border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:8px;padding:4px 6px;width:auto;grid-column:auto;max-width:280px}
.fxg{margin:8px 0 0;border-top:1px solid var(--line);padding-top:6px}
.fxg summary{cursor:pointer;font-weight:600;font-size:13px;color:var(--mut);padding:2px 0}
.fch{display:flex;flex-direction:column;align-items:center;gap:3px;width:96px;font-size:11px;color:var(--mut);text-align:center}
.fch select{width:100%;font-size:12px;padding:3px;grid-column:auto}
.fro{display:flex;flex-direction:column;align-items:center;gap:2px;width:96px;font-size:11px;color:var(--mut);text-align:center}
.fro b{font-size:13px;color:var(--ink);font-weight:600;font-variant-numeric:tabular-nums}
.fro .bar{width:84px}
.tools{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:0 0 6px}
.tools select{width:auto;grid-column:auto;max-width:200px}
.bk.sel{border-color:#a78bfa;box-shadow:0 0 0 3px rgba(167,139,250,.35)}
.bk.off{opacity:.38}
.bk.spl{background:#14532d;color:#fff;font-weight:700;font-size:15px}
.bk .lb{position:absolute;top:49px;left:50%;transform:translateX(-50%);font-size:10px;line-height:1.15;color:#cfd2d8;width:84px;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;text-align:center;font-weight:400;word-break:break-word}
.bk .tg{position:absolute;top:-8px;right:-8px;font-size:8px;line-height:1;padding:2px 3px;border-radius:5px;background:#374151;color:#e5e7eb;font-weight:600}
.bk .tg.st{background:#0e7490;color:#fff}
.fk{display:flex;flex-direction:column;flex:none;position:relative;isolation:isolate}
.fk>.fr{z-index:1}
.fk.svgd .fr::before,.fk.svgd .fr::after{display:none!important}
.fk.svgd .fr>.sl,.fk.svgd .fr>.sr,.fk.svgd .fr>.vl,.fk.svgd .fr>.vr{display:none!important}
.fr{position:relative;display:flex;align-items:center;padding:0 22px;margin:0 0 40px}
.fr:last-child{margin-bottom:0}
.fr::before,.fr::after{content:"";position:absolute;width:var(--u);background:#cfd2d8;top:0;bottom:-40px}
.fr::before{left:0}.fr::after{right:0}
.fr:first-child::before,.fr:first-child::after{top:50%}
.fr:last-child::before,.fr:last-child::after{bottom:50%}
.fk.sep .fr::after{display:none}
.fk.sep .fr{padding-right:8px}
.fr>.sl,.fr>.sr{position:absolute;top:calc(50% - 0.5*var(--u));width:22px;height:var(--u);background:#cfd2d8}
.fr>.sl{left:0}.fr>.sr{right:0}
.fr .rn{position:absolute;left:5px;top:calc(50% - 22px);font-size:10px;color:#9aa0aa}
.pl{width:22px;height:22px;border-radius:50%;border:1px dashed #8a8f99;background:transparent;color:#aeb3bd;cursor:pointer;line-height:1;padding:0;font-size:14px;flex:none}
.pl:hover{border-color:#fff;color:#fff}
.pbs{background:#14151a;color:#e6e6e6;border:1px solid #3a3d46;border-radius:6px;padding:3px;font-size:12px;width:auto;grid-column:auto}
.knobs{display:flex;flex-wrap:wrap;gap:12px;padding:6px 0 2px}
.kn{display:flex;flex-direction:column;align-items:center;width:70px;user-select:none}
.kn svg{width:48px;height:48px}
.kn .kv{font-size:12px;font-variant-numeric:tabular-nums;margin-top:-2px}
.kn .kvin{width:60px;font-size:16px;text-align:center;padding:1px 2px;border:1px solid var(--acc);border-radius:4px;background:var(--bg,transparent);color:var(--ink);user-select:text;-webkit-user-select:text}
.kn .kl{font-size:11px;color:var(--mut);text-align:center}
.ed{margin-top:12px}
.ed h3{font-size:15px;margin:0 0 6px;display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.ed h3 .nm{font-weight:600}
.lib summary{cursor:pointer;font-size:16px;font-weight:600}
.lib .it{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:5px 0;border-top:1px solid var(--line);font-size:14px;word-break:break-all}
.lib button{border:1px solid var(--line);background:var(--card);color:var(--bad);border-radius:8px;padding:4px 10px;font:inherit;cursor:pointer;flex:none}
.lib .tools button{color:var(--ink)}
.lib .tools{margin:8px 0}
.foot{color:var(--mut);font-size:12px;text-align:center;margin-top:8px}
</style></head><body><div class="w">
<h1><span class="dot0" id="dot"></span>NAMberry5 <small style="color:var(--mut);font-weight:400">Circle</small> <small id="fwver" style="color:var(--mut);font-weight:400;font-size:.55em"></small><a id="t_cfg" class="cfgb" href="#config">Configuration</a></h1>
<div class="sub" id="sub">connecting...</div>
<div id="blocks"></div>
<div class="foot">Changes are applied immediately and saved on the board.</div>
</div>
<script>
const T="@@TOKEN@@";
let S=null,sig="",lastOk=0,bind={},mbind=[],sel=null;
function lineVars(){const d=window.devicePixelRatio||1,u=2*Math.max(1,Math.round(d)),g=2*u,r=document.documentElement.style;
  r.setProperty('--u',(u/d)+'px');r.setProperty('--g',(g/d)+'px');r.setProperty('--w',((2*u+g)/d)+'px')}
/* diagram lines in SVG (see patch_stereo_lines12.py): each lane has a single SVG with all the strokes, single or double*/
function drawLines(){
  const cs=getComputedStyle(document.documentElement);
  const u=parseFloat(cs.getPropertyValue('--u'))||2,g=parseFloat(cs.getPropertyValue('--g'))||4,w=2*u+g,PAD=22,d=window.devicePixelRatio||1;
  document.querySelectorAll('.pbi').forEach(pbi=>{
    const P=pbi.getBoundingClientRect(),it=[];
    pbi.querySelectorAll('.ln').forEach(e=>{                                  // strokes between the blocks
      const b=e.getBoundingClientRect();if(!b.width)return;
      const cy=(b.top+b.bottom)/2-P.top;it.push([b.left-P.left,cy,b.right-P.left,cy,e.classList.contains('st'),false,false]);
    });
    pbi.querySelectorAll('.fk').forEach(fk=>{                                 // links of the forks
      const rows=[...fk.querySelectorAll(':scope > .fr')];if(!rows.length)return;
      const F=fk.getBoundingClientRect(),ox=F.left-P.left,oy=F.top-P.top,W=F.width,H=F.height,ym=oy+H/2;
      const sep=fk.classList.contains('sep'),sti=fk.classList.contains('sti'),sto=fk.classList.contains('sto');
      const ys=rows.map(r=>{const b=r.getBoundingClientRect();return (b.top+b.bottom)/2-P.top});
      const st=(r,c)=>{const e=r.querySelector(':scope > .'+c);return !!(e&&e.classList.contains('st'))};
      const xl=ox+u/2,xr=ox+W-u/2,n=ys.length-1;
      it.push([ox,ym,xl,ym,sti,false,true]);                                     // junction with the input stroke
      it.push([xl,ys[0],xl,ys[n],sti,true,true]);                                // left bar
      rows.forEach((r,i)=>it.push([xl,ys[i],ox+PAD,ys[i],st(r,'sl'),true,false]));
      if(!sep){
        it.push([xr,ys[0],xr,ys[n],sto,true,true]);                              // right bar
        rows.forEach((r,i)=>it.push([ox+W-PAD,ys[i],xr,ys[i],st(r,'sr'),false,true]));
        it.push([xr,ym,ox+W,ym,sto,true,false]);                                 // junction with the output stroke
      }
      fk.classList.add('svgd');
    });
    // a single rule for rounding to physical pixels (the +1e-6 makes the results of values that differ only by numerical noise equal)
    const rd=v=>Math.round(v+1e-6),gdev=Math.round(g*d);
    const key=d+"|"+it.map(a=>a.map(v=>typeof v==="number"?v.toFixed(2):(v?1:0)).join(",")).join(";")+"|"+u+"|"+g;
    if(pbi.dataset.k===key)return;
    pbi.dataset.k=key;
    const old=pbi.querySelector(':scope > svg.lsv');if(old)old.remove();
    pbi.insertAdjacentHTML('afterbegin','<svg class="lsv" width="'+pbi.scrollWidth+'" height="'+pbi.scrollHeight+'" shape-rendering="crispEdges"></svg>');
    pbi.classList.add('lsvg');
    const svg=pbi.querySelector(':scope > svg.lsv'),SR=svg.getBoundingClientRect(),X0=SR.left*d,Y0=SR.top*d;     // actual origin of the SVG, in physical pixels
    const tx=v=>((v-X0)/d).toFixed(3),ty=v=>((v-Y0)/d).toFixed(3);
    const q=v=>Math.round(v*4)/4,px=v=>rd(q((P.left+v)*d)),py=v=>rd(q((P.top+v)*d));   // q: centers that differ only by numerical noise (fractions of a pixel) become identical
    const rect=(xa,ya,xb,yb,c)=>'<rect x="'+tx(xa)+'" y="'+ty(ya)+'" width="'+((xb-xa)/d).toFixed(3)+'" height="'+((yb-ya)/d).toFixed(3)+'" fill="'+c+'"/>';
    let gr="",vd="";
    it.forEach(([x1,y1,x2,y2,dd,e1,e2])=>{
      const tdev=Math.round((dd?w:u)*d),hd=tdev/2,kd=gdev/2;
      if(y1===y2){
        const fw=x1<x2,lo=Math.min(x1,x2),hi=Math.max(x1,x2),eLo=fw?e1:e2,eHi=fw?e2:e1;
        const top=rd(q((P.top+y1)*d)-tdev/2),xs=px(lo)-(eLo?0:1),xe=px(hi)+(eHi?0:1);   // 1 physical pixel of overlap at the ends: two collinear strokes that touch never leave a gap
        gr+=rect(xs-(eLo?hd:0),top,xe+(eHi?hd:0),top+tdev,"#cfd2d8");
        if(dd){const vt=top+(tdev-gdev)/2;vd+=rect(xs-(eLo?kd:0),vt,xe+(eHi?kd:0),vt+gdev,"#1b1c20")}
      }else{
        const dn=y1<y2,lo=Math.min(y1,y2),hi=Math.max(y1,y2),eLo=dn?e1:e2,eHi=dn?e2:e1;
        const left=rd(q((P.left+x1)*d)-tdev/2),ys=py(lo),ye=py(hi);
        gr+=rect(left,ys-(eLo?hd:0),left+tdev,ye+(eHi?hd:0),"#cfd2d8");
        if(dd){const vl=left+(tdev-gdev)/2;vd+=rect(vl,ys-(eLo?kd:0),vl+gdev,ye+(eHi?kd:0),"#1b1c20")}
      }
    });
    svg.innerHTML=gr+vd;
  });
}
/* every row of a fork has an even height in physical pixels: its center (where the lines are) falls on a whole pixel*/
function evenRows(){const d=window.devicePixelRatio||1;document.querySelectorAll('.fr').forEach(f=>{f.style.minHeight='';const h=Math.round(f.getBoundingClientRect().height*d);if(h%2)f.style.minHeight=((h+1)/d)+'px'})}
lineVars();window.addEventListener('resize',()=>{lineVars();evenRows();drawLines()});
const $=i=>document.getElementById(i);
const el=(t,c,h)=>{const e=document.createElement(t);if(c)e.className=c;if(h!==undefined)e.innerHTML=h;return e};
const esc=s=>String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
const recent=e=>e.dataset.t&&Date.now()-e.dataset.t<2500;
async function api(path,o){
  const p=new URLSearchParams();p.set("t",T);p.set("pg","1");for(const k in o){let v=o[k];if(typeof v=="number")v=Math.abs(v)<1e-9?"0":(+v.toFixed(6)).toString();p.set(k,v)}
  try{return await (await fetch(path,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:p.toString()})).json()}
  catch(e){return {ok:0,err:"no reply from the Pi"}}
}
const g=o=>api("/g",o);
const warn=async p=>{const r=await p;if(r&&!r.ok)alert(r.err||"error");return r};
function fmt(v,step){const d=step>=1?0:(step>=0.1?1:2);return Number(v).toFixed(d)}
const addBind=(id,f)=>{(bind[id]=bind[id]||[]).push(f)};
function btn(txt,title,fn,cls){const b=el("button","mini "+(cls||""),txt);b.title=title;b.onclick=fn;return b}
function slider(label,min,max,step,unit,send){
  const r=el("div","row");r.appendChild(el("label","",esc(label)));
  const x=el("input");x.type="range";x.min=min;x.max=max;x.step=step;
  const o=el("output");
  x.oninput=()=>{o.textContent=fmt(x.value,step)+" "+unit;x.dataset.t=Date.now()};
  x.onchange=()=>send(parseFloat(x.value));
  r.appendChild(x);r.appendChild(o);
  r.sync=v=>{if(document.activeElement===x||recent(x))return;x.value=v;o.textContent=fmt(v,step)+" "+unit};
  return r;
}
function switchEl(send){
  const w=el("label","sw");const cb=el("input");cb.type="checkbox";
  cb.onchange=()=>{cb.dataset.t=Date.now();send(cb.checked?1:0)};
  w.appendChild(cb);w.appendChild(el("i"));
  w.sync=v=>{if(recent(cb))return;cb.checked=v>0.5};
  return w;
}
function choice(label,opts,send){
  const r=el("div","row");r.appendChild(el("label","",esc(label)));
  const s=el("select");opts.forEach((t,i)=>{const o=el("option","",esc(t));o.value=i;s.appendChild(o)});
  s.onchange=()=>{s.dataset.t=Date.now();send(parseInt(s.value))};
  r.appendChild(s);
  r.sync=v=>{if(document.activeElement===s||recent(s))return;s.value=v};
  return r;
}
/* knob: drag vertically (also with a finger), double click = initial value; arc from the center for bipolar values*/
function polar(cx,cy,r,deg){const a=deg*Math.PI/180;return [cx+r*Math.sin(a),cy-r*Math.cos(a)]}
function knob(label,min,max,step,unit,send,def,o){
  o=o||{};
  const lg=!!o.log&&min>0;
  const w=el("div","kn");
  if(o.hint)w.title=o.hint;
  const NS="http://www.w3.org/2000/svg",sv=document.createElementNS(NS,"svg");
  sv.setAttribute("viewBox","0 0 48 48");
  const mk=(t,at)=>{const e=document.createElementNS(NS,t);for(const k in at)e.setAttribute(k,at[k]);sv.appendChild(e);return e};
  mk("path",{d:(()=>{const p0=polar(24,24,18,-135),p1=polar(24,24,18,135);return "M"+p0[0]+" "+p0[1]+"A18 18 0 1 1 "+p1[0]+" "+p1[1]})(),fill:"none",stroke:"var(--line)","stroke-width":4,"stroke-linecap":"round"});
  const val=mk("path",{fill:"none",stroke:"var(--acc)","stroke-width":4,"stroke-linecap":"round"});
  const ptr=mk("line",{x1:24,y1:24,x2:24,y2:10,stroke:"var(--ink)","stroke-width":3,"stroke-linecap":"round"});
  const txt=el("div","kv"),lb=el("div","kl",esc(label));
  w.appendChild(sv);w.appendChild(txt);w.appendChild(lb);
  let cur=def===undefined?(min+max)/2:def;
  const tOf=v=>lg?Math.log(v/min)/Math.log(max/min):(v-min)/(max-min);
  const vOf=x=>lg?min*Math.pow(max/min,x):min+x*(max-min);
  const deg=v=>-135+270*tOf(v);
  const draw=v=>{
    cur=Math.max(min,Math.min(max,v));const a=deg(cur),z=deg(min<0&&max>0?0:min);
    const p=polar(24,24,18,a),q=polar(24,24,18,z),big=Math.abs(a-z)>180?1:0;
    val.setAttribute("d",Math.abs(a-z)<0.5?"":"M"+q[0]+" "+q[1]+"A18 18 0 "+big+" "+(a>z?1:0)+" "+p[0]+" "+p[1]);
    const t=polar(24,24,14,a);ptr.setAttribute("x2",t[0]);ptr.setAttribute("y2",t[1]);
    txt.textContent=(cur>0&&min<0?"+":"")+fmt(cur,step)+(unit?" "+unit:"");
  };
  let y0=null,t0=0;
  sv.style.touchAction="none";sv.style.cursor="ns-resize";
  sv.onpointerdown=e=>{y0=e.clientY;t0=tOf(cur);sv.setPointerCapture(e.pointerId);w.dataset.t=Date.now();e.preventDefault()};
  sv.onpointermove=e=>{if(y0===null)return;const f=e.shiftKey?0.25:1;let x=t0+(y0-e.clientY)/150*f;x=Math.max(0,Math.min(1,x));let v=vOf(x);v=Math.round(v/step)*step;v=Math.max(min,Math.min(max,+v.toFixed(6)));if(v!==cur)draw(v);w.dataset.t=Date.now()};
  sv.onpointerup=e=>{if(y0===null)return;y0=null;w.dataset.t=Date.now();send(+cur.toFixed(4))};
  sv.ondblclick=()=>{const dv=o.dbl?o.dbl():undefined;draw(dv!==undefined&&isFinite(dv)?dv:(def===undefined?(min+max)/2:def));w.dataset.t=Date.now();send(+cur.toFixed(4))};
  /* touching the number lets you type the value with the keyboard: Enter or clicking outside the field confirms, Esc cancels; the value is limited to the range and snapped to the step*/
  let ed=false;
  txt.style.cursor="text";txt.title="Tap to type the value";
  txt.onclick=ev=>{
    ev.stopPropagation();if(ed)return;ed=true;
    const inp=el("input","kvin");inp.type="text";inp.inputMode="decimal";inp.value=fmt(cur,step);
    txt.textContent="";txt.appendChild(inp);inp.focus();inp.select();w.dataset.t=Date.now();
    let done=false;
    const fin=ok=>{
      if(done)return;done=true;ed=false;
      let v=ok?parseFloat(String(inp.value).replace(",",".")):NaN;
      if(isFinite(v)){v=Math.max(min,Math.min(max,v));v=Math.round(v/step)*step;v=Math.max(min,Math.min(max,+v.toFixed(6)));draw(v);w.dataset.t=Date.now();send(+cur.toFixed(4))}
      else draw(cur);
    };
    inp.onkeydown=e=>{if(e.key=="Enter")fin(true);else if(e.key=="Escape")fin(false);e.stopPropagation()};
    inp.onblur=()=>fin(true);
    inp.onpointerdown=e=>e.stopPropagation();
  };
  w.sync=v=>{if(y0!==null||ed||recent(w))return;draw(v)};
  draw(cur);
  return w;
}
/* value of a parameter in the saved setting linked to the block (for double click on the knobs)*/
function presetVal(id,key){
  const l=locate(id);if(!l||!l.n.pr)return undefined;
  const b=(S.bp||[]).find(x=>x.n==l.n.pr);if(!b)return undefined;
  const e=b.d.split("|").slice(1).map(x=>x.split("=")).find(x=>x[0]==key);
  return e?parseFloat(e[1]):undefined;
}
function modelRow(n){
  const r=el("div","row fr2");r.appendChild(el("label","","Model"));
  const s=el("select");const fav=new Set(S.fav);
  const og=(t,list)=>{if(!list.length)return;const x=el("optgroup");x.label=t;list.forEach(f=>{const o=el("option","",esc(f));o.value=f;x.appendChild(o)});s.appendChild(x)};
  og("★ Favorites",S.lib.filter(f=>fav.has(f)));og("All models",S.lib.filter(f=>!fav.has(f)));
  const st=el("button","ico");st.title="Favorite";
  const star=()=>{const on=fav.has(s.value);st.textContent=on?"★":"☆";st.className="ico"+(on?" fv":"")};
  s.onchange=()=>{s.dataset.t=Date.now();star();warn(g({op:"file",id:n.id,s:s.value}))};
  st.onclick=()=>api("/fav",{s:s.value,v:fav.has(s.value)?0:1});
  const dl=el("button","ico","\u{1F5D1}");dl.title="Delete from the board";
  dl.onclick=async()=>{
    const f=s.value;
    if(S.lib.length<2){alert("It is the only model: it cannot be deleted");return}
    if(!confirm("Delete "+f+" from the board?\nThe block will move to the next model. This cannot be undone."))return;
    const i=S.lib.indexOf(f),nx=S.lib[i+1]||S.lib[i-1];
    s.dataset.t=Date.now();await g({op:"file",id:n.id,s:nx});s.value=nx;star();
    const j=await api("/delete",{s:f});
    if(!j.ok){alert(j.err||"error");await g({op:"file",id:n.id,s:f});s.value=f;star()}
  };
  const ac=el("div","acts");ac.appendChild(st);ac.appendChild(dl);
  r.appendChild(s);r.appendChild(ac);
  r.sync=nn=>{if(document.activeElement===s||recent(s))return;s.value=nn.file;star()};
  s.value=n.file;star();
  return r;
}
/* ---- pedalboard ----*/
const TNAME={nam:"NAM",vol:"Volume",pan:"Pan"};
let FXD=[];
const fxd=k=>FXD.find(d=>d.fx==k);
const fxShort=d=>String(d.title).split(" (")[0];
const nmOf=n=>n.t=="fx"?(fxd(n.fx)?fxShort(fxd(n.fx)):n.fx):(n.t=="vol"&&n.rt?"Routing":TNAME[n.t]);
const addOpts=depth=>[["nam","NAM"],["vol","Volume"],["rout","Routing (routing only)"],["pan","Pan (stereo)"]].concat(FXD.filter(d=>!d.hid).map(d=>[d.fx,d.title]).sort((a,b)=>fxSortKey(a[1]).localeCompare(fxSortKey(b[1]),"en",{sensitivity:"base"})));
/* effects in alphabetical order in the choice menus: the "ADEL" and "ToTest - " prefixes are ignored for the order (the text shown stays the same)*/
function fxSortKey(t){return String(t).replace(/^ToTest - /,"").replace(/^ADEL\s*/i,"")}
const fxMixIdx=n=>{const d=fxd(n.fx);return d?d.p.findIndex(p=>p.k=="mix"):-1};
/* ToTest effects (fx_extra.cpp): they keep a mono input mono, except these four, which make the output stereo*/
const XFX=["tsdrive","muff","fuzzface","tremolo","harmtrem","autowah","octaver","noisegate","univibe","flanger","rotary","tapeecho","booster","tonestack","swell","widener","vibrato","centaur","bluesdrv","ds1","ocd","guvnor","rangemaster","fz1","bender","fuzzfactory","phase90","smallstone","ce1","ce2","ad9","mflanger","dimension","autopan","slicer","crybaby","womantone","geq6","dynacomp","optocomp","limiter","analogdelay","duckdelay","sustainer","selector","tubescreamer","ratts","opticalla"];
const fxStereoOut=n=>["adelverb","adeldelay","adelspring","adelchorus","adelshifter","rotary","widener","dimension","autopan"].includes(n.fx);
const GL={
  io_in:'<svg viewBox="0 0 24 24"><path d="M3 12h13M12 7l5 5-5 5M20 5v14"/></svg>',
  io_out:'<svg viewBox="0 0 24 24"><path d="M4 5v14M8 12h13M17 7l5 5-5 5" transform="translate(-2 0)"/></svg>',
  nam:'<svg viewBox="0 0 24 24"><rect x="3" y="6" width="18" height="12" rx="2"/><circle cx="8" cy="12" r="1.4"/><circle cx="12" cy="12" r="1.4"/><circle cx="16" cy="12" r="1.4"/></svg>',
  vol:'<svg viewBox="0 0 24 24"><path d="M4 9v6h4l5 4V5L8 9H4z"/><path d="M16.5 9a4 4 0 010 6"/></svg>',
  pan:'<svg viewBox="0 0 24 24"><path d="M3 12h18M7 8l-4 4 4 4M17 8l4 4-4 4"/></svg>',
  rout:'<svg viewBox="0 0 24 24"><path d="M3 12h6M9 12c4 0 4-6 8-6h4M9 12c4 0 4 6 8 6h4M19 4l2 2-2 2M19 16l2 2-2 2"/></svg>',
  fx:'<svg viewBox="0 0 24 24"><path d="M2 12c2-7 4-7 6 0s4 7 6 0 4-7 6 0 1 3 2 3"/></svg>',
  reverb:'<svg viewBox="0 0 24 24"><path d="M3 12h2M7 7v10M11 4v16M15 8v8M19 10v4"/></svg>',
  delay:'<svg viewBox="0 0 24 24"><path d="M3 5v14M8 8v8M13 10.5v3M18 11.5v1"/></svg>',
  compressor:'<svg viewBox="0 0 24 24"><path d="M3 6h5l3 4M3 18h5l3-4M21 6h-5l-3 4M21 18h-5l-3-4M11 12h2"/></svg>',
      adelspring:'<svg viewBox="0 0 24 24"><path d="M4 12c0-4 4-4 4 0s4 4 4 0 4-4 4 0 4 4 4 0" /><path d="M2 12h2M20 12h2"/></svg>',
    adelchorus:'<svg viewBox="0 0 24 24"><path d="M3 9c2-4 4-4 6 0s4 4 6 0 4-4 6 0"/><path d="M3 15c2-4 4-4 6 0s4 4 6 0 4-4 6 0" opacity=".55"/></svg>',
  adelsendsweep:'<svg viewBox="0 0 24 24"><path d="M7 5l11 7-11 7z"/></svg>',
  adelequal:'<svg viewBox="0 0 24 24"><path d="M6 4v16M12 4v16M18 4v16"/><circle cx="6" cy="14" r="2" fill="currentColor"/><circle cx="12" cy="8" r="2" fill="currentColor"/><circle cx="18" cy="12" r="2" fill="currentColor"/></svg>',
  cabir:'<svg viewBox="0 0 24 24"><rect x="5" y="3" width="14" height="18" rx="2"/><circle cx="12" cy="13" r="4.5"/><circle cx="12" cy="13" r="1.2"/><path d="M8 6.5h8"/></svg>',
  adelphase:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="7"/><path d="M5 19L19 5"/></svg>',
  phaser:'<svg viewBox="0 0 24 24"><path d="M2 12c2-8 4-8 6 0s4 8 6 0 4-8 6 0"/><path d="M2 17c2-5 4-5 6 0s4 5 6 0 4-5 6 0" opacity=".5"/></svg>'
};
GL.adeldelaym=GL.delay;GL.adelverbm=GL.reverb;
/* icons of the ToTest blocks: distortions (clipped wave), modulations (waves), delays, filters*/
GL.tsdrive=GL.muff=GL.fuzzface=GL.booster='<svg viewBox="0 0 24 24"><path d="M3 18l4-12h4v12h4V6h4l2 12"/></svg>';
GL.tremolo=GL.harmtrem=GL.univibe=GL.flanger=GL.vibrato=GL.rotary=GL.adelchorus;
GL.tapeecho=GL.delay;GL.noisegate=GL.swell=GL.compressor;GL.autowah=GL.tonestack=GL.adelequal;GL.octaver=GL.adelshifter;GL.widener=GL.pan;
GL.centaur=GL.bluesdrv=GL.ds1=GL.ocd=GL.guvnor=GL.rangemaster=GL.fz1=GL.bender=GL.fuzzfactory=GL.tubescreamer=GL.ratts=GL.tsdrive;GL.opticalla=GL.compressor;
GL.phase90=GL.smallstone=GL.phaser;GL.ce1=GL.ce2=GL.dimension=GL.vibrato;GL.autopan=GL.slicer=GL.widener;GL.crybaby=GL.womantone=GL.geq6=GL.adelequal;
GL.dynacomp=GL.limiter=GL.sustainer=GL.compressor;GL.analogdelay=GL.duckdelay=GL.ad9=GL.delay;GL.mflanger=GL.flanger;
GL.selector=GL.rout;
GL.adelshifter='<svg viewBox="0 0 24 24"><path d="M4 18h5l3-12 3 12h5"/><path d="M18 5l2 2-2 2" /></svg>';
function locate(id){if(S.board.net){if(id<=-11)return {n:{id},arr:[],i:0,chain:"net",net:true,io:true};const c=S.board.net.cells.find(q=>q.b[0].id==id);return c?{arr:c.b,i:0,n:c.b[0],chain:"net",net:true}:null}return null}
function lbl(n){
  if(n.t=="nam")return n.loaded?n.name:n.status;
  if(n.t=="fx"){if(n.fx=="cabir"&&n.file)return n.file.replace(/\.air$/i,"");return nmOf(n)}
  if(n.t=="vol")return n.rt?(n.vol==0?"Routing":"Routing "+Number(n.vol).toFixed(1)+" dB"):"Vol "+Number(n.vol).toFixed(1)+" dB";
  if(n.t=="pan")return n.pan==0?"Pan center":"Pan "+(n.pan<0?"L ":"R ")+Math.abs(n.pan)+"%";
return "";
}
function tileHtml(n,mono){
  const inner=n.t=="fx"?(GL[n.fx]||GL[String(n.fx).replace("adel","").replace("comp","compressor")]||GL.fx):GL[n.t=="vol"&&n.rt?"rout":n.t];
  return inner+'<span class="lb">'+esc(lbl(n))+'</span><span class="tg'+(mono?"":" st")+'">'+(mono?"M":"ST")+"</span>";
}
let drag=null,dndSkip=false;
function isOff(n){return !n.on}
/* tile color by effect family*/
const REVERBS=["adelverb","adelverbm","adelspring","reverb"];
const DELAYS=["adeldelay","adeldelaym","delay","tapeecho","analogdelay","duckdelay"];
const DRIVES=["tsdrive","tubescreamer","ratts","muff","fuzzface","centaur","bluesdrv","ds1","ocd","guvnor","rangemaster","fz1","bender","fuzzfactory","booster"];
const catCls=n=>n.t!="fx"?"":(REVERBS.includes(n.fx)?" cg":(DELAYS.includes(n.fx)?" cb":(DRIVES.includes(n.fx)?" co":"")))
/* settings saved on the board*/
const typeKey=n=>n.t=="fx"?"fx:"+n.fx:n.t;
function pairsOf(n){
  if(n.t=="nam")return [["in",n.in],["out",n.out],["bass",n.bass||0],["mid",n.mid||0],["treb",n.treb||0]];
  if(n.t=="vol")return [["vol",n.vol]];
  if(n.t=="pan")return [["pan",n.pan]];
  if(n.t=="fx"){const d=fxd(n.fx);return d?d.p.map((p,k)=>[p.k,n.v[k],p.kind]).filter(x=>x[2]!=3).map(x=>[x[0],x[1]]):[]}
  return [];
}
async function applyPairs(id,s){
  let warnMsg="";
  if((s.t=="nam"||s.fx=="cabir")&&s.file){const r=await g({op:"file",id,s:s.file});if(!r.ok)warnMsg=(r.err||"model not available")+" ("+s.file+")"}
  for(const [k,v] of s.p){const r=await g({op:"set",id,k,v});if(!r.ok)return r}
  if(s.on===0)await g({op:"set",id,k:"on",v:0});
  return {ok:1,warn:warnMsg};
}
const bpOf=n=>(S.bp||[]).filter(b=>b.d.split("|")[0]==typeKey(n));
const bpData=n=>typeKey(n)+"|"+pairsOf(n).map(([k,v])=>k+"="+(+Number(v).toFixed(2))).join("|")+(n.t=="nam"&&n.file?"|file="+n.file:"");
async function bpApply(n,d){
  const s={t:n.t,p:[]};
  d.split("|").slice(1).forEach(x=>{const i=x.indexOf("=");const k=x.slice(0,i),v=x.slice(i+1);if(k=="file")s.file=v;else s.p.push([k,parseFloat(v)])});
  const q=await applyPairs(n.id,s);
  if(!q.ok)alert(q.err||"error");else if(q.warn)alert(q.warn);
}
function toolsRow(n,info){
  const w=el("div","tools");
  const sl=el("select","");sl.title="Saved settings of this block type";
  const o0=el("option","","Saved settings...");o0.value="";sl.appendChild(o0);
  bpOf(n).forEach(b=>{const o=el("option","",esc(b.n));o.value=b.n;sl.appendChild(o)});
  sl.onchange=async()=>{const b=bpOf(n).find(x=>x.n==sl.value);if(b)await bpApply(n,b.d);await g({op:"bpset",id:n.id,s:b?b.n:""})};
  const sv=btn("\u{1F4BE}","Save the settings of this block with a name",async()=>{
    const nm=prompt("Name for the settings of this block:",sl.value||"");
    if(!nm||!nm.trim())return;
    const name=nm.trim();
    if((S.bp||[]).some(b=>b.n==name)&&!confirm("Replace the existing '"+name+"': replace it?"))return;
    const r=await api("/bp",{op:"save",n:name,d:bpData(n)});warn(r);if(r.ok)await g({op:"bpset",id:n.id,s:name});
  });
  const dl=btn("\u{1F5D1}","Delete the selected settings",async()=>{
    if(!sl.value)return;
    if(confirm("Delete the settings '"+sl.value+"'?"))warn(await api("/bp",{op:"del",n:sl.value}));
  });
  const rv=btn("\u21BA","Restore the saved values of these settings",()=>{const c=bpOf(n).find(b=>b.n==sl.value);if(c)bpApply(n,c.d)});
  rv.style.display="none";
  w.appendChild(sl);w.appendChild(rv);w.appendChild(sv);w.appendChild(dl);
  /* the name of the setting in use stays selected; an asterisk indicates values that were changed and not yet saved*/
  const refresh=nn=>{
    if(document.activeElement===sl)return;
    Array.from(sl.options).forEach(o=>{o.textContent=o.value||"Saved settings..."});
    const cur=bpOf(nn).find(b=>b.n==nn.pr);
    if(cur){
      const dirty=bpData(nn)!==cur.d;
      sl.value=cur.n;
      if(dirty)sl.selectedOptions[0].textContent=cur.n+" *";
      rv.style.display=dirty?"":"none";
    }else{sl.value="";rv.style.display="none"}
  };
  refresh(n);addBind(n.id,refresh);
  return w;
}
/* link line: single if the signal is mono, double (L and R) if it is stereo*/
const ln=(mono,x)=>el("span","ln"+(mono?"":" st")+(x?" "+x:""));
function editor(info){
  const n=info.n,c=el("div","card ed");
  const h=el("h3","");
  const sw=switchEl(v=>g({op:"set",id:n.id,k:"on",v}));h.appendChild(sw);addBind(n.id,nn=>sw.sync(nn.on));
  {
    /* the block type is a menu: choosing another one replaces the block (in the same position)*/
    const cur=n.t=="fx"?n.fx:(n.t=="vol"&&n.rt?"rout":n.t),ts=el("select","tsel");ts.title="Block type: choosing another one replaces this block";
    const opts=addOpts(0);
    if(n.t=="fx"&&!opts.some(o=>o[0]==cur)){const d=fxd(cur);opts.push([cur,d?d.title:cur])}
    opts.forEach(([v,tx])=>{const o=el("option","",esc(tx));o.value=v;ts.appendChild(o)});
    ts.value=cur;
    ts.onchange=async()=>{
      const k=ts.value;if(k==cur)return;
      const r=await g({op:"ty",id:n.id,k});if(!r.ok){alert(r.err||"error");ts.value=cur}
    };
    h.appendChild(ts);
  }
  h.appendChild(el("span","grow"));
  h.appendChild(btn("✕","Remove the block",()=>{if(confirm("Remove the block?")){sel=null;g({op:"del",id:n.id}).then(warn)}},"del"));
  c.appendChild(h);
  c.appendChild(toolsRow(n,info));
  if(n.t=="nam"){
    const m=modelRow(n);c.appendChild(m);addBind(n.id,nn=>m.sync(nn));
    const inf=el("div","info");c.appendChild(inf);
    addBind(n.id,nn=>{inf.textContent=nn.loaded?nn.name:nn.status;inf.className="info"+(nn.loaded?"":" bad")});
    const kb=el("div","knobs");c.appendChild(kb);const a=knob("Input",-24,12,0.5,"dB",v=>g({op:"set",id:n.id,k:"in",v}),0,{dbl:()=>presetVal(n.id,"in")});kb.appendChild(a);addBind(n.id,nn=>a.sync(nn.in));
    const b=knob("Output",-24,12,0.5,"dB",v=>g({op:"set",id:n.id,k:"out",v}),0,{dbl:()=>presetVal(n.id,"out")});kb.appendChild(b);addBind(n.id,nn=>b.sync(nn.out));
    for(const [lab,key] of [["Bass","bass"],["Mid","mid"],["Treble","treb"]]){const kt=knob(lab,-12,12,0.5,"dB",v=>g({op:"set",id:n.id,k:key,v}),0,{dbl:()=>presetVal(n.id,key)});kb.appendChild(kt);addBind(n.id,nn=>kt.sync(nn[key]||0))}
    const ro=el("div","row tog");ro.appendChild(el("label","","Use the calibration offset (Input panel)"));
    const ws=switchEl(v=>g({op:"set",id:n.id,k:"noofs",v:v?0:1}));ro.appendChild(ws);c.appendChild(ro);addBind(n.id,nn=>ws.sync(nn.noofs?0:1));
  }else if(n.t=="fx"){
    fxEditor(c,n);
  }else if(n.t=="vol"){
    const kb=el("div","knobs");const a=knob(n.rt?"Level":"Volume",-40,12,0.5,"dB",v=>g({op:"set",id:n.id,k:"vol",v}),0,{dbl:()=>presetVal(n.id,"vol")});kb.appendChild(a);c.appendChild(kb);addBind(n.id,nn=>a.sync(nn.vol));
    if(n.rt)c.appendChild(el("div","info","Routing passes the signal on without changing it (0 dB): it is used to split an arrow into several paths or to merge several. The level is an optional touch-up."));
  }else if(n.t=="pan"){
    const kb=el("div","knobs");const a=knob("Position",-100,100,1,"%",v=>g({op:"set",id:n.id,k:"pan",v}),0,{dbl:()=>presetVal(n.id,"pan")});kb.appendChild(a);c.appendChild(kb);addBind(n.id,nn=>a.sync(nn.pan));
    c.appendChild(el("div","info","-100 = fully left, +100 = fully right. It makes the path stereo."));
  }
  if(info.net)c.appendChild(netPanel(n));
  return c;
}
/* input and output panels: they open by touching the IN and OUT dots of the pedalboard*/
/* pedalboard presets: drop-down menu (with an asterisk if modified) and Save, Save as, Delete icons*/
const IC_SAVE='<svg viewBox="0 0 24 24"><path d="M5 3h12l4 4v14H5z"/><path d="M8 3v6h8V3M8 21v-7h8v7"/></svg>';
const IC_SAVEAS='<svg viewBox="0 0 24 24"><path d="M4 3h10l4 4v5M4 3v18h9"/><path d="M7 3v5h6V3M7 21v-6h5"/><path d="M18 15v6M15 18h6"/></svg>';
const modeIc=m=>IC_GRID;
const IC_GRID='<svg viewBox="0 0 24 24"><rect x="4" y="4" width="7" height="7" rx="1.2"/><rect x="13" y="4" width="7" height="7" rx="1.2"/><rect x="4" y="13" width="7" height="7" rx="1.2"/><rect x="13" y="13" width="7" height="7" rx="1.2"/></svg>';
const IC_DEL='<svg viewBox="0 0 24 24"><path d="M4 7h16M9 7V4h6v3M6 7l1 14h10l1-14M10 11v6M14 11v6"/></svg>';
/* ---- models: .nam -> .adn conversion in the page (like nam2adn.py), upload to the Pi in pieces, TONE3000 selector ----*/
const b64u=u8=>{let s="";for(let i=0;i<u8.length;i++)s+=String.fromCharCode(u8[i]);return btoa(s).replace(/\+/g,"-").replace(/\//g,"_").replace(/=+$/,"")};
function sha256(m){
  const pr=[];for(let n=2;pr.length<64;n++){let ok=true;for(const p of pr)if(n%p==0){ok=false;break}if(ok)pr.push(n)}
  const fr=x=>Math.floor((x-Math.floor(x))*4294967296)>>>0;
  const K=pr.map(p=>fr(Math.cbrt(p))),h=pr.slice(0,8).map(p=>fr(Math.sqrt(p)));
  const l=m.length,nb=(l+9+63)>>6,buf=new Uint8Array(nb*64);buf.set(m);buf[l]=128;
  const dv=new DataView(buf.buffer);dv.setUint32(nb*64-4,(l*8)>>>0);dv.setUint32(nb*64-8,Math.floor(l/536870912));
  const w=new Array(64);
  for(let i=0;i<nb;i++){
    for(let q=0;q<16;q++)w[q]=dv.getUint32(i*64+q*4);
    for(let q=16;q<64;q++){const a=w[q-15],b=w[q-2];w[q]=(w[q-16]+(((a>>>7|a<<25)^(a>>>18|a<<14)^(a>>>3)))+w[q-7]+(((b>>>17|b<<15)^(b>>>19|b<<13)^(b>>>10))))|0}
    let [a,b,c,d,e,f,g,hh]=h;
    for(let q=0;q<64;q++){
      const t1=(hh+((e>>>6|e<<26)^(e>>>11|e<<21)^(e>>>25|e<<7))+((e&f)^(~e&g))+K[q]+w[q])|0;
      const t2=(((a>>>2|a<<30)^(a>>>13|a<<19)^(a>>>22|a<<10))+((a&b)^(a&c)^(b&c)))|0;
      hh=g;g=f;f=e;e=(d+t1)|0;d=c;c=b;b=a;a=(t1+t2)|0;
    }
    h[0]=(h[0]+a)|0;h[1]=(h[1]+b)|0;h[2]=(h[2]+c)|0;h[3]=(h[3]+d)|0;h[4]=(h[4]+e)|0;h[5]=(h[5]+f)|0;h[6]=(h[6]+g)|0;h[7]=(h[7]+hh)|0;
  }
  const o=new Uint8Array(32),ov=new DataView(o.buffer);h.forEach((v,i)=>ov.setUint32(i*4,v>>>0));return o;
}
function crc32(u8){
  if(!crc32.t){crc32.t=new Uint32Array(256);for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;crc32.t[n]=c>>>0}}
  let c=0xFFFFFFFF;for(let i=0;i<u8.length;i++)c=crc32.t[(c^u8[i])&255]^(c>>>8);return (c^0xFFFFFFFF)>>>0;
}
const safeName=s=>(String(s).normalize("NFD").replace(/[\u0300-\u036f]/g,"").replace(/[^A-Za-z0-9_-]+/g,"_").replace(/^_+|_+$/g,"").slice(0,40).replace(/_+$/,""))||"model";
/* same conversion as nam2adn.py: ALWAYS the largest submodel, only A2 models in the form the engine knows*/
function namToAdn(txt,stem){
  const fail=m=>{throw new Error(m)};
  const d=JSON.parse(txt),md=d.metadata||{};
  let m;
  if(d.architecture=="SlimmableContainer"){m=d.config.submodels.reduce((a,b)=>b.max_value>a.max_value?b:a).model}
  else if(d.architecture=="WaveNet")m=d;
  else fail("unsupported architecture: "+d.architecture);
  if(m.architecture!="WaveNet")fail("the submodel is not a WaveNet");
  const cfg=m.config,layers=cfg.layers;
  if(layers.length!=1)fail("not an A2 model (layers: "+layers.length+")");
  const L=layers[0],C=L.channels,n=L.kernel_sizes.length;
  if(n!=L.dilations.length||n!=L.activation.length)fail("lists of different length");
  if(L.bottleneck!=C||L.input_size!=1||L.condition_size!=1)fail("not an A2 form (bottleneck/input/condition)");
  for(const a of L.activation)if(a.type!="LeakyReLU"||Math.abs((a.negative_slope||0)-0.01)>1e-9)fail("unsupported activation");
  if(L.gating_mode.some(g=>g!="none")||L.secondary_activation.some(s=>s!==null))fail("unsupported gating");
  if((L.head1x1&&L.head1x1.active)||!(L.layer1x1&&L.layer1x1.active)||(L.layer1x1.groups||1)!=1)fail("head1x1/layer1x1 not in the A2 form");
  if(L.head.out_channels!=1||!L.head.bias)fail("head not in the A2 form");
  for(const k in L)if(k.endsWith("_film")&&L[k]&&typeof L[k]=="object"&&L[k].active)fail("FiLM active");
  if(cfg.head!==null&&cfg.head!==undefined)fail("unsupported post-stack head");
  const HK=L.head.kernel_size,K=L.kernel_sizes,D=L.dilations,w=m.weights;
  let expect=C+HK*C+2;K.forEach(k=>{expect+=k*C*C+C+C+C*C+C});
  if(w.length!=expect)fail("number of weights "+w.length+", expected "+expect);
  let pos=0;const take=c=>{const r=w.slice(pos,pos+c);pos+=c;return r};
  const P=[];const push=a=>{for(const x of a)P.push(x)};
  push(take(C));
  for(const k of K){
    const raw=take(C*C*k),conv=new Array(k*C*C);let ix=0;
    for(let i=0;i<C;i++)for(let j=0;j<C;j++)for(let q=0;q<k;q++)conv[q*C*C+j*C+i]=raw[ix++];
    push(conv);push(take(C));push(take(C));
    const r2=take(C*C),l1=new Array(C*C);ix=0;
    for(let i=0;i<C;i++)for(let j=0;j<C;j++)l1[j*C+i]=r2[ix++];
    push(l1);push(take(C));
  }
  const rh=take(HK*C),head=new Array(HK*C);let ix=0;
  for(let j=0;j<C;j++)for(let q=0;q<HK;q++)head[q*C+j]=rh[ix++];
  push(head);push(take(1));
  const hs=take(1)[0];
  if(pos!=w.length)fail("excess weights");
  const num=x=>(x===null||x===undefined)?NaN:Number(x);
  const nm=new TextEncoder().encode(String(md.name||stem||"model")).slice(0,63);
  const size=4+20+24+64+8*n+4*P.length+4,out=new Uint8Array(size),dv=new DataView(out.buffer);
  out.set([65,68,78,49],0);
  [1,C,n,HK,0].forEach((v,i)=>dv.setUint32(4+i*4,v,true));
  [0.01,d.sample_rate===undefined?48000:Number(d.sample_rate),hs,num(md.loudness),num(md.input_level_dbu),num(md.output_level_dbu)].forEach((v,i)=>dv.setFloat32(24+i*4,v,true));
  out.set(nm,48);
  let o=112;K.forEach(v=>{dv.setUint32(o,v,true);o+=4});D.forEach(v=>{dv.setUint32(o,v,true);o+=4});
  P.forEach(v=>{dv.setFloat32(o,v,true);o+=4});
  dv.setUint32(o,crc32(out.subarray(0,o)),true);
  return out;
}
const T3K_ID="t3k_pub__Xqich601f7cukGqENOtHxDpfu7j_4_L",T3K_API="https://www.tone3000.com/api/v1";
function t3Msg(h){let c=$("t3");if(!c){c=el("div","card");c.id="t3";const r=$("blocks");r.parentNode.insertBefore(c,r)}c.innerHTML=h;return c}
async function upload(name,bytes,prog){
  if(bytes.length>262144&&name.endsWith(".adn"))throw new Error("model too large for the Pi (maximum 256 KB)");
  const N=1380;
  for(let o=0;o<bytes.length;o+=N){
    const r=await api("/up",{n:name,o,z:bytes.length,d:b64u(bytes.subarray(o,o+N))});
    if(!r.ok)throw new Error(r.err||"upload error");
    if(prog)prog(Math.min(bytes.length,o+N),bytes.length);
  }
  for(let i=0;i<60;i++){
    await new Promise(r=>setTimeout(r,250));
    try{const j=await (await fetch("/status?_="+Date.now(),{cache:"no-store"})).json();if(j.upl&&j.upl.n==name&&j.upl.ok){if(j.upl.ok==1)return;throw new Error(j.upl.e||"rejected by the Pi")}}catch(e){if(e.message&&e.message!="Failed to fetch")throw e}
  }
  throw new Error("no confirmation from the Pi");
}
async function installModel(name,adn,inf,say){
  if((S.lib||[]).includes(name+".adn")&&!confirm("Replace "+name+".adn already in the library?"))return false;
  await upload(name+".adn",adn,(a,b)=>say("Sent to the Pi: "+Math.round(100*a/b)+"%"));
  if(inf)await upload(name+".inf",new TextEncoder().encode(inf));
  return true;
}
async function loadLocal(file){
  const say=h=>t3Msg(h+"<div class=\"info\">Powered by <a href=\"https://www.tone3000.com\" target=\"_blank\" rel=\"noopener\">TONE3000</a></div>");
  try{
    const stem=file.name.replace(/\.[^.]*$/,""),name=safeName(stem);
    say("Reading "+esc(file.name)+"...");
    let adn;
    if(/\.adn$/i.test(file.name))adn=new Uint8Array(await file.arrayBuffer());
    else adn=namToAdn(await file.text(),stem);
    if(await installModel(name,adn,"",say))t3Msg("Uploaded <b>"+esc(name)+".adn</b> ("+(adn.length>>10)+" KB). You will find it in the library.");
    else $("t3").remove();
  }catch(e){t3Msg("<b>Failed:</b> "+esc(e.message||e)+" <button class=\"mini\" onclick=\"$('t3').remove()\">Close</button>")}
}
function t3kStart(){
  const rnd=n=>{const a=new Uint8Array(n);crypto.getRandomValues(a);return b64u(a)};
  const ver=rnd(32),st=rnd(16);
  try{sessionStorage.setItem("t3k_v",ver);sessionStorage.setItem("t3k_s",st)}catch(e){alert("The browser does not allow keeping the temporary data of the link");return}
  const q=new URLSearchParams({client_id:T3K_ID,redirect_uri:location.origin+"/",response_type:"code",code_challenge:b64u(sha256(new TextEncoder().encode(ver))),code_challenge_method:"S256",state:st,prompt:"select_tone",format:"nam",architecture:"2"});
  location.href=T3K_API+"/oauth/authorize?"+q;
}
async function t3kCallback(){
  const q=new URLSearchParams(location.search);
  let st=null,ver=null;
  try{st=sessionStorage.getItem("t3k_s");ver=sessionStorage.getItem("t3k_v");sessionStorage.removeItem("t3k_s");sessionStorage.removeItem("t3k_v")}catch(e){}
  if(!st||!(q.has("state")||q.has("error")))return;
  history.replaceState(null,"",location.pathname+location.hash);
  const attr="<div class=\"info\">Powered by <a href=\"https://www.tone3000.com\" target=\"_blank\" rel=\"noopener\">TONE3000</a></div>";
  const close=" <button class=\"mini\" onclick=\"$('t3').remove()\">Close</button>";
  if(q.get("state")!==st){t3Msg("<b>Invalid TONE3000 link</b> (different check code)."+close);return}
  if(q.get("error")||!q.get("code")){t3Msg("TONE3000: "+esc(q.get("error")||"operation cancelled")+"."+close);return}
  try{
    t3Msg("Connecting to TONE3000...");
    const tr=await fetch(T3K_API+"/oauth/token",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({grant_type:"authorization_code",code:q.get("code"),code_verifier:ver,redirect_uri:location.origin+"/",client_id:T3K_ID})});
    const tj=await tr.json().catch(()=>({}));
    if(!tr.ok||!tj.access_token)throw new Error("access denied ("+(tj.error||tr.status)+")");
    const H={Authorization:"Bearer "+tj.access_token},id=q.get("tone_id");
    if(!id)throw new Error("no tone chosen");
    const tone=await (await fetch(T3K_API+"/tones/"+encodeURIComponent(id),{headers:H})).json();
    const mj=await (await fetch(T3K_API+"/models?tone_id="+encodeURIComponent(id)+"&architecture=2&page_size=25",{headers:H})).json();
    const models=(mj.data||[]).filter(m=>m.model_url);
    const who=(tone.user&&(tone.user.display_name||tone.user.username))||"unknown";
    const c=t3Msg("<h2>"+esc(tone.title||"Tone")+"</h2><div class=\"info\">by <b>"+esc(who)+"</b> &middot; license <b>"+esc(tone.license||"?")+"</b> &middot; <a href=\""+esc(tone.url||("https://www.tone3000.com/tones/"+id))+"\" target=\"_blank\" rel=\"noopener\">page on TONE3000</a></div>");
    if(!models.length){c.appendChild(el("div","info","No downloadable A2 model in this tone: the Pi only reads NAM A2 models."));c.insertAdjacentHTML("beforeend",attr+close);return}
    c.appendChild(el("div","info","Choose the model to download to the Pi (one at a time):"));
    const row=el("div","tools");c.appendChild(row);
    models.forEach(mo=>{
      const b=btn((mo.name||"model")+(mo.size?" ("+mo.size+")":""),"Download and install this model",async()=>{
        row.querySelectorAll("button").forEach(x=>x.disabled=true);
        const say=h=>{let s=$("t3s2");if(!s){s=el("div","info");s.id="t3s2";c.appendChild(s)}s.innerHTML=h};
        try{
          say("Downloading from TONE3000...");
          const r=await fetch(mo.model_url,{headers:H});
          if(!r.ok)throw new Error("download refused ("+r.status+")");
          const txt=await r.text();
          say("Converting for the Pi...");
          const adn=namToAdn(txt,tone.title);
          const name=safeName((tone.title||"tone")+"_"+(mo.size||mo.name||""));
          const info=who+" | "+(tone.license||"?")+" | tone3000.com/tones/"+id;
          if(await installModel(name,adn,info.slice(0,190),say))say("Installed <b>"+esc(name)+".adn</b> ("+(adn.length>>10)+" KB), author and license saved. You will find it in the library."+close);
          else say("Cancelled."+close);
        }catch(e){say("<b>Failed:</b> "+esc(e.message||e)+close)}
        row.querySelectorAll("button").forEach(x=>x.disabled=false);
      });
      row.appendChild(b);
    });
    c.insertAdjacentHTML("beforeend",attr);
  }catch(e){t3Msg("<b>TONE3000 did not answer as expected:</b> "+esc(e.message||e)+close)}
}
/* ---- Cab IR: .air files on the board; import of WAV files (resampled to 48 kHz, cut to 4096 samples, normalized) ----*/
function wavToIr(buf){
  const dv=new DataView(buf),tag=o=>String.fromCharCode(dv.getUint8(o),dv.getUint8(o+1),dv.getUint8(o+2),dv.getUint8(o+3));
  if(buf.byteLength<44||tag(0)!="RIFF"||tag(8)!="WAVE")throw new Error("not a WAV file");
  let o=12,fmt=null,data=null;
  while(o+8<=buf.byteLength){
    const id=tag(o),sz=dv.getUint32(o+4,true);
    if(id=="fmt "){fmt={f:dv.getUint16(o+8,true),ch:dv.getUint16(o+10,true),sr:dv.getUint32(o+12,true),bits:dv.getUint16(o+22,true)};if(fmt.f==0xFFFE&&sz>=26)fmt.f=dv.getUint16(o+32,true)}
    else if(id=="data"){data=[o+8,Math.min(sz,buf.byteLength-o-8)];break}
    o+=8+sz+(sz&1);
  }
  if(!fmt||!data)throw new Error("incomplete WAV file");
  const isF=fmt.f==3,isI=fmt.f==1;
  if(!(isF&&fmt.bits==32)&&!(isI&&(fmt.bits==16||fmt.bits==24||fmt.bits==32)))throw new Error("unsupported WAV format ("+fmt.bits+" bit): PCM 16, 24 or 32 bit, or 32 bit float, is required");
  const nch=Math.min(2,fmt.ch),bps=fmt.bits/8,frames=Math.floor(data[1]/(bps*fmt.ch));
  if(frames<16)throw new Error("the IR is too short");
  const chs=[];
  for(let c=0;c<nch;c++){
    const a=new Float32Array(frames);
    for(let i=0;i<frames;i++){
      const p=data[0]+(i*fmt.ch+c)*bps;
      a[i]=isF?dv.getFloat32(p,true):(fmt.bits==16?dv.getInt16(p,true)/32768:(fmt.bits==24?(dv.getUint8(p)|(dv.getUint8(p+1)<<8)|(dv.getInt8(p+2)<<16))/8388608:dv.getInt32(p,true)/2147483648));
    }
    chs.push(a);
  }
  return {sr:fmt.sr,chs};
}
/* cuts the initial silence, brings it to 48 kHz, maximum 4096 samples with a final fade, unit energy; returns the .air file*/
function irToAir(w){
  const N=4096,sr=w.sr;
  let pk=0;w.chs.forEach(a=>{for(let i=0;i<a.length;i++)pk=Math.max(pk,Math.abs(a[i]))});
  if(pk<1e-6)throw new Error("the IR is empty (silence)");
  let st=0;
  scan:for(let i=0;i<w.chs[0].length;i++)for(const a of w.chs)if(Math.abs(a[i])>0.01*pk){st=Math.max(0,i-8);break scan}
  const ratio=48000/sr,srcLen=w.chs[0].length-st,outLen=Math.min(N,Math.max(16,Math.round(srcLen*ratio)));
  const out=w.chs.map(a=>{
    const o=new Float32Array(outLen);
    if(sr==48000){for(let i=0;i<outLen;i++)o[i]=a[st+i]||0}
    else{
      const A=16,fc=Math.min(1,ratio);
      for(let i=0;i<outLen;i++){
        const t=st+i/ratio,i0=Math.floor(t);let s=0;
        for(let k=i0-A+1;k<=i0+A;k++){
          if(k<0||k>=a.length)continue;
          const x=t-k,sx=x==0?1:Math.sin(Math.PI*x*fc)/(Math.PI*x*fc),lw=x==0?1:Math.sin(Math.PI*x/A)/(Math.PI*x/A);
          s+=a[k]*fc*sx*lw;
        }
        o[i]=s;
      }
    }
    const fl=Math.min(512,outLen>>3);
    for(let i=0;i<fl;i++)o[outLen-1-i]*=0.5*(1-Math.cos(Math.PI*i/fl));
    return o;
  });
  let e=0;out.forEach(o=>{for(let i=0;i<o.length;i++)e+=o[i]*o[i]});e=Math.sqrt(e/out.length);
  if(!(e>0))throw new Error("the IR is empty");
  const nc=out.length,size=16+4*nc*outLen,b=new Uint8Array(size),dv=new DataView(b.buffer);
  b.set([65,68,82,49],0);dv.setUint32(4,outLen,true);dv.setUint32(8,nc,true);
  let p=12;out.forEach(o=>{for(let i=0;i<outLen;i++){dv.setFloat32(p,o[i]/e,true);p+=4}});
  dv.setUint32(p,crc32(b.subarray(0,p)),true);
  return {air:b,taps:outLen,ch:nc,sr};
}
async function importIr(file,n,say){
  const stem=file.name.replace(/\.[^.]*$/,""),name=safeName(stem);
  let air,info;
  if(/\.air$/i.test(file.name)){air=new Uint8Array(await file.arrayBuffer());info=""}
  else{const r=irToAir(wavToIr(await file.arrayBuffer()));air=r.air;info=r.taps+" samples, "+r.ch+" can., da "+r.sr+" Hz"}
  if((S.ir||[]).includes(name+".air")&&!confirm("Replace "+name+".air already in the library?"))return null;
  await upload(name+".air",air,(a,b)=>say("Sent to the Pi: "+Math.round(100*a/b)+"%"));
  if(!S.ir)S.ir=[];
  if(!S.ir.includes(name+".air")){S.ir.push(name+".air");S.ir.sort()}
  return {file:name+".air",info};
}
function cabPanel(c,n){
  const det=el("details","fxg");det.open=true;det.appendChild(el("summary","","IR"));
  const body=el("div","knobs");det.appendChild(body);
  const w=el("div","fch");w.title="Impulse response of the cabinet: the .air files are on the board";
  w.appendChild(el("span","","File IR"));
  const s=el("select");
  const fill=()=>{s.innerHTML="";const L=S.ir||[];L.forEach(f=>{const o=el("option","",esc(f));o.value=f;s.appendChild(o)});if(!L.length){const o=el("option","","No IR on the board: import one");o.value="";s.appendChild(o)}s.value=n.file||""};
  fill();
  s.onchange=()=>{s.dataset.t=Date.now();if(s.value)warn(g({op:"file",id:n.id,s:s.value}))};
  w.appendChild(s);body.appendChild(w);
  const st=el("div","info");body.appendChild(st);
  const stx=nn=>{st.textContent=nn.file?(nn.status||""):"no IR: the signal passes unchanged"};stx(n);
  addBind(n.id,nn=>{
    if(s.options.length!=Math.max(1,(S.ir||[]).length))fill();
    if(!(document.activeElement===s||recent(s)))s.value=nn.file||"";
    stx(nn);
  });
  const act=el("div","tools");
  const fi=el("input");fi.type="file";fi.accept=".wav,.air";fi.style.display="none";
  fi.onchange=async()=>{
    const f=fi.files[0];fi.value="";if(!f)return;
    const say=h=>{st.innerHTML=h};
    try{
      say("Reading "+esc(f.name)+"...");
      const r=await importIr(f,n,say);
      if(!r){stx(n);return}
      fill();s.dataset.t=Date.now();s.value=r.file;
      const q=await g({op:"file",id:n.id,s:r.file});
      say(q.ok?("Imported <b>"+esc(r.file)+"</b>"+(r.info?" ("+esc(r.info)+")":"")):("<b>Failed:</b> "+esc(q.err||"error")));
    }catch(e){say("<b>Failed:</b> "+esc(e.message||e))}
  };
  act.appendChild(fi);
  act.appendChild(btn("Import WAV","Import an IR from a WAV file (mono or stereo): it is brought to 48 kHz, cut to 4096 samples and normalized",()=>fi.click()));
  act.appendChild(btn("Delete IR","Delete the chosen IR from the board",async()=>{
    const f=s.value,L=S.ir||[];
    if(!f)return;
    if(L.length<2){alert("It is the only IR: it cannot be deleted");return}
    if(!confirm("Delete "+f+" from the board?\nThe block will move to the next IR. This cannot be undone."))return;
    const i=L.indexOf(f),nx=L[i+1]||L[i-1];
    s.dataset.t=Date.now();await g({op:"file",id:n.id,s:nx});s.value=nx;
    const j=await api("/delete",{s:f});
    if(!j.ok){alert(j.err||"error");await g({op:"file",id:n.id,s:f});s.value=f}
    else{S.ir=L.filter(x=>x!=f);fill();s.value=nx}
  }));
  body.appendChild(act);c.appendChild(det);
}
function libTools(){
  const w=el("div","tools");
  const fi=el("input");fi.type="file";fi.style.display="none";
  fi.onchange=()=>{const f=fi.files[0];fi.value="";if(!f)return;if(!/\.(nam|adn)$/i.test(f.name)){t3Msg("Choose a .nam (or .adn) file. This is <b>"+esc(f.name)+"</b>. <button class=\"mini\" onclick=\"$('t3').remove()\">Close</button>");return}loadLocal(f)};
  w.appendChild(fi);
  w.appendChild(btn("Load .nam from the PC","Converts and sends a NAM A2 model to the Pi",()=>fi.click()));
  w.appendChild(btn("TONE3000","Choose a model on TONE3000 (their site opens)",t3kStart));
  const a=el("a","");a.href="https://www.tone3000.com";a.target="_blank";a.rel="noopener";a.textContent="Powered by TONE3000";a.style.fontSize="12px";w.appendChild(a);
  return w;
}
/* real-time input and output bars: green up to -18 dBFS, yellow up to -6, red above; a thin white line marks the peak*/
const MT=[];
let mtLast=0;
function meter(kind,idx){
  const w=el("span","mt"+(idx.length==1?" one":""));
  const bars=idx.map(()=>{const i=el("i"),m=el("b"),u=el("u");i.appendChild(m);i.appendChild(u);w.appendChild(i);return {m,u,v:-90,h:-90,t:0}});
  MT.push({kind,idx,bars});return w;
}
const SWL=[];
function swLamp(txt){const w=el("span","swlamp"),l=el("span","lamp green");w.appendChild(l);w.appendChild(document.createTextNode(txt));SWL.push(l);return w}
function mtApply(j){
  SWL.forEach(l=>{if(l.isConnected)l.classList.toggle("on",!!j.sw)});
  document.querySelectorAll('.bk[data-fx="adelsendsweep"]').forEach(e=>e.classList.toggle("swplay",!!j.sw));
  const now=performance.now(),db=p=>p>0?20*Math.log10(p/1e6):-90,pc=x=>Math.max(0,Math.min(100,(x+60)/60*100));
  const dt=mtLast?Math.max(0.5,Math.min(4,(now-mtLast)/400)):1;mtLast=now;		/* the bars' decay was calibrated on 400 ms: scale it with the real interval*/
  MT.forEach(m=>{const src=j[m.kind]||[0,0];m.bars.forEach((b,k)=>{
    const L=db(src[m.idx[k]]||0);
    b.v=L>b.v?L:Math.max(L,b.v-6.5*dt);
    if(L>=b.h){b.h=L;b.t=now}else if(now-b.t>1200)b.h=Math.max(L,b.h-9*dt);
    b.m.style.height=(100-pc(b.v))+"%";b.u.style.bottom=pc(b.h)+"%";b.u.style.display=b.h>-58?"":"none";
  })});
}
/* Circle sustainably accepts about 9 new connections per second (measured: at 10/s 8% fail, at 20/s 41%): the page
   uses 1 per second for the meters (/m every 1000 ms, and only if a meter is in view) and 0.4 for /status every 2.5 s (fewer requests = less risk of the known memory fault; with the page hidden no requests); if a request fails it waits 1 s before retrying*/
/* the page's requests are the load that most often precedes the known memory fault (zeroed DMA descriptors, audio stopped): with no activity for 60 s
   the page stops polling the board (and so does not weigh on it while you play) and resumes as soon as you touch, move the mouse or press a key*/
let lastAct=Date.now();
const idleNow=()=>Date.now()-lastAct>60000;
["pointerdown","keydown","wheel","touchstart","mousemove"].forEach(ev=>window.addEventListener(ev,()=>{lastAct=Date.now();const s=document.getElementById("sub");if(s&&s.dataset.idle)delete s.dataset.idle},{passive:true}));
/* /m is only needed if something shows it: a level bar, a lamp, a measured value of an effect or a Sweep block in the grid; otherwise no request*/
const needM=()=>MT.some(m=>m.bars.length&&m.bars[0].m.isConnected&&(m.kind!="n"||!document.body.classList.contains("nobm")))||FXO.some(x=>(x.val&&x.val.isConnected)||(x.lamp&&x.lamp.isConnected))||SWL.some(l=>l.isConnected)||!!document.querySelector('.bk[data-fx="adelsendsweep"]');
async function mtick(){
  let ok=true;
  if(!document.hidden&&!idleNow()&&needM()){try{const fb=typeof sel=="number"?locate(sel):null;const r=await fetch("/m?_="+Date.now()+(fb&&fb.n.t=="fx"?"&b="+sel:""),{cache:"no-store"});const j=await r.json();mtApply(j);fxOut(j)}catch(e){ok=false}}
  setTimeout(mtick,ok?1000:2000);
}
/* ADELshifter "Manual" section: with Manual on, every voice has down/up arrows that move it by one semitone from the note being played now;
   the voice is switched to Chromatic mode and the page shows the played note and the note the voice makes, so a harmony can be built by ear*/
/* saves the current values of a block as a named setting (same list as the Saved settings menu); the values are re-read first so that a change made a moment ago is included*/
async function bpSaveFlow(id){
  try{await stNow()}catch(e){}
  const l=locate(id);if(!l||!l.n)return;
  const n=l.n;
  const nm=prompt("Name for the settings of this block:","");
  if(!nm||!nm.trim())return;
  const name=nm.trim();
  if((S.bp||[]).some(b=>b.n==name)&&!confirm("Replace the existing \u0027"+name+"\u0027: replace it?"))return;
  const r=await api("/bp",{op:"save",n:name,d:bpData(n)});warn(r);
  if(r.ok)await g({op:"bpset",id:n.id,s:name});
}
const NOTE_NAMES=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
const midiName=m=>NOTE_NAMES[((m%12)+12)%12]+(Math.floor(m/12)-1);
function shManual(n,d){
  const ix=k=>d.p.findIndex(p=>p.k==k);
  const det=el("details","fxg"),key="fxg_adelshifter_manual";
  let open=true;try{const s=localStorage.getItem(key);if(s!==null)open=s=="1"}catch(e){}
  det.open=open;det.ontoggle=()=>{try{localStorage.setItem(key,det.open?"1":"0")}catch(e){}};
  det.appendChild(el("summary","","Manual"));
  const box=el("div","shman");det.appendChild(box);
  let played=null;                       /* MIDI note being played now (rounded), null if the tracker does not hear a note*/
  const voices=[],rows=el("div","shman");
  const top=el("div","shtop");
  top.appendChild(el("span","","Manual"));
  const sw=switchEl(async v=>{
    lsSet("nb_shman",v?"1":"0");rows.style.display=v?"":"none";
    if(v){for(let i=1;i<=3;i++){await g({op:"set",id:n.id,k:"v"+i+"mode",v:1})}}
  });
  top.appendChild(sw);
  const live=el("b","shnote","Play a note");top.appendChild(live);
  top.appendChild(btn("Save settings...","Save the three voices as a named setting: it appears in Saved settings of this block",()=>bpSaveFlow(n.id)));
  box.appendChild(top);
  box.appendChild(el("div","info","With Manual on the three voices are in Chromatic mode. Play a note, listen to the voice and move it up or down one semitone at a time with the arrows (hold the arrow to repeat) until it sounds right; the interval and the note it makes are shown. Switch the voice on if it is off."));
  box.appendChild(rows);
  const refresh=()=>{
    live.textContent=played===null?"Play a note":"Playing: "+midiName(played);
    voices.forEach(vc=>{
      const s=Math.round(vc.semi);
      vc.val.textContent=(s>0?"+":"")+s+" semitones"+(played===null?"":"  →  "+midiName(played+s));
    });
  };
  for(let i=1;i<=3;i++){
    const kOn=ix("v"+i+"on"),kSemi=ix("v"+i+"semi");
    const vc={semi:(n.v&&n.v[kSemi])||0,at:0};
    const row=el("div","shrow");
    row.appendChild(el("span","vn","Voice "+i));
    const onSw=switchEl(v=>g({op:"set",id:n.id,k:"v"+i+"on",v}));row.appendChild(onSw);
    const dn=el("button","mini shar","▼"),up=el("button","mini shar","▲");dn.type=up.type="button";
    dn.title="One semitone down";up.title="One semitone up";
    vc.val=el("b","shval","");
    const step=async dlt=>{
      const nv=Math.max(-24,Math.min(24,Math.round(vc.semi)+dlt));
      if(nv==Math.round(vc.semi))return;
      vc.semi=nv;vc.at=Date.now();refresh();
      await g({op:"set",id:n.id,k:"v"+i+"mode",v:1});
      await g({op:"set",id:n.id,k:"v"+i+"on",v:1});onSw.sync(1);
      await g({op:"set",id:n.id,k:"v"+i+"semi",v:nv});
    };
    const hold=(b,dlt)=>{
      let t1=0,t2=0;
      const stop=()=>{clearTimeout(t1);clearInterval(t2);t1=t2=0};
      b.addEventListener("pointerdown",e=>{e.preventDefault();step(dlt);t1=setTimeout(()=>{t2=setInterval(()=>step(dlt),150)},450)});
      ["pointerup","pointerleave","pointercancel"].forEach(ev=>b.addEventListener(ev,stop));
    };
    hold(dn,-1);hold(up,1);
    row.appendChild(dn);row.appendChild(vc.val);row.appendChild(up);
    rows.appendChild(row);
    voices.push(vc);
    addBind(n.id,nn=>{onSw.sync(nn.v[kOn]);if(Date.now()-vc.at>4000){vc.semi=nn.v[kSemi];refresh()}});
  }
  const man=lsGet("nb_shman","0")=="1";
  sw.sync(man?1:0);rows.style.display=man?"":"none";
  const kNote=ix("note"),kVoiced=ix("voiced");
  if(kNote>=0)FXO.push({k:kNote,p:d.p[kNote],val:live,fn:(v,f)=>{played=(kVoiced>=0&&f[kVoiced]>0.5&&v>0)?Math.round(v):null;refresh()}});
  refresh();
  return det;
}/* effect panel: one box per group as in PiPedal's plugins; knobs (also logarithmic), menus, switches and measured values*/
let FXO=[];
function fxEditor(c,n){
  if(n.fx=="selector")c.appendChild(el("div","info","The Selector sends the signal to ONE output at a time: it has outputs A, B (and C if Outputs = ABC) on the right side of the block, drag from the one you want to the block that must receive the signal. Choose the branch with Branch, from MIDI (Branch parameter) or by double clicking the block in the grid, which moves to the next branch. You can link several blocks from the same output. An output with no links is silence. When it is off, all the outputs pass."));
  FXO=[];
  const d=fxd(n.fx);
  if(!d){c.appendChild(el("div","info bad","Unknown effect: "+esc(n.fx)));return}
  if(n.fx=="cabir")cabPanel(c,n);
  if(n.fx=="adelphase")c.appendChild(swLamp("Probe playing"));
  const groups=d.g&&d.g.length?d.g:[d.title];
  groups.forEach((gname,gi)=>{
    const ps=d.p.map((p,k)=>[p,k]).filter(([p])=>(p.gr||0)==gi);
    if(!ps.length)return;
    const det=el("details","fxg"),key="fxg_"+d.fx+"_"+gi;
    let open=gi==0;
    try{const s=localStorage.getItem(key);if(s!==null)open=s=="1"}catch(e){}
    det.open=open;
    det.ontoggle=()=>{try{localStorage.setItem(key,det.open?"1":"0")}catch(e){}};
    det.appendChild(el("summary","",esc(gname)));
    const kb=el("div","knobs");det.appendChild(kb);
    ps.forEach(([p,k])=>{
      if(p.kind==1){
        const w=el("div","fch");if(p.h)w.title=p.h;
        w.appendChild(el("span","",esc(p.l)));
        const s=el("select");(p.e||[]).forEach((e,i)=>{const o=el("option","",esc(e));o.value=i;s.appendChild(o)});
        s.onchange=()=>{s.dataset.t=Date.now();g({op:"set",id:n.id,k:p.k,v:parseInt(s.value)})};
        w.appendChild(s);kb.appendChild(w);
        addBind(n.id,nn=>{if(document.activeElement===s||recent(s))return;s.value=Math.round(nn.v[k])});
      }else if(p.kind==2){
        const w=el("div","fch");if(p.h)w.title=p.h;
        w.appendChild(el("span","",esc(p.l)));
        const sw=switchEl(v=>g({op:"set",id:n.id,k:p.k,v}));w.appendChild(sw);kb.appendChild(w);
        addBind(n.id,nn=>sw.sync(nn.v[k]));
      }else if(p.kind==3){
        const w=el("div","fro");if(p.h)w.title=p.h;
        w.appendChild(el("span","",esc(p.l)));
        if(p.k=="playing"||p.k=="stale"){
          const lamp=el("span","lamp "+(p.k=="stale"?"red":"green"));w.appendChild(lamp);kb.appendChild(w);
          FXO.push({k,p,lamp});
        }else{
        const val=el("b","","-"),bar=el("div","bar"),bi=el("i");bar.appendChild(bi);
        w.appendChild(val);if(!p.e)w.appendChild(bar);kb.appendChild(w);
        FXO.push({k,p,val,bi});
        }
      }else{
        kb.appendChild(knob(p.l,p.min,p.max,p.step,p.u,v=>g({op:"set",id:n.id,k:p.k,v}),p.def,{log:p.log,hint:p.h,dbl:()=>presetVal(n.id,p.k)}));
        const kn=kb.lastChild;
        addBind(n.id,nn=>kn.sync(nn.v[k]));
      }
    });
    c.appendChild(det);
    if(n.fx=="adelshifter"&&gi==0)c.appendChild(shManual(n,d));
  });
}
function fxOut(j){
  if(!j||!j.f||!FXO.length)return;
  FXO.forEach(o=>{
    const v=j.f[o.k];if(v===undefined)return;
    const p=o.p;
    if(o.fn){o.fn(v,j.f);return}
    if(o.lamp){o.lamp.classList.toggle("on",v>0.5);return}
    if(p.e)o.val.textContent=p.e[Math.max(0,Math.min(p.e.length-1,Math.round(v)))]||String(v);
    else{
      const r=p.max-p.min;
      o.val.textContent=v.toFixed(r>=100?0:(r>=10?1:2))+(p.u?" "+p.u:"");
      const pc=Math.max(0,Math.min(100,(v-p.min)/r*100));
      o.bi.style.width=pc+"%";o.bi.style.background=p.k=="gr"?"var(--warn)":"var(--ok)";
    }
  });
}
function presetBar(){
  const w=el("div","prebar"),cur=S.pc||"",dirty=!!S.pd,cm=S.board.mode||0,pre=S.pre||[],prm=S.prm||"";
  const mOf=i=>+(prm[i]||0),has=n=>pre.some((x,i)=>x==n&&mOf(i)==cm);
  const ic=modeIc;
  const sl=el("div","psel");
  const cb=el("button","pcur");cb.type="button";cb.title="Preset (the icon indicates the mode)";
  const curIdx=pre.findIndex((x,i)=>x==cur&&mOf(i)==cm);
  cb.innerHTML=(curIdx>=0?'<span class="pno">'+(curIdx+1)+"</span>"+ic(cm):"")+'<span class="pn">'+(cur?esc(cur)+(dirty?" *":""):"No preset (choose one)")+"</span>";
  const pick=async i=>{
    const n=pre[i],m=mOf(i);
    if(dirty&&!confirm("Preset '"+cur+"' has unsaved changes: loading '"+n+"' discards them. Continue?"))return;
    warn(await api("/pre",{op:"load",n,m}));
  };
  const closeMenu=()=>{const o=document.getElementById("pmenu");if(o)o.remove();document.removeEventListener("click",closeMenu,true)};
  cb.onclick=e=>{
    e.stopPropagation();
    if(document.getElementById("pmenu")){closeMenu();return}
    const r=cb.getBoundingClientRect(),mn=el("div","pmenu");mn.id="pmenu";
    mn.style.left=r.left+"px";mn.style.top=(r.bottom+4)+"px";mn.style.minWidth=r.width+"px";
    if(!pre.some((x,i)=>x&&mOf(i)!=1))mn.appendChild(el("div","po","No saved preset"));
    pre.forEach((n,i)=>{
      if(!n||mOf(i)==1)return;
      const o=el("div","po"+(i===curIdx?" on":""));
      o.innerHTML='<span class="pno">'+(i+1)+"</span>"+ic(mOf(i))+'<span class="pn">'+esc(n)+(i===curIdx&&dirty?" *":"")+"</span>";
      o.onclick=()=>{closeMenu();pick(i)};mn.appendChild(o);
    });
    document.body.appendChild(mn);
    setTimeout(()=>document.addEventListener("click",closeMenu,true),0);
  };
  sl.appendChild(cb);
  const saveAs=async()=>{
    const nm=prompt("Preset name:",cur||"");
    if(!nm||!nm.trim())return;
    const n=nm.trim();
    if(has(n)&&n!=cur&&!confirm("Replace the existing '"+n+"': replace it?"))return;
    warn(await api("/pre",{op:"save",n}));
  };
  const sv=btn("","Save (overwrites the preset in use)",async()=>{if(!cur)return saveAs();warn(await api("/pre",{op:"save",n:cur}))});
  sv.innerHTML=IC_SAVE;
  const sa=btn("","Save as...",saveAs);sa.innerHTML=IC_SAVEAS;
  const dl=btn("","Delete the preset in use",async()=>{if(cur&&confirm("Delete preset '"+cur+"'? The current blocks do not change."))warn(await api("/pre",{op:"del",n:cur,m:cm}))});
  dl.innerHTML=IC_DEL;if(!cur)dl.disabled=true;
  w.appendChild(sl);w.appendChild(sv);w.appendChild(sa);w.appendChild(dl);
  return w;
}
function ioPanel(which){
  const c=el("div","card ed");const h=el("h3","");
  h.appendChild(el("span","nm",which=="in"?"Input":"Output"));h.appendChild(el("span","grow"));
  h.appendChild(btn("\u2715","Close",()=>{sel=null;build();upd()},"del"));
  c.appendChild(h);
  const bar=(lab,id)=>{const r=el("div","row");r.appendChild(el("label","",lab));const b=el("div","bar");b.innerHTML='<i id="m'+id+'"></i>';r.appendChild(b);const o=el("output");o.id="v"+id;r.appendChild(o);return r};
  const addBlocks=(pred,pf)=>S.blocks.forEach((bl,bi)=>{
    if(!pred(bl))return;
    bl.params.forEach(p=>{
      if(pf&&!pf(p))return;
      if(p.t==0){if(!c.kb){c.kb=el("div","knobs");c.appendChild(c.kb)}const x=knob(p.l,p.min,p.max,p.step,p.u,v=>api("/set",{b:bl.id,k:p.k,v}),Math.max(p.min,Math.min(p.max,0)));c.kb.appendChild(x);mbind.push(()=>x.sync(S.blocks[bi].params.find(q=>q.k==p.k).v))}
      else if(p.t==1){const r=el("div","row tog");r.appendChild(el("label","",esc(p.l)));const w=switchEl(v=>api("/set",{b:bl.id,k:p.k,v}));r.appendChild(w);c.appendChild(r);mbind.push(()=>w.sync(S.blocks[bi].params.find(q=>q.k==p.k).v))}
    });
  });
  if(which=="in"){
    c.appendChild(bar("Input L","L"));c.appendChild(bar("Input R","R"));
    c.appendChild(el("div","info","Play a note: the bar that moves shows the input the cable is connected to."));
    /* guitar profiles: name and input trim for each guitar, chosen with a tap; it applies to all the presets and comes before every block*/
    const gbi=S.blocks.findIndex(b=>b.id=="guitar");
    if(gbi>=0){
      const gp=k=>S.blocks[gbi].params.find(x=>x.k==k);
      const actIdx=()=>{const q=gp("sel");return Math.max(1,Math.min(4,q?Math.round(q.v):1))};
      const gname=i=>{const q=gp("n"+i);return (q&&q.s)?q.s:"Guitar "+i};
      const gr=el("div","row");gr.appendChild(el("label","","Guitar"));
      const gw=el("div","gsel");
      for(let i=1;i<=4;i++){const gbn=btn("","",()=>api("/set",{b:"guitar",k:"sel",v:i}),"gbn");gbn.dataset.i=i;gw.appendChild(gbn)}
      gr.appendChild(gw);c.appendChild(gr);
      const nr=el("div","row");nr.appendChild(el("label","","Name"));
      const ni=el("input","");ni.type="text";ni.maxLength=20;ni.placeholder="name of the chosen guitar";nr.appendChild(ni);c.appendChild(nr);
      ni.onchange=()=>api("/set",{b:"guitar",k:"n"+actIdx(),s:ni.value});
      ni.onkeydown=e=>{if(e.key=="Enter")ni.blur()};
      const kb2=el("div","knobs");c.appendChild(kb2);
      const tk=knob("Trim",-24,12,0.5,"dB",v=>api("/set",{b:"guitar",k:"t"+actIdx(),v}),0,{hint:"Input trim of the chosen guitar (dB)"});kb2.appendChild(tk);
      const gupd=()=>{
        const a=actIdx();
        gw.querySelectorAll("button").forEach(x=>{const i=+x.dataset.i;x.textContent=i+" \u00b7 "+gname(i);x.title=gname(i);x.classList.toggle("on",i==a)});
        if(document.activeElement!==ni)ni.value=(gp("n"+a)||{}).s||"";
        const q=gp("t"+a);if(q)tk.sync(q.v);
      };
      gupd();mbind.push(gupd);
      c.appendChild(el("div","info","Guitar profiles: choose the guitar you are using, write its name and set the input trim in dB (negative if the guitar is louder, positive if it is weaker). The trim acts at the input, before NAM and pedals, and applies to all the presets; the name of the chosen guitar also appears in the top row."));
    }
    addBlocks(bl=>bl.id=="cal");
    c.appendChild(el("div","info","NAM calibration. Max input level = dBu corresponding to 0 dBFS with the Pisound Gain at 0 (measured: 1.5); if you raise the Gain by N dB, lower the value by N dB. Offset: +6 is the historical sound (ADELnam/PiPedal), 0 follows the TONE3000 guide: with +6 the models are pushed 6 dB harder and tend to distort more. It takes effect immediately, for all the NAMs."));
  }else{
    addBlocks(bl=>bl.id!="cal"&&bl.id!="guitar");
    c.appendChild(bar("Output L","OL"));c.appendChild(bar("Output R","OR"));
    c.appendChild(el("div","info","The master volume and the bypass apply to the whole pedal, after all the blocks."));
  }
  return c;
}
/* ---- ADELGrid and ADELMatrix: network of blocks with links (see board.h, graph.h) ----
   Input L, Input R, Output L and Output R are four single-channel blocks: in the page they have identifiers -11, -12, -21, -22;
   in the document they are the link from/to -1 (input) or -2 (output) with port 1 (L) or 2 (R).*/
const NETP=["Stereo","L","R"];
/* name of the starting port: from a Selector it is the branch (A = 1, B = 2, C = 0)*/
const isSelId=id=>{const q=id>=0?locate(id):null;return !!(q&&q.n&&q.n.fx=="selector")};
const pfName=(f,pf)=>isSelId(f)?(pf==1?"A":(pf==2?"B":"C")):(NETP[pf]||"")
const IOE={"-11":{n:"Input L",io:0,id:-1,p:1},"-12":{n:"Input R",io:1,id:-1,p:2},"-21":{n:"Output L",io:2,id:-2,p:1},"-22":{n:"Output R",io:3,id:-2,p:2}};
const isIo=id=>id<=-11,isInE=id=>id==-11||id==-12,isOutE=id=>id==-21||id==-22;
const fdb=v=>(v>0?"+":"")+(+Number(v).toFixed(1));
const netCells=()=>S.board.net.cells;
const netNode=id=>{const c=netCells().find(c=>c.b[0].id==id);return c?c.b[0]:null};
const ndName=id=>isIo(id)?IOE[id].n:(netNode(id)?nmOf(netNode(id)):"?");
/* name of the block with, for NAMs, also the loaded model*/
const ndFull=id=>{const n=isIo(id)?null:netNode(id);const s=ndName(id);if(n&&n.t=="nam"){const m=lbl(n);if(m)return s+" - "+m}return s};
const nf=a=>a[0]==-1?(a[2]==1?-11:-12):a[0],nt=a=>a[1]==-2?(a[3]==1?-21:-22):a[1];
/* API parameters for a link between two identifiers (the ports of a block are the given ones, usually 0 = stereo)*/
const lkArgs=(f,t,pf,pt)=>({fr:isInE(f)?-1:f,to:isOutE(t)?-2:t,pf:isInE(f)?IOE[f].p:(pf||0),pt:isOutE(t)?IOE[t].p:(pt||0)});
const colOf=id=>isIo(id)?S.board.net.io[IOE[id].io][1]:(netCells().find(c=>c.b[0].id==id)||{c:0}).c;
const rowOfId=id=>isIo(id)?S.board.net.io[IOE[id].io][0]:(netCells().find(c=>c.b[0].id==id)||{r:0}).r;
/* all the links (there are no automatic ones)*/
function netAll(){
  const N=S.board.net,out=N.links.map(a=>({f:nf(a),t:nt(a),pf:a[2],pt:a[3],db:a[4],imp:false}));
  return out;
}
/* mono or stereo output of every block, with the same rules as the engine: the input is mono if all its links are*/
function netMono(){
  const cells=netCells(),all=netAll(),m={};
  cells.forEach(c=>{m[c.b[0].id]=true});
  const sm=f=>isIo(f)?true:m[f];
  for(let it=0;it<cells.length+2;it++){
    let ch=false;
    cells.forEach(c=>{
      const n=c.b[0];let mi=true;
      all.forEach(l=>{if(l.t==n.id&&!(l.pt==0&&(l.pf!=0||sm(l.f))))mi=false});
      let o=mi;
      if(n.on){
        if(n.t=="nam")o=true;
        else if(n.t=="pan")o=false;
        else if(n.t=="fx"){
          if(n.fx=="adeldelaym"||n.fx=="adelverbm")o=true;
          else if(fxStereoOut(n))o=false;
          else if(String(n.fx).indexOf("adel")!=0&&n.fx!="cabir"&&!XFX.includes(n.fx)){const k=fxMixIdx(n);if(k<0||n.v[k]>0)o=false}
        }
      }
      if(m[n.id]!==o){m[n.id]=o;ch=true}
    });
    if(!ch)break;
  }
  return m;
}
/* can a be reached from b through the links between blocks?*/
function netReach(a,b){
  const links=S.board.net.links,seen={};const st=[a];
  while(st.length){const n=st.pop();if(n==b)return true;if(seen[n])continue;seen[n]=1;links.forEach(l=>{if(l[0]==n&&l[1]>=0)st.push(l[1])})}
  return false;
}
function netOk(f,t){
  if(f==t||isOutE(f)||isInE(t))return false;
  if(S.board.mode==2&&colOf(t)<colOf(f))return false;
  if(f>=0&&t>=0&&netReach(t,f))return false;
  return true;
}
/* edit window for the links between two blocks (level, ports, delete)*/
let popSpec=null;
function closePop(){popSpec=null;const p=$("lpop");if(p)p.remove()}
document.addEventListener("pointerdown",e=>{const p=$("lpop");if(p&&!p.contains(e.target)&&!e.target.closest(".mtc,.nlab,.chip2"))closePop()},true);
function linkPop(f,t,x,y){popSpec={f,t,x,y};renderPop()}
function renderPop(){
  const old=$("lpop");if(old)old.remove();
  if(!popSpec||!S.board.net)return;
  const {f,t,x,y}=popSpec,N=S.board.net,ls=N.links.filter(a=>nf(a)==f&&nt(a)==t).map(a=>({pf:a[2],pt:a[3],db:a[4]}));
  const pop=el("div","lpop");pop.id="lpop";
  pop.appendChild(el("div","lh",esc(ndFull(f))+" → "+esc(ndFull(t))));
  const portSel=(v,fixed,names,vals)=>{const s=el("select","");(names||NETP).forEach((n,i)=>{const o=el("option","",n);o.value=vals?vals[i]:i;s.appendChild(o)});s.value=v;if(fixed){s.disabled=true;s.style.visibility="hidden"}return s};
  const fsn=(locate(f)||{}).n,fromSel=!!(fsn&&fsn.fx=="selector");   /* from a Selector the starting port is the branch: A = 1, B = 2, C = 0*/
  ls.forEach(l=>{
    const row=el("div","lr");
    const sf=fromSel?portSel(l.pf,false,["A","B","C"],[1,2,0]):portSel(l.pf,isInE(f)),stt=portSel(l.pt,isOutE(t));sf.title=fromSel?"Selector branch":"Source port";stt.title="Destination port";
    const db=el("input","");db.type="number";db.step="0.5";db.min="-60";db.max="12";db.value=l.db;db.title="Level in dB";
    const apply=async(pf,pt)=>{
      if(pf!=l.pf||pt!=l.pt){const r=await g({op:"ul",...lkArgs(f,t,l.pf,l.pt)});if(!r.ok){alert(r.err||"error");return}}
      const r=await g({op:"lk",...lkArgs(f,t,pf,pt),v:+db.value});if(!r.ok)alert(r.err||"error");
    };
    sf.onchange=()=>apply(+sf.value,+stt.value);stt.onchange=()=>apply(+sf.value,+stt.value);db.onchange=()=>apply(l.pf,l.pt);
    row.appendChild(sf);row.appendChild(el("span","ar","→"));row.appendChild(stt);row.appendChild(db);row.appendChild(el("span","u","dB"));
    row.appendChild(btn("✕","Remove the link",async()=>{const r=await g({op:"ul",...lkArgs(f,t,l.pf,l.pt)});if(!r.ok)alert(r.err||"error");if(ls.length<2)closePop()},"del"));
    pop.appendChild(row);
  });
  const combos=[[0,0],[1,1],[2,2],[1,2],[2,1],[1,0],[2,0],[0,1],[0,2]];
  const used=l=>ls.some(q=>(isInE(f)||q.pf==l[0])&&(isOutE(t)||q.pt==l[1]));
  const free=combos.find(c=>!used(c));
  if(free&&!(isInE(f)&&isOutE(t)))pop.appendChild(btn("+ Link","Adds another link (different ports) between these two blocks",async()=>{const r=await g({op:"lk",...lkArgs(f,t,free[0],free[1]),v:0});if(!r.ok)alert(r.err||"error")}));
  pop.appendChild(btn("Close","",closePop));
  document.body.appendChild(pop);
  const w=pop.offsetWidth,h=pop.offsetHeight;
  pop.style.left=Math.max(6,Math.min(x,window.innerWidth-w-6))+"px";
  pop.style.top=Math.max(6,Math.min(y,window.innerHeight-h-6))+"px";
}
/* visible ports: inputs on the left, outputs on the right (L, stereo, R; a single dot for mono blocks). Drag from an output to an input to create the arrow*/
const monoBlock=n=>n.t=="nam"||(n.t=="fx"&&(n.fx=="adeldelaym"||n.fx=="adelverbm"));
function addDots(tile,id,ins,outs,sel){
  const mk=(dir,p)=>{
    const sb=sel&&dir=="out",lt=p==1?"A":(p==2?"B":"C");   /* Selector: the outputs are the branches A (port 1), B (port 2), C (port 0)*/
    const d=el("span","pt "+dir+" "+(sb?(p==1?"sa":(p==2?(sel==3?"sb":"sc"):"sc")):(p==0?"c":(p==1?"l":"r"))));   /* two outputs: one above and one below; three: above, middle, below*/
    if(sb)d.dataset.lb=lt;
    d.dataset.dir=dir;d.dataset.id=id;d.dataset.p=p;d.title=sb?"Branch "+lt+": drag to link":(dir=="in"?"Input":"Output")+(p==0?"(stereo)":(p==1?"L":"R"))+": drag to link";
    d.addEventListener("pointerdown",startLink);d.addEventListener("click",ev=>ev.stopPropagation());tile.appendChild(d);
  };
  ins.forEach(p=>mk("in",p));outs.forEach(p=>mk("out",p));
}
function startLink(e){
  if(e.button>0)return;
  e.preventDefault();e.stopPropagation();
  const d0=e.currentTarget,dir0=d0.dataset.dir,rc=d0.getBoundingClientRect(),x0=rc.left+rc.width/2,y0=rc.top+rc.height/2;
  const NS="http://www.w3.org/2000/svg",svg=document.createElementNS(NS,"svg"),ln=document.createElementNS(NS,"line");
  svg.setAttribute("style","position:fixed;left:0;top:0;width:100vw;height:100vh;pointer-events:none;z-index:70");
  [["x1",x0],["y1",y0],["x2",x0],["y2",y0],["stroke","#a78bfa"],["stroke-width","3"],["stroke-dasharray","6 4"]].forEach(([k,v])=>ln.setAttribute(k,v));
  svg.appendChild(ln);document.body.appendChild(svg);
  document.body.classList.add("lkdrag");   /* while dragging, the arrows do not intercept the pointer: the target is the port or block underneath*/
  d0.setPointerCapture(e.pointerId);
  const under=ev=>document.elementFromPoint(ev.clientX,ev.clientY);
  const mv=ev=>{
    ln.setAttribute("x2",ev.clientX);ln.setAttribute("y2",ev.clientY);
    document.querySelectorAll(".pt.hov").forEach(x=>x.classList.remove("hov"));
    const u=under(ev),p=u&&u.closest?u.closest(".pt"):null;if(p&&p!==d0&&p.dataset.dir!=dir0)p.classList.add("hov");
  };
  const end=async ev=>{
    d0.removeEventListener("pointermove",mv);d0.removeEventListener("pointerup",end);d0.removeEventListener("pointercancel",end);
    svg.remove();document.querySelectorAll(".pt.hov").forEach(x=>x.classList.remove("hov"));
    const u=ev.type=="pointercancel"?null:under(ev);   /* the target must be read before the arrows are re-enabled*/
    document.body.classList.remove("lkdrag");
    if(!u||!u.closest)return;
    const a={id:+d0.dataset.id,p:+d0.dataset.p,dir:dir0};let b=null;
    const pt=u.closest(".pt");
    if(pt&&pt!==d0)b={id:+pt.dataset.id,p:+pt.dataset.p,dir:pt.dataset.dir};
    else{const tl=u.closest(".bk[data-id]");if(tl&&+tl.dataset.id!=a.id)b={id:+tl.dataset.id,p:0,dir:dir0=="out"?"in":"out"}}
    if(!b||b.dir==a.dir)return;
    const from=a.dir=="out"?a:b,to=a.dir=="out"?b:a;
    dndSkip=true;setTimeout(()=>{dndSkip=false},80);
    if(!netOk(from.id,to.id)){alert("Link not possible: it must go to the right or vertically, without closing loops.");return}
    const args=lkArgs(from.id,to.id,from.p,to.p);
    if(S.board.net.links.some(l=>l[0]==args.fr&&l[1]==args.to&&l[2]==args.pf&&l[3]==args.pt)){linkPop(from.id,to.id,ev.clientX,ev.clientY+8);return}
    warn(await g({op:"lk",...args,v:0}));
  };
  d0.addEventListener("pointermove",mv);d0.addEventListener("pointerup",end);d0.addEventListener("pointercancel",end);
}
/* tiles: blocks of the network and the four input and output blocks*/
let lastTileClick={id:-1,t:0};
function ntile(n,mono){
  const t=el("div","bk");t.dataset.id=n.id;t.dataset.fx=n.fx||"";
  let cn=n,sk=null;
  /* double click = standby (on/off): the first click rebuilds the grid, so the second one is recognized here by timing and does not change the selection*/
  t.onclick=()=>{
    if(dndSkip)return;
    const now=Date.now();
    if(lastTileClick.id==n.id&&now-lastTileClick.t<600){
      lastTileClick={id:-1,t:0};
      if(n.fx=="selector"){const no=cn.v[1]>0.5?3:2;g({op:"set",id:n.id,k:"sel",v:(Math.round(cn.v[0])+1)%no})}   /* the Selector moves to the next branch (A->B->C->A, or A->B->A)*/
      else g({op:"set",id:n.id,k:"on",v:cn.on?0:1});
      return}
    lastTileClick={id:n.id,t:now};
    sel=sel==n.id?null:n.id;build();upd();
  };
  const isSelB=n.fx=="selector",ports=monoBlock(n)?[0]:[1,0,2];
  const mtE=meter("n",monoBlock(n)?[2*n.id]:[2*n.id,2*n.id+1]);		/* output level of the block (peak L and R): hidden with the "Levels" button*/
  const paint=nn=>{
    cn=nn;
    if(isSelB){const k2=nn.v[0]+"|"+nn.v[1]+"|"+nn.on;if(sk!==null&&sk!==k2)setTimeout(drawNet,0);sk=k2}   /* changes the branch: redraws the arrows (gray for the ones not chosen)*/
    const h=tileHtml(nn,mono),key=h+(isSelB?"|"+(nn.v[1]>0.5?3:2):"");
    if(t.dataset.h!==key){t.dataset.h=key;t.innerHTML=h;t.title=nmOf(nn)+(nn.fx=="selector"?" (double-click: next branch)":" (double-click: standby)");if(isSelB)addDots(t,n.id,[0],nn.v[1]>0.5?[1,2,0]:[1,2],nn.v[1]>0.5?3:2);else addDots(t,n.id,ports,ports);t.appendChild(mtE)}
    t.className="bk"+catCls(nn)+(isOff(nn)?" off":"")+(sel==nn.id?" sel":"");
  };
  paint(n);addBind(n.id,paint);
  netDnd(t,{id:n.id});
  return t;
}
function iotile(pid){
  const e=IOE[pid],inp=isInE(pid);
  const t=el("div","bk io"+(sel==pid?" sel":""));
  t.innerHTML='<span class="iot">'+(inp?"IN":"OUT")+"</span>"+(inp?GL.io_in:GL.io_out)+'<span class="lb">'+e.n+"</span>";
  t.appendChild(meter(inp?"i":"o",[e.p-1]));
  t.title=e.n+": single-channel signal";t.dataset.id=pid;
  t.onclick=()=>{if(dndSkip)return;sel=sel==pid?null:pid;build();upd()};
  addDots(t,pid,inp?[]:[0],inp?[0]:[]);
  netDnd(t,{id:pid});
  return t;
}
/* long press on a block: a trash button (remove) and a copy button appear over it; a long press on an empty cell shows a paste button.
   Any other tap, or 5 s, dismisses them. The copied block keeps its type, settings, model file and on/off state; it is pasted as a new block with no links.*/
let trashEl=null,trashTm=0,blkClip=null;
const IC_TRASH='<svg viewBox="0 0 24 24"><path d="M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14M10 10v6M14 10v6"/></svg>';
const IC_COPY='<svg viewBox="0 0 24 24"><rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 00-2-2H6a2 2 0 00-2 2v8a2 2 0 002 2h2"/></svg>';
const IC_PASTE='<svg viewBox="0 0 24 24"><rect x="5" y="5" width="14" height="16" rx="2"/><path d="M9 5V4a1 1 0 011-1h4a1 1 0 011 1v1M9 12h6M9 16h6"/></svg>';
function hideTrash(){if(trashTm){clearTimeout(trashTm);trashTm=0}if(trashEl){trashEl.remove();trashEl=null}}
function flash(msg){const o=document.querySelector(".toastmini");if(o)o.remove();const d=document.createElement("div");d.className="toastmini";d.textContent=msg;document.body.appendChild(d);setTimeout(()=>d.remove(),3500)}
function popBtn(cls,title,svg,fn){const b=document.createElement("button");b.className=cls;b.title=title;b.innerHTML=svg;b.onclick=async ev=>{ev.stopPropagation();hideTrash();await fn()};return b}
function popAt(el0,btns){
  hideTrash();
  const r=el0.getBoundingClientRect(),w=document.createElement("div");w.className="blkpop";
  btns.forEach(b=>w.appendChild(b));
  const wd=btns.length*44+(btns.length-1)*12;
  w.style.left=Math.max(4,Math.min(window.innerWidth-wd-4,r.left+r.width/2-wd/2))+"px";
  w.style.top=Math.max(4,r.top+r.height/2-22)+"px";
  document.body.appendChild(w);trashEl=w;
  trashTm=setTimeout(hideTrash,5000);
}
const clipKey=n=>n.t=="fx"?n.fx:(n.t=="vol"&&n.rt?"rout":n.t);
function copyBlock(id){
  const l=locate(id);if(!l||!l.n)return;
  const n=l.n;
  blkClip={k:clipKey(n),t:n.t,fx:n.fx,d:bpData(n)+(n.fx=="cabir"&&n.file?"|file="+n.file:""),on:n.on?1:0,name:lbl(n)||n.fx||n.t};
  flash("Copied: "+blkClip.name+". Long-press an empty cell, or use + > Paste, to paste it.");
}
async function pasteBlock(args){
  if(!blkClip)return;
  const c=blkClip;
  const r=await g({op:"add",k:c.k,...args});warn(r);
  if(!r.ok)return;
  sel=r.id;
  const s={t:c.t,fx:c.fx,p:[],on:c.on};
  c.d.split("|").slice(1).forEach(x=>{const i=x.indexOf("="),k=x.slice(0,i),v=x.slice(i+1);if(k=="file")s.file=v;else s.p.push([k,parseFloat(v)])});
  const q=await applyPairs(r.id,s);
  if(!q.ok)alert(q.err||"error");else if(q.warn)alert(q.warn);
  if(args.r!==undefined)await spliceLinks(r.id,args.r,args.p);
}
function showTrash(tile,id){
  popAt(tile,[popBtn("cp","Copy the block",IC_COPY,()=>copyBlock(id)),popBtn("","Remove the block",IC_TRASH,async()=>{sel=null;warn(await g({op:"del",id}))})]);
}
function emptyCellPress(btnEl,args){
  let tm=0,done=false;
  const clr=()=>{if(tm){clearTimeout(tm);tm=0}};
  btnEl.addEventListener("pointerdown",()=>{done=false;if(!blkClip)return;tm=setTimeout(()=>{done=true;popAt(btnEl,[popBtn("ps","Paste: "+blkClip.name,IC_PASTE,()=>pasteBlock(args))])},600)});
  ["pointerup","pointerleave","pointercancel"].forEach(ev=>btnEl.addEventListener(ev,clr));
  btnEl.addEventListener("click",ev=>{if(done){ev.stopImmediatePropagation();ev.preventDefault();done=false}},true);
}
document.addEventListener("pointerdown",e=>{if(trashEl&&!trashEl.contains(e.target))hideTrash()},true);
function netDnd(tile,n){
  tile.addEventListener("pointerdown",e=>{
    if(e.button>0||(e.target.closest&&e.target.closest(".pt")))return;
    const x0=e.clientX,y0=e.clientY;let on=false,gh=null,cur=null,lpOk=false;
    const lpT=isIo(n.id)?0:setTimeout(()=>{if(!on){lpOk=true;showTrash(tile,n.id)}},600);
    const clean=()=>{
      if(lpT)clearTimeout(lpT);
      tile.removeEventListener("pointermove",mv);tile.removeEventListener("pointerup",up);tile.removeEventListener("pointercancel",clean);
      if(gh)gh.remove();document.querySelectorAll(".gcl.dz").forEach(c=>c.classList.remove("dz"));
      tile.classList.remove("dragging");document.body.classList.remove("lkdrag");
      if(on||lpOk){dndSkip=true;setTimeout(()=>{dndSkip=false},80)}
    };
    const mv=ev=>{
      if(!on){if(Math.hypot(ev.clientX-x0,ev.clientY-y0)<8)return;
        if(lpOk)return;
        if(lpT)clearTimeout(lpT);
        hideTrash();
        on=true;tile.classList.add("dragging");document.body.classList.add("lkdrag");
        gh=tile.cloneNode(true);gh.className="bk gh";gh.style.left=ev.clientX+"px";gh.style.top=ev.clientY+"px";document.body.appendChild(gh)}
      gh.style.left=ev.clientX+"px";gh.style.top=ev.clientY+"px";
      document.querySelectorAll(".gcl.dz").forEach(c=>c.classList.remove("dz"));
      gh.style.display="none";const u=document.elementFromPoint(ev.clientX,ev.clientY);gh.style.display="";
      const c=u&&u.closest?u.closest(".gcl[data-r]"):null;cur=null;
      if(c&&(!c.querySelector(".bk")||c.querySelector(".bk")===tile)){c.classList.add("dz");cur={r:+c.dataset.r,c:+c.dataset.c}}
    };
    const up=async()=>{
      const go=on&&cur;const tg=cur;clean();
      if(go)warn(await g(isIo(n.id)?{op:"mvio",w:IOE[n.id].io,r:tg.r,p:tg.c}:{op:"mv",id:n.id,r:tg.r,p:tg.c}));
    };
    tile.setPointerCapture(e.pointerId);
    tile.addEventListener("pointermove",mv);tile.addEventListener("pointerup",up);tile.addEventListener("pointercancel",clean);
  });
}
/* a new block created on an existing arrow splits it: every arrow that passes over its cell (and whose start and end columns are to the left
   and to the right of it) becomes from -> new -> to, with the center ports (the signal stays the same); the starting arrow keeps the level, the second one is at 0 dB*/
function segHitsRect(x1,y1,x2,y2,R){
  let t0=0,t1=1;const dx=x2-x1,dy=y2-y1;
  const clip=(p,q)=>{if(p===0)return q>=0;const r=q/p;if(p<0){if(r>t1)return false;if(r>t0)t0=r}else{if(r<t0)return false;if(r<t1)t1=r}return true};
  return clip(-dx,x1-R.l)&&clip(dx,R.r-x1)&&clip(-dy,y1-R.t)&&clip(dy,R.b-y1)&&t0<=t1;
}
async function spliceLinks(id,r,c){
  const N=S.board.net;if(!N)return;
  const cell=document.querySelector('.gcl[data-r="'+r+'"][data-c="'+c+'"]'),gd=document.querySelector(".ngd");
  if(!cell||!gd)return;
  const cb=cell.getBoundingClientRect(),m=3;
  const R={l:cb.left+(cb.width-32)/2-m,r:cb.left+(cb.width+32)/2+m,t:cb.top-m,b:cb.top+32+m};
  const dot=(pid,dir,p)=>gd.querySelector('.pt[data-id="'+pid+'"][data-dir="'+dir+'"][data-p="'+p+'"]')||gd.querySelector('.pt[data-id="'+pid+'"][data-dir="'+dir+'"][data-p="0"]');
  const cen=e=>{const b=e.getBoundingClientRect();return [(b.left+b.right)/2,(b.top+b.bottom)/2]};
  for(const a of N.links.slice()){
    const f=nf(a),t=nt(a);
    if(f==id||t==id)continue;
    if(!(colOf(f)<c&&c<colOf(t)))continue;
    const ea=dot(f,"out",isIo(f)?0:a[2]),eb=dot(t,"in",isIo(t)?0:a[3]);
    if(!ea||!eb)continue;
    const [x1,y1]=cen(ea),[x2,y2]=cen(eb);
    if(!segHitsRect(x1,y1,x2,y2,R))continue;
    let q=await g({op:"ul",...lkArgs(f,t,a[2],a[3])});if(!q.ok){alert(q.err||"error");continue}
    q=await g({op:"lk",...lkArgs(f,id,a[2],0),v:a[4]});if(!q.ok){alert(q.err||"error")}
    q=await g({op:"lk",...lkArgs(id,t,0,a[3]),v:0});if(!q.ok){alert(q.err||"error")}
  }
}
/* menu to add a block*/
function netAddMenu(btnEl,args){
  btnEl.onclick=()=>{
    const s=el("select","pbs");
    [["","Block..."]].concat(blkClip?[["__paste","Paste: "+blkClip.name]]:[],addOpts(3)).forEach(([v,t])=>{const o=el("option","",esc(t));o.value=v;s.appendChild(o)});
    s.onchange=async()=>{const k=s.value;if(k=="__paste"){await pasteBlock(args);return}if(k){const r=await g({op:"add",k,...args});warn(r);if(r.ok){sel=r.id;if(args.r!==undefined)await spliceLinks(r.id,args.r,args.p)}}};
    s.onblur=()=>{if(s.parentNode)s.replaceWith(btnEl)};
    btnEl.replaceWith(s);s.focus();
  };
  return btnEl;
}
/* ADELGrid: 6 rows x 8 columns; the cells of a row link by themselves, the other arrows (also diagonal) are the cross links*/
function gridView(mono){
  const N=S.board.net,wrap=el("div","nt"),gd=el("div","ngd");
  for(let r=0;r<6;r++){
    for(let c=0;c<12;c++){
      const cell=el("div","gcl");cell.dataset.r=r;cell.dataset.c=c;
      const cc=N.cells.find(q=>q.r==r&&q.c==c);
      let io=null;for(const k in IOE)if(N.io[IOE[k].io][0]==r&&N.io[IOE[k].io][1]==c)io=+k;
      if(cc)cell.appendChild(ntile(cc.b[0],mono[cc.b[0].id]));
      else if(io!==null)cell.appendChild(iotile(io));
      else{const p=el("button","gp","+");p.title="Add a block in this cell (long press to paste a copied block)";netAddMenu(p,{r,p:c});emptyCellPress(p,{r,p:c});cell.appendChild(p)}
      gd.appendChild(cell);
    }
  }
  wrap.appendChild(gd);
  return wrap;
}
/* arrows and levels above the grid (SVG): the row links are horizontal, the cross ones go diagonally from the starting block to the arrival block*/
function drawNet(){
  document.querySelectorAll(".ngd").forEach(gd=>{
    const old=gd.querySelector(":scope > svg.nsv");if(old)old.remove();
    if(S.board.mode!=2||!S.board.net)return;
    const P=gd.getBoundingClientRect(),all=netAll(),mono=netMono();
    const rel=e=>{const b=e.getBoundingClientRect();return {l:b.left-P.left,r:b.right-P.left,cy:(b.top+b.bottom)/2-P.top}};
    const tileOf=id=>gd.querySelector('.bk[data-id="'+id+'"]');
    let g1="",g2="",lab="";
    all.forEach(l=>{
      const dotOf=(id,dir,p)=>gd.querySelector('.pt[data-id="'+id+'"][data-dir="'+dir+'"][data-p="'+p+'"]')||gd.querySelector('.pt[data-id="'+id+'"][data-dir="'+dir+'"][data-p="0"]')||tileOf(id);
      const ea=dotOf(l.f,"out",isIo(l.f)?0:l.pf),eb=dotOf(l.t,"in",isIo(l.t)?0:l.pt);if(!ea||!eb)return;
      const a=rel(ea),b=rel(eb);
      const sm=isIo(l.f)?true:mono[l.f];
      const st=l.pf==0&&l.pt==0&&!sm;
      /* output of a Selector that is not chosen: gray arrow, so you can see where the signal goes (A = port 1, B = port 2, C = port 0)*/
      const sn=isIo(l.f)?null:(locate(l.f)||{}).n;let off=false;
      if(sn&&sn.fx=="selector"&&sn.on){const no=sn.v[1]>0.5?3:2,s0=Math.max(0,Math.min(no-1,Math.round(sn.v[0])));off=(l.pf==1?0:(l.pf==2?1:2))!=s0}
      const col=off?"#5c5f68":"#cfd2d8";
      const x1=(a.l+a.r)/2,y1=a.cy,x2=b.l-1,y2=b.cy;
      const vert=colOf(l.f)==colOf(l.t);
      const d=vert?"M"+x1.toFixed(1)+" "+y1.toFixed(1)+" C"+(x1+34).toFixed(1)+" "+y1.toFixed(1)+" "+(x2-34).toFixed(1)+" "+y2.toFixed(1)+" "+x2.toFixed(1)+" "+y2.toFixed(1):"M"+x1.toFixed(1)+" "+y1.toFixed(1)+" L"+x2.toFixed(1)+" "+y2.toFixed(1);
      g1+='<path d="'+d+'" fill="none" stroke="'+col+'" stroke-width="'+(st?5:2)+'"'+(off?' stroke-dasharray="5 4"':"")+' />';
      if(st)g2+='<path d="'+d+'" fill="none" stroke="#1b1c20" stroke-width="2" />';
      const ang=vert?0:Math.atan2(y2-y1,x2-x1)*180/Math.PI;
      lab+='<g class="nlab" data-f="'+l.f+'" data-t="'+l.t+'" transform="translate('+x2.toFixed(1)+' '+y2.toFixed(1)+') rotate('+ang.toFixed(1)+')"><circle cx="-4" cy="0" r="9" fill="transparent"/><path d="M0 0 l-8 -4.5 v9 z" fill="'+col+'"/></g>';
      if(!l.imp){
        const mx=(x1+x2)/2,my=(y1+y2)/2;
        const pf=isInE(l.f)?0:l.pf,pt=isOutE(l.t)?0:l.pt;
        const txt=fdb(l.db)+" dB"+(isSelId(l.f)?"  "+pfName(l.f,l.pf)+(pt?"→"+(NETP[pt]||""):""):(pf||pt?"  "+(NETP[pf]||"")+"→"+(NETP[pt]||""):""));
        const w=txt.length*6.2+10;
        lab+='<g class="nlab" data-f="'+l.f+'" data-t="'+l.t+'" transform="translate('+(mx-w/2).toFixed(1)+' '+(my-9).toFixed(1)+')"><rect width="'+w.toFixed(1)+'" height="18" rx="9" fill="#2b2d33" stroke="'+col+'"/><text x="'+(w/2).toFixed(1)+'" y="13" text-anchor="middle" font-size="11" fill="#e6e6e6">'+txt+'</text></g>';
      }
    });
    gd.insertAdjacentHTML("beforeend",'<svg class="nsv" width="'+gd.scrollWidth+'" height="'+gd.scrollHeight+'">'+g1+g2+lab+'</svg>');
    gd.querySelectorAll(".nlab").forEach(e=>{e.onclick=ev=>linkPop(+e.dataset.f,+e.dataset.t,ev.clientX,ev.clientY+8)});
  });
}
/* links panel of the selected block (or input/output)*/
function netPanel(n){
  const N=S.board.net,c=el("div","npan"),id=n.id,io=isIo(id);
  c.appendChild(el("h4","","Links"));
  const chips=(title,list,from)=>{
    const w=el("div","nrow");w.appendChild(el("span","nl",title));
    list.forEach(a=>{
      const other=from?nt(a):nf(a);
      const b=el("button","chip2");
      const pf=isInE(nf(a))?0:a[2],pt=isOutE(nt(a))?0:a[3];
      b.textContent=ndFull(other)+"  "+fdb(a[4])+" dB"+(isSelId(nf(a))?"  branch "+pfName(nf(a),a[2])+(pt?"→"+NETP[pt]:""):((pf||pt)?"  "+NETP[pf]+"→"+NETP[pt]:""));
      b.onclick=e=>linkPop(from?id:other,from?other:id,e.clientX,e.clientY+8);w.appendChild(b);
    });
    if(!list.length)w.appendChild(el("span","hint","none"));
    return w;
  };
  if(!isOutE(id))c.appendChild(chips("Outputs to",N.links.filter(a=>nf(a)==id),true));
  if(!isInE(id))c.appendChild(chips("Inputs from",N.links.filter(a=>nt(a)==id),false));
  const mk=(txt,opts,fn)=>{
    const s=el("select","");s.style.width="auto";s.style.gridColumn="auto";
    [["",txt]].concat(opts).forEach(([v,t])=>{const o=el("option","",esc(t));o.value=v;s.appendChild(o)});
    s.onchange=async()=>{const v=s.value;s.value="";if(v!=="")warn(await fn(+v))};
    return s;
  };
  const blocks=netCells().map(q=>q.b[0].id);
  const targets=blocks.concat([-21,-22]).filter(t=>t!=id&&netOk(id,t)).map(t=>[String(t),ndFull(t)]);
  const sources=[-11,-12].concat(blocks).filter(f=>f!=id&&netOk(f,id)).map(f=>[String(f),ndFull(f)]);
  const sw=el("div","nrow");
  if(!isOutE(id))sw.appendChild(mk("+ Link to...",targets,v=>g({op:"lk",...lkArgs(id,v,0,0),v:0})));
  if(!isInE(id))sw.appendChild(mk("+ Receives from...",sources,v=>g({op:"lk",...lkArgs(v,id,0,0),v:0})));
  c.appendChild(sw);
  c.appendChild(el("div","hint","No link is automatic: drag from an output port (on the right of a block or of the input) to an input port (on the left of a block or of the output) to create an arrow, also diagonal, to the right or vertical (in the same column, up or down). A block with no incoming link makes no sound. Touch the level on an arrow to change or delete it."));
  return c;
}
/* card of an input or an output: levels, master volume (output) and links*/
function ioCard(pid){
  const c=ioPanel(isInE(pid)?"in":"out");
  const h=c.querySelector("h3 .nm");if(h)h.textContent=IOE[pid].n;
  c.appendChild(netPanel({id:pid}));
  return c;
}
/* ---- preset analysis in ADELGrid: static checks on the network (no listening) with explanation and suggestion ----*/
let anOn=false,anPcSeen=null;		/* the analysis closes by itself when the preset in use changes*/
/* analysis preferences, remembered in the browser: what comes after the output and the ignored warnings (per preset)*/
const lsGet=(k,d)=>{try{const v=localStorage.getItem(k);return v===null?d:v}catch(e){return d}};
const lsSet=(k,v)=>{try{localStorage.setItem(k,v)}catch(e){}};
const anDest=()=>lsGet("nb_dest","amp");        /* "amp" = power amp and cabinet after the output (no advice on cabinet and treble cut); "mix" = headphones, mixer or audio interface*/
const anIgnKey=()=>"nb_ign_"+(S.pc||"");
const anIgnored=()=>{try{return JSON.parse(lsGet(anIgnKey(),"[]"))}catch(e){return []}};
const anIgnore=t=>{const a=anIgnored();if(!a.includes(t)){a.push(t);lsSet(anIgnKey(),JSON.stringify(a))}};
const anUnignoreAll=()=>lsSet(anIgnKey(),"[]");
let anShowIgn=false;
const IC_AN='<svg viewBox="0 0 24 24"><circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L21 21M7.8 10.6l2 2 3.6-4.2"/></svg>';
/* "Levels" button (next to MIDI and Functions): shows or hides the blocks' level indicators, IN and OUT always stay; the choice is kept in the browser*/
const IC_LV='<svg viewBox="0 0 24 24"><path d="M6 20V12M12 20V5M18 20V9"/><path d="M3.5 20.5h17"/></svg>';
let bmOn=lsGet("bm","1")!=="0";
const bmApply=()=>document.body.classList.toggle("nobm",!bmOn);
/* estimated gain from the input to the output: Volume and Pan as they are, the links in dB, all the other blocks at gain 1 (the mono NAM)*/
function netGainEstimate(inL,inR){
  const N=S.board.net,all=netAll(),ids=N.cells.map(c=>c.b[0].id),B={};
  N.cells.forEach(c=>B[c.b[0].id]=c.b[0]);
  const lin=d=>Math.pow(10,d/20);
  const done=new Set(),order=[];
  for(let k=0;k<=ids.length;k++)ids.forEach(i=>{if(done.has(i))return;if(all.filter(l=>l.t==i).every(l=>isIo(l.f)||done.has(l.f))){done.add(i);order.push(i)}});
  const pair={},mono={};
  const src=f=>f==-11?[inL,0,true]:(f==-12?[0,inR,true]:[pair[f][0],pair[f][1],mono[f]]);
  const add=(acc,l)=>{
    const [sl,sr,sm]=src(l.f),g=lin(Math.max(-60,Math.min(12,l.db)));
    const pf=isInE(l.f)?(l.f==-11?1:2):l.pf,pt=isOutE(l.t)?(l.t==-21?1:2):l.pt;
    if(pf==0){if(pt==0){acc[0]+=sl*g;acc[1]+=sr*g;if(!sm)acc[2]=false}else{acc[pt-1]+=(sm?sl:(sl+sr)*.5)*g;acc[2]=false}}
    else{const s=pf==1?sl:sr;if(pt==0){acc[0]+=s*g;acc[1]+=s*g}else{acc[pt-1]+=s*g;acc[2]=false}}
  };
  order.forEach(i=>{
    const acc=[0,0,true];all.filter(l=>l.t==i).forEach(l=>add(acc,l));
    const b=B[i];let o=[acc[0],acc[1]],m=acc[2];
    if(b.on){
      if(b.t=="vol"){const g=lin(b.vol);o=[o[0]*g,o[1]*g]}
      else if(b.t=="pan"){const th=(b.pan*.01+1)*.78539816;o=[o[0]*Math.cos(th),o[1]*Math.sin(th)];m=false}
      else if(b.t=="nam"){const v=m?o[0]:(o[0]+o[1])*.5;o=[v,v];m=true}
    }
    pair[i]=o;mono[i]=m;
  });
  const out=[0,0,true];
  all.filter(l=>isOutE(l.t)).forEach(l=>add(out,l));
  return [out[0],out[1]];
}
function anLab(id){
  if(isIo(id))return ndName(id);
  const c=netCells().find(q=>q.b[0].id==id);if(!c)return "?";
  const b=c.b[0];
  const n=b.t=="nam"?(b.loaded&&b.name?b.name:(b.file||"NAM")):nmOf(b);
  return n+" (row "+(c.r+1)+", column "+(c.c+1)+")";
}
function analyzeNet(){
  const N=S.board.net,all=netAll(),res=[];
  const cells=N.cells.map(c=>c.b[0]),B={};cells.forEach(b=>B[b.id]=b);
  const nm=id=>"«"+esc(anLab(id))+"»";
  const bfs=(starts,next)=>{const seen=new Set(),st=starts.slice();while(st.length){const n=st.pop();if(seen.has(n))continue;seen.add(n);next(n).forEach(x=>st.push(x))}return seen};
  const fwd=bfs([-11,-12],n=>all.filter(l=>l.f==n).map(l=>l.t));
  const bwd=bfs([-21,-22],n=>all.filter(l=>l.t==n).map(l=>l.f));
  const anc=id=>bfs([id],n=>all.filter(l=>l.t==n).map(l=>l.f));
  const heavy=b=>b.t=="nam"||(b.t=="fx"&&b.fx!="adelphase"&&b.fx!="adelsendsweep"&&b.fx!="adelcomp"&&b.fx!="compressor");
  const R=(sev,title,why,fix,ids,act)=>res.push({sev,title,why,fix,ids:ids||[],act:act||null});
  const inLinked=[-11,-12].filter(i=>all.some(l=>l.f==i)),outLinked=[-21,-22].filter(i=>all.some(l=>l.t==i));
  if(!cells.length&&!all.length)R("info","The grid is empty","There are no blocks and no links: no signal passes.","Add the blocks and link them with arrows, from the input to the output.");
  else{
    if(!inLinked.length)R("problem","No input connected","Neither Input L nor Input R has an arrow: the guitar does not enter any block.","Drag from an output port of Input L (or R) to a block.",[-11,-12]);
    if(!outLinked.length)R("problem","No output connected","Neither Output L nor Output R receives an arrow: nothing comes out.","Drag from a block's output to the port of Output L (or R).",[-21,-22]);
    else if(outLinked.length==1)R("suggestion",outLinked[0]==-21?"Output R muted":"Output L muted","Solo "+nm(outLinked[0])+" receives signal: the other output channel stays silent (listening in stereo you hear only one side).","If you want the signal on both sides, link the same block to the other output too (if it is a NAM the signal is identical on L and R).",[outLinked[0]==-21?-22:-21]);
    else if(inLinked.length==1&&outLinked.length==2&&all.filter(l=>isOutE(l.t)).every(l=>true)){/* ok: mono in, stereo out */}
  }
  cells.forEach(b=>{
    const nin=all.filter(l=>l.t==b.id).length,nout=all.filter(l=>l.f==b.id).length;
    if(b.t=="fx"&&b.fx=="adelsendsweep")return;
    if(!nin)R("warning",nm(b.id)+" receives no signal","The block has no incoming arrow: it processes nothing and makes no sound."+(b.t=="nam"?" The NAM is not computed, so it does not weigh on the CPU.":""),"Link it to an output of another block or to the input, or delete it.",[b.id]);
    else if(!fwd.has(b.id))R("warning",nm(b.id)+" is disconnected from the input","It only receives arrows from blocks that in turn receive no signal: it stays silent.","Link the chain to Input L or R.",[b.id]);
    else if(!bwd.has(b.id))R("warning",nm(b.id)+" does not reach any output","It receives signal but its output does not lead to Output L or R: it works for nothing."+((b.t=="nam"||b.fx=="cabir")?" The NAM / Cab IR is not processed, so it does not weigh on the CPU.":""),"Link its output (or that of a downstream block) to an output, or delete it.",[b.id]);
    if(!b.on)R("info",nm(b.id)+" is off","A block that is off lets the signal through without processing it.","Turn it on again if you want to hear it.",[b.id]);
  });
  // parallel paths that meet again: risk of phase shift / comb effect
  const targets=cells.map(b=>b.id).concat([-21,-22]);
  const seenPair=new Set();
  targets.forEach(t=>{
    const inc=all.filter(l=>l.t==t),srcs=[...new Set(inc.map(l=>l.f))];
    for(let i=0;i<srcs.length;i++)for(let j=i+1;j<srcs.length;j++){
      const a=srcs[i],b=srcs[j],Aa=anc(a),Ab=anc(b);
      const common=[...Aa].filter(x=>Ab.has(x));
      if(!common.length)continue;                                    // independent sources: nothing to align
      const own=(A,O)=>[...A].filter(x=>!O.has(x)&&x>=0&&B[x]);
      const ba=own(Aa,Ab).map(x=>B[x]),bb=own(Ab,Aa).map(x=>B[x]);
      const ha=ba.filter(heavy),hb=bb.filter(heavy);
      if(!ha.length&&!hb.length)continue;                           // only Volume, Pan and Routing: no different delay
      const amp=q=>q.t=="nam"||(q.t=="fx"&&q.fx=="cabir");
      if(!ba.some(amp)||!bb.some(amp))continue;                    // alignment is for paths of models (NAM, Cab IR) that process the same signal: delays, reverbs and the like in parallel are different on purpose
      const tb=t>=0?B[t]:null;
      if(tb&&tb.t=="fx"&&tb.fx=="adelphase")continue;             // already aligned by ADELphase
      const key=t+":"+Math.min(a,b)+":"+Math.max(a,b);if(seenPair.has(key))continue;seenPair.add(key);
      const names=x=>x.length?x.map(q=>esc(anLab(q.id))).join(", "):"(no block of its own)";
      R("warning","Parallel paths that add up in "+nm(t),
        nm(a)+" and "+nm(b)+" start from the same signal and meet again. One branch goes through "+names(ha.length?ha:ba)+", the other through "+names(hb.length?hb:bb)+": different models or effects can delay or phase-shift the signal differently and, when summed, produce a comb effect (frequency notches), loss of level or a thin sound. With a delay D between the two paths the first cancellation falls at 1/(2D): about 500 Hz divided by the delay in ms (1 ms -> 500 Hz, 0.5 ms -> 1 kHz), then at 3/(2D), 5/(2D)...; inverting the polarity of one branch moves the notches.",
        "Press «Measure the phase» in the «Branch phase» section above: it adds the probe and ADELphase by itself, tells you what is wrong, and «Apply the correction» configures them.",
        ha.concat(hb).map(q=>q.id),{kind:"phase",t,a,b});
    }
  });
  // level: the estimated gain from the input to the output
  if(inLinked.length&&outLinked.length){
    const g=Math.max(...[[1,0],[0,1],[1,1]].map(v=>{const o=netGainEstimate(v[0],v[1]);return Math.max(o[0],o[1])}));
    const gd=20*Math.log10(Math.max(g,1e-6));
    if(gd>4)R("warning","The level may rise by about "+gd.toFixed(1)+" dB","Summing paths (or with positive gains) the estimate from the input to the output is +"+gd.toFixed(1)+" dB, not counting the blocks that add gain (NAM, distortions): risk of saturating the output.","Lower by about "+Math.ceil(gd)+" dB the links to the output or a Volume before the output.",[]);
    else if(gd<-24&&gd>-100)R("suggestion","Very low level ("+gd.toFixed(1)+" dB estimated)","With the current gains the signal reaches the output very attenuated.","Raise a Volume or the links, or check that no level is at -60 dB.",[]);
  }
  // from stereo to a single output: the stereo image is lost
  const mono=netMono();
  all.forEach(l=>{
    if(!l.imp&&l.pf==0&&l.pt!=0&&isOutE(l.t)&&l.f>=0&&mono[l.f]===false){
      const bk="st:"+l.f;if(seenPair.has(bk))return;seenPair.add(bk);
      R("suggestion",nm(l.f)+" is stereo but goes to a single output","The arrow from the stereo port sums L and R into a single channel ("+nm(l.t)+"): the stereo image is lost.","Link the L port of "+nm(l.f)+" to Output L and the R port to Output R (two arrows).",[l.f]);
    }
  });
  // unusual order: space effects before the NAM
  cells.filter(b=>b.t=="nam").forEach(n=>{
    const A=anc(n.id);
    const bad=[...A].filter(x=>x>=0&&B[x]&&B[x].t=="fx"&&["adelverb","adelverbm","adeldelay","adeldelaym","adelspring","reverb","delay"].includes(B[x].fx));
    if(bad.length)R("suggestion","Reverb or delay before "+nm(n.id),"Before the NAM go "+bad.map(x=>nm(x)).join(", ")+": the amplifier simulator also distorts the tails, and the result is often muddled.","Usually reverb, delay and spring go after the NAM.",[n.id].concat(bad));
  });
  // ADELphase and probe
  cells.filter(b=>b.t=="fx"&&b.fx=="adelphase").forEach(p=>{
    const ports=new Set(all.filter(l=>l.t==p.id).map(l=>l.pt));
    const l=ports.has(0)||ports.has(1),r=ports.has(0)||ports.has(2);
    if(!(l&&r))R("warning","ADELphase has a single input path","ADELphase compares the two channels it receives (L port and R port): with only one port connected it cannot measure anything.","Connect one path to the L port and the other to the R port.",[p.id]);
  });
  if(cells.some(b=>b.t=="fx"&&b.fx=="adelsendsweep")&&!cells.some(b=>b.t=="fx"&&b.fx=="adelphase"))R("info","The probe is there but not ADELphase","ADELSendSweep is used to measure with ADELphase.","Add an ADELphase if you want to check the phase between two paths.",[]);
  /* sound: equalization and level advice. Rules from guitar mixing practice and from the NAM documentation (see the sources at the bottom of the panel); it does not listen to the sound, so these are starting points to try by ear*/
  const DRV=["tsdrive","tubescreamer","ratts","muff","fuzzface","centaur","bluesdrv","ds1","ocd","guvnor","rangemaster","fz1","bender","fuzzfactory"];
  const down=id=>bfs([id],n=>all.filter(l=>l.f==n).map(l=>l.t));
  const live=cells.filter(b=>b.on&&fwd.has(b.id)&&bwd.has(b.id));
  const hasDown=(id,list)=>{const d=down(id);return live.some(b=>b.id!=id&&b.t=="fx"&&list.includes(b.fx)&&d.has(b.id))};
  const gainBefore=b=>[...anc(b.id)].some(x=>x>=0&&B[x]&&B[x].on&&(B[x].t=="nam"||(B[x].t=="fx"&&DRV.includes(B[x].fx))));
  const mixDest=anDest()=="mix";		/* the advice on cabinet, treble cut and equalization only applies if after the output there is no power amp with its cabinet*/
  live.filter(b=>b.t=="nam").forEach(n=>{
    if(mixDest&&!hasDown(n.id,["cabir","adelequal"]))R("suggestion",nm(n.id)+": no cabinet or equalizer downstream","After the NAM there is neither a Cab IR nor an ADELequal. A simulated head without a speaker has a lot of harsh treble above 5-6 kHz: guitar speakers typically cover 70 Hz - 5 kHz (catalog data, for example Celestion Vintage 30) and whoever mixes distorted guitars cuts the treble between 5 and 8 kHz.","If the model is a head or a pedal (not a full capture with cabinet), put a Cab IR after it, or an ADELequal with Low Cut 80-100 Hz and High Cut 6-8 kHz.",[n.id]);
    if(n.in>6)R("warning",nm(n.id)+": very high input (+"+n.in.toFixed(1)+" dB)","NAM models are captured at a precise input level (the NAM documentation calls it input_level_dbu): pushing it too far changes how much and how it distorts and can saturate the converter.","Bring the NAM's input back towards 0 dB and adjust the gain with the amplifier's knob or with the pedal in front.",[n.id]);
    else if(n.in<-12)R("suggestion",nm(n.id)+": very low input ("+n.in.toFixed(1)+" dB)","With an input level far below that of the capture the model distorts little and sounds duller than the real amplifier.","Raise the NAM's input towards 0 dB (the board is calibrated: 0 dBFS = +1.5 dBu).",[n.id]);
    if(n.out>6)R("warning",nm(n.id)+": high output (+"+n.out.toFixed(1)+" dB)","An output gain this high risks saturating the downstream blocks and the output.","Lower the NAM's output and adjust the final volume with a Volume or with the Master.",[n.id]);
  });
  (mixDest?live:[]).filter(b=>b.t=="fx"&&b.fx=="adelequal").forEach(b=>{
    const v=b.v||[],hi=gainBefore(b);
    if(v[1]<=20.5)R("suggestion",nm(b.id)+": Low Cut off","The lowest string of the guitar sounds at about 82 Hz: below that there is no music, only rumble, which muddies the bass and needlessly loads the speakers. Mixing guides recommend a high-pass at 80-100 Hz (100-120 Hz with high gains), with a gentle slope.","Set Low Cut to 80-100 Hz and listen whether the sound loses body: if so, go down a little.",[b.id]);
    if(hi&&v[2]>=19999)R("suggestion",nm(b.id)+": High Cut open after a distorted sound","After a NAM or a distortion pedal, the treble above 5-8 kHz is almost always buzz and fizz with no musical information: whoever mixes distorted guitars usually cuts between 5 and 8 kHz, lower if the gain is very high.","Set High Cut between 6 and 8 kHz (5-6 kHz with very high gain) and adjust by ear.",[b.id]);
    else if(hi&&v[2]>9000)R("info",nm(b.id)+": High Cut a "+Math.round(v[2])+" Hz","After a distorted sound, a cut above 9 kHz lets a lot of fizz through.","Try 6-8 kHz.",[b.id]);
    [[v[5],v[6],"1"],[v[8],v[9],"2"],[v[11],v[12],"3"]].forEach(q=>{
      const f=q[0],g=q[1];
      if(f>=200&&f<=500&&g>=3)R("suggestion",nm(b.id)+": +"+g.toFixed(1)+" dB in the 200-500 Hz zone (band "+q[2]+")","Between 200 and 500 Hz lies the 'mud': muddy, boxy sounds. A guitar has body here, but an emphasis adds up with the bass and the voice and covers the rest; usually this region is cut by 2-4 dB with a wide band (300-500 Hz).","Try bringing the gain back close to 0, or cutting 2-3 dB between 300 and 500 Hz.",[b.id]);
      if(hi&&f>=2500&&f<=5000&&g>=4)R("warning",nm(b.id)+": +"+g.toFixed(1)+" dB between 2.5 and 5 kHz (band "+q[2]+")","The 2-5 kHz zone gives attack and presence, but in distorted sounds between 3 and 5 kHz it becomes harsh and tires the ear; more than about 3-4 dB here is risky.","Reduce the emphasis to 2-3 dB or move the band towards 2 kHz, and keep the High Cut below 8 kHz.",[b.id]);
    });
    if(v[4]>=6&&v[3]<=250)R("suggestion",nm(b.id)+": bass shelf at +"+v[4].toFixed(1)+" dB","A strong boost below 250 Hz bloats the sound and makes it clash with the bass; the guitar has nothing useful below about 80 Hz.","Reduce to +2-3 dB or raise the Low Cut.",[b.id]);
    if(hi&&v[15]>=6)R("warning",nm(b.id)+": treble shelf at +"+v[15].toFixed(1)+" dB after a distorted sound","Raising the treble after a distortion emphasizes the fizz and the harshness.","Reduce to 0-2 dB and, if you need brightness, use a wide bell at 2-3 kHz.",[b.id]);
  });
  (mixDest?live:[]).filter(b=>b.t=="fx"&&b.fx=="cabir").forEach(b=>{
    const v=b.v||[];
    if(v[2]<50)R("info",nm(b.id)+": Low Cut a "+Math.round(v[2])+" Hz","Below 80 Hz a guitar has no music, only rumble.","Try Low Cut 80-100 Hz.",[b.id]);
    if(v[3]>9000&&gainBefore(b))R("info",nm(b.id)+": High Cut a "+Math.round(v[3])+" Hz","A real cabinet cuts the treble above 5-6 kHz: leaving it more open makes the fizz sound.","Try High Cut 6-8 kHz.",[b.id]);
  });
  if(live.some(b=>b.t=="fx"&&DRV.includes(b.fx))||live.some(b=>b.t=="nam"&&b.in>=3)){
    if(!cells.some(b=>b.on&&b.t=="fx"&&b.fx=="noisegate"))R("info","High gain without a Noise Gate","Distortion pedals and high-gain amplifiers also amplify hiss and hum, especially in the pauses between notes.","Try a Noise Gate (ToTest block) before the NAM or after the pedals: threshold about -55 dB, Reduction 40-60 dB.",[]);
  }
  return res;
}
/* ---- phase measurement: the analysis does by itself what used to be done by hand with ADELSendSweep and ADELphase ----*/
const PHV=["Waiting for the measurement","Phase OK","Invert the polarity of one branch","Delay branch B","Delay branch A","Invert the polarity and delay","The two paths are not correlated","Phase rotation","The sweep did not start"];
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let phBusy=false;
function netAnc(id){const all=netAll(),seen=new Set(),st=[id];while(st.length){const n=st.pop();if(seen.has(n))continue;seen.add(n);all.filter(l=>l.t==n).forEach(l=>st.push(l.f))}return seen}
/* an ADELphase that is on with two input paths (L port and R port) and a probe that is on upstream*/
function phFind(){
  const cells=netCells().map(c=>c.b[0]),all=netAll();
  const Ps=cells.filter(b=>b.t=="fx"&&b.fx=="adelphase"&&b.on),Ss=cells.filter(b=>b.t=="fx"&&b.fx=="adelsendsweep"&&b.on);
  for(const p of Ps){
    const ins=all.filter(l=>l.t==p.id),ports=new Set(ins.map(l=>l.pt));
    if(!((ports.has(0)||ports.has(1))&&(ports.has(0)||ports.has(2))))continue;
    const A=netAnc(p.id),s=Ss.find(x=>A.has(x.id));
    if(s)return {p,s,ins};
  }
  return {p:Ps[0]||null,s:Ss[0]||null,ins:[]};
}
async function phRead(pid){
  const j=await (await fetch("/m?b="+pid+"&_="+Date.now(),{cache:"no-store"})).json(),d=fxd("adelphase"),f=j.f||[],o={};
  d.p.forEach((q,k)=>{if(q.kind==3&&k<f.length)o[q.k]=f[k]});
  return o;
}
const phSet=async(sv,v)=>{const r=await g({op:"set",id:sv.id,k:v[0],v:v[1]});if(!r||!r.ok)throw new Error("the board refused "+v[0]+": "+((r&&r.err)||"no reply"));return r};
const phHz=i=>i>=1?100*Math.pow(40,(Math.round(i)-1)/23):0;      /* ADELphase frequency index (1..24) -> Hz*/
function phHtml(r,fin){
  const v=Math.max(0,Math.min(8,Math.round(r.verdict||0)));
  let h="<b>"+PHV[v]+"</b>";
  const ms=Math.abs(r.sugdelay||0);
  if(v==3||v==4||v==5)h+=" by about <b>"+ms.toFixed(2)+" ms</b>";
  if(v==2||v==5)h+=" (polarity inverted)";
  if(v==7)h+=": <b>"+Math.round(r.sugstages)+" all-pass stages at "+Math.round(phHz(r.sugfreq))+" Hz</b> on branch B";
  h+="<br>Correlation of the two paths: "+(r.corrin||0).toFixed(0)+"% at the input, "+(r.corrout||0).toFixed(0)+"% at the output; measured phase shift: "+(r.lag||0).toFixed(1)+" samples; confidence "+(r.conf||0).toFixed(0)+"%; level loss "+(r.loss||0).toFixed(1)+" dB.";
  if(v!=7&&Math.round(r.sugstages||0)>0)h+="<br>Recommended phase rotation (after delay and polarity): "+Math.round(r.sugstages)+" stages at "+Math.round(phHz(r.sugfreq))+" Hz on branch B.";
  if(v==6)h+="<br>The two paths do not have the same signal (for example two very different models): there is no alignment to do.";
  if(v==1&&!(Math.round(r.sugstages||0)>0))h+="<br>Nothing to correct: the two paths, when summed, do not cancel."+(r.done?" Delay and polarity are already set in ADELphase (Delay and Invert Polarity knobs).":"");
  if(v==7)h+="<br>Delay and polarity are fine but the phase changes with frequency: the all-pass stages realign it.";
  return h;
}
/* sends the probe and reads the verdict; shows the progress in the phres element*/
async function phMeasure(show,pOver){
  const f=phFind();
  const p=pOver||f.p;
  /* with a chosen ADELphase (alignment of all the branches) the probe is the one upstream of that ADELphase*/
  const s=pOver?(netCells().map(c=>c.b[0]).find(b=>b.t=="fx"&&b.fx=="adelsendsweep"&&b.on&&netAnc(p.id).has(b.id))||null):f.s;
  if(!p||!s)throw new Error("an ADELphase with two paths and an ADELSendSweep probe upstream are needed");
  await phSet(s,["play",0]);await sleep(500);await phSet(s,["play",1]);
  let r=null;
  for(let i=0;i<16;i++){
    await sleep(1200);
    r=await phRead(p.id);
    if(show)show("Measuring... "+Math.min(100,Math.round((i+1)*1.2/6*100))+"%");
    if(i>=4&&!r.stale&&Math.round(r.verdict||0)>=1&&Math.round(r.verdict||0)<=7)break;
  }
  await phSet(s,["play",0]);
  /* ADELphase measures the two input paths, so the delay and polarity verdict does not change after the correction: if the Delay and Invert Polarity knobs of ADELphase
     already have the suggested values, the correction is done and the verdict becomes «nothing to correct»*/
  try{
    await stNow();
    if(phDelayDone(r,p))r={...r,verdict:1,done:1};
  }catch(e){}
  return r;
}
/* the verdict asks for delay and/or polarity and the Delay and Invert Polarity knobs of ADELphase already have the suggested values (page state already updated)*/
function phDelayDone(r,p){
  const v=Math.round(r.verdict||0);
  if(v<2||v>5)return false;
  const cur=(netCells().map(c=>c.b[0]).find(b=>b.id==p.id)||{}).v||[],pd=fxd("adelphase").p;
  const kd=pd.findIndex(q=>q.k=="delay"),ki=pd.findIndex(q=>q.k=="invert");
  const inv=(v==2||v==5||(r.sugpol||0)>0.5)?1:0;
  return Math.abs((cur[kd]||0)-(r.sugdelay||0))<=0.06&&((cur[ki]||0)>0.5?1:0)==inv;
}
/* applies the correction: Auto Align of ADELphase, then measures again to confirm*/
async function phApply(show,r0,pOver){
  const p=pOver||phFind().p;
  const key=k=>fxd("adelphase").p.findIndex(q=>q.k==k);
  const out={steps:[],applied:[]};
  let r=r0||null;
  if(!r)r=await phMeasure(show,pOver);
  await stNow();		/* the knobs may already be right (correction applied earlier): in that case nothing is rewritten*/
  const v0=Math.round(r.verdict||0);
  if(phDelayDone(r,p))r={...r,verdict:1,done:1};
  else if(v0>=2&&v0<=5){
    /* delay and polarity are written into the Delay and Invert Polarity knobs (they stay in the preset); Auto Align is turned off: it kept them only inside the engine and lost them on reload*/
    const ms=Math.round(Math.max(-2,Math.min(2,r.sugdelay||0))*100)/100,inv=(v0==2||v0==5||(r.sugpol||0)>0.5)?1:0;
    await phSet(p,["auto",0]);await phSet(p,["delay",ms]);await phSet(p,["invert",inv]);
    out.applied.push("Delay "+ms.toFixed(2)+" ms"+(inv?", polarity inverted":", polarity unchanged"));
    await sleep(1500);
    r=await phMeasure(show,pOver);out.steps.push({tag:"After delay and polarity",r});
  }
  const st=Math.round(r.sugstages||0),hz=Math.round(phHz(r.sugfreq));
  if(pOver)await stNow();		/* with automatic alignment the page state must be re-read: the parameters have just changed*/
  const cur=(netCells().map(c=>c.b[0]).find(b=>b.id==p.id)||{}).v||[];
  const curS=Math.round(cur[key("apstages")]||0),curF=Math.round(cur[key("apfreq")]||0);
  if(st>0&&hz>0&&(st!=curS||Math.abs(hz-curF)>2)){
    await phSet(p,["apstages",st]);await phSet(p,["apfreq",hz]);out.applied.push("rotation: "+st+" stages at "+hz+" Hz");
    await sleep(1200);
    r=await phMeasure(show,pOver);out.steps.push({tag:"After the phase rotation",r});
  }
  out.final=r;
  return out;
}
/* prepares the measurement: temporarily adds the probe upstream of the two paths and an ADELphase at the summing point; it remembers what it changed to put everything back as it was*/
async function phPrepare(t,a,b){
  const all=netAll(),N=S.board.net,occ=new Set(netCells().map(c=>c.r+","+c.c));
  for(let i=0;i<4;i++)occ.add(N.io[i][0]+","+N.io[i][1]);
  /* free cell closest to the right side of the column range and to the preferred row: the probe sits near the blocks it feeds, ADELphase near the summing point*/
  const free=(c0,c1,pr)=>{const rows=[...Array(N.rows||6).keys()].sort((x,y)=>Math.abs(x-pr)-Math.abs(y-pr));for(let c=c1;c>=Math.max(0,c0);c--)for(const r of rows)if(!occ.has(r+","+c))return [r,c];return null};
  const ca=netAnc(a),cb=netAnc(b),ins=[-11,-12].filter(e=>ca.has(e)&&cb.has(e));
  if(!ins.length)throw new Error("the two paths do not start from the same input: there is no need to align them");
  const inLinks=all.filter(l=>ins.includes(l.f));
  const colIn=Math.max(...ins.map(colOf)),minCol=Math.min(...inLinks.map(l=>colOf(l.t)));
  const cS=free(colIn,Math.max(colIn,minCol-1),rowOfId(ins[0]))||free(minCol,minCol,rowOfId(ins[0]));
  if(!cS)throw new Error("there is no free cell between the input and the first blocks for the probe");
  occ.add(cS[0]+","+cS[1]);
  const colM=Math.max(colOf(a),colOf(b));
  const cP=free(colM,Math.max(colM,colOf(t)-1),rowOfId(t))||free(colOf(t),colOf(t),rowOfId(t));
  if(!cP)throw new Error("there is no free cell before the summing point for ADELphase");
  const La=all.filter(l=>l.f==a&&l.t==t),Lb=all.filter(l=>l.f==b&&l.t==t);
  if(!La.length||!Lb.length)throw new Error("links to analyze not found");
  const info={S:-1,P:-1,pc:S.pc||"",inLinks:inLinks.map(l=>({...l})),mergeLinks:La.concat(Lb).map(l=>({...l})),t,a,b};
  const ok=async(o)=>{const r=await g(o);if(!r||!r.ok)throw new Error((r&&r.err)||"operation refused");return r};
  const lk=(f,to,pf,pt,db)=>ok({op:"lk",...lkArgs(f,to,pf,pt),v:db});
  const ul=l=>ok({op:"ul",...lkArgs(l.f,l.t,l.pf,l.pt)});
  try{
    info.S=(await ok({op:"add",k:"adelsendsweep",r:cS[0],p:cS[1]})).id;
    info.P=(await ok({op:"add",k:"adelphase",r:cP[0],p:cP[1]})).id;
    for(const l of inLinks)await ul(l);
    for(const e of ins)await lk(e,info.S,0,0,0);
    for(const l of inLinks)await lk(info.S,l.t,0,l.pt,l.db);
    for(const l of info.mergeLinks)await ul(l);
    await lk(a,info.P,La[0].pf,1,La[0].db);
    await lk(b,info.P,Lb[0].pf,2,Lb[0].db);
    await lk(info.P,t,0,La[0].pt,0);
  }catch(e){
    try{await phRestore(info,false,true)}catch(e2){}
    throw e;
  }
  lsSet("nb_probe",JSON.stringify(info));
  return info;
}
/* adds only the probe when there already is an ADELphase with two input paths: it puts it between the input and the first blocks*/
async function phAddProbe(p){
  const all=netAll(),N=S.board.net,occ=new Set(netCells().map(c=>c.r+","+c.c));
  for(let i=0;i<4;i++)occ.add(N.io[i][0]+","+N.io[i][1]);
  const srcs=[...new Set(all.filter(l=>l.t==p.id).map(l=>l.f))].filter(x=>x>=0);
  if(srcs.length<2)throw new Error("ADELphase does not have two input paths");
  const anc=srcs.map(netAnc),ins=[-11,-12].filter(e=>anc.every(A=>A.has(e)));
  if(!ins.length)throw new Error("the two paths do not start from the same input: the probe cannot feed both of them");
  const inLinks=all.filter(l=>ins.includes(l.f));
  const colIn=Math.max(...ins.map(colOf)),minCol=Math.min(...inLinks.map(l=>colOf(l.t)));
  const rows=[...Array(N.rows||6).keys()].sort((x,y)=>Math.abs(x-rowOfId(ins[0]))-Math.abs(y-rowOfId(ins[0])));
  let cS=null;
  for(let c=Math.max(colIn,minCol-1);c>=colIn&&!cS;c--)for(const r of rows)if(!occ.has(r+","+c)){cS=[r,c];break}
  if(!cS)for(const r of rows)if(!occ.has(r+","+minCol)){cS=[r,minCol];break}
  if(!cS)throw new Error("there is no free cell between the input and the first blocks for the probe");
  const ok=async(o)=>{const r=await g(o);if(!r||!r.ok)throw new Error((r&&r.err)||"operation refused");return r};
  const lk=(f,to,pf,pt,db)=>ok({op:"lk",...lkArgs(f,to,pf,pt),v:db});
  const info={S:-1,P:-1,pc:S.pc||"",inLinks:inLinks.map(l=>({...l})),mergeLinks:[],t:-1,a:-1,b:-1};
  try{
    info.S=(await ok({op:"add",k:"adelsendsweep",r:cS[0],p:cS[1]})).id;
    for(const l of inLinks)await ok({op:"ul",...lkArgs(l.f,l.t,l.pf,l.pt)});
    for(const e of ins)await lk(e,info.S,0,0,0);
    for(const l of inLinks)await lk(info.S,l.t,0,l.pt,l.db);
  }catch(e){
    try{await phRestore(info,false,true)}catch(e2){}
    throw e;
  }
  lsSet("nb_probe",JSON.stringify(info));
  return info;
}
/* puts the preset back as it was: removes the probe and ADELphase and recreates the original links; with keepPhase it keeps ADELphase (the correction lives in it) and removes only the probe*/
async function phRestore(info,keepPhase,soft){
  /* soft: put back in order after an error in the middle of the preparation, without stopping at the first refused operation*/
  const one=async(o)=>{try{const r=await g(o);if(!r||!r.ok)throw new Error((r&&r.err)||"operation refused");return r}catch(e){if(!soft)throw e}};
  const lk=(f,to,pf,pt,db)=>one({op:"lk",...lkArgs(f,to,pf,pt),v:db});
  if(info.S>=0)await one({op:"del",id:info.S});
  if(!keepPhase&&info.P>=0)await one({op:"del",id:info.P});
  const back=keepPhase?info.inLinks:info.inLinks.concat(info.mergeLinks);
  for(const l of back)await lk(l.f,l.t,l.pf,l.pt,l.db);
  lsSet("nb_probe","");
}
/* the measurement in progress remembered in the browser (nb_probe) is valid only if its blocks still exist, with the right type, in the current preset: otherwise it is the leftover of a
   measurement of another preset or of a preset that was later modified, and it is discarded (it used to block «Prepare the measurement» and propose removing blocks that no longer existed)*/
function phProbe(){
  let pr=null;try{pr=JSON.parse(lsGet("nb_probe","")||"null")}catch(e){}
  if(!pr||typeof pr!="object")return null;
  const B={};netCells().forEach(c=>{B[c.b[0].id]=c.b[0]});
  const okS=pr.S>=0&&!!B[pr.S]&&B[pr.S].t=="fx"&&B[pr.S].fx=="adelsendsweep";
  const okP=pr.P<0||(!!B[pr.P]&&B[pr.P].t=="fx"&&B[pr.P].fx=="adelphase");
  const okPc=pr.pc===undefined||pr.pc===(S.pc||"");
  if(okS&&okP&&okPc)return pr;
  lsSet("nb_probe","");return null;
}
/* ---- «Branch phase» section: two buttons. «Measure the phase» adds the probe and ADELphase by itself (if they are missing), launches the sweep, says what is wrong and removes the probe; ADELphase stays.
   «Apply the correction» writes the delay, polarity and phase rotation into ADELphase and then measures again. The times after that, the measurement takes into account what is already in ADELphase. ----*/
let phMsg="",phFix=false,phPcSeen=null;
const phTwoIn=(p,all)=>{const ports=new Set(all.filter(l=>l.t==p.id).map(l=>l.pt));return (ports.has(0)||ports.has(1))&&(ports.has(0)||ports.has(2))};
/* what is left to correct, compared to the values ADELphase has now (page state already updated)*/
function phNeeds(r,p){
  const v=Math.round(r.verdict||0),st=Math.round(r.sugstages||0),hz=Math.round(phHz(r.sugfreq));
  const cur=(netCells().map(c=>c.b[0]).find(b=>b.id==p.id)||{}).v||[],pd=fxd("adelphase").p;
  const kS=pd.findIndex(q=>q.k=="apstages"),kF=pd.findIndex(q=>q.k=="apfreq");
  return {v,bad:v==6,delay:v>=2&&v<=5,rot:st>0&&hz>0&&(st!=Math.round(cur[kS]||0)||Math.abs(hz-Math.round(cur[kF]||0))>2)};
}
function phShort(r,p){
  const n=phNeeds(r,p),parts=[];
  if(n.bad)return "The two branches have signals that are too different: nothing to align.";
  if(n.delay){
    if(n.v==2)parts.push("polarity to invert");
    else parts.push((n.v==5?"polarity to invert and ":"")+"delay of "+Math.abs(r.sugdelay||0).toFixed(2)+" ms on branch "+(n.v==4?"A":"B"));
  }
  if(n.rot)parts.push("phase rotation: "+Math.round(r.sugstages)+" stages at "+Math.round(phHz(r.sugfreq))+" Hz");
  return parts.length?"To correct: "+parts.join("; ")+".":"All good.";
}
async function phRun(doApply){
  if(phBusy)return;
  phBusy=true;phFix=false;
  const say=h=>{phMsg=h;const e=document.getElementById("phmsg");if(e)e.innerHTML=h};
  const lines=[],doneP=new Set(),doneKeys=new Set();let probe=null,fixLeft=false;
  try{
    for(let it=0;it<10;it++){
      await stNow();
      const all=netAll(),cells=netCells().map(c=>c.b[0]);
      let P=cells.find(b=>b.t=="fx"&&b.fx=="adelphase"&&b.on&&!doneP.has(b.id)&&phTwoIn(b,all)),created=false;
      probe=null;
      if(P){
        /* ADELphase is already there: only the upstream probe is needed (if it is missing I add it and remove it at the end)*/
        if(!cells.some(b=>b.t=="fx"&&b.fx=="adelsendsweep"&&b.on&&netAnc(P.id).has(b.id))){
          say("Adding the probe...");
          probe=await phAddProbe(P);await sleep(800);await stNow();
        }
      }else{
        const act=analyzeNet().map(r=>r.act).find(a=>a&&a.kind=="phase"&&!doneKeys.has(a.t+":"+Math.min(a.a,a.b)+":"+Math.max(a.a,a.b)));
        if(!act)break;
        doneKeys.add(act.t+":"+Math.min(act.a,act.b)+":"+Math.max(act.a,act.b));
        say("Adding the probe and ADELphase...");
        probe=await phPrepare(act.t,act.a,act.b);created=true;await sleep(800);await stNow();
        P=netCells().map(c=>c.b[0]).find(b=>b.id==probe.P);
        if(!P)throw new Error("ADELphase not found after the preparation");
      }
      doneP.add(P.id);
      say("Sweep in progress...");
      let r=await phMeasure(say,P),applied=[];
      if(doApply){const o=await phApply(say,r,P);applied=o.applied;r=o.final||r}
      await stNow();
      const txt=phShort(r,netCells().map(c=>c.b[0]).find(b=>b.id==P.id)||P),ok=txt=="All good.";
      /* the probe is always removed; a newly created ADELphase that is not needed (all good, nothing applied) is removed with it and the links go back as they were*/
      if(probe){await phRestore(probe,!(created&&ok&&!applied.length));probe=null}
      lines.push({txt,ok,applied});
      if(!ok)fixLeft=true;
    }
    await stNow();
    phFix=fixLeft;
    if(!lines.length)say("There are no parallel branches to align.");
    else say(lines.map((l,i)=>(lines.length>1?"<b>Pair "+(i+1)+"</b>: ":"")+(l.applied.length?"Applied: "+l.applied.join("; ")+". ":"")+l.txt).join("<br>"));
  }catch(e){
    say("<span class=\"bad\">"+esc(e.message||e)+"</span>");
    if(probe){try{await phRestore(probe,false,true)}catch(e2){}}
    try{await stNow()}catch(e3){}
  }
  phBusy=false;
  build();upd();
}
function phSection(res){
  const all=netAll(),cells=netCells().map(c=>c.b[0]);
  const pr=phProbe();
  if(!pr&&!res.some(r=>r.act&&r.act.kind=="phase")&&!cells.some(b=>b.t=="fx"&&b.fx=="adelphase"&&b.on&&phTwoIn(b,all)))return null;
  if(phPcSeen!==(S.pc||"")){phPcSeen=S.pc||"";phMsg="";phFix=false}
  const box=el("div","phbox");box.id="phbox";
  box.appendChild(el("b","","Branch phase"));
  const m=el("div","phres");m.id="phmsg";m.innerHTML=phMsg||"After playing, press «Measure the phase»: I add the probe and ADELphase, launch the sweep (about 10 seconds) and tell you what is wrong.";box.appendChild(m);
  const row=el("div","row");box.appendChild(row);
  const b1=btn("Measure the phase","Adds the probe and ADELphase if they are missing, sends the sweep and says whether there is anything to correct",()=>phRun(false));b1.disabled=phBusy;row.appendChild(b1);
  if(phFix){const b2=btn("Apply the correction","Writes the recommended delay, polarity and phase rotation into ADELphase",()=>phRun(true));b2.disabled=phBusy;row.appendChild(b2)}
  if(pr&&!phBusy)row.appendChild(btn("Remove the probe","A probe is left over from an interrupted measurement: it removes it and keeps ADELphase",async()=>{try{await phRestore(pr,true)}catch(e){}try{await stNow()}catch(e){}build();upd()}));
  return box;
}
/* linear reordering of the blocks (positions only, never links or parameters): every block goes into the column of its level (longest path from the input), with a free column
   between one level and the next when there is room (they are needed by the probe and by the ADELphases); the rows follow the average of the rows of the upstream blocks to reduce crossings.
   Returns {blocks:[[id,row,column]...],io:[[row,column] x4]} or {err:"reason"}*/
function layoutNet(N,all){
  const rows=N.rows||6,cols=N.cols||12;
  const ids=N.cells.map(c=>c.b[0].id),isB=x=>x>=0;
  const preds={},succs={};ids.forEach(i=>{preds[i]=[];succs[i]=[]});
  all.forEach(l=>{if(isB(l.t)&&preds[l.t]&&(isB(l.f)?preds[l.f]:true))preds[l.t].push(l.f);if(isB(l.f)&&succs[l.f]&&(isB(l.t)?succs[l.t]:true))succs[l.f].push(l.t)});
  const depth={};let guard=0;
  const dep=i=>{if(depth[i]!==undefined)return depth[i];if(++guard>10000)return 1;depth[i]=1;let d=1;preds[i].forEach(p=>{if(isB(p))d=Math.max(d,dep(p)+1)});depth[i]=d;return d};
  ids.forEach(dep);
  const L=ids.length?Math.max(...ids.map(i=>depth[i])):0;
  /* inputs in column 0 and outputs in the last one: always at the two ends (the arrow labels stay readable); the levels sit in columns 1 to cols-3
     evenly distributed, with a free column before the output*/
  const span=cols-4;
  if(L>span+1)return {err:"too many levels of blocks in series for the grid"};
  /* if there are at least 2 columns between one level and the next (cap 3) the levels are evenly spaced and centered in the free area: the arrows have room for the labels;
     otherwise the extra columns are distributed among the levels*/
  /* the extra columns go where the arrows are most crowded: every link that crosses the space between two levels weighs 1, double if it has a port label (L/R, like
     in the ADELphase inputs); at most 2 extra columns per space, the rest stays as a margin on both sides*/
  const colArr=[],extra=[],gapScore=[];
  for(let d=0;d<=L;d++){extra.push(0);gapScore.push(0)}
  all.forEach(l=>{if(!isB(l.f)||!isB(l.t))return;for(let d=depth[l.f];d<depth[l.t];d++)gapScore[d]+=1+((l.pf||l.pt)?2:0)});
  let rem=Math.max(0,span-(L-1));
  while(L>1&&rem>0){
    let best=-1,bs=-1;
    for(let d=1;d<L;d++){if(extra[d]>=2)continue;const s=(gapScore[d]+0.5)/(1+extra[d]);if(s>bs){bs=s;best=d}}
    if(best<0)break;
    extra[best]++;rem--;
  }
  {let cc=L<=1?1+Math.floor(span/2):1+Math.floor(rem/2);for(let d=1;d<=L;d++){colArr[d]=cc;cc+=1+extra[d]}}
  const colOfLevel=d=>colArr[d];
  const outCol=cols-1;
  const byLevel={};ids.forEach(i=>{(byLevel[depth[i]]=byLevel[depth[i]]||[]).push(i)});
  for(const d in byLevel)if(byLevel[d].length>rows)return {err:"too many blocks in the same level (more than "+rows+")"};
  const pos={};
  /* the second (R) input or output port sits two rows from the first if there is room (a free row in between), otherwise next to it*/
  const sepRow=a=>a+2<=rows-1?a+2:(a-2>=0?a-2:(a<rows-1?a+1:a-1));
  const inRow=[2,4];
  const rowOfNode=x=>x==-11?inRow[0]:(x==-12?inRow[1]:(pos[x]?pos[x][0]:2.5));
  const place=(list,key)=>{
    /* distinct and increasing rows, as close as possible to the desired row*/
    const arr=list.map(i=>({i,d:key(i)})).sort((a,b)=>a.d-b.d||a.i-b.i),n=arr.length,r=[];
    /* minimum distance between rows: 2 (a free row in between) as long as they fit, otherwise 1: the blocks are not crammed*/
    const m=n>1&&(n-1)*2<=rows-1?2:1;
    arr.forEach((a,k)=>{let v=Math.round(a.d);v=Math.max(0,Math.min(rows-1,v));if(k>0&&v<r[k-1]+m)v=r[k-1]+m;r.push(v)});
    for(let k=n-1;k>=0;k--){const mx=rows-1-(n-1-k)*m;if(r[k]>mx)r[k]=mx;if(k<n-1&&r[k]>r[k+1]-m)r[k]=r[k+1]-m}
    arr.forEach((a,k)=>{pos[a.i]=[r[k],0]});
  };
  for(let pass=0;pass<3;pass++){
    for(let d=1;d<=L;d++){
      const list=byLevel[d]||[];if(!list.length)continue;
      place(list,i=>{
        const ps=preds[i].map(rowOfNode),ss=pass?succs[i].map(s=>isB(s)?(pos[s]?pos[s][0]:2.5):(s==-21?2:3)):[];
        const all2=ps.concat(ss);return all2.length?all2.reduce((a,b)=>a+b,0)/all2.length:2.5;
      });
      list.forEach(i=>{pos[i][1]=colOfLevel(d)});
    }
    const mean=(a,def)=>a.length?a.reduce((x,y)=>x+y,0)/a.length:def;
    const toRows=(f)=>all.filter(l=>l.f==f).map(l=>isB(l.t)?(pos[l.t]?pos[l.t][0]:2.5):(l.t==-21?2:3));
    const a0=mean(toRows(-11),2),a1=mean(toRows(-12),3);
    inRow[0]=Math.max(0,Math.min(rows-1,Math.round(a0)));inRow[1]=Math.max(0,Math.min(rows-1,Math.round(a1)));
    if(Math.abs(inRow[1]-inRow[0])<2)inRow[1]=sepRow(inRow[0]);
  }
  const fromRows=t=>all.filter(l=>l.t==t).map(l=>isB(l.f)?(pos[l.f]?pos[l.f][0]:2.5):(l.f==-11?inRow[0]:inRow[1]));
  const m=(a,d)=>a.length?a.reduce((x,y)=>x+y,0)/a.length:d;
  let o0=Math.max(0,Math.min(rows-1,Math.round(m(fromRows(-21),2)))),o1=Math.max(0,Math.min(rows-1,Math.round(m(fromRows(-22),3))));
  if(Math.abs(o1-o0)<2)o1=sepRow(o0);
  /* centers the whole set vertically: the row averages, rounded step by step, push everything downwards*/
  const allRows=ids.map(i=>pos[i][0]).concat([inRow[0],inRow[1],o0,o1]);
  const mn=Math.min(...allRows),mx=Math.max(...allRows);
  let sh=Math.floor((rows-1-(mx+mn))/2);
  sh=Math.max(-mn,Math.min(rows-1-mx,sh));
  const blocks=ids.map(i=>[i,pos[i][0]+sh,pos[i][1]]);
  const io=[[inRow[0]+sh,0],[inRow[1]+sh,0],[o0+sh,outCol],[o1+sh,outCol]];
  /* checks: distinct cells, all links to the right or vertical*/
  const seen=new Set(),colOfX=x=>x==-11||x==-12?0:(x==-21||x==-22?outCol:pos[x][1]);
  for(const b of blocks.map(x=>x.slice(1)).concat(io)){const k=b[0]+","+b[1];if(seen.has(k))return {err:"two elements in the same cell"};seen.add(k)}
  for(const l of all){if(colOfX(l.t)<colOfX(l.f))return {err:"a link would go to the left"}}
  return {blocks,io};
}
let anLayMsg="";
/* applies the reordering: remembers the previous positions (for «Put it back as it was»), sends the request and re-reads the state; if the board refuses it nothing changes*/
async function anRelayout(){
  try{
    await stNow();
    const N=S.board.net;if(!N){anLayMsg="";return}
    const lay=layoutNet(N,netAll());
    if(lay.err){anLayMsg="Blocks not reordered: "+esc(lay.err)+".";return}
    const cur=N.cells.map(c=>[c.b[0].id,c.r,c.c]),curIo=N.io.map(x=>x.slice());
    const same=lay.blocks.every(b=>cur.some(c=>c[0]==b[0]&&c[1]==b[1]&&c[2]==b[2]))&&lay.io.every((x,w)=>x[0]==curIo[w][0]&&x[1]==curIo[w][1]);
    if(same){anLayMsg="Blocks already in linear order.";return}
    lsSet("nb_lay_prev",JSON.stringify({pc:S.pc||"",blocks:cur,io:curIo}));
    const r=await g({op:"relayout",s:lay.blocks.map(b=>b.join(".")).join("_"),io:lay.io.map(x=>x.join(".")).join("_")});
    if(!r||!r.ok){anLayMsg="Blocks not reordered: "+esc((r&&r.err)||"error")+".";lsSet("nb_lay_prev","");return}
    anLayMsg="Blocks reordered linearly (links and parameters unchanged).";
    await stNow();
  }catch(e){anLayMsg="Blocks not reordered: "+esc(e.message||e)+"."}
}
async function anUndoLayout(){
  let p=null;try{p=JSON.parse(lsGet("nb_lay_prev","")||"null")}catch(e){}
  if(!p||p.pc!==(S.pc||"")){anLayMsg="I do not have the previous positions for this preset.";return}
  const r=await g({op:"relayout",s:p.blocks.map(b=>b.join(".")).join("_"),io:p.io.map(x=>x.join(".")).join("_")});
  if(r&&r.ok){anLayMsg="Previous positions restored.";lsSet("nb_lay_prev","")}else anLayMsg="I cannot restore the previous positions: "+esc((r&&r.err)||"error")+".";
  await stNow();
}
function anPanel(){
  const c=el("div","card an");
  const h=el("h3","");h.appendChild(el("span","nm","Preset analysis"));h.appendChild(el("span","grow"));
  h.appendChild(btn("✕","Close the analysis",()=>{anOn=false;build();upd()},"del"));
  c.appendChild(h);
  if(anLayMsg){
    const lr=el("div","row");lr.appendChild(el("span","info",anLayMsg+" "));
    if(/^Blocchi riordinati/.test(anLayMsg))lr.appendChild(btn("Put it back as it was","Puts the blocks back in their previous positions (never changes links or parameters)",async()=>{await anUndoLayout();build();upd()}));
    c.appendChild(lr);
  }
  const res=analyzeNet();
  const cnt=k=>res.filter(r=>r.sev==k).length;
  const ign=anIgnored(),shown=res.filter(r=>anShowIgn||!ign.includes(r.title));
  const sr=el("div","row");sr.appendChild(el("label","","After the output there is"));
  const ds=el("select","");[["amp","a power amp with its cabinet"],["mix","headphones, mixer or audio interface"]].forEach(o=>{const op=el("option","",o[1]);op.value=o[0];ds.appendChild(op)});ds.value=anDest();
  ds.onchange=()=>{lsSet("nb_dest",ds.value);build();upd()};sr.appendChild(ds);
  sr.appendChild(el("span","info"," With a power amp and a cabinet the analysis does not recommend cabinet, treble cut or equalization."));c.appendChild(sr);
  c.appendChild(el("div","info","Check of links, phase and levels from the preset's structure"+(anDest()=="mix"?", plus equalization advice":"")+" (it does not listen to the sound; the phase is measured with the «Measure the phase» button): "+(shown.length?shown.filter(r=>r.sev=="problem").length+" problems, "+shown.filter(r=>r.sev=="warning").length+" warnings, "+shown.filter(r=>r.sev=="suggestion"||r.sev=="info").length+" suggestions.":"no obvious problems.")));
  if(ign.length){const ig=el("div","row");ig.appendChild(el("span","info",ign.length+" warnings ignored for this preset. "));ig.appendChild(btn(anShowIgn?"Hide them":"Show them","Show or hide the ignored warnings",()=>{anShowIgn=!anShowIgn;build();upd()}));ig.appendChild(btn("Stop ignoring any","Restores all the warnings",()=>{anUnignoreAll();build();upd()}));c.appendChild(ig)}
  {const ps=phSection(res);if(ps)c.appendChild(ps)}
  const order={problem:0,warning:1,suggestion:2,info:3};
  shown.sort((a,b)=>order[a.sev]-order[b.sev]).forEach(r=>{
    const d=el("div","ani "+r.sev+(ign.includes(r.title)?" ignd":""));
    d.appendChild(el("b","",r.title));
    d.appendChild(el("p","","<i>Why:</i> "+r.why));
    d.appendChild(el("p","","<i>What to do:</i> "+r.fix));
    {const ib=btn(ign.includes(r.title)?"Stop ignoring":"Ignore","Do not show this warning again for this preset",()=>{if(ign.includes(r.title)){const a=anIgnored().filter(x=>x!=r.title);lsSet(anIgnKey(),JSON.stringify(a))}else anIgnore(r.title);build();upd()});ib.style.marginTop="4px";d.appendChild(ib)}
    if(r.ids&&r.ids.length){
      const w=el("div","anids");
      [...new Set(r.ids)].forEach(id=>{const b=btn(anLab(id),"Select the block",()=>{sel=id;build();upd()});w.appendChild(b)});
      d.appendChild(w);
    }
    c.appendChild(d);
  });
  const src=el("div","info");
  const grp=[
    ["Phase, polarity and delay between parallel paths",[
      ["https://www.masteringbox.com/learn/phase-alignment-polarity-time-correction","Phase Alignment: polarity, time and phase (MasteringBox)"],
      ["https://unity.neuraldsp.com/t/sample-delay-or-other-means-of-correcting-phase-between-two-amps/7873","Correcting the phase between two amps with a sample-level delay (Neural DSP forum)"],
      ["https://digital.livesoundint.com/march-2018/page-36","Comb effect and delays (Live Sound International, 2018)"]]],
    ["Order of the blocks (drive, amp, modulation, delay, reverb)",[
      ["https://www.thomannmusic.com/blog/gear/order-connect-effect-pedals/","Order of the effects: drive, modulation, delay and reverb (Thomann)"],
      ["https://stringjoy.com/effects-loop-amp/","Delay and reverb after the preamp, in the effects loop (Stringjoy)"]]],
    ["Noise gate",[
      ["https://unity.neuraldsp.com/t/noise-gate-block-placement-and-config/16865","Where to put the noise gate and how to set it (Neural DSP forum)"]]],
    ["NAM levels and calibration",[
      ["https://violetrecording.com/recording/gain-staging/","Gain staging: peaks between -12 and -6 dBFS (Violet Recording)"],
      ["https://neural-amp-modeler.readthedocs.io/en/latest/tutorials/calibration.html","Level calibration (Neural Amp Modeler documentation)"],
      ["https://www.tone3000.com/guides/nam-calibration","NAM calibration guide (TONE3000)"]]],
    ["Equalization: bass and treble cuts, problem zones",[
      ["https://emastered.com/blog/electric-guitar-eq","Electric guitar EQ (emastered)"],
      ["https://violetrecording.com/how-to-eq-electric-guitar/","How to EQ electric guitar: high-pass 80-120 Hz, low-pass 8-10 kHz, mud at 300-500 Hz (Violet Recording)"],
      ["https://violetrecording.com/how-to-eq-guitars-in-a-mix/","EQ of the guitars in a mix (Violet Recording)"],
      ["https://forum.fractalaudio.com/threads/overdrive-fizzyness.121899/","The drive's «fizz» between 5 and 6.5 kHz: filters and IRs (Fractal Audio forum)"]]],
    ["Equalization: before or after the amp (NAM)",[
      ["https://mixdownmag.com.au/features/how-to-develop-more-realistic-digital-guitar-tones/","EQ and compression on the direct signal before the amp simulator (Mixdown Magazine)"],
      ["https://forum.fractalaudio.com/threads/general-consensus-for-comp-eq-before-the-amp.101839/","EQ and compressor before or after the amp: the opinions (Fractal Audio forum)"]]]
  ];
  src.innerHTML="<b>Sources of the advice</b> (the forums report the experience of players, not measurements: they are indications, not rules)"+grp.map(g=>"<br><i>"+g[0]+"</i>: "+g[1].map(s=>"<a href=\""+s[0]+"\" target=\"_blank\" rel=\"noopener\">"+s[1]+"</a>").join(" &middot; ")).join("");
  c.appendChild(src);
  return c;
}
/* ---- MIDI: settings, table of the fixed CCs (like the Sonulab StompStation PRO), assignable controllers, block CCs and message monitor ----*/
let miOn=false,MID=null,miTimer=0;
const IC_MIDI='<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8.5"/><circle cx="7.6" cy="10.5" r="1" fill="currentColor"/><circle cx="12" cy="7.8" r="1" fill="currentColor"/><circle cx="16.4" cy="10.5" r="1" fill="currentColor"/><circle cx="9.2" cy="15.4" r="1" fill="currentColor"/><circle cx="14.8" cy="15.4" r="1" fill="currentColor"/></svg>';
const MCLS=["","Drive","Modulation before the NAM","Amp (NAM 1)","Cab IR","Reverb","Modulation after the NAM","Tremolo","Compressor","Gate","Delay 1","Tuner","Expr","Boost","Wah","EQ","Pitch"];
async function miLoad(){try{MID=await (await fetch("/midi?_="+Date.now(),{cache:"no-store"})).json()}catch(e){MID=null}}
function miStart(){miStop();miLoad().then(()=>{if(miOn)build()});miTimer=setInterval(async()=>{if(!miOn){miStop();return}if(idleNow()||document.hidden)return;const p=$("midipanel");if(p&&p.contains(document.activeElement)&&/INPUT|SELECT/.test(document.activeElement.tagName))return;await miLoad();if(miOn&&$("midipanel"))miFill($("midipanel"))},1500)}
function miStop(){if(miTimer){clearInterval(miTimer);miTimer=0}}
const miBlk=id=>netCells().map(c=>c.b[0]).find(b=>b.id==id);
const miName=id=>{const b=miBlk(id);return b?esc(anLab(id)):"(block removed)"};
function miParams(b){
  if(!b)return [];
  if(b.t=="nam")return [[0,"Input"],[1,"Output"]];
  if(b.t=="vol")return [[0,"Volume"]];
  if(b.t=="pan")return [[0,"Pan"]];
  if(b.t=="fx"){const d=fxd(b.fx);return d?d.p.map((p,k)=>[k,p.l||p.k]).filter((x,k)=>d.p[k].kind!=3):[]}
  return [];
}
const miSend=async o=>{const r=await g(o);if(r&&!r.ok)alert(r.err||"error");await miLoad();if($("midipanel"))miFill($("midipanel"))};
function miFill(c){
  c.innerHTML="";
  const h=el("h3","");h.appendChild(el("span","nm","MIDI"));h.appendChild(el("span","grow"));
  h.appendChild(btn("✕","Close the MIDI panel",()=>{miOn=false;miStop();build();upd()},"del"));
  c.appendChild(h);
  if(!MID){c.appendChild(el("div","info bad","The pedalboard does not respond: retrying."));return}
  const M=MID,grid=S.board.mode==2;
  const st=el("div","info");
  st.innerHTML="USB: <b>"+(M.usb?"controller connected":"no controller")+"</b> &middot; received "+M.rx+", sent "+M.tx+(M.drop?", lost "+M.drop:"")+" &middot; tempo: <b>"+(M.bpm>0?M.bpm.toFixed(1)+" bpm"+(M.clk?" (clock MIDI)":""):"none")+"</b>. Pisound DIN: <b>"+(M.din==2?"active (MCU "+(M.dinfw>>8)+"."+("0"+(M.dinfw&255)).slice(-2)+")":(M.din==1?"starting":"not active"))+"</b>.";
  c.appendChild(st);
  const r1=el("div","row");r1.appendChild(el("label","","Channel"));
  const cs=el("select","");[["0","All"]].concat(Array.from({length:16},(_,i)=>[String(i+1),String(i+1)])).forEach(o=>{const op=el("option","",o[1]);op.value=o[0];cs.appendChild(op)});cs.value=String(M.chan);
  cs.onchange=()=>miSend({op:"midiset",c:+cs.value});r1.appendChild(cs);
  const ec=el("input");ec.type="checkbox";ec.checked=!!M.echo;ec.onchange=()=>miSend({op:"midiset",e:ec.checked?1:0});
  const el1=el("label","");el1.style.marginLeft="14px";el1.appendChild(ec);el1.appendChild(document.createTextNode(" Echo: reply with the same CC (controller LED)"));r1.appendChild(el1);
  r1.appendChild(btn("Tap tempo","Like CC 23: tap in time to set the tempo",()=>miSend({op:"midiinj",ty:2,ch:0,a:23,b:127})));
  const sy=el("input");sy.type="checkbox";sy.checked=!!M.sync;sy.onchange=()=>miSend({op:"midiset",s:sy.checked?1:0});
  const sl=el("label","");sl.style.marginLeft="14px";sl.title="The tempo of the MIDI clock or of the tap tempo becomes the base tempo of the ADEL delays (the subdivision stays the one chosen in the block). The ToTest delays have their own Sync parameter.";sl.appendChild(sy);sl.appendChild(document.createTextNode(" The ADEL delays follow the tempo"));r1.appendChild(sl);
  c.appendChild(r1);
  // fixed CCs
  c.appendChild(el("h4","","Fixed CCs (0-63 off, 64-127 on)"));
  const t=el("table","mitab");
  t.innerHTML="<tr><th>CC</th><th>Function</th><th>Block in the preset</th><th>State</th><th></th></tr>";
  M.cls.forEach(q=>{
    const tr=el("tr","");const id=q[1],on=q[2];
    tr.innerHTML="<td>"+q[0]+"</td><td>"+esc(MCLS[q[0]])+"</td><td>"+(id>=0?miName(id):"<i>"+(grid?"none":"only in ADELGrid")+"</i>")+"</td><td>"+(id>=0?(on?"on":"off"):"")+"</td>";
    const td=el("td","");
    if(id>=0){td.appendChild(btn(on?"Turn off":"Turn on","Send CC "+q[0]+" as the controller would do",()=>miSend({op:"midiinj",ty:2,ch:0,a:q[0],b:on?0:127})))}
    tr.appendChild(td);t.appendChild(tr);
  });
  [[20,"Master volume (0-127)"],[21,"Next preset"],[22,"Previous preset"],[23,"Tap tempo"]].forEach(q=>{const tr=el("tr","");tr.innerHTML="<td>"+q[0]+"</td><td>"+q[1]+"</td><td></td><td></td><td></td>";t.appendChild(tr)});
  c.appendChild(t);
  c.appendChild(el("div","info","Program Change: the number is the position of the preset in the list (PC 0 = number 1). The \"first block\" of a function is the leftmost one in the grid; with several blocks of the same type, the others are controlled with a CC assigned to the block."));
  // block CCs
  c.appendChild(el("h4","","CCs assigned to the blocks (from 32 to 127)"));
  const bl=el("div","");
  if(!M.bcc.length)bl.appendChild(el("div","info","None."));
  M.bcc.forEach(q=>{const d=el("div","row");d.appendChild(el("span","","CC "+q[0]+" &rarr; "+miName(q[1])));d.appendChild(btn("Remove","Remove the CC from this block",()=>miSend({op:"midicc",id:q[1],cc:0})));bl.appendChild(d)});
  const r2=el("div","row");
  if(grid&&typeof sel=="number"&&miBlk(sel)){
    const learning=M.lc==2&&M.lid==sel;
    r2.appendChild(btn(learning?"Waiting for a CC... (cancel)":"Learn CC for the selected block","Press a key on the controller: its CC turns the selected block on or off",()=>miSend({op:"midilearn",k:2,id:learning?-1:sel})));
    const ni=el("input");ni.type="number";ni.min=32;ni.max=127;ni.placeholder="CC";ni.style.width="64px";r2.appendChild(ni);
    r2.appendChild(btn("Assign","Assign this CC to the selected block",()=>miSend({op:"midicc",id:sel,cc:+ni.value||0})));
  }else r2.appendChild(el("span","info","Select a block in the grid to assign it a CC."));
  bl.appendChild(r2);c.appendChild(bl);
  // assignable controllers
  c.appendChild(el("h4","","Assignable controllers (continuous parameters, e.g. expression pedal)"));
  M.ctl.forEach((q,ci)=>{
    const d=el("div","mictl");
    const hd=el("div","row");hd.appendChild(el("b","","Controller "+(ci+1)+(ci==2?" (global)":" (of the preset)")));
    const ni=el("input");ni.type="number";ni.min=32;ni.max=127;ni.value=q.cc||"";ni.placeholder="CC";ni.style.width="64px";
    ni.onchange=()=>miSend({op:"midictl",c:ci,cc:+ni.value||0});hd.appendChild(ni);
    const learning=M.lc==1&&M.lid==ci;
    hd.appendChild(btn(learning?"Waiting... (cancel)":"Learn","Move a control on the controller: the first CC (32-127) that arrives becomes the source",()=>miSend({op:"midilearn",k:1,id:learning?-1:ci})));
    d.appendChild(hd);
    q.l.forEach(l=>{
      const r=el("div","row");const b=miBlk(l[1]);const pn=miParams(b).find(x=>x[0]==l[2]);
      r.appendChild(el("span","",miName(l[1])+" &middot; "+esc(pn?pn[1]:"parameter "+l[2])+" ("+l[3]+"% - "+l[4]+"%)"));
      r.appendChild(btn("Remove","Unlink the parameter",()=>miSend({op:"midilink",c:ci,k:l[0],id:-1})));d.appendChild(r);
    });
    if(grid&&q.l.length<3){
      const r=el("div","row");const bs=el("select","");bs.appendChild(el("option","","Block..."));
      netCells().map(c2=>c2.b[0]).forEach(b=>{const o=el("option","",anLab(b.id));o.value=b.id;bs.appendChild(o)});
      const ps=el("select","");ps.appendChild(el("option","","Parameter..."));
      bs.onchange=()=>{ps.innerHTML="";miParams(miBlk(+bs.value)).forEach(x=>{const o=el("option","",x[1]);o.value=x[0];ps.appendChild(o)})};
      const mn=el("input");mn.type="number";mn.min=0;mn.max=100;mn.value=0;mn.style.width="58px";mn.title="Minimum (%)";
      const mx=el("input");mx.type="number";mx.min=0;mx.max=100;mx.value=100;mx.style.width="58px";mx.title="Maximum (%)";
      r.appendChild(bs);r.appendChild(ps);r.appendChild(mn);r.appendChild(el("span",""," - "));r.appendChild(mx);
      r.appendChild(btn("Link","Link the parameter to the controller",()=>{if(bs.value===""||isNaN(+bs.value)||ps.value==="")return alert("Choose block and parameter");const used=new Set(q.l.map(l=>l[0]));let k=0;while(used.has(k))k++;miSend({op:"midilink",c:ci,k,id:+bs.value,p:+ps.value,mn:+mn.value,mx:+mx.value})}));
      d.appendChild(r);
    }
    c.appendChild(d);
  });
  // monitor
  c.appendChild(el("h4","","MIDI monitor (latest messages, the most recent on top)"));
  /* after every preset change the board sends the controllers about twenty status messages (CC, preset echo): by default only the messages RECEIVED from the controller are shown*/
  const showOut=lsGet("mi_out","0")==="1",nOut=M.mon.filter(m=>m.d!="i").length;
  {const lab=el("label","info");const cb=el("input");cb.type="checkbox";cb.checked=showOut;
   cb.onchange=()=>{lsSet("mi_out",cb.checked?"1":"0");const p=$("midipanel");if(p)miFill(p)};
   lab.appendChild(cb);lab.appendChild(document.createTextNode(" Also show the messages sent by the board (CC state, preset echo)"+(showOut?"":": "+nOut+" hidden")));c.appendChild(lab)}
  const mon=el("div","mimon");
  const shown=showOut?M.mon:M.mon.filter(m=>m.d=="i");
  shown.forEach(m=>{mon.appendChild(el("div","mi"+m.d,(m.d=="i"?"&larr; ":"&rarr; ")+esc(m.t)))});
  if(!shown.length)mon.appendChild(el("div","info",showOut?"No messages yet.":"No messages received yet."));
  c.appendChild(mon);
  const r3=el("div","row");r3.appendChild(el("span","info","Test message (as if it came from the controller): "));
  const ty=el("select","");[["2","CC"],["1","Program Change"]].forEach(o=>{const op=el("option","",o[1]);op.value=o[0];ty.appendChild(op)});
  const a=el("input");a.type="number";a.min=0;a.max=127;a.value=5;a.style.width="64px";
  const b=el("input");b.type="number";b.min=0;b.max=127;b.value=127;b.style.width="64px";
  r3.appendChild(ty);r3.appendChild(a);r3.appendChild(b);
  r3.appendChild(btn("Send","Puts the message in the MIDI queue",()=>miSend({op:"midiinj",ty:+ty.value,ch:0,a:+a.value,b:+b.value})));
  c.appendChild(r3);
}
function miPanel(){const c=el("div","card midi");c.id="midipanel";miFill(c);return c}
/* ---- Service functions with a switch (all off at startup): hum killer, rumble filter, limiter, mono check, air, console, metronome, spectrum, tuner, meters ----*/
let ftOn=false,FT=null,ftTimer=0;
const IC_FT='<svg viewBox="0 0 24 24"><path d="M5 5v14M12 5v14M19 5v14"/><circle cx="5" cy="9" r="2" fill="currentColor"/><circle cx="12" cy="15" r="2" fill="currentColor"/><circle cx="19" cy="8" r="2" fill="currentColor"/></svg>';
const FTDEF=[
  ["hum","Hum Killer","Removes the mains hum from the input (50 or 60 Hz and the first harmonics).",[["sel","mains",[[50,"50 Hz"],[60,"60 Hz"]]]]],
  ["rumble","Rumble Filter","80 Hz high-pass on the input: removes knocks, wind and low stage noises.",[]],
  ["limit","Master Limiter","Brick-wall limiter on the output: the signal does not exceed the ceiling. It protects amplifier and speakers.",[["num","ceil","Ceiling (dB)",-12,0,1]]],
  ["mono","Mono Check","Sums left and right to mono on the output: it is used to check phase compatibility and the sound on a single speaker.",[]],
  ["air","Air Band","12 kHz shelf (Maag EQ4 style): opens the highs without making them harsh.",[["num","airdb","Air (dB)",0,12,1]]],
  ["sat","Console Glue","Soft console or tape saturation: rounds the peaks.",[["num","satdrive","Drive (%)",0,100,5]]],
  ["click","Metronome","Click in time mixed into the output, with an accent on the first of four beats.",[["num","bpm","Tempo (bpm)",40,240,1],["num","clickdb","Level (dB)",-40,-6,1]]],
  ["rta","Spectrum","Third-octave analyzer of the output (63 Hz - 12.7 kHz): shows where the energy is.",[]],
  ["tuner","Tuner","Chromatic tuner on the input.",[["chk","tmute","Mutes the output while you tune"]]],
  ["meter","Meters","Momentary loudness (LUFS style), peak, peak/RMS ratio and phase correlation between left and right.",[]]
];
async function ftLoad(){try{FT=await (await fetch("/feat?_="+Date.now(),{cache:"no-store"})).json()}catch(e){FT=null}}
function ftStart(){ftStop();apLoad();thLoad();mcLoad();ftLoad().then(()=>{if(ftOn)build()});ftTimer=setInterval(async()=>{if(!ftOn){ftStop();return}if(idleNow()||document.hidden)return;const p=$("ftpanel");if(p&&p.contains(document.activeElement)&&/INPUT|SELECT/.test(document.activeElement.tagName))return;await ftLoad();await thLoad();if(thStat)thStat.textContent=thText();await mcLoad();if(mcStat)mcStat.textContent=mcText();if(ftOn&&$("ftpanel"))ftFill($("ftpanel"))},2000)}
function ftStop(){if(ftTimer){clearInterval(ftTimer);ftTimer=0}}
const ftSend=async(k,v)=>{const r=await g({op:"feat",k,v});if(r&&!r.ok)alert(r.err||"error");await ftLoad();if($("ftpanel"))ftFill($("ftpanel"))};
function ftRta(){
  const d=el("div","ftrta");let h="";
  FT.rta.forEach((db,k)=>{const pc=Math.max(0,Math.min(100,(db+90)/90*100)),fc=63*Math.pow(2,k/3);const lab=k%3==0?(fc>=1000?(fc/1000).toFixed(fc>=10000?0:1)+"k":String(Math.round(fc))):"";h+='<div class="ftb"><i style="height:'+pc.toFixed(0)+'%"></i><span>'+lab+'</span></div>'});
  d.innerHTML=h;return d;
}
function ftTuner(){
  const d=el("div","fttun"),t=FT.tun;
  if(!t){d.innerHTML='<span class="ftnote dim">&ndash;</span><div class="info">Play a string: no signal recognized.</div>';return d}
  const c=Math.max(-50,Math.min(50,t.cents));
  d.innerHTML='<span class="ftnote">'+esc(t.note)+'</span><span class="ftcents">'+(t.cents>0?"+":"")+t.cents+' cent</span><span class="info">'+t.hz.toFixed(1)+' Hz</span><div class="ftbar"><i style="left:'+(50+c).toFixed(0)+'%"></i><b></b></div>'+(Math.abs(t.cents)<=5?'<div class="ftok">In tune</div>':(t.cents<0?'<div class="info">Flat: tighten</div>':'<div class="info">Sharp: loosen</div>'));
  return d;
}
function ftMeters(){
  const d=el("div","ftmet"),f=(v,u)=>v<=-119?"&ndash;":v.toFixed(1)+u;
  d.innerHTML="<div><small>Loudness (momentary)</small><b>"+f(FT.lufs," LUFS")+"</b></div><div><small>Peak</small><b>"+f(FT.peak," dBFS")+"</b></div><div><small>Peak/RMS</small><b>"+(FT.crest>0?FT.crest.toFixed(1)+" dB":"&ndash;")+"</b></div><div><small>L/R correlation</small><b>"+FT.corr.toFixed(2)+"</b></div>";
  const n=el("div","info","Correlation: +1 = mono signal, 0 = uncorrelated, -1 = out of phase (disappears in mono).");
  const w=el("div","");w.appendChild(d);w.appendChild(n);return w;
}
function ftFill(c){
  c.innerHTML="";
  const h=el("h3","");h.appendChild(el("span","nm","Functions (ToTest)"));h.appendChild(el("span","grow"));
  h.appendChild(btn("✕","Close the panel",()=>{ftOn=false;ftStop();build();upd()},"del"));
  c.appendChild(h);
  if(!FT){c.appendChild(el("div","info bad","The pedalboard does not respond: retrying."));return}
  c.appendChild(el("div","info","Ten service functions, all off at startup. The signal ones (hum killer, rumble filter, limiter, mono, air, console) stay as you leave them; metronome, spectrum, tuner and meters always restart off."));
  FTDEF.forEach((d,i)=>{
    const row=el("div","ftrow");
    const sw=switchEl(v=>ftSend(d[0],v));sw.sync(FT.on[i]);row.appendChild(sw);
    const tx=el("div","fttx");tx.appendChild(el("b","",d[1]));tx.appendChild(el("div","info",d[2]));row.appendChild(tx);
    const pr=el("div","ftpr");
    d[3].forEach(q=>{
      if(q[0]=="sel"){const s=el("select","");q[2].forEach(o=>{const op=el("option","",o[1]);op.value=o[0];s.appendChild(op)});s.value=String(FT[q[1]]);s.onchange=()=>ftSend(q[1],+s.value);pr.appendChild(s)}
      else if(q[0]=="num"){const lb=el("label","",q[2]+" ");const inp=el("input");inp.type="number";inp.min=q[3];inp.max=q[4];inp.step=q[5];inp.value=FT[q[1]];inp.style.width="68px";inp.onchange=()=>ftSend(q[1],+inp.value);lb.appendChild(inp);pr.appendChild(lb)}
      else{const lb=el("label","");const cb=el("input");cb.type="checkbox";cb.checked=FT[q[1]]>0.5;cb.onchange=()=>ftSend(q[1],cb.checked?1:0);lb.appendChild(cb);lb.appendChild(document.createTextNode(" "+q[2]));pr.appendChild(lb)}
    });
    if(d[0]=="limit"&&FT.on[i])pr.appendChild(el("span","info","reduction "+FT.gr.toFixed(1)+" dB"));
    row.appendChild(pr);c.appendChild(row);
    if(d[0]=="rta"&&FT.on[i])c.appendChild(ftRta());
    if(d[0]=="tuner"&&FT.on[i])c.appendChild(ftTuner());
    if(d[0]=="meter"&&FT.on[i])c.appendChild(ftMeters());
  });
}
function ftPanel(){const c=el("div","card ft");c.id="ftpanel";ftFill(c);return c}
/* ---- processor cores: the NAMs of independent branches are computed on different cores (experimental, from the next boot) ----*/
let MCC=null,mcEl=null,mcStat=null;
async function mcLoad(){try{MCC=await (await fetch("/mc?_="+Date.now(),{cache:"no-store"})).json()}catch(e){MCC=null}}
const mcText=()=>MCC?"Cores in use: "+MCC.cores+" · blocks computed with several cores: "+MCC.blocks+" · late: "+(MCC.late||0)+" · maximum wait: "+(MCC.maxus||0)+" us · faults: "+MCC.faults:"";
function mcCard(){
  if(!MCC)return null;
  if(mcEl){mcStat.textContent=mcText();return mcEl}
  const c=el("div","card mc");mcEl=c;
  const h=el("h3","");h.appendChild(el("span","nm","Processor cores (experimental)"));c.appendChild(h);
  mcStat=el("div","info","");mcStat.textContent=mcText();c.appendChild(mcStat);
  if(!MCC.avail){c.appendChild(el("div","info bad","This firmware version is not built with multi-core support."));return c}
  c.appendChild(el("div","info","Usually the audio and everything else run on a single core. With several cores, the NAMs on independent branches of the grid (two amplifiers in parallel, for example) are computed in parallel: in the estimated load they count for the longest path and not for the sum, so more of them fit. Two NAMs one after the other stay on the same core. It applies from the next boot; if a core does not respond in time, the system goes back to core 0 by itself."));
  const r=el("div","row");r.appendChild(el("label","","Extra worker cores"));
  const s=el("select");[["0","none (core 0 only)"],["1","1"],["2","2"],["3","3"]].forEach(o=>{const op=el("option","",o[1]);op.value=o[0];s.appendChild(op)});s.value=String(MCC.cfg);r.appendChild(s);c.appendChild(r);
  const bar=el("div","row");
  bar.appendChild(btn("Save","Save the choice (it applies from the next boot)",async()=>{
    const r2=await g({op:"mcset",v:+s.value});
    if(r2&&r2.ok){MCC.cfg=+s.value;alert("Saved. It applies from the next boot.")}else alert((r2&&r2.err)||"error");
  }));
  c.appendChild(bar);
  return c;
}
/* ---- temperature and fan: thresholds of the active heatsink fan and maximum processor frequency ----*/
let THC=null,thEl=null,thStat=null;
async function thLoad(){try{THC=await (await fetch("/therm?_="+Date.now(),{cache:"no-store"})).json()}catch(e){THC=null}}
const thText=()=>THC?"Temperature "+(THC.temp/10).toFixed(1)+" °C · processor at "+THC.mhz[0]+" MHz (maximum "+THC.mhz[2]+") · fan "+(THC.fanctl?(THC.fp>0?"at "+THC.fp+"%":(THC.fp===undefined&&THC.fan?"on":"off")):"managed by the system"):"";
function thCard(){
  if(!THC)return null;
  if(thEl){thStat.textContent=thText();return thEl}
  const c=el("div","card th");thEl=c;
  const h=el("h3","");h.appendChild(el("span","nm","Temperature and fan"));c.appendChild(h);
  thStat=el("div","info","");thStat.textContent=thText();c.appendChild(thStat);
  const chk=(lab,v)=>{const r=el("div","row");const l=el("label","");const cb=el("input");cb.type="checkbox";cb.checked=!!v;l.appendChild(cb);l.appendChild(document.createTextNode(" "+lab));r.appendChild(l);c.appendChild(r);return cb};
  const num=(lab,v,a,b)=>{const r=el("div","row");r.appendChild(el("label","",lab));const i=el("input");i.type="number";i.min=a;i.max=b;i.step=1;i.value=v;i.style.width="68px";r.appendChild(i);c.appendChild(r);return i};
  const fc=chk("I control the fan with these thresholds",THC.fanctl);
  const on=num("Turns on above (°C)",THC.on,40,80);
  const off=num("Turns off below (°C)",THC.off,30,78);
  const mx=num("Maximum speed up to 68 °C (%)",THC.mx||55,35,100);
  const bo=chk("Processor at 2400 MHz at the next boot (the NAMs cost less CPU, but the fan heats up and is heard more)",THC.boost);
  c.appendChild(el("div","info","With my control the fan has a variable speed: at the turn-on threshold it starts at 30%, rises with the temperature up to the chosen maximum speed at 68 °C (the quiet part) and only from 68 to 75 °C does it rise to 100% (protection). If I do not control the fan, the system manages it as before. The thresholds apply immediately; to go back to system management a reboot is needed. Above 82 °C the processor goes back to 1500 MHz by itself. The chosen frequency applies from the next boot."));
  const bar=el("div","row");
  bar.appendChild(btn("Save","Save the temperature and fan settings",async()=>{
    const a=+on.value,b=+off.value,m=+mx.value;
    if(!(a>=40&&a<=80&&b>=30&&b<=a-2)){alert("Turn-on from 40 to 80 degrees; turn-off from 30 and at least 2 degrees below the turn-on");return}
    if(!(m>=35&&m<=100)){alert("Maximum speed from 35 to 100 percent");return}
    const r=await g({op:"thset",fc:fc.checked?1:0,on:a,off:b,mx:m,bo:bo.checked?1:0});
    if(r&&r.ok){THC.fanctl=fc.checked?1:0;THC.on=a;THC.off=b;THC.mx=m;THC.boost=bo.checked?1:0;alert("Saved.")}else alert((r&&r.err)||"error");
  }));
  c.appendChild(bar);
  return c;
}
/* ---- concert mode: reboot without network to play live ----*/
let ccEl=null,ccStat=null;
const ccText=()=>THC?"Audio dropouts recorded on the board (DMA alarms): "+(THC.da||0):"";
function concertCard(){
  if(!THC)return null;
  if(ccEl){ccStat.textContent=ccText();return ccEl}
  const c=el("div","card th");ccEl=c;
  const h=el("h3","");h.appendChild(el("span","nm","Concert mode"));c.appendChild(h);
  ccStat=el("div","info","");ccStat.textContent=ccText();c.appendChild(ccStat);
  c.appendChild(el("div","info","Reboots the board WITHOUT network: no Wi-Fi and no page. The audio dropouts are linked to the network traffic, so it is the safest condition for playing live. The preset in use, preset changes and MIDI work as always. Without the page you also enter and leave it with the Pisound button: hold it for 3 seconds (the board reboots by itself, into concert mode or into normal mode). To get back to the page you can also power the Pi off and on. The pedalboard in use is saved before the reboot."));
  const bar=el("div","row");
  bar.appendChild(btn("Reboot into concert mode","Reboot the board without network",async()=>{
    if(!confirm("I am rebooting the board WITHOUT network: the page will no longer be reachable until the next power-off and power-on. Continue?"))return;
    const r=await g({op:"concert"});
    if(!(r&&r.ok))alert((r&&r.err)||"error");
  }));
  c.appendChild(bar);
  return c;
}
/* ---- DI recorder: raw input (24-bit stereo) kept in the board's memory (up to about 10 minutes), several takes can be added one after the other, downloaded as a WAV file ----*/
let diEl=null,diStat=null,diSecs=null,diApp=null,diTimer=null,diBusy=false;
async function diState(){try{return await (await fetch("/di?_="+Date.now(),{cache:"no-store"})).json()}catch(e){return null}}
const diFmt=s=>{s=Math.round(s);return Math.floor(s/60)+":"+String(s%60).padStart(2,"0")};
async function diRefresh(){
  const s=await diState();if(!s||!diStat)return s;
  const rate=s.rate||48000,sec=n=>diFmt(n/rate),nt=(s.marks||[]).length;
  if(s.state==1)diStat.textContent="Recording take "+nt+": "+sec(s.frames)+" of "+sec(s.target)+" (the recording so far)";
  else if(s.state==2)diStat.textContent="In memory: "+sec(s.frames)+" in "+nt+" take"+(nt==1?"":"s")+", "+sec(Math.max(0,(s.max||0)-s.frames))+" still free";
  else diStat.textContent="No recording in memory";
  if(s.state!=1&&diTimer){clearInterval(diTimer);diTimer=null}
  return s;
}
async function diDownload(){
  if(diBusy)return;diBusy=true;
  try{
    const s=await diState();if(!s||s.state!=2||!s.frames){alert("Nothing to download");return}
    const n=s.frames,parts=[new Uint8Array(44)],CH=32000;
    for(let o=0;o<n;o+=CH){
      const c=Math.min(CH,n-o);
      diStat.textContent="Downloading... "+Math.round(100*o/n)+"%";
      const r=await fetch("/di?o="+o+"&n="+c+"&_="+Date.now(),{cache:"no-store"});
      const b=new Uint8Array(await r.arrayBuffer());
      if(b.length!=c*6)throw new Error("short read");
      parts.push(b);
    }
    const h=new DataView(parts[0].buffer),w=(o,t)=>{for(let i=0;i<t.length;i++)h.setUint8(o+i,t.charCodeAt(i))};
    const dl=n*6;w(0,"RIFF");h.setUint32(4,36+dl,true);w(8,"WAVE");w(12,"fmt ");h.setUint32(16,16,true);h.setUint16(20,1,true);h.setUint16(22,2,true);
    h.setUint32(24,s.rate,true);h.setUint32(28,s.rate*6,true);h.setUint16(32,6,true);h.setUint16(34,24,true);w(36,"data");h.setUint32(40,dl,true);
    const d=new Date(),p2=x=>String(x).padStart(2,"0"),stamp=d.getFullYear()+p2(d.getMonth()+1)+p2(d.getDate())+"_"+p2(d.getHours())+p2(d.getMinutes())+p2(d.getSeconds());
    const save=(blob,name)=>{const a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download=name;document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove()},2000)};
    save(new Blob(parts,{type:"audio/wav"}),"DI_"+stamp+".wav");
    const mk=s.marks||[];
    if(mk.length>1){/* where each take starts, to cut the long file back into takes*/
      let tx="take,start_seconds,start_frame\n";mk.forEach((m,i)=>{tx+=(i+1)+","+(m/s.rate).toFixed(3)+","+m+"\n"});
      setTimeout(()=>save(new Blob([tx],{type:"text/csv"}),"DI_"+stamp+"_takes.csv"),600);
    }
  }catch(e){alert("Download failed: "+e.message)}
  finally{diBusy=false;diRefresh()}
}
function diCard(){
  if(diEl)return diEl;
  const c=el("div","card th");diEl=c;
  const h=el("h3","");h.appendChild(el("span","nm","DI recorder"));c.appendChild(h);
  diStat=el("div","info","");c.appendChild(diStat);
  c.appendChild(el("div","info","Records the raw guitar input (both channels, 24 bit, before any effect) in the board's memory, up to about 10 minutes, then downloads it as a WAV file. Tick \"Add to the recording\" to keep what is already recorded and add a new take after it: the takes join into one long file, and a small .csv file says where each one starts. Download the file when you have finished playing (the download is web traffic)."));
  const bar=el("div","row");
  diSecs=document.createElement("input");diSecs.type="number";diSecs.min="1";diSecs.max="600";diSecs.value="60";diSecs.style.width="4.5em";
  bar.appendChild(diSecs);bar.appendChild(el("span","info"," seconds (1-600) "));
  const lb=document.createElement("label");lb.style.cssText="display:inline-flex;align-items:center;gap:4px;margin:0 8px";
  diApp=document.createElement("input");diApp.type="checkbox";lb.appendChild(diApp);lb.appendChild(document.createTextNode("Add to the recording"));bar.appendChild(lb);
  bar.appendChild(btn("Start","Start recording the input",async()=>{
    const s=Math.max(1,Math.min(600,parseInt(diSecs.value)||60));
    const r=await g({op:"di",v:1,s:s,a:diApp.checked?1:0});
    if(!(r&&r.ok)){alert((r&&r.err)||"error");return}
    if(!diTimer)diTimer=setInterval(diRefresh,500);
    setTimeout(diRefresh,300);
  }));
  bar.appendChild(btn("Stop","Stop the recording now",async()=>{await g({op:"di",v:2});setTimeout(diRefresh,300)}));
  bar.appendChild(btn("Download WAV","Download the recording as a WAV file",diDownload));
  bar.appendChild(btn("Free memory","Delete the recording from the board's memory",async()=>{if(!confirm("Delete the whole recording from the board's memory?"))return;await g({op:"di",v:3});setTimeout(diRefresh,300)}));
  c.appendChild(bar);
  diRefresh();
  return c;
}/* ---- fallback network: if the home network is not found at power-on, the board creates its own protected Wi-Fi network (WPA2) ----*/
let APC=null,apEl=null;
async function apLoad(){try{APC=await (await fetch("/ap?_="+Date.now(),{cache:"no-store"})).json()}catch(e){APC=null}}
const apOk=(s,a,b)=>/^[A-Za-z0-9_.-]+$/.test(s)&&s.length>=a&&s.length<=b;
function apCard(){
  if(!APC)return null;
  if(apEl&&apEl.dataset.act==String(APC.active))return apEl;
  const c=el("div","card ap");apEl=c;c.dataset.act=String(APC.active);
  const h=el("h3","");h.appendChild(el("span","nm","Fallback network (access point)"));c.appendChild(h);
  if(APC.active)c.appendChild(el("div","info bad","You are connected to the board's fallback network (address 192.168.4.1): the home network was not there at power-on."));
  c.appendChild(el("div","info","If at power-on the home network is not found within "+APC.wait+" s, the board reboots and creates this protected Wi-Fi network. Connect with your phone or PC (address 192.168.4.x assigned by it) and open http://192.168.4.1 or http://namberry5.local . At the next reboot it looks for the home network again. Name and password apply from the next boot into the fallback network."));
  const row=(lab,node)=>{const r=el("div","row");r.appendChild(el("label","",lab));r.appendChild(node);c.appendChild(r)};
  const ss=el("input");ss.type="text";ss.maxLength=32;ss.value=APC.ssid;ss.id="apSsid";
  const pp=el("input");pp.type="text";pp.maxLength=63;pp.value=APC.pass;pp.id="apPass";
  const ch=el("select");for(let i=1;i<=13;i++){const o=el("option","",String(i));o.value=String(i);ch.appendChild(o)}ch.value=String(APC.ch);
  row("Network name",ss);row("Password",pp);row("Channel",ch);
  c.appendChild(el("div","info","Name and password: 1 to 32 and 8 to 63 characters, only letters, digits, _ - . (no spaces)."));
  const bar=el("div","row");
  bar.appendChild(btn("Save","Save the name, password and channel of the fallback network",async()=>{
    if(!apOk(ss.value,1,32)||!apOk(pp.value,8,63)){alert("Name: 1-32 characters; password: 8-63 characters; only letters, digits, _ - .");return}
    const r=await g({op:"apset",s:ss.value,p:pp.value,c:+ch.value});
    if(r&&r.ok){APC.ssid=ss.value;APC.pass=pp.value;APC.ch=+ch.value;alert("Saved. It applies from the next boot into the fallback network.")}else alert((r&&r.err)||"error");
  }));
  if(APC.active)bar.appendChild(btn("Back to the home network","Reboots the board: it looks for the home network again",async()=>{if(confirm("Reboot the board? The audio is interrupted for a few seconds."))try{await g({op:"reboot"})}catch(e){}}));
  else bar.appendChild(btn("Try the fallback network now","Reboots the board into the fallback network (to try it)",async()=>{if(confirm("The board now reboots into the fallback network \""+APC.ssid+"\": this page stops responding until you connect to that network. To go back to the home network, just power the board off and on. Continue?"))try{await g({op:"apnow"})}catch(e){}}));
  c.appendChild(bar);
  return c;
}
/* content of the flow window in ADELGrid and ADELMatrix*/
function netView(){
  const w=el("div","pb"),mono=netMono();
  w.appendChild(gridView(mono));
  return w;
}
function build(){
  const root=$("blocks");
  const od=root.querySelector("details.lib"),wasOpen=od&&od.open;root.innerHTML="";bind={};mbind=[];MT.length=0;
  const cfg=location.hash=="#config";
  if(cfg){
    const m=el("div","card");
    m.innerHTML="<h2>Signal on the inputs and outputs</h2><div class=\"info\">Play a note: the bar that moves shows the input the cable is connected to. The inputs and outputs (L and R) are blocks of the grid: they are linked to the blocks by dragging from one port to the other.</div>"+
      '<div class="row"><label>Input L</label><div class="bar"><i id="mL"></i></div><output id="vL"></output></div>'+
      '<div class="row"><label>Input R</label><div class="bar"><i id="mR"></i></div><output id="vR"></output></div>'+
      '<div class="row"><label>Output L</label><div class="bar"><i id="mOL"></i></div><output id="vOL"></output></div>'+
      '<div class="row"><label>Output R</label><div class="bar"><i id="mOR"></i></div><output id="vOR"></output></div>';
    root.appendChild(m);
    return;
  }
  if(typeof sel=="number"&&!locate(sel))sel=null;
  {
    const c=el("div","card");
    const h=el("h2","ph");h.appendChild(presetBar());
    {const ab=btn("","Analyze the preset: links, phase, delays, levels",()=>{if(anOn){anOn=false;build();upd();return}anOn=true;anRelayout().then(()=>{build();upd()})});ab.innerHTML=IC_AN;ab.id="anbtn";ab.classList.add("gb");if(anOn)ab.classList.add("on");h.appendChild(ab)}
    {const mb=btn("","MIDI: channel, fixed CCs, controllers, message monitor",()=>{miOn=!miOn;if(miOn)miStart();else miStop();build();upd()});mb.innerHTML=IC_MIDI;mb.id="midibtn";mb.classList.add("gb");if(miOn)mb.classList.add("on");h.appendChild(mb)}
    {const fb=btn("","Functions: hum killer, limiter, mono check, spectrum, tuner, meters (all off until you turn them on)",()=>{ftOn=!ftOn;if(ftOn)ftStart();else ftStop();build();upd()});fb.innerHTML=IC_FT;fb.id="ftbtn";fb.classList.add("gb");if(ftOn)fb.classList.add("on");h.appendChild(fb)}
    {const lb=btn("","Levels: shows or hides the signal indicators on every block (the input and output ones always stay)",()=>{bmOn=!bmOn;lsSet("bm",bmOn?"1":"0");bmApply();build();upd()});lb.innerHTML=IC_LV;lb.id="lvbtn";lb.classList.add("gb");if(bmOn)lb.classList.add("on");h.appendChild(lb)}
    c.appendChild(h);
    c.appendChild(netView());root.appendChild(c);
    const inf2=typeof sel=="number"?locate(sel):null;if(inf2)root.appendChild(inf2.io?ioCard(sel):editor(inf2));
    if(anOn)root.appendChild(anPanel());
    if(miOn)root.appendChild(miPanel());
    if(ftOn){root.appendChild(ftPanel());const mcc=mcCard();if(mcc)root.appendChild(mcc);const tc=thCard();if(tc)root.appendChild(tc);const cc=concertCard();if(cc)root.appendChild(cc);root.appendChild(diCard());const ac=apCard();if(ac)root.appendChild(ac)}
  }
  const Lb=el("details","card lib");Lb.appendChild(el("summary","","Model library ("+S.lib.length+")"));Lb.appendChild(libTools());
  S.lib.forEach(f=>{
    const r=el("div","it");const li=(S.lib||[]).indexOf(f),inf=(S.libinf||[])[li]||"";r.appendChild(el("span","",(S.fav.includes(f)?"★ ":"")+esc(f)+(inf?" <small style=\"color:var(--mut)\">"+esc(inf)+"</small>":"")));
    const x=el("button","","Delete");
    x.onclick=async()=>{if(!confirm("Delete "+f+" from the board? This cannot be undone."))return;warn(await api("/delete",{s:f}))};
    r.appendChild(x);Lb.appendChild(r);
  });
  if(wasOpen)Lb.open=true;
  root.appendChild(Lb);
}
{const _b=build;build=function(){_b.apply(this,arguments);evenRows();drawLines();drawNet();renderPop()}}
window.addEventListener("resize",()=>{if(typeof S!=="undefined"&&S&&S.board&&S.board.net)drawNet()});
function upd(){
  const cf=location.hash=="#config";
  if(!cf&&S.board.net)S.board.net.cells.forEach(c=>{const n=c.b[0];(bind[n.id]||[]).forEach(f=>f(n))});
  mbind.forEach(f=>f());
  const mp=(id,v)=>{const e=$("m"+id);if(!e)return;const d=v>0?20*Math.log10(v/1e6):-120;e.style.width=Math.max(0,Math.min(100,(d+80)/80*100))+"%";e.style.background=d>-3?"var(--bad)":d>-12?"var(--warn)":"var(--ok)";$("v"+id).textContent=d>-100?d.toFixed(0)+" dB":"-"};
  mp("L",S.peakL);mp("R",S.peakR);mp("OL",S.outL);mp("OR",S.outR);
  {const b=$("t_cfg");b.className="cfgb"+(cf?" on":"");b.href=cf?"#":"#config";b.textContent=cf?"← Back":"Configuration"}
  const c=S.cpu,lat=(2*S.frames+44*S.rate/48000)/S.rate*1000;
  const gnm=(()=>{const b=(S.blocks||[]).find(x=>x.id=="guitar");if(!b)return "";const sl=b.params.find(p=>p.k=="sel"),i=sl?Math.round(sl.v):1,n=b.params.find(p=>p.k=="n"+i);return " · guitar: "+((n&&n.s)?n.s:"Guitar "+i)})();
  {const fv=$("fwver");if(fv&&S.ver)fv.textContent=S.ver}
  $("sub").textContent=S.ip+gnm+" · on for "+Math.floor(S.up/60)+" min · CPU "+c.avg.toFixed(1)+"% (peak "+c.max.toFixed(1)+"%) · latency "+lat.toFixed(2)+" ms · "+(S.rate/1000)+" kHz, block "+S.frames+" frame · NAM "+S.board.nam+"/"+S.board.max+(S.safe?" · SAFE MODE":"");
}
let ovLast=0,ovAt=0,LIBC=null,LIBV=-1,BPC=null,BPV=-1;
/* saved block settings are in /bpl: they are re-read only when "bv" of /status changes (a status without "bv" keeps an empty list)*/
async function bpSync(bv){
  if(bv===undefined){if(!BPC)BPC={bp:[]};return}
  if(BPC&&bv===BPV)return;
  try{const j=await (await fetch("/bpl?_="+Date.now(),{cache:"no-store"})).json();BPC=j;BPV=j.bv}catch(e){if(!BPC)BPC={bp:[]}}
}
/* models, IRs and information are in /lib (about 10 KB): they are re-read only when "lv" of /status changes*/
async function libSync(lv){
  if(LIBC&&lv===LIBV)return;
  const j=await (await fetch("/lib?_="+Date.now(),{cache:"no-store"})).json();
  LIBC=j;LIBV=j.lv;
}
/* re-reads the state from the board once, without restarting the page's update cycle (needed by the automatic branch alignment)*/
async function stNow(){
  const r=await fetch("/status?_="+Date.now(),{cache:"no-store"});S=await r.json();
  await libSync(S.lv);S.lib=LIBC.lib.slice();S.ir=LIBC.ir.slice();S.libinf=LIBC.libinf.slice();await bpSync(S.bv);S.bp=BPC.bp.slice();
}
async function poll(){
  let ok=true;
  if(document.hidden||idleNow()){
    const d=$("dot");if(d)d.className="dot0";
    const s=$("sub");if(s&&!s.dataset.idle&&!document.hidden){s.dataset.idle="1";s.textContent+=" · updating paused: touch the page to resume"}
    setTimeout(poll,1000);return}
  try{
    const r=await fetch("/status?_="+Date.now(),{cache:"no-store"});S=await r.json();
    await libSync(S.lv);S.lib=LIBC.lib.slice();S.ir=LIBC.ir.slice();S.libinf=LIBC.libinf.slice();await bpSync(S.bv);S.bp=BPC.bp.slice();
    const s=location.hash+"#"+(S.board.mode||0)+"#"+S.board.s+(S.board.net?JSON.stringify(S.board.net.links)+JSON.stringify(S.board.net.io):"")+"#"+S.lib.join(",")+"#"+S.fav.join(",")+"#"+(S.bp||[]).map(b=>b.n+b.d).join(";")+"#"+(S.pre||[]).join(",")+(S.prm||"")+"#"+(S.pc||"")+(S.pd?"*":"");
    {const pcNow=S.pc||"";if(anPcSeen!==null&&anPcSeen!==pcNow)anOn=false;anPcSeen=pcNow}
    if(s!==sig&&drag===null){sig=s;build()}
    upd();lastOk=Date.now();drawLines();
    {const tb=$("tlb");if(S.tl){if(!tb){const x=el("div","tlbar","Acceptance test in progress: changes are blocked until the end of the test");x.id="tlb";document.body.prepend(x)}}else if(tb)tb.remove()}
    {if(S.ov>ovLast)ovAt=Date.now();ovLast=S.ov||0;const ob=$("ovb"),on=Date.now()-ovAt<9000&&ovAt>0;
     if(on&&!ob){const x=el("div","tlbar bad","OVERLOAD: the blocks exceed the processor's capacity and the audio is excluded at times to keep the board alive. Delete or disconnect some blocks (a NAM weighs about 21%, a reverb up to 9%).");x.id="ovb";document.body.prepend(x)}else if(!on&&ob)ob.remove()}
  }catch(e){ok=false}
  $("dot").className="dot0"+(Date.now()-lastOk<3000?" on":"");
  setTimeout(poll,ok?2500:2000);
}
window.onhashchange=()=>{if(S){sig=location.hash+"#";build();upd()}};
bmApply();
(async()=>{try{const r=await fetch("/fx");const j=await r.json();if(Array.isArray(j))FXD=j}catch(e){}t3kCallback();poll();mtick()})();
</script><div id="dm"></div></body></html>)HTML";
