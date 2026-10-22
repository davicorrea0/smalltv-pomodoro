// Comprime um firmware para a atualização comprimida do relógio: node compactar.js <firmware.bin>
// Gera <firmware.bin>.gz com janela de 8 KB: é o que o relógio consegue conferir antes de liberar a troca
// (um .gz com janela maior é recusado). Serve também para o firmware original da GeekMagic, para voltar
// a ele quando o nosso firmware passar de ~530 KB.
const fs = require('fs'), zlib = require('zlib');
const f = process.argv[2];
if (!f) { console.log('uso: node compactar.js <firmware.bin>'); process.exit(1); }
const bin = fs.readFileSync(f), gz = zlib.gzipSync(bin, { level: 9, windowBits: 13, memLevel: 9 });
fs.writeFileSync(f + '.gz', gz);
console.log(`${f}.gz: ${gz.length} bytes (${Math.round(gz.length * 100 / bin.length)}% de ${bin.length})`);
