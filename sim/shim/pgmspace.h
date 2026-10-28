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
#define pgm_read_dword(addr) (*(const unsigned int *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strlen_P strlen
#define strcpy_P strcpy
#define strstr_P strstr
#define snprintf_P snprintf
#define sprintf_P sprintf
