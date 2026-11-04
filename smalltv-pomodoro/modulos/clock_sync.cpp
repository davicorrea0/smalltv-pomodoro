// Hora: confirmação por segunda fonte (TimeGuard), memória RTC entre reinícios e fuso horário.
#include "../app.h"

TimeGuard timeGuard;      // hora confirmada por uma segunda fonte (security.h); ver timeGuardBegin

bool localNow(struct tm &t) {
  if (!timeValid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

// Tempos do último início, em ms desde que ligou (0 = ainda não aconteceu); aparecem em diag.boot
uint32_t bootScreenMs = 0, bootWifiMs = 0, bootTimeMs = 0, bootNtpMs = 0;

uint8_t bootTimeSrc = 0;   // de onde veio a primeira hora: 1 SNTP, 2 memória RTC (reinício por software), 3 cabeçalho do clima

uint8_t bootWifiPath = 0;  // 0 rede salva (varredura completa), 1 canal e BSSID guardados, 2 o SDK já tinha conectado, 3 o rápido falhou

uint32_t ntpAskAt = 0;  // quando o SNTP foi (re)configurado; 0 = ainda não

uint8_t ntpTries = 0;

bool syncHintOn = false;  // o mostrador está com o indicador "sincronizando" por cima (ver renderClock)

bool timeSoft = false;     // hora de reserva (RTC ou cabeçalho HTTP) ainda sem SNTP: só mostra, nunca confirma

static void bootNote(uint8_t src) {
  if (!bootTimeMs) {
    bootTimeMs = millis() | 1;
    bootTimeSrc = src;
  }
}

void softTime(int64_t sec, uint8_t src) {
  timeval tv = {(time_t)sec, 0};
  settimeofday(&tv, nullptr);  // não é o SNTP: o aviso do timeGuardBegin ignora
  timeSoft = true;
  bootNote(src);
}

void rtcTimeSave() {
  if (!timeTrusted()) return;
  eboot_command cmd;
  if (eboot_command_read(&cmd) == 0) return;  // atualização à espera do eboot: não mexe na memória RTC agora
  RtcClock c = rtcClockMake((uint32_t)time(nullptr), system_get_rtc_time());
  ESP.rtcUserMemoryWrite(RTC_CLOCK_BLOCK, (uint32_t *)&c, sizeof c);
}

void rtcTimeRestore() {
  RtcClock c;
  if (!ESP.rtcUserMemoryRead(RTC_CLOCK_BLOCK, (uint32_t *)&c, sizeof c)) return;
  int64_t t = rtcClockRestore(c, system_get_rtc_time(), system_rtc_clock_cali_proc());
  if (t <= 0) return;
  setenv("TZ", cfg.tz, 1);
  tzset();
  softTime(t, 2);
}

// O NTP.br responde mais rápido no Brasil; no resto do mundo o pool escolhe um servidor perto. rot gira a
// ordem: se o primeiro não respondeu, o lwIP só tenta o seguinte depois de ~15 s (ver sntpWatch).
void applyTimezone(uint8_t rot) {
  const char *s[3] = {"pool.ntp.org", "time.google.com", "a.st1.ntp.br"};
  const char *z = cfg.tz;  // fusos do Brasil e vizinhos: "<-03>3", "<-04>4"...
  if (z[0] == '<' && z[1] == '-' && z[2] == '0' && z[3] >= '2' && z[3] <= '5' && z[4] == '>') {
    s[0] = "a.st1.ntp.br";
    s[1] = "pool.ntp.org";
    s[2] = "time.google.com";
  }
  configTime(z, s[rot % 3], s[(rot + 1) % 3], s[(rot + 2) % 3]);
}

void timeGuardBegin() {
  settimeofday_cb([](bool fromSntp) {
    if (!fromSntp) return;  // a própria correção abaixo (e a do clima) também chamam este aviso
    timeSoft = false;
    bootNote(1);
    if (!bootNtpMs) bootNtpMs = millis() | 1;
    timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t want = timeGuard.onSntp(tv.tv_sec, millis(), !weatherConfirms());
    if (want != tv.tv_sec) {  // salto não confirmado: volta à hora que vinha andando
      timeval w = {(time_t)want, 0};
      settimeofday(&w, nullptr);
    }
    if (!timeGuard.confirmed && weatherConfirms()) wxBack.next = millis();  // confirma já pelo clima
  });
}

// enquanto a hora não estiver confirmada, o SNTP pergunta a cada minuto (o padrão é a cada hora)
extern "C" uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() { return timeGuard.confirmed ? 3600000 : 60000; }

// Deslocamento do fuso do relógio agora, em segundos (hora local - UTC), pela diferença entre localtime e gmtime.
int32_t localOffset() {
  time_t now = time(nullptr);
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  int32_t d = (lt.tm_hour - gt.tm_hour) * 3600 + (lt.tm_min - gt.tm_min) * 60;
  int dd = lt.tm_yday - gt.tm_yday;  // virada de dia (ou de ano) entre os dois
  if (dd == 1 || dd < -1) d += 86400;
  else if (dd == -1 || dd > 1) d -= 86400;
  return d;
}
