#!/usr/bin/env node
// ESP8266 core format: payload + RSA/SHA256 signature + uint32 LE signature length.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const pubPath = path.join(root, 'smalltv-pomodoro/ota-public.pem');
function rsa(key) {
  if (key.asymmetricKeyType !== 'rsa' || key.asymmetricKeyDetails.modulusLength !== 2048)
    throw Error('A chave precisa ser RSA de 2048 bits.');
}
function publicKey() { const k = crypto.createPublicKey(fs.readFileSync(pubPath)); rsa(k); return k; }
function keyId(k) { return crypto.createHash('sha256').update(k.export({type:'spki',format:'der'})).digest('hex'); }
function sign(payload, privatePem) {
  const k = crypto.createPrivateKey(privatePem); rsa(k);
  if (keyId(crypto.createPublicKey(k)) !== keyId(publicKey())) throw Error('Chave privada nao corresponde a chave publica do firmware.');
  const sig = crypto.sign('sha256', payload, {key:k,padding:crypto.constants.RSA_PKCS1_PADDING});
  const len = Buffer.alloc(4); len.writeUInt32LE(sig.length);
  return Buffer.concat([payload,sig,len]);
}
function verify(bytes, pub = publicKey()) {
  if (bytes.length < 261 || bytes.readUInt32LE(bytes.length-4) !== 256) return false;
  return crypto.verify('sha256',bytes.subarray(0,-260),{key:pub,padding:crypto.constants.RSA_PKCS1_PADDING},bytes.subarray(-260,-4));
}
if (require.main === module) {
  try {
    const [cmd, ...files] = process.argv.slice(2);
    if (cmd === 'header') {
      const pem = publicKey().export({type:'spki',format:'pem'}).trim();
      fs.writeFileSync(path.join(root,'smalltv-pomodoro/ota-public.h'), '// Generated from ota-public.pem (public key only).\n#pragma once\nstatic const char OTA_PUBLIC_KEY[] PROGMEM = R"OTAK(' + pem + ')OTAK";\n');
    } else if (cmd === 'sign') {
      if (!process.env.SMALLTV_SIGNING_KEY) throw Error('Defina SMALLTV_SIGNING_KEY para a chave privada fora do repositorio.');
      if (!files.length) throw Error('Informe os arquivos que serao assinados.');
      const key = fs.readFileSync(process.env.SMALLTV_SIGNING_KEY);
      for (const f of files) {
        const output = sign(fs.readFileSync(f),key);
        if (!verify(output)) throw Error('Assinatura nao confere.');
        fs.writeFileSync(f+'.signed',output);
        console.log(path.basename(f)+'.signed: assinatura verificada');
      }
    } else if (cmd === 'verify') {
      if (!files.length) throw Error('Informe os arquivos para verificar.');
      for (const f of files) { if (!verify(fs.readFileSync(f))) throw Error(path.basename(f)+': assinatura invalida'); console.log(path.basename(f)+': OK'); }
    } else throw Error('Uso: node scripts/assinar.cjs header|sign|verify [arquivos]');
  } catch(e) { console.error(e.message); process.exitCode=1; }
}
module.exports = {verify, keyId};
