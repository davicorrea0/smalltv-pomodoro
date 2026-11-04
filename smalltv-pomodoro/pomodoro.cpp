// Fases do Pomodoro, comandos e avisos de fim de fase (hooks).
#include "app.h"

const char *PHASE_KEY[] = {"idle", "work", "short", "long"};

static const char *PHASE_LABEL[] = {"", "FOCO", "PAUSA", "PAUSA LONGA"};

Phase phase = IDLE;

bool running = false;

uint32_t totalMs = 0, remainingMs = 0, lastTick = 0;

uint8_t cycleDone = 0;      // focos concluídos no ciclo atual

uint32_t alertUntil = 0;

static uint8_t phaseMinutes(Phase p) {
  return p == WORK ? cfg.work : p == SHORT_BREAK ? cfg.shortBreak : cfg.longBreak;
}

// Cada fase que começa ganha um número (phaseSeq); quando a tela termina de desenhá-la, com os números,
// shownSeq passa a ser esse número. O painel espera os dois baterem para mostrar a contagem: nunca começa
// antes do relógio (ver "pseq"/"pshown" em /api/state).
uint16_t phaseSeq = 0, shownSeq = 0;

void startPhase(Phase p, bool run) {
  phase = p;
  totalMs = remainingMs = (uint32_t)phaseMinutes(p) * 60000UL;
  running = run;
  lastTick = millis();
  phaseSeq++;
}

static int8_t hookPending = -1;  // aviso de fim de fase a enviar no loop: 0 foco, 1 pausa (avisos na Alexa)

String hookWork, hookBreak;  // endereços dos avisos (mais abaixo, em "avisos de fim de fase")

static void advance(bool completed) {
  if (completed) hookPending = phase == WORK ? 0 : 1;
  if (phase == WORK) {
    if (completed) {
      cycleDone++;
      cfg.todayCount++;
      saveSettings();
      statsFocus();
    }
    bool longNext = completed && cfg.longEvery && cycleDone % cfg.longEvery == 0;
    startPhase(longNext ? LONG_BREAK : SHORT_BREAK, cfg.autoStart);
  } else {
    if (phase == LONG_BREAK) cycleDone = 0;
    startPhase(WORK, cfg.autoStart);
  }
}

void tickPomodoro() {
  if (!running) return;
  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;
  if (dt >= remainingMs) {
    remainingMs = 0;
    alertUntil = now + 4000;
    advance(true);
  } else {
    remainingMs -= dt;
  }
}

bool colorTestRequested = false;  // tratado no loop (precisa da tela)

// Prévia de mostrador pedida pelo painel: mostra outro tema por alguns segundos sem salvar.
uint8_t previewTheme = 0;

uint32_t previewUntil = 0;

void command(const String &c) {
  if (c == "start") {
    if (phase == IDLE) startPhase(WORK, true);
    else { running = true; lastTick = millis(); }
  } else if (c == "pause") {
    running = false;
  } else if (c == "toggle") {
    command(running ? "pause" : "start");
  } else if (c == "skip") {
    if (phase != IDLE) advance(false);
  } else if (c == "reset") {
    phase = IDLE;
    running = false;
    cycleDone = 0;
  } else if (c == "focus") {
    startPhase(WORK, true);
  } else if (c == "short") {
    startPhase(SHORT_BREAK, true);
  } else if (c == "long") {
    startPhase(LONG_BREAK, true);
  } else if (c == "colortest") {
    colorTestRequested = true;
  }
}

// ---------- avisos de fim de fase (Alexa e afins) ----------
// Um endereço por evento em /hooks.txt (fim do foco, fim da pausa). Quando a fase termina o relógio
// chama o endereço (GET), e o serviço dispara uma rotina: a Virtual Smart Home e o Voice Monkey fazem a
// Alexa falar, o IFTTT liga a centenas de serviços. Vai sem criptografia (http): os três aceitam, e
// TLS não cabe no relógio. Um "https://" colado no painel vira "http://"; o resto do endereço é conferido
// por safeHttpUrl (security.h) antes de montar o pedido.

void hooksLoad() {
  File f = fsOk ? LittleFS.open("/hooks.txt", "r") : File();
  if (!f) return;
  hookWork = f.readStringUntil('\n');
  hookBreak = f.readStringUntil('\n');
  f.close();
  hookWork = hookClean(hookWork);
  hookBreak = hookClean(hookBreak);
}

String hookClean(String u) {
  u.trim();
  if (u.startsWith("https://")) u = "http://" + u.substring(8);
  if (!safeHttpUrl(u.c_str(), u.length())) return "";
  return u;
}

// chama o endereço; devolve o código HTTP (0 = sem endereço ou sem Wi-Fi, negativo = falha de conexão)
// A fase acabou: avisa fora do tick do Pomodoro. O nome vem do cache (buscando, o laço segue); o aviso que
// encontra a espera das falhas (2, 4, 8, 15 min), sem Wi-Fi ou sem nome é descartado, sem atrasar nada.
void hookLoop() {
  if (hookPending < 0) return;
  const String &u = hookPending ? hookBreak : hookWork;
  if (!u.length() || WiFi.status() != WL_CONNECTED || !hookBack.due(millis())) {
    hookPending = -1;
    return;
  }
  diagWhere = 2;
  int code = hookFire(u);
  if (code == HTTP_WAIT) return;  // buscando o nome: tenta no próximo laço (o cache desiste em 4 s)
  hookPending = -1;
  if (code < 0) hookBack.fail(millis());
  else hookBack.ok(millis(), 0);  // o serviço respondeu (mesmo com erro HTTP): a rede está boa
}

int hookFire(const String &url) {
  if (!url.length() || WiFi.status() != WL_CONNECTED) return 0;
  WiFiClient client;
  int code = httpGet(client, url, 2000);
  client.stop();
  return code;
}
