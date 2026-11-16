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
// restoreActivePack: falta de memória agora (mensagem do motor ou do ArduinoJson) não troca o mostrador pelo Matriz;
// marca nova tentativa do packEnsureLoaded e sai antes de rebaixar o tema. Pacote ausente ou inválido continua no Matriz.
const ra = fn('void restoreActivePack() {');
const mem = ra.indexOf('msg.indexOf(F("sem memoria")) >= 0 || msg.indexOf(F("NoMemory")) >= 0'), demote = ra.indexOf('cfg.theme = THEME_MATRIX');
if (mem < 0) throw Error('restoreActivePack deve reconhecer a falta de memória ("sem memoria" e "NoMemory")');
if (demote < 0) throw Error('restoreActivePack deve continuar voltando ao Matriz com pacote ausente ou inválido');
const memBloco = ra.slice(mem, demote);
if (memBloco.indexOf('packRetryAt = millis() + 2000;') < 0 || memBloco.indexOf('return;') < 0) throw Error('com falta de memória, restoreActivePack deve marcar nova tentativa e sair antes de trocar o tema');
if (require('fs').readFileSync(__dirname + '/../smalltv-pomodoro/facepack.cpp', 'utf8').indexOf('PSTR("sem memoria para o pacote")') < 0) throw Error('o motor mudou a mensagem "sem memoria para o pacote": atualize restoreActivePack');
console.log('pacote_ram: OTA libera o pacote, o relógio recarrega sem mexer no tema, o álbum libera só sob pressão, falta de memória ao restaurar não troca o mostrador');
