// Shim mínimo de Arduino para compilar ui.cpp + Adafruit_GFX no PC (simulador).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#ifndef ARDUINO
#define ARDUINO 100
#endif
typedef bool boolean;
typedef uint8_t byte;
#include "Print.h"
#ifdef _WIN32
static inline struct tm *localtime_r(const time_t *t, struct tm *r) { return localtime_s(r, t) ? nullptr : r; }
#endif
// O Adafruit_GFX só usa String/__FlashStringHelper em getTextBounds; basta o mínimo.
class __FlashStringHelper;
class String {
  const char *s;
 public:
  String(const char *x = "") : s(x) {}
  const char *c_str() const { return s; }
  unsigned length() const { return strlen(s); }
};
#define radians(d) ((d) * 0.017453292519943295)
