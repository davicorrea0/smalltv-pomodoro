// Corpo HTTP com prazo único e teto de bytes: um par que goteja não segura o laço (fetchWeather).
// O ArduinoJson aceita qualquer tipo com read() e readBytes(); sem herdar de Stream, sem vtable.
// Fica num .h para o teste de host (sim/http_body_test.cpp) incluir a struct real.
#pragma once

static const uint16_t HTTP_BODY_MS = 3000;  // depois dos cabeçalhos (que já têm 3 s): pior caso 6 s
static const uint16_t HTTP_BODY_MAX = 8192;  // a resposta do clima tem ~1,4 KB

struct HttpBody {
  WiFiClient &c;
  uint32_t t0;
  uint16_t left;

  int read() {  // espera o próximo byte até o prazo; -1 no fim, no prazo ou no teto
    while (left && millis() - t0 < HTTP_BODY_MS) {
      if (c.available()) {
        int ch = c.read();
        if (ch >= 0) {
          left--;
          return ch;
        }
      } else if (!c.connected()) return -1;
      else delay(1);
    }
    return -1;
  }

  size_t readBytes(char *buf, size_t n) {
    size_t i = 0;
    for (int ch; i < n && (ch = read()) >= 0;) buf[i++] = (char)ch;
    return i;
  }
};
