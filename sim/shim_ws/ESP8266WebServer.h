// Cópia fiel do que o Parsing-impl.h do core 3.1.2 usa da classe ESP8266WebServerTemplate (campos, tipos e assinaturas
// iguais às do core; o resto da classe não existe aqui). As definições dos métodos vêm do Parsing-impl.h REAL (copiado
// pelo sim/testes.sh) e, no teste do remendo, do ws_parse.h do firmware, na mesma ordem do build de verdade.
#ifndef ESP8266WEBSERVER_H
#define ESP8266WEBSERVER_H
#include "Arduino.h"
#include "WiFiServer.h"
#define HTTP_MAX_POST_WAIT 5000
#define HTTP_UPLOAD_BUFLEN 2048
#define DBGWS(...) do { (void)0; } while (0)
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
typedef struct {
  HTTPUploadStatus status;
  String filename;
  String name;
  String type;
  size_t totalSize;
  size_t currentSize;
  size_t contentLength;
  uint8_t buf[HTTP_UPLOAD_BUFLEN];
} HTTPUpload;
namespace esp8266webserver {
template <typename ServerType> class ESP8266WebServerTemplate;
template <typename ServerType>
class RequestHandler {
public:
  virtual ~RequestHandler() {}
  virtual bool canHandle(HTTPMethod, const String&) { return false; }
  virtual bool canUpload(const String&) { return false; }
  virtual void upload(ESP8266WebServerTemplate<ServerType>&, const String&, HTTPUpload&) {}
  RequestHandler<ServerType>* next() { return _next; }
  RequestHandler<ServerType>* _next = nullptr;
};
template <typename ServerType>
class ESP8266WebServerTemplate {
public:
  using ClientType = typename ServerType::ClientType;
  using RequestHandlerType = RequestHandler<ServerType>;
  enum ClientFuture { CLIENT_REQUEST_CAN_CONTINUE, CLIENT_REQUEST_IS_HANDLED, CLIENT_MUST_STOP, CLIENT_IS_GIVEN };
  typedef String (*ContentTypeFunction)(const String&);
  using HookFunction = std::function<ClientFuture(const String&, const String&, WiFiClient*, ContentTypeFunction)>;
  static String urlDecode(const String& text);
  bool hasArg(const String& name) const {
    for (int i = 0; i < _currentArgCount + _currentArgsHavePlain; i++) if (_currentArgs[i].key == name) return true;
    return false;
  }
  const String& arg(const String& name) const {  // como o core: o corpo "plain" fica logo depois dos argumentos
    static String vazio;
    for (int i = 0; i < _currentArgCount + _currentArgsHavePlain; i++) if (_currentArgs[i].key == name) return _currentArgs[i].value;
    return vazio;
  }
protected:
  ClientFuture _parseRequest(ClientType& client);
  void _parseArguments(const String& data);
  int _parseArgumentsPrivate(const String& data, std::function<void(String&, String&, const String&, int, int, int, int)> handler);
  bool _parseForm(ClientType& client, const String& boundary, uint32_t len);
  bool _parseFormUploadAborted();
  void _uploadWriteByte(uint8_t b);
  int _uploadReadByte(ClientType& client);
  bool _collectHeader(const char* headerName, const char* headerValue);
  struct RequestArgument { String key; String value; };
  HTTPMethod _currentMethod = HTTP_ANY;
  String _currentUri;
  uint8_t _currentVersion = 0;
  RequestHandlerType* _currentHandler = nullptr;
  RequestHandlerType* _firstHandler = nullptr;
  int _currentArgCount = 0;
  RequestArgument* _currentArgs = nullptr;
  int _currentArgsHavePlain = 0;
  std::unique_ptr<HTTPUpload> _currentUpload;
  int _postArgsLen = 0;
  RequestArgument* _postArgs = nullptr;
  int _headerKeysCount = 0;
  RequestArgument* _currentHeaders = nullptr;
  String _hostHeader;
  bool _chunked = false;
  bool _keepAlive = false;
  HookFunction _hook;
};
}  // namespace esp8266webserver
// o Parsing-impl.h real: o sim/testes.sh o copia para uma pasta só dele (-I), para não puxar o ESP8266WebServer.h do core
#include <Parsing-impl.h>
using ESP8266WebServer = esp8266webserver::ESP8266WebServerTemplate<WiFiServer>;
using RequestHandler = esp8266webserver::RequestHandler<WiFiServer>;
#endif
