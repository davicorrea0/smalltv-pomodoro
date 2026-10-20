#pragma once
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0e0e10"><title>SmallTV Pomodoro</title>
<style>
:root{--bg:#0e0e10;--card:#18181b;--line:#27272a;--fg:#f4f4f5;--mut:#8b8b93;--work:#ff5a47;--short:#3ecf7a;--long:#4f8dff;--ac:var(--work)}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:440px;margin:auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;color:var(--mut)}
header b{color:var(--fg);font-size:16px}#net{display:inline-block;width:8px;height:8px;border-radius:50%;background:#555;margin-right:6px}
#net.on{background:var(--short)}#net.off{background:var(--work)}
.card{background:var(--card);border:1px solid var(--line);border-radius:20px;padding:20px;margin-bottom:14px}
.tabs{display:flex;background:var(--bg);border-radius:12px;padding:4px;gap:4px}
.tabs button{flex:1;padding:9px 4px;border-radius:9px;background:none;color:var(--mut);font-size:14px}
.tabs button.sel{background:var(--line);color:var(--fg)}
.ring{position:relative;width:min(280px,78vw);aspect-ratio:1;margin:22px auto 10px}
.ring svg{width:100%;height:100%;transform:rotate(-90deg)}
.ring circle{fill:none;stroke-width:10}#trk{stroke:var(--line)}
#arc{stroke:var(--ac);stroke-linecap:round;transition:stroke-dashoffset .3s linear,stroke .3s}
.mid{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center}
#phase{font-size:13px;font-weight:700;letter-spacing:3px;color:var(--ac)}
#time{font-size:clamp(52px,17vw,68px);font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:-1px}
#dots{display:flex;gap:7px;margin-top:4px}#dots i{width:9px;height:9px;border-radius:50%;border:2px solid var(--mut)}
#dots i.f{background:var(--work);border-color:var(--work)}
.ctl{display:flex;justify-content:center;align-items:center;gap:22px;margin-top:8px}
button{font:inherit;border:0;cursor:pointer;color:var(--fg)}
.ic{width:52px;height:52px;border-radius:50%;background:var(--line);font-size:20px}
#go{width:76px;height:76px;border-radius:50%;background:var(--ac);font-size:28px;color:#fff;box-shadow:0 6px 24px -6px var(--ac)}
button:active{transform:scale(.95)}
.stats{display:flex;justify-content:space-around;text-align:center}.stats b{display:block;font-size:24px}.stats span{color:var(--mut);font-size:13px}
details summary{cursor:pointer;font-weight:600;list-style:none;display:flex;justify-content:space-between}
details summary::-webkit-details-marker{display:none}
details summary::after{content:'+';color:var(--mut)}details[open] summary::after{content:'−'}
label{display:flex;justify-content:space-between;align-items:center;margin:12px 0;gap:10px}
input,select{background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:9px;width:84px;font:inherit}
select{width:170px}input[type=text],input[type=password],input[type=search]{width:170px}input[type=range]{width:170px;padding:0;accent-color:var(--ac)}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--ac)}input[type=color]{width:60px;height:38px;padding:2px}
.btn{width:100%;padding:13px;border-radius:12px;background:var(--line);margin-top:6px}.btn.p{background:var(--ac);color:#fff;font-weight:600}
.row{display:flex;gap:8px}.row .btn{flex:1}small{color:var(--mut)}
h3{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:1px;margin:20px 0 4px}
#wx{display:flex;align-items:center;gap:14px}#wx b{font-size:34px}#wx div{color:var(--mut)}
#res button{display:block;width:100%;text-align:left;padding:10px;border-radius:10px;background:var(--bg);margin-top:6px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:12px}
.grid div{position:relative;aspect-ratio:1;border-radius:12px;overflow:hidden;background:var(--bg);cursor:pointer}
.grid img{width:100%;height:100%;object-fit:cover}.grid div.on{outline:3px solid var(--ac)}
.grid span{position:absolute;bottom:4px;right:4px;background:#000a;font-size:10px;padding:1px 5px;border-radius:6px}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;background:var(--ac)}
#menu{position:fixed;inset:0;background:#000b;display:none;align-items:flex-end;justify-content:center;padding:16px}
#menu.v{display:flex}#menu .card{width:100%;max-width:440px}#menu img{width:100%;border-radius:12px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--line);padding:10px 16px;border-radius:12px;opacity:0;transition:opacity .3s;pointer-events:none;z-index:9}
#toast.v{opacity:1}
</style></head><body><main>
<header><span><b>SmallTV</b> Pomodoro</span><span><i id="net"></i><span id="clock">--:--</span></span></header>
<div class="card">
 <div class="tabs"><button data-p="work" onclick="cmd('focus')">Foco</button><button data-p="short" onclick="cmd('short')">Pausa</button><button data-p="long" onclick="cmd('long')">Pausa longa</button></div>
 <div class="ring"><svg viewBox="0 0 120 120"><circle id="trk" cx="60" cy="60" r="54"/><circle id="arc" cx="60" cy="60" r="54" stroke-dasharray="339.3" stroke-dashoffset="339.3"/></svg>
  <div class="mid"><div id="phase">PRONTO</div><div id="time">--:--</div><div id="dots"></div></div></div>
 <div class="ctl"><button class="ic" title="Zerar" onclick="cmd('reset')">&#8634;</button><button id="go" title="Iniciar/pausar (espaço)" onclick="cmd('toggle')">&#9654;</button><button class="ic" title="Pular fase" onclick="cmd('skip')">&#9197;</button></div>
</div>
<div class="card stats"><div><b id="today">0</b><span>focos hoje</span></div><div><b id="mins">0</b><span>min focados</span></div></div>
<div class="card" id="wxc" hidden><div id="wx"><b id="wt"></b><div><div id="wd"></div><small id="wm"></small></div></div></div>

<form id="f">
<div class="card"><details><summary>Tela e temas</summary>
 <label>Tema do relógio<select name="theme"><option value="0">Matriz</option><option value="1">Tomate (pixel art)</option><option value="2">Analógico</option><option value="3">Clima</option><option value="4">Neon</option><option value="5">Flip</option><option value="6">LCD retrô</option><option value="7">Palavras</option></select></label>
 <label>Cor do relógio<input name="color" type="color"></label>
 <label>O que mostrar<select name="mode"><option value="0">Só relógio</option><option value="1">Álbum de fotos</option><option value="2">Alternar relógio e álbum</option></select></label>
 <label>Segundos por item<input name="delay" type="number" min="3" max="255"></label>
 <label>Formato 12h<input name="h12" type="checkbox"></label>
 <label>Brilho<input name="bri" type="range" min="5" max="100"></label>
 <h3>Painel</h3>
 <label>Ordem de cor<select name="bgr"><option value="0">RGB (padrão)</option><option value="1">BGR</option></select></label>
 <label>Inverter cores<input name="inv" type="checkbox"></label>
 <label>Girar 180°<input name="flip" type="checkbox"></label>
 <button class="btn" type="button" onclick="cmd('colortest');toast('Teste de cores por 10 s no relógio')">Testar cores no relógio</button>
 <small>No teste, a 1ª barra deve ser vermelha e o fundo preto. Vermelho e azul trocados: mude a ordem de cor. Fundo branco/cores negativas: alterne "Inverter cores".</small>
 <h3>Brilho noturno</h3>
 <label>Ativar<input name="non" type="checkbox"></label>
 <label>Das (hora)<input name="nstart" type="number" min="0" max="23"></label>
 <label>Até (hora)<input name="nend" type="number" min="0" max="23"></label>
 <label>Brilho à noite<input name="nbri" type="range" min="1" max="100"></label>
 <button class="btn p" type="submit">Salvar</button>
</details></div>
<div class="card"><details><summary>Clima</summary>
 <label>Mostrar clima<input name="won" type="checkbox"></label>
 <label>Cidade atual<b id="city">—</b></label>
 <label>Buscar cidade<input id="q" type="search" placeholder="ex.: Campinas"></label>
 <div id="res"></div>
 <small>Dados de Open-Meteo, sem chave de API. Atualiza a cada 15 min.</small>
 <button class="btn p" type="submit">Salvar</button>
</details></div>
<div class="card"><details><summary>Pomodoro</summary>
 <label>Foco (min)<input name="work" type="number" min="1" max="99"></label>
 <label>Pausa curta (min)<input name="short" type="number" min="1" max="99"></label>
 <label>Pausa longa (min)<input name="long" type="number" min="1" max="99"></label>
 <label>Pausa longa a cada<input name="every" type="number" min="1" max="8"></label>
 <label>Meta diária de focos<input name="goal" type="number" min="1" max="30"></label>
 <label>Iniciar próxima fase sozinho<input name="auto" type="checkbox"></label>
 <label>Fuso (POSIX TZ)<input name="tz" type="text" spellcheck="false"></label>
 <small>Brasília: &lt;-03&gt;3 &middot; Manaus: &lt;-04&gt;4 &middot; Lisboa: WET0WEST,M3.5.0/1,M10.5.0</small>
 <button class="btn p" type="submit">Salvar</button>
</details></div>
</form>

<div class="card"><details id="albd"><summary>Fotos e GIFs</summary>
 <small id="fsinfo"></small><div class="bar"><i id="fsbar" style="width:0"></i></div>
 <div class="grid" id="grid"></div>
 <input id="up" type="file" accept="image/*" multiple hidden>
 <button class="btn p" onclick="$('up').click()">Enviar fotos ou GIFs</button>
 <p><small>Fotos são recortadas para 240×240 no seu navegador antes de enviar. GIFs vão como estão (ideal 240×240).</small></p>
</details></div>

<div class="card"><details><summary>Sistema</summary>
 <form id="op">
 <label id="oldl">Senha atual<input name="old" type="password" autocomplete="current-password"></label>
 <label>Senha do /update<input name="new" type="password" autocomplete="new-password" placeholder="vazio = sem senha"></label>
 <button class="btn" type="submit">Salvar senha</button></form>
 <div class="row" style="margin-top:10px"><button class="btn" onclick="location='/update'">Atualizar firmware</button>
 <button class="btn" onclick="if(confirm('Apagar Wi-Fi salvo e reiniciar?'))post('/api/wifireset').then(()=>toast('Reiniciando...'))">Resetar Wi-Fi</button></div>
 <button class="btn" id="fmt" hidden onclick="if(confirm('Formatar a memória? Todas as fotos e GIFs serão apagados.'))post('/api/format').then(r=>r.text()).then(t=>{toast(t);files()})">Formatar memória de arquivos</button>
 <p><small>Usuário do /update: admin. Para voltar ao firmware original, envie o .bin da GeekMagic em "Atualizar firmware". Versão <span id="ver"></span> · RAM livre <span id="heap"></span></small></p>
</details></div>
</main>
<div id="menu" onclick="if(event.target==this)this.classList.remove('v')"><div class="card"><img id="mimg"><p id="mname" style="word-break:break-all"></p>
 <div class="row"><button class="btn p" id="mshow">Mostrar agora</button><button class="btn" id="mdel">Apagar</button></div></div></div>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id),f=$('f'),C=339.3;
const LBL={work:'FOCO',short:'PAUSA',long:'PAUSA LONGA',idle:'PRONTO'};
let st=null,at=0,filled=false,lastPhase=null,actx=null,place=null;
const post=(u,b)=>fetch(u,{method:'POST',body:b});
const kb=n=>n>1048576?(n/1048576).toFixed(1)+' MB':Math.round(n/1024)+' KB';
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('v');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('v'),2200)}
function beep(){try{actx=actx||new AudioContext();[0,.25,.5].forEach(d=>{const o=actx.createOscillator(),g=actx.createGain();
 o.frequency.value=880;g.gain.setValueAtTime(.2,actx.currentTime+d);g.gain.exponentialRampToValueAtTime(.001,actx.currentTime+d+.2);
 o.connect(g).connect(actx.destination);o.start(actx.currentTime+d);o.stop(actx.currentTime+d+.2)})}catch(e){}}
function cmd(c){try{actx=actx||new AudioContext()}catch(e){}post('/api/cmd',new URLSearchParams({c})).then(r=>r.json()).then(load)}
function load(s){if(lastPhase&&s.phase!=lastPhase&&s.phase!='idle'&&lastPhase!='idle')beep();lastPhase=s.phase;st=s;at=Date.now();$('net').className='on';
 document.documentElement.style.setProperty('--ac','var(--'+(s.phase=='idle'?'work':s.phase)+')');
 document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('sel',b.dataset.p==s.phase));
 const n=s.cfg.every,d=s.phase=='long'?n:s.cycle%n;$('dots').innerHTML='<i class="f"></i>'.repeat(d)+'<i></i>'.repeat(n-d);
 $('go').innerHTML=s.running?'&#10074;&#10074;':'&#9654;';$('today').textContent=s.today;$('mins').textContent=s.today*s.cfg.work;
 $('clock').textContent=s.clock;$('ver').textContent=s.ver;$('heap').textContent=kb(s.heap)+' (bloco '+kb(s.blk)+')';$('oldl').style.display=s.cfg.lock?'':'none';
 $('city').textContent=s.cfg.city||'—';
 $('wxc').hidden=!(s.cfg.won&&s.wx.ok);if(s.wx.ok){$('wt').textContent=Math.round(s.wx.t)+'°';$('wd').textContent=(s.cfg.city?s.cfg.city+' · ':'')+s.wx.desc;
  $('wm').textContent='Mín '+Math.round(s.wx.min)+'° · Máx '+Math.round(s.wx.max)+'° · Umidade '+s.wx.h+'%'}
 $('fsinfo').textContent=s.fs.ok?kb(s.fs.used)+' usados de '+kb(s.fs.total):'Memória de arquivos indisponível';
 $('fsbar').style.width=(s.fs.total?s.fs.used/s.fs.total*100:0)+'%';$('fmt').hidden=!!s.fs.ok;
 document.querySelectorAll('#grid div').forEach(e=>e.classList.toggle('on',e.dataset.n==s.showing));
 if(!filled){filled=true;for(const k in s.cfg){const e=f.elements[k];if(!e||!e.name)continue;if(e.type=='checkbox')e.checked=!!s.cfg[k];else e.value=s.cfg[k]}}draw()}
function draw(){if(!st)return;const s=st,idle=s.phase=='idle';
 const rem=idle?s.cfg.work*60000:Math.max(0,s.remaining-(s.running?Date.now()-at:0)),sec=Math.ceil(rem/1000);
 const t=String(Math.floor(sec/60)).padStart(2,'0')+':'+String(sec%60).padStart(2,'0');
 $('time').textContent=t;$('phase').textContent=LBL[s.phase]+(!idle&&!s.running?' · PAUSADO':'');
 $('arc').style.strokeDashoffset=idle||!s.total?C:C*rem/s.total;
 document.title=idle?'SmallTV Pomodoro':t+' · '+LBL[s.phase]}
setInterval(draw,250);
f.onsubmit=e=>{e.preventDefault();const b=new URLSearchParams(new FormData(f));
 ['auto','h12','non','won','inv','flip'].forEach(k=>b.set(k,f[k].checked?1:0));
 if(place){b.set('lat',place.latitude);b.set('lon',place.longitude);b.set('city',place.name.slice(0,31))}
 post('/api/settings',b).then(r=>r.json()).then(s=>{place=null;filled=false;load(s);toast('Configurações salvas')})};
f.bri.onchange=()=>post('/api/settings',new URLSearchParams({bri:f.bri.value}));
// busca de cidade direto do navegador (o relógio só guarda latitude/longitude)
let qt;$('q').oninput=()=>{clearTimeout(qt);const q=$('q').value.trim();if(q.length<2){$('res').innerHTML='';return}
 qt=setTimeout(()=>fetch('https://geocoding-api.open-meteo.com/v1/search?count=5&language=pt&name='+encodeURIComponent(q)).then(r=>r.json()).then(j=>{
  const r=$('res');r.innerHTML='';(j.results||[]).forEach(p=>{const b=document.createElement('button');b.type='button';
   b.textContent=[p.name,p.admin1,p.country].filter(Boolean).join(', ');b.onclick=()=>{place={name:p.name.normalize('NFD').replace(/[^\x20-\x7e]/g,'').replace(/["\\]/g,''),latitude:p.latitude,longitude:p.longitude};
   $('city').textContent=place.name+' (salve para aplicar)';r.innerHTML='';$('q').value='';f.won.checked=true};r.appendChild(b)});
  if(!r.children.length)r.textContent='Nenhuma cidade encontrada'}).catch(()=>toast('Sem internet no navegador')),400)};
// álbum
function files(){fetch('/api/files').then(r=>r.json()).then(j=>{const g=$('grid');g.innerHTML='';
 j.files.forEach(x=>{const d=document.createElement('div');d.dataset.n=x.n;d.innerHTML='<img loading="lazy">'+(x.n.toLowerCase().endsWith('.gif')?'<span>GIF</span>':'');
  d.firstChild.src='/fs?f='+encodeURIComponent(x.n);d.onclick=()=>openMenu(x);g.appendChild(d)});
 if(!j.files.length)g.innerHTML='<small style="grid-column:1/-1">Nenhuma foto ainda.</small>'})}
$('albd').ontoggle=()=>{if($('albd').open)files()};
function openMenu(x){$('mimg').src='/fs?f='+encodeURIComponent(x.n);$('mname').textContent=x.n.split('/').pop()+' · '+kb(x.s);
 $('mshow').onclick=()=>post('/api/show',new URLSearchParams({f:x.n})).then(r=>r.text()).then(t=>{toast(t=='ok'?'Mostrando no relógio':t);$('menu').classList.remove('v')});
 $('mdel').onclick=()=>{if(confirm('Apagar '+x.n.split('/').pop()+'?'))post('/api/delete',new URLSearchParams({f:x.n})).then(r=>r.text()).then(t=>{toast(t=='ok'?'Apagado':t);$('menu').classList.remove('v');files()})};
 $('menu').classList.add('v')}
async function prep(file){if(file.type=='image/gif')return{blob:file,name:file.name};
 const img=await createImageBitmap(file),c=document.createElement('canvas');c.width=c.height=240;
 const s=Math.max(240/img.width,240/img.height),w=img.width*s,h=img.height*s;c.getContext('2d').drawImage(img,(240-w)/2,(240-h)/2,w,h);
 const blob=await new Promise(r=>c.toBlob(r,'image/jpeg',.85));return{blob,name:file.name.replace(/\.[^.]+$/,'')+'.jpg'}}
$('up').onchange=async e=>{const fs=[...e.target.files];e.target.value='';let ok=0;
 for(const file of fs){try{toast('Enviando '+file.name+'...');const p=await prep(file),fd=new FormData();fd.append('f',p.blob,p.name);
  const r=await fetch('/api/upload',{method:'POST',body:fd});if(r.ok)ok++;else toast(await r.text())}catch(x){toast('Falha: '+file.name)}}
 if(ok)toast(ok+' arquivo(s) enviado(s)');files()};
$('op').onsubmit=e=>{e.preventDefault();const o=e.target;post('/api/otapass',new URLSearchParams(new FormData(o))).then(r=>r.text()).then(t=>{toast(t);o.reset()})};
document.onkeydown=e=>{if(e.code=='Space'&&!/INPUT|BUTTON|SUMMARY|SELECT/.test(e.target.tagName)){e.preventDefault();cmd('toggle')}};
function poll(){fetch('/api/state').then(r=>r.json()).then(load).catch(()=>$('net').className='off').finally(()=>setTimeout(poll,1000))}poll();
</script></body></html>)rawliteral";
