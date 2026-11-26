#include <cassert>
#include <cstddef>
#include <cstdint>
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

  nowMs = 0;
  std::string large(HTTP_BODY_MAX + 1, 'x');
  WiFiClient tooLarge = immediate(large);
  HttpBody limitedBody{tooLarge, nowMs, HTTP_BODY_MAX};
  assert(readAll(limitedBody).size() == HTTP_BODY_MAX);
  assert(tooLarge.available() == 1);
}
