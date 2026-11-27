#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static uint32_t nowMs = 0;

uint32_t millis() { return nowMs; }
void delay(uint32_t ms) { nowMs += ms; }

class WiFiClient {
 public:
  struct Byte {
    uint32_t at;
    uint8_t value;
  };

  explicit WiFiClient(std::vector<Byte> bytes) : bytes(std::move(bytes)) {}

  int available() const {
    size_t n = 0;
    while (pos + n < bytes.size() && bytes[pos + n].at <= nowMs) n++;
    return (int)n;
  }

  int read() {
    if (!available()) return -1;
    return bytes[pos++].value;
  }

  int peek() const { return available() ? bytes[pos].value : -1; }
  bool connected() const { return pos < bytes.size(); }

 private:
  std::vector<Byte> bytes;
  size_t pos = 0;
};


#include "../smalltv-pomodoro/modulos/http_body.h"

static WiFiClient immediate(const std::string &text) {
  std::vector<WiFiClient::Byte> bytes;
  bytes.reserve(text.size());
  for (uint8_t ch : text) bytes.push_back({0, ch});
  return WiFiClient(std::move(bytes));
}

static std::string readAll(HttpBody &body) {
  std::string out;
  char ch;
  while (body.readBytes(&ch, 1) == 1) out += ch;
  return out;
}

int main() {
  const std::string normal =
      R"({"latitude":-25.39,"longitude":-51.46,"generationtime_ms":0.11,"utc_offset_seconds":-10800,"timezone":"America/Sao_Paulo","timezone_abbreviation":"GMT-3","elevation":1041.0,"current_units":{"time":"iso8601","interval":"seconds","temperature_2m":"°C","relative_humidity_2m":"%","weather_code":"wmo code","is_day":""},"current":{"time":"2026-11-23T22:45","interval":900,"temperature_2m":18.7,"relative_humidity_2m":91,"weather_code":3,"is_day":0},"minutely_15_units":{"time":"iso8601","precipitation":"mm","precipitation_probability":"%"},"minutely_15":{"time":["2026-11-23T22:45","2026-11-23T23:00","2026-11-23T23:15","2026-11-23T23:30"],"precipitation":[0.00,0.10,0.00,0.00],"precipitation_probability":[12,44,25,18]},"daily_units":{"time":"iso8601","weather_code":"wmo code","temperature_2m_max":"°C","temperature_2m_min":"°C","precipitation_probability_max":"%","sunrise":"iso8601","sunset":"iso8601"},"daily":{"time":["2026-11-23","2026-11-24","2026-11-25","2026-11-26","2026-11-27"],"weather_code":[3,61,80,2,1],"temperature_2m_max":[24.1,22.8,25.0,26.4,27.2],"temperature_2m_min":[16.2,15.9,17.1,17.8,18.0],"precipitation_probability_max":[44,70,55,20,10],"sunrise":["2026-11-23T05:31","2026-11-24T05:31","2026-11-25T05:30","2026-11-26T05:30","2026-11-27T05:30"],"sunset":["2026-11-23T18:48","2026-11-24T18:49","2026-11-25T18:50","2026-11-26T18:50","2026-11-27T18:51"]}})";

  nowMs = 0;
  WiFiClient ok = immediate(normal);
  HttpBody normalBody{ok, nowMs, HTTP_BODY_MAX};
  assert(readAll(normalBody) == normal);
  assert(normal.size() < HTTP_BODY_MAX);

  nowMs = 0;
  WiFiClient drip({{1400, 'a'}, {2800, 'b'}, {4200, 'c'}});  // 1 byte a cada 1,4 s
  HttpBody slowBody{drip, nowMs, HTTP_BODY_MAX};
  assert(readAll(slowBody) == "ab");
  assert(nowMs == HTTP_BODY_MS);

  // chunked (HTTP/1.1 com keep-alive): pedaços de tamanhos variados, extensão, maiúsculas e o pedaço 0 do fim
  auto chunks = [](const std::string &body, size_t size) {
    std::string out;
    char hex[16];
    for (size_t i = 0; i < body.size(); i += size) {
      std::string part = body.substr(i, size);
      snprintf(hex, sizeof hex, "%zX", part.size());
      out += std::string(hex) + (i ? "" : ";ext=1") + "\r\n" + part + "\r\n";
    }
    return out + "0\r\n\r\n";
  };
  for (size_t size : {3, 7, 280, 536, 1000, 4096}) {
    nowMs = 0;
    WiFiClient pieces = immediate(chunks(normal, size));
    HttpBody chunkedBody{pieces, nowMs, HTTP_BODY_MAX};
    chunkedBody.chunked = true;
    assert(readAll(chunkedBody) == normal);
    assert(chunkedBody.read() == -1);  // depois do pedaço 0, o fim continua o fim
  }

  // o teto conta os bytes da conexão, tamanhos e \r\n incluídos: pedaços de 1 byte (6 bytes cada) param antes
  nowMs = 0;
  WiFiClient tiny = immediate(chunks(normal, 1));
  HttpBody tinyBody{tiny, nowMs, HTTP_BODY_MAX};
  tinyBody.chunked = true;
  std::string got = readAll(tinyBody);
  assert(got.size() < normal.size() && normal.compare(0, got.size(), got) == 0);

  // chunked com defeito: para sem entregar bytes de controle como corpo
  struct Bad { const char *raw, *body; } bad[] = {
      {"3\r\nabc", "abc"},                        // conexão acaba no meio: entrega o que veio
      {"3\r\nabcXY2\r\nde\r\n0\r\n\r\n", "abc"},  // falta o \r\n depois do pedaço
      {"G\r\nabc\r\n0\r\n\r\n", ""},              // tamanho não é hexadecimal
      {"10000\r\nabc\r\n", ""},                   // 5 dígitos: maior que qualquer corpo aceito
      {"\r\nabc\r\n", ""},                        // linha sem tamanho
      {"0\r\n\r\nabc", ""},                       // pedaço 0 logo de cara: corpo vazio
  };
  for (const Bad &b : bad) {
    nowMs = 0;
    WiFiClient broken = immediate(b.raw);
    HttpBody badBody{broken, nowMs, HTTP_BODY_MAX};
    badBody.chunked = true;
    assert(readAll(badBody) == b.body);
    assert(badBody.read() == -1);
  }

  // chunked gotejando: o prazo vale para o corpo inteiro, contando os tamanhos
  nowMs = 0;
  WiFiClient dripChunks({{0, '2'}, {0, '\r'}, {0, '\n'}, {1000, 'a'}, {2000, 'b'}, {4000, '\r'}});
  HttpBody slowChunks{dripChunks, nowMs, HTTP_BODY_MAX};
  slowChunks.chunked = true;
  assert(readAll(slowChunks) == "ab");
  assert(nowMs == HTTP_BODY_MS);

  nowMs = 0;
  std::string large(HTTP_BODY_MAX + 1, 'x');
  WiFiClient tooLarge = immediate(large);
  HttpBody limitedBody{tooLarge, nowMs, HTTP_BODY_MAX};
  assert(readAll(limitedBody).size() == HTTP_BODY_MAX);
  assert(tooLarge.available() == 1);
}
