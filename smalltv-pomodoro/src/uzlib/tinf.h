/* Compatibility header for the original tinf lib/older versions of uzlib.
   Note: may be removed in the future, please migrate to uzlib.h. */
#include "uzlib.h"

/* SmallTV: constant tables live in flash (PROGMEM) on the ESP8266 and are read with pgm_read_*. */
#if defined(ARDUINO_ARCH_ESP8266)
#include <pgmspace.h>
#else
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#endif
