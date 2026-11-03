// Teste de robustez do leitor de pacotes .stheme (smalltv-pomodoro/stheme.h) com ASan/UBSan, no WSL:
//   bash sim/test-stheme.sh [casos]          compila em 32 bits e roda o teste e este fuzzing
//   ./fuzz run <arquivo.stheme>              confere um arquivo (para reproduzir)
//   ./fuzz fuzz <sementes> <casos> [semente] corrompe as sementes e confere cada caso
// Metade dos casos troca bytes do arquivo inteiro; a outra metade mexe no theme.json ou numa imagem
// e remonta o contêiner com os tamanhos certos, para chegar ao leitor de JSON e às linhas do STI2.
// Um caso falha se ler fora do pacote, se der resultado diferente na segunda vez, se a mensagem
// passar do espaço dado ou se um pacote aceito trouxer valores fora dos limites.
#include "../smalltv-pomodoro/stheme.h"
#include <dirent.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

using Bytes = std::string;

static const Bytes *current;
static bool memRead(void *, uint32_t pos, uint8_t *buf, uint16_t n) {
  if (pos > current->size() || n > current->size() - pos) {
    fprintf(stderr, "stheme_fuzz: leitura fora do pacote (%u + %u de %zu)\n", pos, n, current->size());
    abort();
  }
  memcpy(buf, current->data() + pos, n);
  return true;
}

static SthWork work;
static bool check(const Bytes &p, char *err, size_t errLen) {
  current = &p;
  return sthCheck(memRead, nullptr, (uint32_t)p.size(), work, err, errLen);
}

static void fail(const Bytes &p, const char *why) {
  const char *out = "/tmp/stheme-fuzz-falha.stheme";
  std::ofstream(out, std::ios::binary).write(p.data(), p.size());
  fprintf(stderr, "stheme_fuzz: %s (caso gravado em %s)\n", why, out);
  abort();
}

// confere um caso; devolve o motivo da recusa ("" se aceito)
static std::string one(const Bytes &p, std::mt19937 &rng) {
  char err[160], again[160];
  memset(err, 0x55, sizeof err);
  const size_t errLen = rng() % 8 ? sizeof err - 1 : rng() % 12;  // às vezes, espaço curto para a mensagem
  const bool ok = check(p, err, errLen);
  if (!ok && errLen && !memchr(err, 0, errLen)) fail(p, "mensagem sem fim");
  if ((unsigned char)err[sizeof err - 1] != 0x55) fail(p, "mensagem passou do espaço");
  const SthInfo first = work.info;
  if (check(p, again, errLen) != ok || (!ok && errLen && strcmp(err, again))) fail(p, "resultado diferente na segunda vez");
  if (!ok) return errLen == sizeof err - 1 ? err : "?";
  const SthInfo &f = first;
  if (f.spec < 1 || f.spec > 2 || f.layers > STH_MAX_LAYERS || f.data > STH_MAX_DATA || !f.entries || f.entries > STH_MAX_ENTRIES ||
      f.sti1 + f.sti2 + 1 != f.entries || !f.manifestLen || f.manifestLen > STH_MAX_MANIFEST || f.manifestPos + f.manifestLen > p.size() ||
      (f.sti2 && f.spec < 2) || !memchr(f.id, 0, sizeof f.id) || !f.id[0] || !memchr(f.name, 0, sizeof f.name) || !f.name[0] ||
      !memchr(f.author, 0, sizeof f.author) || !memchr(f.version, 0, sizeof f.version))
    fail(p, "pacote aceito com valores fora dos limites");
  return "";
}

static Bytes file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), {});
}

// ---------- contêiner ----------
struct Entry { Bytes path, data; };
static bool unpack(const Bytes &p, std::vector<Entry> &out) {
  if (p.size() < 8) return false;
  auto u16 = [&](size_t at) { return (unsigned)(uint8_t)p[at] | (unsigned)(uint8_t)p[at + 1] << 8; };
  size_t n = u16(4), at = 8;
  for (size_t i = 0; i < n; i++) {
    if (p.size() - at < 6) return false;
    size_t len = u16(at), size = u16(at + 2) | (size_t)u16(at + 4) << 16;
    if (p.size() - at - 6 < len + size) return false;
    out.push_back({p.substr(at + 6, len), p.substr(at + 6 + len, size)});
    at += 6 + len + size;
  }
  return true;
}
static Bytes pack(const std::vector<Entry> &entries) {
  Bytes s = "STH1";
  auto u16 = [&](unsigned n) { s += char(n), s += char(n >> 8); };
  u16(entries.size()), u16(0);
  for (const Entry &e : entries) u16(e.path.size()), u16(e.data.size() & 0xFFFF), u16(e.data.size() >> 16), s += e.path, s += e.data;
  return s;
}

// ---------- mutações ----------
static const char *const TOKENS[] = {
  "{", "}", "[", "]", ",", ":", "\"", "\\", "\\u", "\\ud83c", "\\udf45", "\\u0000", "null", "true", "false", "0", "-0", "1", "2", "240",
  "86400", "1e3", "2.5", "99999999999", "\"spec\":2,", "\"spec\":1", "\"layers\":[]", "\"data\":[]", "\"theme\":{}", "\"id\":\"time\"",
  "\"url\":\"https://x/y\"", "\"insecureTls\":true", "\"interval\":10", "\"fields\":[{\"id\":\"a\",\"path\":\"b.c\"}]",
  "\"background\":\"#ffffff\"", "{\"a\":[[[[[[[[1]]]]]]]]}", "\x01", "\x7f", "\xc3\xa3", " ", "\n",
};
static uint8_t interesting(std::mt19937 &rng) {
  static const uint8_t V[] = {0, 1, 2, 3, 0x0f, 0x10, 0x11, 0x3f, 0x40, 0x7f, 0x80, 0x81, 0xbf, 0xc0, 0xff, 240, 241, '/', '.', '"', '\\'};
  return V[rng() % sizeof V];
}
static void bytes(Bytes &s, std::mt19937 &rng) {
  for (int k = 1 + rng() % 4; k--;) {
    size_t at = s.empty() ? 0 : rng() % s.size();
    switch (rng() % 7) {
      case 0: if (!s.empty()) s[at] ^= char(1 << rng() % 8); break;
      case 1: if (!s.empty()) s[at] = char(interesting(rng)); break;
      case 2: if (!s.empty()) s[at] = char(rng()); break;
      case 3: s.insert(at, 1 + rng() % 4, char(interesting(rng))); break;
      case 4: if (!s.empty()) s.erase(at, 1 + rng() % 8); break;
      case 5: if (!s.empty()) s.insert(at, s.substr(rng() % s.size(), 1 + rng() % 16)); break;
      default: if (!s.empty()) s.resize(rng() % s.size()); break;
    }
  }
}
static void json(Bytes &s, std::mt19937 &rng) {
  for (int k = 1 + rng() % 3; k--;) {
    size_t at = s.empty() ? 0 : rng() % (s.size() + 1);
    if (rng() % 3 == 0 && !s.empty()) s.erase(at < s.size() ? at : 0, 1 + rng() % 6);
    else s.insert(at, TOKENS[rng() % (sizeof TOKENS / sizeof *TOKENS)]);
  }
}
static Bytes mutate(const Bytes &seed, std::mt19937 &rng) {
  std::vector<Entry> e;
  if (rng() % 2 && unpack(seed, e) && !e.empty()) {
    Entry &t = e[rng() % e.size()];
    switch (rng() % 6) {
      case 0: bytes(t.path, rng); break;
      case 1: e.push_back(e[rng() % e.size()]); break;  // entrada repetida
      case 2: e.erase(e.begin() + rng() % e.size()); break;
      default:
        if (t.path == "theme.json" && rng() % 4) json(t.data, rng);
        else if (t.data.size() > 12 && rng() % 2) t.data[12 + rng() % (t.data.size() - 12)] = char(rng() % 2 ? interesting(rng) : rng());  // tabela e linhas do STI2
        else bytes(t.data, rng);
    }
    return pack(e);
  }
  Bytes s = seed;
  bytes(s, rng);
  return s;
}

int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "run")) {
    std::mt19937 rng(1);
    Bytes p = file(argv[2]);
    std::string why = one(p, rng);
    printf("%s: %s\n", argv[2], why.empty() ? "aceito" : why.c_str());
    return 0;
  }
  if (argc < 4 || strcmp(argv[1], "fuzz")) {
    fprintf(stderr, "uso: %s run <arquivo> | fuzz <sementes> <casos> [semente]\n", argv[0]);
    return 2;
  }
  std::vector<Bytes> seeds;
  if (DIR *d = opendir(argv[2])) {
    while (dirent *f = readdir(d)) {
      std::string name = f->d_name;
      if (name.size() > 7 && name.substr(name.size() - 7) == ".stheme") seeds.push_back(file(std::string(argv[2]) + "/" + name));
    }
    closedir(d);
  }
  if (seeds.empty()) {
    fprintf(stderr, "stheme_fuzz: nenhum .stheme em %s\n", argv[2]);
    return 1;
  }
  const long cases = atol(argv[3]);
  const unsigned seed = argc > 4 ? (unsigned)atol(argv[4]) : 1;
  std::mt19937 rng(seed);
  for (const Bytes &s : seeds)
    if (!one(s, rng).empty()) fail(s, "semente recusada");
  // quantos casos terminam em cada motivo (sem os números), para ver até onde o fuzzing chega
  std::map<std::string, long> reasons;
  long accepted = 0;
  for (long i = 0; i < cases; i++) {
    std::string why = one(mutate(seeds[rng() % seeds.size()], rng), rng);
    if (why.empty()) {
      accepted++;
      continue;
    }
    // "data[1].fields[0].id: motivo" vira "data: motivo"; o que não é campo conhecido vira "<outro>"
    size_t colon = why.rfind(": ");
    std::string field = colon == std::string::npos ? "" : why.substr(0, colon), key;
    if (!field.rfind("theme.json", 0)) field = "theme.json";
    else field = field.substr(0, field.find_first_of(".[ "));
    if (!field.empty() && field != "theme.json" && field != "spec" && field != "theme" && field != "display" && field != "data" &&
        field != "layers")
      field = "<outro>";
    for (char c : field + (colon == std::string::npos ? why : why.substr(colon))) key += c >= '0' && c <= '9' ? '#' : c;
    reasons[key]++;
  }
  printf("stheme_fuzz: %ld casos (semente %u), %ld aceitos, %zu motivos de recusa diferentes\n", cases, seed, accepted, reasons.size());
  if (getenv("STHEME_MOTIVOS"))
    for (auto &r : reasons) printf("%8ld  %s\n", r.second, r.first.c_str());
  return 0;
}
