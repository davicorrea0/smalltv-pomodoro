// Gera a página de novidades do site (gh-pages): novidades.html e en/news.html.
//
// Uso: node scripts/noticias-page.cjs <pasta do site>
// No fim roda o site-seo.cjs, que reescreve title, description, canonical, hreflang e Open Graph destas páginas.
//
// Fontes:
//  - notas/noticias.json   notícias por idioma (a ordem é refeita pela data, da mais nova para a mais antiga):
//                          {"pt": [{"data": "AAAA-MM-DD", "titulo", "texto", "link": {"href", "texto"}}], "en": [...]}
//                          "link" é opcional; href relativo ao site (ex.: "versoes.html") ou endereço completo.
//  - notas/proximas.json   a seção "O que vem por aí" (opcional), por idioma:
//                          {"pt": {"titulo", "lead", "itens": [{"t", "d", "i"}], "link": {"href", "texto"}}, "en": {...}}; "i" (opcional) = ícone de img/icons/ sem .svg
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const REPO = 'davicorrea0/smalltv-pomodoro', ROOT = path.join(__dirname, '..');
const { rodape } = require('./site-rodape.cjs');  // o mesmo rodapé das outras páginas

const site = process.argv[2];
if (!site) { console.error('uso: node scripts/noticias-page.cjs <pasta do site>'); process.exit(1); }

const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const lerJson = f => fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null;
const NOT = lerJson(path.join(ROOT, 'notas', 'noticias.json')) || {};
const PROX = lerJson(path.join(ROOT, 'notas', 'proximas.json'));

const L = {
  pt: {
    lang: 'pt-BR', file: 'novidades.html', root: '', title: 'Novidades — SmallTV Pomodoro', h1: 'Novidades',
    desc: 'Novidades do SmallTV Pomodoro: o que saiu, o que está em teste e o que vem por aí.',
    lead: 'O que saiu, o que está em teste e o que vem por aí, da mais nova para a mais antiga. Os arquivos de cada versão estão em <a href="versoes.html">Versões</a>.',
    eyebrow: 'Projeto', empty: 'Ainda não há novidades por aqui.',
    nav: ['Início', 'Galeria', 'Docs', 'Versões', 'Novidades', 'Jogo'], navHref: ['./', 'galeria.html', 'docs.html', 'versoes.html', 'novidades.html', 'jogo.html'],
    other: 'en/news.html', otherLang: 'EN', date: { locale: 'pt-BR', opt: { day: 'numeric', month: 'short', year: 'numeric' } },
  },
  en: {
    lang: 'en', file: 'en/news.html', root: '../', title: 'News — SmallTV Pomodoro', h1: 'News',
    desc: 'SmallTV Pomodoro news: what is out, what is being tested and what is coming.',
    lead: 'What is out, what is being tested and what is coming, newest first. The files for each version are under <a href="releases.html">Releases</a>.',
    eyebrow: 'Project', empty: 'No news here yet.',
    nav: ['Home', 'Gallery', 'Docs', 'Releases', 'News', 'Game'], navHref: ['./', 'gallery.html', 'docs.html', 'releases.html', 'news.html', 'game.html'],
    other: 'novidades.html', otherLang: 'PT', date: { locale: 'en-US', opt: { day: 'numeric', month: 'short', year: 'numeric' } },
  },
};

function noticia(n, t) {
  const data = String(n.data || '');
  const date = /^\d{4}-\d{2}-\d{2}$/.test(data)
    ? new Date(data + 'T12:00:00Z').toLocaleDateString(t.date.locale, { ...t.date.opt, timeZone: 'America/Sao_Paulo' }) : '';
  const link = n.link && n.link.href ? `\n    <div class="btns"><a class="btn small" href="${esc(n.link.href)}">${esc(n.link.texto || n.link.href)}</a></div>` : '';
  return `  <article class="rel">
    <div class="rel-head"><h2>${esc(n.titulo)}</h2>${date ? `<time datetime="${esc(data)}">${date}</time>` : ''}</div>
    <p>${esc(n.texto)}</p>${link}
  </article>`;
}

// ícone de img/icons/ pelo nome (só letras minúsculas, números e hífen; outro valor fica sem ícone)
const icone = (nome, root) => /^[a-z0-9-]+$/.test(nome || '') ? `<img class="px" src="${root}img/icons/${nome}.svg" alt="">` : '';
function proximas(code, root) {
  const p = PROX && PROX[code];
  if (!p || !Array.isArray(p.itens) || !p.itens.length) return '';
  const cards = p.itens.map(i => {
    const ico = icone(i.i, root);
    return `        <div class="card feature"><h3>${ico ? `<span class="ico">${ico}</span>` : ''}${esc(i.t)}</h3><p>${esc(i.d)}</p></div>`;
  }).join('\n');
  const gh = p.link && p.link.href.startsWith('https://github.com/') ? icone('github', root) : '';
  const link = p.link ? `\n      <a class="btn primary" href="${esc(p.link.href)}">${gh}${esc(p.link.texto)}</a>` : '';
  return `  <section id="${code === 'en' ? 'coming' : 'em-breve'}">
    <div class="wrap">
      <h2>${esc(p.titulo)}</h2>
      <p class="section-lead">${esc(p.lead)}</p>
      <div class="grid three">
${cards}
      </div>${link}
    </div>
  </section>
`;
}

function page(t, code) {
  const self = path.basename(t.file);
  const nav = t.nav.map((n, i) => `<a href="${t.navHref[i]}"${t.navHref[i] === self ? ' aria-current="page"' : ''}>${n}</a>`).join('\n      ');
  const lista = (NOT[code] || []).slice().sort((a, b) => String(b.data).localeCompare(String(a.data)));
  const corpo = lista.length ? lista.map(n => noticia(n, t)).join('\n') : `  <p class="section-lead">${t.empty}</p>`;
  return `<!doctype html>
<html lang="${t.lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${t.title}</title>
<meta name="description" content="${t.desc}">
<meta property="og:title" content="${t.title}">
<meta property="og:image" content="https://davicorrea0.github.io/smalltv-pomodoro/img/mostradores.png">
<meta name="theme-color" content="#0b0d10">
<link rel="alternate" hreflang="pt-BR" href="https://davicorrea0.github.io/smalltv-pomodoro/novidades.html">
<link rel="alternate" hreflang="en" href="https://davicorrea0.github.io/smalltv-pomodoro/en/news.html">
<link rel="icon" href="${t.root}img/tomate.svg" type="image/svg+xml">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&family=Silkscreen&display=swap">
<link rel="stylesheet" href="${t.root}style.css?v=21">
</head>
<body>
<!-- gerado por scripts/noticias-page.cjs (repositório principal); não edite à mão -->

<header class="top">
  <div class="wrap">
    <a class="brand" href="./"><img class="px" src="${t.root}img/tomate.svg" alt=""><span>SmallTV Pomodoro</span></a>
    <nav class="nav">
      ${nav}
      <a class="opt" href="https://github.com/${REPO}"><span class="gh" aria-hidden="true"></span>GitHub</a>
    </nav>
    <a class="lang" href="${code === 'en' ? '../' : ''}${t.other}" hreflang="${code === 'en' ? 'pt-BR' : 'en'}"><img class="px" src="${t.root}img/icons/globo.svg" alt="">${t.otherLang}</a>
  </div>
</header>

<main>
  <div class="page-head">
    <div class="wrap">
      <div class="eyebrow">${t.eyebrow}</div>
      <h1>${t.h1}</h1>
      <p>${t.lead}</p>
    </div>
  </div>

  <section class="first releases">
    <div class="wrap">
${corpo}
    </div>
  </section>

${proximas(code, t.root)}</main>

${rodape(code, (code === 'en' ? '../' : '') + t.other)}

</body>
</html>
`;
}

for (const [code, t] of Object.entries(L)) {
  const f = path.join(site, t.file);
  fs.mkdirSync(path.dirname(f), { recursive: true });
  fs.writeFileSync(f, page(t, code));
  console.log(`${t.file}: ${(NOT[code] || []).length} notícias`);
}
execFileSync(process.execPath, [path.join(__dirname, 'site-seo.cjs'), site], { stdio: 'inherit' });
