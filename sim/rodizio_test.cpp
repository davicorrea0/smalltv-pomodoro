#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "../smalltv-pomodoro/rodizio.h"

static void proximo(const char *lista, const char *atual, const char *esperado) {
  char out[64];
  assert(rodProximo(lista, atual, out, sizeof out));
  assert(!strcmp(out, esperado));
}

static const char *vivos[4];
static bool tem(const char *campo) {
  for (int i = 0; vivos[i]; i++) if (!strcmp(vivos[i], campo)) return true;
  return false;
}

int main() {
  proximo("t:1,p:casa,t:3", "p:casa", "t:3");
  proximo("t:1,p:casa,t:3", "t:3", "t:1");
  proximo("t:1,p:casa", "t:9", "t:1");
  proximo("t:1,p:a,t:1,p:b", "t:1", "p:a");
  proximo("lixo,,t:2,,p:ok", "t:2", "p:ok");
  proximo(",,t:7,,", "t:7", "t:7");
  char out[16];
  assert(!rodProximo("lixo,,,p:INVALIDO", "t:0", out, sizeof out));
  assert(!rodProximo("", "t:0", out, sizeof out));

  assert(rodSegundos("p:casa=60,t:3=120", "p:casa", 300) == 60);
  assert(rodSegundos("p:casa=30,t:3=3600", "p:casa", 300) == 30);
  assert(rodSegundos("p:casa=30,t:3=3600", "t:3", 300) == 3600);
  assert(rodSegundos("p:casa=29", "p:casa", 300) == 300);
  assert(rodSegundos("p:casa=3601", "p:casa", 300) == 300);
  assert(rodSegundos("p:casa=abc", "p:casa", 300) == 300);
  assert(rodSegundos("p:casa60", "p:casa", 300) == 300);
  assert(rodSegundos("p:casa=60,p:casa=120", "p:casa", 300) == 120);

  char limpo[301];
  rodLimparTempos("p:casa=60,t:3=120,p:x=29,p:y=3601,p:z=abc,p:ruim!=60,semigual", limpo, sizeof limpo);
  assert(!strcmp(limpo, "p:casa=60,t:3=120"));
  std::string grande;
  for (int i = 0; i < 60; i++) {
    char e[24];
    snprintf(e, sizeof e, "%sp:item-%02d=3600", i ? "," : "", i);
    grande += e;
  }
  size_t n = rodLimparTempos(grande.c_str(), limpo, sizeof limpo);
  assert(n == 299 && n == strlen(limpo) && limpo[n - 1] != ',');
  assert(strstr(limpo, "p:item-19=3600") && !strstr(limpo, "p:item-20=3600"));

  vivos[0] = nullptr;
  assert(!rodPularMiniapp(nullptr, tem));
  assert(!rodPularMiniapp("", tem));
  std::string truncada(90, 'a');
  assert(!rodPularMiniapp(truncada.c_str(), tem));
  assert(!rodPularMiniapp("luz,temperatura!", tem));
  assert(rodPularMiniapp("luz,temperatura", tem));
  vivos[0] = "temperatura"; vivos[1] = nullptr;
  assert(!rodPularMiniapp("luz,temperatura", tem));

  // mais de 32 campos nunca vivos: não dá para conferir tudo, o miniapp fica
  std::string muitos;
  for (int i = 0; i < 40; i++) muitos += i ? ",x" : "x";
  assert(muitos.size() < 90 && !rodPularMiniapp(muitos.c_str(), tem));
  // item na lista (só inteiro, nunca pedaço)
  assert(rodNaLista("t:1,p:koi,t:3", "p:koi") && rodNaLista("t:1,p:koi,t:3", "t:3") && rodNaLista("t:1", "t:1"));
  assert(!rodNaLista("t:1,p:koi", "p:ko") && !rodNaLista("t:1,p:koi", "t:") && !rodNaLista("", "t:1") && !rodNaLista("t:1", "") && !rodNaLista(nullptr, "t:1"));

  uint32_t s = 0x13579bdu;
  char aleatorio[512];
  for (int caso = 0; caso < 5000; caso++) {
    s = s * 1664525u + 1013904223u;
    size_t tam = s % (sizeof aleatorio - 1);
    for (size_t i = 0; i < tam; i++) {
      s = s * 1664525u + 1013904223u;
      aleatorio[i] = (char)(1 + s % 126);
    }
    aleatorio[tam] = 0;
    rodProximo(aleatorio, "t:1", out, sizeof out);
    rodSegundos(aleatorio, "p:x", 60);
    rodLimparTempos(aleatorio, limpo, sizeof limpo);
    rodPularMiniapp(aleatorio, tem);
    rodNaLista(aleatorio, "p:x");
  }
  puts("rodizio: OK");
  return 0;
}
