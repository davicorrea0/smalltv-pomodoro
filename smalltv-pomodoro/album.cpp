// Fotos JPEG, GIFs animados, álbum, pacotes de mostradores e rotação entre mostradores.
#include "app.h"
#include "src/JPEGDEC/JPEGDEC.h"
#include "src/AnimatedGIF/AnimatedGIF.h"
#include "gifcheck.h"

// ---------- rodízio de mostradores ----------
// Lista "t:1,p:bento,..." (embutido pelo número, pacote pelo id) em /rotation.txt; a cada cfg.rotMin
// minutos, com o relógio parado (sem Pomodoro nem prévia), passa para o próximo. A troca não grava na
// EEPROM (seria uma gravação a cada troca): vale o que estiver salvo ao reiniciar.
String rotList;

static uint32_t rotNext = 0;

std::vector<String> album;

static size_t albumIdx = 0;

bool inAlbum = false;

uint32_t viewUntil = 0;

String curFile;

AnimatedGIF *gif = nullptr;

static File mediaFile;

static int gifOffX = 0, gifOffY = 0;

static uint32_t nextFrameAt = 0;

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
      if ((isJpg(n) || isGif(n)) && d.fileName() != "boot.gif" && d.fileName() != "boot.jpg") album.push_back(n);
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
    tftBitmap(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
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

// O decodificador (~18 KB) precisa de um bloco contíguo; sem ele, nullptr (e o arquivo não é culpado).
static JPEGDEC *jpgAlloc() {
  if (ESP.getMaxFreeBlockSize() < sizeof(JPEGDEC) + 1024) return nullptr;
  return new (std::nothrow) JPEGDEC();
}

JpgResult validJpg(const String &path) {
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
  jpg->close();
  delete jpg;
  return ok ? JPG_OK : JPG_BAD;
}

// Camada "image" dos pacotes: JPEG em /faces/<arquivo>, no ponto (x, y), sem escala.
bool drawJpgAt(const char *file, int x, int y) {
  String path = String("/faces/") + file;
  if (strstr(file, "..") || !LittleFS.exists(path)) return false;
  JPEGDEC *jpg = jpgAlloc();
  if (!jpg) return false;
  bool ok = false;
  if (jpg->open(path.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw) && jpgDimensionsOk(jpg)) {
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(x, y, 0);
  }
  jpg->close();
  delete jpg;
  return ok;
}

static JpgResult drawJpg(const String &f) {
  JPEGDEC *jpg = jpgAlloc();  // ~18 KB: só existe durante a decodificação
  if (!jpg) return JPG_NOMEM;
  bool ok = false;
  if (jpg->open(f.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw) && jpgDimensionsOk(jpg)) {
    int w = jpg->getWidth(), h = jpg->getHeight(), sc = 1;
    while (sc < 8 && (w / sc > 240 || h / sc > 240)) sc *= 2;
    int opt = sc == 2 ? JPEG_SCALE_HALF : sc == 4 ? JPEG_SCALE_QUARTER : sc == 8 ? JPEG_SCALE_EIGHTH : 0;
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(max(0, (240 - w / sc) / 2), max(0, (240 - h / sc) / 2), opt);
  }
  jpg->close();
  delete jpg;
  return ok ? JPG_OK : JPG_BAD;
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

static void gifDraw(GIFDRAW *p) {
  uint16_t *buf = uiRowBuf();  // alinhado: tftPixels não copia
  int y = p->iY + p->y + gifOffY;
  if (y < 0 || y >= 240) return;
  int x0 = p->iX + gifOffX;
  int w = p->iWidth;
  if (x0 + w > 240) w = 240 - x0;
  if (w <= 0) return;
  uint8_t *s = p->pPixels;
  uint16_t *pal = p->pPalette;
  if (p->ucDisposalMethod == 2) {  // restaurar fundo
    for (int i = 0; i < w; i++)
      if (s[i] == p->ucTransparent) s[i] = p->ucBackground;
    p->ucHasTransparency = 0;
  }
  tft.startWrite();
  if (p->ucHasTransparency) {
    uint8_t tr = p->ucTransparent;
    int x = 0;
    while (x < w) {
      while (x < w && s[x] == tr) x++;
      int start = x, n = 0;
      while (x < w && s[x] != tr) buf[n++] = pal[s[x++]];
      if (n) {
        tft.setAddrWindow(x0 + start, y, n, 1);
        tftPixels(buf, n);
      }
    }
  } else {
    for (int i = 0; i < w; i++) buf[i] = pal[s[i]];
    tft.setAddrWindow(x0, y, w, 1);
    tftPixels(buf, w);
  }
  tft.endWrite();
}

void stopAlbum() {
  if (gif) {
    gif->close();
    delete gif;
    gif = nullptr;
  }
  inAlbum = false;
  curFile = "";
  uiInvalidate();  // o relógio redesenha tudo ao voltar
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
    gif = new (std::nothrow) AnimatedGIF();  // ~24 KB contíguos de RAM
    if (!gif) {
      uiError(tr("Sem memoria p/ GIF", "No memory for GIF", "Sin memoria p/ GIF"));
      return true;  // mantém a tela de aviso até o próximo item
    }
    gif->begin(GIF_PALETTE_RGB565_LE);
    if (!gif->open(f.c_str(), mediaOpen, mediaClose, gifRead, gifSeek, gifDraw)) {
      gif->close();
      delete gif;
      gif = nullptr;
      uiError(tr("GIF invalido", "Invalid GIF", "GIF invalido"));
      return false;
    }
    gifOffX = max(0, (240 - gif->getCanvasWidth()) / 2);
    gifOffY = max(0, (240 - gif->getCanvasHeight()) / 2);
    nextFrameAt = 0;
  } else {
    JpgResult r = drawJpg(f);
    if (r == JPG_NOMEM) uiError(tr("Sem memoria p/ foto", "No memory for photo", "Sin memoria p/ foto"));  // o próximo item tenta de novo
    else if (r == JPG_BAD) uiError(tr("JPG invalido", "Invalid JPG", "JPG invalido"));
  }
  return true;
}

void albumLoop() {
  if (!gif || (int32_t)(millis() - nextFrameAt) < 0) return;
  int delayMs = 0;
  int r = gif->playFrame(false, &delayMs);
  if (r < 0) {  // erro: desiste deste GIF
    stopAlbum();
    return;
  }
  if (r == 0) gif->reset();  // último quadro: recomeça
  nextFrameAt = millis() + max(delayMs, 20);
}

// Decide o que mostrar quando não há Pomodoro: relógio ou um item do álbum.
void scheduleView() {
  uint32_t now = millis();
  if (viewUntil && (int32_t)(now - viewUntil) < 0) return;
  bool wantAlbum = !album.empty() && (cfg.mode == 1 || (cfg.mode == 2 && !inAlbum));
  if (wantAlbum) {
    const String &next = album[albumIdx++ % album.size()];
    // com um item só, não redesenha a mesma foto (evita piscar e reiniciar o GIF)
    if (!(inAlbum && next == curFile)) showAlbumItem(next);
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

// Lê o arquivo e carrega no motor. Em erro, msg recebe o motivo.
bool loadPackFile(const String &path, String &msg) {
  File f = fsOk ? LittleFS.open(path, "r") : File();
  if (!f) {
    msg = F("arquivo nao encontrado");
    return false;
  }
  size_t n = f.size();
  if (n == 0 || n > 16384) {
    f.close();
    msg = F("pacote vazio ou maior que 16 KB");
    return false;
  }
  char err[96];
  bool ok = facePackLoadFile(f, err, sizeof err);  // em passadas: o JSON não cabe inteiro na RAM
  f.close();
  if (!ok) msg = err;
  uiInvalidate();
  return ok;
}

bool loadPack(const String &id) {
  String msg;
  return idOk(id) && loadPackFile("/faces/" + id + ".json", msg);
}

// Volta o motor ao pacote salvo nas configurações (ou o libera, se o ativo é um embutido).
void restoreActivePack() {
  if (cfg.theme == THEME_PACK && loadPack(cfg.facePkg)) return;
  facePackUnload();
  if (cfg.theme == THEME_PACK) cfg.theme = THEME_MATRIX;
  uiInvalidate();
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
    packGate.begin(true, up.name == "face" ? 16384 : 65536);
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

void rotSet(const String &v) {
  String s;
  s.reserve(min((size_t)300, v.length()) + 1);
  for (char c : v)  // só o formato da lista: nada de aspas ou barras no JSON do estado
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == ',' || c == '-' || c == '_') s += c;
  if (s.length() > 300) s = s.substring(0, s.lastIndexOf(',', 300));
  rotList = s;
  rotNext = millis() + cfg.rotMin * 60000UL;
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
  if (!cfg.rotMin || !rotList.length() || phase != IDLE || previewActive() || inAlbum) return;
  if ((int32_t)(millis() - rotNext) < 0) return;
  rotNext = millis() + cfg.rotMin * 60000UL;
  // o item depois do mostrador atual (ou o primeiro, se o atual não está na lista)
  String cur = cfg.theme == THEME_PACK ? String("p:") + cfg.facePkg : String("t:") + cfg.theme, l = rotList + ",";
  int at = ("," + l).indexOf("," + cur + ","), start = at >= 0 ? at + cur.length() + 1 : 0;
  for (int tries = 0; tries < 32; tries++) {  // pula itens inválidos (pacote removido)
    if (start >= (int)l.length()) start = 0;
    int e = l.indexOf(',', start);
    String it = l.substring(start, e);
    start = e + 1;
    if (it.startsWith("t:")) {
      int t = it.substring(2).toInt();
      if (t < 0 || t >= THEME_COUNT) continue;
      cfg.theme = t;
      facePackUnload();
    } else if (it.startsWith("p:") && loadPack(it.substring(2))) {
      cfg.theme = THEME_PACK;
      strlcpy(cfg.facePkg, it.substring(2).c_str(), sizeof cfg.facePkg);
    } else continue;
    uiInvalidateSlide();  // mostrador novo entra rolando (ui.cpp)
    return;
  }
}
