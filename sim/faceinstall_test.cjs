// Instalar um mostrador por cima de outro com o mesmo id nunca pode apagar o antigo antes da troca: o rename do
// LittleFS (núcleo 3.1.2, lfs_rename) já substitui o destino de forma atômica, e um LittleFS.remove() do .json
// antes dele deixaria o relógio sem o mostrador se a energia caísse entre os dois. Confere o /api/faceinstall real.
const source = require('./fonte.cjs');
const a = source.indexOf('server.on(F("/api/faceinstall")'), b = source.indexOf('server.on(', a + 10);  // até o próximo handler (o de remoção apaga o .json de propósito)
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
// reinstalar o pacote ativo: o motor já tem o novo; só pula a recarga se o .json foi mesmo trocado no disco
if (h.indexOf('renamed = LittleFS.rename("/faces/_novo.json", "/faces/" + id + ".json")') < 0) throw Error('o /api/faceinstall deve guardar o resultado do rename do .json');
if (h.indexOf('if (!(renamed && cfg.theme == THEME_PACK && id == cfg.facePkg)) restoreActivePack();') < 0) throw Error('o /api/faceinstall só pode pular o restoreActivePack com o rename feito e o pacote novo sendo o ativo');
{ // falha no rename do .json: não mexe no .png/.txt antigo, apaga os temporários e responde erro
  const ir = h.indexOf('if (renamed) {', ren), ob = h.indexOf('{', ir);
  let dp = 0, ib = ob;
  for (; ob >= 0 && ib < h.length; ib++) { if (h[ib] === '{') dp++; else if (h[ib] === '}' && --dp === 0) break; }
  if (ir < 0 || ob < 0 || dp !== 0) throw Error('o /api/faceinstall deve guardar .png/.txt dentro de if (renamed)');
  const dentro = h.slice(ob, ib), fora = h.slice(0, ob) + h.slice(ib);
  if (!dentro.includes('id + ".png"') || !dentro.includes('id + ".txt"') || fora.includes('id + ".png"') || fora.includes('id + ".txt"'))
    throw Error('o /api/faceinstall só pode mexer no .png e no .txt com renamed verdadeiro');
  const nr = h.indexOf('if (!renamed)', ib), s200 = h.indexOf('server.send(200,');
  if (nr < 0 || s200 < 0 || nr > s200 || !h.slice(nr, s200).includes('LittleFS.remove("/faces/_novo.json")') ||
      !h.slice(nr, s200).includes('LittleFS.remove("/faces/_novo.png")') || !/if \(!renamed\)\s*\{\s*server\.send(_P)?\(\s*500\b/.test(h.slice(nr, s200)))
    throw Error('o /api/faceinstall deve apagar os temporários e responder erro quando !renamed');
}
console.log('faceinstall: o .json novo substitui o antigo só pelo rename, sem remove antes; o ativo reinstalado não é recarregado');
// Pacote instalado com um firmware anterior (o <id>.txt sem a 4ª linha dos campos de fora): a listagem acha os campos
// lendo o .json, sem carregar o pacote no motor, e grava a linha uma vez só, em modo de acréscimo, depois de percorrer o
// diretório (gravar durante o openDir mexeria na própria listagem). O .txt novo (4 linhas) nunca é lido de novo assim.
{
  const fa = source.indexOf('server.on(F("/api/faces"), HTTP_GET'), fb = source.indexOf('server.on(', fa + 10);
  if (fa < 0 || fb < 0) throw Error('não achei o /api/faces');
  const f = source.slice(fa, fb), loop = f.indexOf('while (d.next())'), fim = f.indexOf('if (pend.length()) facesFieldsSave(pend);');
  if (!f.includes('bool velho = !t.available();') || !f.includes('campos = velho ? String() : t.readStringUntil'))
    throw Error('/api/faces: o .txt de 3 linhas tem de ser reconhecido pelo fim do arquivo');
  // Stream::readStringUntil espera o timeout (1 s) quando o File acaba sem '\n': com 0, o fim do arquivo encerra na hora
  if (!/t\.setTimeout\(0\);[^\n]*\n\s*name = t\.readStringUntil/.test(f))
    throw Error('/api/faces: o .txt tem de ser lido com setTimeout(0), senão cada linha sem \\n no fim custa 1 s');
  if (loop < 0 || fim < 0 || fim < loop || f.slice(loop, fim).includes('LittleFS.open("/faces/" + id + ".txt", "a")'))
    throw Error('/api/faces: a 4ª linha é gravada só depois do laço do diretório');
  const sc = source.slice(source.indexOf('static String facesFieldsScan('), source.indexOf('static void facesFieldsSave('));
  if (!sc.includes('FieldScan sc;') || /loadPack|facePackLoad/.test(sc) || !sc.includes('".json", "r"')) throw Error('facesFieldsScan: só lê o .json, sem o motor');
  const sv = source.slice(source.indexOf('static void facesFieldsSave('), source.indexOf('void setupWeb() {'));
  if (!sv.includes('".txt", "a"') || /remove|"w"/.test(sv)) throw Error('facesFieldsSave: acrescenta a linha, sem apagar nem reescrever o .txt');
  console.log('faceinstall: .txt antigo ganha a 4ª linha (campos lidos do .json, sem o motor) uma vez, depois da listagem');
}
