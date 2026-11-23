// Fotos JPEG, GIFs animados, álbum, pacotes de mostradores e rotação entre mostradores.
#include "../app.h"
#include "../src/JPEGDEC/JPEGDEC.h"
#include "../src/AnimatedGIF/AnimatedGIF.h"
#include "../gifcheck.h"
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

std::vector<String> album;

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

// O pacote de mostrador ativo (até ~6,6 KB) disputa o heap com o GIF (~24 KB contíguos) e o JPEG (~19 KB + as
// tabelas). Só sai da RAM quando falta bloco contíguo para o item; volta sozinho em packEnsureLoaded(), quando o
// relógio é desenhado de novo. Sem pressão ele fica, para o modo "alternado" não reler o pacote a cada item.
static void albumMakeRoom(uint32_t need) {
  if (facePackLoaded() && ESP.getMaxFreeBlockSize() < need) facePackUnload();
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
    albumMakeRoom(sizeof(AnimatedGIF) + 1024);
    gif = new (std::nothrow) AnimatedGIF();  // ~24 KB contíguos de RAM
    if (!gif) {
      diagNoMem(1);
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
    nextFrameAt = millis();  // não 0: com mais de 24,8 dias ligado, (int32_t)(millis() - 0) é negativo e o GIF nunca tocava
  } else {
    diagBlkSample();  // minblk: o maior bloco antes da foto (e antes de albumMakeRoom soltar o pacote)
    albumMakeRoom(sizeof(JPEGDEC) + 1024 + 8192);  // o decodificador e, depois, as tabelas de ~7,7 KB
    JpgResult r = drawJpg(f);
    if (r == JPG_NOMEM) {
      diagNoMem(0);
      uiError(tr("Sem memoria p/ foto", "No memory for photo", "Sin memoria p/ foto"));  // o próximo item tenta de novo
    } else if (r == JPG_BAD) uiError(tr("JPG invalido", "Invalid JPG", "JPG invalido"));
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
// "Trocar de mostrador". Calculado a cada volta, sem guardar: mudar o intervalo, a lista ou os tempos vale na hora, e
// o tempo é o do item que ESTÁ na tela, não o do próximo.
static uint32_t rotEspera() {
  char cur[32];
  rotChave(cur, sizeof cur);
  uint16_t padrao = (uint16_t)cfg.rotMin * 60;
  return (uint32_t)(rodNaLista(rotList.c_str(), cur) ? rodSegundos(rotTimes.c_str(), cur, padrao) : padrao) * 1000UL;
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
}
