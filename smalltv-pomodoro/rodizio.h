#pragma once

#include <stdint.h>
#include <string.h>
#include <stddef.h>

static inline bool rodCharId(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static inline bool rodItemValido(const char *p, size_t n) {
  if (!p || n < 3 || p[1] != ':') return false;
  if (p[0] == 't') {
    for (size_t i = 2; i < n; i++) if (p[i] < '0' || p[i] > '9') return false;
    return true;
  }
  if (p[0] != 'p') return false;
  for (size_t i = 2; i < n; i++) if (!rodCharId(p[i])) return false;
  return true;
}

static inline bool rodCopiaItem(const char *p, size_t n, char *out, size_t cap) {
  if (!rodItemValido(p, n) || !out || cap <= n) return false;
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

// Devolve o item válido depois da primeira ocorrência de atual. Se atual não existir, começa no primeiro.
static inline bool rodProximo(const char *lista, const char *atual, char *out, size_t cap) {
  if (!lista || !out || !cap) return false;
  out[0] = 0;
  size_t total = strlen(lista), inicio = 0, atualN = atual ? strlen(atual) : 0;
  bool achou = false;
  for (size_t p = 0; p <= total;) {
    size_t e = p;
    while (e < total && lista[e] != ',') e++;
    if (!achou && atualN == e - p && atualN && !memcmp(lista + p, atual, atualN)) {
      inicio = e < total ? e + 1 : 0;
      achou = true;
      break;
    }
    if (e == total) break;
    p = e + 1;
  }
  size_t p = achou ? inicio : 0;
  for (int volta = 0; volta < 2; volta++) {
    size_t fim = volta == 0 ? total : inicio;
    while (p <= fim) {
      size_t e = p;
      while (e < fim && lista[e] != ',') e++;
      if (rodCopiaItem(lista + p, e - p, out, cap)) return true;
      if (e == fim) break;
      p = e + 1;
    }
    if (!achou) break;
    p = 0;
  }
  return false;
}

static inline bool rodNumero(const char *p, size_t n, uint16_t *valor) {
  if (!p || !n) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    v = v * 10 + (uint8_t)(p[i] - '0');
    if (v > 3600) return false;
  }
  if (v < 30) return false;
  if (valor) *valor = (uint16_t)v;
  return true;
}

// Entradas duplicadas são aceitas; a última é a que vale, como num mapa do painel.
static inline uint16_t rodSegundos(const char *tempos, const char *chave, uint16_t padrao) {
  if (!tempos || !chave || !*chave) return padrao;
  size_t total = strlen(tempos), chaveN = strlen(chave);
  uint16_t achado = padrao;
  for (size_t p = 0; p <= total;) {
    size_t e = p;
    while (e < total && tempos[e] != ',') e++;
    const char *eq = (const char *)memchr(tempos + p, '=', e - p);
    uint16_t v;
    if (eq && (size_t)(eq - (tempos + p)) == chaveN && !memcmp(tempos + p, chave, chaveN) &&
        rodNumero(eq + 1, (size_t)(tempos + e - eq - 1), &v)) achado = v;
    if (e == total) break;
    p = e + 1;
  }
  return achado;
}

// Normaliza somente chave=segundos válidos e nunca deixa uma entrada pela metade no teto de 300 bytes.
static inline size_t rodLimparTempos(const char *entrada, char *out, size_t cap) {
  if (!out || !cap) return 0;
  out[0] = 0;
  if (!entrada) return 0;
  size_t usado = 0, total = strlen(entrada);
  for (size_t p = 0; p <= total;) {
    size_t e = p;
    while (e < total && entrada[e] != ',') e++;
    const char *eq = (const char *)memchr(entrada + p, '=', e - p);
    size_t kn = eq ? (size_t)(eq - (entrada + p)) : 0;
    uint16_t v;
    bool ok = eq && rodItemValido(entrada + p, kn) && rodNumero(eq + 1, (size_t)(entrada + e - eq - 1), &v);
    for (size_t i = p; ok && i < e; i++)
      if (!rodCharId(entrada[i]) && entrada[i] != ':' && entrada[i] != '=') ok = false;
    size_t n = e - p;
    if (ok) {
      size_t precisa = n + (usado ? 1 : 0);
      if (usado + precisa > 300 || usado + precisa + 1 > cap) break;
      if (usado) out[usado++] = ',';
      memcpy(out + usado, entrada + p, n);
      usado += n;
      out[usado] = 0;
    }
    if (e == total) break;
    p = e + 1;
  }
  return usado;
}

// A chave ("p:id" ou "t:N") é um item da lista "a,b,c"?
static inline bool rodNaLista(const char *lista, const char *chave) {
  if (!lista || !chave || !*chave) return false;
  size_t cn = strlen(chave), total = strlen(lista);
  for (size_t p = 0; p <= total;) {
    size_t e = p;
    while (e < total && lista[e] != ',') e++;
    if (e - p == cn && !memcmp(lista + p, chave, cn)) return true;
    if (e == total) break;
    p = e + 1;
  }
  return false;
}

// Metadado ausente, vazio, suspeito ou possivelmente truncado nunca tira o miniapp do rodízio.
static inline bool rodPularMiniapp(const char *campos, bool (*tem)(const char *campo)) {
  if (!campos || !tem) return false;
  size_t n = strlen(campos);
  if (!n || n >= 90) return false;
  for (size_t i = 0; i < n; i++)
    if (!((campos[i] >= 'a' && campos[i] <= 'z') || (campos[i] >= '0' && campos[i] <= '9') || campos[i] == '_' || campos[i] == ',')) return false;
  char campo[90];
  int vistos = 0;
  for (size_t p = 0; p <= n;) {
    size_t e = p;
    while (e < n && campos[e] != ',') e++;
    size_t m = e - p;
    if (m) {
      if (vistos >= 32) return false;  // lista longa demais para conferir tudo: na dúvida, o miniapp fica
      memcpy(campo, campos + p, m);
      campo[m] = 0;
      vistos++;
      if (tem(campo)) return false;
    }
    if (e == n) break;
    p = e + 1;
  }
  return true;
}
