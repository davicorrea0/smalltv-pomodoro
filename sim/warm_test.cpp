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
  // verde de LCD antigo: R = i, B = i*55/100, DGMEN ligado por último; vence a cor quente
  log_.clear();
  warmApplyState(rec, warmState(true, 70));
  assert(log_.size() == 3 && log_[0].c == 0xE2 && log_[1].c == 0xE3 && log_[2].c == 0xBA && log_[2].d[0] == 0x04);
  for (int i = 0; i < 64; i++) assert(log_[0].d[i] == i && log_[1].d[i] == i * 55 / 100);
  assert(log_[0].d[63] == 63 && log_[1].d[63] == 34);

  // entrar no LCD manda o verde; sair volta ao certo (noite com cor quente = tabelas quentes; dia = DGMEN desligado)
  // simula o warmFlush: só envia quando o estado muda
  int sent = -1;
  auto flush = [&](bool lcd, bool on, bool night) {
    uint8_t st = warmState(lcd && on, warmTarget(night, false, 70, 0));
    if (st == sent) return;
    warmApplyState(rec, st);
    sent = st;
  };
  log_.clear();
  flush(false, true, false);  // relógio comum de dia: DGMEN off
  assert(log_.size() == 1 && log_[0].c == 0xBA && log_[0].d[0] == 0);
  log_.clear();
  flush(true, true, false);  // entra no LCD: verde
  assert(log_.size() == 3 && log_[1].d[63] == 34);
  log_.clear();
  flush(true, true, false);  // continua no LCD: nada novo
  assert(log_.empty());
  flush(true, true, true);  // anoitece dentro do LCD: o verde vence, nada novo
  assert(log_.empty());
  flush(false, true, true);  // sai do LCD à noite: cor quente a 70 %
  assert(log_.size() == 3 && log_[0].d[63] == 255 && log_[1].d[63] == 176);
  log_.clear();
  flush(true, true, true);
  log_.clear();
  flush(false, true, false);  // sai do LCD de dia: DGMEN desligado
  assert(log_.size() == 1 && log_[0].c == 0xBA && log_[0].d[0] == 0);
  log_.clear();
  flush(true, false, false);  // desligado no painel: o LCD não manda tabela verde
  assert(log_.empty());
  flush(true, false, true);   // idem à noite: só a cor quente
  assert(log_.size() == 3 && log_[0].d[63] == 255);
  puts("warm: ok");
}
