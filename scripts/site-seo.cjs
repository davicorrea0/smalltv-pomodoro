// Gera o que o site (gh-pages) tira de outras fontes, para nenhum número ficar escrito à mão:
//   faces.js, os cartões estáticos da galeria, as contagens e a versão nos textos, o bloco de SEO do <head>
//   (title, description, canonical, hreflang, Open Graph, Twitter, JSON-LD) e o sitemap.xml.
//
// Uso: node scripts/site-seo.cjs <pasta do site>      (é idempotente: rodar de novo não muda nada)
//
// Fontes:
//  - <site>/firmware/latest.json     versão estável e data (a mesma que o publicar.sh grava e o painel lê; com a
//                                    ponte de layout, a do campo "l2" quando houver: é a mais nova, ver ponte.cjs)
//  - <site>/catalog/index.json       mostradores instaláveis (galeria e contagem)
//  - smalltv-pomodoro/faces_list.h   mostradores embutidos (a contagem e os nomes); scripts/site-faces.json
//                                    guarda o que a lista não tem (nome e descrição em inglês, estilos do filtro)
// Nas páginas, o texto que muda fica entre <!--g:NOME-->...<!--/g--> e é reescrito aqui. Chamado pelo
// publicar.sh (depois de gerar os JSON) e pelo releases-page.cjs (a página de versões nasce sem esse bloco).
const fs = require('fs'), path = require('path');
const REPO = 'davicorrea0/smalltv-pomodoro', ROOT = path.join(__dirname, '..');
const BASE = 'https://davicorrea0.github.io/smalltv-pomodoro/';

const site = process.argv[2];
if (!site) { console.error('uso: node scripts/site-seo.cjs <pasta do site>'); process.exit(1); }
const die = m => { console.error('ERRO: ' + m); process.exit(1); };
const read = f => fs.readFileSync(path.join(site, f), 'utf8');
const write = (f, s) => fs.writeFileSync(path.join(site, f), s);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

// ---------- fatos ----------
const latestJson = JSON.parse(read('firmware/latest.json'));
const latest = latestJson.l2 && latestJson.l2.version ? latestJson.l2 : latestJson;
if (!/^\d+\.\d+\.\d+$/.test(latest.version)) die('firmware/latest.json sem versão válida');
const catalogo = JSON.parse(read('catalog/index.json'));
const pacotes = (Array.isArray(catalogo) ? catalogo : catalogo.faces).filter(f => f && f.name && f.file);

const meta = JSON.parse(fs.readFileSync(path.join(__dirname, 'site-faces.json'), 'utf8'));
const lista = fs.readFileSync(path.join(ROOT, 'smalltv-pomodoro/faces_list.h'), 'utf8');
const embutidos = [...lista.matchAll(/\bX\(([A-Z0-9_]+),\s*\w+,\s*"([^"]*)",\s*"([^"]*)"\)/g)].map((m, i) => {
  const id = m[1].toLowerCase().replace(/_/g, '-'), e = meta[id];
  if (!e) die(`faces_list.h tem o mostrador ${m[1]}, mas scripts/site-faces.json não tem "${id}" (nome e descrição em inglês, estilos)`);
  for (const p of ['tags', 'name_en', 'desc_en']) if (!e[p]) die(`scripts/site-faces.json: "${id}" sem ${p}`);
  return { id, num: i, name: m[2], desc: m[3], author: 'davicorrea0', ...e };
});
if (!embutidos.length) die('nenhum mostrador lido de faces_list.h');
for (const f of embutidos) if (!fs.existsSync(path.join(site, `img/faces/face-${f.num}.png`))) die(`falta a prévia img/faces/face-${f.num}.png (${f.name}): publique a prévia antes`);
const F = { version: latest.version, date: latest.date, builtin: embutidos.length, catalog: pacotes.length };

// ---------- faces.js (lido pela galeria e pela vitrine da página inicial) ----------
const q = s => JSON.stringify(s);
write('faces.js', `// Mostradores embutidos no firmware, na ordem de smalltv-pomodoro/faces_list.h (o número é o salvo nas
// configurações). Gerado por scripts/site-seo.cjs (repositório principal); não edite à mão. Prévias: img/faces/.
// tags: estilos do filtro da galeria (analogico, digital, pixel, esportivo, retro).
// As páginas em inglês ficam em en/ e declaram <html data-root="../">.
window.SITE_ROOT = document.documentElement.getAttribute("data-root") || "";
window.SITE_LANG = document.documentElement.lang === "en" ? "en" : "pt";
window.BUILTIN_FACES = [
${embutidos.map(f => `  { id: ${q(f.id)}, tags: ${q(f.tags)}, name: ${q(f.name)}, desc: ${q(f.desc)},
    name_en: ${q(f.name_en)}, desc_en: ${q(f.desc_en)} }`).join(',\n')}
].map(function (f, i) {
  if (window.SITE_LANG === "en") { f.name = f.name_en; f.desc = f.desc_en; }
  f.num = i;
  f.author = "davicorrea0";
  f.preview = window.SITE_ROOT + "img/faces/face-" + i + ".png";
  return f;
});
`);

// ---------- páginas ----------
// Cada linha é um par PT/EN. title e desc podem usar {version}, {builtin} e {catalog}.
const PAGES = [
  { pt: 'index.html', en: 'en/index.html', img: 'img/mostradores.png',
    title: { pt: 'SmallTV Pomodoro — firmware para o GeekMagic SmallTV Ultra', en: 'SmallTV Pomodoro — firmware for the GeekMagic SmallTV Ultra' },
    desc: {
      pt: 'Firmware de código aberto para o GeekMagic SmallTV Ultra: {builtin} mostradores embutidos e {catalog} para instalar, Pomodoro com mascote tomate, clima sem chave de API, fotos e painel web.',
      en: 'Open source firmware for the GeekMagic SmallTV Ultra desk clock: {builtin} built-in clock faces and {catalog} installable ones, a Pomodoro timer with a tomato mascot, keyless weather, photo album and a web panel.' },
    alt: { pt: 'Os mostradores do SmallTV Pomodoro no relógio GeekMagic SmallTV Ultra', en: 'SmallTV Pomodoro clock faces on the GeekMagic SmallTV Ultra' }, home: true },
  { pt: 'galeria.html', en: 'en/gallery.html', img: 'img/mostradores.png',
    title: { pt: 'Galeria de mostradores para o SmallTV Ultra — SmallTV Pomodoro', en: 'Clock faces for the GeekMagic SmallTV Ultra — SmallTV Pomodoro' },
    desc: {
      pt: '{catalog} mostradores para instalar no GeekMagic SmallTV Ultra com o firmware SmallTV Pomodoro, além dos {builtin} que já vêm no firmware. Prévia em tamanho real e download do .json.',
      en: '{catalog} clock faces to install on the GeekMagic SmallTV Ultra with the SmallTV Pomodoro firmware, plus the {builtin} built-in ones. Full-size previews and .json download.' },
    alt: { pt: 'Prévias dos mostradores embutidos do SmallTV Pomodoro', en: 'Previews of the SmallTV Pomodoro built-in clock faces' } },
  { pt: 'docs.html', en: 'en/docs.html', img: 'img/mostradores.png',
    title: { pt: 'Como instalar e usar o SmallTV Pomodoro — guia passo a passo', en: 'How to install and use SmallTV Pomodoro — step-by-step guide' },
    desc: {
      pt: 'Guia passo a passo do SmallTV Pomodoro: instalar, conectar ao Wi-Fi, achar o painel, usar o Pomodoro, mostradores, fotos, clima, backup, recuperação e perguntas frequentes.',
      en: 'Step by step guide to SmallTV Pomodoro: install, connect to Wi-Fi, find the panel, use Pomodoro, faces, photos, weather, backup, recovery and FAQ.' },
    alt: { pt: 'Os mostradores do SmallTV Pomodoro', en: 'SmallTV Pomodoro clock faces' } },
  { pt: 'versoes.html', en: 'en/releases.html', img: 'img/mostradores.png',
    title: { pt: 'Versões e downloads do firmware — SmallTV Pomodoro', en: 'Firmware releases and downloads — SmallTV Pomodoro' },
    desc: {
      pt: 'Versões do firmware SmallTV Pomodoro (atual: {version}): o que mudou em cada uma e os arquivos .bin.gz com assinatura para atualizar o GeekMagic SmallTV Ultra.',
      en: 'SmallTV Pomodoro firmware releases (current: {version}): what changed in each one and the signed .bin.gz files to update the GeekMagic SmallTV Ultra.' },
    alt: { pt: 'Os mostradores do SmallTV Pomodoro', en: 'SmallTV Pomodoro clock faces' } },
  { pt: 'jogo.html', en: 'en/game.html', img: { pt: 'img/jogo/cartao-pt.png', en: 'img/jogo/cartao-en.png' },
    title: { pt: 'Jogo do foco: sequência, planta e horta — SmallTV Pomodoro', en: 'Focus game: streaks, plant and garden — SmallTV Pomodoro' },
    desc: {
      pt: 'Sequência de dias, planta que cresce até o tomate dourado, horta do ano, 10 conquistas e cartão do dia para compartilhar: o jogo do foco do SmallTV Pomodoro.',
      en: 'Day streaks, a plant that grows into the golden tomato, a yearly garden, 10 achievements and a shareable day card: the SmallTV Pomodoro focus game.' },
    alt: { pt: 'Cartão do dia do jogo do foco', en: 'Day card from the focus game' } },
];
const LANG = { pt: { html: 'pt-BR', og: 'pt_BR', other: 'en', otherOg: 'en_US' }, en: { html: 'en', og: 'en_US', other: 'pt', otherOg: 'pt_BR' } };
const url = f => BASE + f.replace(/index\.html$/, '');
const fill = (s, lang) => s[lang].replace(/\{(\w+)\}/g, (_, k) => F[k] ?? die(`campo {${k}} desconhecido`));
const png = f => {  // largura e altura do PNG, lidas do cabeçalho (o og:image declara o tamanho)
  const b = fs.readFileSync(path.join(site, f));
  return b.toString('latin1', 1, 4) === 'PNG' ? { w: b.readUInt32BE(16), h: b.readUInt32BE(20) } : null;
};

const jsonld = (page, lang) => {
  if (!page.home) return '';
  const L = LANG[lang], desc = fill(page.desc, lang), home = url(page[lang]);
  const autor = { '@type': 'Person', name: 'Davi Mattos', url: 'https://github.com/davicorrea0' };
  const grafo = { '@context': 'https://schema.org', '@graph': [
    { '@type': 'WebSite', '@id': BASE + '#site', name: 'SmallTV Pomodoro', url: home, description: desc, inLanguage: L.html,
      publisher: autor },
    { '@type': 'SoftwareSourceCode', '@id': BASE + '#codigo', name: 'SmallTV Pomodoro', url: home, description: desc,
      codeRepository: `https://github.com/${REPO}`, license: 'https://www.gnu.org/licenses/gpl-3.0.html',
      programmingLanguage: ['C++', 'JavaScript'], runtimePlatform: 'ESP8266', version: F.version, dateModified: F.date,
      author: autor, inLanguage: L.html, isPartOf: { '@id': BASE + '#site' } },
  ] };
  return `<script type="application/ld+json">${JSON.stringify(grafo)}</script>\n`;
};

function headSeo(page, lang) {
  const L = LANG[lang], other = L.other, title = esc(fill(page.title, lang)), desc = esc(fill(page.desc, lang));
  const img = typeof page.img === 'string' ? page.img : page.img[lang], dim = png(img);
  if (!dim) die(`${img} não é um PNG`);
  const alt = esc(page.alt[lang]), self = url(page[lang]);
  return `<!-- seo: gerado por scripts/site-seo.cjs (repositório principal); não edite à mão -->
<title>${title}</title>
<meta name="description" content="${desc}">
<link rel="canonical" href="${self}">
<link rel="alternate" hreflang="pt-BR" href="${url(page.pt)}">
<link rel="alternate" hreflang="en" href="${url(page.en)}">
<meta property="og:type" content="website">
<meta property="og:site_name" content="SmallTV Pomodoro">
<meta property="og:title" content="${title}">
<meta property="og:description" content="${desc}">
<meta property="og:url" content="${self}">
<meta property="og:locale" content="${L.og}">
<meta property="og:locale:alternate" content="${LANG[other].og}">
<meta property="og:image" content="${BASE + img}">
<meta property="og:image:width" content="${dim.w}">
<meta property="og:image:height" content="${dim.h}">
<meta property="og:image:alt" content="${alt}">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="${title}">
<meta name="twitter:description" content="${desc}">
<meta name="twitter:image" content="${BASE + img}">
<meta name="twitter:image:alt" content="${alt}">
${jsonld(page, lang)}<!-- /seo -->
`;
}

// ---------- cartões estáticos da galeria (mesmo HTML que o galeria.js monta; o JS refaz por cima, com os filtros) ----------
const TXT = {
  pt: { zoom: 'Ampliar ', preview: 'Prévia do mostrador ', by: 'por ', builtin: 'Embutido', anim: 'animado', inFw: ' · já vem no firmware', dl: 'Baixar .json',
    fw: v => `Precisa do firmware v${v} ou mais novo`, dec: ',' },
  en: { zoom: 'Enlarge ', preview: 'Preview of ', by: 'by ', builtin: 'Built-in', anim: 'animated', inFw: ' · comes with the firmware', dl: 'Download .json',
    fw: v => `Requires firmware v${v} or newer`, dec: '.' },
};
function cartao(f, kind, lang, root) {
  const S = TXT[lang], en = lang === 'en';
  const nome = (en && f.name_en) || f.name, desc = (en && f.desc_en) || f.desc;
  const img = kind === 'pkg' ? root + 'catalog/' + f.preview : root + 'img/faces/face-' + f.num + '.png';
  const tags = kind === 'pkg'
    ? (f.fw ? `<span class="tag pkg" title="${esc(S.fw(f.fw))}">v${esc(f.fw)}+</span>` : '')
    : `<span class="num">#${f.num}</span><span class="tag">${S.builtin}</span>`;
  const anim = kind === 'pkg' && f.anim ? `<span class="tag anim">${S.anim}</span>` : '';
  const size = kind === 'pkg' && f.size ? ` · ${(f.size / 1024).toFixed(1).replace('.', S.dec)} KB` : '';
  const acao = kind === 'pkg'
    ? `${f.fw ? `<p class="req">${esc(S.fw(f.fw))}</p>` : ''}<div class="actions"><a class="btn small primary" href="${esc(root + 'catalog/' + f.file)}" download="${esc(f.id || 'mostrador')}.json"><img class="px" src="${root}img/icons/download.svg" alt="" width="16" height="16">${S.dl}</a></div>`
    : '';
  return `<article class="face"${kind === 'pkg' && f.id ? ` id="${esc(f.id)}"` : ''}><button class="prev" type="button" aria-label="${S.zoom}${esc(nome)}"><img class="px" src="${esc(img)}" alt="${S.preview}${esc(nome)}" width="240" height="240" loading="lazy"></button><div class="body"><div class="title"><h3>${esc(nome)}</h3>${tags}${anim}</div>${desc ? `<p class="desc">${esc(desc)}</p>` : ''}<div class="by">${S.by}<b>${esc(f.author || '?')}</b>${kind === 'pkg' ? size : S.inFw}</div>${acao}</div></article>`;
}

// ---------- aplicar nas páginas ----------
const hash = f => require('crypto').createHash('sha256').update(fs.readFileSync(path.join(site, f))).digest('hex').slice(0, 8);
const gen = (html, valores) => html.replace(/<!--g:(\w+)-->[\s\S]*?<!--\/g-->/g, (m, k) => {
  if (!(k in valores)) die(`marcador <!--g:${k}--> desconhecido`);
  return `<!--g:${k}-->${valores[k]}<!--/g-->`;
});
const unicos = new Set();
for (const page of PAGES) for (const lang of ['pt', 'en']) {
  const file = page[lang], root = lang === 'en' ? '../' : '';
  if (!fs.existsSync(path.join(site, file))) die(`${file} não existe em ${site}`);
  let html = read(file);
  const i = html.indexOf('</head>');
  if (i < 0) die(`${file} sem </head>`);
  // o <head> perde as tags que este script reescreve (inclusive as de antes dele) e ganha o bloco novo depois do viewport
  const strip = /^[ \t]*(?:<title>[\s\S]*?<\/title>|<meta (?:name="description"|property="og:[^"]*"|name="twitter:[^"]*")[^>]*>|<link rel="(?:canonical|alternate)"[^>]*>|<script type="application\/ld\+json">[\s\S]*?<\/script>|<!-- seo:[\s\S]*?<!-- \/seo -->)[ \t]*\r?\n/gm;
  let head = html.slice(0, i).replace(strip, '');
  const vp = head.match(/^<meta name="viewport"[^>]*>\r?\n/m);
  if (!vp) die(`${file} sem <meta name="viewport">`);
  head = head.replace(vp[0], vp[0] + headSeo(page, lang));
  html = head + html.slice(i);

  const valores = { version: F.version, builtin: F.builtin, catalog: F.catalog };
  if (file === 'galeria.html' || file === 'en/gallery.html') {
    valores.cards_packs = pacotes.map(f => cartao(f, 'pkg', lang, root)).join('\n');
    valores.cards_builtin = embutidos.map(f => cartao(f, 'builtin', lang, root)).join('\n');
    html = html.replace(/(<span class="count" id="(n-all|n-pkg|n-builtin|packs-count|builtin-count)">)[^<]*(<\/span>)/g,
      (m, a, id, z) => a + ({ 'n-all': F.builtin + F.catalog, 'n-pkg': F.catalog, 'n-builtin': F.builtin, 'packs-count': F.catalog, 'builtin-count': F.builtin })[id] + z);
  }
  html = gen(html, valores);
  // endereço novo do script a cada mudança do conteúdo (o ?v= é o que fura o cache do navegador)
  html = html.replace(/(\b(?:faces|galeria)\.js)\?v=\w+/g, (m, js) => `${js}?v=${hash(js)}`);
  write(file, html);
  unicos.add(file);
}

// ---------- sitemap.xml ----------
// Só páginas que existem (conferido acima). Sem lastmod: a data de arquivo no CI não diz nada sobre o conteúdo.
const entrada = (page, lang) => `  <url>
    <loc>${url(page[lang])}</loc>
    <xhtml:link rel="alternate" hreflang="pt-BR" href="${url(page.pt)}"/>
    <xhtml:link rel="alternate" hreflang="en" href="${url(page.en)}"/>
  </url>`;
write('sitemap.xml', `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">
${PAGES.flatMap(p => ['pt', 'en'].map(l => entrada(p, l))).join('\n')}
</urlset>
`);
console.log(`site-seo: versão ${F.version}, ${F.builtin} embutidos, ${F.catalog} instaláveis; ${unicos.size} páginas e sitemap.xml (${PAGES.length * 2} URLs)`);
