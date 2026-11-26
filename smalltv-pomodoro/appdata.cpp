// appdata.cpp — ver appdata.h
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include "appdata.h"

// em vetores separados, sem enchimento entre nome, valor e prazo: 16 × (9 + 16 + 4) = 464 B
static char nm[APPDATA_FIELDS][APPDATA_NAME + 1];  // "" = livre
static char vl[APPDATA_FIELDS][APPDATA_VALUE + 1];
static uint32_t until[APPDATA_FIELDS];             // millis() em que o campo vence

// vale enquanto até o vencimento falta mais que zero, em aritmética com sinal: sobrevive à volta do millis() (49,7 dias)
static bool alive(int i, uint32_t now) { return nm[i][0] && (int32_t)(until[i] - now) > 0; }

void appDataClear() {
  memset(nm, 0, sizeof nm);
  memset(vl, 0, sizeof vl);
  memset(until, 0, sizeof until);
}

void appDataExpire(uint32_t now) {
  for (int i = 0; i < APPDATA_FIELDS; i++)
    if (nm[i][0] && !alive(i, now)) nm[i][0] = vl[i][0] = 0;
}

static int find(const char *name, uint32_t now) {
  for (int i = 0; i < APPDATA_FIELDS; i++)
    if (alive(i, now) && !strcmp(nm[i], name)) return i;
  return -1;
}

const char *appDataGet(const char *name, uint32_t now) {
  int i = find(name, now);
  return i < 0 ? nullptr : vl[i];
}

bool appDataAt(int k, uint32_t now, const char **name, const char **value, uint32_t *leftS) {
  for (int i = 0; i < APPDATA_FIELDS; i++)
    if (alive(i, now) && k-- == 0) {
      *name = nm[i];
      *value = vl[i];
      *leftS = (until[i] - now + 999) / 1000;
      return true;
    }
  return false;
}

static bool nameOk(const char *k) {
  size_t n = strlen(k);
  if (n < 1 || n > APPDATA_NAME) return false;
  for (; *k; k++)
    if (!((*k >= 'a' && *k <= 'z') || (*k >= '0' && *k <= '9') || *k == '_')) return false;
  return true;
}

static void showSpace(const char *&p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
}

const char *appShowParse(const char *body, size_t len, char *pkg, size_t pkgCap, uint8_t *secs) {
  if (len > APPSHOW_BODY_MAX) return PSTR("corpo grande demais");
  const char *p = body, *end = body + len;
  bool gotPkg = false, gotSecs = false;
  char parsedPkg[24];
  size_t pkgLen = 0;
  uint8_t parsedSecs = 0;

  showSpace(p, end);
  if (p == end || *p++ != '{') return PSTR("JSON invalido");
  for (;;) {
    showSpace(p, end);
    if (p == end || *p != '"') return PSTR("JSON invalido");
    const char *key = ++p;
    while (p < end && *p != '"' && *p != '\\' && (uint8_t)*p >= 0x20) p++;
    if (p == end || *p != '"') return PSTR("JSON invalido");
    size_t keyLen = (size_t)(p - key);
    p++;
    showSpace(p, end);
    if (p == end || *p++ != ':') return PSTR("JSON invalido");
    showSpace(p, end);

    if (keyLen == 3 && !memcmp(key, "pkg", 3)) {
      if (gotPkg) return PSTR("JSON invalido");
      gotPkg = true;
      if (p == end || *p++ != '"') return PSTR("pkg invalido");
      pkgLen = 0;
      while (p < end && *p != '"') {
        char c = *p++;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') || pkgLen >= 23)
          return PSTR("pkg invalido");
        parsedPkg[pkgLen++] = c;
      }
      if (p == end || *p++ != '"' || !pkgLen) return PSTR("pkg invalido");
      parsedPkg[pkgLen] = 0;
      if (parsedPkg[0] == '_') return PSTR("pkg invalido");  // "_novo" e afins são os temporários do envio de pacote
    } else if (keyLen == 1 && *key == 's') {
      if (gotSecs) return PSTR("JSON invalido");
      gotSecs = true;
      if (p == end || *p < '1' || *p > '9') return PSTR("s fora de 5 a 60");
      unsigned v = 0;
      bool grande = false;
      do {
        unsigned dig = (unsigned)(*p++ - '0');
        if (v > 6 || (v == 6 && dig > 0)) grande = true;
        else v = v * 10 + dig;
      } while (p < end && *p >= '0' && *p <= '9');
      if (grande || v < 5 || v > 60) return PSTR("s fora de 5 a 60");
      parsedSecs = (uint8_t)v;
    } else return PSTR("JSON invalido");

    showSpace(p, end);
    if (p == end) return PSTR("JSON invalido");
    if (*p == ',') { p++; continue; }
    if (*p++ == '}') break;
    return PSTR("JSON invalido");
  }
  showSpace(p, end);
  if (p != end) return PSTR("JSON invalido");
  if (!gotPkg) return PSTR("pkg invalido");
  if (!gotSecs) return PSTR("s fora de 5 a 60");
  if (!pkg || pkgCap <= pkgLen || !secs) return PSTR("pkg invalido");
  memcpy(pkg, parsedPkg, pkgLen + 1);
  *secs = parsedSecs;
  return nullptr;
}

const char *appDataPut(const char *body, size_t len, uint32_t now) {
  if (len > APPDATA_BODY_MAX) return PSTR("corpo grande demais");
  // no máximo 2 níveis: a raiz e "values" (um array ou objeto dentro de um valor passa do limite)
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::NestingLimit(2))) return PSTR("JSON invalido");
  if (!doc.is<JsonObject>()) return PSTR("esperado objeto");
  JsonObject root = doc.as<JsonObject>();
  for (JsonPair p : root)
    if (strcmp(p.key().c_str(), "ttl") && strcmp(p.key().c_str(), "values")) return PSTR("chave desconhecida");
  JsonVariantConst t = root["ttl"];
  if (!t.is<int32_t>()) return PSTR("ttl invalido");
  int32_t ttl = t.as<int32_t>();
  if (ttl < 10 || ttl > 86400) return PSTR("ttl fora de 10 a 86400");
  JsonVariantConst vs = root["values"];
  if (!vs.is<JsonObjectConst>()) return PSTR("values ausente");
  JsonObjectConst vals = vs.as<JsonObjectConst>();
  if (vals.size() < 1) return PSTR("values vazio");
  if (vals.size() > (size_t)APPDATA_PUT_MAX) return PSTR("campos demais no envio");

  // valida tudo antes de mudar qualquer coisa
  char name[APPDATA_PUT_MAX][APPDATA_NAME + 1], val[APPDATA_PUT_MAX][APPDATA_VALUE + 1];
  int n = 0;
  for (JsonPairConst p : vals) {
    const char *k = p.key().c_str();
    if (!nameOk(k)) return PSTR("nome de campo invalido");
    for (int j = 0; j < n; j++)
      if (!strcmp(name[j], k)) return PSTR("campo repetido");
    JsonVariantConst v = p.value();
    if (v.is<const char *>()) {
      const char *s = v.as<const char *>();
      if (strlen(s) > (size_t)APPDATA_VALUE) return PSTR("valor grande demais");
      strcpy(val[n], s);
    } else if (v.is<int32_t>()) {
      snprintf_P(val[n], sizeof val[n], PSTR("%ld"), (long)v.as<int32_t>());
    } else return PSTR("valor deve ser texto ou inteiro");
    strcpy(name[n], k);
    n++;
  }

  // lugar para todos? (um campo que já existe reusa o seu; os novos precisam de um livre ou vencido)
  int slot[APPDATA_PUT_MAX];
  bool usado[APPDATA_FIELDS] = {};
  for (int j = 0; j < n; j++)
    if ((slot[j] = find(name[j], now)) >= 0) usado[slot[j]] = true;
  for (int j = 0, i = 0; j < n; j++) {
    if (slot[j] >= 0) continue;
    while (i < APPDATA_FIELDS && (usado[i] || alive(i, now))) i++;
    if (i == APPDATA_FIELDS) return PSTR("armazem cheio: espere campos vencerem");
    slot[j] = i;
    usado[i++] = true;
  }

  // aplica: só os campos do envio, todos com o ttl dele
  uint32_t fim = now + (uint32_t)ttl * 1000u;
  for (int j = 0; j < n; j++) {
    strcpy(nm[slot[j]], name[j]);
    strcpy(vl[slot[j]], val[j]);
    until[slot[j]] = fim;
  }
  return nullptr;
}
