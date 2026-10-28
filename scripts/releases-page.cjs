// Gera a página de versões do site (gh-pages): versoes.html e en/releases.html, da mais nova para a mais antiga.
//
// Uso: node scripts/releases-page.cjs <pasta do site> [--releases releases.json]
//
// Fontes:
//  - notas/<versão>.json        notas da versão (o publicar.sh exige pt/en/es; o site usa pt e en)
//  - notas/historico.json       notas curtas das versões antigas e o "aviso" das que não devem ser usadas
//  - releases do GitHub         data e arquivos para baixar (gh api; ou --releases com o JSON da API já baixado)
// Uma versão sem release (retirada do ar) entra só com as notas e o aviso.
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const REPO = 'davicorrea0/smalltv-pomodoro', ROOT = path.join(__dirname, '..');

const args = process.argv.slice(2);
const site = args.find(a => !a.startsWith('--') && args[args.indexOf(a) - 1] !== '--releases');
if (!site) { console.error('uso: node scripts/releases-page.cjs <pasta do site> [--releases releases.json]'); process.exit(1); }
const relFile = args.includes('--releases') ? args[args.indexOf('--releases') + 1] : null;
const releases = JSON.parse(relFile ? fs.readFileSync(relFile, 'utf8')
  : execFileSync('gh', ['api', '--paginate', '--slurp', `repos/${REPO}/releases?per_page=100`], { encoding: 'utf8', maxBuffer: 32 << 20 })).flat()
  .filter(r => !r.draft);

// ---------- versões ----------
const notasDir = path.join(ROOT, 'notas');
const hist = JSON.parse(fs.readFileSync(path.join(notasDir, 'historico.json'), 'utf8'));
const vers = new Map();
const get = v => vers.get(v) || vers.set(v, { v }).get(v);
for (const r of releases) Object.assign(get(r.tag_name.replace(/^v/, '')), { rel: r });
for (const [v, n] of Object.entries(hist)) if (!v.startsWith('_')) Object.assign(get(v), { notas: n, aviso: n.aviso, data: n.publicada });
for (const f of fs.readdirSync(notasDir)) {
  const m = f.match(/^(\d+\.\d+\.\d+(?:-[\w.]+)?)\.json$/);
  if (m) get(m[1]).notas = JSON.parse(fs.readFileSync(path.join(notasDir, f), 'utf8'));  // as notas da versão valem mais
}
const key = v => { const [n, pre] = v.split('-'); return [...n.split('.').map(Number), pre ? 0 : 1, pre || '']; };
const list = [...vers.values()].sort((a, b) => {
  const x = key(a.v), y = key(b.v);
  for (let i = 0; i < 4; i++) if (x[i] !== y[i]) return y[i] - x[i];
  return y[4] < x[4] ? -1 : y[4] > x[4] ? 1 : 0;
});
const latest = list.find(x => x.rel && !x.rel.prerelease && !x.aviso);
for (const x of list) if (!x.notas) {  // release sem notas em nenhum lugar: usa o nome da release nos três idiomas
  const t = (x.rel.name || x.v).replace(/^.*?—\s*/, '');
  x.notas = { pt: [t], en: [t], es: [t] };
  console.warn(`aviso: ${x.v} sem notas em notas/ (usando o nome da release)`);
}

// ---------- página ----------
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const L = {
  pt: {
    lang: 'pt-BR', file: 'versoes.html', root: '', title: 'Versões — SmallTV Pomodoro', h1: 'Versões',
    desc: 'Todas as versões do firmware SmallTV Pomodoro, com as novidades de cada uma e os arquivos para baixar.',
    lead: 'Todas as versões do firmware, da mais nova para a mais antiga, com o que mudou em cada uma e os arquivos. Para atualizar, o mais fácil é o botão <strong>Atualizar agora</strong> no painel do relógio; os arquivos daqui são para instalar pelo <code>/update</code> ou conferir.',
    avoid: v => `<strong>Não use a ${v.map(x => `<a href="#v${x}">v${x}</a>`).join(' nem a ')}.</strong> A 1.15.1 aumentava a imagem fantasma da tela e a 1.20.0 podia deixar o relógio sem ligar. Se o seu já não liga, siga o <a href="https://github.com/${REPO}/blob/main/docs/RECUPERACAO.md">guia de recuperação</a>.`,
    latest: 'mais recente', pre: 'teste', bad: 'não use', gone: 'retirada',
    files: 'Arquivos', gh: 'Ver no GitHub', sig: 'assinatura', sums: 'SHA256SUMS',
    help: 'Pelo <code>/update</code>, envie o <code>.bin.gz</code> sem descompactar; a partir da 1.20.1 envie junto o <code>.bin.gz.sig</code> (a assinatura). O <code>.bin</code> sem compressão serve para gravar pelo cabo USB-TTL. <code>SHA256SUMS.txt</code> confere os arquivos.',
    count: n => `${n} versões`, nav: ['Início', 'Galeria', 'Versões', 'Jogo'], navHref: ['./', 'galeria.html', 'versoes.html', 'jogo.html'],
    other: 'en/releases.html', otherLang: 'EN', foot: 'English',
    date: { locale: 'pt-BR', opt: { day: 'numeric', month: 'short', year: 'numeric' } },
  },
  en: {
    lang: 'en', file: 'en/releases.html', root: '../', title: 'Releases — SmallTV Pomodoro', h1: 'Releases',
    desc: 'Every SmallTV Pomodoro firmware release, with what changed in each one and the files to download.',
    lead: 'Every firmware release, newest first, with what changed in each one and its files. The easiest way to update is the <strong>Update now</strong> button in the clock\'s panel; the files here are for installing through <code>/update</code> or for checking.',
    avoid: v => `<strong>Do not use ${v.map(x => `<a href="#v${x}">v${x}</a>`).join(' or ')}.</strong> 1.15.1 made screen ghosting worse and 1.20.0 could leave the clock unable to start. If yours no longer starts, follow the <a href="https://github.com/${REPO}/blob/main/docs/RECUPERACAO.md">recovery guide</a> (in Portuguese).`,
    latest: 'latest', pre: 'test', bad: 'do not use', gone: 'withdrawn',
    files: 'Files', gh: 'View on GitHub', sig: 'signature', sums: 'SHA256SUMS',
    help: 'Through <code>/update</code>, send the <code>.bin.gz</code> without unpacking it; from 1.20.1 on, send the <code>.bin.gz.sig</code> (the signature) with it. The uncompressed <code>.bin</code> is for flashing over a USB-TTL cable. <code>SHA256SUMS.txt</code> checks the files.',
    count: n => `${n} releases`, nav: ['Home', 'Gallery', 'Releases', 'Game'], navHref: ['./', 'gallery.html', 'releases.html', 'game.html'],
    other: 'versoes.html', otherLang: 'PT', foot: 'Português',
    date: { locale: 'en-US', opt: { day: 'numeric', month: 'short', year: 'numeric' } },
  },
};
const AVOID = ['1.15.1', '1.20.0'];

function fileLinks(x, t) {
  const a = x.rel ? x.rel.assets : [];
  const pick = re => a.find(f => re.test(f.name));
  const kb = n => `${Math.round(n / 1024)} KB`;
  const out = [];
  const bin = pick(/\.bin$/), gz = pick(/\.bin\.gz$/), sigs = a.filter(f => /\.sig$/.test(f.name)), sums = pick(/^SHA256SUMS/);
  if (gz) out.push(`<a class="btn small primary" href="${esc(gz.browser_download_url)}"><img class="px" src="${t.root}img/icons/download.svg" alt="">.bin.gz <span class="sz">${kb(gz.size)}</span></a>`);
  if (bin) out.push(`<a class="btn small${gz ? '' : ' primary'}" href="${esc(bin.browser_download_url)}"><img class="px" src="${t.root}img/icons/download.svg" alt="">.bin <span class="sz">${kb(bin.size)}</span></a>`);
  for (const s of sigs) out.push(`<a class="btn small" href="${esc(s.browser_download_url)}" title="${t.sig}">${esc(s.name.replace(/^.*?(\.bin(\.gz)?\.sig)$/, '$1'))}</a>`);
  if (sums) out.push(`<a class="btn small" href="${esc(sums.browser_download_url)}">${t.sums}</a>`);
  return out;
}

function item(x, t) {
  const tags = [];
  if (x === latest) tags.push(`<span class="tag pkg">${t.latest}</span>`);
  if (x.rel && x.rel.prerelease && !x.aviso) tags.push(`<span class="tag">${t.pre}</span>`);
  if (x.aviso) tags.push(`<span class="tag bad">${x.rel ? t.bad : t.gone}</span>`);
  const when = x.rel ? x.rel.published_at : x.data;
  const date = when ? new Date(when.length === 10 ? when + 'T12:00:00Z' : when).toLocaleDateString(t.date.locale, { ...t.date.opt, timeZone: 'America/Sao_Paulo' }) : '';
  // o site tem português e inglês: cada página mostra as notas no seu idioma
  const lang = t === L.en ? 'en' : 'pt';
  const notes = `<ul class="list">${x.notas[lang].map(n => `<li>${esc(n)}</li>`).join('')}</ul>`;
  const aviso = x.aviso ? `<div class="note warn">${esc(x.aviso[lang])}</div>\n    ` : '';
  const files = x.aviso ? [] : fileLinks(x, t);
  const gh = x.rel ? `<a class="btn small" href="${esc(x.rel.html_url)}"><span class="gh" aria-hidden="true"></span>${t.gh}</a>` : '';
  return `  <article class="rel${x.aviso ? ' avoid' : ''}" id="v${x.v}">
    <div class="rel-head"><h2>${x.v}</h2>${tags.join('')}${date ? `<time datetime="${esc(when.slice(0, 10))}">${date}</time>` : ''}</div>
    ${aviso}${notes}
    ${files.length || gh ? `<div class="btns">${files.join('')}${gh}</div>` : ''}
  </article>`;
}

function page(t, code) {
  const nav = t.nav.map((n, i) => `<a href="${t.navHref[i]}"${i === 2 ? ' aria-current="page"' : ''}>${n}</a>`).join('\n      ');
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
<link rel="alternate" hreflang="pt-BR" href="https://davicorrea0.github.io/smalltv-pomodoro/versoes.html">
<link rel="alternate" hreflang="en" href="https://davicorrea0.github.io/smalltv-pomodoro/en/releases.html">
<link rel="icon" href="${t.root}img/tomate.svg" type="image/svg+xml">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&family=Silkscreen&display=swap">
<link rel="stylesheet" href="${t.root}style.css?v=15">
</head>
<body>
<!-- gerado por scripts/releases-page.cjs (repositório principal); não edite à mão -->

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
      <div class="eyebrow">Firmware</div>
      <h1>${t.h1} <span class="count">${t.count(list.length)}</span></h1>
      <p>${t.lead}</p>
      <div class="note warn">${t.avoid(AVOID)}</div>
    </div>
  </div>

  <section class="first releases">
    <div class="wrap">
      <p class="section-lead">${t.help}</p>
${list.map(x => item(x, t)).join('\n')}
    </div>
  </section>
</main>

<footer>
  <div class="wrap">
    <div class="foot-bottom">
      <img class="px" src="${t.root}img/tomate.svg" alt="">
      <span>SmallTV Pomodoro · feito por Davi Mattos</span>
      <div class="links">
        <a href="./">${t.nav[0]}</a>
        <a href="https://github.com/${REPO}"><span class="gh" aria-hidden="true"></span>GitHub</a>
        <a href="${code === 'en' ? '../' : ''}${t.other}" hreflang="${code === 'en' ? 'pt-BR' : 'en'}">${t.foot}</a>
      </div>
    </div>
  </div>
</footer>

</body>
</html>
`;
}

for (const [code, t] of Object.entries(L)) {
  const f = path.join(site, t.file);
  fs.mkdirSync(path.dirname(f), { recursive: true });
  fs.writeFileSync(f, page(t, code));
  console.log(`${t.file}: ${list.length} versões`);
}
