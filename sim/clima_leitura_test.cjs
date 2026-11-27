// Extrai o trecho de falha do weatherLoop (weather.cpp), o HttpBackoff (security.h) e o WX_STALE_MS (app.h) e gera
// um C++ que confere: a última leitura fica na tela até 1 h depois da última busca certa, o aviso de chuva sai na 3ª
// falha, a tela sem leitura redesenha uma vez na 3ª falha e a conta sobrevive à volta do millis().
// Uso: node clima_leitura_test.cjs <saida.cpp>  (o sim/build.sh compila e roda)
const fs = require('fs'), path = require('path'), assert = require('assert');
const FW = path.join(__dirname, '../smalltv-pomodoro');
const ler = f => fs.readFileSync(path.join(FW, f), 'utf8').replace(/\r\n/g, '\n');
const weather = ler('modulos/weather.cpp'), security = ler('security.h'), app = ler('app.h');

const ini = weather.indexOf('    wxBack.fail(millis());');
assert(ini >= 0, 'weatherLoop: wxBack.fail(millis()) no ramo de falha');
const fim = weather.indexOf('\n  }\n}', ini);
assert(fim > ini, 'weatherLoop: fim do ramo de falha');
const falha = weather.slice(ini, fim);
assert(falha.includes('WX_STALE_MS') && falha.includes('wx.okAt'), 'a leitura só expira pelo tempo desde a última busca certa');
assert(/wx\.okAt = millis\(\);\n\s*wx\.ok = true;/.test(weather), 'fetchWeather marca okAt junto com wx.ok');

const ba = security.indexOf('struct HttpBackoff {'), bb = security.indexOf('\n};', ba);
assert(ba >= 0 && bb > ba, 'struct HttpBackoff');
const stale = app.match(/const uint32_t WX_STALE_MS = [^;]+;/);
assert(stale, 'WX_STALE_MS em app.h');

const out = process.argv[2];
if (!out) { console.log('clima: trecho de falha encontrado'); process.exit(0); }
fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstdlib>
static uint32_t agora;
static uint32_t millis() { return agora; }
${security.slice(ba, bb + 3)}
${stale[0]}
static struct { bool ok = false; uint8_t rainIn = 0; uint16_t version = 0; uint32_t okAt = 0; } wx;
static HttpBackoff wxBack;
static void falha() {
${falha}
}
static void certo() { wxBack.ok(millis(), 15UL * 60000UL); wx.okAt = millis(); wx.ok = true; wx.rainIn = 15; wx.version++; }
static int erros = 0;
#define CONFERE(c, m) do { if (!(c)) { printf("clima_leitura: FALHOU %s (agora=%lu)\\n", m, (unsigned long)agora); erros++; } } while (0)
static const uint32_t MIN = 60000UL;
static void roda(uint32_t base) {
  wx = {}; wxBack = {};
  agora = base;
  certo();
  const uint16_t v0 = wx.version;
  agora = base + 17 * MIN; falha();  // 1ª
  agora = base + 19 * MIN; falha();  // 2ª
  CONFERE(wx.ok && wx.rainIn == 15 && wx.version == v0, "duas falhas não mexem na leitura");
  agora = base + 23 * MIN; falha();  // 3ª
  CONFERE(wx.ok && wx.rainIn == 0 && wx.version == v0 + 1, "3ª falha: chuva sai, leitura fica e a tela redesenha");
  agora = base + 31 * MIN; falha();
  agora = base + 46 * MIN; falha();
  agora = base + 59 * MIN; falha();
  CONFERE(wx.ok && wx.version == v0 + 1, "a leitura fica até 1 h depois da última busca certa");
  agora = base + 61 * MIN; falha();
  CONFERE(!wx.ok && wx.version == v0 + 2, "passada 1 h, a leitura sai da tela uma vez");
  agora = base + 76 * MIN; falha();
  CONFERE(!wx.ok && wx.version == v0 + 2, "sem leitura, falhas seguintes não redesenham");
  agora = base + 80 * MIN; certo();
  CONFERE(wx.ok && wxBack.fails == 0, "busca certa volta a leitura");

  wx = {}; wxBack = {};  // sem leitura nenhuma (ligou sem internet)
  agora = base;
  falha(); falha();
  CONFERE(!wx.ok && wx.version == 0, "duas falhas sem leitura: ainda buscando");
  falha();
  CONFERE(!wx.ok && wx.version == 1, "3ª falha sem leitura: a tela troca para indisponível");
  falha();
  CONFERE(wx.version == 1, "4ª falha sem leitura: nada muda");
}
int main() {
  roda(5 * MIN);
  roda(0xFFFFFFFFu - 30 * MIN);  // o millis() volta a zero no meio da hora
  printf("clima_leitura: %s (1 h de leitura antiga, chuva na 3ª falha, volta do millis)\\n", erros ? "FALHOU" : "ok");
  return erros ? 1 : 0;
}
`);
console.log('clima_leitura: ' + out);
