#pragma once
// No PC não há flash separada: PROGMEM some e as leituras são acessos normais.
#include <string.h>
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define memcpy_P memcpy
#define strncpy_P strncpy
#define PSTR(s) (s)
#define FPSTR(s) (s)
typedef const char *PGM_P;
