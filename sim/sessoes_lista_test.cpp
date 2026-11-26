#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "../smalltv-pomodoro/sessoes.h"

static std::vector<std::string> nomes;
static std::string texto;

static void recolhe(const SessaoLinha *s, void *) {
  nomes.emplace_back(s->nome, s->nome_len);
  texto.append(s->nome, s->nome_len);
  char n[40];
  snprintf(n, sizeof n, "\t%u\t%u\t%u\t%u\t%u\n", s->work, s->short_break, s->long_break, s->every, s->cycles);
  texto += n;
}

static SessoesErro valida(const std::string &s) { return sessoesValidar(s.data(), s.size()); }

static std::string linha(const std::string &nome = "Sessão") { return nome + "\t25\t5\t20\t4\t4\n"; }

static std::string normaliza(const std::string &s) {
  texto.clear();
  sessoesLer(s.data(), s.size(), recolhe, nullptr);
  return texto;
}

static std::string jsonEsc(const char *p, size_t n) {
  std::string o;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)p[i];
    if (c == '"' || c == '\\') o += '\\';
    if (c >= 0x20) o += (char)c;
  }
  return o;
}

static void casosFixos() {
  std::string boa = linha("Café") + "Final\t50\t10\t25\t3\t03\r\n";
  assert(valida(boa) == SESSOES_OK);
  assert(normaliza(boa) == linha("Café") + "Final\t50\t10\t25\t3\t3\n");

  assert(valida(linha(std::string(24, 'a'))) == SESSOES_OK);
  assert(valida(linha(std::string(25, 'a'))) == SESSOES_NOME);
  std::string emoji;
  for (int i = 0; i < 24; i++) emoji += "\xF0\x9F\x98\x80";
  assert(emoji.size() == 96 && valida(linha(emoji)) == SESSOES_OK);
  assert(valida(linha(emoji + "a")) == SESSOES_NOME);

  std::string nove;
  for (int i = 0; i < 9; i++) nove += linha("S" + std::to_string(i));
  assert(valida(nove) == SESSOES_LISTA_CHEIA);
  assert(valida(std::string(1025, 'x')) == SESSOES_CORPO_GRANDE);
  assert(valida("A\t1\t2\t3\t4\n") == SESSOES_CAMPOS);
  assert(valida("A\t1\t2\t3\t4\t5\t6\n") == SESSOES_CAMPOS);
  assert(valida("A\tB\t1\t2\t3\t4\t5\n") == SESSOES_CAMPOS);  // TAB no nome vira campo extra
  assert(valida(std::string("A\x01\t1\t1\t1\t1\t1\n")) == SESSOES_NOME);
  assert(valida("A\rB\t1\t1\t1\t1\t1\n") == SESSOES_NOME);
  assert(valida(linha(" A")) == SESSOES_NOME);
  assert(valida(linha("A ")) == SESSOES_NOME);
  assert(valida(linha("   ")) == SESSOES_NOME);
  assert(valida("\t1\t1\t1\t1\t1\n") == SESSOES_NOME);
  assert(valida("\n") == SESSOES_LINHA_VAZIA);
  assert(valida("A\t1\t1\t1\t1\t1") == SESSOES_SEM_NOVA_LINHA);
  assert(valida(linha("\xC0\xAF")) == SESSOES_NOME);          // overlong
  assert(valida(linha("\xED\xA0\x80")) == SESSOES_NOME);    // surrogate
  assert(valida(linha(std::string("\xE2\x82", 2))) == SESSOES_NOME);  // truncado
  assert(valida("A\t0\t1\t1\t1\t1\n") == SESSOES_NUMERO);
  assert(valida("A\t100\t1\t1\t1\t1\n") == SESSOES_NUMERO);
  assert(valida("A\t1\t1\t1\t9\t1\n") == SESSOES_NUMERO);
  assert(valida("A\t-1\t1\t1\t1\t1\n") == SESSOES_NUMERO);
  assert(valida("A\tabc\t1\t1\t1\t1\n") == SESSOES_NUMERO);
  assert(valida("A\t007\t1\t1\t1\t1\n") == SESSOES_NUMERO);
  assert(valida("A\t01\t01\t01\t01\t00\n") == SESSOES_OK);  // zeros à esquerda: até dois dígitos
  assert(normaliza("A\t01\t01\t01\t01\t00\n") == "A\t1\t1\t1\t1\t0\n");
  assert(valida(linha("A") + linha("A")) == SESSOES_NOME_REPETIDO);
  assert(valida("") == SESSOES_OK && normaliza("").empty());

  std::string tolerante = linha("Um") + "ruim\n" + linha("Dois");
  for (int i = 0; i < 10; i++) tolerante += "V" + std::to_string(i) + "\t1\t1\t1\t1\t1\n";
  nomes.clear();
  int n = sessoesLer(tolerante.data(), tolerante.size() - 1, recolhe, nullptr);  // última sem \n
  assert(n == 8 && nomes[0] == "Um" && nomes[1] == "Dois" && nomes[7] == "V5");
  nomes.clear();
  assert(sessoesLer((linha("A") + linha("A") + linha("B")).data(), linha("A").size() * 2 + linha("B").size(), recolhe, nullptr) == 2);

  const std::string nome = "Café \"azul\" \\ casa";
  const std::string js = "{\"name\":\"" + jsonEsc(nome.data(), nome.size()) + "\"}";
  assert(js == "{\"name\":\"Café \\\"azul\\\" \\\\ casa\"}");
}

static uint32_t rnd = 0x51a7d00d;
static uint32_t aleatorio() { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }

static void fuzz(int casos) {
  const std::string sementes[] = {linha("Café"), linha(std::string(24, 'x')), "A\t01\t02\t03\t04\t00\r\n", ""};
  for (int c = 0; c < casos; c++) {
    std::string s = sementes[aleatorio() % 4];
    int mudancas = 1 + aleatorio() % 12;
    while (mudancas--) {
      int op = aleatorio() % 3;
      if (op == 0 && s.size() < 1100) s.insert(s.begin() + (s.empty() ? 0 : aleatorio() % (s.size() + 1)), (char)aleatorio());
      else if (op == 1 && !s.empty()) s.erase(s.begin() + aleatorio() % s.size());
      else if (!s.empty()) s[aleatorio() % s.size()] = (char)aleatorio();
    }
    SessoesErro e = valida(s);
    nomes.clear();
    int n = sessoesLer(s.data(), s.size(), recolhe, nullptr);
    assert(n >= 0 && n <= SESSOES_MAX);
    if (e == SESSOES_OK) assert(normaliza(s).size() <= SESSOES_TEXTO_MAX);
  }
}

int main(int argc, char **argv) {
  casosFixos();
  fuzz(argc > 1 ? atoi(argv[1]) : 2000);
  puts("sessoes_lista: ok (validação estrita, leitura tolerante, JSON e fuzz)");
}
