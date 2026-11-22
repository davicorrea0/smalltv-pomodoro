// Testa o analisador de requisições do ESP8266WebServer (core 3.1.2) com e sem o remendo do smalltv-pomodoro/ws_parse.h.
// Usa o Parsing-impl.h REAL do core (copiado pelo sim/testes.sh) sobre um cliente falso com relógio simulado.
//   ws_parse <caso>|todos         com o remendo: todos os casos precisam passar;
//   ws_parse -DWS_UNPATCHED ...   sem o remendo (só o core): os casos c1, c2*, c3 e c4 precisam FALHAR (travar, estourar o heap
//                                 ou ler o corpo inteiro). É a prova de que o teste enxerga os defeitos da auditoria (C1–C4).
// Códigos de saída: 0 passou, 1 falhou uma conferência (ou o ASan achou erro de memória), 3 ficou preso (laço sem fim).
#include <ESP8266WebServer.h>
#ifndef WS_UNPATCHED
#include "ws_parse.h"
#else
#define WS_FORM_MAX 4096
#define WS_BODY_MAX 4096
#endif
#include <map>
#include <unistd.h>

uint32_t wsNow = 0;
uint64_t wsSteps = 0;
[[noreturn]] void wsHang(const char* onde) {
  printf("PRESO (%s): o analisador não voltou (%u ms simulados, %llu leituras)\n", onde, wsNow, (unsigned long long)wsSteps);
  fflush(stdout);
  _exit(3);
}

struct Rec : RequestHandler {  // anota o que o handler de upload do firmware receberia
  std::string eventos, dados;
  bool canHandle(HTTPMethod, const String&) override { return true; }
  bool canUpload(const String&) override { return true; }
  void upload(esp8266webserver::ESP8266WebServerTemplate<WiFiServer>&, const String&, HTTPUpload& up) override {
    static const char* nome = "SWEA";
    eventos += nome[up.status];
    if (up.status == UPLOAD_FILE_WRITE) dados.append((const char*)up.buf, up.currentSize);
  }
};
struct Srv : ESP8266WebServer {
  ClientFuture parse(WiFiClient& c) { return _parseRequest(c); }
  bool semPostArgs() const { return _postArgs == nullptr && _postArgsLen == 0; }
  int nargs() const { return _currentArgCount; }
  Srv(Rec& h) { _firstHandler = &h; }
  ~Srv() { delete[] _currentArgs; delete[] _postArgs; }
};

static int falhas = 0;
#define CONFERE(c, msg) do { if (!(c)) { printf("  FALHOU: %s (%s:%d)\n", msg, __FILE__, __LINE__); falhas++; } } while (0)

static const std::string B = "----boundaryTeste123";
static std::string pedido(const std::string& corpo, const std::string& tipo, const std::string& boundary = B, const char* metodo = "POST",
                          long clen = -2) {
  std::string t = tipo == "multipart" ? "multipart/form-data; boundary=" + boundary : tipo;
  return std::string(metodo) + " /x HTTP/1.1\r\nHost: 192.168.0.9\r\nContent-Type: " + t + "\r\nContent-Length: " +
         std::to_string(clen == -2 ? (long)corpo.size() : clen) + "\r\n\r\n" + corpo;
}
static std::string arquivo(const std::string& nome, const std::string& dados, bool ultimo, const std::string& b = B) {
  return "--" + b + "\r\nContent-Disposition: form-data; name=\"" + nome + "\"; filename=\"" + nome + ".bin\"\r\nContent-Type: application/octet-stream\r\n\r\n" +
         dados + "\r\n--" + b + (ultimo ? "--\r\n" : "\r\n");
}
static std::string campo(const std::string& nome, const std::string& valor, bool ultimo, const std::string& b = B) {
  return "--" + b + "\r\nContent-Disposition: form-data; name=\"" + nome + "\"\r\n\r\n" + valor + "\r\n--" + b + (ultimo ? "--\r\n" : "\r\n");
}
static std::string cabecalhoArquivo(const std::string& nome) {  // só a abertura do arquivo, sem dados nem fim
  return "--" + B + "\r\nContent-Disposition: form-data; name=\"" + nome + "\"; filename=\"" + nome + ".bin\"\r\nContent-Type: application/octet-stream\r\n\r\n";
}
static std::string bytes(size_t n) {  // com trechos que parecem o começo do boundary
  std::string s;
  for (size_t i = 0; i < n; i++) s += (i % 97 == 0) ? "\r\n-" : (i % 131 == 0) ? "\r\n--" + B.substr(0, 5) : std::string(1, (char)('a' + i % 26));
  return s.substr(0, n);
}
static void novo() { wsNow = 0; wsSteps = 0; }
using Fut = ESP8266WebServer::ClientFuture;
static const Fut OK = ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE, PARAR = ESP8266WebServer::CLIENT_MUST_STOP;

// ---- casos que precisam passar com e sem o remendo (comportamento legítimo preservado) ----
static void ok_arquivo_e_campo() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  std::string d = bytes(5000);  // mais de um HTTP_UPLOAD_BUFLEN
  c.segs.push_back({0, pedido(campo("k", "v", false) + arquivo("f", d, true), "multipart")});
  CONFERE(s.parse(c) == OK, "multipart legítimo é aceito");
  CONFERE(h.dados == d, "os bytes do arquivo chegam inteiros ao handler de upload");
  CONFERE(h.eventos.front() == 'S' && h.eventos.back() == 'E', "upload começa em START e termina em END");
  CONFERE(s.arg("k") == "v", "campo comum chega como argumento");
}
static void ok_dois_arquivos() {  // /api/faceinstall: face + preview
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido(arquivo("face", "{\"a\":1}", false) + arquivo("preview", "PNG", true), "multipart")});
  CONFERE(s.parse(c) == OK, "dois arquivos aceitos");
  CONFERE(h.dados == "{\"a\":1}PNG", "os dois arquivos chegam");
}
static void ok_envio_lento() {  // 60 pedaços a cada 3 s (3 min no total) e depois outra parte: o prazo é de silêncio, não de duração
  novo();
  WiFiClient c; Rec h; Srv s(h);
  std::string d = bytes(60000), corpo = cabecalhoArquivo("f");
  c.segs.push_back({0, pedido("", "multipart", B, "POST", 60000 + 400)});
  uint32_t t = 3000;
  for (size_t i = 0; i < d.size(); i += 1000, t += 3000) c.segs.push_back({t, (i == 0 ? corpo : std::string()) + d.substr(i, 1000)});
  // depois do arquivo vem outra parte: o resto da linha do boundary é uma linha vazia, e o prazo de silêncio recomeça no fim do arquivo
  c.segs.push_back({t, "\r\n--" + B + "\r\nContent-Disposition: form-data; name=\"k\"\r\n\r\nv\r\n--" + B + "--\r\n"});
  CONFERE(s.parse(c) == OK, "envio lento mas contínuo (celular com sinal fraco) é aceito");
  CONFERE(h.dados == d, "e chega inteiro");
  CONFERE(s.arg("k") == "v", "e a parte seguinte também");
  CONFERE(wsNow > 150000, "o teste de fato durou mais que o prazo de silêncio");
}
static void ok_json() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido("{\"devicetype\":\"x\"}", "application/json", B, "PUT")});
  CONFERE(s.parse(c) == OK && s.arg("plain") == "{\"devicetype\":\"x\"}", "corpo JSON do Alexa chega em plain");
}

// ---- C1: 33 campos comuns ----
static std::string campos(int n) {
  std::string b;
  for (int i = 0; i < n; i++) b += campo("k" + std::to_string(i), "v", i == n - 1);
  return b;
}
static void c1() {
  novo();
  { WiFiClient c; Rec h; Srv s(h);
    c.segs.push_back({0, pedido(campos(32), "multipart")});
    CONFERE(s.parse(c) == OK && s.nargs() == 32, "32 campos (o limite do core) continuam aceitos"); }
  { WiFiClient c; Rec h; Srv s(h);
    c.segs.push_back({0, pedido(campos(33), "multipart")});
    CONFERE(s.parse(c) == PARAR, "33 campos: a requisição é recusada (o core escrevia além do array)");
    CONFERE(s.semPostArgs(), "e os campos são liberados"); }
}

// ---- C2: multipart truncado ----
static void c2_fechado() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido("--" + B + "\r\nContent-Disposition: form-data; name=\"k\"\r\n\r\nabc\r\n", "multipart", B, "POST", 100)});
  c.closeAt = 50;
  c.timeout = 1000;  // cada espera por uma linha custa 1 s; sem olhar o socket fechado levaria o prazo inteiro (10 s)
  CONFERE(s.parse(c) == PARAR, "multipart truncado com o cliente fechando o socket é recusado");
  CONFERE(wsNow < 5000, "assim que o socket fecha, sem esperar o prazo de silêncio");
}
static void c2_fechado_cabecalho() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido("--" + B + "\r\nContent-Disposition: form-d", "multipart", B, "POST", 100)});
  c.closeAt = 50;
  CONFERE(s.parse(c) == PARAR && wsNow < 60000, "cortado no meio do cabeçalho da parte");
}
static void c2_mudo() {  // o cliente para de enviar mas não fecha
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido("--" + B + "\r\nContent-Disposition: form-data; name=\"k\"\r\n\r\nabc\r\n", "multipart", B, "POST", 100)});
  CONFERE(s.parse(c) == PARAR, "cliente mudo no meio de um campo: recusado");
  CONFERE(wsNow < 40000, "dentro do prazo de silêncio");
}
static void c2_lixo() {  // lixo sem parar depois do primeiro boundary, sem nunca fechar uma parte
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido("--" + B + "\r\n", "multipart", B, "POST", 1000000)});
  c.junk = true;
  CONFERE(s.parse(c) == PARAR, "lixo sem fim fora dos arquivos: recusado");
  CONFERE(c.consumed < 4096 + 1000, "depois de ler só o limite de texto fora dos arquivos");
}
static void c2_valor_enorme() {  // um campo comum que nunca termina
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido("--" + B + "\r\nContent-Disposition: form-data; name=\"k\"\r\n\r\n", "multipart", B, "POST", 1000000)});
  c.junk = true;
  CONFERE(s.parse(c) == PARAR && c.consumed < 4096 + 1000, "valor de campo comum sem fim: recusado no limite");
}

// ---- C4: envio de arquivo parado ----
static void c4_mudo() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido(cabecalhoArquivo("f") + bytes(100), "multipart", B, "POST", 5000)});  // o celular dorme aqui
  CONFERE(s.parse(c) == PARAR, "envio parado: recusado");
  CONFERE(h.eventos.back() == 'A', "o handler de upload recebe ABORTED (fecha o arquivo, libera o UploadGate)");
  CONFERE(wsNow < 20000, "em ~10 s");
}
static void c4_fechado() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  c.segs.push_back({0, pedido(cabecalhoArquivo("f") + bytes(100), "multipart", B, "POST", 5000)});
  c.closeAt = 50;
  CONFERE(s.parse(c) == PARAR && h.eventos.back() == 'A', "socket fechado no meio do envio: ABORTED");
}

// ---- C3: corpo grande que não é multipart ----
static void c3() {
  novo();
  { WiFiClient c; Rec h; Srv s(h);
    c.segs.push_back({0, pedido("a=1&b=2", "application/x-www-form-urlencoded")});
    CONFERE(s.parse(c) == OK && s.arg("b") == "2", "POST urlencoded pequeno continua igual"); }
  { WiFiClient c; Rec h; Srv s(h);
    c.segs.push_back({0, pedido("d=" + std::string(WS_BODY_MAX - 2, '1'), "application/x-www-form-urlencoded")});
    CONFERE(s.parse(c) == OK, "corpo no limite é aceito (backup de 371 dias tem ~1,5 KB)"); }
  { WiFiClient c; Rec h; Srv s(h);
    std::string corpo = "d=" + std::string(20000, '1');
    std::string p = pedido(corpo, "application/x-www-form-urlencoded");
    c.segs.push_back({0, p});
    CONFERE(s.parse(c) == PARAR, "corpo de 20 KB: recusado");
    CONFERE(c.consumed < p.size() - corpo.size() + 100, "sem ler o corpo para a memória"); }
  { WiFiClient c; Rec h; Srv s(h);
    c.segs.push_back({0, pedido("x", "application/x-www-form-urlencoded", B, "POST", -1)});
    CONFERE(s.parse(c) == PARAR, "Content-Length negativo (vira 4 GB sem sinal): recusado"); }
}

// ---- C5: boundary gigante (vira array na pilha) ----
static void c5() {
  novo();
  WiFiClient c; Rec h; Srv s(h);
  std::string b(5000, 'b');
  c.segs.push_back({0, pedido(arquivo("f", "x", true, b), "multipart", b)});
  CONFERE(s.parse(c) == PARAR, "boundary de 5000 bytes: recusado");
  CONFERE(h.eventos.empty(), "sem chegar a abrir o upload");
}

int main(int argc, char** argv) {
  std::map<std::string, void (*)()> casos = {{"ok_arquivo_e_campo", ok_arquivo_e_campo}, {"ok_dois_arquivos", ok_dois_arquivos}, {"ok_envio_lento", ok_envio_lento},
      {"ok_json", ok_json}, {"c1", c1}, {"c2_fechado", c2_fechado}, {"c2_fechado_cabecalho", c2_fechado_cabecalho}, {"c2_mudo", c2_mudo}, {"c2_lixo", c2_lixo},
      {"c2_valor_enorme", c2_valor_enorme}, {"c4_mudo", c4_mudo}, {"c4_fechado", c4_fechado}, {"c3", c3}, {"c5", c5}};
  std::string quer = argc > 1 ? argv[1] : "todos";
  for (auto& k : casos) {
    if (quer != "todos" && quer != k.first) continue;
    int antes = falhas;
    k.second();
    printf("%s %s\n", falhas == antes ? "ok    " : "FALHOU", k.first.c_str());
    fflush(stdout);
  }
  return falhas ? 1 : 0;
}
