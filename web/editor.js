// Editor de mostrador do painel: monta um pacote (docs/PACOTES.md) a partir de um modelo pronto, sem escrever JSON.
// Fica no site como panel/editor-<código>.js (web/build.js) e só é baixado quando a pessoa abre "Criar mostrador",
// como o cartão de compartilhar: não ocupa a flash do relógio.
// A prévia aqui é só um esboço (caixas e tamanhos): o desenho de verdade é o do relógio, depois de instalar.
// Os tamanhos de cada vaga estão na tabela SZ; o simulador desenha todas as combinações com o motor do firmware e
// confere que nenhum pixel sai da vaga (sim/editor_modelos.cjs gera os pacotes, sim.cpp: testEditorModels).
(() => {
// cores do guia de design para 240x240: texto normal, rótulo e um destaque por estado
const INK = '#f2eee6', MUTED = '#8c887f', FAINT = '#2c2a27';
const COLORS = [['Branco', INK], ['Cor do relógio', 'accent'], ['Verde', '#46d27e'], ['Âmbar', '#ffb020'], ['Vermelho', '#ff5a47']];
// modelos: vagas [x, y, largura, altura, tipo]; 12 px de margem nas bordas e 8 entre os blocos
const MODELS = [
  ['Um bloco', [[12, 12, 216, 216, 'F']]],
  ['Duas linhas', [[12, 12, 216, 104, 'W'], [12, 124, 216, 104, 'W']]],
  ['Destaque e dois', [[12, 12, 216, 128, 'H'], [12, 148, 104, 80, 'S'], [124, 148, 104, 80, 'S']]]];
// conteúdo: [nome, texto, texto na vaga pequena (0 = o mesmo), fontes (a primeira é a padrão)]
// bebas só desenha 0-9 e ':': letras e o "-" de uma temperatura negativa vão em pixel (em bebas, -5° sairia "5°")
const KINDS = {
  hora: ['Hora', '{HH}:{MM}', 0, ['bebas', 'pixel']],
  data: ['Data', '{WD} {DD} {MON}', '{DD}/{MO}', ['pixel']],
  temp: ['Temperatura', '{TEMP}°', 0, ['pixel']],
  texto: ['Texto', '', 0, ['pixel']]};
// tamanho por tipo de vaga (F 216x216, W 216x104, H 216x128, S 104x80), [sem rótulo, com rótulo]:
// pixel = escala da fonte 6x8, bebas = altura em px. O maior que cabe com o texto mais largo ("23:59", "-13°",
// "QUA 31 OUT", TXTMAX letras); o simulador confere.
const SZ = {
  'hora.bebas': {F: [84, 84], W: [84, 80], H: [84, 84], S: [40, 40]},
  'hora.pixel': {F: [7, 7], W: [7, 7], H: [7, 7]},
  'temp.pixel': {F: [8, 8], W: [8, 8], H: [8, 8], S: [5, 5]},
  'data.pixel': {F: [3, 3], W: [3, 3], H: [3, 3], S: [3, 3]},
  'texto.pixel': {F: [4, 4], W: [3, 3], H: [3, 3], S: [2, 2]}};
// letras do texto livre e do rótulo (pixel 2, em MUTED, no alto da vaga) por tipo de vaga
const TXTMAX = {F: 9, W: 12, H: 12, S: 8}, LBLMAX = {F: 18, W: 18, H: 18, S: 8}, LBL = 24;  // rótulo: 16 px + 8 de vão
const fontH = (f, s) => f == 'pixel' ? 8 * s : s;
// fontes do conteúdo k numa vaga do tipo v: na vaga pequena a hora em pixel teria 24 px, abaixo do mínimo de 40 px
// do guia (docs/PACOTES.md), então lá ela só vai em bebas
const fonts = (k, v) => k == 'hora' && v == 'S' ? ['bebas'] : KINDS[k][3];
// só o que a fonte do relógio desenha (ASCII e letras acentuadas, que viram a letra sem acento); sem { }, que
// seriam lidos como variável
const clean = s => String(s || '').replace(/[^\x20-\x7a|~°À-ÿ]/g, '').trim();
// bloco com valores válidos para a vaga
function block(d, i) {
  const s = MODELS[d.m][1][i], b = d.b[i] || {}, k = KINDS[b.c] ? b.c : 'hora', K = KINDS[k], F = fonts(k, s[4]), f = F.includes(b.f) ? b.f : F[0];
  const lb = clean(b.l).slice(0, LBLMAX[s[4]]);
  return {s, k, f, lb, size: SZ[k + '.' + f][s[4]][lb ? 1 : 0], color: COLORS.some(c => c[1] == b.k) ? b.k : INK,
    text: k == 'texto' ? clean(b.t).slice(0, TXTMAX[s[4]]) : s[4] == 'S' && K[2] || K[1]};
}
// pacote do rascunho d = {m: modelo, n: nome, id, b: [{c: conteúdo, f: fonte, k: cor, l: rótulo, t: texto}]}
function pack(d) {
  const layers = [];
  MODELS[d.m][1].forEach((s, i) => {
    const B = block(d, i), top = s[1] + (B.lb ? LBL : 0), h = s[3] - (B.lb ? LBL : 0);
    if (B.lb) layers.push({type: 'text', x: s[0], y: s[1], font: 'pixel', size: 2, color: MUTED, text: B.lb});
    if (B.text) layers.push({type: 'text', x: s[0], y: top + (h - fontH(B.f, B.size) >> 1), font: B.f, size: B.size, color: B.color, text: B.text});
  });
  return {format: 1, id: d.id, name: clean(d.n).slice(0, 24) || 'Meu mostrador', author: '', desc: 'Feito no editor do painel',
    bg: '#000000', layers};
}
// id próprio (não substitui um mostrador da galeria com o mesmo nome), fixo no rascunho: reinstalar atualiza o mesmo
const newId = (safeId, n) => 'ed-' + (safeId(clean(n)).slice(0, 14).replace(/-+$/, '') || 'tela') + '-' + Math.random().toString(36).slice(2, 6);
const fresh = () => ({m: 2, n: '', b: [{c: 'hora'}, {c: 'data'}, {c: 'temp'}]});

// a prévia fica parada no alto e só o formulário rola (a folha do painel não tem rolagem própria)
const CSS = '.edpv{display:flex;flex-direction:column;align-items:center;gap:4px;text-align:center;margin:10px 0 6px}' +
  '.edcv{width:min(220px,56vw,30dvh);aspect-ratio:1;border-radius:12px;background:#000}' +
  '.edw{max-height:calc(100dvh - min(220px,56vw,30dvh) - 210px);overflow:auto;margin:0 -18px;padding:0 18px}' +
  '.edh{display:block;margin:14px 0 4px}';

function open(P) {
  const {t, el, c} = P;
  if (!document.getElementById('edcss')) { const s = el('style'); s.id = 'edcss'; s.textContent = CSS; document.head.append(s); }
  let d = null;
  try { d = JSON.parse(localStorage.editor || 'null'); } catch (e) {}
  if (!d || !MODELS[d.m] || !Array.isArray(d.b)) d = fresh();
  const keep = () => { try { localStorage.editor = JSON.stringify(d); } catch (e) {} };
  const cv = el('canvas'), g = cv.getContext('2d'), R = Math.max(1, Math.ceil(window.devicePixelRatio || 1));
  cv.width = cv.height = 240 * R;
  cv.className = 'edcv';
  // esboço: vagas tracejadas, rótulo e o conteúdo de exemplo no tamanho da tabela, numa fonte do navegador
  function sketch() {
    g.setTransform(R, 0, 0, R, 0, 0);
    g.fillStyle = '#000';
    g.fillRect(0, 0, 240, 240);
    const now = new Date(), p2 = n => String(n).padStart(2, '0');
    MODELS[d.m][1].forEach((s, i) => {
      const B = block(d, i);
      g.setLineDash([3, 3]);
      g.strokeStyle = FAINT;
      g.strokeRect(s[0] + .5, s[1] + .5, s[2] - 1, s[3] - 1);
      g.textBaseline = 'top';
      if (B.lb) { g.fillStyle = MUTED; g.font = '600 14px monospace'; g.fillText(B.lb, s[0] + 2, s[1] + 1, s[2] - 4); }
      const ex = {hora: p2(now.getHours()) + ':' + p2(now.getMinutes()), data: s[4] == 'S' ? p2(now.getDate()) + '/' + p2(now.getMonth() + 1) :
        now.toLocaleDateString(P.lang, {weekday: 'short', day: '2-digit', month: 'short'}).replace(/[.,]/g, '').toUpperCase(), temp: '23°', texto: B.text}[B.k];
      if (!ex) return;
      const h = fontH(B.f, B.size), top = s[1] + (B.lb ? LBL : 0) + (s[3] - (B.lb ? LBL : 0) - h >> 1);
      g.fillStyle = B.color == 'accent' ? P.accent : B.color;
      g.font = B.f == 'pixel' ? '700 ' + Math.round(h * 1.15) + 'px monospace' : Math.round(h * 1.35) + 'px Impact,"Arial Narrow",sans-serif';
      g.fillText(ex, s[0] + 2, top - h * .08, s[2] - 4);
    });
  }
  const pv = el('div', 'edpv'), w = el('div', 'edw');
  pv.append(cv, el('small', null, t('Esboço. O desenho de verdade aparece no relógio quando você instala.')));
  c.innerHTML = '';
  c.append(el('h3', null, t('Criar mostrador')), pv, w);
  // formulário: refeito a cada troca de modelo ou de conteúdo, no mesmo ponto da rolagem
  function draw() {
    const y = w.scrollTop;
    w.innerHTML = '';
    // modelo
    const ch = el('div', 'chips');
    MODELS.forEach((M, i) => {
      const b = el('button', 'chip' + (i == d.m ? ' on' : ''), t(M[0]));
      b.setAttribute('aria-pressed', i == d.m);
      b.onclick = () => { d.m = i; keep(); draw(); };
      ch.append(b);
    });
    w.append(el('b', 'edh', t('Modelo')), ch);
    // blocos
    MODELS[d.m][1].forEach((s, i) => {
      const b = d.b[i] = d.b[i] || {c: 'texto'}, B = block(d, i), box = el('div');
      box.append(el('b', 'edh', t('Bloco {0}', i + 1)));
      const sel = (name, opts, val, on) => {
        const l = el('label', null, t(name)), x = el('select');
        opts.forEach(([v, n]) => { const o = el('option', null, t(n)); o.value = v; o.selected = v == val; x.append(o); });
        x.onchange = () => { on(x.value); keep(); draw(); };
        l.append(x);
        box.append(l);
      };
      const inp = (name, val, max, ph, on) => {
        const l = el('label', null, t(name)), x = el('input');
        x.type = 'text';
        x.maxLength = max;
        x.value = val || '';
        x.placeholder = t(ph);
        x.oninput = () => { on(x.value); keep(); sketch(); };
        l.append(x);
        box.append(l);
      };
      sel('Mostra', Object.entries(KINDS).map(([k, K]) => [k, K[0]]), B.k, v => { b.c = v; });
      if (B.k == 'texto') inp('Texto', b.t, TXTMAX[s[4]], 'ex.: BOM DIA', v => { b.t = v; });
      if (fonts(B.k, s[4]).length > 1) sel('Fonte', [['bebas', 'Números grandes'], ['pixel', 'Pixel']], B.f, v => { b.f = v; });
      sel('Cor', COLORS.map(([n, v]) => [v, n]), B.color, v => { b.k = v; });
      inp('Rótulo', b.l, LBLMAX[s[4]], 'opcional', v => { b.l = v; });
      w.append(box);
    });
    const nl = el('label', null, t('Nome')), nx = el('input');
    nx.type = 'text';
    nx.maxLength = 24;
    nx.value = d.n || '';
    nx.placeholder = t('Meu mostrador');
    nx.oninput = () => { d.n = nx.value; keep(); };
    nl.append(nx);
    w.append(nl);
    w.append(el('small', null, t('Instalar põe no relógio e já usa; ele aparece em Baixados e pode entrar no rodízio como qualquer outro.')));
    const r = el('div', 'row'), dl = el('button', 'btn', t('Baixar .json')), ok = el('button', 'btn p', t('Instalar e usar'));
    r.style.marginTop = '12px';
    const ready = () => {
      const p = pack(d);
      if (!p.layers.length) { P.toast(t('Escreva algo em pelo menos um bloco')); return null; }
      if (!d.id) { d.id = p.id = newId(P.safeId, p.name); keep(); }
      return new Blob([JSON.stringify(p)], {type: 'application/json'});
    };
    dl.onclick = () => {
      const j = ready();
      if (!j) return;
      const a = el('a');
      a.href = URL.createObjectURL(j);
      a.download = d.id + '.json';
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    };
    ok.onclick = () => { const j = ready(); if (j) P.install(j, null); };
    r.append(dl, ok);
    const nw = el('button', 'btn', t('Começar de novo'));
    nw.style.marginTop = '8px';
    nw.onclick = () => { if (confirm(t('Apagar este rascunho e começar outro?'))) { d = fresh(); keep(); draw(); } };
    w.append(r, nw);
    w.scrollTop = y;
    sketch();
  }
  draw();
}
open.pack = pack;
open.newId = newId;
open.MODELS = MODELS;
open.KINDS = KINDS;
open.fonts = fonts;
open.TXTMAX = TXTMAX;
open.LBLMAX = LBLMAX;
window.faceEditor = open;
})();
