// mDNS (smalltv.local) e emulação de lâmpadas Hue para a Alexa.
#include "app.h"

// ---------- Alexa ----------
// O relógio aparece para a Alexa como lâmpadas reguláveis de uma ponte Philips Hue, na rede local: sem
// conta, skill nem nuvem. É o mesmo protocolo do Espalexa (respostas iguais às dele, que os Echo aceitam),
// reduzido ao que o Pomodoro usa e sem cor: a biblioteca inteira custaria ~26 KB de flash.
//   descoberta: o Echo manda M-SEARCH por SSDP (UDP 239.255.255.250:1900) e lê /description.xml;
//   controle:   GET /api/<usuário>/lights e PUT /api/<usuário>/lights/<id>/state {"on":..,"bri":..}.
// Lâmpadas (o nome sai no idioma do relógio na hora da descoberta):
//   Pomodoro    ligar: começa o foco (ou continua um pausado); desligar: pausa, e de novo encerra;
//               "em 50%": foco de 50 minutos
//   Pausa       ligar: pausa curta ("em 10%": de 10 minutos); desligar: volta ao foco
//   Pausa longa ligar: pausa longa ("em 30%": de 30 minutos); desligar: volta ao foco
// A lâmpada acesa mostra a fase atual no app da Alexa.
// Nome na rede, sem decorar o IP:
//  - lanName: http://smalltv.local; com outro relógio já usando, smalltv-2.local, -3... (conferido ao conectar)
//  - netName: "smalltv-" + 4 hex do chip, sempre válido como reserva e usado no DHCP.
// Só os registros A e AAAA, com o leitor de pacotes de security.h (testado): o ESP8266mDNS custaria ~21 KB.
char netName[16], lanName[12];

static WiFiUDP hueUdp;

static WiFiUDP mdnsUdp;

static IPAddress mdnsIp;

static void mdnsSend(const char *name, uint16_t id, bool legacy, MdnsTo to, uint8_t want = MDNS_A) {
  uint8_t out[96], ip[4] = {mdnsIp[0], mdnsIp[1], mdnsIp[2], mdnsIp[3]};
  size_t m = mdnsAnswer(out, sizeof out, name, ip, id, legacy, want);
  if (!m) return;
  if (to == MDNS_DIRECT) mdnsUdp.beginPacket(mdnsUdp.remoteIP(), mdnsUdp.remotePort());
  else if (to == MDNS_BROADCAST) mdnsUdp.beginPacket(IPAddress((uint32_t)mdnsIp | ~(uint32_t)WiFi.subnetMask()), 5353);
  else mdnsUdp.beginPacketMulticast(IPAddress(224, 0, 0, 251), 5353, mdnsIp);
  mdnsUdp.write(out, m);
  mdnsUdp.endPacket();
}

static void mdnsAnnounce() {
  if (lanName[0]) mdnsSend(lanName, 0, false, MDNS_GROUP);
  mdnsSend(netName, 0, false, MDNS_GROUP);
}

static bool mdnsJoin() {
  mdnsIp = WiFi.localIP();
  mdnsUdp.stop();
  return mdnsUdp.beginMulticast(mdnsIp, IPAddress(224, 0, 0, 251), 5353);
}

// Alguém já responde por esse nome? Pergunta 3 vezes, 250 ms cada (RFC 6762, 8.1).
static bool mdnsTaken(const char *name) {
  uint8_t q[48], b[256];
  size_t m = mdnsProbeQuery(q, sizeof q, name);
  for (int t = 0; t < 3 && m; t++) {
    mdnsUdp.beginPacketMulticast(IPAddress(224, 0, 0, 251), 5353, mdnsIp);
    mdnsUdp.write(q, m);
    mdnsUdp.endPacket();
    for (uint32_t t0 = millis(); millis() - t0 < 250; delay(10)) {
      if (mdnsUdp.parsePacket() <= 0) continue;
      int n = mdnsUdp.read(b, sizeof b);
      if (n > 0 && mdnsHasAnswer(b, n, name)) return true;
    }
  }
  return false;
}

// Ao conectar: escolhe o nome curto livre e se anuncia.
static bool mdnsUp = false;  // Wi-Fi conectado na última volta do mdnsLoop (reconectou: entra de novo no grupo)

void mdnsStart() {
  // Sem a economia de energia do Wi-Fi: cochilando entre os beacons, o ESP8266 perde pacotes multicast, e o
  // .local some "às vezes". O relógio vive na tomada; o custo é ~15 mA.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  lanName[0] = 0;
  mdnsUp = true;
  if (!mdnsJoin()) return;
  for (int i = 1; i <= 9; i++) {
    char name[sizeof lanName];
    if (i == 1) strlcpy(name, HOSTNAME, sizeof name);
    else snprintf(name, sizeof name, HOSTNAME "-%d", i);
    if (!mdnsTaken(name)) {
      strlcpy(lanName, name, sizeof lanName);
      break;
    }
  }
  mdnsAnnounce();
}

void mdnsLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    mdnsUp = false;
    return;
  }
  if (!mdnsUp || WiFi.localIP() != mdnsIp) {  // reconectou ou o IP mudou: entra de novo no grupo e se anuncia
    mdnsUp = true;
    if (mdnsJoin()) mdnsAnnounce();
  }
  int len = mdnsUdp.parsePacket();
  if (len <= 0) return;
  uint8_t b[256];  // a pergunta vem no começo; o resto (respostas conhecidas) pode ficar de fora
  int n = mdnsUdp.read(b, sizeof b);
  if (n <= 0) return;
  bool legacy = mdnsUdp.remotePort() != 5353;
  uint16_t id = (uint16_t)(b[0] << 8 | b[1]);
  uint8_t want = lanName[0] ? mdnsAsksFor(b, n, lanName, nullptr) : 0;
  const char *name = want ? lanName : (want = mdnsAsksFor(b, n, netName, nullptr)) ? netName : nullptr;
  if (!name) return;
  if (legacy) {  // resolvedor comum: só ele espera a resposta
    mdnsSend(name, id, true, MDNS_DIRECT, want);
    return;
  }
  // Por multicast mesmo quando pediram resposta direta (bit QU; a RFC 6762, 5.4, permite): no Windows vários programas
  // escutam a porta 5353 (Chrome, Edge...) e a resposta direta pode cair num deles em vez do resolvedor, que então
  // espera ~2,8 s. E uma cópia direta para quem perguntou: há roteadores que não repassam multicast no Wi-Fi.
  mdnsSend(name, id, false, MDNS_GROUP, want);
  mdnsSend(name, id, false, MDNS_DIRECT, want);
  // E por broadcast da sub-rede: roteador com 2,4 e 5 GHz na mesma rede pode não levar o multicast do relógio (2,4 GHz)
  // até o PC no 5 GHz, e a cópia direta cai num dos programas da porta 5353. O resolvedor do Windows aceita a resposta
  // por broadcast (medido). No máximo 4 por segundo: uma busca do Windows faz 12 perguntas em ~0,9 s.
  static RateLimit bcastLimit;
  if (bcastLimit.take(millis(), 4)) mdnsSend(name, id, false, MDNS_BROADCAST, want);
}

static bool alexaUp = false;     // ouvindo a descoberta

static uint8_t alexaBri = 255;   // último brilho pedido (1-255)

uint32_t alexaSeen = 0;   // millis() do último pedido de um Echo (0 = nenhum desde que ligou)

IPAddress alexaIp;        // de onde veio

// último comando para o painel: 0 nenhum, 1 começou/continuou o foco, 2 pausou, 3 foco de N min,
// 4 encerrou, 5 pausa curta, 6 pausa longa, 7 voltou ao foco, 8 pausa de N min
uint8_t alexaLast = 0;

uint8_t alexaMin = 0;     // N dos comandos 3 e 8

static char hueMac[13];          // MAC em minúsculas, sem ":" (identificador da "ponte")

static const int HUE_LIGHTS = 3;

// chave da lâmpada como o Espalexa: 24 bits de baixo do MAC << 7 | índice (int, pode ser negativa)
static long hueKey(int i) { return (long)(int)((strtoul(hueMac + 6, nullptr, 16) << 7) | i); }

static bool hueOn(int i) {
  return i == 0 ? phase == WORK && running : i == 1 ? phase == SHORT_BREAK : phase == LONG_BREAK;
}

static void hueLight(String &j, int i) {
  uint8_t m[6];
  WiFi.macAddress(m);
  char id[27];
  snprintf(id, sizeof id, "%02X:%02X:%02X:%02X:%02X:%02X:00:11-%02X", m[0], m[1], m[2], m[3], m[4], m[5], i + 1);
  j += F("{\"state\":{\"on\":");
  j += hueOn(i) ? F("true") : F("false");
  j += F(",\"bri\":");
  j += i ? 254 : alexaBri - 1;
  j += F(",\"alert\":\"none\",\"mode\":\"homeautomation\",\"reachable\":true},\"type\":\"Dimmable light\",\"name\":\"");
  j += i == 0 ? tr("Pomodoro", "Pomodoro", "Pomodoro") : i == 1 ? tr("Pausa", "Break", "Pausa") : tr("Pausa longa", "Long break", "Pausa larga");
  j += F("\",\"modelid\":\"LWB010\",\"manufacturername\":\"Philips\",\"productname\":\"E1\",\"uniqueid\":\"");
  j += id;
  j += F("\",\"swversion\":\"espalexa-2.7.0\"}");
}

// comando de uma lâmpada: ligar, desligar ou brilho (minutos)
static void hueCommand(int i, const String &body) {
  bool off = body.indexOf("false") > 0, bri = body.indexOf("bri") > 0;
  int min = 0;
  if (bri) {
    int b = body.substring(body.indexOf("bri") + 5).toInt();
    b = b >= 255 ? 255 : b + 1;
    if (i == 0) alexaBri = b;
    min = b * 100 / 255;
    min = min < 1 ? 1 : min > 99 ? 99 : min;
  }
  if (i == 0) {
    if (off) {
      alexaLast = phase != IDLE && running ? 2 : 4;  // rodando: pausa; já pausado (ou na pausa): encerra
      command(alexaLast == 2 ? "pause" : "reset");
    } else if (bri) {
      startPhase(WORK, true);
      totalMs = remainingMs = min * 60000UL;
      alexaLast = 3;
    } else {
      alexaLast = 1;
      command(phase == WORK ? "start" : "focus");
    }
  } else if (off) {  // desligar uma pausa: volta ao foco
    if (phase == (i == 1 ? SHORT_BREAK : LONG_BREAK)) {
      command("focus");
      alexaLast = 7;
    }
  } else {
    startPhase(i == 1 ? SHORT_BREAK : LONG_BREAK, true);
    if (bri) totalMs = remainingMs = min * 60000UL;
    alexaLast = bri ? 8 : i == 1 ? 5 : 6;
  }
  alexaMin = min;
  previewUntil = 0;  // a tela vai direto para o timer
}

void alexaApi() {
  const String &req = server.uri(), &body = server.arg("plain");
  if (!cfg.alexa || req.indexOf("api") < 0) {
    server.send(404, "text/plain", "");
    return;
  }
  if (server.hasHeader("Origin")) {  // o Echo não manda Origin; navegador manda: é outro site tentando mexer no timer
    server.send(403, "text/plain", "");
    return;
  }
  alexaSeen = millis() | 1;  // um Echo falou com o relógio (o painel mostra)
  alexaIp = server.client().remoteIP();
  if (body.indexOf("devicetype") > 0) {  // pede um usuário da API do Hue: qualquer um serve
    server.send(200, "application/json", F("[{\"success\":{\"username\":\"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]"));
    return;
  }
  int pos = req.indexOf("lights");
  long key = pos > 0 ? req.substring(pos + 7).toInt() : 0;
  int i = key & 127;  // índice da lâmpada na chave
  if (req.indexOf("state") > 0 && body.length()) {
    server.send(200, "application/json", F("[{\"success\":{\"/lights/1/state/\": true}}]"));
    if (i < HUE_LIGHTS) hueCommand(i, body);
    return;
  }
  String j;
  j.reserve(512);
  if (pos > 0 && key == 0) {  // todas as lâmpadas
    j = '{';
    for (int k = 0; k < HUE_LIGHTS; k++) {
      j += k ? ",\"" : "\"";
      j += hueKey(k);
      j += "\":";
      hueLight(j, k);
    }
    j += '}';
  } else if (pos > 0 && i < HUE_LIGHTS) hueLight(j, i);
  else j = "{}";
  server.send(200, "application/json", j);
}

void alexaBegin() {
  if (alexaUp || !cfg.alexa) return;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  strlcpy(hueMac, mac.c_str(), sizeof hueMac);
  alexaUp = hueUdp.beginMulticast(WiFi.localIP(), IPAddress(239, 255, 255, 250), 1900);
}

// responde às buscas por SSDP do Echo apontando para /description.xml
void alexaLoop() {
  if (!alexaUp) return;
  int n = hueUdp.parsePacket();
  if (n < 1) return;
  char p[256];
  int len = hueUdp.read(p, sizeof p - 1);
  p[len < 0 ? 0 : len] = 0;
  hueUdp.flush();
  if (!cfg.alexa || !strstr(p, "M-SEARCH") || !strstr(p, "ssdp:disc") ||
      !(strstr(p, "upnp:rootd") || strstr(p, "ssdp:all") || strstr(p, "asic:1")))
    return;
  // no máximo 4 respostas por segundo: o Echo pergunta poucas vezes, e um pedido pequeno vira uma resposta
  // ~3x maior (dava para usar o relógio para multiplicar tráfego na rede)
  static RateLimit ssdpLimit;
  if (!ssdpLimit.take(millis(), 4)) return;
  String ip = WiFi.localIP().toString(), r;
  r.reserve(400);
  r = F("HTTP/1.1 200 OK\r\nEXT:\r\nCACHE-CONTROL: max-age=100\r\nLOCATION: http://");
  r += ip;
  r += F(":80/description.xml\r\nSERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\nhue-bridgeid: ");
  r += hueMac;
  r += F("\r\nST: urn:schemas-upnp-org:device:basic:1\r\nUSN: uuid:2f402f80-da50-11e1-9b23-");
  r += hueMac;
  r += F("::upnp:rootdevice\r\n\r\n");
  hueUdp.beginPacket(hueUdp.remoteIP(), hueUdp.remotePort());
  hueUdp.write(r.c_str(), r.length());
  hueUdp.endPacket();
}

void sendHueDescription() {
  if (!cfg.alexa) {  // com a Alexa desligada não se anuncia (nem expõe o MAC)
    server.send(404, "text/plain", "");
    return;
  }
  String ip = WiFi.localIP().toString(), x;
  x.reserve(900);
  x = F("<?xml version=\"1.0\" ?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://");
  x += ip;
  x += F(":80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType><friendlyName>Espalexa (");
  x += ip;
  x += F(":80)</friendlyName><manufacturer>Royal Philips Electronics</manufacturer><manufacturerURL>http://www.philips.com</manufacturerURL>"
         "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription><modelName>Philips hue bridge 2012</modelName>"
         "<modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL><serialNumber>");
  x += hueMac;
  x += F("</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-");
  x += hueMac;
  x += F("</UDN><presentationURL>index.html</presentationURL></device></root>");
  server.send(200, "text/xml", x);
}
