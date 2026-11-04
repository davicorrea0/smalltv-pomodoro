// Fonte do firmware para os testes que extraem funções reais: main.cpp, os módulos (modulos/*.cpp) e os
// cabeçalhos do sketch, num texto só. Ficam de fora as telas e os dados gerados; app.h vai por último, para
// fn('void x(') achar a definição e não o protótipo.
const fs = require('fs'), path = require('path');
const dir = path.join(__dirname, '../smalltv-pomodoro');
const skip = /^(face_|facepack|fonts|ui\.cpp|assets|web\.h|ota_key)/;
const files = [
  ...fs.readdirSync(dir).filter(f => /\.(cpp|h)$/.test(f) && !skip.test(f)),
  ...fs.readdirSync(path.join(dir, 'modulos')).filter(f => f.endsWith('.cpp')).map(f => 'modulos/' + f),
].sort((a, b) => (a === 'app.h') - (b === 'app.h') || (a < b ? -1 : 1));
module.exports = files.map(f => fs.readFileSync(path.join(dir, f), 'utf8')).join('\n');
