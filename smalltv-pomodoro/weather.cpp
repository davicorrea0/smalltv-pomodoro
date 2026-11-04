// Cliente HTTP mínimo (DNS assíncrono) e busca do clima.
#include "app.h"

int16_t sunRise = -1, sunSet = -1;  // minutos do dia (hora local), da última busca do clima

Weather wx;

HttpBackoff wxBack;  // espera entre buscas de clima: 15 min se deu certo; 2, 4, 8, 15 min a cada falha seguida

HttpBackoff hookBack;  // o mesmo para os avisos de fim de fase

static DnsCache dnsCache;

static void dnsFound(const char *name, const ip_addr_t *ip, void *) { dnsCache.done(name, ip ? (uint32_t)IPAddress(ip) : 0, millis()); }

static int dnsStart(const char *host, uint32_t &ip) {
  ip_addr_t a;
  err_t e = dns_gethostbyname(host, &a, dnsFound, nullptr);
  if (e == ERR_OK) { ip = (uint32_t)IPAddress(a); return 0; }
  return e == ERR_INPROGRESS ? 1 : -1;
}

// 1 = ip pronto (cache de 1 h), 0 = buscando, -1 = falhou
static int dnsResolve(const char *host, uint32_t &ip) { return dnsCache.get(host, millis(), ip, dnsStart); }

static bool httpLine(WiFiClient &c, String &line, uint32_t start, uint16_t timeoutMs) {
  line = "";
  while (millis() - start < timeoutMs) {
    if (c.available()) {
      int ch = c.read();
      if (ch == '\n') return true;
      if (ch < 0) continue;
      if (line.length() >= 512) return false;
      line += (char)ch;
    } else if (!c.connected()) return false;
    else delay(1);
  }
  return false;
}

// Acrescenta um decimal já limitado pelo firmware sem criar outro String no heap.
void appendFixed(String &out, float value, uint8_t digits) {
  bool neg = value < 0;
  if (neg) value = -value;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < digits; i++) scale *= 10;
  uint32_t scaled = lroundf(value * scale);
  if (neg && scaled) out += '-';
  out += scaled / scale;
  if (!digits) return;
  out += '.';
  uint32_t frac = scaled % scale;
  for (uint32_t div = scale / 10; div; div /= 10) out += (char)('0' + frac / div % 10);
}

static uint32_t httpDateEpoch = 0;  // cabeçalho Date da última resposta (0 = não veio)

// Devolve também HTTP_WAIT (buscando o nome: nada foi enviado, tente no próximo laço) e HTTP_DNS (nome não resolveu).
// readMs vale para a primeira resposta e para cada pausa na leitura do corpo (o clima lê em pedaços).
int httpGet(WiFiClient &c, const String &url, uint16_t readMs) {
  httpDateEpoch = 0;
  if (!safeHttpUrl(url.c_str(), url.length())) return -1;
  int end = 7;
  while (end < (int)url.length() && url[end] != '/' && url[end] != '?') end++;
  String host = url.substring(7, end), path = url.substring(end);
  if (!path.length() || path[0] == '?') path = "/" + path;
  uint16_t port = 80;
  int colon = host.indexOf(':');
  if (colon >= 0) {
    port = host.substring(colon + 1).toInt();
    host.remove(colon);
  }
  if (!host.length() || !port) return -1;
  uint32_t ip = 0;
  int d = dnsResolve(host.c_str(), ip);
  if (d <= 0) return d ? HTTP_DNS : HTTP_WAIT;
  c.setTimeout(HTTP_CONNECT_MS);
  if (!c.connect(IPAddress(ip), port)) return -1;
  c.setTimeout(readMs);
  // Envia em partes: concatenar tudo criava um String temporário grande em toda consulta.
  c.print(F("GET "));
  c.print(path);
  c.print(F(" HTTP/1.0\r\nHost: "));
  c.print(host);
  c.print(F("\r\nUser-Agent: SmallTV\r\nConnection: close\r\n\r\n"));
  uint32_t start = millis();
  String line;
  line.reserve(128);  // httpLine reutiliza a mesma capacidade para status e cabeçalhos
  if (!httpLine(c, line, start, readMs)) return -2;
  int sp = line.indexOf(' ');
  int code = sp > 0 ? line.substring(sp + 1).toInt() : 0;
  if (code <= 0) return -2;
  size_t total = line.length();
  for (int headers = 0; headers < 48; headers++) {
    if (!httpLine(c, line, start, readMs)) return -2;
    total += line.length();
    if (total > 8192) return -2;
    if (line.length() <= 1) return code;
    if (!strncasecmp_P(line.c_str(), PSTR("date:"), 5)) {
      int64_t d = httpDateToEpoch(line.c_str() + 5);
      if (d > 0) httpDateEpoch = (uint32_t)d;
    }
  }
  return -2;
}

static const char WX_HOST[] = "api.open-meteo.com";

static bool fetchWeather() {
  WiFiClient client;
  String url;
  url.reserve(384);
  url = F("http://");
  url += WX_HOST;
  url += F("/v1/forecast?latitude=");
  appendFixed(url, cfg.lat, 4);
  url += F("&longitude=");
  appendFixed(url, cfg.lon, 4);
  url += F("&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&minutely_15=precipitation,precipitation_probability&forecast_minutely_15=4"
           "&timezone=auto&forecast_days=");
  url += UI_DAYS;
  if (httpGet(client, url, 3000) != 200) return false;
  if (!timeValid() && httpDateEpoch) softTime(httpDateEpoch, 3);  // reserva só para mostrar até o SNTP chegar
  JsonDocument filter;
  filter["minutely_15"]["precipitation"] = true;  // 4 quartos de hora a partir do atual: a próxima hora
  filter["minutely_15"]["precipitation_probability"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["current"]["time"] = true;  // hora local do serviço: a segunda fonte que confirma a do SNTP
  filter["utc_offset_seconds"] = true;
  filter["timezone"] = true;  // fuso da cidade: o painel confere se bate com o do relógio
  filter["daily"]["weather_code"] = true;
  filter["daily"]["precipitation_probability_max"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  filter["daily"]["sunrise"] = true;
  filter["daily"]["sunset"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));
  client.stop();
  if (err || doc["current"]["temperature_2m"].isNull()) return false;
  wx.t = doc["current"]["temperature_2m"];
  wx.hum = doc["current"]["relative_humidity_2m"];
  wx.code = doc["current"]["weather_code"];
  wx.day = doc["current"]["is_day"] | 1;
  wx.tmax = doc["daily"]["temperature_2m_max"][0];
  wx.tmin = doc["daily"]["temperature_2m_min"][0];
  // a resposta vem em HTTP puro: limita antes de converter os decimais de /api/state
  for (float *v : {&wx.t, &wx.tmax, &wx.tmin}) *v = isfinite(*v) ? constrain(*v, -99.0f, 99.0f) : 0;
  // vai chover: o primeiro quarto de hora com chuva de verdade (≥ 0,1 mm e chance ≥ 40%), se agora
  // ainda não chove. O atual vira "em instantes" (5); os seguintes, 15/30/45 min.
  bool rainingNow = (wx.code >= 51 && wx.code <= 67) || (wx.code >= 80 && wx.code <= 82) || wx.code >= 95;
  JsonArrayConst qp = doc["minutely_15"]["precipitation"], qc = doc["minutely_15"]["precipitation_probability"];
  wx.rainIn = 0;
  for (int i = 0; !rainingNow && i < 4 && i < (int)qp.size(); i++)
    if (qp[i].as<float>() >= 0.1f && (qc[i].isNull() || qc[i].as<int>() >= 40)) {
      wx.rainIn = i ? i * 15 : 5;
      break;
    }
  JsonArrayConst dc = doc["daily"]["weather_code"], dmax = doc["daily"]["temperature_2m_max"],
                 dmin = doc["daily"]["temperature_2m_min"], drain = doc["daily"]["precipitation_probability_max"];
  wx.nd = 0;
  for (int i = 0; i < UI_DAYS && i < (int)dc.size() && i < (int)dmax.size() && i < (int)dmin.size(); i++) {
    auto deg = [](float v) { return (int8_t)constrain(lroundf(v), -99, 99); };
    wx.d[i] = {dc[i].as<uint8_t>(), deg(dmin[i].as<float>()), deg(dmax[i].as<float>()),
               drain[i].isNull() ? (uint8_t)255 : (uint8_t)constrain(drain[i].as<int>(), 0, 100)};
    wx.nd = i + 1;
  }
  // "2026-10-21T05:31" (hora local, timezone=auto) -> minutos do dia
  auto minutes = [](const char *iso) {
    const char *p = iso ? strchr(iso, 'T') : nullptr;
    return p && strlen(p) >= 6 ? (p[1] - '0') * 600 + (p[2] - '0') * 60 + (p[4] - '0') * 10 + (p[5] - '0') : -1;
  };
  sunRise = minutes(doc["daily"]["sunrise"][0] | (const char *)nullptr);
  sunSet = minutes(doc["daily"]["sunset"][0] | (const char *)nullptr);
  int64_t local = isoToEpoch(doc["current"]["time"] | (const char *)nullptr);
  // Com hora de reserva (RTC ou cabeçalho HTTP) o clima só confirmaria contra a mesma fonte: espera o SNTP.
  if (local > 0 && !timeSoft) {  // confere a hora (veio por TCP): confirma a atual ou a que o SNTP trouxe e foi desfeita
    timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t want = timeGuard.onWeather(local - doc["utc_offset_seconds"].as<int32_t>(), tv.tv_sec, millis());
    if (want != tv.tv_sec) {
      timeval w = {(time_t)want, 0};
      settimeofday(&w, nullptr);
    }
  }
  strlcpy(wx.tzName, doc["timezone"] | "", sizeof wx.tzName);
  for (char *c = wx.tzName; *c; c++)  // nome IANA ("America/Sao_Paulo"): só isso vai para o JSON do estado
    if (!isalnum((unsigned char)*c) && !strchr("/_+-", *c)) *c = 0;
  wx.utcOff = doc["utc_offset_seconds"] | 0;
  wx.ok = true;
  wx.version++;
  return true;
}

void weatherLoop() {
  if (!cfg.weatherOn || !cfg.city[0] || WiFi.status() != WL_CONNECTED) return;
  if (!wxBack.due(millis())) return;
  // a cada 15 min; falhando, espera 2, 4, 8 e 15 min. O nome vem do cache; buscando, o laço segue (sem parar)
  uint32_t ip;
  int d = dnsResolve(WX_HOST, ip);
  if (d == 0) return;
  diagWhere = 1;
  bool ok = d > 0 && fetchWeather();
  if (ok) wxBack.ok(millis(), 15UL * 60000UL);
  else wxBack.fail(millis());
}
