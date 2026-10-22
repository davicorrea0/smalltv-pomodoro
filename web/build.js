// Gera smalltv-pomodoro/web.h a partir de web/index.html: o painel vai comprimido (gzip) na
// flash e o navegador descompacta sozinho (Content-Encoding: gzip). ~70 KB viram ~19 KB.
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
  const gz = zlib.gzipSync(min, { level: 9 });
  gz[4] = gz[5] = gz[6] = gz[7] = 0;  // zera o horário no cabeçalho gzip: mesmo HTML -> mesmo web.h
  let h = '// Gerado por web/build.js a partir de web/index.html — não edite à mão.\n';
  h += '// Painel web comprimido com gzip, servido com "Content-Encoding: gzip".\n';
  h += '#pragma once\n#include <pgmspace.h>\n\n';
  h += `static const uint8_t INDEX_HTML_GZ[${gz.length}] PROGMEM = {`;
  for (let i = 0; i < gz.length; i++) h += (i % 24 ? '' : '\n  ') + '0x' + gz[i].toString(16).padStart(2, '0') + ',';
  h += '\n};\n';
  fs.writeFileSync(out, h);
  console.log(`painel: ${(fs.statSync(src).size / 1024).toFixed(1)} KB -> ${(gz.length / 1024).toFixed(1)} KB (gzip); dicionário i18n-${ver}.json, cartão share-${shv}.js`);
})();
