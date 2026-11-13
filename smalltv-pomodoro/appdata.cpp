// appdata.cpp — ver appdata.h
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include "appdata.h"

struct Store {
  char name[APPDATA_FIELDS][APPDATA_NAME + 1];
  char val[APPDATA_FIELDS][APPDATA_VALUE + 1];
  uint32_t until;  // millis() em que o conjunto vence
  bool has;        // já chegou algo (until == 0 também é um instante válido)
};
static Store store;

// vale enquanto até o vencimento falta mais que zero, em aritmética com sinal: sobrevive à volta do millis() (49,7 dias)
static bool alive(uint32_t now) { return store.has && (int32_t)(store.until - now) > 0; }

void appDataClear() { memset(&store, 0, sizeof store); }

void appDataExpire(uint32_t now) {
  if (store.has && !alive(now)) appDataClear();
}

const char *appDataGet(const char *name, uint32_t now) {
  if (!alive(now)) return nullptr;
  for (int i = 0; i < APPDATA_FIELDS; i++)
    if (store.name[i][0] && !strcmp(store.name[i], name)) return store.val[i];
  return nullptr;
}

static bool nameOk(const char *k) {
  size_t n = strlen(k);
  if (n < 1 || n > APPDATA_NAME) return false;
  for (; *k; k++)
    if (!((*k >= 'a' && *k <= 'z') || (*k >= '0' && *k <= '9') || *k == '_')) return false;
  return true;
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
  if (vals.size() > (size_t)APPDATA_FIELDS) return PSTR("campos demais");

  // valida tudo antes de mudar qualquer coisa
  char name[APPDATA_FIELDS][APPDATA_NAME + 1], val[APPDATA_FIELDS][APPDATA_VALUE + 1];
  int n = 0;
  for (JsonPairConst p : vals) {
    const char *k = p.key().c_str();
    if (!nameOk(k)) return PSTR("nome de campo invalido");
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

  // aplica numa cópia (parte do conjunto atual, vazio se venceu) e só grava se tudo coube: atômico
  Store next;
  if (alive(now)) next = store;
  else memset(&next, 0, sizeof next);
  for (int j = 0; j < n; j++) {
    int slot = -1;
    for (int i = 0; i < APPDATA_FIELDS; i++)
      if (next.name[i][0] && !strcmp(next.name[i], name[j])) { slot = i; break; }
    if (slot < 0)
      for (int i = 0; i < APPDATA_FIELDS; i++)
        if (!next.name[i][0]) { slot = i; break; }
    if (slot < 0) return PSTR("campos demais no conjunto");
    strcpy(next.name[slot], name[j]);
    strcpy(next.val[slot], val[j]);
  }
  next.until = now + (uint32_t)ttl * 1000u;
  next.has = true;
  store = next;
  return nullptr;
}
