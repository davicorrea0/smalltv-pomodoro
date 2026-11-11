// Brilho noturno 0 = tela apagada. Quando algo precisa ser visto (aviso, comando, OTA, portal), acende neste brilho mínimo útil.
#pragma once
#include <stdint.h>
#define NIGHT_WAKE_BRI 10

// until: millis() em que a tela acesa à força termina (0 = não há); now: millis() atual (cálculo com sinal, sem estourar na virada)
inline bool wakeAtivo(uint32_t until, uint32_t now) { return until && (int32_t)(now - until) < 0; }

// b: brilho pedido (já o noturno, se for noite); acordar: algo precisa ser visto agora
inline int briEfetivo(int b, bool acordar) { return b <= 0 && acordar ? NIGHT_WAKE_BRI : b; }
