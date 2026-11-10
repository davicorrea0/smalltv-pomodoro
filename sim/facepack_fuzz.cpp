// Fuzzing do motor de mostradores de pacote (facepack.cpp), com ASan/UBSan (sim/testes.sh).
// Parte dos pacotes da galeria (faces/*.json) e de sementes pequenas e, com semente fixa, estraga cada um: corta,
// troca, apaga e duplica bytes, emenda dois pacotes, troca números por valores extremos. Cada entrada passa pelo
// facePackLoad; as aceitas são desenhadas inteiras em alguns instantes e depois de forma incremental (áreas sujas e
// faixas fora da tela), e descarregadas. Qualquer acesso fora do bloco, estouro ou comportamento indefinido para o
// teste (o sanitizer aborta). Pacote ruim só pode virar recusa com mensagem, nunca queda.
// Uso: facepack_fuzz <pasta dos pacotes> [casos]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <string>
#include <vector>
#include <algorithm>
#include "../smalltv-pomodoro/ui_kit.h"
#include "../smalltv-pomodoro/facepack.h"

class Tela : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  Tela() : Adafruit_GFX(240, 240) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = c;
  }
};

static uint32_t semente = 0xFACE2026u;
static uint32_t rnd(uint32_t n) {
  semente ^= semente << 13;
  semente ^= semente >> 17;
  semente ^= semente << 5;
  return n ? semente % n : 0;
}

// sementes pequenas com cada recurso do formato (sprites animados, texto por idioma, brilho, imagem, ponteiros...)
static const char *SEMENTES[] = {
  "{\"format\":1,\"bg\":\"#102030\",\"sprites\":{\"a\":{\"pal\":{\"x\":\"#ff0000\",\"y\":\"accent\"},\"fps\":7,\"frames\":[[\"x.y\",\".x.\"],[\"y.x\",\"...\"],[\"x.y\",\".x.\"]]},"
  "\"b\":{\"pal\":{\"o\":\"#00ff00\"},\"px\":[\"oo\",\"o\"]}},\"layers\":[{\"type\":\"sprite\",\"sprite\":\"a\",\"x\":10,\"y\":20,\"size\":3},"
  "{\"type\":\"sprite\",\"sprite\":\"b\",\"x\":-5,\"y\":230,\"size\":2},{\"type\":\"text\",\"x\":120,\"y\":100,\"text\":{\"pt\":\"{HH}:{MM}\",\"en\":\"{H}\"},\"align\":\"center\",\"font\":\"geo\",\"size\":40,"
  "\"glow\":\"#ffaa00\",\"pulse\":900,\"spread\":3}]}",
  "{\"format\":1,\"layers\":[{\"type\":\"hand\",\"x\":120,\"y\":120,\"len\":90,\"tail\":10,\"value\":\"sec\"},{\"type\":\"arc\",\"x\":120,\"y\":120,\"r\":100,\"size\":12,\"value\":\"min\",\"track\":\"card\"},"
  "{\"type\":\"ticks\",\"x\":120,\"y\":120,\"r\":110,\"count\":60,\"len\":6},{\"type\":\"bar\",\"x\":20,\"y\":200,\"w\":200,\"h\":8,\"r\":4,\"value\":\"focos\",\"max\":8},"
  "{\"type\":\"image\",\"x\":0,\"y\":0,\"w\":40,\"h\":40,\"file\":\"/a.jpg\"},{\"type\":\"mascot\",\"x\":80,\"y\":80,\"size\":4,\"mood\":\"auto\"},{\"type\":\"plant\",\"x\":0,\"y\":180,\"size\":2},"
  "{\"type\":\"weather\",\"x\":200,\"y\":40,\"size\":12,\"when\":\"weather\"},{\"type\":\"line\",\"x\":0,\"y\":0,\"x2\":239,\"y2\":239,\"size\":5},{\"type\":\"circle\",\"x\":30,\"y\":30,\"r\":20,\"color\":\"accent\",\"stroke\":\"text\"},"
  "{\"type\":\"text\",\"x\":5,\"y\":5,\"font\":\"dseg\",\"size\":30,\"text\":\"{SS}\",\"off\":\"#202020\"},{\"type\":\"text\",\"x\":5,\"y\":60,\"font\":\"dm\",\"size\":4,\"text\":\"{TEMP}\\u00b0{UNIT}\"}]}",
  // formato 2: texto "overlay" sobre sprites (círculo + sprite com "motion") e sprite com muitos quadros e "motion"
  "{\"format\":2,\"bg\":\"#102040\",\"sprites\":{\"s\":{\"fps\":4,\"pal\":{\"a\":\"#e0a030\",\"b\":\"#30a0e0\"},\"frames\":[[\"aab\",\"aab\",\"bbb\"],[\"baa\",\"baa\",\"bbb\"]]}},"
  "\"layers\":[{\"type\":\"circle\",\"x\":120,\"y\":120,\"r\":90,\"color\":\"#2a5a8a\"},{\"type\":\"sprite\",\"sprite\":\"s\",\"size\":8,\"motion\":{\"period\":4000,\"keys\":[{\"t\":0,\"x\":20,\"y\":40},{\"t\":2000,\"x\":180,\"y\":90},{\"t\":4000,\"x\":20,\"y\":40}]}},"
  "{\"type\":\"text\",\"x\":120,\"y\":72,\"font\":\"bebas\",\"size\":96,\"align\":\"center\",\"color\":\"#ecffff\",\"overlay\":true,\"text\":\"{HH}:{MM}\"}]}",
  "{\"format\":2,\"sprites\":{\"m\":{\"pal\":{\"a\":\"#ffcc00\"},\"fps\":10,\"frames\":[[\"a.\"],[\".a\"],[\"aa\"],[\"a.\"],[\".a\"],[\"aa\"],[\"a.\"],[\".a\"],[\"aa\"]]}},"
  "\"layers\":[{\"type\":\"rect\",\"x\":0,\"y\":0,\"w\":240,\"h\":240,\"color\":\"#15151d\",\"when\":\"night\"},"
  "{\"type\":\"sprite\",\"sprite\":\"m\",\"size\":6,\"motion\":{\"period\":3000,\"keys\":[{\"t\":0,\"x\":-20,\"y\":10},{\"t\":1500,\"x\":230,\"y\":200},{\"t\":3000,\"x\":-20,\"y\":10}]}}]}",
};

static const char *EXTREMOS[] = {"0", "-1", "1", "255", "256", "32767", "-32768", "65535", "99999", "2147483647", "-2147483648", "1e9", "0.5", "null", "true", "\"\"", "[]", "{}"};

static void mutar(std::string &s, const std::vector<std::string> &todos) {
  for (int k = 1 + rnd(3); k-- && !s.empty();) {
    size_t a = rnd(s.size());
    switch (rnd(8)) {
      case 0: s.resize(a); break;                                                    // corte
      case 1: s[a] = "{}[]\",:0123456789-.aztx #"[rnd(25)]; break;                   // troca
      case 2: s.erase(a, 1 + rnd(8)); break;                                        // apaga
      case 3: { size_t b = rnd(s.size()); if (a > b) std::swap(a, b);                 // duplica um trecho
                s.insert(b, s.substr(a, std::min<size_t>(b - a, 1500))); break; }
      case 4: { const std::string &o = todos[rnd(todos.size())];                    // emenda com outro pacote
                s = s.substr(0, a) + o.substr(rnd(o.size())); break; }
      case 5: case 6: {                                                             // número -> valor extremo
        size_t i = a;
        while (i < s.size() && !(s[i] >= '0' && s[i] <= '9')) i++;
        if (i == s.size()) break;
        size_t j = i;
        while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == '-')) j++;
        if (i && s[i - 1] == '-') i--;
        s.replace(i, j - i, EXTREMOS[rnd(sizeof EXTREMOS / sizeof *EXTREMOS)]);
        break;
      }
      default: { size_t b = rnd(s.size()); std::swap(s[a], s[b]); break; }          // troca dois bytes
    }
  }
  if (s.size() > 24576) s.resize(24576);  // o firmware recusa arquivos maiores antes de carregar
}

int main(int argc, char **argv) {
  const char *pasta = argc > 1 ? argv[1] : "../faces";
  long casos = argc > 2 ? atol(argv[2]) : 5000;
  std::vector<std::string> todos;
  for (const char *s : SEMENTES) todos.push_back(s);
  DIR *d = opendir(pasta);
  for (struct dirent *e; d && (e = readdir(d));) {
    size_t n = strlen(e->d_name);
    if (n < 6 || strcmp(e->d_name + n - 5, ".json") || !strcmp(e->d_name, "catalog.json")) continue;
    FILE *f = fopen((std::string(pasta) + "/" + e->d_name).c_str(), "rb");
    if (!f) continue;
    std::string s;
    char b[4096];
    for (size_t k; (k = fread(b, 1, sizeof b, f));) s.append(b, k);
    fclose(f);
    todos.push_back(s);
  }
  if (d) closedir(d);
  if (todos.size() < 10) { printf("facepack_fuzz: poucos pacotes em %s\n", pasta); return 1; }
  Tela *tela = new Tela();
  uiBegin(tela);
  long aceitos = 0;
  char err[96];
  for (long c = 0; c < casos + (long)todos.size(); c++) {
    std::string s = todos[c < (long)todos.size() ? c : rnd(todos.size())];
    if (c >= (long)todos.size()) mutar(s, todos);
    std::vector<char> buf(s.begin(), s.end());  // o leitor pode alterar o buffer
    buf.push_back(0);
    if (!facePackLoad(buf.data(), s.size(), err, sizeof err)) {
      if (c < (long)todos.size()) { printf("facepack_fuzz: semente %ld (sem estragar) recusada: %s\n", c, err); return 1; }
      if (!err[0]) { printf("facepack_fuzz: recusa sem mensagem no caso %ld\n", c); return 1; }
      continue;
    }
    aceitos++;
    UiState st;
    memset(&st, 0, sizeof st);
    st.theme = THEME_PACK;
    st.timeOk = rnd(4) != 0;
    st.now.tm_hour = rnd(24); st.now.tm_min = rnd(60); st.now.tm_sec = rnd(60); st.now.tm_mday = 1 + rnd(28); st.now.tm_mon = rnd(12);
    st.now.tm_wday = rnd(7); st.now.tm_year = 126;
    st.weatherOn = rnd(2);
    st.wx.ok = rnd(2); st.wx.code = rnd(100); st.wx.day = rnd(2); st.wx.t = (float)rnd(60) - 20; st.wx.hum = rnd(101);
    st.city = "Cidade"; st.ip = "10.0.0.1"; st.dailyGoal = rnd(12); st.today = rnd(15); st.streak = rnd(40);
    st.accent = (uint16_t)rnd(65536);
    uiSetLang(rnd(3));
    st.ms = rnd(1u << 31);
    facePackRender(st, true);
    for (int k = 0; k < 4; k++) {  // incremental: o tempo anda, os segundos mudam
      st.ms += 37 + rnd(400);
      st.now.tm_sec = (st.now.tm_sec + 1) % 60;
      facePackRender(st, false);
    }
    facePackUnload();
  }
  uiSetLang(0);
  delete tela;
  printf("facepack_fuzz: ok (%ld casos, %ld aceitos e desenhados, %zu sementes)\n", casos, aceitos, todos.size());
  return 0;
}
