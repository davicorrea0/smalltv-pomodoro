// facepack.cpp — motor dos mostradores instaláveis (pacotes JSON). Formato: docs/PACOTES.md
//
// Ao carregar, o JSON vira estruturas compactas na RAM (≈40 bytes por camada + sprites em 1, 2 ou
// 4 bits por pixel, só com o recorte visível de cada quadro) e é descartado. A cada quadro, cada camada
// calcula uma "assinatura" do que mostraria (texto resolvido, ângulo, largura da barra...). Só as camadas
// cuja assinatura mudou marcam áreas sujas; essas áreas são repintadas com recorte (uiClip), redesenhando
// por cima todas as camadas que as tocam, na ordem — sem piscar a tela toda.

#include "facepack.h"
#include "ui_kit.h"
#include "tunel_lut.h"
#include <ArduinoJson.h>
#include <stdlib.h>

bool (*uiDrawImage)(const char *path, int x, int y) = nullptr;

enum : uint8_t { L_RECT, L_CIRCLE, L_LINE, L_TEXT, L_SPRITE, L_MASCOT, L_WEATHER, L_BAR, L_HAND, L_IMAGE, L_ARC, L_TICKS, L_PLANT, L_WORLD3D, L_TYPES };
enum : uint8_t { F_PIXEL, F_DM, F_GEO, F_DSEG, F_BEBAS, F_OUTFIT, F_RIGHTEOUS, F_FONTS };
enum : uint8_t { A_LEFT, A_CENTER, A_RIGHT };
enum : uint8_t { W_ALWAYS, W_DAY, W_NIGHT, W_RAIN, W_DRY, W_WX, W_NOWX, W_GOAL, W_NOGOAL, W_STREAK, W_WHENS };
enum : uint8_t { V_SEC, V_MIN, V_HOUR, V_FOCOS, V_HUM, V_VARS };
static const char TYPES[] PROGMEM = "rect\0circle\0line\0text\0sprite\0mascot\0weather\0bar\0hand\0image\0arc\0ticks\0plant\0world3d";
static const char FONTS[] PROGMEM = "pixel\0dm\0geo\0dseg\0bebas\0outfit\0righteous";
static const char ALIGNS[] PROGMEM = "left\0center\0right";
static const char WHENS[] PROGMEM = "always\0day\0night\0rain\0dry\0weather\0noweather\0goal\0nogoal\0streak";
static const char VARS[] PROGMEM = "sec\0min\0hour\0focos\0hum";
static const char MOODS[] PROGMEM = "feliz\0dormindo\0focado\0relax\0comemorando\0guarda-chuva\0cachecol\0oculos\0auto";
// chaves das camadas lidas no formato 2 (filtro da carga, sem "text", "file" e "motion", lidos à parte); fora da função-modelo, senão o PROGMEM não vale
static const char LAYER_KEYS[] PROGMEM = "type\0x\0y\0w\0h\0r\0max\0x2\0y2\0len\0tail\0count\0size\0font\0align\0when\0mood\0value\0"
    "color\0color2\0track\0off\0stroke\0bg\0glow\0spread\0base\0pulse\0sprite\0overlay\0geometry\0shape\0fps\0"
    "speed\0sway\0spin\0colors\0accent\0map\0period\0camera\0";
static const int32_t C_NONE = -1, C_ACCENT = -2;
// Formato 2 (1.26.0): até 16 quadros e 8.192 B crus, "world3d", "motion" e "overlay". O formato 1 continua com 8 quadros
// e 6.144 B, e o firmware recusa no formato 1 o que só o 2 tem (a 1.25.0 recusaria o pacote com outra mensagem).
// Chaves e nomes novos do formato 2 vão para a flash (no ESP8266 o .rodata das strings ocupa RAM); o simulador não tem
// PROGMEM no ArduinoJson, então lá ficam como texto comum.
#ifdef ESP8266
#define JK(s) F(s)
#else
#define JK(s) s
#endif
static const uint8_t MAX_LAYERS = 48, MAX_SPRITES = 12, MAX_FRAMES = 16, V1_FRAMES = 8, MAX_MOTION = 12, MAX_TEXT = 48;
static const uint16_t MAX_SPR_BYTES = 8192, V1_SPR_BYTES = 6144, V1_FILE = 16384, MAX_POOL = 1536;  // arquivo: até 24 KB (album.cpp)

struct Layer {
  uint8_t type, font, align, when, var, size;
  uint8_t mot, ov;         // mot: movimento (posição em motPool, 0xFF = parado); ov: texto por cima da cena 3D
  int16_t x, y, w, h, r, mx;
  int32_t c1, c2;
  uint16_t str;            // texto/arquivo (posição no pool) ou sprite (posição em spr)
  int16_t bx, by, bw, bh;  // caixa da última vez que foi desenhada
  uint32_t sig;            // 0 = invisível
  uint8_t tr;              // texto com uma versão por idioma (pt, en, es em sequência no pool)
  uint8_t gl, gs;          // brilho: 1 = tem, e o alcance em px (1-3)
  uint16_t gc, gb, gp;     // brilho: cor, cor da superfície sob o halo (0xFFFF = fundo do pacote) e período do pulso em ms (0 = fixo)
};
struct Box { int16_t x, y, w, h; };
// mot e ov ocupam o enchimento que já existia antes de c1: 48 camadas não podem crescer
static_assert(sizeof(Layer) == 56, "Layer cresceu: cada byte conta 48 vezes no pacote carregado");

// Movimento de um sprite: posição absoluta em 2 a 6 pontos no tempo, em volta (o último ponto fecha o período).
// Fica num bloco à parte, só para as camadas que têm movimento (até MAX_MOTION).
struct MotionKey { uint16_t t; int16_t x, y; };
struct Motion { uint16_t period; uint8_t n; MotionKey k[6]; };
static Motion *motPool = nullptr;

// Cena 3D (camada "world3d", só como primeira camada): túnel (tabela polar na flash, tunel_lut.h) ou corredores num
// mapa 8x8 com câmera. O cache das 120 colunas do corredor vem no mesmo bloco, só para o corredor. Tudo é alocado na
// carga e liberado no facePackUnload (álbum e OTA também descarregam o pacote).
struct WorldKey { uint16_t t; int16_t x, y, yaw; };  // x, y em centésimos de célula; yaw em graus
struct World3D {
  uint8_t kind, fps, speed, sway, spin, keyCount, shape;  // kind: 0 túnel, 1 corredor; shape: 0 redondo, 1 quadrado
  uint16_t period, colors[4], accent;
  uint8_t cells[8];  // bit x da linha y = parede
  WorldKey camera[6];
};
struct GridCache {
  uint32_t tick;
  bool ready;
  int16_t top[120], bottom[120];
  uint16_t wall[120], mortar[120];
  uint8_t seam[120];
};
static World3D *world3d = nullptr;
static GridCache *gridCache() { return (GridCache *)(world3d + 1); }

static Layer *lay = nullptr;
static uint8_t nLay = 0;
static char *pool = nullptr;
static uint16_t poolUsed = 0, poolCap = 0;
static uint8_t *spr = nullptr;
static uint16_t sprUsed = 0, sprRaw = 0;  // bytes usados em spr; sprRaw = soma crua, só para o teto
static uint16_t bgColor = 0;
static char pkId[24], pkName[32], pkDesc[64], pkAuthor[32];

bool facePackLoaded() { return lay != nullptr; }
uint16_t facePackSpriteBytes() { return sprUsed; }
const char *facePackId() { return pkId; }
const char *facePackName() { return pkName; }
const char *facePackDesc() { return pkDesc; }
const char *facePackAuthor() { return pkAuthor; }

void facePackUnload() {
  free(world3d);
  free(motPool);
  world3d = nullptr;
  motPool = nullptr;
  free(lay);
  free(pool);
  free(spr);
  lay = nullptr;
  pool = nullptr;
  spr = nullptr;
  nLay = 0;
  poolUsed = poolCap = sprUsed = sprRaw = 0;
  pkId[0] = pkName[0] = pkDesc[0] = pkAuthor[0] = 0;
}

// ---------- leitura ----------
// `list`: nomes na flash, separados por \0 (ver TYPES, FONTS...)
static uint8_t pick(const char *s, PGM_P list, uint8_t n, uint8_t def) {
  if (!s) return def;
  for (uint8_t i = 0; i < n; i++, list += strlen_P(list) + 1)
    if (!strcmp_P(s, list)) return i;
  return 255;
}

static int32_t parseColor(const char *s, int32_t def) {
  if (!s) return def;
  if (s[0] == '#' && strlen(s) == 7) {
    long c = strtol(s + 1, nullptr, 16);
    return uiRgb(c >> 16, (c >> 8) & 255, c & 255);
  }
  static const char NAMES[] PROGMEM = "accent\0none\0bg\0card\0card2\0text\0muted\0dim\0work\0short\0long\0sun\0cloud\0rain";
  static const int32_t COLORS[] PROGMEM = {C_ACCENT, C_NONE, BG, CARD, CARD2, TEXT, MUTED, DIM, WORK, SHORT, LONG, SUN, CLOUD, RAIN};
  PGM_P n = NAMES;
  for (uint8_t i = 0; i < sizeof COLORS / sizeof COLORS[0]; i++, n += strlen_P(n) + 1)
    if (!strcmp_P(s, n)) return (int32_t)pgm_read_dword(&COLORS[i]);
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

// ---------- cena 3D e movimento: tudo conferido antes de alocar ----------
static bool worldColor(JsonVariantConst v, uint16_t &out) {
  const char *s = v.is<const char *>() ? v.as<const char *>() : nullptr;
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  for (int i = 1; i < 7; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return false;
  out = (uint16_t)parseColor(s, 0);
  return true;
}
static bool worldOpen(const World3D &w, int x, int y) {
  return x >= 100 && x < 700 && y >= 100 && y < 700 && !(w.cells[y / 100] & (1U << (x / 100)));
}
// O trecho reto da câmera entre dois pontos não pode tocar nenhuma célula de parede (nem pelo canto).
static bool worldSegmentClear(const World3D &w, int x0, int y0, int x1, int y1) {
  for (int cy = 0; cy < 8; cy++)
    for (int cx = 0; cx < 8; cx++) {
      if (!(w.cells[cy] & (1U << cx))) continue;
      float lo = 0, hi = 1;
      const int a[2] = {x0, y0}, d[2] = {x1 - x0, y1 - y0}, mn[2] = {cx * 100, cy * 100}, mxv[2] = {cx * 100 + 100, cy * 100 + 100};
      bool hit = true;
      for (int k = 0; k < 2 && hit; k++) {
        if (!d[k]) {
          if (a[k] < mn[k] || a[k] > mxv[k]) hit = false;
        } else {
          float t0 = (float)(mn[k] - a[k]) / d[k], t1 = (float)(mxv[k] - a[k]) / d[k];
          if (t0 > t1) { float t = t0; t0 = t1; t1 = t; }
          if (t0 > lo) lo = t0;
          if (t1 < hi) hi = t1;
          if (lo > hi) hit = false;
        }
      }
      if (hit) return false;
    }
  return true;
}
static bool loadWorld3D(JsonObjectConst l, char *err, size_t len) {
  World3D w = {};
  const char *kind = l[JK("geometry")] | "";
  if (!strcmp_P(kind, PSTR("tunnel"))) w.kind = 0;
  else if (!strcmp_P(kind, PSTR("grid"))) w.kind = 1;
  else { snprintf_P(err, len, PSTR("cena 3D: geometry deve ser tunnel ou grid")); return false; }
  const char *shape = l[JK("shape")] | "";  // padrão: redondo
  if (!strcmp_P(shape, PSTR("square"))) w.shape = 1;
  else if (*shape && strcmp_P(shape, PSTR("round"))) { snprintf_P(err, len, PSTR("cena 3D: shape deve ser round ou square")); return false; }
  int fps = l[JK("fps")] | 8, speed = l[JK("speed")] | 16, sway = l[JK("sway")] | 0, spin = l[JK("spin")] | 0;
  if (fps < 1 || fps > 10 || speed < 0 || speed > 32 || sway < 0 || sway > 8 || spin < 0 || spin > 8) {
    snprintf_P(err, len, PSTR("cena 3D: fps 1-10, speed 0-32, sway e spin 0-8"));
    return false;
  }
  w.fps = fps; w.speed = speed; w.sway = sway; w.spin = spin;
  JsonArrayConst colors = l[JK("colors")];
  if (colors.size() != 4) { snprintf_P(err, len, PSTR("cena 3D: \"colors\" precisa de 4 cores")); return false; }
  for (int i = 0; i < 4; i++)
    if (!worldColor(colors[i], w.colors[i])) { snprintf_P(err, len, PSTR("cena 3D: cor invalida (use #rrggbb)")); return false; }
  if (!worldColor(l[JK("accent")], w.accent)) { snprintf_P(err, len, PSTR("cena 3D: \"accent\" invalido (use #rrggbb)")); return false; }
  if (w.kind == 1) {
    JsonArrayConst map = l[JK("map")], keys = l[JK("camera")];
    int period = l[JK("period")] | 0;
    if (map.size() != 8 || keys.size() < 2 || keys.size() > 6 || period < 1000 || period > 60000) {
      snprintf_P(err, len, PSTR("cena 3D: mapa 8x8, 2 a 6 pontos de camera e period 1000-60000"));
      return false;
    }
    for (int y = 0; y < 8; y++) {
      const char *row = map[y] | "";
      if (strlen(row) != 8) { snprintf_P(err, len, PSTR("cena 3D: mapa deve ter 8x8")); return false; }
      for (int x = 0; x < 8; x++) {
        char c = row[x];
        if ((c != '#' && c != '.') || ((x == 0 || x == 7 || y == 0 || y == 7) && c != '#')) {
          snprintf_P(err, len, PSTR("cena 3D: mapa de # e . com a borda fechada"));
          return false;
        }
        if (c == '#') w.cells[y] |= 1U << x;
      }
    }
    w.period = period;
    for (JsonObjectConst key : keys) {
      if (!key[JK("t")].is<int>() || !key[JK("x")].is<int>() || !key[JK("y")].is<int>() || !key[JK("yaw")].is<int>()) {
        snprintf_P(err, len, PSTR("cena 3D: ponto da camera precisa de t, x, y e yaw"));
        return false;
      }
      int t = key[JK("t")], x = key[JK("x")], y = key[JK("y")], yaw = key[JK("yaw")];
      if (t < 0 || t > period || yaw < -720 || yaw > 720 || !worldOpen(w, x, y) || (!w.keyCount && t != 0) ||
          (w.keyCount && t <= w.camera[w.keyCount - 1].t)) {
        snprintf_P(err, len, PSTR("cena 3D: camera fora do mapa ou tempos fora de ordem"));
        return false;
      }
      if (w.keyCount && !worldSegmentClear(w, w.camera[w.keyCount - 1].x, w.camera[w.keyCount - 1].y, x, y)) {
        snprintf_P(err, len, PSTR("cena 3D: o caminho da camera cruza uma parede"));
        return false;
      }
      w.camera[w.keyCount++] = {(uint16_t)t, (int16_t)x, (int16_t)y, (int16_t)yaw};
    }
    const WorldKey &a = w.camera[0], &b = w.camera[w.keyCount - 1];
    if (b.t != period || a.x != b.x || a.y != b.y || (b.yaw - a.yaw) % 360) {
      snprintf_P(err, len, PSTR("cena 3D: o ultimo ponto (t = period) volta ao primeiro"));
      return false;
    }
  }
  world3d = (World3D *)malloc(sizeof w + (w.kind == 1 ? sizeof(GridCache) : 0));
  if (!world3d) { snprintf_P(err, len, PSTR("sem memoria para a cena 3D")); return false; }
  *world3d = w;
  if (w.kind == 1) gridCache()->ready = false;
  return true;
}
// "motion": {"period": ms, "keys": [{"t", "x", "y"}, ...]}: 2 a 6 pontos, t do 0 ao period, sempre crescente
static bool loadMotion(JsonObjectConst m, Motion &out, char *err, size_t len, int layer) {
  JsonArrayConst keys = m[JK("keys")];
  int period = m[JK("period")] | 0;
  if (period < 100 || period > 60000 || keys.size() < 2 || keys.size() > 6) {
    snprintf_P(err, len, PSTR("camada %d: motion com period 100-60000 e 2 a 6 pontos"), layer);
    return false;
  }
  out.period = period;
  out.n = 0;
  for (JsonObjectConst key : keys) {
    if (!key[JK("t")].is<int>() || !key[JK("x")].is<int>() || !key[JK("y")].is<int>()) {
      snprintf_P(err, len, PSTR("camada %d: ponto do motion precisa de t, x e y"), layer);
      return false;
    }
    int t = key[JK("t")], x = key[JK("x")], y = key[JK("y")];
    if (t < 0 || t > period || abs(x) > 480 || abs(y) > 480 || (!out.n && t != 0) || (out.n && t <= out.k[out.n - 1].t)) {
      snprintf_P(err, len, PSTR("camada %d: ponto do motion fora do limite ou fora de ordem"), layer);
      return false;
    }
    out.k[out.n++] = {(uint16_t)t, (int16_t)x, (int16_t)y};
  }
  if (out.k[out.n - 1].t != period) {
    snprintf_P(err, len, PSTR("camada %d: o ultimo ponto do motion precisa de t = period"), layer);
    return false;
  }
  return true;
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

// Fonte do JSON em memória (o simulador): o mesmo leitor que o ArduinoJson usa com arquivos
struct MemIn {
  const char *p;
  size_t n, i;
  int read() { return i < n ? (uint8_t)p[i++] : -1; }
  size_t readBytes(char *b, size_t m) {
    size_t k = n - i < m ? n - i : m;
    memcpy(b, p + i, k);
    i += k;
    return k;
  }
};

// ---------- sprites compactados na RAM ----------
// Cada sprite em `spr`:
//   [0] w  [1] h  [2] n (cores)  [3] fps << 4 | (quadros - 1)  (até 16 quadros; leia sempre com sprNf)
//   [4 ..] paleta: n x RGB565 (alto, baixo)
//   tabela: um uint16 por quadro (alto, baixo) = posição do quadro, contada do início do sprite
//   quadro: [x0][y0][cw][ch] = recorte só com os pixels visíveis (cw = 0: quadro vazio) e os pixels do recorte,
//           linha a linha, em 1, 2 ou 4 bits (pelo número de cores: 1, até 3, até 15; 0 = transparente).
// Quadros com as mesmas linhas no JSON são guardados uma vez só (a tabela aponta para o mesmo lugar).
// Pior caso (quadros cheios, mais de 3 cores, nenhum repetido): a conta crua + 6 B por quadro (4 do recorte e 2 da
// tabela), ou seja, até ~576 B acima do teto público com 12 sprites de 8 quadros. O oráculo do sim confere esse limite.
static uint8_t sprBits(uint8_t n) { return n <= 1 ? 1 : n <= 3 ? 2 : 4; }

// valor (0 = transparente, 1..n = cor) da letra `c` da linha; como sempre, vale a última letra igual da paleta
static uint8_t sprKey(const char *keys, uint8_t n, char c) {
  uint8_t v = 0;
  for (uint8_t i = 0; i < n; i++)
    if (keys[i] == c) v = i + 1;
  return v;
}

// Mede (out = nullptr) ou escreve (out = destino zerado) o sprite e devolve o tamanho em bytes. As duas coisas
// passam pelo mesmo código, e quem chama confere que o escrito tem o tamanho medido.
static size_t sprEncode(JsonArrayConst fr, JsonArrayConst px1, uint8_t w, uint8_t h, uint8_t nf, uint8_t fps,
                        const char *keys, const uint16_t *colors, uint8_t n, uint8_t *out) {
  const uint8_t bits = sprBits(n);
  size_t at = 4 + n * 2 + nf * 2;
  if (out) {
    out[0] = w;
    out[1] = h;
    out[2] = n;
    out[3] = fps << 4 | (nf - 1);
    for (uint8_t i = 0; i < n; i++) {
      out[4 + i * 2] = colors[i] >> 8;
      out[5 + i * 2] = colors[i] & 255;
    }
  }
  for (uint8_t k = 0; k < nf; k++) {
    JsonArrayConst px = fr.isNull() ? px1 : fr[k].as<JsonArrayConst>();
    uint8_t same = k;  // quadro anterior com as mesmas linhas
    for (uint8_t j = 0; j < k && same == k; j++) {
      JsonArrayConst pj = fr[j];
      uint8_t r = 0;
      while (r < h && !strcmp(px[r] | "", pj[r] | "")) r++;
      if (r == h) same = j;
    }
    size_t pos = at;
    if (same < k) pos = out ? (out[4 + n * 2 + same * 2] << 8 | out[5 + n * 2 + same * 2]) : 0;
    else {
      int x0 = w, y0 = h, x1 = -1, y1 = -1;  // recorte
      for (uint8_t r = 0; r < h; r++) {
        const char *row = px[r] | "";
        for (uint8_t c = 0; c < w && row[c]; c++)
          if (sprKey(keys, n, row[c])) {
            if (c < x0) x0 = c;
            if (c > x1) x1 = c;
            if (r < y0) y0 = r;
            if (r > y1) y1 = r;
          }
      }
      uint8_t cw = x1 < 0 ? 0 : x1 - x0 + 1, ch = x1 < 0 ? 0 : y1 - y0 + 1;
      if (out) {
        uint8_t *q = out + at;
        q[0] = cw ? x0 : 0;
        q[1] = cw ? y0 : 0;
        q[2] = cw;
        q[3] = ch;
        uint16_t i = 0;
        for (uint8_t r = 0; r < ch; r++) {
          const char *row = px[y0 + r] | "";
          size_t len = strlen(row);
          for (uint8_t c = 0; c < cw; c++, i++) {
            uint8_t v = (size_t)(x0 + c) < len ? sprKey(keys, n, row[x0 + c]) : 0;
            q[4 + i * bits / 8] |= v << (8 - bits - i * bits % 8);
          }
        }
      }
      at += 4 + (cw * ch * bits + 7) / 8;
    }
    if (out) {
      out[4 + n * 2 + k * 2] = pos >> 8;
      out[5 + n * 2 + k * 2] = pos & 255;
    }
  }
  return at;
}

// Lê o pacote em passadas com filtro, direto da fonte (arquivo ou memória), em vez de montar a árvore do
// arquivo inteiro: com ~31 KB de heap livre, um JSON acima de ~10 KB dava "NoMemory". O pico de memória da
// carga é a maior árvore viva somada ao que já foi alocado, então a ordem importa:
//  1. passada mínima: "format" e os nomes dos sprites (a árvore é liberada logo);
//  2. uma passada por sprite, crescendo `spr` no tamanho exato de cada um;
//  3. passada das camadas e dos metadados; só então `lay` e `pool`.
// Assim a árvore das camadas não fica viva durante as passadas dos sprites, e `lay`/`pool` não ocupam a RAM
// enquanto os sprites são lidos. `rewind` volta a fonte ao início.
template <class In, class Rew>
static bool loadFrom(In &in, Rew rewind, size_t size, char *err, size_t errLen) {
  facePackUnload();
  char sprName[MAX_SPRITES][24];
  uint16_t sprOff[MAX_SPRITES];
  uint8_t ns = 0, fmt = 0;
  bool v2 = size > V1_FILE;  // usa algo que só o formato 2 tem (ver MAX_FRAMES); a 1.25.0 recusa arquivo acima de 16 KB
  {
    JsonDocument doc;
    {
      JsonDocument filter;
      filter["format"] = true;
      filter["sprites"]["*"]["fps"] = true;  // só para ter as chaves; os sprites vêm um por vez, abaixo
      rewind();
      DeserializationError e = deserializeJson(doc, in, DeserializationOption::Filter(filter));
      if (e) {
        snprintf_P(err, errLen, PSTR("JSON invalido: %s"), e.c_str());
        return false;
      }
    }
    int f = doc["format"] | 0;
    if (f != 1 && f != 2) {
      snprintf_P(err, errLen, PSTR("formato desconhecido (use \"format\": 1 ou 2)"));
      return false;
    }
    fmt = f;
    for (JsonPairConst kv : doc["sprites"].as<JsonObjectConst>()) {
      if (ns >= MAX_SPRITES || strlen(kv.key().c_str()) >= sizeof sprName[0]) {
        snprintf_P(err, errLen, PSTR("sprite \"%s\": ate 32x32 e no maximo %d sprites"), kv.key().c_str(), MAX_SPRITES);
        return false;
      }
      strcpy(sprName[ns++], kv.key().c_str());
    }
  }

  // sprites, um por vez, compactados (ver sprEncode)
  for (uint8_t si = 0; si < ns; si++) {
    JsonDocument sd;
    {
      JsonDocument filter;
      filter["sprites"][sprName[si]] = true;
      rewind();
      DeserializationError e = deserializeJson(sd, in, DeserializationOption::Filter(filter));
      if (e) {
        facePackUnload();
        snprintf_P(err, errLen, PSTR("JSON invalido: %s"), e.c_str());
        return false;
      }
    }
    JsonObjectConst sp = sd["sprites"][sprName[si]];
    JsonObjectConst palObj = sp["pal"];
    JsonArrayConst fr = sp["frames"], px0 = fr.isNull() ? sp["px"] : fr[0];
    size_t w0 = px0.size() ? strlen(px0[0] | "") : 0, h0 = px0.size(), nf0 = fr.isNull() ? 1 : fr.size();
    int fps = sp["fps"] | 4;
    if (!w0 || w0 > 32 || h0 > 32) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sprite \"%s\": ate 32x32 e no maximo %d sprites"), sprName[si], MAX_SPRITES);
      return false;
    }
    if (nf0 < 1 || nf0 > MAX_FRAMES || fps < 1 || fps > 10) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sprite \"%s\": de 1 a %d quadros e \"fps\" de 1 a 10"), sprName[si], MAX_FRAMES);
      return false;
    }
    for (size_t k = 1; k < nf0; k++)
      if (fr[k].size() != h0 || strlen(fr[k][0] | "") != w0) {
        facePackUnload();
        snprintf_P(err, errLen, PSTR("sprite \"%s\": todos os quadros precisam ter o mesmo tamanho"), sprName[si]);
        return false;
      }
    // teto público: tamanho cru, sempre com 15 cores por sprite (a conta que a doc e a galeria usam)
    size_t add = 4 + 15 * 2 + nf0 * ((w0 * h0 + 1) / 2);
    if (sprRaw + add > MAX_SPR_BYTES) {
      size_t tot = sprRaw + add;
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sprites grandes demais: %d bytes no total (maximo %d)"), (int)tot, MAX_SPR_BYTES);
      return false;
    }
    sprRaw += add;
    if (nf0 > V1_FRAMES || sprRaw > V1_SPR_BYTES) v2 = true;
    uint8_t w = w0, h = h0, n = 0, nf = nf0;
    char keys[16];
    uint16_t colors[15];
    for (JsonPairConst c : palObj) {
      if (n == 15 || strlen(c.key().c_str()) != 1) {
        snprintf_P(err, errLen, PSTR("sprite \"%s\": paleta com ate 15 cores de 1 caractere"), sprName[si]);
        facePackUnload();
        return false;
      }
      keys[n] = c.key().c_str()[0];
      int32_t col = parseColor(c.value().as<const char *>(), 0);
      colors[n++] = col < 0 ? 0 : col;
    }
    // alocação exata, já compactada (a árvore da passada das camadas, mais adiante, aproveita o resto)
    JsonArrayConst px1 = sp["px"];
    size_t sz = sprEncode(fr, px1, w, h, nf, fps, keys, colors, n, nullptr);
    uint8_t *ns_ = sprUsed + sz <= 0xFFFF ? (uint8_t *)realloc(spr, sprUsed + sz) : nullptr;
    if (!ns_) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sem memoria para o pacote"));
      return false;
    }
    spr = ns_;
    sprOff[si] = sprUsed;
    memset(spr + sprUsed, 0, sz);
    if (sprEncode(fr, px1, w, h, nf, fps, keys, colors, n, spr + sprUsed) != sz) {  // medida e escrita divergiram
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sprite \"%s\": erro interno ao compactar"), sprName[si]);
      return false;
    }
    sprUsed += sz;
  }

  bool hasMot = false, hasTxt = false;
  // Corta strings gigantes (> 64 B, ou textos >= MAX_TEXT / acima do pool) para 48 'x' antes de irem ao StringPool:
  // um pacote de 24 KB com 48 textos distintos estouraria os 15 KB já no deserializeJson; na validação, >= 48 B é recusado.
  struct CapIn {
    In &in;
    char b[66], last = 0;
    uint8_t i = 0, len = 0;
    bool isTxt = false;
    uint16_t tot = 0;
    int read() {
      if (i < len) return (uint8_t)b[i++];
      int c = in.read();
      if (c != '"') {
        if (c > ' ') last = c;
        return c;
      }
      i = len = 0;
      int d = -1;
      for (bool esc = false;;) {
        d = in.read();
        if (d < 0) break;
        if (len <= 65) b[len++] = d;
        if (esc) esc = false;
        else if (d == '\\') esc = true;
        else if (d == '"') break;
      }
      uint8_t sLen = d == '"' && len ? len - 1 : len;
      if (last == '{' || last == ',')
        isTxt = (sLen == 2 && ((b[0] == 'p' && b[1] == 't') || (b[0] == 'e' && (b[1] == 'n' || b[1] == 's')))) ||
                (sLen == 4 && (!memcmp(b, "text", 4) || !memcmp(b, "file", 4)));
      else if (last == ':' && isTxt) {
        tot += sLen + 1;
        if (sLen >= MAX_TEXT || tot > MAX_POOL + 512) len = 66;
      }
      last = '"';
      if (len > 65) {
        memset(b, 'x', MAX_TEXT);
        b[MAX_TEXT] = '"';
        len = d < 0 ? MAX_TEXT : MAX_TEXT + 1;
      }
      return '"';
    }
    size_t readBytes(char *p, size_t m) {
      size_t k = 0;
      while (k < m) { int c = read(); if (c < 0) break; p[k++] = c; }
      return k;
    }
  };
  auto loadTexts = [&](JsonArrayConst tl) -> bool {
    size_t pcap = 1;
    uint8_t i = 0;
    for (JsonObjectConst l : tl) {
      if (i >= nLay) break;
      const Layer &L = lay[i++];
      if (L.type == L_TEXT && l[JK("text")].is<JsonObjectConst>()) {
        JsonObjectConst tx = l[JK("text")];
        const char *pt = tx["pt"] | "";
        for (const char *k : {"pt", "en", "es"}) {
          size_t n = strlen(tx[k] | pt);
          if (n >= MAX_TEXT) { pcap = MAX_POOL + 1; break; }
          pcap += n + 1;
        }
      } else if (L.type == L_TEXT || L.type == L_IMAGE) {
        const char *s = (L.type == L_TEXT ? l[JK("text")] : l[JK("file")]) | "";
        size_t n = strlen(s);
        if (n >= MAX_TEXT) pcap = MAX_POOL + 1;
        else pcap += n + 1;
      }
      if (pcap > MAX_POOL) {
        snprintf_P(err, errLen, PSTR("camada %d: texto grande demais"), i);
        facePackUnload();
        return false;
      }
    }
    pool = (char *)malloc(pcap);
    if (!pool) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sem memoria para o pacote"));
      return false;
    }
    poolCap = pcap;
    pool[0] = 0;
    poolUsed = 1;  // posição 0 = texto vazio
    i = 0;
    for (JsonObjectConst l : tl) {
      if (i >= nLay) break;
      Layer &L = lay[i++];
      if (L.type == L_TEXT && l[JK("text")].is<JsonObjectConst>()) {  // um texto por idioma, em sequência no pool
        JsonObjectConst tx = l[JK("text")];
        const char *pt = tx["pt"] | "";
        L.str = addStr(pt);
        addStr(tx["en"] | pt);
        addStr(tx["es"] | pt);
        L.tr = 1;
      } else if (L.type == L_TEXT) L.str = addStr(l[JK("text")] | "");
      else if (L.type == L_IMAGE) L.str = addStr(l[JK("file")] | "");
    }
    return true;
  };
  // camadas e metadados (no formato 2, sem "text", "file" e "motion", lidos depois em passadas próprias: somar
  // 48 textos ou 12 caminhos de movimento à árvore das 48 camadas passaria de 15 KB)
  {
  JsonDocument doc;
  {
    JsonDocument filter;
    for (const char *k : {"bg", "name", "desc", "author", "id"}) filter[k] = true;
    if (fmt == 2) {
      // todas as chaves que as camadas usam, menos "text", "file" e "motion".
      // O filtro maior custa ~1 KB a mais durante a leitura; o formato 1 continua com o filtro de antes (mesmo pico)
      char k[12];
      for (PGM_P p = LAYER_KEYS; pgm_read_byte(p); p += strlen_P(p) + 1) {
        strcpy_P(k, p);
        filter["layers"][0][k] = true;
      }
    } else filter["layers"] = true;  // no formato 1 o "motion" não vale; se aparecer, a passada do movimento recusa
    rewind();
    CapIn cap = {in};
    DeserializationError e = deserializeJson(doc, cap, DeserializationOption::Filter(filter));
    if (e) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("JSON invalido: %s"), e.c_str());
      return false;
    }
  }
  JsonArrayConst layers = doc["layers"];
  if (layers.isNull() || layers.size() == 0 || layers.size() > MAX_LAYERS) {
    facePackUnload();
    snprintf_P(err, errLen, PSTR("\"layers\" precisa ter de 1 a %d camadas"), MAX_LAYERS);
    return false;
  }
  lay = (Layer *)calloc(layers.size(), sizeof(Layer));
  if (!lay) {
    facePackUnload();
    snprintf_P(err, errLen, PSTR("sem memoria para o pacote"));
    return false;
  }

  // camadas
  for (JsonObjectConst l : layers) {
    Layer &L = lay[nLay];
    L.type = pick(l["type"] | (const char *)nullptr, TYPES, L_TYPES, 255);
    if (L.type == 255) {
      snprintf_P(err, errLen, PSTR("camada %d: tipo desconhecido"), nLay + 1);
      facePackUnload();
      return false;
    }
    L.mot = 0xFF;
    if (L.type == L_WORLD3D) {  // a cena cobre a tela inteira: só faz sentido no fundo, e uma por pacote
      if (nLay) {
        snprintf_P(err, errLen, PSTR("camada %d: \"world3d\" so como primeira camada"), nLay + 1);
        facePackUnload();
        return false;
      }
      if (!loadWorld3D(l, err, errLen)) {
        facePackUnload();
        return false;
      }
      v2 = true;
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
    // Limites de desenho: um raio de 30000 ou 30000 marcas prenderia o relógio num quadro só (watchdog) e
    // ele cairia na recuperação. Os mostradores da galeria usam no máximo ~480 de coordenada, raio 170,
    // 60 marcas e traço 26.
    bool stroke = L.type == L_LINE || L.type == L_HAND || L.type == L_TICKS || L.type == L_ARC;
    if (abs(L.x) > 960 || abs(L.y) > 960 || abs(L.w) > 960 || abs(L.h) > 960 || L.r < 0 || L.r > 360 ||
        (L.type == L_TICKS && (L.mx < 1 || L.mx > 120 || abs(L.w) > 240)) || (stroke && L.size > 64)) {
      snprintf_P(err, errLen, PSTR("camada %d: tamanho ou posicao fora do limite"), nLay + 1);
      facePackUnload();
      return false;
    }
    L.font = pick(l["font"] | (const char *)nullptr, FONTS, F_FONTS, F_PIXEL);
    L.align = pick(l["align"] | (const char *)nullptr, ALIGNS, 3, A_LEFT);
    L.when = pick(l["when"] | (const char *)nullptr, WHENS, W_WHENS, W_ALWAYS);
    L.var = L.type == L_MASCOT ? pick(l["mood"] | (const char *)nullptr, MOODS, 9, 8)
                               : pick(l["value"] | (const char *)nullptr, VARS, V_VARS, V_SEC);
    if (L.font == 255 || L.align == 255 || L.when == 255 || L.var == 255) {
      snprintf_P(err, errLen, PSTR("camada %d: font/align/when/value/mood invalido"), nLay + 1);
      facePackUnload();
      return false;
    }
    if (l[JK("overlay")] | false) {  // dígitos com borda suave por cima do que já está desenhado (a cena 3D), sem fundo
      if (L.type != L_TEXT || (L.font != F_BEBAS && L.font != F_OUTFIT && L.font != F_RIGHTEOUS)) {
        snprintf_P(err, errLen, PSTR("camada %d: \"overlay\" so em texto bebas, outfit ou righteous"), nLay + 1);
        facePackUnload();
        return false;
      }
      L.ov = 1;
      v2 = true;
    }
    int32_t defC1 = L.type == L_RECT || L.type == L_CIRCLE ? CARD : L.type == L_BAR || L.type == L_ARC ? WORK : TEXT;
    L.c1 = parseColor(l["color"] | (const char *)nullptr, defC1);
    const char *c2 = l["color2"] | (const char *)nullptr;  // aceita também os apelidos abaixo
    if (!c2) c2 = l["track"] | (const char *)nullptr;
    if (!c2) c2 = l["off"] | (const char *)nullptr;
    if (!c2) c2 = l["stroke"] | (const char *)nullptr;
    if (!c2) c2 = l["bg"] | (const char *)nullptr;
    L.c2 = parseColor(c2, C_NONE);
    if (L.type == L_TEXT && l["glow"].is<const char *>()) {  // halo que pulsa atrás do texto
      int gs = l["spread"] | 2, gp = l["pulse"] | 0;
      L.gl = 1;
      L.gs = gs < 1 ? 1 : gs > 3 ? 3 : gs;
      L.gc = parseColor(l["glow"], 0);
      L.gb = l["base"].is<const char *>() ? parseColor(l["base"], 0) : 0xFFFF;
      L.gp = gp <= 0 ? 0 : gp < 400 ? 400 : gp > 10000 ? 10000 : gp;
    }
    if (L.type == L_TEXT || L.type == L_IMAGE) hasTxt = true;
    else if (L.type == L_SPRITE) {
      const char *sn = l["sprite"] | "";
      L.str = 0xFFFF;
      for (uint8_t i = 0; i < ns; i++)
        if (!strcmp(sprName[i], sn)) L.str = sprOff[i];
      if (L.str == 0xFFFF) {
        snprintf_P(err, errLen, PSTR("camada %d: sprite \"%s\" nao existe"), nLay + 1, sn);
        facePackUnload();
        return false;
      }
    }
    if (!l[JK("motion")].isNull()) hasMot = true;
    nLay++;
  }
  if (fmt == 1 && !loadTexts(layers)) return false;
  bgColor = (uint16_t)parseColor(doc["bg"] | (const char *)nullptr, BG);
  snprintf(pkName, sizeof pkName, "%s", doc["name"] | "Sem nome");
  snprintf(pkDesc, sizeof pkDesc, "%s", doc["desc"] | "");
  snprintf(pkAuthor, sizeof pkAuthor, "%s", doc["author"] | "");
  for (char *t : {pkName, pkDesc, pkAuthor})  // uma linha só (vão para o .txt de metadados)
    for (; *t; t++)
      if (*t == '\n' || *t == '\r') *t = ' ';
  safeId(doc["id"] | pkName, pkId, sizeof pkId);
  if (!pkId[0]) strcpy(pkId, "pacote");
  }  // a árvore das camadas sai da memória aqui

  if (fmt == 2 && hasTxt) {
    JsonDocument td;
    {
      JsonDocument filter;
      filter["layers"][0][JK("text")] = true;
      filter["layers"][0][JK("file")] = true;
      rewind();
      CapIn cap = {in};
      DeserializationError e = deserializeJson(td, cap, DeserializationOption::Filter(filter));
      if (e) {
        facePackUnload();
        snprintf_P(err, errLen, PSTR("JSON invalido: %s"), e.c_str());
        return false;
      }
    }
    if (!loadTexts(td["layers"])) return false;
  }

  // movimento: uma passada só com o "motion" de cada camada, depois de lay/pool alocados e da árvore das camadas solta
  // (só no formato 2 ou se apareceu "motion": os pacotes do formato 1 não pagam a passada a mais)
  if (fmt == 2 || hasMot) {
    JsonDocument md;
    {
      JsonDocument filter;
      filter["layers"][0][JK("motion")] = true;
      rewind();
      DeserializationError e = deserializeJson(md, in, DeserializationOption::Filter(filter));
      if (e) {
        facePackUnload();
        snprintf_P(err, errLen, PSTR("JSON invalido: %s"), e.c_str());
        return false;
      }
    }
    JsonArrayConst ml = md["layers"];
    uint8_t nMot = 0;
    for (JsonObjectConst l : ml)
      if (!l[JK("motion")].isNull()) nMot++;
    if (nMot > MAX_MOTION) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("no maximo %d camadas com \"motion\""), MAX_MOTION);
      return false;
    }
    if (nMot && !(motPool = (Motion *)malloc(nMot * sizeof(Motion)))) {
      facePackUnload();
      snprintf_P(err, errLen, PSTR("sem memoria para o pacote"));
      return false;
    }
    uint8_t i = 0, motUsed = 0;
    for (JsonObjectConst l : ml) {
      if (i >= nLay) break;  // a mesma lista de camadas da passada anterior
      if (!l[JK("motion")].isNull()) {
        if (lay[i].type != L_SPRITE) {
          snprintf_P(err, errLen, PSTR("camada %d: \"motion\" so em sprite"), i + 1);
          facePackUnload();
          return false;
        }
        if (!loadMotion(l[JK("motion")], motPool[motUsed], err, errLen, i + 1)) {
          facePackUnload();
          return false;
        }
        lay[i].mot = motUsed++;
        v2 = true;
      }
      i++;
    }
  }
  if (fmt == 1 && v2) {
    facePackUnload();
    snprintf_P(err, errLen, PSTR("use \"format\": 2 (3D, motion, overlay, mais de 8 quadros, 6 KB de sprites ou 16 KB)"));
    return false;
  }
  return true;
}

bool facePackLoad(char *json, size_t len, char *err, size_t errLen) {
  MemIn in = {json, len, 0};
  return loadFrom(in, [&] { in.i = 0; }, len, err, errLen);
}

#ifdef ESP8266
bool facePackLoadFile(File &f, char *err, size_t errLen) {
  return loadFrom(f, [&] { f.seek(0); }, f.size(), err, errLen);
}
#endif

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
  if (!strcmp_P(k, PSTR("HH"))) t ? snprintf_P(v, n, PSTR("%02d"), displayHour(s)) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("H"))) t ? snprintf_P(v, n, PSTR("%d"), displayHour(s)) : snprintf_P(v, n, PSTR("-"));
  else if (!strcmp_P(k, PSTR("MM"))) t ? snprintf_P(v, n, PSTR("%02d"), m.tm_min) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("SS"))) t ? snprintf_P(v, n, PSTR("%02d"), m.tm_sec) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("WD"))) snprintf_P(v, n, PSTR("%s"), t ? WD[m.tm_wday] : "");
  else if (!strcmp_P(k, PSTR("DD"))) t ? snprintf_P(v, n, PSTR("%02d"), m.tm_mday) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("MON"))) snprintf_P(v, n, PSTR("%s"), t ? MO[m.tm_mon] : "");
  else if (!strcmp_P(k, PSTR("MO"))) t ? snprintf_P(v, n, PSTR("%02d"), m.tm_mon + 1) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("YYYY"))) t ? snprintf_P(v, n, PSTR("%d"), m.tm_year + 1900) : snprintf_P(v, n, PSTR("----"));
  else if (!strcmp_P(k, PSTR("AMPM"))) snprintf_P(v, n, PSTR("%s"), t && m.tm_hour >= 12 ? "PM" : "AM");
  else if (!strcmp_P(k, PSTR("TEMP"))) wx ? snprintf_P(v, n, PSTR("%d"), (int)lroundf(s.wx.t)) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("TMIN"))) wx ? snprintf_P(v, n, PSTR("%d"), (int)lroundf(s.wx.tmin)) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("TMAX"))) wx ? snprintf_P(v, n, PSTR("%d"), (int)lroundf(s.wx.tmax)) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("HUM"))) wx ? snprintf_P(v, n, PSTR("%d"), s.wx.hum) : snprintf_P(v, n, PSTR("--"));
  else if (!strcmp_P(k, PSTR("UNIT"))) snprintf_P(v, n, PSTR("%s"), s.fahr ? "F" : "C");
  else if (!strcmp_P(k, PSTR("RAIN"))) snprintf_P(v, n, PSTR("%s"), wx ? rainSoonText(s.wx.rainIn) : "");
  else if (!strcmp_P(k, PSTR("WX"))) snprintf_P(v, n, PSTR("%s"), wx ? weatherText(s.wx.code) : "");
  else if (!strcmp_P(k, PSTR("CITY"))) snprintf_P(v, n, PSTR("%s"), s.city ? s.city : "");
  else if (!strcmp_P(k, PSTR("FOCOS"))) snprintf_P(v, n, PSTR("%u"), s.today);
  else if (!strcmp_P(k, PSTR("META"))) snprintf_P(v, n, PSTR("%u"), s.dailyGoal);
  else if (!strcmp_P(k, PSTR("STREAK"))) snprintf_P(v, n, PSTR("%u"), s.streak);
  else if (!strcmp_P(k, PSTR("BEST"))) snprintf_P(v, n, PSTR("%u"), s.best);
  else if (!strcmp_P(k, PSTR("IP"))) snprintf_P(v, n, PSTR("%s"), s.ip ? s.ip : "");
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
  if (s.weatherOn && s.wx.ok && uiTempC(s, s.wx.t) < 15) return FACE_SCARF;  // em °C, qualquer que seja a unidade da tela
  if (s.weatherOn && s.wx.ok && uiTempC(s, s.wx.t) > 28) return FACE_SHADES;
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
static int runText(const Layer &L, const char *str, int x, int y, bool draw, uint16_t fg, int32_t c2, int grow = 0) {
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
        const int sw = (L.w > 0 ? L.w : (sz / 10 > 2 ? sz / 10 : 2)) + grow;
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
        if (draw && (ch == ':' || (ch >= '0' && ch <= '9'))) {
          if (L.ov) vecTextOver(vf, x, y, sz, one, fg);  // borda misturada com o que já está na faixa
          else vecText(vf, x, y, sz, one, fg, c2 == C_NONE ? bgColor : (uint16_t)c2);
        }
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
      int pad = (L.font == F_GEO ? (L.w > 0 ? L.w : L.size / 10 + 2) : 2) + (L.gl ? L.gs : 0);
      return {(int16_t)(x - pad), (int16_t)(L.y - pad), (int16_t)(w + 2 * pad), (int16_t)(h + 2 * pad)};
    }
    case L_SPRITE: return {L.x, L.y, (int16_t)(spr[L.str] * L.size), (int16_t)(spr[L.str + 1] * L.size)};
    case L_WORLD3D: return {0, 0, 240, 240};
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

// intensidade do brilho (0-7) no instante s.ms: onda suave só com inteiros
static uint8_t glowLevel(const Layer &L, const UiState &s) {
  if (!L.gp) return 7;
  int t = (uint32_t)s.ms % L.gp * 64 / L.gp, x = t < 32 ? t : 63 - t;
  return x * x * (93 - 2 * x) * 7 / 29791;
}

// mistura a e b em RGB565 (k de 0 a 256 = quanto de b)
static uint16_t mix565(uint16_t a, uint16_t b, int k) {
  int r = ((a >> 11) * (256 - k) + (b >> 11) * k) >> 8, g = (((a >> 5) & 63) * (256 - k) + ((b >> 5) & 63) * k) >> 8;
  int bl = ((a & 31) * (256 - k) + (b & 31) * k) >> 8;
  return r << 11 | g << 5 | bl;
}

// i-ésimo pixel (0 = transparente, 1..n = cor) dos pixels de um quadro compactado (ver sprEncode)
static inline uint8_t sprPixel(const uint8_t *px, uint8_t bits, int i) {
  int b = i * bits;
  return px[b >> 3] >> (8 - bits - (b & 7)) & ((1 << bits) - 1);
}

// número de quadros do sprite (1 a 16; ver o cabeçalho em sprEncode)
static inline uint8_t sprNf(const uint8_t *p) { return (p[3] & 15) + 1; }

// quadro do sprite no instante s.ms (1 quadro = parado)
static uint8_t sprFrame(const Layer &L, const UiState &s) {
  uint8_t nf = sprNf(spr + L.str);
  return nf > 1 ? (uint64_t)s.ms * (spr[L.str + 3] >> 4) / 1000 % nf : 0;  // 64 bits: ms x fps estouraria em ~5 dias
}

// Sprite com "motion": a posição do instante s.ms vai para a própria camada (as posições do movimento são absolutas,
// e o x/y do JSON não é usado). Inteiros só; cada ponto vale até o próximo, e o último fecha a volta.
static void moveLayer(Layer &L, const UiState &s) {
  const Motion &m = motPool[L.mot];
  uint32_t t = s.ms % m.period;
  uint8_t i = 1;
  while (i + 1 < m.n && t >= m.k[i].t) i++;
  const MotionKey &a = m.k[i - 1], &b = m.k[i];
  int32_t dt = (int32_t)t - a.t, span = b.t - a.t;
  L.x = a.x + ((int32_t)b.x - a.x) * dt / span;
  L.y = a.y + ((int32_t)b.y - a.y) * dt / span;
}

// quadro da cena 3D no instante s.ms (o que muda a imagem: a cena só é refeita quando ele muda)
static uint32_t worldTick(const UiState &s) { return (uint64_t)s.ms * world3d->fps / 1000; }

static uint32_t signature(const Layer &L, const UiState &s, char *txt, size_t n) {
  uint32_t h = 2166136261u;
  txt[0] = 0;
  switch (L.type) {
    case L_TEXT: {
      resolve(layerText(L), s, txt, n);
      h = fnv(h, txt, strlen(txt));
      if (L.gl) { uint8_t g = glowLevel(L, s); h = fnv(h, &g, 1); }  // o pulso redesenha só a área do texto
      break;
    }
    case L_MASCOT: { uint8_t m = L.var == 8 ? autoMood(s) : L.var; h = fnv(h, &m, 1); break; }
    case L_PLANT: { const UiSprite *p = plantSprite(s.streak); h = fnv(h, &p, sizeof p); break; }
    case L_WEATHER: { int v = s.weatherOn && s.wx.ok ? s.wx.code * 2 + s.wx.day + 1 : 0; h = fnv(h, &v, sizeof v); break; }
    case L_BAR: { int mx, v = value(L, s, mx); int fw = L.mx > 0 ? (v > L.mx ? L.mx : v) * L.w / L.mx : (v > mx ? mx : v) * L.w / mx; h = fnv(h, &fw, sizeof fw); break; }
    case L_HAND: { int a = (int)(handAngle(L, s) * 1000); h = fnv(h, &a, sizeof a); break; }
    case L_ARC: { int mx, v = value(L, s, mx); if (L.mx > 0) mx = L.mx; int q = (v > mx ? mx : v) * 720 / mx; h = fnv(h, &q, sizeof q); break; }
    case L_SPRITE: if (sprNf(spr + L.str) > 1) { uint8_t f = sprFrame(L, s); h = fnv(h, &f, 1); } break;  // só animado muda
    case L_WORLD3D: { uint32_t k = worldTick(s); h = fnv(h, &k, sizeof k); break; }
  }
  if (L.mot != 0xFF) {  // o sprite que anda muda de lugar (moveLayer já pôs a posição do instante)
    h = fnv(h, &L.x, sizeof L.x);
    h = fnv(h, &L.y, sizeof L.y);
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

// ---------- cena 3D ----------
// Corredor: uma coluna de raio a cada 2 px (120), com o mapa 8x8 e a câmera do instante. As colunas são calculadas uma
// vez por quadro da cena (gridCache) e cada faixa da tela só pinta as linhas dela.
static void drawGrid(const UiState &s) {
  const World3D &w = *world3d;
  GridCache &g = *gridCache();
  uint32_t tick = worldTick(s);
  if (!g.ready || tick != g.tick) {
    g.ready = true;
    g.tick = tick;
    uint32_t ct = s.ms % w.period;
    int j = 1;
    while (j + 1 < w.keyCount && ct >= w.camera[j].t) j++;
    const WorldKey &u = w.camera[j - 1], &v = w.camera[j];
    float f = (float)(ct - u.t) / (v.t - u.t);
    float px = (u.x + (v.x - u.x) * f) / 100.0f, py = (u.y + (v.y - u.y) * f) / 100.0f;
    float a = (u.yaw + (v.yaw - u.yaw) * f) * 0.0174532925f, dx = cosf(a), dy = sinf(a);
    for (int i = 0; i < 120; i++) {
      float c = ((i * 2 + 1) - 120) / 120.0f * 0.66f, rx = dx - dy * c, ry = dy + dx * c;
      int mx = (int)px, my = (int)py, sx = rx < 0 ? -1 : 1, sy = ry < 0 ? -1 : 1;
      float ax = rx == 0 ? 1e20f : fabsf(1.0f / rx), ay = ry == 0 ? 1e20f : fabsf(1.0f / ry);
      float tx = (rx < 0 ? px - mx : mx + 1.0f - px) * ax, ty = (ry < 0 ? py - my : my + 1.0f - py) * ay;
      int side = 0;
      float dist = 0.01f;
      for (int n = 0; n < 24; n++) {  // o mapa é fechado: 24 passos atravessam qualquer diagonal de 8x8
        if (tx < ty) { dist = tx; tx += ax; mx += sx; side = 0; }
        else { dist = ty; ty += ay; my += sy; side = 1; }
        if (mx < 0 || mx >= 8 || my < 0 || my >= 8 || (w.cells[my] & (1U << mx))) break;
      }
      if (dist < 0.05f) dist = 0.05f;
      int h = (int)(158.0f / dist);
      if (h > 480) h = 480;
      g.top[i] = 120 - h / 2;
      g.bottom[i] = 120 + h / 2;
      float hit = side ? px + dist * rx : py + dist * ry;
      hit -= floorf(hit);
      g.seam[i] = ((int)(hit * 8) & 3) == 0;
      int shade = 16 - (int)(dist * 2) - (side ? 2 : 0);
      if (shade < 4) shade = 4;
      if (shade > 16) shade = 16;
      g.wall[i] = mix16(0, w.colors[0], shade);
      g.mortar[i] = mix16(0, w.accent, shade);
    }
  }
  int x0 = 0, x1 = 240, y0 = 0, y1 = 240;
  if (uiClip.on) { x0 = uiClip.x0; x1 = uiClip.x1; y0 = uiClip.y0; y1 = uiClip.y1; }
  const uint16_t ceiling = w.colors[1], floor = w.colors[2];
  G->fillRect(x0, 0, x1 - x0, 120, ceiling);
  G->fillRect(x0, 120, x1 - x0, 120, floor);
  for (int i = x0 / 2; i < 120 && i * 2 < x1; i++) {
    int t = g.top[i], b = g.bottom[i], h = b - t, ct = t > y0 ? t : y0, cb = b < y1 ? b : y1;
    if (cb <= ct) continue;
    G->fillRect(i * 2, ct, 2, cb - ct, g.wall[i]);
    for (int k = 1; k < 8; k++) {  // juntas horizontais da parede
      int y = t + h * k / 8;
      if (y >= y0 && y < y1) G->fillRect(i * 2, y, 2, 1, g.mortar[i]);
    }
    if (g.seam[i]) G->fillRect(i * 2, ct, 1, cb - ct, g.mortar[i]);  // juntas verticais
    if (t >= y0 && t < y1) G->fillRect(i * 2, t, 2, 1, mix565(ceiling, g.wall[i], 128));  // borda mais suave
    if (b - 1 >= y0 && b - 1 < y1) G->fillRect(i * 2, b - 1, 2, 1, mix565(floor, g.wall[i], 128));
  }
}

// Túnel: cada amostra 2x2 tem profundidade e ângulo na tabela da flash (tunel_lut.h); o avanço desloca as faixas de
// cor ao longo do eixo, sem conta em ponto flutuante por quadro. Desenha linha a linha (uiRowBuf), só as da faixa.
static void drawTunnel(const UiState &s) {
  const World3D &w = *world3d;
  int y0 = uiClip.on ? uiClip.y0 : 0, y1 = uiClip.on ? uiClip.y1 : 240;
  if (y0 < 0) y0 = 0;
  if (y1 > 240) y1 = 240;
  unsigned phase = (uint64_t)s.ms * w.speed / 1000U & 31U;
  int t = s.ms % 4000, tri = t < 2000 ? t : 4000 - t;
  int shift = (tri * 2 - 2000) * w.sway / 2000;  // "sway" balança o ponto de fuga
  int rotation = (uint64_t)s.ms * w.spin / 4000U & 15;  // "spin" gira os setores de cor
  uint16_t *row = uiRowBuf();
  uint8_t depth[60];
  for (int y = y0 & ~1; y < y1; y += 2) {
    int py = y / 2, qy = py < 60 ? 59 - py : py - 60;
    unsigned nib = (unsigned)pgm_read_word(&TUNEL_ROWS[qy]) * 2;
    int prev = 0;
    for (int q = 0; q < 60; q++) {
      auto next = [&]() -> uint8_t { uint8_t b = pgm_read_byte(&TUNEL_DEPTH[nib / 2]); uint8_t v = nib & 1 ? b & 15 : b >> 4; nib++; return v; };
      uint8_t v = next();
      if (v == 15) { uint8_t hi = next(), lo = next(); prev = hi << 4 | lo; }
      else prev += (int)v - 7;
      depth[q] = (uint8_t)prev;
    }
    for (int px = 0; px < 120; px++) {
      int sx = px - shift;
      if (sx < 0) sx = 0;
      if (sx > 119) sx = 119;
      int qx = sx < 60 ? 59 - sx : sx - 60;
      unsigned idx = qy * 60 + qx, a = (pgm_read_byte(&TUNEL_ANGLE[idx / 4]) >> ((idx & 3) * 2)) & 3, z = depth[qx];
      if (w.shape) z = pgm_read_byte(&TUNEL_SQUARE[qx > qy ? qx : qy]);
      int sector = sx >= 60 ? (py >= 60 ? a : 15 - a) : (py >= 60 ? 7 - a : 8 + a);
      uint16_t c = w.colors[((sector + rotation) & 15) / 4];
      unsigned band = (z + phase) & 31;
      if (band < 13) c = (c & 0xf7de) >> 1;  // metade do brilho: as faixas escuras
      if (band == 0 || band == 16) c = w.accent;
      row[px * 2] = row[px * 2 + 1] = c;
    }
    if (y >= y0) blit(0, y, row, 240, 1);
    if (y + 1 < y1) blit(0, y + 1, row, 240, 1);
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
      if (L.gl) {  // anéis de fora para dentro, cada um mais forte que o anterior
        static const int8_t D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
        int a = 96 + glowLevel(L, s) * 20;
        uint16_t gb = L.gb == 0xFFFF ? bgColor : L.gb;
        for (int r = L.gs; r >= 1; r--) {
          uint16_t gc = mix565(gb, L.gc, a * (L.gs - r + 1) / (L.gs + 1) / 2);
          if (L.font == F_GEO) runText(L, txt, x, L.y, true, gc, C_NONE, 2 * r);  // traço mais grosso: uma passada por anel
          else
            for (int k = 0; k < 8; k++) runText(L, txt, x + D[k][0] * r, L.y + D[k][1] * r, true, gc, C_NONE);
        }
      }
      runText(L, txt, x, L.y, true, c1, L.c2 == C_ACCENT ? s.accent : L.c2);
      break;
    }
    case L_SPRITE: {
      const uint8_t *p = spr + L.str, n = p[2], bits = sprBits(n), *pal = p + 4;
      uint8_t f = sprFrame(L, s);
      const uint8_t *q = p + (p[4 + n * 2 + f * 2] << 8 | p[5 + n * 2 + f * 2]), x0 = q[0], y0 = q[1], cw = q[2], ch = q[3];
      for (int r = 0, i = 0; r < ch; r++)
        for (int c = 0; c < cw;) {
          uint8_t v = sprPixel(q + 4, bits, i), run = 1;
          while (c + run < cw && sprPixel(q + 4, bits, i + run) == v) run++;
          if (v) G->fillRect(L.x + (x0 + c) * L.size, L.y + (y0 + r) * L.size, run * L.size, L.size, pal[v * 2 - 2] << 8 | pal[v * 2 - 1]);
          c += run;
          i += run;
        }
      break;
    }
    case L_WORLD3D: world3d->kind ? drawGrid(s) : drawTunnel(s); break;
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
    if (L.mot != 0xFF) moveLayer(L, s);  // posição do instante, usada abaixo e no desenho das faixas
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
  // Com cena 3D o desenho completo também vai em faixas fora da tela: a cena cobre tudo (sem apagar antes, a tela não
  // pisca) e o texto "overlay" precisa dos pixels da cena na faixa para misturar a borda.
  const bool scene = world3d && lay[0].sig;  // a cena é sempre a camada 0; com "when" ela pode estar escondida
  // texto "overlay" sobre sprites (sem cena 3D) também só mistura a borda numa faixa fora da tela: o desenho completo
  // direto na tela deixaria a borda binária e diferente do incremental
  bool strips = scene;
  for (int i = 0; i < nLay && !strips; i++) strips = lay[i].ov && lay[i].sig;
  if (full && strips) addDirty(dirty, nd, {0, 0, 240, 240});
  else if (full) {
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
    if (!world3d || !lay[0].sig) G->fillRect(a.d.x, a.d.y, a.d.w, a.d.h, bgColor);  // a cena 3D (camada 0) pinta tudo
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
