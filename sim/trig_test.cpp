// sinf/cosf do relógio (smalltv-pomodoro/trig.cpp) contra o newlib que eles substituem.
//
// A referência abaixo é a transcrição do newlib (sf_sin.c, sf_cos.c, ef_rem_pio2.c com a tabela npio2_hw, kf_sin.c,
// kf_cos.c), conferida contra a desmontagem do libm.a do toolchain do core 3.1.2 (constantes e ordem das operações).
// Confere:
//  1. igualdade bit a bit com a referência até |x| <= 2^7·π/2 (a faixa da libm sem Payne–Hanek, onde estão todos os
//     ângulos dos mostradores); "todos" percorre TODOS os floats dessa faixa (~2,3 bilhões, ~35 s em 12 núcleos).
//     Nessa faixa o erro é o do newlib (até ~1,3 ulp no seno e 2,9 ulp no cosseno, contra o valor em double);
//  2. redução nova em double, 2^7·π/2 < |x| < 2^19: erro abaixo de 1,5 ulp (medido: ~1,33 ulp, o dos núcleos);
//  3. |x| >= 2^19: resultado finito entre −1 e 1; inf e NaN dão NaN; ±0 e subnormais;
//  4. os ângulos que os mostradores calculam (mesmas contas das chamadas), com o maior erro em pixels num raio de 120.
// Precisa de float IEEE de verdade (SSE, sem x87): compile em 64 bits ou com -msse2 -mfpmath=sse.
// Uso: trig_test [todos]
#define TRIG_SEM_SIMBOLOS
#include "../smalltv-pomodoro/trig.cpp"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <vector>

// ---------------- referência: newlib (fdlibm float) ----------------
namespace ref {
typedef union { float value; uint32_t word; } ieee_float_shape_type;
#define GET_FLOAT_WORD(i, d) do { ieee_float_shape_type gf_u; gf_u.value = (d); (i) = gf_u.word; } while (0)
#define SET_FLOAT_WORD(d, i) do { ieee_float_shape_type sf_u; sf_u.word = (i); (d) = sf_u.value; } while (0)

static const float kone = 1.0f, khalf = 5.0000000000e-01f,
  S1 = -1.6666667163e-01f, S2 = 8.3333337680e-03f, S3 = -1.9841270114e-04f, S4 = 2.7557314297e-06f,
  S5 = -2.5050759689e-08f, S6 = 1.5896910177e-10f,
  C1 = 4.1666667908e-02f, C2 = -1.3888889225e-03f, C3 = 2.4801587642e-05f, C4 = -2.7557314297e-07f,
  C5 = 2.0875723372e-09f, C6 = -1.1359647598e-11f;

float __kernel_sinf(float x, float y, int iy) {
  float z, r, v;
  int32_t ix;
  GET_FLOAT_WORD(ix, x);
  ix &= 0x7fffffff;
  if (ix < 0x32000000) { if ((int)x == 0) return x; }
  z = x * x;
  v = z * x;
  r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  if (iy == 0) return x + v * (S1 + z * r);
  else return x - ((z * (khalf * y - v * r) - y) - v * S1);
}

float __kernel_cosf(float x, float y) {
  float a, hz, z, r, qx;
  int32_t ix;
  GET_FLOAT_WORD(ix, x);
  ix &= 0x7fffffff;
  if (ix < 0x32000000) { if (((int)x) == 0) return kone; }
  z = x * x;
  r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  if (ix < 0x3e99999a) return kone - ((float)0.5 * z - (z * r - x * y));
  else {
    if (ix > 0x3f480000) qx = (float)0.28125;
    else SET_FLOAT_WORD(qx, ix - 0x01000000);
    hz = (float)0.5 * z - qx;
    a = kone - qx;
    return a - (hz - (z * r - x * y));
  }
}

// bytes da .rodata de lib_a-ef_rem_pio2.o (objdump -s), iguais ao fonte do newlib
static const int32_t npio2_hw[] = {
  0x3fc90f00, 0x40490f00, 0x4096cb00, 0x40c90f00, 0x40fb5300, 0x4116cb00, 0x412fed00, 0x41490f00,
  0x41623100, 0x417b5300, 0x418a3a00, 0x4196cb00, 0x41a35c00, 0x41afed00, 0x41bc7e00, 0x41c90f00,
  0x41d5a000, 0x41e23100, 0x41eec200, 0x41fb5300, 0x4203f200, 0x420a3a00, 0x42108300, 0x4216cb00,
  0x421d1400, 0x42235c00, 0x4229a500, 0x422fed00, 0x42363600, 0x423c7e00, 0x4242c700, 0x42490f00,
};
static const float invpio2 = 6.3661980629e-01f, pio2_1 = 1.5707855225e+00f, pio2_1t = 1.0804334124e-05f,
  pio2_2 = 1.0804273188e-05f, pio2_2t = 6.0770999344e-11f, pio2_3 = 6.0770943833e-11f, pio2_3t = 6.1232342629e-17f;

// só a parte até 2^7·π/2 (o resto devolve -99: o teste não compara lá)
int32_t __ieee754_rem_pio2f(float x, float *y) {
  float z, w, t, r, fn;
  int32_t i, j, n, ix, hx;
  GET_FLOAT_WORD(hx, x);
  ix = hx & 0x7fffffff;
  if (ix <= 0x3f490fd8) { y[0] = x; y[1] = 0; return 0; }
  if (ix < 0x4016cbe4) {
    if (hx > 0) {
      z = x - pio2_1;
      if ((ix & 0xfffffff0) != 0x3fc90fd0) { y[0] = z - pio2_1t; y[1] = (z - y[0]) - pio2_1t; }
      else { z -= pio2_2; y[0] = z - pio2_2t; y[1] = (z - y[0]) - pio2_2t; }
      return 1;
    } else {
      z = x + pio2_1;
      if ((ix & 0xfffffff0) != 0x3fc90fd0) { y[0] = z + pio2_1t; y[1] = (z - y[0]) + pio2_1t; }
      else { z += pio2_2; y[0] = z + pio2_2t; y[1] = (z - y[0]) + pio2_2t; }
      return -1;
    }
  }
  if (ix <= 0x43490f80) {
    t = fabsf(x);
    n = (int32_t)(t * invpio2 + khalf);
    fn = (float)n;
    r = t - fn * pio2_1;
    w = fn * pio2_1t;
    if (n < 32 && (int32_t)(ix & 0xffffff00) != npio2_hw[n - 1]) {
      y[0] = r - w;
    } else {
      uint32_t high;
      j = ix >> 23;
      y[0] = r - w;
      GET_FLOAT_WORD(high, y[0]);
      i = j - ((high >> 23) & 0xff);
      if (i > 8) {
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        GET_FLOAT_WORD(high, y[0]);
        i = j - ((high >> 23) & 0xff);
        if (i > 25) {
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
    else return n;
  }
  if (ix >= 0x7f800000) { y[0] = y[1] = x - x; return 0; }
  return -99;
}

float sinf(float x) {
  float y[2], z = 0.0f;
  int32_t n, ix;
  GET_FLOAT_WORD(ix, x);
  ix &= 0x7fffffff;
  if (ix <= 0x3f490fd8) return __kernel_sinf(x, z, 0);
  else if (ix >= 0x7f800000) return x - x;
  else {
    n = __ieee754_rem_pio2f(x, y);
    switch (n & 3) {
      case 0: return __kernel_sinf(y[0], y[1], 1);
      case 1: return __kernel_cosf(y[0], y[1]);
      case 2: return -__kernel_sinf(y[0], y[1], 1);
      default: return -__kernel_cosf(y[0], y[1]);
    }
  }
}

float cosf(float x) {
  float y[2], z = 0.0f;
  int32_t n, ix;
  GET_FLOAT_WORD(ix, x);
  ix &= 0x7fffffff;
  if (ix <= 0x3f490fd8) return __kernel_cosf(x, z);
  else if (ix >= 0x7f800000) return x - x;
  else {
    n = __ieee754_rem_pio2f(x, y);
    switch (n & 3) {
      case 0: return __kernel_cosf(y[0], y[1]);
      case 1: return -__kernel_sinf(y[0], y[1], 1);
      case 2: return -__kernel_cosf(y[0], y[1]);
      default: return __kernel_sinf(y[0], y[1], 1);
    }
  }
}
}  // namespace ref

// ---------------- conferências ----------------
static float asFloat(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t asBits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static bool same(float a, float b) { return (isnan(a) && isnan(b)) || asBits(a) == asBits(b); }

// erro em ulps do float f contra o valor exato d (double)
static double ulpErr(float f, double d) {
  double ad = fabs(d);
  int e;
  frexp(ad, &e);  // ad = m·2^e, m em [0,5, 1)
  double ulp = ad < 1.1754943508222875e-38 ? ldexp(1.0, -149) : ldexp(1.0, e - 24);
  return fabs((double)f - d) / ulp;
}

static int falhas = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("trig: FALHA: " __VA_ARGS__); printf("\n"); if (++falhas > 20) return 1; } } while (0)

struct Faixa {  // resultado de uma varredura
  std::atomic<uint64_t> n{0}, dif{0};
  std::atomic<uint32_t> primeiraDif{0xffffffff};
  double maxUlpS = 0, maxUlpC = 0;
};

// percorre os bits [b0, b1) com passo, nos dois sinais, comparando com a referência e com double
static void varre(uint32_t b0, uint32_t b1, uint32_t passo, Faixa &F, bool comRef) {
  unsigned nt = std::thread::hardware_concurrency();
  if (nt == 0) nt = 4;
  std::vector<std::thread> th;
  std::vector<double> mS(nt, 0), mC(nt, 0);
  for (unsigned k = 0; k < nt; k++)
    th.emplace_back([&, k] {
      uint64_t cont = 0, dif = 0;
      for (uint64_t b = b0 + (uint64_t)k * passo; b < b1; b += (uint64_t)nt * passo)
        for (uint32_t s = 0; s < 2; s++) {
          float x = asFloat((uint32_t)b | (s << 31));
          float ts = trigSinf(x), tc = trigCosf(x);
          cont++;
          if (comRef && (!same(ts, ref::sinf(x)) || !same(tc, ref::cosf(x)))) {
            dif++;
            uint32_t prev = F.primeiraDif.load();
            while (asBits(x) < prev && !F.primeiraDif.compare_exchange_weak(prev, asBits(x))) {}
          }
          double es = ulpErr(ts, sin((double)x)), ec = ulpErr(tc, cos((double)x));
          if (es > mS[k]) mS[k] = es;
          if (ec > mC[k]) mC[k] = ec;
        }
      F.n += cont;
      F.dif += dif;
    });
  for (auto &t : th) t.join();
  for (unsigned k = 0; k < nt; k++) {
    if (mS[k] > F.maxUlpS) F.maxUlpS = mS[k];
    if (mC[k] > F.maxUlpC) F.maxUlpC = mC[k];
  }
}

int main(int argc, char **argv) {
  bool todos = argc > 1 && !strcmp(argv[1], "todos");
  const uint32_t LIM = 0x43490f80;  // 2^7·π/2 (~201 rad)

  // 1 + 2: faixa da libm sem Payne–Hanek
  Faixa A;
  varre(0, LIM + 1, todos ? 1 : 97, A, true);
  printf("trig: ate 2^7*pi/2 rad: %llu floats%s, %llu diferentes do newlib; erro max %.3f ulp (sin), %.3f ulp (cos),"
         " o mesmo do newlib\n", (unsigned long long)A.n.load(), todos ? " (todos)" : " (1 a cada 97)",
      (unsigned long long)A.dif.load(), A.maxUlpS, A.maxUlpC);
  CHECK(A.dif == 0, "primeira diferença em x = %.9g (0x%08x)", asFloat(A.primeiraDif), A.primeiraDif.load());
  CHECK(A.maxUlpS < 3 && A.maxUlpC < 3, "erro de 3 ulp ou mais");  // o newlib chega a 2,9 ulp no cosseno

  // 2: redução em double, 2^7·π/2 < |x| < 2^19
  Faixa B;
  varre(LIM + 1, 0x49000000, todos ? 7 : 997, B, false);
  printf("trig: de 2^7*pi/2 a 2^19 rad (reducao em double): %llu floats, erro max %.3f ulp (sin), %.3f ulp (cos)\n",
      (unsigned long long)B.n.load(), B.maxUlpS, B.maxUlpC);
  CHECK(B.maxUlpS < 1.5 && B.maxUlpC < 1.5, "erro de 1,5 ulp ou mais na redução em double");

  // 3: enormes, inf, NaN, zero, subnormais
  for (uint64_t b = 0x49000000; b < 0x7f800000; b += 4099)
    for (uint32_t s = 0; s < 2; s++) {
      float x = asFloat((uint32_t)b | (s << 31)), ts = trigSinf(x), tc = trigCosf(x);
      CHECK(isfinite(ts) && isfinite(tc) && fabsf(ts) <= 1 && fabsf(tc) <= 1, "x = %g fora de [-1, 1]", x);
    }
  const float esp[] = {INFINITY, -INFINITY, NAN, -NAN};
  for (float x : esp) CHECK(isnan(trigSinf(x)) && isnan(trigCosf(x)), "%g não deu NaN", x);
  CHECK(asBits(trigSinf(0.0f)) == 0 && asBits(trigSinf(-0.0f)) == 0x80000000 && trigCosf(-0.0f) == 1, "±0");
  for (uint32_t b = 1; b < 0x00800000; b += 4093) {
    float x = asFloat(b);
    CHECK(trigSinf(x) == x && trigSinf(-x) == -x && trigCosf(x) == 1, "subnormal 0x%08x", b);
  }

  // 4: ângulos dos mostradores, com as contas das chamadas (K_PI de ui_kit.h)
  const float K_PI = 3.14159265f;
  std::vector<float> ang;
  for (int i = 0; i < 60; i++) ang.push_back(i * K_PI / 30);             // face_analogico, facepack (min/seg)
  for (int h = 0; h < 12; h++)
    for (int m = 0; m < 60; m++) ang.push_back((h + m / 60.0f) * K_PI / 6);  // facepack (hora)
  for (int i = 0; i < 8; i++) ang.push_back(i * K_PI / 4);               // ui.cpp sunShape
  for (int d = -360; d <= 720; d++) ang.push_back(d * K_PI / 180);       // graus: face_flip, ui.cpp, anel
  for (int i = 0; i <= 1000; i++) ang.push_back(K_PI * i / 1000.0f);     // face_tomate (arco), face_flip (quique)
  int difLibm = 0;
  double maxPx = 0;
  for (float a : ang) {
    float ts = trigSinf(a), tc = trigCosf(a);
    CHECK(same(ts, ref::sinf(a)) && same(tc, ref::cosf(a)), "ângulo %.9g diferente do newlib", a);
    difLibm += !same(ts, sinf(a)) + !same(tc, cosf(a));
    double px = 120 * fmax(fabs(ts - sin((double)a)), fabs(tc - cos((double)a)));
    if (px > maxPx) maxPx = px;
  }
  printf("trig: %zu angulos dos mostradores iguais ao newlib; erro max %.2g px num raio de 120; "
         "%d resultados diferem da libm deste PC (so informativo)\n", ang.size(), maxPx, difLibm);
  if (falhas) return 1;
  printf("trig: ok\n");
  return 0;
}
