#!/usr/bin/env node
// Assinatura das atualizações do SmallTV Pomodoro (RSA-2048, PKCS#1 v1.5, SHA-256).
//
// A assinatura NUNCA vai dentro do firmware: é um arquivo à parte (<arquivo>.sig, 256 bytes) e o relógio a
// recebe no cabeçalho X-Firmware-Sig. Na 1.20.0 ela ia no fim do .bin.gz e o eboot usou os 4 últimos bytes
// como tamanho da imagem, apagando o gerenciador de boot (docs/ASSINATURAS.md).
//
//   node scripts/assinar.cjs chave              cria o par de chaves (uma vez; a privada fica FORA do repositório)
//   node scripts/assinar.cjs header             gera smalltv-pomodoro/ota_key.h a partir de ota-public.pem
//   node scripts/assinar.cjs sign <arquivos>    cria <arquivo>.sig para cada um (precisa da chave privada)
//   node scripts/assinar.cjs verify <arquivos>  confere <arquivo> contra <arquivo>.sig com a chave pública
//
// Chave privada: SMALLTV_SIGNING_KEY ou ~/.smalltv-pomodoro/ota-private.pem.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const pubPath = path.join(root, 'smalltv-pomodoro/ota-public.pem');
const privPath = () => process.env.SMALLTV_SIGNING_KEY || path.join(os.homedir(), '.smalltv-pomodoro', 'ota-private.pem');

function rsa2048(k) {
  if (k.asymmetricKeyType !== 'rsa' || k.asymmetricKeyDetails.modulusLength !== 2048) throw Error('a chave precisa ser RSA de 2048 bits');
  return k;
}
const publicKey = () => rsa2048(crypto.createPublicKey(fs.readFileSync(pubPath)));
const keyId = k => crypto.createHash('sha256').update(k.export({ type: 'spki', format: 'der' })).digest('hex').slice(0, 16);
// Módulo (256 bytes) e expoente, big-endian, como o br_rsa_public_key espera.
function rawKey(k = publicKey()) {
  const j = k.export({ format: 'jwk' });
  return { n: Buffer.from(j.n, 'base64url'), e: Buffer.from(j.e, 'base64url') };
}

function sign(data, priv) {
  return crypto.sign('sha256', data, { key: priv, padding: crypto.constants.RSA_PKCS1_PADDING });
}
function verify(data, sig, pub = publicKey()) {
  return sig.length === 256 && crypto.verify('sha256', data, { key: pub, padding: crypto.constants.RSA_PKCS1_PADDING }, sig);
}

function main(cmd, files) {
  if (cmd === 'chave') {
    const p = privPath();
    if (fs.existsSync(p) || fs.existsSync(pubPath)) throw Error('já existe uma chave (' + (fs.existsSync(p) ? p : pubPath) + '): não troque sem planejar os relógios instalados');
    const { publicKey: pub, privateKey } = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
    fs.writeFileSync(pubPath, pub.export({ type: 'spki', format: 'pem' }));
    console.log('chave privada: ' + p + '\nchave pública: ' + pubPath + '\nid: ' + keyId(pub) + '\nGUARDE UMA CÓPIA DA PRIVADA: sem ela não dá para assinar atualizações para os relógios que confiam nesta chave.');
  } else if (cmd === 'header') {
    // Em bytes (módulo e expoente), para o firmware usar a conta RSA do BearSSL sem o leitor de PEM.
    const { n, e } = rawKey(), hex = b => [...b].map(x => '0x' + x.toString(16).padStart(2, '0'));
    const rows = a => a.join(',').match(/(0x..,?){1,16}/g).join('\n  ');
    fs.writeFileSync(path.join(root, 'smalltv-pomodoro/ota_key.h'),
      '// Gerado por scripts/assinar.cjs a partir de ota-public.pem (só a chave PÚBLICA). Não edite à mão.\n' +
      '// id ' + keyId(publicKey()) + '\n#pragma once\n#include <pgmspace.h>\n' +
      'static const uint8_t OTA_KEY_N[' + n.length + '] PROGMEM = {\n  ' + rows(hex(n)) + '};\n' +
      'static const uint8_t OTA_KEY_E[' + e.length + '] PROGMEM = {' + hex(e).join(',') + '};\n');
    console.log('ota_key.h gerado (id ' + keyId(publicKey()) + ')');
  } else if (cmd === 'sign') {
    if (!files.length) throw Error('informe os arquivos');
    const p = privPath();
    if (!fs.existsSync(p)) throw Error('chave privada não encontrada em ' + p + ' (defina SMALLTV_SIGNING_KEY)');
    const priv = crypto.createPrivateKey(fs.readFileSync(p));
    if (keyId(rsa2048(crypto.createPublicKey(priv))) !== keyId(publicKey())) throw Error('a chave privada não corresponde à ota-public.pem do firmware');
    for (const f of files) {
      const data = fs.readFileSync(f), sig = sign(data, priv);
      if (!verify(data, sig)) throw Error(f + ': a assinatura gerada não confere');
      fs.writeFileSync(f + '.sig', sig);
      console.log(path.basename(f) + '.sig: assinatura conferida');
    }
  } else if (cmd === 'verify') {
    if (!files.length) throw Error('informe os arquivos');
    for (const f of files) {
      if (!verify(fs.readFileSync(f), fs.readFileSync(f + '.sig'))) throw Error(path.basename(f) + ': assinatura inválida');
      console.log(path.basename(f) + ': assinatura OK');
    }
  } else {
    throw Error('uso: node scripts/assinar.cjs chave|header|sign|verify [arquivos]');
  }
}

if (require.main === module) {
  try { main(process.argv[2], process.argv.slice(3)); } catch (e) { console.error('ERRO: ' + e.message); process.exitCode = 1; }
}
module.exports = { sign, verify, keyId, publicKey, rawKey };
