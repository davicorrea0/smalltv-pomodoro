// sinf/cosf próprios, no lugar dos da libm do newlib (core 3.1.2), sem mudar nenhuma chamada.
//
// Por quê: a redução de argumento da libm (__ieee754_rem_pio2f + __kernel_rem_pio2f) traz as tabelas two_over_pi,
// npio2_hw e PIo2 como .rodata, que no ESP8266 fica na RAM: ~976 B de RAM fixa por um caminho (|x| > ~201 rad) que
// nenhum mostrador usa. Estas definições substituem os símbolos no link (o objeto do sketch vem antes do -lm), e
// os objetos da libm deixam de entrar.
//
// Como: é o mesmo algoritmo do newlib (sf_sin.c, sf_cos.c, ef_rem_pio2.c, kf_sin.c, kf_cos.c), com as mesmas
// constantes e a mesma ordem das operações em float, conferidas na desmontagem do libm.a do toolchain. Até
// |x| <= 2^7·π/2 o resultado é igual bit a bit ao da libm: o sim/trig_test.cpp compara com uma transcrição do
// newlib em todos os floats dessa faixa. Diferenças:
//  - a tabela npio2_hw (128 B) fica na flash (PROGMEM). Ela não pode sair: sem ela o resultado muda em alguns
//    floats perto de n·π/2, porque o newlib pula a 2ª volta da redução quando x não bate com a tabela;
//  - |x| > 2^7·π/2 (sem o Payne–Hanek e sem two_over_pi): redução de Cody–Waite em double (soma e multiplicação
//    de double ficam na ROM do ESP8266), com erro do mesmo tamanho do newlib (~1,3 ulp) até |x| < 2^19; acima
//    disso, fmodf(|x|, 2π) antes: resultado sempre entre −1 e 1, mas sem precisão (nesse tamanho o float da
//    entrada já não tem casas decimais). Os mostradores usam ângulos de poucas voltas.
// Nada de IRAM_ATTR: o código fica na flash, como o da libm.
//
// Baseado no fdlibm, versão float do newlib:
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
// Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
// (cópia em licenses/fdlibm-LICENSE.txt)
#include <math.h>
#include <stdint.h>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP8266)
#include <pgmspace.h>
#else  // simulador e teste do PC
#define PROGMEM
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#endif

namespace {

// ef_rem_pio2.c: parte alta de n·π/2 (n = 1..32), para saber quando x está perto de um múltiplo de π/2
const uint32_t npio2_hw[32] PROGMEM = {
  0x3fc90f00, 0x40490f00, 0x4096cb00, 0x40c90f00, 0x40fb5300, 0x4116cb00, 0x412fed00, 0x41490f00,
  0x41623100, 0x417b5300, 0x418a3a00, 0x4196cb00, 0x41a35c00, 0x41afed00, 0x41bc7e00, 0x41c90f00,
  0x41d5a000, 0x41e23100, 0x41eec200, 0x41fb5300, 0x4203f200, 0x420a3a00, 0x42108300, 0x4216cb00,
  0x421d1400, 0x42235c00, 0x4229a500, 0x422fed00, 0x42363600, 0x423c7e00, 0x4242c700, 0x42490f00,
};

inline uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
inline float fromBits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

// kf_sin.c: seno em [−π/4, π/4]; y é a cauda de x, iy = 0 quando y é zero
float kSin(float x, float y, int iy) {
  const float half = 5.0000000000e-01f,
    S1 = -1.6666667163e-01f,  // 0xbe2aaaab
    S2 = 8.3333337680e-03f,   // 0x3c088889
    S3 = -1.9841270114e-04f,  // 0xb9500d01
    S4 = 2.7557314297e-06f,   // 0x3638ef1b
    S5 = -2.5050759689e-08f,  // 0xb2d72f34
    S6 = 1.5896910177e-10f;   // 0x2f2ec9d3
  uint32_t ix = fbits(x) & 0x7fffffff;
  if (ix < 0x32000000) return x;  // |x| < 2^-27 (o newlib confere (int)x == 0, sempre verdadeiro aqui)
  float z = x * x, v = z * x;
  float r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  if (iy == 0) return x + v * (S1 + z * r);
  return x - ((z * (half * y - v * r) - y) - v * S1);
}

// kf_cos.c: cosseno em [−π/4, π/4]
float kCos(float x, float y) {
  const float one = 1.0f,
    C1 = 4.1666667908e-02f,   // 0x3d2aaaab
    C2 = -1.3888889225e-03f,  // 0xbab60b61
    C3 = 2.4801587642e-05f,   // 0x37d00d01
    C4 = -2.7557314297e-07f,  // 0xb493f27c
    C5 = 2.0875723372e-09f,   // 0x310f74f6
    C6 = -1.1359647598e-11f;  // 0xad47d74e
  uint32_t ix = fbits(x) & 0x7fffffff;
  if (ix < 0x32000000) return one;  // |x| < 2^-27
  float z = x * x;
  float r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  if (ix < 0x3e99999a) return one - (0.5f * z - (z * r - x * y));  // |x| < 0.3
  float qx = ix > 0x3f480000 ? 0.28125f : fromBits(ix - 0x01000000);  // x/4
  float hz = 0.5f * z - qx, a = one - qx;
  return a - (hz - (z * r - x * y));
}

// |x| > 2^7·π/2, finito: Cody–Waite em double (π/2 em pedaços de 33 bits; fn·pio2_1 é exato para n < 2^20)
int32_t remBig(float x, float *y) {
  float ax = fromBits(fbits(x) & 0x7fffffff);
  if (ax >= 524288.0f) ax = fmodf(ax, 6.28318548f);  // 2^19: n caberia, mas o produto deixaria de ser exato
  const double invpio2 = 6.36619772367581382433e-01,
    pio2_1 = 1.57079632673412561417e+00,   // 0x3FF921FB54400000
    pio2_1t = 6.07710050650619224932e-11;  // 0x3DD0B4611A626331
  double t = ax;
  int32_t n = (int32_t)(t * invpio2 + 0.5);
  double fn = n, r = t - fn * pio2_1 - fn * pio2_1t;
  float y0 = (float)r;
  float y1 = (float)(r - y0);
  if ((int32_t)fbits(x) < 0) { y[0] = -y0; y[1] = -y1; return -n; }
  y[0] = y0; y[1] = y1;
  return n;
}

// ef_rem_pio2.c: x = n·π/2 + y[0] + y[1], |y| <= π/4; devolve n
int32_t remPio2(float x, float *y) {
  const float half = 5.0000000000e-01f,
    invpio2 = 6.3661980629e-01f,  // 0x3f22f984
    pio2_1 = 1.5707855225e+00f,   // 0x3fc90f80
    pio2_1t = 1.0804334124e-05f,  // 0x37354443
    pio2_2 = 1.0804273188e-05f,   // 0x37354400
    pio2_2t = 6.0770999344e-11f,  // 0x2e85a308
    pio2_3 = 6.0770943833e-11f,   // 0x2e85a300
    pio2_3t = 6.1232342629e-17f;  // 0x248d3132
  int32_t hx = (int32_t)fbits(x), ix = hx & 0x7fffffff;
  if (ix <= 0x3f490fd8) { y[0] = x; y[1] = 0; return 0; }  // |x| <= π/4
  if (ix < 0x4016cbe4) {  // |x| < 3π/4: n = ±1
    float z;
    if (hx > 0) {
      z = x - pio2_1;
      if ((ix & 0xfffffff0) != 0x3fc90fd0) {
        y[0] = z - pio2_1t;
        y[1] = (z - y[0]) - pio2_1t;
      } else {  // perto de π/2: π com 24+24+24 bits
        z -= pio2_2;
        y[0] = z - pio2_2t;
        y[1] = (z - y[0]) - pio2_2t;
      }
      return 1;
    }
    z = x + pio2_1;
    if ((ix & 0xfffffff0) != 0x3fc90fd0) {
      y[0] = z + pio2_1t;
      y[1] = (z - y[0]) + pio2_1t;
    } else {
      z += pio2_2;
      y[0] = z + pio2_2t;
      y[1] = (z - y[0]) + pio2_2t;
    }
    return -1;
  }
  if (ix <= 0x43490f80) {  // |x| <= 2^7·π/2
    float t = fromBits(ix);
    int32_t n = (int32_t)(t * invpio2 + half);
    float fn = (float)n;
    float r = t - fn * pio2_1;
    float w = fn * pio2_1t;  // 1ª volta: 40 bits
    y[0] = r - w;
    if (n >= 32 || (uint32_t)(ix & 0xffffff00) == pgm_read_dword(&npio2_hw[n - 1])) {  // pode haver cancelamento
      int32_t j = ix >> 23;
      int32_t i = j - (int32_t)((fbits(y[0]) >> 23) & 0xff);
      if (i > 8) {  // 2ª volta: 57 bits
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        i = j - (int32_t)((fbits(y[0]) >> 23) & 0xff);
        if (i > 25) {  // 3ª volta: 74 bits
          t = r;
          w = fn * pio2_3;
          r = t - w;
          w = fn * pio2_3t - ((t - r) - w);
          y[0] = r - w;
        }
      }
    }
    y[1] = (r - y[0]) - w;
    if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
    return n;
  }
  if (ix >= 0x7f800000) { y[0] = y[1] = x - x; return 0; }  // inf ou NaN: NaN
  return remBig(x, y);
}

}  // namespace

float trigSinf(float x) {
  uint32_t ix = fbits(x) & 0x7fffffff;
  if (ix <= 0x3f490fd8) return kSin(x, 0.0f, 0);
  if (ix >= 0x7f800000) return x - x;
  float y[2];
  switch (remPio2(x, y) & 3) {
    case 0: return kSin(y[0], y[1], 1);
    case 1: return kCos(y[0], y[1]);
    case 2: return -kSin(y[0], y[1], 1);
    default: return -kCos(y[0], y[1]);
  }
}

float trigCosf(float x) {
  uint32_t ix = fbits(x) & 0x7fffffff;
  if (ix <= 0x3f490fd8) return kCos(x, 0.0f);
  if (ix >= 0x7f800000) return x - x;
  float y[2];
  switch (remPio2(x, y) & 3) {
    case 0: return kCos(y[0], y[1]);
    case 1: return -kSin(y[0], y[1], 1);
    case 2: return -kCos(y[0], y[1]);
    default: return kSin(y[0], y[1], 1);
  }
}

// o teste do host (sim/trig_test.cpp) define TRIG_SEM_SIMBOLOS para comparar com a libm do PC
#ifndef TRIG_SEM_SIMBOLOS
extern "C" float sinf(float x) { return trigSinf(x); }
extern "C" float cosf(float x) { return trigCosf(x); }
#endif
