#pragma once
#include "../Arduino.h"
namespace mime {
enum type { html, htm, css, txt, js, json, maxType };
struct Entry { const char* endsWith; const char* mimeType; };
static const Entry mimeTable[maxType] = {{".html", "text/html"}, {".htm", "text/html"}, {".css", "text/css"}, {".txt", "text/plain"}, {".js", "text/javascript"}, {".json", "application/json"}};
inline String getContentType(const String&) { return "text/plain"; }
}  // namespace mime
