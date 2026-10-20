#pragma once
#include <pgmspace.h>

// Painel web: 4 abas (Relógio, Foco, Fotos, Ajustes) com galeria de mostradores.
// As prévias dos mostradores vêm do próprio relógio (/face?t=N, ver faces.h).
// Tudo salva sozinho ao mudar (POST /api/settings com só o campo alterado).
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0e0e10"><title>SmallTV</title>
<style>
:root{--bg:#0e0e10;--card:#18181b;--card2:#202024;--line:#2a2a2f;--fg:#f4f4f5;--mut:#8b8b93;--work:#ff5a47;--short:#3ecf7a;--long:#4f8dff;--ac:var(--work)}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:480px;margin:auto;padding:4px 16px calc(96px + env(safe-area-inset-bottom))}
header{position:sticky;top:0;z-index:5;display:flex;justify-content:space-between;align-items:center;max-width:480px;margin:auto;padding:14px 16px;background:linear-gradient(var(--bg) 75%,transparent)}
header b{font-size:18px}header span{color:var(--mut);font-variant-numeric:tabular-nums}
#net{display:inline-block;width:8px;height:8px;border-radius:50%;background:#555;margin-right:8px;vertical-align:middle}
#net.on{background:var(--short)}#net.off{background:var(--work)}
h2{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:1.2px;margin:22px 4px 10px;font-weight:600}
.card{background:var(--card);border:1px solid var(--line);border-radius:20px;padding:18px;margin-bottom:12px}
.tab{display:none}.tab.on{display:block;animation:fade .2s}@keyframes fade{from{opacity:0;transform:translateY(4px)}}
button{font:inherit;border:0;cursor:pointer;color:var(--fg);background:none}
button:active{transform:scale(.97)}
.btn{width:100%;padding:13px;border-radius:12px;background:var(--card2);border:1px solid var(--line)}.btn.p{background:var(--ac);border-color:transparent;color:#fff;font-weight:600}
.row{display:flex;gap:8px}.row>*{flex:1}
small{color:var(--mut)}
/* galeria */
.hero{display:flex;gap:16px;align-items:center}
.scr{position:relative;border-radius:16px;overflow:hidden;background:#000;border:6px solid #2b2b30;box-shadow:0 10px 30px -12px #000}
.scr img{display:block;width:100%;image-rendering:pixelated}
.hero .scr{width:132px;flex:none}.hero b{font-size:20px;display:block}.hero p{margin:4px 0 0;color:var(--mut)}
.faces{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}
.face{text-align:left;padding:0}.face .scr{border-width:5px}.face span{display:block;padding:8px 4px 0;font-weight:600}
.face.cur .scr{border-color:var(--ac)}.face.cur span::after{content:' · em uso';color:var(--ac);font-weight:400;font-size:13px}
.noimg{aspect-ratio:1;display:flex;align-items:center;justify-content:center;color:var(--mut);font-size:13px}
#pvbar{display:none;margin-bottom:12px;padding:10px 14px;border-radius:12px;background:#1d2a44;color:#bcd3ff}
/* formulário */
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:11px 0;border-top:1px solid var(--line)}
.card label:first-of-type{border-top:0}
input,select{background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:9px;font:inherit;width:96px}
select,input[type=text],input[type=search],input[type=password]{width:180px}input[type=range]{width:160px;padding:0;accent-color:var(--ac)}
input[type=checkbox]{width:44px;height:26px;appearance:none;-webkit-appearance:none;border-radius:13px;background:var(--line);position:relative;padding:0;border:0;transition:.2s;flex:none}
input[type=checkbox]::after{content:'';position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:.2s}
input[type=checkbox]:checked{background:var(--short)}input[type=checkbox]:checked::after{left:21px}
input[type=color]{width:52px;height:36px;padding:2px}
/* Pomodoro */
.tabs{display:flex;background:var(--bg);border-radius:12px;padding:4px;gap:4px}
.tabs button{flex:1;padding:9px 4px;border-radius:9px;color:var(--mut);font-size:14px}.tabs button.sel{background:var(--card2);color:var(--fg)}
.ring{position:relative;width:min(260px,72vw);aspect-ratio:1;margin:20px auto 8px}
.ring svg{width:100%;height:100%;transform:rotate(-90deg)}.ring circle{fill:none;stroke-width:10}
#trk{stroke:var(--line)}#arc{stroke:var(--ac);stroke-linecap:round;transition:stroke-dashoffset .3s linear,stroke .3s}
.mid{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center}
#phase{font-size:13px;font-weight:700;letter-spacing:3px;color:var(--ac)}
#time{font-size:clamp(50px,16vw,64px);font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:-1px}
#dots{display:flex;gap:7px;margin-top:4px}#dots i{width:9px;height:9px;border-radius:50%;border:2px solid var(--mut)}#dots i.f{background:var(--work);border-color:var(--work)}
.ctl{display:flex;justify-content:center;align-items:center;gap:22px;margin-top:6px}
.ic{width:52px;height:52px;border-radius:50%;background:var(--card2);font-size:20px}
#go{width:76px;height:76px;border-radius:50%;background:var(--ac);font-size:28px;color:#fff;box-shadow:0 6px 24px -6px var(--ac)}
.stats{display:flex;justify-content:space-around;text-align:center}.stats b{display:block;font-size:26px}.stats span{color:var(--mut);font-size:13px}
#wx{display:flex;align-items:center;gap:14px;padding-bottom:8px}#wx b{font-size:34px}#wx div{color:var(--mut)}
#res button{display:block;width:100%;text-align:left;padding:10px;border-radius:10px;background:var(--bg);margin-top:6px}
/* fotos */
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:12px}
.grid div{position:relative;aspect-ratio:1;border-radius:12px;overflow:hidden;background:var(--bg);cursor:pointer}
.grid img{width:100%;height:100%;object-fit:cover}.grid div.on{outline:3px solid var(--ac)}
.grid span{position:absolute;bottom:4px;right:4px;background:#000a;font-size:10px;padding:1px 5px;border-radius:6px}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;background:var(--ac)}
/* barra de abas */
nav{position:fixed;left:0;right:0;bottom:0;z-index:6;background:#121215ee;backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);border-top:1px solid var(--line);padding-bottom:env(safe-area-inset-bottom)}
nav div{display:flex;max-width:480px;margin:auto}
nav button{flex:1;padding:10px 0 12px;color:var(--mut);font-size:12px;display:flex;flex-direction:column;align-items:center;gap:3px}
nav button.on{color:var(--fg)}nav svg{width:24px;height:24px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
nav button.on svg{stroke:var(--ac)}
/* folha de detalhes */
#sheet{position:fixed;inset:0;z-index:8;background:#000a;display:none;align-items:flex-end;justify-content:center}
#sheet.v{display:flex}#sheet>div{width:100%;max-width:480px;background:var(--card);border-radius:22px 22px 0 0;padding:18px 18px calc(18px + env(safe-area-inset-bottom));animation:up .2s}
@keyframes up{from{transform:translateY(40px);opacity:.5}}
#sheet .scr{width:min(240px,64vw);margin:4px auto 14px}#sheet h3{margin:0;text-align:center;font-size:20px}#sheet p{text-align:center;color:var(--mut);margin:6px 0 16px}
#toast{position:fixed;left:50%;bottom:calc(84px + env(safe-area-inset-bottom));transform:translateX(-50%);background:var(--card2);border:1px solid var(--line);padding:10px 16px;border-radius:12px;opacity:0;transition:opacity .3s;pointer-events:none;z-index:9;white-space:nowrap}
#toast.v{opacity:1}
</style></head><body>
<header><b>SmallTV</b><span><i id="net"></i><span id="clock">--:--</span></span></header>
<main>

<!-- ============ RELÓGIO ============ -->
<section class="tab" id="t0">
 <div id="pvbar"></div>
 <div class="card hero"><div class="scr"><img id="curimg" alt=""></div><div><small>Mostrador atual</small><b id="curname">—</b><p id="curdesc"></p></div></div>
 <h2>Mostradores</h2>
 <div class="faces" id="faces"></div>
 <h2>Aparência</h2>
 <div class="card">
  <label>Cor dos números<input data-k="color" type="color"></label>
  <label>Formato 12 h<input data-k="h12" type="checkbox"></label>
  <label>O que mostrar<select data-k="mode"><option value="0">Só o relógio</option><option value="1">Álbum de fotos</option><option value="2">Alternar relógio e álbum</option></select></label>
  <label>Segundos por item<input data-k="delay" type="number" min="3" max="255"></label>
 </div>
</section>

<!-- ============ FOCO ============ -->
<section class="tab" id="t1">
 <div class="card">
  <div class="tabs"><button data-p="work" onclick="cmd('focus')">Foco</button><button data-p="short" onclick="cmd('short')">Pausa</button><button data-p="long" onclick="cmd('long')">Pausa longa</button></div>
  <div class="ring"><svg viewBox="0 0 120 120"><circle id="trk" cx="60" cy="60" r="54"/><circle id="arc" cx="60" cy="60" r="54" stroke-dasharray="339.3" stroke-dashoffset="339.3"/></svg>
   <div class="mid"><div id="phase">PRONTO</div><div id="time">--:--</div><div id="dots"></div></div></div>
  <div class="ctl"><button class="ic" title="Zerar" onclick="cmd('reset')">&#8634;</button><button id="go" title="Iniciar/pausar (espaço)" onclick="cmd('toggle')">&#9654;</button><button class="ic" title="Pular fase" onclick="cmd('skip')">&#9197;</button></div>
 </div>
 <div class="card"><label>O que vou fazer no foco<select data-k="act"><option value="0">&#128214; Ler</option><option value="1">&#128211; Estudar</option><option value="2">&#128187; Programar</option><option value="3">&#128203; Tarefas</option><option value="4">&#127912; Desenhar</option></select></label></div>
 <div class="card stats"><div><b id="today">0</b><span>focos hoje</span></div><div><b id="goal">8</b><span>meta</span></div><div><b id="mins">0</b><span>min focados</span></div></div>
 <h2>Tempos</h2>
 <div class="card">
  <label>Foco (min)<input data-k="work" type="number" min="1" max="99"></label>
  <label>Pausa curta (min)<input data-k="short" type="number" min="1" max="99"></label>
  <label>Pausa longa (min)<input data-k="long" type="number" min="1" max="99"></label>
  <label>Pausa longa a cada<input data-k="every" type="number" min="1" max="8"></label>
  <label>Meta diária de focos<input data-k="goal" type="number" min="1" max="30"></label>
  <label>Iniciar a próxima fase sozinho<input data-k="auto" type="checkbox"></label>
 </div>
</section>

<!-- ============ FOTOS ============ -->
<section class="tab" id="t2">
 <div class="card">
  <small id="fsinfo"></small><div class="bar"><i id="fsbar" style="width:0"></i></div>
  <div class="grid" id="grid"></div>
  <input id="up" type="file" accept="image/*" multiple hidden>
  <p><button class="btn p" onclick="$('up').click()">Enviar fotos ou GIFs</button></p>
  <small>Fotos são recortadas para 240×240 no seu navegador antes de enviar. GIFs vão como estão (ideal 240×240). Para exibir, escolha "Álbum" ou "Alternar" em Relógio → Aparência.</small>
 </div>
</section>

<!-- ============ AJUSTES ============ -->
<section class="tab" id="t3">
 <h2>Clima</h2>
 <div class="card">
  <div id="wx" hidden><b id="wt"></b><div><div id="wd"></div><small id="wm"></small></div></div>
  <label>Mostrar clima<input data-k="won" type="checkbox"></label>
  <label>Cidade<b id="city">—</b></label>
  <label>Buscar cidade<input id="q" type="search" placeholder="ex.: Campinas"></label>
  <div id="res"></div>
 </div>
 <h2>Brilho</h2>
 <div class="card">
  <label>Brilho<input data-k="bri" type="range" min="5" max="100"></label>
  <label>Brilho noturno<input data-k="non" type="checkbox"></label>
  <label>Das (hora)<input data-k="nstart" type="number" min="0" max="23"></label>
  <label>Até (hora)<input data-k="nend" type="number" min="0" max="23"></label>
  <label>Brilho à noite<input data-k="nbri" type="range" min="1" max="100"></label>
 </div>
 <h2>Tela do aparelho</h2>
 <div class="card">
  <label>Ordem de cor<select data-k="bgr"><option value="0">RGB (padrão)</option><option value="1">BGR</option></select></label>
  <label>Inverter cores<input data-k="inv" type="checkbox"></label>
  <label>Girar 180°<input data-k="flip" type="checkbox"></label>
  <p><button class="btn" onclick="cmd('colortest');toast('Teste de cores por 10 s no relógio')">Testar cores no relógio</button></p>
  <small>No teste, a 1ª barra deve ser vermelha e o fundo preto. Vermelho e azul trocados: mude a ordem de cor. Fundo branco ou cores negativas: alterne "Inverter cores".</small>
 </div>
 <h2>Data e hora</h2>
 <div class="card">
  <label>Fuso (POSIX TZ)<input data-k="tz" type="text" spellcheck="false"></label>
  <small>Brasília: &lt;-03&gt;3 · Manaus: &lt;-04&gt;4 · Lisboa: WET0WEST,M3.5.0/1,M10.5.0</small>
 </div>
 <h2>Sistema</h2>
 <div class="card">
  <form id="op">
   <label id="oldl">Senha atual<input name="old" type="password" autocomplete="current-password"></label>
   <label>Senha do /update<input name="new" type="password" autocomplete="new-password" placeholder="vazio = sem senha"></label>
   <p><button class="btn" type="submit">Salvar senha</button></p>
  </form>
  <div class="row"><button class="btn" onclick="location='/update'">Atualizar firmware</button>
  <button class="btn" onclick="if(confirm('Apagar Wi-Fi salvo e reiniciar?'))post('/api/wifireset').then(()=>toast('Reiniciando...'))">Resetar Wi-Fi</button></div>
  <p><button class="btn" id="fmt" hidden onclick="if(confirm('Formatar a memória? Todas as fotos e GIFs serão apagados.'))post('/api/format').then(r=>r.text()).then(t=>{toast(t);files()})">Formatar memória de arquivos</button></p>
  <small>Usuário do /update: admin. Para voltar ao firmware original, envie o .bin da GeekMagic em "Atualizar firmware". Versão <span id="ver"></span> · RAM livre <span id="heap"></span></small>
 </div>
</section>
</main>

<nav><div>
 <button data-t="0" onclick="tab(0)"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>Relógio</button>
 <button data-t="1" onclick="tab(1)"><svg viewBox="0 0 24 24"><circle cx="12" cy="14" r="7"/><path d="M12 7c0-2 1-3 3-4M9 6l3 1 3-1"/></svg>Foco</button>
 <button data-t="2" onclick="tab(2)"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="16" rx="3"/><path d="M3 16l5-5 5 5 3-3 5 5"/><circle cx="16" cy="9" r="1.5"/></svg>Fotos</button>
 <button data-t="3" onclick="tab(3)"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1"/></svg>Ajustes</button>
</div></nav>

<div id="sheet" onclick="if(event.target==this)closeSheet()"><div id="sheetc"></div></div>
<div id="toast"></div>

<script>
const $=id=>document.getElementById(id),C=339.3;
const LBL={work:'FOCO',short:'PAUSA',long:'PAUSA LONGA',idle:'PRONTO'};
const FACES=[['Matriz','Pixel art com cartões de clima e focos do dia'],['Tomate','Cena em pixel art que muda com a hora e o clima'],['Analógico','Ponteiros bastão e marcas grossas'],['Clima','Temperatura grande e mini cartões'],['Neon','Tubos rosa e ciano com brilho'],['Flip','Relógio de cartões, como os antigos'],['LCD retrô','Relógio digital dos anos 80 (fonte DSEG7)'],['Palavras','A hora por extenso, em português']];
let st=null,at=0,filled=false,lastPhase=null,actx=null,curTab=0;
const post=(u,b)=>fetch(u,{method:'POST',body:b});
const kb=n=>n>1048576?(n/1048576).toFixed(1)+' MB':Math.round(n/1024)+' KB';
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('v');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('v'),2200)}
function tab(i){curTab=i;document.querySelectorAll('.tab').forEach((e,k)=>e.classList.toggle('on',k==i));document.querySelectorAll('nav button').forEach(b=>b.classList.toggle('on',b.dataset.t==i));
 try{localStorage.tab=i}catch(e){}if(i==2)files();window.scrollTo(0,0)}
function img(t){const i=new Image();i.alt=FACES[t][0];i.src='/face?t='+t;i.onerror=()=>{const d=document.createElement('div');d.className='noimg';d.textContent=FACES[t][0];i.replaceWith(d)};return i}
// ---------- mostradores ----------
function buildFaces(){const g=$('faces');FACES.forEach((f,t)=>{const b=document.createElement('button');b.className='face';b.dataset.t=t;
 const s=document.createElement('div');s.className='scr';s.appendChild(img(t));b.appendChild(s);const n=document.createElement('span');n.textContent=f[0];b.appendChild(n);b.onclick=()=>faceSheet(t);g.appendChild(b)})}
function faceSheet(t){const c=$('sheetc');c.innerHTML='';const s=document.createElement('div');s.className='scr';s.appendChild(img(t));c.appendChild(s);
 const h=document.createElement('h3');h.textContent=FACES[t][0];const p=document.createElement('p');p.textContent=FACES[t][1];c.append(h,p);
 if(st&&st.phase!='idle'){const n=document.createElement('p');n.textContent='Durante o Pomodoro o relógio mostra o timer; a prévia aparece quando ele parar.';c.appendChild(n)}
 const r=document.createElement('div');r.className='row';const a=document.createElement('button');a.className='btn';a.textContent='Ver no relógio (15 s)';
 a.onclick=()=>{post('/api/cmd',new URLSearchParams({c:'preview',t})).then(r=>r.json()).then(load);toast('Prévia no relógio por 15 s')};
 const u=document.createElement('button');u.className='btn p';u.textContent=st&&st.cfg.theme==t?'Em uso':'Usar este';
 u.onclick=()=>save({theme:t}).then(()=>{closeSheet();toast(FACES[t][0]+' no relógio')});r.append(a,u);c.appendChild(r);$('sheet').classList.add('v')}
function closeSheet(){$('sheet').classList.remove('v')}
// ---------- configurações (salva sozinho) ----------
function save(o){return post('/api/settings',new URLSearchParams(o)).then(r=>r.json()).then(s=>{filled=false;load(s);return s})}
document.querySelectorAll('[data-k]').forEach(e=>{e.onchange=()=>{const v=e.type=='checkbox'?(e.checked?1:0):e.value;save({[e.dataset.k]:v}).then(()=>toast('Salvo'))}});
// ---------- estado ----------
function beep(){try{actx=actx||new AudioContext();[0,.25,.5].forEach(d=>{const o=actx.createOscillator(),g=actx.createGain();
 o.frequency.value=880;g.gain.setValueAtTime(.2,actx.currentTime+d);g.gain.exponentialRampToValueAtTime(.001,actx.currentTime+d+.2);
 o.connect(g).connect(actx.destination);o.start(actx.currentTime+d);o.stop(actx.currentTime+d+.2)})}catch(e){}}
function cmd(c){try{actx=actx||new AudioContext()}catch(e){}post('/api/cmd',new URLSearchParams({c})).then(r=>r.json()).then(load)}
function load(s){if(lastPhase&&s.phase!=lastPhase&&s.phase!='idle'&&lastPhase!='idle')beep();lastPhase=s.phase;st=s;at=Date.now();$('net').className='on';
 document.documentElement.style.setProperty('--ac','var(--'+(s.phase=='idle'?'work':s.phase)+')');
 document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('sel',b.dataset.p==s.phase));
 const n=s.cfg.every,d=s.phase=='long'?n:s.cycle%n;$('dots').innerHTML='<i class="f"></i>'.repeat(d)+'<i></i>'.repeat(n-d);
 $('go').innerHTML=s.running?'&#10074;&#10074;':'&#9654;';$('today').textContent=s.today;$('goal').textContent=s.cfg.goal;$('mins').textContent=s.today*s.cfg.work;
 $('clock').textContent=s.clock;$('ver').textContent=s.ver;$('heap').textContent=kb(s.heap)+' (bloco '+kb(s.blk)+')';$('oldl').style.display=s.cfg.lock?'':'none';
 // mostrador atual
 const t=s.cfg.theme;if($('curimg').dataset.t!=t){$('curimg').dataset.t=t;$('curimg').src='/face?t='+t;$('curname').textContent=FACES[t][0];$('curdesc').textContent=FACES[t][1]}
 document.querySelectorAll('.face').forEach(b=>b.classList.toggle('cur',b.dataset.t==t));
 $('pvbar').style.display=s.pv?'block':'none';if(s.pv)$('pvbar').textContent='Prévia no relógio por mais '+s.pv+' s · depois ele volta para '+FACES[t][0];
 // clima
 $('city').textContent=s.cfg.city||'—';$('wx').hidden=!(s.cfg.won&&s.wx.ok);
 if(s.wx.ok){$('wt').textContent=Math.round(s.wx.t)+'°';$('wd').textContent=(s.cfg.city?s.cfg.city+' · ':'')+s.wx.desc;$('wm').textContent='Mín '+Math.round(s.wx.min)+'° · Máx '+Math.round(s.wx.max)+'° · Umidade '+s.wx.h+'%'}
 // fotos
 $('fsinfo').textContent=s.fs.ok?kb(s.fs.used)+' usados de '+kb(s.fs.total):'Memória de arquivos indisponível';
 $('fsbar').style.width=(s.fs.total?s.fs.used/s.fs.total*100:0)+'%';$('fmt').hidden=!!s.fs.ok;
 document.querySelectorAll('#grid div').forEach(e=>e.classList.toggle('on',e.dataset.n==s.showing));
 if(!filled){filled=true;document.querySelectorAll('[data-k]').forEach(e=>{const v=s.cfg[e.dataset.k];if(v===undefined)return;if(e.type=='checkbox')e.checked=!!v;else e.value=v})}
 draw()}
function draw(){if(!st)return;const s=st,idle=s.phase=='idle';
 const rem=idle?s.cfg.work*60000:Math.max(0,s.remaining-(s.running?Date.now()-at:0)),sec=Math.ceil(rem/1000);
 const t=String(Math.floor(sec/60)).padStart(2,'0')+':'+String(sec%60).padStart(2,'0');
 $('time').textContent=t;$('phase').textContent=LBL[s.phase]+(!idle&&!s.running?' · PAUSADO':'');
 $('arc').style.strokeDashoffset=idle||!s.total?C:C*rem/s.total;document.title=idle?'SmallTV':t+' · '+LBL[s.phase]}
setInterval(draw,250);
// ---------- cidade (busca direto do navegador; o relógio guarda só lat/lon) ----------
let qt;$('q').oninput=()=>{clearTimeout(qt);const q=$('q').value.trim();if(q.length<2){$('res').innerHTML='';return}
 qt=setTimeout(()=>fetch('https://geocoding-api.open-meteo.com/v1/search?count=5&language=pt&name='+encodeURIComponent(q)).then(r=>r.json()).then(j=>{
  const r=$('res');r.innerHTML='';(j.results||[]).forEach(p=>{const b=document.createElement('button');b.type='button';
   b.textContent=[p.name,p.admin1,p.country].filter(Boolean).join(', ');
   b.onclick=()=>{const name=p.name.normalize('NFD').replace(/[^\x20-\x7e]/g,'').replace(/["\\]/g,'').slice(0,31);
    save({lat:p.latitude,lon:p.longitude,city:name,won:1}).then(()=>toast('Cidade: '+name));r.innerHTML='';$('q').value=''};r.appendChild(b)});
  if(!r.children.length)r.textContent='Nenhuma cidade encontrada'}).catch(()=>toast('Sem internet no navegador')),400)};
// ---------- fotos ----------
function files(){fetch('/api/files').then(r=>r.json()).then(j=>{const g=$('grid');g.innerHTML='';
 j.files.forEach(x=>{const d=document.createElement('div');d.dataset.n=x.n;d.innerHTML='<img loading="lazy">'+(x.n.toLowerCase().endsWith('.gif')?'<span>GIF</span>':'');
  d.firstChild.src='/fs?f='+encodeURIComponent(x.n);d.onclick=()=>photoSheet(x);g.appendChild(d)});
 if(!j.files.length)g.innerHTML='<small style="grid-column:1/-1">Nenhuma foto ainda.</small>'})}
function photoSheet(x){const c=$('sheetc');c.innerHTML='';const s=document.createElement('div');s.className='scr';const i=new Image();i.src='/fs?f='+encodeURIComponent(x.n);s.appendChild(i);
 const h=document.createElement('p');h.textContent=x.n.split('/').pop()+' · '+kb(x.s);const r=document.createElement('div');r.className='row';
 const a=document.createElement('button');a.className='btn p';a.textContent='Mostrar agora';a.onclick=()=>post('/api/show',new URLSearchParams({f:x.n})).then(r=>r.text()).then(t=>{toast(t=='ok'?'Mostrando no relógio':t);closeSheet()});
 const d=document.createElement('button');d.className='btn';d.textContent='Apagar';d.onclick=()=>{if(confirm('Apagar '+x.n.split('/').pop()+'?'))post('/api/delete',new URLSearchParams({f:x.n})).then(r=>r.text()).then(t=>{toast(t=='ok'?'Apagado':t);closeSheet();files()})};
 r.append(a,d);c.append(s,h,r);$('sheet').classList.add('v')}
async function prep(file){if(file.type=='image/gif')return{blob:file,name:file.name};
 const im=await createImageBitmap(file),c=document.createElement('canvas');c.width=c.height=240;
 const k=Math.max(240/im.width,240/im.height),w=im.width*k,h=im.height*k;c.getContext('2d').drawImage(im,(240-w)/2,(240-h)/2,w,h);
 const blob=await new Promise(r=>c.toBlob(r,'image/jpeg',.85));return{blob,name:file.name.replace(/\.[^.]+$/,'')+'.jpg'}}
$('up').onchange=async e=>{const fs=[...e.target.files];e.target.value='';let ok=0;
 for(const file of fs){try{toast('Enviando '+file.name+'...');const p=await prep(file),fd=new FormData();fd.append('f',p.blob,p.name);
  const r=await fetch('/api/upload',{method:'POST',body:fd});if(r.ok)ok++;else toast(await r.text())}catch(x){toast('Falha: '+file.name)}}
 if(ok)toast(ok+' arquivo(s) enviado(s)');files()};
$('op').onsubmit=e=>{e.preventDefault();const o=e.target;post('/api/otapass',new URLSearchParams(new FormData(o))).then(r=>r.text()).then(t=>{toast(t);o.reset()})};
document.onkeydown=e=>{if(e.code=='Space'&&curTab==1&&!/INPUT|BUTTON|SELECT/.test(e.target.tagName)){e.preventDefault();cmd('toggle')}};
function poll(){fetch('/api/state').then(r=>r.json()).then(load).catch(()=>$('net').className='off').finally(()=>setTimeout(poll,1000))}
buildFaces();let t0=0;try{t0=+localStorage.tab||0}catch(e){}tab(t0);poll();
</script></body></html>)rawliteral";
