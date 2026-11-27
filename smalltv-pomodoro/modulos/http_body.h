// Corpo HTTP com prazo único e teto de bytes: um par que goteja não segura o laço (fetchWeather).
// Decodifica Transfer-Encoding: chunked, que o servidor usa quando o pedido é HTTP/1.1 com keep-alive.
// O ArduinoJson aceita qualquer tipo com read() e readBytes(); sem herdar de Stream, sem vtable.
// Fica num .h para o teste de host (sim/http_body_test.cpp) incluir a struct real.
#pragma once

static const uint16_t HTTP_BODY_MS = 3000;  // depois dos cabeçalhos (que já têm 3 s): pior caso 6 s
static const uint16_t HTTP_BODY_MAX = 8192;  // a resposta do clima tem ~1,4 KB

struct HttpBody {
  WiFiClient &c;
  uint32_t t0;
  uint16_t left;          // teto em bytes da conexão, contando os tamanhos dos pedaços
  bool chunked = false;   // Transfer-Encoding: chunked
  uint16_t inChunk = 0;   // bytes que faltam no pedaço atual
  bool afterChunk = false, ended = false;

  // Próximo byte do corpo, sem os tamanhos e os \r\n dos pedaços. -1 no fim, no prazo, no teto ou em pedaço
  // malformado: o ArduinoJson devolve IncompleteInput e nunca recebe os bytes de controle como JSON.
  int read() {
    if (!chunked) return raw();
    if (!inChunk) {
      if (ended) return -1;
      if (afterChunk && (raw() != '\r' || raw() != '\n')) return stop();  // \r\n que fecha o pedaço anterior
      uint32_t n = 0;
      uint8_t digits = 0;
      bool ext = false;  // ";extensão" depois do tamanho: ignorada
      int ch;
      while ((ch = raw()) >= 0 && ch != '\n') {
        if (ext || ch == '\r') continue;
        if (ch == ';') {
          ext = true;
          continue;
        }
        int v = ch >= '0' && ch <= '9' ? ch - '0' : (ch | 32) >= 'a' && (ch | 32) <= 'f' ? (ch | 32) - 'a' + 10 : -1;
        if (v < 0 || ++digits > 4) return stop();  // 4 dígitos (até 64 KB) já passam do teto de 8 KB
        n = n * 16 + v;
      }
      if (ch < 0 || !digits || !n) return stop();  // conexão no fim, linha sem tamanho ou o pedaço 0 (fim do corpo)
      inChunk = (uint16_t)n;
      afterChunk = true;
    }
    int ch = raw();
    if (ch < 0) return stop();
    inChunk--;
    return ch;
  }

  size_t readBytes(char *buf, size_t n) {
    size_t i = 0;
    for (int ch; i < n && (ch = read()) >= 0;) buf[i++] = (char)ch;
    return i;
  }

  int stop() {  // fim do corpo: as próximas leituras devolvem -1
    ended = true;
    inChunk = 0;
    return -1;
  }

  int raw() {  // espera o próximo byte até o prazo; -1 no fim, no prazo ou no teto
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
};
