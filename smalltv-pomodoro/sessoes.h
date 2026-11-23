#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Lista textual guardada em /sessoes.txt. Este cabeçalho não depende de Arduino e pode ser
// incluído diretamente pelos testes do host. A validação estrita não copia nomes nem mantém
// uma tabela deles na pilha: para achar repetidos, relê somente as linhas anteriores.
#define SESSOES_MAX 8
#define SESSOES_TEXTO_MAX 1024

typedef enum {
  SESSOES_OK = 0,
  SESSOES_CORPO_GRANDE,
  SESSOES_LISTA_CHEIA,
  SESSOES_SEM_NOVA_LINHA,
  SESSOES_LINHA_VAZIA,
  SESSOES_CAMPOS,
  SESSOES_NOME,
  SESSOES_NOME_REPETIDO,
  SESSOES_NUMERO
} SessoesErro;

typedef struct {
  const char *nome;
  size_t nome_len;
  uint8_t work, short_break, long_break, every, cycles;
} SessaoLinha;

typedef void (*SessaoVisita)(const SessaoLinha *sessao, void *contexto);

static inline int sessoesUtf8Nome(const char *p, size_t n) {
  size_t i = 0;
  int pontos = 0, tem_nao_espaco = 0;
  if (!n || n > 96 || p[0] == ' ' || p[n - 1] == ' ') return 0;
  while (i < n) {
    const unsigned char c = (unsigned char)p[i];
    uint32_t cp;
    size_t k;
    if (c < 0x80) {
      if (c < 0x20 || c == 0x7f) return 0;
      cp = c;
      k = 1;
    } else if (c >= 0xc2 && c <= 0xdf) {
      if (i + 1 >= n || ((unsigned char)p[i + 1] & 0xc0) != 0x80) return 0;
      cp = ((uint32_t)(c & 0x1f) << 6) | ((unsigned char)p[i + 1] & 0x3f);
      k = 2;
    } else if (c >= 0xe0 && c <= 0xef) {
      if (i + 2 >= n || ((unsigned char)p[i + 1] & 0xc0) != 0x80 ||
          ((unsigned char)p[i + 2] & 0xc0) != 0x80) return 0;
      const unsigned char b1 = (unsigned char)p[i + 1];
      if ((c == 0xe0 && b1 < 0xa0) || (c == 0xed && b1 >= 0xa0)) return 0;
      cp = ((uint32_t)(c & 0x0f) << 12) | ((uint32_t)(b1 & 0x3f) << 6) |
           ((unsigned char)p[i + 2] & 0x3f);
      k = 3;
    } else if (c >= 0xf0 && c <= 0xf4) {
      if (i + 3 >= n || ((unsigned char)p[i + 1] & 0xc0) != 0x80 ||
          ((unsigned char)p[i + 2] & 0xc0) != 0x80 || ((unsigned char)p[i + 3] & 0xc0) != 0x80) return 0;
      const unsigned char b1 = (unsigned char)p[i + 1];
      if ((c == 0xf0 && b1 < 0x90) || (c == 0xf4 && b1 > 0x8f)) return 0;
      cp = ((uint32_t)(c & 7) << 18) | ((uint32_t)(b1 & 0x3f) << 12) |
           ((uint32_t)((unsigned char)p[i + 2] & 0x3f) << 6) | ((unsigned char)p[i + 3] & 0x3f);
      k = 4;
    } else {
      return 0;
    }
    if (cp != 0x20) tem_nao_espaco = 1;
    if (++pontos > 24) return 0;
    i += k;
  }
  return pontos && tem_nao_espaco;
}

static inline int sessoesNumero(const char *p, size_t n, int minimo, int maximo, uint8_t *saida) {
  if (n < 1 || n > 2) return 0;  // 01 é válido; 007 não é, pois tem três dígitos.
  int v = 0;
  for (size_t i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return 0;
    v = v * 10 + p[i] - '0';
  }
  if (v < minimo || v > maximo) return 0;
  *saida = (uint8_t)v;
  return 1;
}

// Analisa uma linha sem CR/LF. O nome continua apontando para o buffer recebido.
static inline SessoesErro sessoesLinha(const char *p, size_t n, SessaoLinha *s) {
  const char *campo[6];
  size_t tam[6];
  int nf = 0;
  const char *inicio = p, *fim = p + n;
  if (!n) return SESSOES_LINHA_VAZIA;
  for (const char *q = p; q <= fim; q++) {
    if (q < fim && *q == '\r') return SESSOES_NOME;
    if (q == fim || *q == '\t') {
      if (nf >= 6) return SESSOES_CAMPOS;
      campo[nf] = inicio;
      tam[nf++] = (size_t)(q - inicio);
      inicio = q + 1;
    }
  }
  if (nf != 6) return SESSOES_CAMPOS;
  if (!sessoesUtf8Nome(campo[0], tam[0])) return SESSOES_NOME;
  s->nome = campo[0];
  s->nome_len = tam[0];
  if (!sessoesNumero(campo[1], tam[1], 1, 99, &s->work) ||
      !sessoesNumero(campo[2], tam[2], 1, 99, &s->short_break) ||
      !sessoesNumero(campo[3], tam[3], 1, 99, &s->long_break) ||
      !sessoesNumero(campo[4], tam[4], 1, 8, &s->every) ||
      !sessoesNumero(campo[5], tam[5], 0, 99, &s->cycles)) return SESSOES_NUMERO;
  return SESSOES_OK;
}

static inline int sessoesNomeAnterior(const char *texto, const char *linha, const SessaoLinha *atual) {
  const char *p = texto;
  while (p < linha) {
    const char *fim = (const char *)memchr(p, '\n', (size_t)(linha - p));
    if (!fim) break;
    size_t n = (size_t)(fim - p);
    if (n && p[n - 1] == '\r') n--;
    const char *tab = (const char *)memchr(p, '\t', n);
    if (tab && (size_t)(tab - p) == atual->nome_len && !memcmp(p, atual->nome, atual->nome_len)) return 1;
    p = fim + 1;
  }
  return 0;
}

static inline int sessoesNomeValidoAnterior(const char *texto, const char *linha, const SessaoLinha *atual) {
  const char *p = texto;
  while (p < linha) {
    const char *lf = (const char *)memchr(p, '\n', (size_t)(linha - p));
    if (!lf) break;
    size_t tam = (size_t)(lf - p);
    if (tam && p[tam - 1] == '\r') tam--;
    SessaoLinha anterior;
    if (sessoesLinha(p, tam, &anterior) == SESSOES_OK && anterior.nome_len == atual->nome_len &&
        !memcmp(anterior.nome, atual->nome, atual->nome_len)) return 1;
    p = lf + 1;
  }
  return 0;
}

static inline SessoesErro sessoesValidar(const char *texto, size_t n) {
  if (n > SESSOES_TEXTO_MAX) return SESSOES_CORPO_GRANDE;
  if (!n) return SESSOES_OK;
  const char *p = texto, *fim_texto = texto + n;
  int linhas = 0;
  while (p < fim_texto) {
    const char *lf = (const char *)memchr(p, '\n', (size_t)(fim_texto - p));
    if (!lf) return SESSOES_SEM_NOVA_LINHA;
    if (++linhas > SESSOES_MAX) return SESSOES_LISTA_CHEIA;
    size_t tam = (size_t)(lf - p);
    if (tam && p[tam - 1] == '\r') tam--;
    SessaoLinha s;
    SessoesErro e = sessoesLinha(p, tam, &s);
    if (e != SESSOES_OK) return e;
    if (sessoesNomeAnterior(texto, p, &s)) return SESSOES_NOME_REPETIDO;
    p = lf + 1;
  }
  return SESSOES_OK;
}

// Leitor tolerante: pula linhas ruins, aceita a última sem \n e entrega no máximo oito válidas.
static inline int sessoesLer(const char *texto, size_t n, SessaoVisita visita, void *contexto) {
  const char *p = texto, *fim_texto = texto + n;
  int validas = 0;
  while (p < fim_texto && validas < SESSOES_MAX) {
    const char *lf = (const char *)memchr(p, '\n', (size_t)(fim_texto - p));
    const char *fim = lf ? lf : fim_texto;
    size_t tam = (size_t)(fim - p);
    if (tam && p[tam - 1] == '\r') tam--;
    SessaoLinha s;
    if (sessoesLinha(p, tam, &s) == SESSOES_OK && !sessoesNomeValidoAnterior(texto, p, &s)) {
      if (visita) visita(&s, contexto);
      validas++;
    }
    if (!lf) break;
    p = lf + 1;
  }
  return validas;
}

static inline const char *sessoesErroTexto(SessoesErro e) {
  switch (e) {
    case SESSOES_CORPO_GRANDE: return "lista grande demais";
    case SESSOES_LISTA_CHEIA: return "lista cheia";
    case SESSOES_SEM_NOVA_LINHA: return "fim de linha invalido";
    case SESSOES_LINHA_VAZIA: return "linha vazia";
    case SESSOES_CAMPOS: return "linha incompleta";
    case SESSOES_NOME: return "nome invalido";
    case SESSOES_NOME_REPETIDO: return "nome repetido";
    case SESSOES_NUMERO: return "numero fora da faixa";
    default: return "ok";
  }
}
