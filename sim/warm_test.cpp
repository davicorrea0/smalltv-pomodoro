#include "../smalltv-pomodoro/warm.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>

// Registra o que iria ao chip (cmd + parâmetros), no lugar do tft.sendCommand
struct Cmd { uint8_t c; std::vector<uint8_t> d; };
static std::vector<Cmd> log_;
static void rec(uint8_t c, uint8_t *d, uint8_t n) { log_.push_back({c, std::vector<uint8_t>(d, d + n)}); }

int main() {
  // de dia (ou cor quente desligada): só o DGMEN desligado, nada de tabela
  log_.clear();
  warmApply(rec, 0);
  assert(log_.size() == 1 && log_[0].c == 0xBA && log_[0].d.size() == 1 && log_[0].d[0] == 0x00);

  // noite a 100 %: tabelas primeiro, DGMEN (bit 2) por último
  log_.clear();
  warmApply(rec, 100);
  assert(log_.size() == 3);
  assert(log_[0].c == 0xE2 && log_[0].d.size() == 64);
  assert(log_[1].c == 0xE3 && log_[1].d.size() == 64);
  assert(log_[2].c == 0xBA && log_[2].d.size() == 1 && log_[2].d[0] == 0x04);
  for (int i = 0; i < 64; i++) assert(log_[0].d[i] == (i * 255 + 31) / 63);  // vermelho intacto, 8 bits
  assert(log_[0].d[0] == 0 && log_[0].d[63] == 255);
  const auto &b = log_[1].d;
  assert(b[0] == 0);
  assert(b[63] == 140);  // 255 * 55 / 100, arredondado
  for (int i = 0; i < 64; i++) {
    assert(b[i] <= log_[0].d[i]);
    if (i) assert(b[i] >= b[i - 1]);  // nunca desce
  }

  // intensidade intermediária: azul entre o intacto e o de 100 %
  uint8_t l50[64], l1[64], l100[64];
  warmLutBlue(l50, 50);
  warmLutBlue(l1, 1);
  warmLutBlue(l100, 100);
  assert(l50[63] > l100[63] && l50[63] < 255);
  assert(l1[63] >= 253);
  for (int p = 0; p <= 100; p++) {
    uint8_t l[64];
    warmLutBlue(l, p);
    for (int i = 0; i < 64; i++) assert(l[i] <= warmId(i) && l[i] >= l100[i]);
  }
  // tabela conjugada (INVON aplicado antes da tabela): 255 - f(255 - x) sobre os mesmos 64 degraus
  uint8_t c100[64];
  warmLutBlue(c100, 100, true);
  for (int i = 0; i < 64; i++) assert(c100[i] == 255 - l100[63 - i]);
  assert(c100[0] == 255 - 140 && c100[63] == 255);  // o claro do dado invertido fica sem corte
  log_.clear();
  warmApply(rec, 100, true);
  assert(log_.size() == 3 && log_[1].c == 0xE3 && log_[1].d[63] == 255 && log_[2].d[0] == 0x04);
  warmApply(rec, 0, true);
  assert(log_.back().c == 0xBA && log_.back().d[0] == 0);

  // qual intensidade vale: dia = 0; noite = ajuste; a prévia vence os dois (e o 0 da prévia também)
  assert(warmTarget(false, false, 80, 0) == 0);
  assert(warmTarget(true, false, 80, 0) == 80);
  assert(warmTarget(true, false, 0, 0) == 0);
  assert(warmTarget(false, true, 0, 60) == 60);
  assert(warmTarget(true, true, 80, 30) == 30);
  assert(warmTarget(true, false, 250, 0) == 100);  // lixo nunca passa de 100

  // dia -> noite -> prévia -> dia: o último comando é sempre o DGMEN desligado, sem nada preso
  log_.clear();
  bool nights[] = {false, true, false, true, false};
  for (bool n : nights) warmApply(rec, warmTarget(n, false, 70, 0));
  warmApply(rec, warmTarget(false, true, 70, 40));
  warmApply(rec, warmTarget(false, false, 70, 40));
  assert(log_.back().c == 0xBA && log_.back().d[0] == 0x00);
  assert(log_.size() == 1 + 3 + 1 + 3 + 1 + 3 + 1);
  puts("warm: ok");
}
