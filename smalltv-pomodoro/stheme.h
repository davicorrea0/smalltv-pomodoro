// stheme.h — leitor dos pacotes .stheme do smalltv-mod (spec 2, RFC giovi321/smalltv-mod#31), etapa 0.
//
// Confere o pacote inteiro sem desenhar nada: o contêiner STH1 (entradas, caminhos, tamanhos), o
// cabeçalho de cada imagem STI1/STI2 (e todas as linhas de cada STI2, como o smalltv-mod faz ao
// instalar) e o começo do theme.json: "spec", "theme", "display", "data" e quantas camadas há. O
// conteúdo das camadas só tem a sintaxe e o aninhamento conferidos; validá-lo é a etapa 2.
//
// Não aloca: lê o pacote por uma janela de 32 bytes e usa só a memória de SthWork (tamanho fixo,
// onde o chamador quiser), que não cresce com o pacote. Formato: docs do smalltv-mod
// (features/themes, "Device package format").
// Ainda não está ligado ao firmware: roda no PC (sim/stheme_test e sim/stheme_fuzz).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pgmspace.h>

static const uint16_t STH_MAX_ENTRIES = 256;
static const uint32_t STH_MAX_SIZE = 3UL << 20;  // 3 MiB, o mesmo teto do smalltv-mod
static const uint16_t STH_MAX_MANIFEST = 16384;
static const uint8_t STH_MAX_PATH = 120, STH_MAX_LAYERS = 32, STH_MAX_DATA = 4, STH_MAX_FIELDS = 8, STH_MAX_DEPTH = 8;

// lê n bytes do pacote a partir de pos; false se não conseguiu ler todos. No relógio, é aqui que
// se dá o yield() de vez em quando: um pacote grande tem milhares de leituras.
typedef bool (*SthRead)(void *ctx, uint32_t pos, uint8_t *buf, uint16_t n);

// o que a etapa 0 tira de um pacote aceito
struct SthInfo {
  uint8_t spec, layers, data;   // 1 ou 2; camadas; fontes de dados
  uint16_t entries, sti1, sti2;  // entradas do contêiner; imagens de cada tipo
  uint16_t background;           // RGB565
  uint32_t manifestPos, manifestLen;
  char id[49], name[97], author[97], version[33];
};

// memória de trabalho, de tamanho fixo (~2,8 KB): o hash e a posição de cada entrada, para achar
// caminhos repetidos, e os ids das fontes de dados e dos campos já vistos
struct SthWork {
  uint32_t hash[STH_MAX_ENTRIES], at[STH_MAX_ENTRIES];
  char dataId[STH_MAX_DATA][33], fieldId[STH_MAX_FIELDS][25], url[201];
  SthInfo info;
};

// ---------- leitura em sequência por uma janela ----------
struct SthIn {
  SthRead rd;
  void *ctx;
  uint32_t at, end;  // próxima leitura da janela e fim permitido
  uint8_t buf[32], len, i;
  bool bad;          // uma leitura falhou (o que é diferente de chegar ao fim)
};

static void sthIn(SthIn &r, SthRead rd, void *ctx, uint32_t at, uint32_t end) {
  r.rd = rd;
  r.ctx = ctx;
  r.at = at;
  r.end = end;
  r.len = r.i = 0;
  r.bad = false;
}

static int sthPeek(SthIn &r) {
  if (r.i == r.len) {
    if (r.bad || r.at >= r.end) return -1;
    uint32_t n = r.end - r.at;
    r.len = n > sizeof r.buf ? sizeof r.buf : (uint8_t)n;
    r.i = 0;
    if (!r.rd(r.ctx, r.at, r.buf, r.len)) {
      r.bad = true;
      r.len = 0;
      return -1;
    }
    r.at += r.len;
  }
  return r.buf[r.i];
}

static int sthGet(SthIn &r) {
  int c = sthPeek(r);
  if (c >= 0) r.i++;
  return c;
}

static uint32_t sthPos(const SthIn &r) { return r.at - r.len + r.i; }

// vai para `to`, aproveitando a janela quando `to` está nela
static void sthSeek(SthIn &r, uint32_t to) {
  if (to >= r.at - r.len && to <= r.at) {
    r.i = (uint8_t)(to - (r.at - r.len));
  } else {
    r.at = to;
    r.len = r.i = 0;
  }
}

// ---------- mensagens ----------
// "campo: motivo" em err (why na flash, ou na RAM com ram); devolve false para encerrar a conferência numa linha só
static bool sthErr(char *err, size_t n, const char *field, PGM_P why, bool ram = false) {
  if (!n) return false;
  size_t k = 0;
  if (field && *field) {
    for (; *field && k + 1 < n; field++) err[k++] = *field;
    if (k + 1 < n) err[k++] = ':';
    if (k + 1 < n) err[k++] = ' ';
  }
  for (char c; (c = ram ? *why : (char)pgm_read_byte(why)) && k + 1 < n; why++) err[k++] = c;
  err[k] = 0;
  return false;
}

// ---------- contêiner STH1 e imagens STI ----------
static uint16_t sthU16(const uint8_t *b) { return b[0] | b[1] << 8; }
static uint32_t sthU32(const uint8_t *b) { return sthU16(b) | (uint32_t)sthU16(b + 2) << 16; }

static bool sthIdChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// caminho seguro, a mesma regra do smalltv-mod: partes separadas por '/' com letras, números, '-',
// '_' e '.', sem parte vazia, "." ou ".." (portanto sem '/' no começo ou no fim)
static bool sthPathOk(const char *p, uint8_t n) {
  if (!n || n > STH_MAX_PATH) return false;
  uint8_t start = 0;
  for (uint8_t i = 0; i <= n; i++) {
    if (i == n || p[i] == '/') {
      uint8_t k = i - start;
      if (!k || (k == 1 && p[start] == '.') || (k == 2 && p[start] == '.' && p[start + 1] == '.')) return false;
      start = i + 1;
    } else if (!sthIdChar(p[i]) && p[i] != '.') {
      return false;
    }
  }
  return true;
}

static uint32_t sthHash(const char *s) {  // FNV-1a
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

// Uma linha do STI2: séries até cobrir a largura w. O primeiro byte de cada série traz o tipo nos
// 2 bits de cima e o comprimento menos 1 (1-64) nos outros 6: 00 transparente (só em imagem com
// alfa), 01 uma cor repetida, 10 tantas cores; 11 é reservado. Com paleta, a cor é um índice menor
// que pal: um byte na repetida, dois por byte (nibble alto primeiro) na lista, com o nibble baixo
// que sobra numa lista ímpar em 0. Sem paleta, RGB565 (+ alfa) por cor.
static bool sthRow(SthIn &r, uint16_t w, uint8_t stride, uint8_t pal) {
  for (uint16_t x = 0; x < w;) {
    int head = sthGet(r);
    if (head < 0) return false;
    const uint8_t kind = head >> 6, n = (head & 63) + 1;
    if (kind == 3 || (kind == 0 && stride != 3) || x + n > w) return false;
    if (kind == 1 && pal) {
      int k = sthGet(r);
      if (k < 0 || k >= pal) return false;
    } else if (kind == 2 && pal) {
      for (uint8_t j = 0; j < n; j += 2) {
        int b = sthGet(r);
        if (b < 0 || (b >> 4) >= pal || (j + 1 < n ? (b & 15) >= pal : (b & 15) != 0)) return false;
      }
    } else if (kind) {
      for (uint16_t k = kind == 1 ? stride : (uint16_t)n * stride; k; k--)
        if (sthGet(r) < 0) return false;
    }
    x += n;
  }
  return true;
}

// Depois do cabeçalho de 12 bytes e da paleta, um u32 por linha: onde ela começa, contado do fim da
// tabela. As posições nunca voltam; linhas com a mesma posição dividem os dados. Cada linha distinta
// precisa cobrir a largura sem passar do começo da seguinte (ou do fim da imagem).
static bool sthRows(SthRead rd, void *ctx, uint32_t at, uint32_t len, uint16_t w, uint16_t h, uint8_t stride, uint8_t pal) {
  const uint32_t table = 12 + (uint32_t)pal * stride, rows = table + 4UL * h;
  if (len < rows + 1) return false;
  const uint32_t data = len - rows;
  SthIn offs, runs;
  sthIn(offs, rd, ctx, at + table, at + rows);
  sthIn(runs, rd, ctx, at + rows, at + len);
  uint32_t start = 0;
  for (uint32_t y = 0; y <= h; y++) {
    uint32_t next = data;  // depois da última linha, o limite é o fim
    if (y < h) {
      uint8_t b[4];
      for (uint8_t k = 0; k < 4; k++) {
        int c = sthGet(offs);
        if (c < 0) return false;
        b[k] = (uint8_t)c;
      }
      next = sthU32(b);
      if (next >= data || next < start) return false;
      if (y > 0 && next == start) continue;  // divide os dados com a de cima
    }
    if (y > 0) {
      sthSeek(runs, at + rows + start);
      if (!sthRow(runs, w, stride, pal) || sthPos(runs) > at + rows + next) return false;
    }
    start = next;
  }
  return true;
}

// Imagem: "STI1" ou "STI2", u16 largura, u16 altura (1-240), u8 alfa (0 ou 1) e 3 bytes em 0, exceto
// que o STI2 guarda no primeiro o tamanho da paleta (0-16). STI1: os pixels crus, RGB565 (+ alfa),
// com o tamanho exato.
static bool sthImage(SthRead rd, void *ctx, uint32_t at, uint32_t len, bool &sti2) {
  uint8_t b[12];
  if (len < 12 || !rd(ctx, at, b, 12) || b[0] != 'S' || b[1] != 'T' || b[2] != 'I' || (b[3] != '1' && b[3] != '2')) return false;
  sti2 = b[3] == '2';
  const uint16_t w = sthU16(b + 4), h = sthU16(b + 6);
  const uint8_t stride = b[8] ? 3 : 2, pal = sti2 ? b[9] : 0;
  if (!w || w > 240 || !h || h > 240 || b[8] > 1 || (sti2 ? b[9] > 16 : b[9]) || b[10] || b[11]) return false;
  return sti2 ? sthRows(rd, ctx, at, len, w, h, stride, pal) : len == 12 + (uint32_t)w * h * stride;
}

// ---------- theme.json ----------
// Leitor de JSON estrito (o padrão, sem extensões) que não monta árvore: lê na ordem do arquivo e
// guarda só o que a etapa 0 usa. Mais rígido que o ArduinoJson do smalltv-mod em três pontos, que o
// empacotador nunca gera: chave repetida, "\u" de surrogate sozinho e caractere de controle cru num
// texto são recusados.
struct SthJson {
  SthIn in;
  SthWork *w;
  char *err;
  size_t errLen;
  char base[24];   // objeto atual nas mensagens ("theme", "data[1].fields[0]")
  char field[48];
};

static bool sthSyntax(SthJson &j) {
  if (j.in.bad) return sthErr(j.err, j.errLen, nullptr, PSTR("theme.json: nao foi possivel ler"));
  char at[40];
  snprintf_P(at, sizeof at, PSTR("theme.json (byte %u)"), (unsigned)(sthPos(j.in) - j.w->info.manifestPos));
  return sthErr(j.err, j.errLen, at, PSTR("JSON invalido"));
}

// "base.chave: motivo" (a chave só com caracteres visíveis)
static bool sthFail(SthJson &j, const char *key, PGM_P why, bool ram = false) {
  size_t k = 0;
  for (const char *s = j.base; *s && k + 1 < sizeof j.field; s++) j.field[k++] = *s;
  if (key && *key) {
    if (k && k + 1 < sizeof j.field) j.field[k++] = '.';
    for (; *key && k + 1 < sizeof j.field; key++) j.field[k++] = *key >= 32 && *key < 127 ? *key : '?';
  }
  j.field[k] = 0;
  if (!k) strcpy_P(j.field, PSTR("theme.json"));
  return sthErr(j.err, j.errLen, j.field, why, ram);
}

// idem, com o nome do campo na flash (um campo obrigatório que faltou)
static bool sthMissing(SthJson &j, PGM_P key, PGM_P why) {
  char k[16];
  strncpy_P(k, key, sizeof k - 1);
  k[sizeof k - 1] = 0;
  return sthFail(j, k, why);
}

static void sthWs(SthJson &j) {
  for (int c; (c = sthPeek(j.in)) == ' ' || c == '\t' || c == '\n' || c == '\r';) sthGet(j.in);
}

static bool sthHex4(SthJson &j, uint16_t &v) {
  v = 0;
  for (uint8_t k = 0; k < 4; k++) {
    int c = sthGet(j.in), d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    v = (uint16_t)(v << 4 | d);
  }
  return true;
}

// Texto já depois da aspa de abertura, decodificado em UTF-8: guarda até cap bytes em out (se houver)
// e conta todos em n.
static bool sthStr(SthJson &j, char *out, uint16_t cap, uint16_t &n) {
  n = 0;
  for (;;) {
    int c = sthGet(j.in);
    if (c < 0x20) return sthSyntax(j);  // inclui o fim do arquivo (-1)
    if (c == '"') return true;
    uint32_t cp = (uint32_t)c;
    uint8_t len = 1;
    if (c == '\\') {
      c = sthGet(j.in);
      if (c == 'u') {
        uint16_t u, lo;
        if (!sthHex4(j, u) || (u >= 0xDC00 && u <= 0xDFFF)) return sthSyntax(j);
        cp = u;
        if (u >= 0xD800 && u <= 0xDBFF) {
          if (sthGet(j.in) != '\\' || sthGet(j.in) != 'u' || !sthHex4(j, lo) || lo < 0xDC00 || lo > 0xDFFF) return sthSyntax(j);
          cp = 0x10000 + ((uint32_t)(u - 0xD800) << 10) + (lo - 0xDC00);
        }
        len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
      } else {
        PGM_P esc = PSTR("\"\"\\\\//b\bf\fn\nr\rt\t");  // pares: letra depois da barra, caractere
        while (pgm_read_byte(esc) && pgm_read_byte(esc) != c) esc += 2;
        if (!pgm_read_byte(esc)) return sthSyntax(j);
        cp = pgm_read_byte(esc + 1);
      }
    }
    uint8_t b[4];
    if (len == 1) b[0] = (uint8_t)cp;
    else {
      for (uint8_t k = len - 1; k; k--, cp >>= 6) b[k] = 0x80 | (cp & 63);
      b[0] = (uint8_t)((0xF00 >> len) | cp);  // 2: 110xxxxx, 3: 1110xxxx, 4: 11110xxx
    }
    for (uint8_t k = 0; k < len; k++, n++)
      if (out && n < cap) out[n] = (char)b[k];
  }
}

// true, false ou null, conforme a primeira letra (já conferida pelo chamador)
static bool sthWord(SthJson &j) {
  const int c = sthPeek(j.in);
  for (PGM_P w = c == 't' ? PSTR("true") : c == 'f' ? PSTR("false") : PSTR("null"); pgm_read_byte(w); w++)
    if (sthGet(j.in) != (int)pgm_read_byte(w)) return sthSyntax(j);
  return true;
}

// Número na gramática do JSON; v só vale se integer (sem parte decimal nem expoente). Um inteiro que
// não cabe em 9 dígitos vira 1000000000, fora de toda faixa aceita.
static bool sthNum(SthJson &j, int32_t &v, bool &integer) {
  int c = sthGet(j.in);
  const bool neg = c == '-';
  if (neg) c = sthGet(j.in);
  if (c < '0' || c > '9') return sthSyntax(j);
  uint32_t a = (uint32_t)(c - '0');
  if (c != '0')
    for (int d; (d = sthPeek(j.in)) >= '0' && d <= '9';) {
      sthGet(j.in);
      a = a >= 100000000 ? 1000000000 : a * 10 + (d - '0');
    }
  integer = true;
  if (sthPeek(j.in) == '.') {
    sthGet(j.in);
    integer = false;
    if ((c = sthPeek(j.in)) < '0' || c > '9') return sthSyntax(j);
    while ((c = sthPeek(j.in)) >= '0' && c <= '9') sthGet(j.in);
  }
  if ((c = sthPeek(j.in)) == 'e' || c == 'E') {
    sthGet(j.in);
    integer = false;
    if ((c = sthPeek(j.in)) == '+' || c == '-') sthGet(j.in);
    if ((c = sthPeek(j.in)) < '0' || c > '9') return sthSyntax(j);
    while ((c = sthPeek(j.in)) >= '0' && c <= '9') sthGet(j.in);
  }
  v = neg ? -(int32_t)a : (int32_t)a;
  return true;
}

// Próximo membro de um objeto já aberto: 1 com a chave lida (até 15 bytes; uma chave mais longa ou
// com \u0000 vira "\x7f", que nenhum campo tem) e a leitura no valor; 0 no fim; -1 em erro.
static int sthMember(SthJson &j, bool &first, char *key) {
  sthWs(j);
  int c = sthPeek(j.in);
  if (first && c == '}') {
    sthGet(j.in);
    return 0;
  }
  if (!first) {
    c = sthGet(j.in);
    if (c == '}') return 0;
    if (c != ',') return sthSyntax(j), -1;
    sthWs(j);
  }
  first = false;
  uint16_t n;
  if (sthGet(j.in) != '"' || !sthStr(j, key, 15, n)) return sthSyntax(j), -1;
  key[n < 15 ? n : 15] = 0;
  if (n > 15 || strlen(key) != n) key[0] = 127, key[1] = 0;
  sthWs(j);
  if (sthGet(j.in) != ':') return sthSyntax(j), -1;
  sthWs(j);
  return 1;
}

// próximo item de uma lista já aberta: 1 com a leitura no item, 0 no fim, -1 em erro
static int sthItem(SthJson &j, bool &first) {
  sthWs(j);
  int c = sthPeek(j.in);
  if (first && c == ']') {
    sthGet(j.in);
    return 0;
  }
  if (!first) {
    c = sthGet(j.in);
    if (c == ']') return 0;
    if (c != ',') return sthSyntax(j), -1;
    sthWs(j);
  }
  first = false;
  return 1;
}

static bool sthDeep(SthJson &j) { return sthErr(j.err, j.errLen, nullptr, PSTR("theme.json: aninhamento maior que 8 niveis")); }

// pula um valor qualquer; depth = nível que ele teria se fosse objeto ou lista (a raiz é o 1)
static bool sthSkip(SthJson &j, uint8_t depth) {
  int c = sthPeek(j.in);
  if (c == '{' || c == '[') {
    if (depth > STH_MAX_DEPTH) return sthDeep(j);
    sthGet(j.in);
    char key[16];
    for (bool first = true;;) {
      int m = c == '{' ? sthMember(j, first, key) : sthItem(j, first);
      if (m <= 0) return !m;
      if (!sthSkip(j, depth + 1)) return false;
    }
  }
  uint16_t n;
  int32_t v;
  bool integer;
  if (c == '"') return sthGet(j.in), sthStr(j, nullptr, 0, n);
  if (c == 't' || c == 'f' || c == 'n') return sthWord(j);
  return sthNum(j, v, integer);
}

static bool sthOpen(SthJson &j, char open) {
  if (sthPeek(j.in) != open) return false;
  sthGet(j.in);
  return true;
}

// texto de 1 a max bytes, sem caracteres de controle; out com max + 1 bytes
static bool sthText(SthJson &j, const char *key, char *out, uint16_t max) {
  uint16_t n;
  if (sthPeek(j.in) != '"') return sthFail(j, key, PSTR("esperado um texto"));
  sthGet(j.in);
  if (!sthStr(j, out, max, n)) return false;
  if (!n || n > max) {
    char why[40];
    snprintf_P(why, sizeof why, PSTR("esperado texto de 1 a %u bytes"), (unsigned)max);
    return sthFail(j, key, why, true);
  }
  out[n] = 0;
  for (uint16_t k = 0; k < n; k++)
    if ((uint8_t)out[k] < 32 || out[k] == 127) return sthFail(j, key, PSTR("caracteres de controle nao sao permitidos"));
  return true;
}

static bool sthInt(SthJson &j, const char *key, int32_t lo, int32_t hi, int32_t &v) {
  int c = sthPeek(j.in);
  if (c == '-' || (c >= '0' && c <= '9')) {
    bool integer;
    if (!sthNum(j, v, integer)) return false;
    if (integer && v >= lo && v <= hi) return true;
  }
  char why[48];
  snprintf_P(why, sizeof why, PSTR("esperado um numero inteiro de %ld a %ld"), (long)lo, (long)hi);
  return sthFail(j, key, why, true);
}

static bool sthIdOk(const char *s) {
  for (; *s; s++)
    if (!sthIdChar(*s)) return false;
  return true;
}

// chave igual a um nome da flash
static bool sthIs(const char *key, PGM_P name) { return !strcmp_P(key, name); }

static bool sthTheme(SthJson &j) {
  SthInfo &f = j.w->info;
  if (!sthOpen(j, '{')) return sthFail(j, nullptr, PSTR("esperado um objeto"));
  char key[16];
  uint8_t seen = 0;
  for (bool first = true;;) {
    int m = sthMember(j, first, key);
    if (m < 0) return false;
    if (!m) break;
    uint8_t bit = sthIs(key, PSTR("id")) ? 1 : sthIs(key, PSTR("name")) ? 2 : sthIs(key, PSTR("author")) ? 4 : sthIs(key, PSTR("version")) ? 8 : 0;
    if (!bit) return sthFail(j, key, PSTR("campo desconhecido"));
    if (seen & bit) return sthFail(j, key, PSTR("campo repetido"));
    seen |= bit;
    if (bit == 1 && (!sthText(j, key, f.id, 48))) return false;
    if (bit == 1 && !sthIdOk(f.id)) return sthFail(j, key, PSTR("use letras e numeros ASCII, '-' ou '_'"));
    if (bit == 2 && !sthText(j, key, f.name, 96)) return false;
    if (bit == 4 && !sthText(j, key, f.author, 96)) return false;
    if (bit == 8 && !sthText(j, key, f.version, 32)) return false;
  }
  if (!(seen & 1)) return sthMissing(j, PSTR("id"), PSTR("esperado um texto"));
  if (!(seen & 2)) return sthMissing(j, PSTR("name"), PSTR("esperado um texto"));
  if (!(seen & 4)) return sthMissing(j, PSTR("author"), PSTR("esperado um texto"));
  if (!(seen & 8)) return sthMissing(j, PSTR("version"), PSTR("esperado um texto"));
  return true;
}

static bool sthDisplay(SthJson &j) {
  if (!sthOpen(j, '{')) return sthFail(j, nullptr, PSTR("esperado um objeto"));
  char key[16], color[8];
  uint8_t seen = 0;
  int32_t v;
  for (bool first = true;;) {
    int m = sthMember(j, first, key);
    if (m < 0) return false;
    if (!m) break;
    uint8_t bit = sthIs(key, PSTR("width")) ? 1 : sthIs(key, PSTR("height")) ? 2 : sthIs(key, PSTR("background")) ? 4 : 0;
    if (!bit) return sthFail(j, key, PSTR("campo desconhecido"));
    if (seen & bit) return sthFail(j, key, PSTR("campo repetido"));
    seen |= bit;
    if (bit < 4 && !sthInt(j, key, 240, 240, v)) return false;
    if (bit == 4) {
      uint16_t n;
      uint32_t rgb = 0;
      bool ok = sthPeek(j.in) == '"';
      if (ok) {
        sthGet(j.in);
        if (!sthStr(j, color, 7, n)) return false;
        ok = n == 7 && color[0] == '#';
        for (uint8_t k = 1; ok && k < 7; k++) {
          char c = color[k];
          int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
          ok = d >= 0;
          rgb = rgb << 4 | (uint32_t)d;
        }
      }
      if (!ok) return sthFail(j, key, PSTR("esperada uma cor #RRGGBB"));
      j.w->info.background = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
    }
  }
  if (!(seen & 1)) return sthMissing(j, PSTR("width"), PSTR("esperado um numero inteiro de 240 a 240"));
  if (!(seen & 2)) return sthMissing(j, PSTR("height"), PSTR("esperado um numero inteiro de 240 a 240"));
  if (!(seen & 4)) return sthMissing(j, PSTR("background"), PSTR("esperada uma cor #RRGGBB"));
  return true;
}

// caminho de um campo no JSON da fonte: chaves não vazias separadas por '.'
static bool sthDotted(const char *s) {
  for (bool empty = true;; s++) {
    if (!*s || *s == '.') {
      if (empty) return false;
      if (!*s) return true;
      empty = true;
    } else if (!sthIdChar(*s)) {
      return false;
    } else {
      empty = false;
    }
  }
}

// um item de "fields": {"id": ..., "path": ...}; nf = quantos a fonte já tem
static bool sthField(SthJson &j, uint8_t nf) {
  char key[16], path[97];
  char *id = j.w->fieldId[nf];
  if (!sthOpen(j, '{')) return sthFail(j, nullptr, PSTR("esperado um objeto"));
  uint8_t seen = 0;
  for (bool first = true;;) {
    int m = sthMember(j, first, key);
    if (m < 0) return false;
    if (!m) break;
    uint8_t bit = sthIs(key, PSTR("id")) ? 1 : sthIs(key, PSTR("path")) ? 2 : 0;
    if (!bit) return sthFail(j, key, PSTR("campo desconhecido"));
    if (seen & bit) return sthFail(j, key, PSTR("campo repetido"));
    seen |= bit;
    if (bit == 1 && !sthText(j, key, id, 24)) return false;
    if (bit == 1 && !sthIdOk(id)) return sthFail(j, key, PSTR("use letras e numeros ASCII, '-' ou '_'"));
    if (bit == 2 && !sthText(j, key, path, 96)) return false;
    if (bit == 2 && !sthDotted(path)) return sthFail(j, key, PSTR("use chaves de objeto separadas por '.'"));
  }
  if (!(seen & 1)) return sthMissing(j, PSTR("id"), PSTR("esperado um texto"));
  if (!(seen & 2)) return sthMissing(j, PSTR("path"), PSTR("esperado um texto"));
  for (uint8_t k = 0; k < nf; k++)
    if (!strcmp(j.w->fieldId[k], id)) return sthMissing(j, PSTR("id"), PSTR("id de campo repetido"));
  return true;
}

// Uma fonte de dados (só conferida: buscar é outra etapa). Como no smalltv-mod: http:// ou https://
// com host, e https só com "insecureTls": true, porque o certificado não é conferido.
static bool sthSource(SthJson &j, uint8_t ns, bool &reserved) {
  SthWork &w = *j.w;
  char key[16], *id = w.dataId[ns];
  if (!sthOpen(j, '{')) return sthFail(j, nullptr, PSTR("esperado um objeto"));
  uint8_t seen = 0, nf = 0;
  bool insecure = false;
  int32_t v;
  for (bool first = true;;) {
    int m = sthMember(j, first, key);
    if (m < 0) return false;
    if (!m) break;
    uint8_t bit = sthIs(key, PSTR("id")) ? 1 : sthIs(key, PSTR("url")) ? 2 : sthIs(key, PSTR("interval")) ? 4 : sthIs(key, PSTR("insecureTls")) ? 8 : sthIs(key, PSTR("fields")) ? 16 : 0;
    if (!bit) return sthFail(j, key, PSTR("campo desconhecido"));
    if (seen & bit) return sthFail(j, key, PSTR("campo repetido"));
    seen |= bit;
    if (bit == 1 && !sthText(j, key, id, 32)) return false;
    if (bit == 1 && !sthIdOk(id)) return sthFail(j, key, PSTR("use letras e numeros ASCII, '-' ou '_'"));
    if (bit == 2 && !sthText(j, key, w.url, 200)) return false;
    if (bit == 4 && !sthInt(j, key, 10, 86400, v)) return false;
    if (bit == 8) {
      int c = sthPeek(j.in);
      // null vale como ausente, como no smalltv-mod
      if (c != 't' && c != 'f' && c != 'n') return sthFail(j, key, PSTR("esperado true ou false"));
      insecure = c == 't';
      if (!sthWord(j)) return false;
    }
    if (bit == 16) {
      size_t baseLen = strlen(j.base);
      if (!sthOpen(j, '[')) return sthFail(j, key, PSTR("esperada uma lista de 1 a 8 campos"));
      for (bool firstField = true;;) {
        int f = sthItem(j, firstField);
        if (f < 0) return false;
        if (!f) break;
        if (nf == STH_MAX_FIELDS) return sthFail(j, key, PSTR("esperada uma lista de 1 a 8 campos"));
        snprintf_P(j.base + baseLen, sizeof j.base - baseLen, PSTR(".fields[%u]"), (unsigned)nf);
        if (!sthField(j, nf)) return false;
        j.base[baseLen] = 0;
        nf++;
      }
      if (!nf) return sthFail(j, key, PSTR("esperada uma lista de 1 a 8 campos"));
    }
  }
  if (!(seen & 1)) return sthMissing(j, PSTR("id"), PSTR("esperado um texto"));
  if (!(seen & 2)) return sthMissing(j, PSTR("url"), PSTR("esperado um texto"));
  if (!(seen & 4)) return sthMissing(j, PSTR("interval"), PSTR("esperado um numero inteiro de 10 a 86400"));
  if (!(seen & 16)) return sthMissing(j, PSTR("fields"), PSTR("esperada uma lista de 1 a 8 campos"));
  const char *u = w.url;
  size_t host = !strncmp_P(u, PSTR("https://"), 8) ? 8 : !strncmp_P(u, PSTR("http://"), 7) ? 7 : 0;
  if (!host) return sthMissing(j, PSTR("url"), PSTR("so URLs http:// e https://"));
  if (host == 8 && !insecure) return sthMissing(j, PSTR("insecureTls"), PSTR("use true para aceitar HTTPS sem conferir o certificado"));
  if (!u[host] || u[host] == '/' || u[host] == '?' || u[host] == '#') return sthMissing(j, PSTR("url"), PSTR("falta o host"));
  for (uint8_t k = 0; k < ns; k++)
    if (!strcmp(w.dataId[k], id)) return sthMissing(j, PSTR("id"), PSTR("id de fonte repetido"));
  reserved = !strcmp_P(id, PSTR("time")) || !strcmp_P(id, PSTR("device"));
  return true;
}

static bool sthManifest(SthJson &j) {
  SthInfo &f = j.w->info;
  sthWs(j);
  if (!sthOpen(j, '{')) return sthPeek(j.in) < 0 && j.in.bad ? sthSyntax(j) : sthErr(j.err, j.errLen, nullptr, PSTR("theme.json: esperado um objeto JSON"));
  char key[16];
  uint8_t seen = 0, reservedAt = 255;
  int32_t v;
  for (bool first = true;;) {
    j.base[0] = 0;
    int m = sthMember(j, first, key);
    if (m < 0) return false;
    if (!m) break;
    uint8_t bit = sthIs(key, PSTR("spec")) ? 1 : sthIs(key, PSTR("theme")) ? 2 : sthIs(key, PSTR("display")) ? 4 : sthIs(key, PSTR("data")) ? 8 : sthIs(key, PSTR("layers")) ? 16 : 0;
    if (!bit) return sthFail(j, key, PSTR("campo desconhecido"));
    if (seen & bit) return sthFail(j, key, PSTR("campo repetido"));
    seen |= bit;
    if (bit == 1) {
      if (!sthInt(j, key, 1, 2, v)) return false;
      f.spec = (uint8_t)v;
    } else if (bit == 2 || bit == 4) {
      strcpy(j.base, key);
      if (!(bit == 2 ? sthTheme(j) : sthDisplay(j))) return false;
    } else if (bit == 8) {
      if (!sthOpen(j, '[')) return sthFail(j, key, PSTR("esperada uma lista de fontes"));
      for (bool firstSource = true;;) {
        int s = sthItem(j, firstSource);
        if (s < 0) return false;
        if (!s) break;
        if (f.data == STH_MAX_DATA) return sthFail(j, key, PSTR("no maximo 4 fontes"));
        snprintf_P(j.base, sizeof j.base, PSTR("data[%u]"), (unsigned)f.data);
        bool reserved = false;
        if (!sthSource(j, f.data, reserved)) return false;
        if (reserved && reservedAt == 255) reservedAt = f.data;
        j.base[0] = 0;
        f.data++;
      }
    } else {
      if (!sthOpen(j, '[')) return sthFail(j, key, PSTR("esperada uma lista de ate 32 camadas"));
      for (bool firstLayer = true;;) {
        int l = sthItem(j, firstLayer);
        if (l < 0) return false;
        if (!l) break;
        if (f.layers == STH_MAX_LAYERS) return sthFail(j, key, PSTR("esperada uma lista de ate 32 camadas"));
        if (sthPeek(j.in) != '{') {
          snprintf_P(j.base, sizeof j.base, PSTR("layers[%u]"), (unsigned)f.layers);
          return sthFail(j, nullptr, PSTR("esperado um objeto"));
        }
        if (!sthSkip(j, 3)) return false;  // raiz (1) > lista (2) > camada (3)
        f.layers++;
      }
    }
  }
  sthWs(j);
  if (sthPeek(j.in) >= 0) return sthErr(j.err, j.errLen, nullptr, PSTR("theme.json: sobra conteudo depois do JSON"));
  if (j.in.bad) return sthSyntax(j);
  j.base[0] = 0;
  if (!(seen & 1)) return sthMissing(j, PSTR("spec"), PSTR("esperado um numero inteiro de 1 a 2"));
  if (!(seen & 2)) return sthMissing(j, PSTR("theme"), PSTR("esperado um objeto"));
  if (!(seen & 4)) return sthMissing(j, PSTR("display"), PSTR("esperado um objeto"));
  if (!(seen & 16)) return sthMissing(j, PSTR("layers"), PSTR("esperada uma lista de ate 32 camadas"));
  if (f.spec >= 2 && reservedAt != 255) {
    snprintf_P(j.base, sizeof j.base, PSTR("data[%u]"), (unsigned)reservedAt);
    return sthMissing(j, PSTR("id"), PSTR("time e device sao nomes de fontes embutidas"));
  }
  return true;
}

// ---------- o pacote ----------
// Confere o pacote inteiro (size bytes, lidos por rd). true = aceito, com w.info preenchido; false =
// recusado, com o motivo em err ("theme.name: esperado texto de 1 a 96 bytes").
// Contêiner: "STH1", u16 entradas (1-256), u16 em 0; cada entrada: u16 tamanho do caminho, u32
// tamanho dos dados, o caminho (ASCII) e os dados. Tudo little-endian, sem sobra no fim. Um
// theme.json (1-16384 bytes); o resto são imagens .sti. STI2 só num pacote "spec": 2.
static bool sthCheck(SthRead rd, void *ctx, uint32_t size, SthWork &w, char *err, size_t errLen) {
  SthInfo &f = w.info;
  memset(&f, 0, sizeof f);
  uint8_t b[8];
  if (size < 8 || !rd(ctx, 0, b, 8) || b[0] != 'S' || b[1] != 'T' || b[2] != 'H' || b[3] != '1')
    return sthErr(err, errLen, nullptr, PSTR("nao e um pacote .stheme (STH1)"));
  if (size > STH_MAX_SIZE) return sthErr(err, errLen, nullptr, PSTR("pacote maior que 3 MB"));
  const uint16_t n = sthU16(b + 4);
  if (sthU16(b + 6)) return sthErr(err, errLen, nullptr, PSTR("cabecalho do pacote invalido"));
  if (!n || n > STH_MAX_ENTRIES) return sthErr(err, errLen, nullptr, PSTR("o pacote precisa ter de 1 a 256 entradas"));
  char path[STH_MAX_PATH + 1], other[STH_MAX_PATH + 1], entry[16];
  uint32_t pos = 8;
  uint16_t firstSti2 = 0xFFFF;
  bool manifest = false;
  for (uint16_t i = 0; i < n; i++) {
    snprintf_P(entry, sizeof entry, PSTR("entrada %u"), (unsigned)i + 1);
    if (size - pos < 6 || !rd(ctx, pos, b, 6)) return sthErr(err, errLen, entry, PSTR("pacote cortado"));
    const uint16_t plen = sthU16(b);
    const uint32_t len = sthU32(b + 2);
    if (!plen || plen > STH_MAX_PATH || plen > size - pos - 6) return sthErr(err, errLen, entry, PSTR("caminho invalido"));
    if (!rd(ctx, pos + 6, (uint8_t *)path, plen)) return sthErr(err, errLen, entry, PSTR("pacote cortado"));
    if (!sthPathOk(path, (uint8_t)plen)) return sthErr(err, errLen, entry, PSTR("caminho invalido"));
    path[plen] = 0;
    const uint32_t at = pos + 6 + plen;
    if (len > size - at) return sthErr(err, errLen, path, PSTR("passa do fim do pacote"));
    const uint32_t h = sthHash(path);
    for (uint16_t k = 0; k < i; k++) {
      if (w.hash[k] != h) continue;
      if (!rd(ctx, w.at[k], b, 2) || (sthU16(b) == plen && !rd(ctx, w.at[k] + 6, (uint8_t *)other, plen))) return sthErr(err, errLen, entry, PSTR("pacote cortado"));
      if (sthU16(b) == plen && !memcmp(other, path, plen)) return sthErr(err, errLen, path, PSTR("caminho repetido"));
    }
    w.hash[i] = h;
    w.at[i] = pos;
    if (!strcmp_P(path, PSTR("theme.json"))) {
      if (!len || len > STH_MAX_MANIFEST) return sthErr(err, errLen, path, PSTR("esperado de 1 a 16384 bytes"));
      f.manifestPos = at;
      f.manifestLen = len;
      manifest = true;
    } else {
      bool sti2;
      if (plen < 4 || strcmp_P(path + plen - 4, PSTR(".sti"))) return sthErr(err, errLen, path, PSTR("alem do theme.json, so imagens .sti"));
      if (!sthImage(rd, ctx, at, len, sti2)) return sthErr(err, errLen, path, PSTR("imagem STI invalida"));
      if (sti2 && firstSti2 == 0xFFFF) firstSti2 = i;
      if (sti2) f.sti2++;
      else f.sti1++;
    }
    pos = at + len;
  }
  if (pos != size) return sthErr(err, errLen, nullptr, PSTR("sobram bytes depois da ultima entrada"));
  if (!manifest) return sthErr(err, errLen, nullptr, PSTR("falta o theme.json"));
  f.entries = n;
  SthJson j;
  sthIn(j.in, rd, ctx, f.manifestPos, f.manifestPos + f.manifestLen);
  j.w = &w;
  j.err = err;
  j.errLen = errLen;
  j.base[0] = 0;
  if (!sthManifest(j)) return false;
  if (f.spec < 2 && firstSti2 != 0xFFFF) {  // o firmware que só lê STI1 recusa spec 2: o pacote diz o que precisa
    const uint16_t plen = rd(ctx, w.at[firstSti2], b, 2) ? sthU16(b) : 0;
    if (!plen || !rd(ctx, w.at[firstSti2] + 6, (uint8_t *)path, plen)) return sthErr(err, errLen, nullptr, PSTR("pacote cortado"));
    path[plen] = 0;
    return sthErr(err, errLen, path, PSTR("imagem STI2 precisa de \"spec\": 2"));
  }
  return true;
}
