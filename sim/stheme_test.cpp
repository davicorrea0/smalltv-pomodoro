// Teste do leitor de pacotes .stheme, etapa 0 (smalltv-pomodoro/stheme.h), no WSL com ASan/UBSan:
//   bash sim/test-stheme.sh   compila em 32 bits (como o ESP8266) e roda este teste e o fuzzing
// Os pacotes de sim/stheme-seeds/ precisam ser aceitos com o que se sabe deles; cada defeito montado
// aqui precisa ser recusado com a mensagem certa; nenhum corte, leitura que falha ou byte trocado
// pode passar despercebido ou ler fora do pacote.
#include "../smalltv-pomodoro/stheme.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Bytes = std::string;

struct Mem {
  const Bytes *data;
  long failFrom = -1;  // leituras que tocam [failFrom, ...) falham
  bool failed = false;
};
static bool memRead(void *ctx, uint32_t pos, uint8_t *buf, uint16_t n) {
  Mem &m = *(Mem *)ctx;
  if (pos > m.data->size() || n > m.data->size() - pos) {
    fprintf(stderr, "stheme: leitura fora do pacote (%u + %u de %zu)\n", pos, n, m.data->size());
    abort();  // o leitor confere os tamanhos antes de pedir: pedir fora já é um erro
  }
  if (m.failFrom >= 0 && pos + n > (unsigned long)m.failFrom) return !(m.failed = true);
  memcpy(buf, m.data->data() + pos, n);
  return true;
}

static SthWork work;
static char err[160];
static bool readFailed;
static bool check(const Bytes &p, long failFrom = -1) {
  Mem m{&p, failFrom};
  memset(err, 0, sizeof err);
  bool ok = sthCheck(memRead, &m, (uint32_t)p.size(), work, err, sizeof err);
  readFailed = m.failed;
  return ok;
}
static int failures = 0;
static void accept(const Bytes &p, const char *what) {
  if (!check(p)) {
    printf("FALHA: %s foi recusado: %s\n", what, err);
    failures++;
  }
}
static void reject(const Bytes &p, const char *why, const char *what) {
  if (check(p)) {
    printf("FALHA: %s foi aceito\n", what);
    failures++;
  } else if (!strstr(err, why)) {
    printf("FALHA: %s: esperado \"%s\", veio \"%s\"\n", what, why, err);
    failures++;
  }
}

static Bytes file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    printf("FALHA: sem o arquivo %s\n", path.c_str());
    exit(1);
  }
  return Bytes(std::istreambuf_iterator<char>(f), {});
}
static void u16(Bytes &s, unsigned n) { s += char(n), s += char(n >> 8); }
static void u32(Bytes &s, unsigned long n) { u16(s, n & 0xFFFF), u16(s, n >> 16); }
struct Entry { Bytes path, data; };
static Bytes pack(const std::vector<Entry> &entries) {
  Bytes s = "STH1";
  u16(s, entries.size()), u16(s, 0);
  for (const Entry &e : entries) u16(s, e.path.size()), u32(s, e.data.size()), s += e.path, s += e.data;
  return s;
}
static Bytes replace(Bytes s, const Bytes &from, const Bytes &to) {
  size_t at = s.find(from);
  if (at == Bytes::npos) {
    printf("FALHA: \"%s\" não está no texto\n", from.c_str());
    exit(1);
  }
  return s.replace(at, from.size(), to);
}

static const Bytes HEAD = R"("theme":{"id":"teste","name":"Teste","author":"Eu","version":"1"},"display":{"width":240,"height":240,"background":"#102030"})";
static Bytes manifest(int spec, const Bytes &rest = R"("layers":[])") {
  return "{\"spec\":" + std::to_string(spec) + "," + HEAD + "," + rest + "}";
}
static Bytes withJson(const Bytes &json) { return pack({{"theme.json", json}}); }
// imagem STI2 de largura 5 e alfa, com as linhas dadas (u32 por linha, contado do fim da tabela)
static Bytes sti2(const std::vector<Bytes> &rows, uint8_t pal = 0, const Bytes &palette = "") {
  Bytes s = "STI2";
  u16(s, 5), u16(s, rows.size()), s += char(1), s += char(pal), s += Bytes(2, '\0'), s += palette;
  unsigned long at = 0;
  for (const Bytes &r : rows) u32(s, at), at += r.size();
  for (const Bytes &r : rows) s += r;
  return s;
}
static Bytes px(unsigned color, unsigned alpha) {
  Bytes s;
  u16(s, color), s += char(alpha);
  return s;
}

int main(int argc, char **argv) {
  const std::string seeds = argc > 1 ? argv[1] : "stheme-seeds";

  // ---- pacotes de verdade ----
  struct Known { const char *file; uint8_t spec, layers, data; uint16_t entries, sti1, sti2; const char *id, *name; };
  const Known known[] = {
    {"terminal-ops.stheme", 1, 13, 0, 1, 0, 0, "terminal-ops", "Terminal Ops"},
    {"live-status.stheme", 1, 8, 1, 1, 0, 0, "live-status", "Live Status"},
    {"pixel-room.stheme", 1, 5, 0, 6, 5, 0, "pixel-room", "Pixel Room"},
    {"imagens-spec2.stheme", 2, 7, 0, 7, 2, 4, "imagens-spec2", "Imagens STI1 e STI2"},
    {"dados-spec2.stheme", 2, 3, 2, 2, 0, 1, "dados-spec2", "Previs\xc3\xa3o e foco"},
  };
  for (const Known &k : known) {
    Bytes p = file(seeds + "/" + k.file);
    if (!check(p)) {
      printf("FALHA: %s recusado: %s\n", k.file, err);
      failures++;
      continue;
    }
    const SthInfo &f = work.info;
    if (f.spec != k.spec || f.layers != k.layers || f.data != k.data || f.entries != k.entries || f.sti1 != k.sti1 ||
        f.sti2 != k.sti2 || strcmp(f.id, k.id) || strcmp(f.name, k.name)) {
      printf("FALHA: %s: spec %u, %u camadas, %u fontes, %u entradas, %u STI1, %u STI2, \"%s\", \"%s\"\n", k.file, f.spec, f.layers,
             f.data, f.entries, f.sti1, f.sti2, f.id, f.name);
      failures++;
    }
    // todo corte do arquivo é recusado (o pixel-room, de 140 KB, nos primeiros e últimos 4 KB)
    for (size_t n = 0; n < p.size(); n += (n > 4096 && n + 4096 < p.size()) ? 997 : 1)
      if (check(p.substr(0, n))) {
        printf("FALHA: %s cortado em %zu bytes foi aceito\n", k.file, n);
        failures++;
        break;
      }
    // uma leitura que falha em qualquer ponto recusa o pacote (os pixels do STI1 só são lidos na etapa 1)
    for (size_t at = 0; at < p.size(); at += (p.size() > 8192 ? 509 : 7))
      if (check(p, (long)at) && readFailed) {
        printf("FALHA: %s com leitura falhando no byte %zu foi aceito\n", k.file, at);
        failures++;
        break;
      }
  }
  // o fundo do live-status, #0b1420 em RGB565
  check(file(seeds + "/live-status.stheme"));
  if (work.info.background != (((0x0b >> 3) << 11) | ((0x14 >> 2) << 5) | (0x20 >> 3))) {
    printf("FALHA: fundo 0x%04x\n", work.info.background);
    failures++;
  }

  // ---- as imagens de teste do smalltv-mod, num pacote spec 2 (e o mesmo pacote em spec 1) ----
  for (const char *form : {"alpha-sti1", "alpha-sti2-direct", "alpha-sti2-indexed", "opaque-sti1", "opaque-sti2-direct", "opaque-sti2-indexed"}) {
    Bytes image = file(seeds + "/sti/" + form + ".sti"), path = std::string("images/") + form + ".png.sti";
    accept(pack({{"theme.json", manifest(2)}, {path, image}}), form);
    if (image[3] == '2') reject(pack({{"theme.json", manifest(1)}, {path, image}}), "precisa de \"spec\": 2", form);
    else accept(pack({{"theme.json", manifest(1)}, {path, image}}), form);
    for (size_t n = 12; n < image.size(); n++)
      if (check(pack({{"theme.json", manifest(2)}, {path, image.substr(0, n)}}))) {
        printf("FALHA: %s cortada em %zu bytes foi aceita\n", form, n);
        failures++;
        break;
      }
  }

  // ---- contêiner ----
  const Bytes ok = withJson(manifest(1));
  accept(ok, "pacote mínimo");
  Bytes p = ok;
  p[0] = 'X';
  reject(p, "nao e um pacote .stheme", "magia");
  p = ok, p[6] = 1;
  reject(p, "cabecalho do pacote invalido", "reservado");
  p = ok, p[4] = 0;
  reject(p, "de 1 a 256 entradas", "sem entradas");
  p = ok, p[4] = 1, p[5] = 1;
  reject(p, "de 1 a 256 entradas", "257 entradas");
  reject(ok + "x", "sobram bytes", "sobra no fim");
  reject(pack({{"theme.json", manifest(1)}, {"theme.json", manifest(1)}}), "theme.json: caminho repetido", "caminho repetido");
  reject(pack({{"theme.json", manifest(1)}, {"a/b.sti", sti2({"\x04"})}, {"a/b.sti", sti2({"\x04"})}}), "a/b.sti: caminho repetido", "imagem repetida");
  for (const char *bad : {"../x.sti", "/x.sti", "x.sti/", "a//b.sti", "a/./b.sti", "a/../b.sti", "a\\b.sti", "a b.sti", "a:b.sti", "\x01.sti"})
    reject(pack({{"theme.json", manifest(1)}, {bad, sti2({"\x04"})}}), "entrada 2: caminho invalido", bad);
  reject(pack({{"theme.json", manifest(2)}, {std::string(117, 'a') + ".sti", sti2({"\x04"})}}), "caminho invalido", "caminho de 121 bytes");
  accept(pack({{"theme.json", manifest(2)}, {std::string(116, 'a') + ".sti", sti2({"\x04"})}}), "caminho de 120 bytes");
  reject(pack({{"x.png", "abc"}}), "x.png: alem do theme.json, so imagens .sti", "entrada que não é imagem");
  reject(pack({{"a/theme.json", manifest(1)}}), "alem do theme.json", "theme.json em subpasta");
  reject(pack({{"x.sti", sti2({"\x04"})}}), "falta o theme.json", "sem manifesto");
  reject(withJson(""), "theme.json: esperado de 1 a 16384 bytes", "manifesto vazio");
  {
    Bytes big = manifest(1, R"("layers":[],"x":")" + std::string(16384, 'a') + "\"");
    reject(withJson(big), "theme.json: esperado de 1 a 16384 bytes", "manifesto grande");
    Bytes exact = manifest(1);
    exact.insert(exact.size() - 1, std::string(16384 - exact.size(), ' '));
    accept(withJson(exact), "manifesto de 16384 bytes");
  }
  p = ok, p[10] = char(255), p[11] = char(255), p[12] = char(255), p[13] = char(255);
  reject(p, "passa do fim do pacote", "dados maiores que o pacote");
  p = ok, p[8] = char(200);
  reject(p, "caminho invalido", "caminho maior que o pacote");
  {
    std::vector<Entry> many{{"theme.json", manifest(2)}};
    for (int i = 0; i < 255; i++) many.push_back({"f/" + std::to_string(i) + ".sti", sti2({"\x04"})});
    accept(pack(many), "256 entradas");
    many.push_back({"f/x.sti", sti2({"\x04"})});
    reject(pack(many), "de 1 a 256 entradas", "257 entradas de verdade");
  }

  // ---- imagens ----
  const Bytes rows[3] = {Bytes("\x01") + "\x81" + px(0xf800, 255) + px(0x07e0, 128) + "\x40" + px(0x001f, 255),  // 2 transp., 2 cores, 1 repetida
                         "\x44" + px(0xffff, 255),                                                             // 5 repetidas
                         "\x04"};                                                                              // 5 transparentes
  auto img = [&](const Bytes &image) { return pack({{"theme.json", manifest(2)}, {"i.sti", image}}); };
  accept(img(sti2({rows[0], rows[1], rows[2]})), "STI2 direto");
  reject(img(sti2({rows[0], rows[1], "\x05"})), "imagem STI invalida", "série passa da largura");
  reject(img(sti2({rows[0], rows[1], "\x02"})), "imagem STI invalida", "linha termina antes");
  reject(img(sti2({rows[0].substr(0, 5), rows[1], rows[2]})), "imagem STI invalida", "lista de cores cortada");
  reject(img(sti2({"\x01", "\x02\x01", rows[2]})), "imagem STI invalida", "linha lê a seguinte");
  reject(img(sti2({rows[0], rows[1], "\xc4"})), "imagem STI invalida", "tipo 11");
  {
    Bytes s = sti2({rows[0], rows[1], rows[2]});
    s[12 + 8] = char(200);
    reject(img(s), "imagem STI invalida", "posição passa dos dados");
    s = sti2({rows[0], rows[1], rows[2]});
    s[12 + 4] = 0, s[12 + 8] = char(rows[0].size());
    accept(img(s), "linhas que dividem os dados");
    s = sti2({rows[0], rows[1], rows[2]});
    s[12 + 8] = 0, s[12 + 4] = char(rows[0].size() + 1);
    reject(img(s), "imagem STI invalida", "posições voltando");
    accept(img(sti2({rows[0] + "\x3f", rows[1], rows[2]})), "sobra no fim de uma linha");
    s = sti2({rows[0], rows[1], rows[2]});
    reject(img(s.substr(0, 12 + 8)), "imagem STI invalida", "tabela cortada");
    reject(pack({{"theme.json", manifest(2)}, {"i.sti", s.substr(0, 12 + 8)}, {"j.sti", s}}), "i.sti: imagem STI invalida", "tabela cortada com outra imagem depois");
    s = sti2({rows[0], rows[1], rows[2]}), s[4] = 0;
    reject(img(s), "imagem STI invalida", "largura 0");
    s = sti2({rows[0], rows[1], rows[2]}), s[4] = char(241);
    reject(img(s), "imagem STI invalida", "largura 241");
    s = sti2({rows[0], rows[1], rows[2]}), s[8] = 2;
    reject(img(s), "imagem STI invalida", "alfa 2");
    s = sti2({rows[0], rows[1], rows[2]}), s[11] = 1;
    reject(img(s), "imagem STI invalida", "reservado do STI2");
    s = sti2({rows[0], rows[1], rows[2]}), s[3] = '3';
    reject(img(s), "imagem STI invalida", "STI3");
  }
  {
    // sem alfa: série transparente é recusada
    Bytes s = "STI2";
    u16(s, 5), u16(s, 1), s += Bytes(4, '\0'), u32(s, 0), s += "\x04";
    reject(img(s), "imagem STI invalida", "transparente sem alfa");
    // com paleta de 2 cores (com alfa): índice 2 e nibble que sobra fora de 0
    const Bytes pal = px(0xf800, 255) + px(0x001f, 128);
    // 5 cores na lista (0 1 0 1 1: três bytes, o último com o nibble baixo em 0) e 5 repetidas
    accept(img(sti2({"\x84\x01\x01\x10", "\x44\x01", "\x04"}, 2, pal)), "STI2 com paleta");
    reject(img(sti2({"\x84\x01\x01\x20", "\x44\x01", "\x04"}, 2, pal)), "imagem STI invalida", "índice fora da paleta (lista)");
    reject(img(sti2({"\x84\x01\x01\x11", "\x44\x01", "\x04"}, 2, pal)), "imagem STI invalida", "nibble que sobra");
    reject(img(sti2({"\x84\x01\x01\x10", "\x44\x02", "\x04"}, 2, pal)), "imagem STI invalida", "índice fora da paleta (repetida)");
    reject(img(sti2({"\x84\x01\x01", "\x44\x01", "\x04"}, 2, pal)), "imagem STI invalida", "lista com paleta cortada");
    s = sti2({"\x04"}, 17, Bytes(17 * 3, '\0'));
    reject(img(s), "imagem STI invalida", "paleta de 17");
    // STI1 com o tamanho exato, nem um byte a mais ou a menos; reservado em 0
    Bytes s1 = "STI1";
    u16(s1, 2), u16(s1, 1), s1 += char(1), s1 += Bytes(3, '\0'), s1 += px(0xf800, 255) + px(0x07e0, 0);
    accept(img(s1), "STI1");
    reject(img(s1 + "x"), "imagem STI invalida", "STI1 com sobra");
    reject(img(s1.substr(0, s1.size() - 1)), "imagem STI invalida", "STI1 cortado");
    s1[9] = 1;
    reject(img(s1), "imagem STI invalida", "reservado do STI1");
  }

  // ---- theme.json ----
  auto j = [&](const Bytes &json, const char *why, const char *what) { reject(withJson(json), why, what); };
  auto jok = [&](const Bytes &json, const char *what) { accept(withJson(json), what); };
  jok(" \r\n\t" + manifest(2) + "\n ", "espaços em volta");
  j("[]", "esperado um objeto JSON", "lista na raiz");
  j(manifest(1) + "{}", "sobra conteudo depois do JSON", "sobra depois do objeto");
  j(manifest(1, R"("layers":[],"x":1)"), "x: campo desconhecido", "campo desconhecido");
  j(manifest(1, R"("layers":[],"spec":1)"), "spec: campo repetido", "campo repetido");
  j(manifest(1, R"("layers":[],"\u0000":1)"), "?: campo desconhecido", "chave com \\u0000");
  j(manifest(1, R"("layers":[],"spec":1)"), "spec: campo repetido", "chave com escape (é a mesma)");
  for (const char *spec : {"0", "3", "-1", "\"2\"", "2.0", "1e0", "true", "null", "99999999999"})
    j(replace(manifest(1), "\"spec\":1", std::string("\"spec\":") + spec), "spec: esperado um numero inteiro de 1 a 2", spec);
  j(replace(manifest(1), "\"spec\":1", "\"spec\":01"), "spec:", "zero à esquerda");
  j(replace(manifest(1), "\"spec\":1,", ""), "spec: esperado um numero", "sem spec");
  j(replace(manifest(1), ",\"layers\":[]", ""), "layers: esperada uma lista", "sem camadas");
  j(replace(manifest(1), "\"theme\":{\"id\":\"teste\",\"name\":\"Teste\",\"author\":\"Eu\",\"version\":\"1\"},", ""), "theme: esperado um objeto", "sem theme");
  j(replace(manifest(1), "\"id\":\"teste\"", "\"id\":\"te.ste\""), "theme.id: use letras", "id com ponto");
  j(replace(manifest(1), "\"id\":\"teste\"", "\"id\":\"" + std::string(49, 'a') + "\""), "theme.id: esperado texto de 1 a 48 bytes", "id longo");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"\""), "theme.name: esperado texto de 1 a 96 bytes", "nome vazio");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":null"), "theme.name: esperado um texto", "nome null");
  j(replace(manifest(1), "\"name\":\"Teste\",", ""), "theme.name: esperado um texto", "sem nome");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"" + std::string(97, 'a') + "\""), "de 1 a 96 bytes", "nome de 97 bytes");
  jok(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"" + std::string(96, 'a') + "\""), "nome de 96 bytes");
  // 48 "é" em \u são 96 bytes de UTF-8; 49 passam
  Bytes accents;
  for (int i = 0; i < 48; i++) accents += "\\u00e9";
  jok(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"" + accents + "\""), "nome com 96 bytes em \\u");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"" + accents + "\\u00e9\""), "de 1 a 96 bytes", "nome com 98 bytes em \\u");
  jok(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"\\ud83c\\udf45 tomate\""), "emoji em par de surrogates");
  check(withJson(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"\\ud83c\\udf45\\u00e3\"")));
  if (strcmp(work.info.name, "\xf0\x9f\x8d\x85\xc3\xa3")) {
    printf("FALHA: UTF-8 decodificado errado\n");
    failures++;
  }
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"\\ud83c\""), "JSON invalido", "surrogate sozinho");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"\\udf45\""), "JSON invalido", "segunda metade sozinha");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"a\\u0001\""), "theme.name: caracteres de controle", "controle em \\u");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"a\\u007f\""), "caracteres de controle", "DEL");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"a\tb\""), "JSON invalido", "tab cru no texto");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"a\\xb\""), "JSON invalido", "escape inválido");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":\"a\\u12g4\""), "JSON invalido", "\\u inválido");
  j(replace(manifest(1), "\"name\":\"Teste\"", "\"name\":'Teste'"), "theme.name: esperado um texto", "aspas simples");
  j(replace(manifest(1), "\"name\":\"Teste\"", "name:\"Teste\""), "JSON invalido", "chave sem aspas");
  j(replace(manifest(1), "\"version\":\"1\"", "\"version\":\"1\",\"x\":1"), "theme.x: campo desconhecido", "campo desconhecido em theme");
  j(replace(manifest(1), "\"version\":\"1\"", "\"version\":\"1\",\"id\":\"b\""), "theme.id: campo repetido", "id repetido");
  j(replace(manifest(1), "\"width\":240", "\"width\":239"), "display.width: esperado um numero inteiro de 240 a 240", "largura 239");
  j(replace(manifest(1), "\"height\":240", "\"height\":240.0"), "display.height", "altura 240.0");
  j(replace(manifest(1), "\"#102030\"", "\"#10203\""), "display.background: esperada uma cor #RRGGBB", "cor curta");
  j(replace(manifest(1), "\"#102030\"", "\"#10203g\""), "display.background", "cor com g");
  j(replace(manifest(1), "\"#102030\"", "\"102030\""), "display.background", "cor sem #");
  j(replace(manifest(1), "\"#102030\"", "\"#1020300\""), "display.background", "cor longa");
  jok(replace(manifest(1), "\"#102030\"", "\"#AbCdEf\""), "cor com maiúsculas");
  j(manifest(1, R"("layers":{})"), "layers: esperada uma lista", "camadas num objeto");
  j(manifest(1, R"("layers":[1])"), "layers[0]: esperado um objeto", "camada que não é objeto");
  {
    Bytes l = "\"layers\":[";
    for (int i = 0; i < 32; i++) l += std::string(i ? "," : "") + "{\"id\":\"c" + std::to_string(i) + "\"}";
    jok(manifest(1, l + "]"), "32 camadas");
    j(manifest(1, l + ",{}]"), "layers: esperada uma lista de ate 32 camadas", "33 camadas");
  }
  // aninhamento: raiz (1) > layers (2) > camada (3) > ... até 8 níveis
  jok(manifest(1, R"("layers":[{"a":[[[[{}]]]]}])"), "8 níveis");
  j(manifest(1, R"("layers":[{"a":[[[[[[]]]]]]}])"), "aninhamento maior que 8", "9 níveis");
  jok(manifest(1, R"("layers":[{"a":[1,-2.5e-3,0.0,true,false,null,"x\"y\\\/\b\f\n\r\t",{"b":{}}],"c":-0}])"), "valores variados nas camadas");
  for (const char *bad : {"[01]", "[1.]", "[.5]", "[1e]", "[+1]", "[-]", "[tru]", "[nul]", "[1,]", "[,1]", "['a']", "{'a':1}", "{a:1}", "{\"a\"}", "{\"a\":1,}", "[\"a]"})
    j(manifest(1, std::string("\"layers\":[{\"x\":") + bad + "}]"), "JSON invalido", bad);
  j(manifest(1, R"("layers":[{"x":1})"), "JSON invalido", "lista sem fechar");

  // ---- fontes de dados ----
  const Bytes src = R"({"id":"clima","url":"http://192.168.1.10/c.json","interval":300,"fields":[{"id":"temp","path":"main.temp"}]})";
  auto data = [&](const Bytes &sources, int spec = 1) { return manifest(spec, "\"data\":[" + sources + "],\"layers\":[]"); };
  jok(data(src), "uma fonte");
  jok(data(src + "," + replace(src, "clima", "b") + "," + replace(src, "clima", "c") + "," + replace(src, "clima", "d")), "4 fontes");
  j(data(src + "," + replace(src, "clima", "b") + "," + replace(src, "clima", "c") + "," + replace(src, "clima", "d") + "," + replace(src, "clima", "e")), "data: no maximo 4 fontes", "5 fontes");
  j(data(src + "," + src), "data[1].id: id de fonte repetido", "fonte repetida");
  j(manifest(1, R"("data":{},"layers":[])"), "data: esperada uma lista de fontes", "data num objeto");
  j(manifest(1, R"("data":null,"layers":[])"), "data: esperada uma lista", "data null");
  jok(manifest(1, R"("data":[],"layers":[])"), "data vazio");
  j(data(replace(src, "http://", "https://")), "data[0].insecureTls: use true", "https sem insecureTls");
  jok(data(replace(replace(src, "http://", "https://"), "\"interval\"", "\"insecureTls\":true,\"interval\"")), "https com insecureTls");
  j(data(replace(replace(src, "http://", "https://"), "\"interval\"", "\"insecureTls\":false,\"interval\"")), "insecureTls: use true", "https com insecureTls false");
  j(data(replace(replace(src, "http://", "https://"), "\"interval\"", "\"insecureTls\":null,\"interval\"")), "insecureTls: use true", "https com insecureTls null");
  jok(data(replace(src, "\"interval\"", "\"insecureTls\":null,\"interval\"")), "http com insecureTls null");
  j(data(replace(src, "\"interval\"", "\"insecureTls\":1,\"interval\"")), "insecureTls: esperado true ou false", "insecureTls 1");
  j(data(replace(src, "http://192.168.1.10/c.json", "ftp://x/c.json")), "data[0].url: so URLs http://", "ftp");
  j(data(replace(src, "http://192.168.1.10/c.json", "HTTP://x/c.json")), "so URLs http://", "HTTP maiúsculo");
  for (const char *url : {"http://", "http:///c", "http://?a", "http://#a"})
    j(data(replace(src, "http://192.168.1.10/c.json", url)), "data[0].url: falta o host", url);
  j(data(replace(src, "\"interval\":300", "\"interval\":9")), "data[0].interval: esperado um numero inteiro de 10 a 86400", "intervalo 9");
  j(data(replace(src, "\"interval\":300", "\"interval\":86401")), "interval", "intervalo 86401");
  j(data(replace(src, "\"interval\":300", "\"interval\":10.5")), "interval", "intervalo 10.5");
  j(data(replace(src, "\"interval\":300,", "")), "data[0].interval", "sem intervalo");
  j(data(replace(src, "\"id\":\"clima\"", "\"id\":\"cl.ima\"")), "data[0].id: use letras", "id com ponto");
  j(data(replace(src, "\"id\":\"clima\"", "\"id\":\"" + std::string(33, 'a') + "\"")), "data[0].id: esperado texto de 1 a 32", "id de 33");
  j(data(replace(src, "\"url\"", "\"x\":1,\"url\"")), "data[0].x: campo desconhecido", "campo desconhecido na fonte");
  j(data(replace(src, "/c.json", "/" + std::string(200, 'c'))), "data[0].url: esperado texto de 1 a 200", "URL longa");
  j(data(replace(src, "[{\"id\":\"temp\",\"path\":\"main.temp\"}]", "[]")), "data[0].fields: esperada uma lista de 1 a 8", "sem campos");
  {
    Bytes f = "[";
    for (int i = 0; i < 8; i++) f += std::string(i ? "," : "") + "{\"id\":\"f" + std::to_string(i) + "\",\"path\":\"a\"}";
    jok(data(replace(src, "[{\"id\":\"temp\",\"path\":\"main.temp\"}]", f + "]")), "8 campos");
    j(data(replace(src, "[{\"id\":\"temp\",\"path\":\"main.temp\"}]", f + ",{\"id\":\"x\",\"path\":\"a\"}]")), "fields: esperada uma lista de 1 a 8", "9 campos");
  }
  j(data(replace(src, "{\"id\":\"temp\",\"path\":\"main.temp\"}", "{\"id\":\"temp\",\"path\":\"a\"},{\"id\":\"temp\",\"path\":\"b\"}")), "data[0].fields[1].id: id de campo repetido", "campo repetido");
  for (const char *path : {"a..b", ".a", "a.", "a b", "a[0]"})
    j(data(replace(src, "main.temp", path)), "data[0].fields[0].path: use chaves", path);
  j(data(replace(src, "\"path\":\"main.temp\"", "\"path\":\"main.temp\",\"x\":1")), "data[0].fields[0].x: campo desconhecido", "campo desconhecido no campo");
  j(data(replace(src, "{\"id\":\"temp\",\"path\":\"main.temp\"}", "1")), "data[0].fields[0]: esperado um objeto", "campo que não é objeto");
  // time e device são nomes reservados só no spec 2 (lá eles são as fontes embutidas)
  jok(data(replace(src, "\"id\":\"clima\"", "\"id\":\"time\"")), "fonte time no spec 1");
  j(data(replace(src, "\"id\":\"clima\"", "\"id\":\"device\""), 2), "data[0].id: time e device sao nomes de fontes embutidas", "fonte device no spec 2");
  j(replace(data(replace(src, "\"id\":\"clima\"", "\"id\":\"time\"")), "{\"spec\":1,", "{") + "", "spec: esperado", "sem spec, mas com time");
  {
    // spec depois de data: a regra vale do mesmo jeito
    Bytes late = "{" + HEAD + ",\"data\":[" + replace(src, "\"id\":\"clima\"", "\"id\":\"time\"") + "],\"layers\":[],\"spec\":2}";
    j(late, "data[0].id: time e device", "spec 2 depois de data");
  }

  // ---- memória ----
  printf("stheme: SthWork tem %zu bytes, SthJson %zu\n", sizeof(SthWork), sizeof(SthJson));
  if (sizeof(SthWork) > 3072) {
    printf("FALHA: SthWork passou de 3 KB\n");
    failures++;
  }
  if (failures) {
    printf("stheme: %d falhas\n", failures);
    return 1;
  }
  puts("stheme: pacotes reais, contêiner, imagens STI1/STI2, theme.json e fontes de dados ok");
  return 0;
}
