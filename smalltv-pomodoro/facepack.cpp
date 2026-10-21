// facepack.cpp — motor dos mostradores instaláveis (pacotes JSON). Formato: docs/PACOTES.md
//
// Ao carregar, o JSON vira estruturas compactas na RAM (≈40 bytes por camada + sprites em
// 4 bits por pixel) e é descartado. A cada quadro, cada camada calcula uma "assinatura" do
// que mostraria (texto resolvido, ângulo, largura da barra...). Só as camadas cuja
// assinatura mudou marcam áreas sujas; essas áreas são repintadas com recorte (uiClip),
// redesenhando por cima todas as camadas que as tocam, na ordem — sem piscar a tela toda.

#include "facepack.h"
#include "ui_kit.h"
#include <ArduinoJson.h>
#include <stdlib.h>

bool (*uiDrawImage)(const char *path, int x, int y) = nullptr;

enum : uint8_t { L_RECT, L_CIRCLE, L_LINE, L_TEXT, L_SPRITE, L_MASCOT, L_WEATHER, L_BAR, L_HAND, L_IMAGE, L_ARC, L_TICKS, L_PLANT, L_TYPES };
enum : uint8_t { F_PIXEL, F_DM, F_GEO, F_DSEG, F_BEBAS, F_OUTFIT, F_RIGHTEOUS, F_FONTS };
enum : uint8_t { A_LEFT, A_CENTER, A_RIGHT };
enum : uint8_t { W_ALWAYS, W_DAY, W_NIGHT, W_RAIN, W_DRY, W_WX, W_NOWX, W_GOAL, W_NOGOAL, W_STREAK, W_WHENS };
enum : uint8_t { V_SEC, V_MIN, V_HOUR, V_FOCOS, V_HUM, V_VARS };
static const char *const TYPES[L_TYPES] = {"rect", "circle", "line", "text", "sprite", "mascot", "weather", "bar", "hand", "image", "arc", "ticks", "plant"};
static const char *const FONTS[F_FONTS] = {"pixel", "dm", "geo", "dseg", "bebas", "outfit", "righteous"};
static const char *const ALIGNS[3] = {"left", "center", "right"};
static const char *const WHENS[W_WHENS] = {"always", "day", "night", "rain", "dry", "weather", "noweather", "goal", "nogoal", "streak"};
static const char *const VARS[V_VARS] = {"sec", "min", "hour", "focos", "hum"};
static const char *const MOODS[9] = {"feliz", "dormindo", "focado", "relax", "comemorando", "guarda-chuva", "cachecol", "oculos", "auto"};
static const int32_t C_NONE = -1, C_ACCENT = -2;
static const uint8_t MAX_LAYERS = 48, MAX_SPRITES = 12;

struct Layer {
  uint8_t type, font, align, when, var, size;
  int16_t x, y, w, h, r, mx;
  int32_t c1, c2;
  uint16_t str;            // texto/arquivo (posição no pool) ou sprite (posição em spr)
  int16_t bx, by, bw, bh;  // caixa da última vez que foi desenhada
  uint32_t sig;            // 0 = invisível
  uint8_t tr;              // texto com uma versão por idioma (pt, en, es em sequência no pool)
};
struct Box { int16_t x, y, w, h; };

static Layer *lay = nullptr;
static uint8_t nLay = 0;
static char *pool = nullptr;
static uint16_t poolUsed = 0, poolCap = 0;
static uint8_t *spr = nullptr;
static uint16_t sprUsed = 0, sprCap = 0;
static uint16_t bgColor = 0;
static char pkId[24], pkName[32], pkDesc[64], pkAuthor[32];

bool facePackLoaded() { return lay != nullptr; }
const char *facePackId() { return pkId; }
const char *facePackName() { return pkName; }
const char *facePackDesc() { return pkDesc; }
const char *facePackAuthor() { return pkAuthor; }

void facePackUnload() {
  free(lay);
  free(pool);
  free(spr);
  lay = nullptr;
  pool = nullptr;
  spr = nullptr;
  nLay = 0;
  poolUsed = poolCap = sprUsed = sprCap = 0;
  pkId[0] = pkName[0] = pkDesc[0] = pkAuthor[0] = 0;
}

// ---------- leitura ----------
static uint8_t pick(const char *s, const char *const *list, uint8_t n, uint8_t def) {
  if (!s) return def;
  for (uint8_t i = 0; i < n; i++)
    if (!strcmp(s, list[i])) return i;
  return 255;
}

static int32_t parseColor(const char *s, int32_t def) {
  if (!s) return def;
  if (s[0] == '#' && strlen(s) == 7) {
    long c = strtol(s + 1, nullptr, 16);
    return uiRgb(c >> 16, (c >> 8) & 255, c & 255);
  }
  struct Named { const char *n; int32_t c; };
  static const Named NAMES[] = {{"accent", C_ACCENT}, {"none", C_NONE}, {"bg", BG}, {"card", CARD}, {"card2", CARD2},
                                {"text", TEXT}, {"muted", MUTED}, {"dim", DIM}, {"work", WORK}, {"short", SHORT},
                                {"long", LONG}, {"sun", SUN}, {"cloud", CLOUD}, {"rain", RAIN}};
  for (const Named &n : NAMES)
    if (!strcmp(n.n, s)) return n.c;
  return def;
}

static uint16_t addStr(const char *s) {
  uint16_t off = poolUsed;
  size_t n = strlen(s);
  if (poolUsed + n + 1 > poolCap) return 0xFFFF;
  memcpy(pool + poolUsed, s, n + 1);
  poolUsed += n + 1;
  return off;
}

// id seguro: letras minúsculas, números, '-' e '_'
static void safeId(const char *s, char *out, size_t n) {
  size_t o = 0;
  for (; s && *s && o + 1 < n; s++) {
    char c = *s;
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out[o++] = c;
    else if (c == ' ' && o && out[o - 1] != '-') out[o++] = '-';
  }
  out[o] = 0;
}

bool facePackLoad(char *json, size_t len, char *err, size_t errLen) {
  facePackUnload();
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json, len);
  if (e) {
    snprintf(err, errLen, "JSON invalido: %s", e.c_str());
    return false;
  }
  if ((doc["format"] | 0) != 1) {
    snprintf(err, errLen, "formato desconhecido (use \"format\": 1)");
    return false;
  }
  JsonArrayConst layers = doc["layers"];
  if (layers.isNull() || layers.size() == 0 || layers.size() > MAX_LAYERS) {
    snprintf(err, errLen, "\"layers\" precisa ter de 1 a %d camadas", MAX_LAYERS);
    return false;
  }
  // tamanhos para alocar de uma vez
  size_t pcap = 1;
  for (JsonObjectConst l : layers) {
    const char *t = l["text"] | (const char *)nullptr, *f = l["file"] | (const char *)nullptr;
    pcap += (t ? strlen(t) + 1 : 0) + (f ? strlen(f) + 1 : 0);
    if (l["text"].is<JsonObjectConst>())  // {"pt": ..., "en": ..., "es": ...}
      for (const char *k : {"pt", "en", "es"}) pcap += strlen(l["text"][k] | (l["text"]["pt"] | "")) + 1;
  }
  JsonObjectConst sprites = doc["sprites"];
  size_t scap = 0;
  uint8_t ns = 0;
  for (JsonPairConst kv : sprites) {
    JsonArrayConst px = kv.value()["px"];
    size_t w = px.size() ? strlen(px[0] | "") : 0, h = px.size();
    if (!w || w > 32 || h > 32 || ++ns > MAX_SPRITES) {
      snprintf(err, errLen, "sprite \"%s\": ate 32x32 e no maximo %d sprites", kv.key().c_str(), MAX_SPRITES);
      return false;
    }
    scap += 3 + 15 * 2 + (w * h + 1) / 2;
  }
  lay = (Layer *)calloc(layers.size(), sizeof(Layer));
  pool = (char *)malloc(pcap);
  spr = scap ? (uint8_t *)malloc(scap) : nullptr;
  if (!lay || !pool || (scap && !spr)) {
    facePackUnload();
    snprintf(err, errLen, "sem memoria para o pacote");
    return false;
  }
  poolCap = pcap;
  sprCap = scap;
  pool[0] = 0;
  poolUsed = 1;  // posição 0 = texto vazio

  // sprites: [w][h][n][paleta n x RGB565][pixels 4 bits; 0 = transparente]
  const char *sprName[MAX_SPRITES];
  uint16_t sprOff[MAX_SPRITES];
  ns = 0;
  for (JsonPairConst kv : sprites) {
    JsonObjectConst palObj = kv.value()["pal"];
    JsonArrayConst px = kv.value()["px"];
    uint8_t w = strlen(px[0] | ""), h = px.size(), n = 0;
    char keys[16];
    uint16_t colors[15];
    for (JsonPairConst c : palObj) {
      if (n == 15 || strlen(c.key().c_str()) != 1) {
        snprintf(err, errLen, "sprite \"%s\": paleta com ate 15 cores de 1 caractere", kv.key().c_str());
        facePackUnload();
        return false;
      }
      keys[n] = c.key().c_str()[0];
      int32_t col = parseColor(c.value().as<const char *>(), 0);
      colors[n++] = col < 0 ? 0 : col;
    }
    uint8_t *p = spr + sprUsed;
    sprName[ns] = kv.key().c_str();
    sprOff[ns++] = sprUsed;
    p[0] = w;
    p[1] = h;
    p[2] = n;
    for (uint8_t i = 0; i < n; i++) {
      p[3 + i * 2] = colors[i] >> 8;
      p[4 + i * 2] = colors[i] & 255;
    }
    uint8_t *pix = p + 3 + n * 2;
    memset(pix, 0, (w * h + 1) / 2);
    for (uint8_t r = 0; r < h; r++) {
      const char *row = px[r] | "";
      for (uint8_t c = 0; c < w && row[c]; c++) {
        uint8_t v = 0;
        for (uint8_t i = 0; i < n; i++)
          if (keys[i] == row[c]) v = i + 1;
        int idx = r * w + c;
        pix[idx / 2] |= idx & 1 ? v : v << 4;
      }
    }
    sprUsed += 3 + n * 2 + (w * h + 1) / 2;
  }

  // camadas
  for (JsonObjectConst l : layers) {
    Layer &L = lay[nLay];
    L.type = pick(l["type"] | (const char *)nullptr, TYPES, L_TYPES, 255);
    if (L.type == 255) {
      snprintf(err, errLen, "camada %d: tipo desconhecido", nLay + 1);
      facePackUnload();
      return false;
    }
    L.x = l["x"] | 0;
    L.y = l["y"] | 0;
    L.w = l["w"] | 0;
    L.h = l["h"] | 0;
    L.r = l["r"] | 0;
    L.mx = l["max"] | 0;
    if (L.type == L_LINE) {
      L.w = l["x2"] | 0;
      L.h = l["y2"] | 0;
    } else if (L.type == L_HAND) {
      L.w = l["len"] | 60;
      L.h = l["tail"] | 0;
    } else if (L.type == L_TICKS) {
      L.w = l["len"] | 8;
      L.mx = l["count"] | 12;
    }
    uint8_t defSize = L.type == L_TEXT ? 2 : L.type == L_WEATHER ? 10 : L.type == L_ARC ? 10 : L.type == L_LINE || L.type == L_HAND || L.type == L_TICKS ? 3 : 1;
    L.size = l["size"] | defSize;
    L.font = pick(l["font"] | (const char *)nullptr, FONTS, F_FONTS, F_PIXEL);
    L.align = pick(l["align"] | (const char *)nullptr, ALIGNS, 3, A_LEFT);
    L.when = pick(l["when"] | (const char *)nullptr, WHENS, W_WHENS, W_ALWAYS);
    L.var = L.type == L_MASCOT ? pick(l["mood"] | (const char *)nullptr, MOODS, 9, 8)
                               : pick(l["value"] | (const char *)nullptr, VARS, V_VARS, V_SEC);
    if (L.font == 255 || L.align == 255 || L.when == 255 || L.var == 255) {
      snprintf(err, errLen, "camada %d: font/align/when/value/mood invalido", nLay + 1);
      facePackUnload();
      return false;
    }
    int32_t defC1 = L.type == L_RECT || L.type == L_CIRCLE ? CARD : L.type == L_BAR || L.type == L_ARC ? WORK : TEXT;
    L.c1 = parseColor(l["color"] | (const char *)nullptr, defC1);
    const char *c2 = l["color2"] | (const char *)nullptr;  // aceita também os apelidos abaixo
    if (!c2) c2 = l["track"] | (const char *)nullptr;
    if (!c2) c2 = l["off"] | (const char *)nullptr;
    if (!c2) c2 = l["stroke"] | (const char *)nullptr;
    if (!c2) c2 = l["bg"] | (const char *)nullptr;
    L.c2 = parseColor(c2, C_NONE);
    if (L.type == L_TEXT && l["text"].is<JsonObjectConst>()) {  // um texto por idioma, em sequência no pool
      JsonObjectConst tx = l["text"];
      const char *pt = tx["pt"] | "";
      L.str = addStr(pt);
      addStr(tx["en"] | pt);
      addStr(tx["es"] | pt);
      L.tr = 1;
    } else if (L.type == L_TEXT) L.str = addStr(l["text"] | "");
    else if (L.type == L_IMAGE) L.str = addStr(l["file"] | "");
    else if (L.type == L_SPRITE) {
      const char *sn = l["sprite"] | "";
      L.str = 0xFFFF;
      for (uint8_t i = 0; i < ns; i++)
        if (!strcmp(sprName[i], sn)) L.str = sprOff[i];
      if (L.str == 0xFFFF) {
        snprintf(err, errLen, "camada %d: sprite \"%s\" nao existe", nLay + 1, sn);
        facePackUnload();
        return false;
      }
    }
    nLay++;
  }
  bgColor = (uint16_t)parseColor(doc["bg"] | (const char *)nullptr, BG);
  snprintf(pkName, sizeof pkName, "%s", doc["name"] | "Sem nome");
  snprintf(pkDesc, sizeof pkDesc, "%s", doc["desc"] | "");
  snprintf(pkAuthor, sizeof pkAuthor, "%s", doc["author"] | "");
  for (char *t : {pkName, pkDesc, pkAuthor})  // uma linha só (vão para o .txt de metadados)
    for (; *t; t++)
      if (*t == '\n' || *t == '\r') *t = ' ';
  safeId(doc["id"] | pkName, pkId, sizeof pkId);
  if (!pkId[0]) strcpy(pkId, "pacote");
  return true;
}

// texto da camada no idioma da tela
static const char *layerText(const Layer &L) {
  const char *p = pool + L.str;
  if (L.tr)
    for (int i = 0; i < uiLang; i++) p += strlen(p) + 1;
  return p;
}

// ---------- variáveis e condições ----------
static void variable(const char *k, const UiState &s, char *v, size_t n) {
  v[0] = 0;
  bool t = s.timeOk, wx = s.weatherOn && s.wx.ok;
  const struct tm &m = s.now;
  if (!strcmp(k, "HH")) t ? snprintf(v, n, "%02d", displayHour(s)) : snprintf(v, n, "--");
  else if (!strcmp(k, "H")) t ? snprintf(v, n, "%d", displayHour(s)) : snprintf(v, n, "-");
  else if (!strcmp(k, "MM")) t ? snprintf(v, n, "%02d", m.tm_min) : snprintf(v, n, "--");
  else if (!strcmp(k, "SS")) t ? snprintf(v, n, "%02d", m.tm_sec) : snprintf(v, n, "--");
  else if (!strcmp(k, "WD")) snprintf(v, n, "%s", t ? WD[m.tm_wday] : "");
  else if (!strcmp(k, "DD")) t ? snprintf(v, n, "%02d", m.tm_mday) : snprintf(v, n, "--");
  else if (!strcmp(k, "MON")) snprintf(v, n, "%s", t ? MO[m.tm_mon] : "");
  else if (!strcmp(k, "MO")) t ? snprintf(v, n, "%02d", m.tm_mon + 1) : snprintf(v, n, "--");
  else if (!strcmp(k, "YYYY")) t ? snprintf(v, n, "%d", m.tm_year + 1900) : snprintf(v, n, "----");
  else if (!strcmp(k, "AMPM")) snprintf(v, n, "%s", t && m.tm_hour >= 12 ? "PM" : "AM");
  else if (!strcmp(k, "TEMP")) wx ? snprintf(v, n, "%d", (int)lroundf(s.wx.t)) : snprintf(v, n, "--");
  else if (!strcmp(k, "TMIN")) wx ? snprintf(v, n, "%d", (int)lroundf(s.wx.tmin)) : snprintf(v, n, "--");
  else if (!strcmp(k, "TMAX")) wx ? snprintf(v, n, "%d", (int)lroundf(s.wx.tmax)) : snprintf(v, n, "--");
  else if (!strcmp(k, "HUM")) wx ? snprintf(v, n, "%d", s.wx.hum) : snprintf(v, n, "--");
  else if (!strcmp(k, "WX")) snprintf(v, n, "%s", wx ? weatherText(s.wx.code) : "");
  else if (!strcmp(k, "CITY")) snprintf(v, n, "%s", s.city ? s.city : "");
  else if (!strcmp(k, "FOCOS")) snprintf(v, n, "%u", s.today);
  else if (!strcmp(k, "META")) snprintf(v, n, "%u", s.dailyGoal);
  else if (!strcmp(k, "STREAK")) snprintf(v, n, "%u", s.streak);
  else if (!strcmp(k, "BEST")) snprintf(v, n, "%u", s.best);
  else if (!strcmp(k, "IP")) snprintf(v, n, "%s", s.ip ? s.ip : "");
}

static void resolve(const char *tpl, const UiState &s, char *out, size_t n) {
  size_t o = 0;
  for (const char *p = tpl; *p && o + 1 < n;) {
    const char *e = *p == '{' ? strchr(p, '}') : nullptr;
    if (e && e - p - 1 < 8) {
      char key[8], v[32];
      memcpy(key, p + 1, e - p - 1);
      key[e - p - 1] = 0;
      variable(key, s, v, sizeof v);
      for (const char *q = v; *q && o + 1 < n;) out[o++] = *q++;
      p = e + 1;
    } else out[o++] = *p++;
  }
  out[o] = 0;
}

static bool isRain(const UiState &s) {
  int c = s.wx.code;
  return s.weatherOn && s.wx.ok && ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95);
}

static bool isDay(const UiState &s) {
  if (s.weatherOn && s.wx.ok) return s.wx.day;
  return !s.timeOk || (s.now.tm_hour >= 6 && s.now.tm_hour < 18);
}

static bool cond(uint8_t w, const UiState &s) {
  bool wx = s.weatherOn && s.wx.ok;
  switch (w) {
    case W_DAY: return isDay(s);
    case W_NIGHT: return !isDay(s);
    case W_RAIN: return isRain(s);
    case W_DRY: return !isRain(s);
    case W_WX: return wx;
    case W_NOWX: return !wx;
    case W_GOAL: return s.today >= s.dailyGoal;
    case W_NOGOAL: return s.today < s.dailyGoal;
    case W_STREAK: return s.streak > 0;
  }
  return true;
}

// Humor automático do tomate: dorme à noite, guarda-chuva na chuva, comemora a meta...
static uint8_t autoMood(const UiState &s) {
  if (!isDay(s)) return FACE_SLEEP;
  if (isRain(s)) return FACE_UMBRELLA;
  if (s.today >= s.dailyGoal && s.today) return FACE_CHEER;
  if (s.weatherOn && s.wx.ok && s.wx.t < 15) return FACE_SCARF;
  if (s.weatherOn && s.wx.ok && s.wx.t > 28) return FACE_SHADES;
  return FACE_HAPPY;
}

static int value(const Layer &L, const UiState &s, int &mx) {
  switch (L.var) {
    case V_MIN: mx = 60; return s.timeOk ? s.now.tm_min : 0;
    case V_HOUR: mx = 24; return s.timeOk ? s.now.tm_hour : 0;
    case V_FOCOS: mx = s.dailyGoal ? s.dailyGoal : 1; return s.today;
    case V_HUM: mx = 100; return s.weatherOn && s.wx.ok ? s.wx.hum : 0;
  }
  mx = 60;
  return s.timeOk ? s.now.tm_sec : 0;
}

static float handAngle(const Layer &L, const UiState &s) {
  if (!s.timeOk) return 0;
  const struct tm &m = s.now;
  if (L.var == V_HOUR) return ((m.tm_hour % 12) + m.tm_min / 60.0f) * K_PI / 6;
  if (L.var == V_MIN) return m.tm_min * K_PI / 30;
  return m.tm_sec * K_PI / 30;
}

static uint16_t col(int32_t c, const UiState &s) { return c == C_ACCENT ? s.accent : (uint16_t)c; }

// ---------- texto nas 7 fontes ----------
// Percorre o texto; desenha se draw = true. Devolve a largura.
static int runText(const Layer &L, const char *str, int x, int y, bool draw, uint16_t fg, int32_t c2) {
  const int sz = L.size, x0 = x;
  for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
    unsigned char ch = *p;
    if (ch == 0xC2 && p[1] == 0xB0) {  // "°"
      p++;
      if (draw) degree(x, y, L.font == F_PIXEL ? sz : (sz > 20 ? sz / 10 : 1), fg);
      x += L.font == F_PIXEL ? 4 * sz : sz / 3 + 4;
      continue;
    }
    if (ch == 0xC3 && p[1]) {  // letras acentuadas: só a letra base (a fonte embutida é ASCII)
      unsigned char c = *++p;
      ch = c <= 0x85 ? 'A' : c == 0x87 ? 'C' : c <= 0x8B ? 'E' : c <= 0x8F ? 'I' : c <= 0x96 ? 'O' : c <= 0x9C ? 'U'
         : c <= 0xA5 ? 'a' : c == 0xA7 ? 'c' : c <= 0xAB ? 'e' : c <= 0xAF ? 'i' : c <= 0xB6 ? 'o' : 'u';
    } else if (ch >= 0x80) continue;
    switch (L.font) {
      case F_PIXEL:
        if (draw) G->drawChar(x, y, ch, fg, fg, sz);
        x += 6 * sz;
        break;
      case F_DM: {
        const int p = sz;
        if (ch >= '0' && ch <= '9' || ch == '-') {
          if (draw) dmDigit(x, y, ch == '-' ? DM_MINUS : ch - '0', p, fg, c2 == C_NONE ? fg : (uint16_t)c2, c2 != C_NONE);
          x += 6 * p;
        } else if (ch == ':' || ch == '.') {
          if (draw) {
            if (ch == ':') G->fillRect(x + p, y + 2 * p, p, p, fg);
            G->fillRect(x + p, y + (ch == ':' ? 4 : 6) * p, p, p, fg);
          }
          x += 3 * p;
        } else x += 3 * p;
        break;
      }
      case F_GEO: {
        const float s10 = sz / 10.0f;
        const int sw = L.w > 0 ? L.w : (sz / 10 > 2 ? sz / 10 : 2);
        if (ch >= '0' && ch <= '9') {
          if (draw) geoDigit(x, y, ch - '0', s10, sw, fg);
          x += (int)(7.5f * s10);
        } else if (ch == ':') {
          if (draw) {
            G->fillCircle(x + 1.5f * s10, y + 3 * s10, sw / 2 + 1, fg);
            G->fillCircle(x + 1.5f * s10, y + 7 * s10, sw / 2 + 1, fg);
          }
          x += (int)(3 * s10);
        } else x += (int)(3 * s10);
        break;
      }
      case F_DSEG: {
        const int dw = sz * 618 / 1000, adv = dw + sz / 10;
        if (ch >= '0' && ch <= '9') {
          if (draw) dsegDigit(x, y, sz, ch - '0', fg, c2 == C_NONE ? fg : (uint16_t)c2);  // off == on: só os acesos
          x += adv;
        } else if (ch == ':') {
          if (draw) {
            G->fillCircle(x + sz / 8, y + sz * 3 / 10, sz / 22 + 1, fg);
            G->fillCircle(x + sz / 8, y + sz * 7 / 10, sz / 22 + 1, fg);
          }
          x += sz / 4;
        } else x += adv;
        break;
      }
      case F_BEBAS:
      case F_OUTFIT:
      case F_RIGHTEOUS: {
        const VecFont &vf = L.font == F_BEBAS ? VF_BEBAS : L.font == F_OUTFIT ? VF_OUTFIT : VF_RIGHTEOUS;
        char one[2] = {(char)ch, 0};
        int w = vecWidth(vf, one, sz);
        if (draw && (ch == ':' || (ch >= '0' && ch <= '9'))) vecText(vf, x, y, sz, one, fg, c2 == C_NONE ? bgColor : (uint16_t)c2);
        x += w;
        break;
      }
    }
  }
  return x - x0;
}

static int fontHeight(const Layer &L) {
  switch (L.font) {
    case F_PIXEL: return 8 * L.size;
    case F_DM: return 7 * L.size;
    default: return L.size;
  }
}

// ---------- caixa, assinatura e desenho de cada camada ----------
static uint32_t fnv(uint32_t h, const void *data, size_t n) {
  const uint8_t *b = (const uint8_t *)data;
  while (n--) h = (h ^ *b++) * 16777619u;
  return h;
}

static Box handBox(const Layer &L, float a) {
  float sa = sinf(a), ca = cosf(a);
  int x0 = L.x - sa * L.h, y0 = L.y + ca * L.h, x1 = L.x + sa * L.w, y1 = L.y - ca * L.w;
  int p = L.size / 2 + 2;
  int bx = (x0 < x1 ? x0 : x1) - p, by = (y0 < y1 ? y0 : y1) - p;
  return {(int16_t)bx, (int16_t)by, (int16_t)(abs(x1 - x0) + 2 * p + 1), (int16_t)(abs(y1 - y0) + 2 * p + 1)};
}

static Box measure(const Layer &L, const UiState &s, const char *txt) {
  switch (L.type) {
    case L_RECT: case L_IMAGE: return {L.x, L.y, L.w, L.h};
    case L_CIRCLE: return {(int16_t)(L.x - L.r - 1), (int16_t)(L.y - L.r - 1), (int16_t)(2 * L.r + 3), (int16_t)(2 * L.r + 3)};
    case L_LINE: {
      int p = L.size / 2 + 2;
      return {(int16_t)((L.x < L.w ? L.x : L.w) - p), (int16_t)((L.y < L.h ? L.y : L.h) - p), (int16_t)(abs(L.w - L.x) + 2 * p + 1), (int16_t)(abs(L.h - L.y) + 2 * p + 1)};
    }
    case L_TEXT: {
      int w = runText(L, txt, 0, 0, false, 0, C_NONE), h = fontHeight(L);
      int x = L.align == A_CENTER ? L.x - w / 2 : L.align == A_RIGHT ? L.x - w : L.x;
      int pad = L.font == F_GEO ? (L.w > 0 ? L.w : L.size / 10 + 2) : 2;
      return {(int16_t)(x - pad), (int16_t)(L.y - pad), (int16_t)(w + 2 * pad), (int16_t)(h + 2 * pad)};
    }
    case L_SPRITE: return {L.x, L.y, (int16_t)(spr[L.str] * L.size), (int16_t)(spr[L.str + 1] * L.size)};
    case L_MASCOT:
    case L_PLANT: return {L.x, L.y, (int16_t)(16 * L.size), (int16_t)(16 * L.size)};
    case L_WEATHER: {
      float sc = L.size / 10.0f;
      return {(int16_t)(L.x - 24 * sc), (int16_t)(L.y - 24 * sc), (int16_t)(48 * sc + 2), (int16_t)(52 * sc + 2)};
    }
    case L_BAR: return {L.x, L.y, L.w, L.h};
    case L_HAND: return handBox(L, handAngle(L, s));
    case L_ARC:
    case L_TICKS: return {(int16_t)(L.x - L.r - 2), (int16_t)(L.y - L.r - 2), (int16_t)(2 * L.r + 5), (int16_t)(2 * L.r + 5)};
  }
  return {0, 0, 0, 0};
}

static uint32_t signature(const Layer &L, const UiState &s, char *txt, size_t n) {
  uint32_t h = 2166136261u;
  txt[0] = 0;
  switch (L.type) {
    case L_TEXT: resolve(layerText(L), s, txt, n); h = fnv(h, txt, strlen(txt)); break;
    case L_MASCOT: { uint8_t m = L.var == 8 ? autoMood(s) : L.var; h = fnv(h, &m, 1); break; }
    case L_PLANT: { const UiSprite *p = plantSprite(s.streak); h = fnv(h, &p, sizeof p); break; }
    case L_WEATHER: { int v = s.weatherOn && s.wx.ok ? s.wx.code * 2 + s.wx.day + 1 : 0; h = fnv(h, &v, sizeof v); break; }
    case L_BAR: { int mx, v = value(L, s, mx); int fw = L.mx > 0 ? (v > L.mx ? L.mx : v) * L.w / L.mx : (v > mx ? mx : v) * L.w / mx; h = fnv(h, &fw, sizeof fw); break; }
    case L_HAND: { int a = (int)(handAngle(L, s) * 1000); h = fnv(h, &a, sizeof a); break; }
    case L_ARC: { int mx, v = value(L, s, mx); if (L.mx > 0) mx = L.mx; int q = (v > mx ? mx : v) * 720 / mx; h = fnv(h, &q, sizeof q); break; }
  }
  return h | 1;  // nunca 0 (0 = invisível)
}

// Anel de espessura th e raio externo r, preenchido de 0 a f (fração) a partir do topo, no sentido
// horário, com pontas redondas; o resto em "track" (C_NONE = não desenha). Linha a linha em trechos
// (sem atan2 por pixel: o lado do ponto em relação ao raio final decide).
static void arcRing(int cx, int cy, int r, int th, float f, uint16_t c, int32_t track, const UiState &s) {
  if (th > r) th = r;
  int ri = r - th;
  float a = f * 2 * K_PI;
  int ex = (int)(sinf(a) * 1024), ey = (int)(-cosf(a) * 1024);
  auto inArc = [&](int dx, int dy) {
    if (f >= 1) return true;
    if (f <= 0) return false;
    long cr = (long)ex * dy - (long)ey * dx;  // < 0: antes do raio final
    if (f <= 0.5f) return (dx > 0 || (dx == 0 && dy < 0)) && cr < 0;
    return !(dx < 0 && cr >= 0);
  };
  for (int dy = -r; dy <= r; dy++) {
    int xo = (int)sqrtf((float)r * r - dy * dy + r), xi = abs(dy) < ri ? (int)sqrtf((float)ri * ri - dy * dy - ri) : -1;
    for (int side = 0; side < 2; side++) {
      int x0 = side ? (xi >= 0 ? xi + 1 : -xo) : -xo, x1 = side ? xo : (xi >= 0 ? -xi - 1 : -xo - 1);
      for (int x = x0; x <= x1;) {
        bool in = inArc(x, dy);
        int e = x;
        while (e + 1 <= x1 && inArc(e + 1, dy) == in) e++;
        if (in) G->drawFastHLine(cx + x, cy + dy, e - x + 1, c);
        else if (track != C_NONE) G->drawFastHLine(cx + x, cy + dy, e - x + 1, col(track, s));
        x = e + 1;
      }
    }
  }
  if (f > 0 && f < 1 && th > 3) {  // pontas redondas
    float rm = r - th / 2.0f;
    G->fillCircle(cx, cy - (int)rm, th / 2, c);
    G->fillCircle(cx + (int)(sinf(a) * rm), cy - (int)(cosf(a) * rm), th / 2, c);
  }
}

static void drawLayer(const Layer &L, const UiState &s, const char *txt) {
  uint16_t c1 = col(L.c1, s);
  switch (L.type) {
    case L_RECT:
      if (L.c1 != C_NONE) L.r ? G->fillRoundRect(L.x, L.y, L.w, L.h, L.r, c1) : G->fillRect(L.x, L.y, L.w, L.h, c1);
      if (L.c2 != C_NONE) G->drawRoundRect(L.x, L.y, L.w, L.h, L.r, col(L.c2, s));
      break;
    case L_CIRCLE:
      if (L.c1 != C_NONE) G->fillCircle(L.x, L.y, L.r, c1);
      if (L.c2 != C_NONE) G->drawCircle(L.x, L.y, L.r, col(L.c2, s));
      break;
    case L_LINE: thickLine(L.x, L.y, L.w, L.h, L.size, c1); break;
    case L_TEXT: {
      int w = runText(L, txt, 0, 0, false, 0, C_NONE);
      int x = L.align == A_CENTER ? L.x - w / 2 : L.align == A_RIGHT ? L.x - w : L.x;
      runText(L, txt, x, L.y, true, c1, L.c2 == C_ACCENT ? s.accent : L.c2);
      break;
    }
    case L_SPRITE: {
      const uint8_t *p = spr + L.str;
      uint8_t w = p[0], h = p[1], n = p[2];
      const uint8_t *pix = p + 3 + n * 2;
      for (int r = 0; r < h; r++)
        for (int c = 0; c < w;) {
          int i = r * w + c;
          uint8_t v = i & 1 ? pix[i / 2] & 15 : pix[i / 2] >> 4, run = 1;
          while (c + run < w) {
            int k = i + run;
            if ((k & 1 ? pix[k / 2] & 15 : pix[k / 2] >> 4) != v) break;
            run++;
          }
          if (v) G->fillRect(L.x + c * L.size, L.y + r * L.size, run * L.size, L.size, (p[1 + v * 2] << 8) | p[2 + v * 2]);
          c += run;
        }
      break;
    }
    case L_MASCOT: mascot(L.x, L.y, L.size, L.var == 8 ? autoMood(s) : L.var); break;
    case L_PLANT: uiSprite(plantSprite(s.streak), L.x, L.y, L.size); break;
    case L_WEATHER:
      if (s.weatherOn && s.wx.ok) weatherIcon(L.x, L.y, L.size / 10.0f, s.wx.code, s.wx.day, L.c2 == C_NONE ? bgColor : col(L.c2, s));
      break;
    case L_BAR: {
      int mx, v = value(L, s, mx);
      if (L.mx > 0) mx = L.mx;
      if (v > mx) v = mx;
      int fw = v * L.w / mx;
      if (L.c2 != C_NONE) G->fillRoundRect(L.x, L.y, L.w, L.h, L.r, col(L.c2, s));
      if (fw > 0) G->fillRoundRect(L.x, L.y, fw < 2 * L.r ? 2 * L.r : fw, L.h, L.r, c1);
      break;
    }
    case L_HAND: {
      float a = handAngle(L, s), sa = sinf(a), ca = cosf(a);
      thickLine(L.x - sa * L.h, L.y + ca * L.h, L.x + sa * L.w, L.y - ca * L.w, L.size, c1);
      break;
    }
    case L_ARC: {
      int mx, v = value(L, s, mx);
      if (L.mx > 0) mx = L.mx;
      arcRing(L.x, L.y, L.r, L.size, (float)(v > mx ? mx : v) / mx, c1, L.c2, s);
      break;
    }
    case L_TICKS:
      for (int i = 0; i < L.mx; i++) {
        float a = i * 2 * K_PI / L.mx, sa = sinf(a), ca = cosf(a);
        thickLine(L.x + sa * (L.r - L.w), L.y - ca * (L.r - L.w), L.x + sa * L.r, L.y - ca * L.r, L.size, c1);
      }
      break;
    case L_IMAGE:
      if (!uiDrawImage || !uiDrawImage(pool + L.str, L.x, L.y)) G->fillRect(L.x, L.y, L.w, L.h, uiRgb(70, 70, 76));
      break;
  }
}

// ---------- áreas sujas ----------
static bool overlap(const Box &a, const Box &b, int gap = 0) {
  return a.w > 0 && b.w > 0 && a.x < b.x + b.w + gap && b.x < a.x + a.w + gap && a.y < b.y + b.h + gap && b.y < a.y + a.h + gap;
}
static Box unite(const Box &a, const Box &b) {
  int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
  int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return {(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}
static void addDirty(Box *d, int &n, Box b) {
  if (b.w <= 0 || b.h <= 0) return;
  for (int i = 0; i < n; i++)
    if (overlap(d[i], b, 4)) {  // junta áreas que se tocam
      d[i] = unite(d[i], b);
      return;
    }
  if (n < 6) d[n++] = b;
  else d[0] = unite(d[0], b);
}

void facePackRender(const UiState &s, bool full) {
  if (!lay) {
    if (full) textC(120, 112, 2, MUTED, tr("Pacote ausente", "Face missing", "Esfera ausente"));
    return;
  }
  char txt[48];
  Box dirty[6];
  int nd = 0;
  for (int i = 0; i < nLay; i++) {
    Layer &L = lay[i];
    bool vis = cond(L.when, s);
    uint32_t sig = vis ? signature(L, s, txt, sizeof txt) : 0;
    if (!full && sig == L.sig) continue;
    Box nb = vis ? measure(L, s, txt) : Box{0, 0, 0, 0};
    if (!full) {
      addDirty(dirty, nd, {L.bx, L.by, L.bw, L.bh});
      addDirty(dirty, nd, nb);
    }
    L.sig = sig;
    L.bx = nb.x;
    L.by = nb.y;
    L.bw = nb.w;
    L.bh = nb.h;
  }
  if (full) {
    G->fillScreen(bgColor);
    for (int i = 0; i < nLay; i++)
      if (lay[i].sig) {
        if (lay[i].type == L_TEXT) resolve(layerText(lay[i]), s, txt, sizeof txt);
        drawLayer(lay[i], s, txt);
      }
    return;
  }
  // cada área suja é desenhada fora da tela e vai pronta para ela (o ponteiro de segundos não pisca);
  // sem memória para a faixa, desenha direto com recorte, como antes
  struct Area { Box d; const UiState *s; char *txt; } area = {{0, 0, 0, 0}, &s, txt};
  auto paint = [](void *p) {
    Area &a = *(Area *)p;
    G->fillRect(a.d.x, a.d.y, a.d.w, a.d.h, bgColor);
    for (int i = 0; i < nLay; i++) {
      const Layer &L = lay[i];
      Box lb = {L.bx, L.by, L.bw, L.bh};
      if (!L.sig || !overlap(lb, a.d)) continue;
      if (L.type == L_TEXT) resolve(layerText(L), *a.s, a.txt, 48);
      drawLayer(L, *a.s, a.txt);
    }
  };
  for (int k = 0; k < nd; k++) {
    area.d = dirty[k];
    Box d = area.d;
    if (uiOffscreen(d.x, d.y, d.w, d.h, paint, &area)) continue;
    uiClipBegin(d.x, d.y, d.x + d.w, d.y + d.h);
    paint(&area);
    uiClipEnd();
  }
}
