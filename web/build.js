// Gera o painel comprimido (gzip) a partir de web/index.html em web/out/panel-<hash>.html.gz (vai para o
// site e daí para a memória de arquivos do relógio) e smalltv-pomodoro/web.h com o carregador e o hash.
// O navegador descompacta sozinho (Content-Encoding: gzip). ~70 KB viram ~19 KB.
// Edite web/index.html (textos em inglês e espanhol: web/i18n.json) e rode este script
// (sim/build.sh já roda). Com o terser instalado (npm install em web/), o JavaScript sai minificado.
//
// O dicionário inglês/espanhol não vai na flash: o painel busca no site panel/i18n-<código>.json
// (o código é um hash do web/i18n.json, trocado no lugar de __I18V__) e guarda no navegador.
// O cartão de compartilhar (web/share.js) idem, como panel/share-<código>.js, baixado só na hora de compartilhar.
// A cada versão, publique os arquivos de web/out/ na pasta panel/ do site (gh-pages).
const fs = require('fs'), path = require('path'), zlib = require('zlib'), crypto = require('crypto');
const src = path.join(__dirname, 'index.html');
const out = path.join(__dirname, '..', 'smalltv-pomodoro', 'web.h');

(async () => {
  let html = fs.readFileSync(src, 'utf8');
  const dict = fs.readFileSync(path.join(__dirname, 'i18n.json'));
  const ver = crypto.createHash('sha1').update(dict).digest('hex').slice(0, 10);
  html = html.replace(/__I18V__/g, ver).replace(/__I18I__/g, 'sha384-' + crypto.createHash('sha384').update(dict).digest('base64'));
  fs.mkdirSync(path.join(__dirname, 'out'), { recursive: true });
  fs.writeFileSync(path.join(__dirname, 'out', `i18n-${ver}.json`), dict);
  // fusos (web/tz.json, gerado por web/tzgen.sh da tzdata): idem, panel/tz-<código>.json com SRI, baixado só
  // quando a pessoa escolhe a cidade (o relógio recebe a regra POSIX pronta)
  const tz = fs.readFileSync(path.join(__dirname, 'tz.json'));
  const tzv = crypto.createHash('sha1').update(tz).digest('hex').slice(0, 10);
  html = html.replace(/__TZV__/g, tzv).replace(/__TZI__/g, 'sha384-' + crypto.createHash('sha384').update(tz).digest('base64'));
  fs.writeFileSync(path.join(__dirname, 'out', `tz-${tzv}.json`), tz);
  // cartão de compartilhar: web/share.js vai para o site como panel/share-<código>.js (minificado, se der)
  let share = fs.readFileSync(path.join(__dirname, 'share.js'), 'utf8');
  const shv = crypto.createHash('sha1').update(share).digest('hex').slice(0, 10);
  html = html.replace(/__SHV__/g, shv);

  // JavaScript minificado pelo terser, se estiver instalado (só renomeia o que é local: o HTML chama funções globais)
  const a = html.indexOf('<script>') + 8, b = html.indexOf('</script>');
  let terser = null;
  try { terser = require('terser'); } catch (e) { console.log('aviso: sem o terser (npm install em web/), o painel fica ~1 KB maior'); }
  if (terser) share = (await terser.minify(share, { compress: { passes: 2 }, format: { comments: false } })).code;
  fs.writeFileSync(path.join(__dirname, 'out', `share-${shv}.js`), share);
  // SRI do arquivo publicado (já minificado): o navegador recusa rodar se o site servir outra coisa
  html = html.replace(/__SHI__/g, 'sha384-' + crypto.createHash('sha384').update(share).digest('base64'));
  if (terser) html = html.slice(0, a) + (await terser.minify(html.slice(a, b), { compress: { passes: 2 }, format: { comments: false } })).code + html.slice(b);

  // tira o que o navegador não usa antes de comprimir: comentários de linha inteira (HTML e JS) e indentação
  const min = html.replace(/<!--[\s\S]*?-->/g, '').split('\n').map(l => l.trim()).filter(l => l && !l.startsWith('//')).join('\n');
  const gzip = s => {
    const g = zlib.gzipSync(s, { level: 9 });
    g[4] = g[5] = g[6] = g[7] = 0;  // zera o horário no cabeçalho gzip: mesmo HTML -> mesmo arquivo
    return g;
  };
  const gz = gzip(min);
  // A partir da 1.19 o painel não vai na flash do programa: fica na memória de arquivos do relógio como
  // /www/<hash>.gz. O site publica panel/panel-<hash>.html.gz (nunca sobrescrever: cada firmware pede o seu)
  // e o firmware só serve o arquivo cujo SHA-256 bate com o PANEL_HASH abaixo.
  const sha = crypto.createHash('sha256').update(gz);
  const hash = sha.copy().digest('hex').slice(0, 24), sri = 'sha256-' + sha.digest('base64');
  const file = `panel-${hash}.html.gz`;
  fs.writeFileSync(path.join(__dirname, 'out', file), gz);
  // carregador mínimo (web/loader.html), este sim na flash: instala o painel na primeira vez
  const loader = gzip(fs.readFileSync(path.join(__dirname, 'loader.html'), 'utf8').replace(/<!--[\s\S]*?-->/g, '')
    .replace(/__PANEL_FILE__/g, file).replace(/__PANEL_SRI__/g, sri).split('\n').map(l => l.trim()).filter(l => l).join('\n'));
  let h = '// Gerado por web/build.js a partir de web/index.html e web/loader.html — não edite à mão.\n';
  h += '#pragma once\n#include <pgmspace.h>\n\n';
  h += `// painel desta versão: ${file} (${gz.length} bytes), SRI ${sri}\n`;
  // o gzip do Node (zlib com otimizações do processador) pode dar bytes diferentes em outra máquina com o mesmo
  // conteúdo; esta linha só depende do conteúdo e é o que o build automático do GitHub compara
  h += `// conteúdo do painel antes do gzip: sha256 ${crypto.createHash('sha256').update(min).digest('hex')}\n`;
  h += `#define PANEL_HASH "${hash}"  // 24 primeiros hex do SHA-256: nome do arquivo em /www\n`;
  h += `static const uint8_t LOADER_HTML_GZ[${loader.length}] PROGMEM = {`;
  for (let i = 0; i < loader.length; i++) h += (i % 24 ? '' : '\n  ') + '0x' + loader[i].toString(16).padStart(2, '0') + ',';
  h += '\n};\n';
  fs.writeFileSync(out, h);
  // para o latest.json e o scripts/publicar.sh ("site": os arquivos que vão para panel/ no site)
  fs.writeFileSync(path.join(__dirname, 'out', 'panel.json'), JSON.stringify({ panel: file, psri: sri, psize: gz.length,
    site: [file, `i18n-${ver}.json`, `tz-${tzv}.json`, `share-${shv}.js`] }) + '\n');
  console.log(`painel: ${(fs.statSync(src).size / 1024).toFixed(1)} KB -> ${(gz.length / 1024).toFixed(1)} KB (gzip) em ${file}; carregador na flash: ${loader.length} bytes; dicionário i18n-${ver}.json, fusos tz-${tzv}.json, cartão share-${shv}.js`);
})();
