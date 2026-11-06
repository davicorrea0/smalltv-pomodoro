// Instalar um mostrador por cima de outro com o mesmo id nunca pode apagar o antigo antes da troca: o rename do
// LittleFS (núcleo 3.1.2, lfs_rename) já substitui o destino de forma atômica, e um LittleFS.remove() do .json
// antes dele deixaria o relógio sem o mostrador se a energia caísse entre os dois. Confere o /api/faceinstall real.
const source = require('./fonte.cjs');
const a = source.indexOf('server.on("/api/faceinstall"'), b = source.indexOf('server.on(', a + 10);  // até o próximo handler (o de remoção apaga o .json de propósito)
if (a < 0 || b < 0) throw Error('não achei o /api/faceinstall');
const h = source.slice(a, b);
const ren = h.indexOf('LittleFS.rename("/faces/_novo.json", "/faces/" + id + ".json")');
if (ren < 0) throw Error('não achei o rename do .json no /api/faceinstall');
const antes = h.slice(0, ren).split('\n').filter(l => !/^\s*\/\//.test(l)).join('\n');
if (/LittleFS\.remove\([^)]*\.json"\)/.test(antes.replace(/LittleFS\.remove\("\/faces\/_novo\.json"\)/g, ''))) {
  throw Error('o /api/faceinstall apaga o .json de destino antes do rename (janela sem mostrador se a energia cair)');
}
// o rename do .json acontece antes de qualquer remove do .json de destino em todo o handler
if (/LittleFS\.remove\("\/faces\/" \+ id \+ "\.json"\)/.test(h)) throw Error('o /api/faceinstall não deve apagar /faces/<id>.json');
console.log('faceinstall: o .json novo substitui o antigo só pelo rename, sem remove antes');
