// Desgaste da flash do SDK: com WiFi.persistent(true), WiFi.mode() e softAP() gravam o modo e o AP na flash a cada
// troca (núcleo 3.1.2, ESP8266WiFiGeneric.cpp, mode()). O persistent fica desligado (padrão do núcleo) e só é ligado
// logo antes de uma gravação de propósito: a rede no /save do portal e o esquecimento dela (wifireset, rec/wifi,
// rec/wipe). Este teste confere isso no código real: quatro ligações, cada uma seguida de WiFi.begin() ou de
// WiFi.disconnect(true), e nenhuma no caminho de boot (connectStored, wifiUnpin).
const source = require('./fonte.cjs');
const ligas = [...source.matchAll(/WiFi\.persistent\(true\)/g)].map(m => m.index);
if (ligas.length !== 4) throw Error('WiFi.persistent(true) deve aparecer só nas 4 gravações de propósito, achei ' + ligas.length);
for (const i of ligas) {
  const depois = source.slice(i, i + 160);
  if (!/WiFi\.(begin\(|disconnect\(true\))/.test(depois)) throw Error('WiFi.persistent(true) sem gravação logo depois: ' + depois.slice(0, 80));
}
for (const head of ['bool connectStored(uint32_t timeoutMs) {', 'static void wifiUnpin() {', 'void startPortal() {']) {
  const a = source.indexOf(head), b = source.indexOf('\n}', a);
  if (a < 0) throw Error('não achei ' + head);
  let corpo = source.slice(a, b);
  if (head.startsWith('void startPortal')) corpo = corpo.slice(0, corpo.indexOf('server.on("/save"'));  // o /save liga de propósito
  if (/WiFi\.persistent\(true\)/.test(corpo)) throw Error(head + ' não pode ligar o persistent');
}
console.log('wifi_flash: persistent só ligado nas 4 gravações de propósito; boot e portal não gravam a flash do SDK');
