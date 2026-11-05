// Idioma padrão (inglês): compila defaultSettings() e loadSettings() REAIS contra uma EEPROM simulada.
// Ajustes do zero (EEPROM apagada ou de outro firmware) começam em inglês; quem já usa mantém o idioma (0 pt, 2 es) e o
// lixo continua virando português, então nenhum relógio instalado troca de idioma ao atualizar.
const fs = require('fs'), path = require('path');
const source = require('./fonte.cjs');
function between(a, b) {
  const i = source.indexOf(a), j = source.indexOf(b, i + a.length);
  if (i < 0 || j < 0) throw Error('não achei: ' + a);
  return source.slice(i, j);
}
const strukt = between('struct Settings {', '\n};') + '\n};';
const consts = source.match(/const uint32_t SETTINGS_MAGIC(_V3)? = 0x[0-9A-F]+;/g).join('\n');
const defaults = between('void defaultSettings() {', '\n}\n') + '\n}\n';
const load = between('void loadSettings() {', '\nvoid saveSettings()');
const out = process.argv[2];
if (!out) throw Error('output.cpp required');
fs.writeFileSync(out, `#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cassert>
#include <cstdio>
enum { THEME_MATRIX, THEME_TOMATO, THEME_ANALOG, THEME_WEATHER, THEME_PACK, THEME_COUNT };
enum { ACT_READ, ACT_COUNT = 5 };
const uint8_t UI_VCOM_DEFAULT = 0x20;
const int EE_SIZE = 512;
${strukt}
${consts}
static uint8_t mem[EE_SIZE];
struct { void begin(int) {} template <class T> void get(int a, T &t) { memcpy(&t, mem + a, sizeof t); } template <class T> void put(int a, const T &t) { memcpy(mem + a, &t, sizeof t); } void commit() {} } EEPROM;
struct { uint32_t random() { return rand(); } } ESP;
Settings cfg;
${defaults}
${load}
static uint8_t langAfterBoot() { loadSettings(); return cfg.lang; }
int main() {
  for (int fill : {0xFF, 0x00}) {  // EEPROM virgem ou apagada ("Apagar todos os dados pessoais"): magia ausente
    memset(mem, fill, sizeof mem);
    assert(langAfterBoot() == 1);
    assert(cfg.work == 25 && cfg.shortBreak == 5 && cfg.dailyGoal == 8 && cfg.activity == ACT_READ && cfg.alexa == 1 && cfg.theme == THEME_MATRIX);
    assert(cfg.nightSun == 0 && cfg.rotMin == 0 && cfg.lcdGreen == 1 && cfg.rainAlert == 1 && cfg.fahr == 0 && cfg.beta == 0 && cfg.warm == 0);  // a posição do idioma não deslocou os vizinhos
  }
  memset(mem, 0xFF, sizeof mem);  // EEPROM de outro firmware (ex.: o original): lixo sem a magia dos ajustes
  for (int i = 0; i < EE_SIZE; i++) mem[i] = (uint8_t)(i * 37 + 11);
  assert(langAfterBoot() == 1);
  for (int lang : {0, 1, 2}) {  // quem já usa e atualiza: o idioma gravado continua
    defaultSettings(); cfg.lang = (uint8_t)lang; EEPROM.put(0, cfg);
    assert(langAfterBoot() == lang);
  }
  for (int lixo : {3, 4, 100, 255}) {  // campo lido de uma versão antiga (nunca gravado): português, como sempre
    defaultSettings(); cfg.lang = (uint8_t)lixo; EEPROM.put(0, cfg);
    assert(langAfterBoot() == 0);
  }
  { // "POM3" (v3): o idioma não existia, os bytes são lixo: português; o resto dos ajustes é mantido
    defaultSettings(); cfg.magic = SETTINGS_MAGIC_V3; cfg.lang = 0xFF; cfg.work = 30; EEPROM.put(0, cfg);
    assert(langAfterBoot() == 0 && cfg.work == 30 && cfg.magic == SETTINGS_MAGIC);
  }
  puts("idioma: ajustes do zero em inglês; relógios com idioma gravado (pt, en, es) e lixo não mudam");
}
`);
