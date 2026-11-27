// Fotos JPEG, GIFs animados, álbum, pacotes de mostradores e rotação entre mostradores.
#include "../app.h"
#include "../src/JPEGDEC/JPEGDEC.h"
#include "../src/AnimatedGIF/AnimatedGIF.h"
#include "../gifcheck.h"
#include "../album_gif_renderer.h"
#include "../rodizio.h"

// ---------- álbum: fotos (JPG) e GIFs no LittleFS; rodízio de mostradores ----------
// Lista "t:1,p:bento,..." (embutido pelo número, pacote pelo id) em /rotation.txt; a cada cfg.rotMin
// minutos, com o relógio parado (sem Pomodoro nem prévia), passa para o próximo. A troca não grava na
// EEPROM (seria uma gravação a cada troca): vale o que estiver salvo ao reiniciar.
String rotList;
String rotTimes;

// Troca do rodízio: a espera é o tempo DECORRIDO desde a última troca (não um prazo em millis(), que parece "no futuro"
// depois de 24,8 dias sem trocar, por exemplo se o rodízio for ligado só depois disso). Sem troca ainda, a primeira vale já.
static uint32_t rotAt = 0;
static bool rotArmed = false;

AlbumList album;

// 100 limita novos envios antes de pressionar o heap; 128 protege o rebuild se a flash já tiver mais arquivos.
// Cada item foi estimado em ~50 B de heap (String + bloco), mas essa estimativa não foi medida.
static constexpr size_t ALBUM_MAX_UPLOAD = 100;
static constexpr size_t ALBUM_MAX_LISTA = 128;

static size_t albumIdx = 0;

bool inAlbum = false;

uint32_t viewUntil = 0;

String curFile;

AnimatedGIF *gif = nullptr;

static File mediaFile;

static int gifOffX = 0, gifOffY = 0, gifScale = 1;

static uint32_t nextFrameAt = 0;
static uint8_t gifNoMemTries = 0;
static AlbumGifRenderState gifRender;

static bool endsWithIgnoreCase(const String &s, const char *suffix) {
  size_t n = s.length(), m = strlen(suffix);
  if (m > n) return false;
  for (size_t i = 0; i < m; i++) {
    char a = s[n - m + i];
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (a != suffix[i]) return false;
  }
  return true;
}

bool isGif(const String &n) { return endsWithIgnoreCase(n, ".gif"); }

bool isJpg(const String &n) { return endsWithIgnoreCase(n, ".jpg") || endsWithIgnoreCase(n, ".jpeg"); }

// Pastas usadas pelo firmware original: /image (fotos e GIFs) e /gif.
static const char *MEDIA_DIRS[] = {"/image", "/gif"};

void rebuildAlbum() {
  fsInfoAt = 0;  // fotos enviadas, apagadas ou formatação: o espaço livre mudou
  album.clear();
  if (!fsOk) return;
  for (const char *dir : MEDIA_DIRS) {
    Dir d = LittleFS.openDir(dir);
    while (d.next()) {
      if (d.isDirectory()) continue;
      String n = String(dir) + "/" + d.fileName();
      // a animação de boot do firmware original não entra no álbum
      if ((isJpg(n) || isGif(n)) && d.fileName() != "boot.gif" && d.fileName() != "boot.jpg") {
        album.push_back(n);
        if (album.size() >= ALBUM_MAX_LISTA) return;
      }
    }
  }
}

// Callbacks de arquivo compartilhados pelos decodificadores de GIF e JPEG (mesmo autor, mesma API).
static void *mediaOpen(const char *fn, int32_t *size) {
  mediaFile = LittleFS.open(fn, "r");
  if (!mediaFile) return nullptr;
  *size = mediaFile.size();
  return &mediaFile;
}

static void mediaClose(void *) {
  if (mediaFile) mediaFile.close();
}

static int32_t jpgRead(JPEGFILE *f, uint8_t *buf, int32_t len) {
  yield();
  File *fl = (File *)f->fHandle;
  int32_t n = len;
  if (f->iSize - f->iPos < len) n = f->iSize - f->iPos;
  if (n <= 0) return 0;
  n = fl->read(buf, n);
  f->iPos = fl->position();
  return n;
}

static int32_t jpgSeek(JPEGFILE *f, int32_t pos) {
  File *fl = (File *)f->fHandle;
  if (pos < 0 || pos > f->iSize || !fl->seek(pos)) return -1;
  f->iPos = fl->position();
  return f->iPos;
}

static int jpgDraw(JPEGDRAW *p) {
  if (p->y >= 240) return 0;
  if (!uiClip.on) {
    tftBitmapBE(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
    return 1;
  }
  blit(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);  // recorta para a área suja; fora da tela, cai na faixa
  return 1;
}

static const int MAX_JPEG_DIM = 1920;  // escala máxima 1/8: nunca entrega mais de 240 px ao display

static bool jpgDimensionsOk(JPEGDEC *jpg) {
  return jpg->getWidth() > 0 && jpg->getHeight() > 0 && jpg->getWidth() <= MAX_JPEG_DIM && jpg->getHeight() <= MAX_JPEG_DIM;
}

static int jpgCheckDraw(JPEGDRAW *) {
  yield();
  return 1;
}

// O decodificador (~7,7 KB) + 2 tabelas AC (4 KB cada) cabem sem tabelas de faixa: sem bloco contiguo para JPEGDEC, nullptr (e o arquivo não é culpado).
static JPEGDEC *jpgAlloc() {
  if (ESP.getMaxFreeBlockSize() < sizeof(JPEGDEC) + 1024) return nullptr;
  return new (std::nothrow) JPEGDEC();
}

JpgResult validJpg(const String &path) {
  diagBlkSample();  // minblk: o maior bloco antes de o decodificador pegar ~18 KB
  JPEGDEC *jpg = jpgAlloc();
  if (!jpg) return JPG_NOMEM;
  bool opened = jpg->open(path.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgCheckDraw);
  bool ok = false;
  if (opened && jpgDimensionsOk(jpg)) {
    int sc = 1;
    while (sc < 8 && (jpg->getWidth() / sc > 240 || jpg->getHeight() / sc > 240)) sc *= 2;
    int opt = sc == 2 ? JPEG_SCALE_HALF : sc == 4 ? JPEG_SCALE_QUARTER : sc == 8 ? JPEG_SCALE_EIGHTH : 0;
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(0, 0, opt);
  }
  bool noMem = !ok && jpg->getLastError() == JPEG_ERROR_MEMORY;  // as tabelas do decodificador não couberam
  jpg->close();
  delete jpg;
  return ok ? JPG_OK : noMem ? JPG_NOMEM : JPG_BAD;
}

// Camada "image" dos pacotes: JPEG em /faces/<arquivo>, no ponto (x, y), sem escala.
bool drawJpgAt(const char *file, int x, int y) {
  String path = String("/faces/") + file;
  if (strstr(file, "..") || !LittleFS.exists(path)) return false;
  JPEGDEC *jpg = jpgAlloc();
  if (!jpg) return false;
  bool ok = false;
  if (jpg->open(path.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw) && jpgDimensionsOk(jpg)) {
    jpg->setPixelType(uiClip.on ? RGB565_LITTLE_ENDIAN : RGB565_BIG_ENDIAN);
    ok = jpg->decode(x, y, 0);
  }
  jpg->close();
  delete jpg;
  return ok;
}

static JpgResult drawJpg(const String &f) {
  JPEGDEC *jpg = jpgAlloc();  // ~7,7 KB + 2x4 KB: só existem durante a decodificação
  if (!jpg) return JPG_NOMEM;
  bool ok = false;
  if (jpg->open(f.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw) && jpgDimensionsOk(jpg)) {
    int w = jpg->getWidth(), h = jpg->getHeight(), sc = 1;
    while (sc < 8 && (w / sc > 240 || h / sc > 240)) sc *= 2;
    int opt = sc == 2 ? JPEG_SCALE_HALF : sc == 4 ? JPEG_SCALE_QUARTER : sc == 8 ? JPEG_SCALE_EIGHTH : 0;
    jpg->setPixelType(RGB565_BIG_ENDIAN);
    ok = jpg->decode(max(0, (240 - w / sc) / 2), max(0, (240 - h / sc) / 2), opt);
  }
  bool noMem = !ok && jpg->getLastError() == JPEG_ERROR_MEMORY;
  jpg->close();
  delete jpg;
  return ok ? JPG_OK : noMem ? JPG_NOMEM : JPG_BAD;
}

static int32_t gifRead(GIFFILE *f, uint8_t *buf, int32_t len) {
  yield();  // inclusive arquivos que gastam muito tempo em códigos LZW sem produzir pixels
  File *fl = (File *)f->fHandle;
  int32_t n = len;
  if (f->iSize - f->iPos < len) n = f->iSize - f->iPos;
  if (n <= 0) return 0;
  n = fl->read(buf, n);
  f->iPos = fl->position();
  return n;
}

static int32_t gifSeek(GIFFILE *f, int32_t pos) {
  File *fl = (File *)f->fHandle;
  fl->seek(pos);
  f->iPos = fl->position();
  return f->iPos;
}

static int gifCheckRead(void *ctx, uint8_t *buf, int n) {
  yield();  // a conferência completa de um arquivo grande não pode disparar o watchdog
  return ((File *)ctx)->read(buf, n);
}

bool validGif(const String &path) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const char *error = gifCheck(gifCheckRead, &file, 480, 480);
  file.close();
  return error == nullptr;
}

struct AlbumGifDisplay {
  void startWrite() { tft.startWrite(); }
  void endWrite() { tft.endWrite(); }
  void setAddrWindow(int x, int y, int w, int h) { tft.setAddrWindow(x, y, w, h); }
  void writePixelsBE(uint16_t *p, int n) { tftPixelsBE(p, n); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { tft.fillRect(x, y, w, h, c); }
};

static void gifDraw(GIFDRAW *p) {
  AlbumGifDisplay out;
  albumGifDraw(gifRender, out, uiRowBuf(), gifOffX, gifOffY, gifScale, p);
}

static void releaseGif() {
  if (gif) {
    gif->close();
    delete gif;
    gif = nullptr;
  }
  gifNoMemTries = 0;
  albumGifRenderFree(gifRender);  // tabela de cores do filtro (768 B no heap)
  gifRender = AlbumGifRenderState();
}

void stopAlbum() {
  releaseGif();
  inAlbum = false;
  curFile = "";
  uiInvalidate();  // o relógio redesenha tudo ao voltar
}

// O pacote de mostrador ativo (até ~6,6 KB) disputa o heap com o GIF (668 B residentes + ~18,9 KB temporários
// por quadro em 4 blocos de até 8 KB) e o JPEG (~7,7 KB + 2x4 KB em blocos separados). Só sai da RAM quando falta
// heap total ou bloco contíguo para a maior alocação; volta sozinho em packEnsureLoaded(). Sem pressão ele fica,
// para o modo "alternado" não reler o pacote a cada item.
static void albumMakeRoom(uint32_t totalNeed, uint32_t maxBlockNeed) {
  if (facePackLoaded() && (ESP.getFreeHeap() < totalNeed || ESP.getMaxFreeBlockSize() < maxBlockNeed))
    facePackUnload();
}

bool showAlbumItem(const String &f) {
  stopAlbum();
  if (!LittleFS.exists(f)) return false;
  setLcdView(false);  // fotos e GIFs usam as cores normais mesmo com um LCD salvo em cfg.theme
  tft.fillScreen(0);
  uiInvalidate();
  inAlbum = true;
  curFile = f;
  if (isGif(f)) {
    if (!validGif(f)) {
      uiError(tr("GIF fora do padrao", "Unsupported GIF", "GIF fuera de norma"));
      return false;
    }
    diagBlkSample();  // minblk: o maior bloco antes do GIF (e antes de albumMakeRoom soltar o pacote)
    albumMakeRoom(sizeof(AnimatedGIF) + 1024 + 19456 + 768, 8192 + 1024);
    gif = new (std::nothrow) AnimatedGIF();  // ~668 B residentes (~18,9 KB temporários só durante playFrame())
    // tabela de cores do filtro (768 B) junto com o GIF, antes dos temporários de cada quadro: alocada no meio de um
    // quadro, ficaria presa entre eles e picotaria o heap. Sem memória, o GIF toca sem o filtro.
    if (gif && !gifRender.rgb) gifRender.rgb = (uint8_t *)malloc(256 * 3);
    if (!gif) {
      diagNoMem(1);
      uiError(tr("Sem memoria p/ GIF", "No memory for GIF", "Sin memoria p/ GIF"));
      return true;  // mantém a tela de aviso até o próximo item
    }
    gif->begin(GIF_PALETTE_RGB565_BE);
    if (!gif->open(f.c_str(), mediaOpen, mediaClose, gifRead, gifSeek, gifDraw)) {
      gif->close();
      delete gif;
      gif = nullptr;
      albumGifRenderFree(gifRender);
      uiError(tr("GIF invalido", "Invalid GIF", "GIF invalido"));
      return false;
    }
    gifScale = min(240 / gif->getCanvasWidth(), 240 / gif->getCanvasHeight());
    if (gifScale < 2) gifScale = 1;
    gifOffX = max(0, (240 - gif->getCanvasWidth() * gifScale) / 2);
    gifOffY = max(0, (240 - gif->getCanvasHeight() * gifScale) / 2);
    nextFrameAt = millis();  // não 0: com mais de 24,8 dias ligado, (int32_t)(millis() - 0) é negativo e o GIF nunca tocava
  } else {
    diagBlkSample();  // minblk: o maior bloco antes da foto (e antes de albumMakeRoom soltar o pacote)
    albumMakeRoom(sizeof(JPEGDEC) + 1024 + 8192, sizeof(JPEGDEC) + 1024);  // ~7,7 KB + 2x4 KB separados
    JpgResult r = drawJpg(f);
    if (r == JPG_NOMEM) {
      diagNoMem(0);
      uiError(tr("Sem memoria p/ foto", "No memory for photo", "Sin memoria p/ foto"));  // o próximo item tenta de novo
    } else if (r == JPG_BAD) uiError(tr("JPG invalido", "Invalid JPG", "JPG invalido"));
  }
  return true;
}

#ifdef GIF_CRONO
uint32_t gifCronoN = 0, gifCronoSum = 0, gifCronoMax = 0;
uint32_t gifCronoDelay = 0, gifCronoGap = 0, gifCronoLast = 0;  // atraso pedido (ms) e intervalo real entre quadros (ms), somados
#endif

void albumLoop() {
  if (!gif || (int32_t)(millis() - nextFrameAt) < 0) return;
  albumMakeRoom(19456 + 1024, 8192 + 1024);
  int delayMs = 0;
#ifdef GIF_CRONO  // build de medição: tempo de cada quadro (decodificar + mandar à tela), lido no diag
  uint32_t cronoT0 = micros();
#endif
  int r = gif->playFrame(false, &delayMs);
#ifdef GIF_CRONO
  uint32_t cronoDt = micros() - cronoT0;
  if (r >= 0) {
    gifCronoN++;
    gifCronoSum += cronoDt;
    if (cronoDt > gifCronoMax) gifCronoMax = cronoDt;
    gifCronoDelay += delayMs;
    uint32_t agora = cronoT0 / 1000;
    if (gifCronoLast) gifCronoGap += agora - gifCronoLast;
    gifCronoLast = agora;
  }
#endif
  if (r < 0) {
    if (gif->getLastError() == GIF_ERROR_MEMORY) {
      if (++gifNoMemTries < 5) {
        nextFrameAt = millis() + 200;  // pico passageiro: tenta de novo sem perder a posição no arquivo
        return;
      }
      diagNoMem(1);
      releaseGif();
      curFile = "";
      uiError(tr("Sem memoria p/ GIF", "No memory for GIF", "Sin memoria p/ GIF"));  // mantém inAlbum=true até o próximo item
      return;
    }
    stopAlbum();
    return;
  }
  gifNoMemTries = 0;
  if (r == 0) gif->reset();  // último quadro: recomeça
  // O próximo quadro conta a partir da hora prevista deste, não do fim do desenho: antes, um quadro que levava 35 ms
  // para desenhar com 40 ms de atraso saía a cada 75 ms (GIF a ~53% da velocidade). Atrasado, não recupera em rajada:
  // segue com 5 ms de folga para o painel e o Wi-Fi entre um quadro e outro.
  uint32_t alvo = nextFrameAt + (uint32_t)max(delayMs, 20);
  uint32_t minimo = millis() + 5;
  nextFrameAt = (int32_t)(alvo - minimo) < 0 ? minimo : alvo;
}

// Decide o que mostrar quando não há Pomodoro: relógio ou um item do álbum.
void scheduleView() {
  uint32_t now = millis();
  if (viewUntil && (int32_t)(now - viewUntil) < 0) return;
  bool wantAlbum = !album.empty() && (cfg.mode == 1 || (cfg.mode == 2 && !inAlbum));
  if (wantAlbum) {
    const char *next = album[albumIdx++ % album.size()];
    // com um item só, não redesenha a mesma foto (evita piscar e reiniciar o GIF)
    if (!(inAlbum && curFile == next)) showAlbumItem(next);
  } else if (inAlbum) {
    stopAlbum();
  }
  viewUntil = now + cfg.albumDelay * 1000UL;
  if (!viewUntil) viewUntil = 1;
}

// ---------- pacotes de mostrador (/faces/<id>.json) ----------
bool idOk(const String &id) {
  if (!id.length() || id.length() > 23) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}

uint16_t packLoadMs = 0;  // ms da última carga que deu certo (0 = nenhuma desde que ligou)

// Lê o arquivo e carrega no motor. Em erro, msg recebe o motivo.
bool loadPackFile(const String &path, String &msg) {
  File f = fsOk ? LittleFS.open(path, "r") : File();
  if (!f) {
    msg = F("arquivo nao encontrado");
    return false;
  }
  size_t n = f.size();
  if (n == 0 || n > 24576) {
    f.close();
    msg = F("pacote vazio ou maior que 24 KB");
    return false;
  }
  char err[96];
  diagBlkSample();  // minblk: o maior bloco antes de o pacote ocupar a RAM
  uint32_t t0 = millis();
  bool ok = facePackLoadFile(f, err, sizeof err);  // em passadas: o JSON não cabe inteiro na RAM
  if (ok) packLoadMs = min<uint32_t>(millis() - t0, 65535);  // diag.pack
  f.close();
  if (!ok) msg = err;
  uiInvalidate();
  return ok;
}

bool loadPack(const String &id) {
  String msg;
  return idOk(id) && loadPackFile("/faces/" + id + ".json", msg);
}

static uint32_t packRetryAt;  // próxima tentativa de packEnsureLoaded() depois de faltar memória (só vale com packRetryWait)
static bool packRetryWait;  // sem isso, o 0 inicial parecia "no futuro" depois de 24,8 dias ligado e o pacote nunca recarregava

// Volta o motor ao pacote salvo nas configurações (ou o libera, se o ativo é um embutido). Pacote ausente ou inválido
// volta ao Matriz; se o que faltou foi memória agora, o mostrador fica e packEnsureLoaded() tenta de novo em 2 s (antes,
// uma falta de RAM passageira depois de instalar um pacote trocava o mostrador da pessoa pelo Matriz).
void restoreActivePack() {
  String msg;
  if (cfg.theme == THEME_PACK && idOk(cfg.facePkg) && loadPackFile("/faces/" + String(cfg.facePkg) + ".json", msg)) return;
  facePackUnload();
  if (cfg.theme == THEME_PACK && (msg.indexOf(F("sem memoria")) >= 0 || msg.indexOf(F("NoMemory")) >= 0)) {
    packRetryAt = millis() + 2000;
    packRetryWait = true;
    uiInvalidate();
    return;
  }
  if (cfg.theme == THEME_PACK) cfg.theme = THEME_MATRIX;
  uiInvalidate();
}

// Recarrega o pacote ativo que o álbum ou o OTA tiraram da RAM (só o relógio precisa dele). Diferente de
// restoreActivePack(), nunca mexe em cfg.theme: se faltar memória agora, tenta de novo em 2 s e a tela mostra
// "Pacote ausente" até lá. Pacote apagado de verdade já é tratado na hora da remoção (/api/facedelete).

void packEnsureLoaded() {
  if (cfg.theme != THEME_PACK || previewActive() || facePackLoaded()) return;
  if (packRetryWait && (int32_t)(millis() - packRetryAt) < 0) return;
  packRetryWait = false;
  if (!loadPack(cfg.facePkg)) { packRetryAt = millis() + 2000; packRetryWait = true; }
}

File packUpload;

String packUploadError;

static UploadGate packGate;

bool packFaceReady = false;

// Recebe "face" (o .json) e, opcional, "preview" (o .png) em arquivos temporários.
void handlePackUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (packUpload) packUpload.close();
    packGate.begin(false, 0);
    if (up.name == "face") { packUploadError = ""; packFaceReady = false; }
    if (!panelOk() || !uploadAuthed()) {
      packUploadError = F("Proibido");
      return;
    }
    if (up.name != "face" && up.name != "preview") { packUploadError = F("Campo invalido"); return; }
    if (packUploadError.length()) return;
    if (!fsOk) {
      packUploadError = F("Sistema de arquivos indisponivel");
      return;
    }
    if (!LittleFS.exists("/faces")) LittleFS.mkdir("/faces");
    if (up.name == "face") { LittleFS.remove("/faces/_novo.json"); LittleFS.remove("/faces/_novo.png"); }
    packUpload = LittleFS.open(up.name == "preview" ? "/faces/_novo.png" : "/faces/_novo.json", "w");
    if (!packUpload) { packUploadError = F("Falha ao criar arquivo"); return; }
    packGate.begin(true, up.name == "face" ? 24576 : 65536);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!packGate.active) return;
    if (!packGate.take(up.currentSize)) packUploadError = F("Arquivo grande demais");
    else if (packUpload.write(up.buf, up.currentSize) != up.currentSize) packUploadError = F("Memoria cheia");
    if (packUploadError.length()) { packUpload.close(); packGate.stop(); }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (packUpload) packUpload.close();
    if (up.status == UPLOAD_FILE_ABORTED) packUploadError = F("Envio interrompido");
    if (packGate.active && !packUploadError.length() && up.name == "face") packFaceReady = true;
    packGate.stop();
    if (packUploadError.length()) packFaceReady = false;
  }
}

bool telaLivre(bool comAlbum) {
  uint32_t now = millis();
  return phase == IDLE && !previewActive() && (comAlbum || !inAlbum) && !vcomTestUntil && !colorTestRequested && !colorTestUntil &&
         !(bootHoldUntil && (int32_t)(now - bootHoldUntil) < 0) && !(alertUntil && (int32_t)(now - alertUntil) < 0) &&
         !recoveryMode && !portalMode && !screenDark();
}

static uint32_t rotDadosAgora;

static bool rotTemCampo(const char *campo) { return appDataGet(campo, rotDadosAgora) != nullptr; }

static bool rotPularPacote(const char *id) {
  char path[40];
  snprintf(path, sizeof path, "/faces/%s.txt", id);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  f.setTimeout(0);
  char linha[104];
  for (int i = 0; i < 3; i++) {
    if (!f.available()) { f.close(); return false; }
    size_t n = f.readBytesUntil('\n', linha, sizeof linha - 1);
    if (n == sizeof linha - 1) { f.close(); return false; }
  }
  if (!f.available()) { f.close(); return false; }
  size_t n = f.readBytesUntil('\n', linha, sizeof linha - 1);
  f.close();
  if (n && linha[n - 1] == '\r') n--;
  linha[n] = 0;
  rotDadosAgora = millis();
  return rodPularMiniapp(linha, rotTemCampo);
}

// chave do mostrador que está na tela, no formato da lista: "t:N" ou "p:id"
static void rotChave(char *out, size_t n) {
  if (cfg.theme == THEME_PACK) snprintf(out, n, "p:%s", cfg.facePkg);
  else snprintf(out, n, "t:%u", (unsigned)cfg.theme);
}

// Quanto o mostrador que está na tela fica (ms): o tempo dele em /rotime.txt (só vale para item que está na lista) ou o
// "Trocar de mostrador". Cacheado por (theme, facePkg, rotMin) e invalidado em rotSet/rotTimesSet/rotTimesLoad: mudar
// qualquer um deles vale na hora, sem varrer rotList e rotTimes em toda volta do loop().
static uint32_t rotCacheMs = 0;
static uint8_t rotCacheTheme = 255, rotCacheMin = 0;
static char rotCachePkg[24] = "";
static bool rotCacheOk = false;

static uint32_t rotEspera() {
  if (rotCacheOk && rotCacheTheme == cfg.theme && rotCacheMin == cfg.rotMin &&
      (cfg.theme != THEME_PACK || !strcmp(rotCachePkg, cfg.facePkg)))
    return rotCacheMs;
  char cur[32];
  rotChave(cur, sizeof cur);
  uint16_t padrao = (uint16_t)cfg.rotMin * 60;
  rotCacheMs = (uint32_t)(rodNaLista(rotList.c_str(), cur) ? rodSegundos(rotTimes.c_str(), cur, padrao) : padrao) * 1000UL;
  rotCacheTheme = cfg.theme;
  rotCacheMin = cfg.rotMin;
  if (cfg.theme == THEME_PACK) strlcpy(rotCachePkg, cfg.facePkg, sizeof rotCachePkg);
  else rotCachePkg[0] = 0;
  rotCacheOk = true;
  return rotCacheMs;
}

// recomeça a contagem agora (a espera vem de rotEspera())
static void rotRecomecar() {
  rotAt = millis();
  rotArmed = true;
}

void rotSet(const String &v) {
  String s;
  s.reserve(min((size_t)300, v.length()) + 1);
  for (char c : v)  // só o formato da lista: nada de aspas ou barras no JSON do estado
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == ',' || c == '-' || c == '_') s += c;
  if (s.length() > 300) {  // corta na última vírgula até o 300º; sem vírgula (lastIndexOf = -1, que o substring lê como "tudo") a lista é inválida
    int cut = s.lastIndexOf(',', 300);
    s = cut > 0 ? s.substring(0, cut) : String();
  }
  rotList = s;
  rotCacheOk = false;
  rotRecomecar();
  if (!fsOk) return;
  if (!s.length()) LittleFS.remove("/rotation.txt");
  else {
    File f = LittleFS.open("/rotation.txt", "w");
    if (f) {
      f.print(s);
      f.close();
    }
  }
}

void rotLoop() {
  if (!cfg.rotMin || !rotList.length() || !telaLivre()) return;
  if (rotArmed && (uint32_t)(millis() - rotAt) < rotEspera()) return;
  String cur = cfg.theme == THEME_PACK ? String("p:") + cfg.facePkg : String("t:") + cfg.theme;
  static String rotParou;
  String anterior = rotParou.length() ? rotParou : cur, primeiro;
  int cargas = 0;
  for (int tries = 0; tries < 32; tries++) {
    char item[32];
    if (!rodProximo(rotList.c_str(), anterior.c_str(), item, sizeof item)) break;
    String it(item);
    if (!primeiro.length()) primeiro = it;
    else if (it == primeiro) break;
    anterior = it;
    if (it == cur) {
      rotParou = "";
      rotRecomecar();
      return;
    }
    if (it.startsWith("t:")) {
      int t = it.substring(2).toInt();
      if (t < 0 || t >= THEME_COUNT) continue;
      cfg.theme = t;
      facePackUnload();
    } else if (it.startsWith("p:")) {
      String id = it.substring(2);
      if (rotPularPacote(id.c_str())) continue;
      cargas++;
      if (!loadPack(id)) {
        rotParou = it;
        if (cargas >= 2) { rotAt = millis() - (rotEspera() - 2000); rotArmed = true; return; }  // tenta de novo em ~2 s
        continue;
      }
      cfg.theme = THEME_PACK;
      strlcpy(cfg.facePkg, id.c_str(), sizeof cfg.facePkg);
    } else continue;
    rotParou = "";
    rotRecomecar();
    uiInvalidateSlide();
    return;
  }
  rotParou = "";
  rotRecomecar();
}

void rotTimesSet(const String &v) {
  char limpo[301];
  rodLimparTempos(v.c_str(), limpo, sizeof limpo);
  rotTimes = limpo;
  rotCacheOk = false;
  rotRecomecar();
  if (!fsOk) return;
  if (!rotTimes.length()) LittleFS.remove("/rotime.txt");
  else {
    File f = LittleFS.open("/rotime.txt", "w");
    if (f) {
      f.print(rotTimes);
      f.close();
    }
  }
}

// Só no início: lê o /rotime.txt sem regravá-lo (o rotTimesSet grava) e sem rearmar a contagem.
void rotTimesLoad() {
  File f = fsOk ? LittleFS.open("/rotime.txt", "r") : File();
  if (!f) return;
  f.setTimeout(0);
  char buf[301], limpo[301];
  size_t n = f.readBytes(buf, sizeof buf - 1);
  f.close();
  buf[n] = 0;
  rodLimparTempos(buf, limpo, sizeof limpo);
  rotTimes = limpo;
  rotCacheOk = false;
}
