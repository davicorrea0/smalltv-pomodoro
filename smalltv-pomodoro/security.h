#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
// Allocation-free checks shared with the regression tests.
struct UploadGate {
  size_t bytes = 0, limit = 0;
  bool active = false;
  void begin(bool authorized, size_t maxBytes) { bytes = 0; limit = maxBytes; active = authorized; }
  bool take(size_t n) {
    if (!active || n > limit - bytes) { active = false; return false; }
    bytes += n; return true;
  }
  void stop() { active = false; }
};
static bool safeHttpUrl(const char *s, size_t n) {
  if (n < 8 || n > 400 || memcmp(s, "http://", 7)) return false;
  for (size_t i=0; i<n; i++) {
    unsigned char c = s[i];
    if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '#') return false;
  }
  size_t end=7;
  while (end<n && s[end]!='/' && s[end]!='?') end++;
  size_t hostEnd=7;
  while(hostEnd<end && s[hostEnd]!=':') {
    char c=s[hostEnd++];
    if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return false;
  }
  if (hostEnd==7) return false;
  if (hostEnd<end) {
    uint32_t port=0;
    if (++hostEnd==end) return false;
    while(hostEnd<end) {
      char c=s[hostEnd++]; if(c<'0'||c>'9')return false;
      port=port*10+(c-'0'); if(port>65535)return false;
    }
    if (!port) return false;
  }
  return true;
}
static bool safeIpv4Host(const char *s, size_t n) {
  if (!n || n > 21) return false;
  size_t i = 0;
  for (int part = 0; part < 4; part++) {
    if (i >= n || s[i] < '0' || s[i] > '9') return false;
    unsigned value = 0, digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      value = value * 10 + (unsigned)(s[i++] - '0');
      if (++digits > 3 || value > 255) return false;
    }
    if (part < 3) {
      if (i >= n || s[i++] != '.') return false;
    }
  }
  if (i == n) return true;
  if (s[i++] != ':' || i == n) return false;
  unsigned port = 0;
  while (i < n) {
    if (s[i] < '0' || s[i] > '9') return false;
    port = port * 10 + (unsigned)(s[i++] - '0');
    if (port > 65535) return false;
  }
  return port > 0;
}
