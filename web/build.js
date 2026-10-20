// Gera smalltv-pomodoro/web.h a partir de web/index.html: o painel vai comprimido (gzip) na
// flash e o navegador descompacta sozinho (Content-Encoding: gzip). ~24 KB viram ~9 KB.
// Edite web/index.html e rode este script (sim/build.sh já roda).
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const src = path.join(__dirname, 'index.html');
const out = path.join(__dirname, '..', 'smalltv-pomodoro', 'web.h');
const html = fs.readFileSync(src, 'utf8');
const gz = zlib.gzipSync(html, { level: 9 });
gz[4] = gz[5] = gz[6] = gz[7] = 0;  // zera o horário no cabeçalho gzip: mesmo HTML -> mesmo web.h
let h = '// Gerado por web/build.js a partir de web/index.html — não edite à mão.\n';
h += '// Painel web comprimido com gzip, servido com "Content-Encoding: gzip".\n';
h += '#pragma once\n#include <pgmspace.h>\n\n';
h += `static const uint8_t INDEX_HTML_GZ[${gz.length}] PROGMEM = {`;
for (let i = 0; i < gz.length; i++) h += (i % 24 ? '' : '\n  ') + '0x' + gz[i].toString(16).padStart(2, '0') + ',';
h += '\n};\n';
fs.writeFileSync(out, h);
console.log(`painel: ${(html.length / 1024).toFixed(1)} KB -> ${(gz.length / 1024).toFixed(1)} KB (gzip)`);
