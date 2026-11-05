// Rodapé comum do site (gh-pages), o mesmo em todas as páginas e nos dois idiomas.
// Usado pelo releases-page.cjs (a página de versões é gerada) e, com --aplicar, reescreve o <footer> das páginas
// estáticas, para todas ficarem iguais:
//   node scripts/site-rodape.cjs --aplicar <pasta do site>
const fs = require('fs'), path = require('path');
const REPO = 'https://github.com/davicorrea0/smalltv-pomodoro';

const T = {
  pt: {
    root: '', about: 'Firmware de código aberto para o GeekMagic SmallTV Ultra. Feito por Davi Mattos.',
    site: 'Site', nav: [['./', 'Início'], ['galeria.html', 'Galeria'], ['docs.html', 'Docs'], ['versoes.html', 'Versões'], ['jogo.html', 'Jogo']],
    proj: 'Projeto', code: 'Código no GitHub', issue: 'Relatar um problema', lic: 'Licença GPL v3', other: 'English', otherLang: 'en',
    fine: 'GeekMagic e SmallTV são marcas dos seus donos. Este é um projeto independente, sem ligação com a GeekMagic. Gravar outro firmware é por sua conta e risco.',
  },
  en: {
    root: '../', about: 'Open-source firmware for the GeekMagic SmallTV Ultra. Made by Davi Mattos.',
    site: 'Site', nav: [['./', 'Home'], ['gallery.html', 'Gallery'], ['docs.html', 'Docs'], ['releases.html', 'Releases'], ['game.html', 'Game']],
    proj: 'Project', code: 'Code on GitHub', issue: 'Report a problem', lic: 'GPL v3 license', other: 'Português', otherLang: 'pt-BR',
    fine: 'GeekMagic and SmallTV are trademarks of their owners. This is an independent project, not affiliated with GeekMagic. Flashing other firmware is at your own risk.',
  },
};

// lang: 'pt' ou 'en'; outra: endereço da mesma página no outro idioma, relativo a esta (ex.: 'en/docs.html', '../docs.html')
function rodape(lang, outra) {
  const t = T[lang];
  return `<footer>
  <div class="wrap">
    <div class="foot-main">
      <div class="foot-brand">
        <a class="brand" href="./"><img class="px" src="${t.root}img/tomate.svg" alt=""><span>SmallTV Pomodoro</span></a>
        <p>${t.about}</p>
      </div>
      <nav class="foot-nav" aria-label="${t.site}">
        <h3>${t.site}</h3>
        ${t.nav.map(([h, n]) => `<a href="${h}">${n}</a>`).join('\n        ')}
      </nav>
      <nav class="foot-nav" aria-label="${t.proj}">
        <h3>${t.proj}</h3>
        <a href="${REPO}"><span class="gh" aria-hidden="true"></span>${t.code}</a>
        <a href="${REPO}/issues">${t.issue}</a>
        <a href="${REPO}/blob/main/LICENSE">${t.lic}</a>
        <a href="${outra}" hreflang="${t.otherLang}" lang="${t.otherLang}">${t.other}</a>
      </nav>
    </div>
    <p class="fine">${t.fine}</p>
  </div>
</footer>`;
}
module.exports = { rodape };

// Páginas estáticas: arquivo → [idioma, a mesma página no outro idioma]
const PAGINAS = {
  'index.html': ['pt', 'en/'], 'galeria.html': ['pt', 'en/gallery.html'], 'docs.html': ['pt', 'en/docs.html'], 'jogo.html': ['pt', 'en/game.html'],
  'en/index.html': ['en', '../'], 'en/gallery.html': ['en', '../galeria.html'], 'en/docs.html': ['en', '../docs.html'], 'en/game.html': ['en', '../jogo.html'],
};

if (require.main === module) {
  const i = process.argv.indexOf('--aplicar'), site = i >= 0 && process.argv[i + 1];
  if (!site) { console.error('uso: node scripts/site-rodape.cjs --aplicar <pasta do site>'); process.exit(1); }
  for (const [f, [lang, outra]] of Object.entries(PAGINAS)) {
    const p = path.join(site, f), html = fs.readFileSync(p, 'utf8');
    if (!/<footer>[\s\S]*?<\/footer>/.test(html)) { console.error(`ERRO: ${f} sem <footer>`); process.exit(1); }
    fs.writeFileSync(p, html.replace(/<footer>[\s\S]*?<\/footer>/, rodape(lang, outra)));
  }
  console.log(`site-rodape: ${Object.keys(PAGINAS).length} páginas`);
}
