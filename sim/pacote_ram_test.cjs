// Pacote de mostrador na RAM × álbum e OTA: a ligação entre as peças (o comportamento de cada uma está em
// jpg_nomem_test.cjs). O OTA libera o pacote sempre, depois de autorizado; o relógio recarrega o pacote sozinho
// ao desenhar (packEnsureLoaded em renderClock, antes de montar o estado) sem rebaixar o tema; o álbum só libera
// o pacote quando falta bloco (albumMakeRoom), nunca incondicionalmente.
const source = require('./fonte.cjs');
const fn = (sig) => { const a = source.indexOf(sig); if (a < 0) throw Error('não achei ' + sig); return source.slice(a, source.indexOf('\n}', a)); };
const ota = source.slice(source.indexOf('if (up.status == UPLOAD_FILE_START) {', source.indexOf('server.on("/update", HTTP_POST')));
const start = ota.slice(0, ota.indexOf('} else if (up.status == UPLOAD_FILE_WRITE'));
if (!/ota\.authOk\) ota\.err = F\("senha"\);\s*else \{[^}]*facePackUnload\(\)/.test(start)) throw Error('o OTA deve liberar o pacote (facePackUnload) logo que o envio é autorizado');
const rc = fn('static void renderClock() {');
if (rc.indexOf('packEnsureLoaded()') < 0 || rc.indexOf('packEnsureLoaded()') > rc.indexOf('makeState()')) throw Error('renderClock deve chamar packEnsureLoaded() antes de montar o estado');
const pe = fn('void packEnsureLoaded() {');
if (/restoreActivePack|cfg\.theme\s*=[^=]/.test(pe)) throw Error('packEnsureLoaded não pode rebaixar nem trocar o tema');
if (!/previewActive\(\)/.test(pe) || !/facePackLoaded\(\)/.test(pe)) throw Error('packEnsureLoaded deve respeitar a prévia e o pacote já carregado');
const show = fn('bool showAlbumItem(');
if (/facePackUnload\(\)/.test(show)) throw Error('showAlbumItem só pode liberar o pacote por albumMakeRoom (sob pressão de memória)');
if ((show.match(/albumMakeRoom\(/g) || []).length !== 2) throw Error('showAlbumItem deve abrir espaço antes do GIF e antes do JPEG');
if (!/ESP\.getMaxFreeBlockSize\(\) < need/.test(fn('static void albumMakeRoom('))) throw Error('albumMakeRoom deve liberar só quando falta bloco contíguo');
console.log('pacote_ram: OTA libera o pacote, o relógio recarrega sem mexer no tema, o álbum libera só sob pressão');
