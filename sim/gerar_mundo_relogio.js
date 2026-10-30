// Monta smalltv-pomodoro/face_mundo_relogio.cpp a partir de sim/mundo_relogio.tpl, embutindo o mapa (sim/gerar_mapa_relogio.js).
// Rode depois de mexer no mapa ou no modelo: node sim/gerar_mundo_relogio.js
const fs = require('fs'), path = require('path'), cp = require('child_process');
let t = fs.readFileSync(path.join(__dirname, 'mundo_relogio.tpl'), 'utf8');
const map = cp.execFileSync('node', [path.join(__dirname, 'gerar_mapa_relogio.js')]).toString().trimEnd();
const sq = [];
for (let i = 0; i <= 64; i++) sq.push(Math.round(255 * Math.sin(i * Math.PI / 128)));
t = t.replace('@MAPA@', () => map).replace('@SEN@', () => sq.join(', '));
fs.writeFileSync(path.join(__dirname, '..', 'smalltv-pomodoro', 'face_mundo_relogio.cpp'), t);
