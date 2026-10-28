// ui.h — telas do SmallTV (240x240). Só depende do Adafruit_GFX: o firmware e o
// simulador de PC (sim/) compilam este mesmo código, então o que o simulador mostra
// é o que aparece no aparelho.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>
#include <time.h>

// Mostradores: a lista fica em faces_list.h (o enum THEME_* é gerado a partir dela).
#include "faces_list.h"
enum UiTheme : uint8_t {
#define X(id, fn, name, desc) THEME_##id,
  UI_FACES(X)
#undef X
  THEME_COUNT
};
PGM_P uiFaceName(uint8_t t);  // nome no painel (na flash: copie com strncpy_P)
PGM_P uiFaceDesc(uint8_t t);  // descrição curta (na flash, idem)
enum UiPhase : uint8_t { PH_IDLE = 0, PH_WORK, PH_SHORT, PH_LONG };
// O que a pessoa faz durante o foco: muda o desenho ao lado do tomate e o título da tela.
enum UiActivity : uint8_t { ACT_READ = 0, ACT_STUDY, ACT_CODE, ACT_TASKS, ACT_DRAW, ACT_COUNT };

// Um dia da previsão (hoje é o primeiro)
struct UiDay {
  uint8_t code;        // código WMO do tempo
  int8_t tmin, tmax;   // °C, arredondadas
  uint8_t rain;        // chance de chuva em %, 255 = sem dado
};
static const int UI_DAYS = 5;

struct UiWeather {
  bool ok;
  float t, tmin, tmax;
  int hum, code;
  bool day;
  uint16_t version;  // muda a cada atualização: força redesenho do clima
  uint8_t nd;        // dias em d[] (0 se a previsão não veio)
  UiDay d[UI_DAYS];
  uint8_t rainIn;    // chuva começando na próxima hora: minutos até ela (5 = em instantes, 15, 30, 45); 0 = não
};

struct UiState {
  bool timeOk;
  struct tm now;
  bool hour12;
  // Pomodoro
  uint8_t phase;
  bool running;
  uint32_t remainingMs, totalMs;
  uint8_t cycleDone, longEvery;
  uint16_t today;
  uint8_t dailyGoal;
  uint8_t activity;  // UiActivity
  uint16_t streak;   // dias seguidos batendo a meta (inclui hoje se já bateu)
  uint16_t best;     // maior sequência registrada
  // clima
  bool weatherOn;
  UiWeather wx;
  const char *city;
  bool fahr;  // temperaturas de wx já em °F (os mostradores só desenham o número; ver uiTempC)
  // geral
  uint8_t theme;
  uint16_t accent;  // cor dos números do relógio (RGB565)
  const char *ip;
  uint32_t ms;     // millis(): anda as animações
  bool celebrate;  // uma fase do Pomodoro acabou de terminar: toca a animação de fim
};

constexpr uint16_t uiRgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Temperatura de s.wx em °C, para decisões (cachecol, óculos) que não podem mudar com a unidade.
inline float uiTempC(const UiState &s, float v) { return s.fahr ? (v - 32) * 5 / 9 : v; }

// Sprite em pixel art. Os sprites são gerados de assets/*.txt por assets/gerar.js em
// assets.h (SPR_<NOME>) e ficam na flash (PROGMEM), com 4 bits por pixel.
struct UiSprite {
  uint8_t w, h, frames;
  uint16_t frameMs;     // duração de cada quadro (animações)
  const uint16_t *pal;  // cor dos índices 1..15 (0 = transparente)
  const uint8_t *px;    // índices, linha a linha, um quadro depois do outro
};
static const int32_t UI_TRANSPARENT = -1;

// Desenha o quadro "frame" com pixels de scale x scale em (x, y). Pixels transparentes
// ficam como estão, ou recebem "bg" (útil para repintar por cima do desenho anterior).
void uiSprite(const UiSprite *s, int x, int y, uint8_t scale = 1, uint8_t frame = 0, int32_t bg = UI_TRANSPARENT);
uint8_t uiSpriteFrame(const UiSprite *s, uint32_t ms);  // quadro da animação no instante ms

void uiBegin(Adafruit_GFX *gfx);
// Chamado entre os quadros de animação (ex.: virada do Flip) para ceder tempo ao Wi-Fi.
extern void (*uiFrameHook)();
// Relógio em ms para animações guiadas por tempo (firmware: millis). Sem ele, passos de 20 ms.
extern uint32_t (*uiMillis)();
// Envia um bloco de pixels RGB565 para a tela. O firmware aponta para o método rápido do
// display (uma transação SPI por bloco); sem isso cai no genérico do Adafruit_GFX, pixel a pixel.
extern void (*uiBlit)(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h);
// Recorte ativo (pacotes de mostrador redesenham só a área que mudou). O firmware respeita
// isto ao desenhar imagens JPEG por conta própria.
struct UiClip { bool on; int16_t x0, y0, x1, y1; };
extern UiClip uiClip;
void uiClipBegin(int x0, int y0, int x1, int y1);
void uiClipEnd();
// Desenha a área (x, y, w, h) fora da tela, em faixas de alguns KB, e manda cada faixa pronta para a
// tela de uma vez: a tela nunca mostra o fundo apagado entre o "limpar" e o "desenhar de novo" (o que
// piscava nos ponteiros). draw(ctx) desenha a cena inteira em G, que recorta para a faixa da vez; pode
// ser chamada várias vezes. Devolve false sem memória para a faixa (aí quem chamou desenha direto).
bool uiOffscreen(int x, int y, int w, int h, void (*draw)(void *), void *ctx);
void uiInvalidate();                         // redesenha tudo na próxima chamada
// Igual a uiInvalidate(), mas, se um mostrador do firmware está na tela e o próximo também é um, a
// troca é uma rolagem vertical por hardware (o antigo sobe e sai, o novo entra por baixo) em vez de
// apagar e redesenhar na frente da pessoa. Sem uiScrollHw (simulador), vira uiInvalidate().
void uiInvalidateSlide();

// ---- rolagem vertical por hardware (ST7789: VSCRDEF 0x33 e VSCSAD 0x37) ----
// A RAM do painel tem 320 linhas e o vidro mostra 240. Rolar é só mudar de onde o painel começa a
// ler a RAM: 4 bytes no SPI, zero de CPU por quadro (rolar por software reenviaria 115 KB por quadro).
// As 80 linhas de RAM que o vidro não mostra (a "margem") viram o vão por onde o conteúdo passa; o
// firmware as deixa pretas ao configurar o painel (applyPanel). Rola-se a tela inteira: as faixas fixas
// TFA/BFA do chip não servem, porque as linhas visíveis são sempre as 240 primeiras da leitura.
//   rows   = linhas do anel de rolagem, 241..UI_PANEL_ROWS (320 usa a RAM toda)
//   offset = quantas linhas o conteúdo subiu na tela (0 = parado), vale módulo rows
// O conteúdo de y = 0..239 sobe e sai pelo topo; o que sai volta por baixo depois do vão, de modo que
// as linhas de RAM de y = 0..239 já podem receber o desenho novo assim que saíram de vista.
const int UI_PANEL_ROWS = 320;
// Hook do firmware (rows = 0 desliga: padrão do chip, offset 0). O simulador modela o painel.
extern void (*uiScrollHw)(int16_t rows, int16_t offset);
bool uiScrollBegin(int rows);   // false: sem suporte ou rows inválido (nada muda)
void uiScrollTo(int offset);
void uiScrollEnd();             // sempre volta ao offset 0; inofensivo se nada está rolando
bool uiScrollActive();
// Valor do VSCSAD (linha de RAM em que a leitura começa) para um offset de tela. Com "girar 180°"
// (MADCTL MY) a RAM fica espelhada em relação ao que a pessoa vê, então o sentido inverte:
//   normal: linha lida n mostra a linha de RAM n + SAD, o conteúdo sobe com SAD = offset
//   girado: SAD = (rows - offset) % rows
inline uint16_t uiScrollSad(bool flip, int rows, int offset) {
  return (uint16_t)(flip && offset ? rows - offset : offset);
}
void uiSetLang(uint8_t lang);                // textos da tela: 0 português, 1 inglês, 2 espanhol
extern uint8_t uiLang;
// tr("pt", "en", "es"): os três textos ficam na flash (PSTR) e o do idioma atual é copiado para um
// de 6 buffers rotativos de 48 bytes (RAM é escassa: o GIF do álbum precisa de ~22 KB contíguos).
// O ponteiro vale até mais 5 chamadas de tr(); quem guarda o texto deve copiar.
const char *trP(PGM_P pt, PGM_P en, PGM_P es);
#define tr(pt, en, es) trP(PSTR(pt), PSTR(en), PSTR(es))
// Uma linha de 240 pixels alinhada a 4 bytes, compartilhada: a cópia alinhada que o SPI exige (tftPixels), o
// desenho do GIF e a linha do Flip. Nunca há dois usos ao mesmo tempo (um buffer só em vez de três: ~700 bytes).
uint16_t *uiRowBuf();
extern uint32_t uiEnters;                    // contador de trocas de tela inteira
void uiRenderClock(const UiState &s);        // tela de relógio no tema escolhido
void uiRenderPomodoro(const UiState &s);     // anel de progresso do Pomodoro
void uiMessage(const char *title, const char *l1, const char *l2, const char *l3);
void uiMessageLine(const char *l1);              // troca só a linha de destaque da mensagem na tela
void uiBootProgress(uint32_t ms, uint8_t done);  // barra e etapas da tela de início (só com uiMessage na tela)
void uiSyncHint(uint32_t ms);                    // "sincronizando" por cima do mostrador, enquanto não há hora
void uiColorTest();                          // barras R/G/B/branco para ajustar o painel
// VCOM padrão: 0x1E = 0,850 V (com 0,900 V, o valor de reset do chip, o SmallTV Ultra ficava com imagem fantasma)
static const uint8_t UI_VCOM_DEFAULT = 0x1E;
void uiVcomTest(uint8_t vcom, bool full);    // estampas do ajuste anti-fantasma (full: desenha tudo; senão só o valor)
void uiError(const char *msg);               // aviso centralizado (ex.: GIF inválido)
