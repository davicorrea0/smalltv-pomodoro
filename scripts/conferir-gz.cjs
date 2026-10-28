// Confere um .bin.gz do jeito que o eboot do ESP8266 vai consumi-lo: node scripts/conferir-gz.cjs <firmware.bin> <firmware.bin.gz>
// O eboot (copy_raw) lê o tamanho da imagem nos 4 ÚLTIMOS bytes do arquivo gravado, não no fim do fluxo gzip, e
// descompacta com a uzlib (janela de 8 KB). Qualquer byte depois do gzip (como a assinatura da 1.20.0) faz o eboot
// apagar o próprio setor 0 e o relógio não liga mais. Por isso nada vai para a rede sem passar por aqui.
const fs = require('fs'), zlib = require('zlib');

// Devolve a lista de problemas (vazia = o eboot vai aceitar).
function conferir(bin, gz) {
  const erros = [];
  if (bin[0] !== 0xE9) erros.push(`o .bin não começa com o byte mágico 0xE9 (começa com 0x${(bin[0] ?? 0).toString(16)})`);
  if (gz.length < 18 || gz[0] !== 0x1f || gz[1] !== 0x8b || gz[2] !== 8) return [...erros, 'o .bin.gz não é um gzip (deflate)'];
  const ult = gz.readUInt32LE(gz.length - 4);
  if (ult !== bin.length) erros.push(`os 4 últimos bytes do .bin.gz dizem ${ult}, mas o .bin tem ${bin.length} bytes (o eboot apagaria o gerenciador de boot)`);
  // gzip mínimo: cabeçalho de 10 bytes sem campos opcionais (é o que o zlib/compactar.js gera) + deflate + CRC32 + tamanho
  if (gz[3] !== 0) { erros.push('o cabeçalho gzip tem campos opcionais (nome, comentário...); o gerado por compactar.js não tem'); return erros; }
  let r;
  try {
    // windowBits 13 = janela de 8 KB: a zlib recusa distâncias maiores, como a uzlib do relógio
    r = zlib.inflateRawSync(gz.subarray(10), { windowBits: 13, info: true });
  } catch (e) {
    return [...erros, `não descompacta com janela de 8 KB (${e.code || e.message}); gere com node compactar.js`];
  }
  if (!r.buffer.equals(bin)) erros.push('o .bin.gz descompactado não é idêntico ao .bin');
  const depois = gz.length - (10 + r.engine.bytesWritten + 8);
  if (depois !== 0) erros.push(`${depois} byte(s) depois do fim do gzip (assinatura ou lixo): o eboot leria o tamanho errado`);
  else {
    const t = gz.length - 8;
    if (gz.readUInt32LE(t + 4) !== bin.length % 2 ** 32) erros.push('o tamanho no trailer do gzip não confere com o .bin');
    if (zlib.crc32 && gz.readUInt32LE(t) !== zlib.crc32(bin)) erros.push('o CRC32 do trailer do gzip não confere com o .bin');
  }
  return erros;
}
module.exports = { conferir };

if (require.main === module) {
  const [b, g] = process.argv.slice(2);
  if (!b || !g) { console.error('uso: node scripts/conferir-gz.cjs <firmware.bin> <firmware.bin.gz>'); process.exit(2); }
  const e = conferir(fs.readFileSync(b), fs.readFileSync(g));
  if (e.length) { console.error(`ERRO: ${g} NÃO é seguro para o eboot:\n` + e.map(x => '  - ' + x).join('\n')); process.exit(1); }
  console.log(`ok: ${g} confere com o eboot (tamanho ${fs.statSync(b).size} nos 4 últimos bytes, janela 8 KB, nada depois do gzip, magic 0xE9)`);
}
