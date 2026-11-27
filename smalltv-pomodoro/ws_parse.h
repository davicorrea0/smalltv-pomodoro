// ws_parse.h — o analisador de requisições do ESP8266WebServer (core 3.1.2) com limites.
//
// Por quê: o `Parsing-impl.h` do core roda dentro do `handleClient`, antes de qualquer guarda (hostOk, panelOk, senha),
// e tem quatro defeitos que um vizinho na rede explora sem login:
//  C1  multipart com mais de 32 campos comuns escreve além do `_postArgs` (WEBSERVER_MAX_POST_ARGS) no heap;
//  C2  multipart cortado com o cliente fechando o socket: os `while (1)` só saem ao ver o boundary e o laço principal
//      nunca mais volta (tela, timer e Wi-Fi do portal param até o relógio ser religado);
//  C3  corpo que não é multipart é lido inteiro em `String`, sem limite de Content-Length;
//  C4  `_uploadReadByte` espera sem prazo (celular dorme no meio de um envio, ou corpo enorme sem senha).
// Também limita o tamanho do boundary (o core o copia para um array na pilha: boundary de milhares de bytes estoura a
// pilha de 4 KB do laço).
//
// Como: especializações explícitas de três métodos de ESP8266WebServerTemplate<WiFiServer>. A especialização troca a
// definição do template: a original deixa de ser compilada, então o custo é só o dos limites. Não mexe no core
// instalado nem no compilar.sh. Cada mudança em relação ao core está marcada com "PATCH". O código é cópia do core
// 3.1.2 (o sim/testes.sh confere o sha256 do Parsing-impl.h): em outro core a compilação para (#error), para alguém
// rever esta cópia, como o tcpTwPurge() em modulos/web_portal.cpp.
//
// Incluído por app.h logo depois de <ESP8266WebServer.h>: a especialização precisa vir antes do primeiro uso, em TODA
// unidade de compilação que instancie o servidor. Hoje só o main.cpp (que inclui os modulos/) inclui o app.h; outra
// unidade que o incluísse deixaria duas definições fracas (COMDAT) e o linker escolheria uma em silêncio. O sim/build.sh
// confere que só o app.h inclui ESP8266WebServer.h e que só o main.cpp inclui o app.h.
//
// C4 fechado: quando um handler recusa em UPLOAD_FILE_START (sem auth), fecha o socket (`server.client().stop()`) e
// `_parseForm` aborta antes de ler o corpo; para envios aceitos (inclusive sem senha), a taxa mínima após carência e o
// teto de duração barram ataques de gotejamento (trickle).
#pragma once
#include <ESP8266WebServer.h>

#ifndef ARDUINO_ESP8266_RELEASE_3_1_2
#error "ws_parse.h copia o Parsing-impl.h do core 3.1.2: revise a cópia contra o core novo (ou tire-a se ele já corrige C1-C5)"
#endif

// Limites (bytes ou ms). Os formulários do painel mandam arquivos em multipart (face, painel, foto, firmware) e o
// resto em urlencoded: o maior é o /api/statsimport com 371 dias (~2,3 KB no pior caso, 371 x "255%2C") e o "Ajustes"
// da restauração, com a configuração inteira e o rodízio de 300 caracteres (~1,1 KB).
#define WS_IDLE_MS 10000UL     // sem receber nenhum byte por tanto tempo: larga a requisição
#define WS_FORM_MAX 4096       // multipart: cabeçalhos das partes e campos comuns (os arquivos não contam)
#define WS_BODY_MAX 4096       // corpo que não é multipart (POST urlencoded, JSON do Alexa)
#define WS_BOUNDARY_MAX 80     // o RFC 2046 limita o boundary a 70
#define WS_LINE_MAX 768        // limite por linha (request line, header e linha de cabeçalho multipart)
#define WS_HDR_MAX 32          // máximo de cabeçalhos HTTP por requisição
#define WS_HDR_MS 10000UL      // prazo total para terminar a linha inicial e os cabeçalhos
#define WS_UPLOAD_MAX (3UL * 1024 * 1024 + 65536UL)  // teto de bytes por arquivo no multipart
#define WS_UPLOAD_GRACE_MS 10000UL                   // carência inicial antes de exigir taxa mínima no upload
#define WS_UPLOAD_MIN_BPS 128UL                      // taxa média mínima (B/s) após a carência: barra gotejamento (trickle)
#define WS_UPLOAD_MAX_MS 240000UL                    // prazo máximo total por arquivo no multipart (4 min)

namespace esp8266webserver {

template <typename ClientType>
static bool wsReadLine(ClientType& client, String& out, size_t maxLen, uint32_t idleMs, uint32_t deadline = 0, bool hasDeadline = false) {
  out.clear();
  uint32_t last = millis();
  for (;;) {
    int ch = client.read();
    if (ch >= 0) {
      last = millis();
      if (ch == '\n') {
        if (out.length() && out.charAt(out.length() - 1) == '\r') out = out.substring(0, out.length() - 1);
        return true;
      }
      if (out.length() >= maxLen) return false;
      out += (char)ch;
      continue;
    }
    if (!client.connected()) return false;
    uint32_t now = millis();
    if ((uint32_t)(now - last) >= idleMs || (hasDeadline && (int32_t)(now - deadline) >= 0)) return false;
    yield();
  }
}

template <>
inline int ESP8266WebServerTemplate<WiFiServer>::_uploadReadByte(ClientType& client) {
  int res = client.read();
  if (res == -1) {
    // PATCH C4: prazo sem nenhum byte (o core esperava enquanto o socket estivesse aberto)
    uint32_t t0 = millis();
    while (!client.available() && client.connected() && (uint32_t)(millis() - t0) < WS_IDLE_MS) yield();
    res = client.read();
  }
  return res;  // -1: o chamador aborta o envio (_parseFormUploadAborted)
}

template <>
inline bool ESP8266WebServerTemplate<WiFiServer>::_parseForm(ClientType& client, const String& boundary, uint32_t len) {
  (void)len;
  // PATCH C5: o boundary vira um array na pilha (fastBoundary) e vem do cabeçalho, sem limite
  if (boundary.length() > WS_BOUNDARY_MAX) return false;
  // PATCH C1/C2: lê uma linha; false = o cliente foi embora, parou de enviar ou mandou texto demais fora dos arquivos
  size_t used = 0;
  auto readLine = [&](String& out) -> bool {
    if (!wsReadLine(client, out, WS_LINE_MAX, WS_IDLE_MS)) return false;
    used += out.length() + 2;
    return used <= WS_FORM_MAX;
  };
  auto bail = [&]() -> bool {  // libera os campos e recusa a requisição (o servidor fecha o socket)
    if (_postArgs) delete[] _postArgs;
    _postArgs = nullptr;
    _postArgsLen = 0;
    return false;
  };
  String line;
  int retry = 0;
  do {
    if (!wsReadLine(client, line, WS_LINE_MAX, WS_IDLE_MS)) return false;
    ++retry;
  } while (line.length() == 0 && retry < 3);
  //start reading the form
  if (line == ("--"+boundary)){
    if(_postArgs) delete[] _postArgs;
    _postArgs = new RequestArgument[WEBSERVER_MAX_POST_ARGS];
    _postArgsLen = 0;
    while(1){
      String argName;
      String argValue;
      String argType;
      String argFilename;
      bool argIsFile = false;

      if (!readLine(line)) return bail();  // PATCH C2
      if (line.length() > 19 && line.substring(0, 19).equalsIgnoreCase(F("Content-Disposition"))){
        int nameStart = line.indexOf('=');
        if (nameStart != -1){
          argName = line.substring(nameStart+2);
          nameStart = argName.indexOf('=');
          if (nameStart == -1){
            argName = argName.substring(0, argName.length() - 1);
          } else {
            argFilename = argName.substring(nameStart+2, argName.length() - 1);
            argName = argName.substring(0, argName.indexOf('"'));
            argIsFile = true;
            //use GET to set the filename if uploading using blob
            if (argFilename == F("blob") && hasArg(FPSTR(filename)))
              argFilename = arg(FPSTR(filename));
          }
          using namespace mime;
          argType = FPSTR(mimeTable[txt].mimeType);
          if (!readLine(line)) return bail();  // PATCH C2
          if (line.length() > 12 && line.substring(0, 12).equalsIgnoreCase(FPSTR(Content_Type))){
            argType = line.substring(line.indexOf(':')+2);
            //skip next line
            if (!readLine(line)) return bail();  // PATCH C2
          }
          if (!argIsFile){
            while(1){
              if (!readLine(line)) return bail();  // PATCH C2: o core só saía ao ver o boundary
              if (line.startsWith("--"+boundary)) break;
              if (argValue.length() > 0) argValue += '\n';
              argValue += line;
            }

            if (_postArgsLen >= WEBSERVER_MAX_POST_ARGS) return bail();  // PATCH C1: o core escrevia além do array
            RequestArgument& arg = _postArgs[_postArgsLen++];
            arg.key = argName;
            arg.value = argValue;

            if (line == ("--"+boundary+"--")){
              break;
            }
          } else {
            _currentUpload.reset(new HTTPUpload());
            _currentUpload->status = UPLOAD_FILE_START;
            _currentUpload->name = argName;
            _currentUpload->filename = argFilename;
            _currentUpload->type = argType;
            _currentUpload->totalSize = 0;
            _currentUpload->currentSize = 0;
            _currentUpload->contentLength = len;
            if(_currentHandler && _currentHandler->canUpload(_currentUri))
              _currentHandler->upload(*this, _currentUri, *_currentUpload);
            if (!client.connected()) return _parseFormUploadAborted();  // PATCH C4: recusa no UPLOAD_FILE_START fechou o socket
            _currentUpload->status = UPLOAD_FILE_WRITE;

            int fastBoundaryLen = 4 /* \r\n-- */ + boundary.length() + 1 /* \0 */;
            char fastBoundary[ fastBoundaryLen ];
            snprintf(fastBoundary, fastBoundaryLen, "\r\n--%s", boundary.c_str());
            int boundaryPtr = 0;
            uint32_t fileBytes = 0;
            uint32_t fileStartMs = millis();
            while ( true ) {
                int ret = _uploadReadByte(client);
                if (ret < 0 || ++fileBytes > WS_UPLOAD_MAX) {
                    // Unexpected, we should have had data available per above
                    return _parseFormUploadAborted();
                }
                // PATCH C4: teto de duração e taxa mínima após carência (barra gotejamento sem travar o loop)
                uint32_t elapsed = (uint32_t)(millis() - fileStartMs);
                if (elapsed > WS_UPLOAD_MAX_MS ||
                    (elapsed >= WS_UPLOAD_GRACE_MS && fileBytes < (elapsed / 1000UL) * WS_UPLOAD_MIN_BPS)) {
                    return _parseFormUploadAborted();
                }
                char in = (char) ret;
                if (in == fastBoundary[ boundaryPtr ]) {
                    // The input matched the current expected character, advance and possibly exit this file
                    boundaryPtr++;
                    if (boundaryPtr == fastBoundaryLen - 1) {
                        // We read the whole boundary line, we're done here!
                        break;
                    }
                } else {
                    // The char doesn't match what we want, so dump whatever matches we had, the read in char, and reset ptr to start
                    for (int i = 0; i < boundaryPtr; i++) {
                        _uploadWriteByte( fastBoundary[ i ] );
                    }
                    if (in == fastBoundary[ 0 ]) {
                       // This could be the start of the real end, mark it so and don't emit/skip it
                       boundaryPtr = 1;
                    } else {
                      // Not the 1st char of our pattern, so emit and ignore
                      _uploadWriteByte( in );
                      boundaryPtr = 0;
                    }
                }
            }
            // Found the boundary string, finish processing this file upload
            if (_currentHandler && _currentHandler->canUpload(_currentUri))
                _currentHandler->upload(*this, _currentUri, *_currentUpload);
            _currentUpload->totalSize += _currentUpload->currentSize;
            _currentUpload->status = UPLOAD_FILE_END;
            if (_currentHandler && _currentHandler->canUpload(_currentUri))
                _currentHandler->upload(*this, _currentUri, *_currentUpload);
            if (!client.connected()) return _parseFormUploadAborted();
            if (!readLine(line)) return bail();  // PATCH C2: cada linha pós-arquivo usa seu próprio idle timeout (WS_IDLE_MS)
            if (line == "--") {     // extra two dashes mean we reached the end of all form fields
                break;
            }
            continue;
          }
        }
      }
    }

    int iarg;
    int totalArgs = ((WEBSERVER_MAX_POST_ARGS - _postArgsLen) < _currentArgCount)?(WEBSERVER_MAX_POST_ARGS - _postArgsLen):_currentArgCount;
    for (iarg = 0; iarg < totalArgs; iarg++){
      RequestArgument& arg = _postArgs[_postArgsLen++];
      arg.key = _currentArgs[iarg].key;
      arg.value = _currentArgs[iarg].value;
    }
    if (_currentArgs) delete[] _currentArgs;
    _currentArgs = new RequestArgument[_postArgsLen];
    for (iarg = 0; iarg < _postArgsLen; iarg++){
      RequestArgument& arg = _currentArgs[iarg];
      arg.key = _postArgs[iarg].key;
      arg.value = _postArgs[iarg].value;
    }
    _currentArgCount = iarg;
    if (_postArgs) {
      delete[] _postArgs;
      _postArgs = nullptr;
      _postArgsLen = 0;
    }
    return true;
  }
  return false;
}

template <>
inline ESP8266WebServerTemplate<WiFiServer>::ClientFuture ESP8266WebServerTemplate<WiFiServer>::_parseRequest(ClientType& client) {
  const uint32_t hdrDeadline = millis() + WS_HDR_MS;
  // Read the first line of HTTP request
  String req;
  if (!wsReadLine(client, req, WS_LINE_MAX, WS_IDLE_MS, hdrDeadline, true)) {
    return CLIENT_MUST_STOP;
  }
  //reset header value
  for (int i = 0; i < _headerKeysCount; ++i) {
    _currentHeaders[i].value.clear();
   }
  _hostHeader.clear();  // PATCH: evita herdar Host da requisição anterior quando vier sem cabeçalho Host

  // First line of HTTP request looks like "GET /path HTTP/1.1"
  // Retrieve the "/path" part by finding the spaces
  int addr_start = req.indexOf(' ');
  int addr_end = req.indexOf(' ', addr_start + 1);
  if (addr_start == -1 || addr_end == -1) {
    return CLIENT_MUST_STOP;
  }

  String methodStr = req.substring(0, addr_start);
  String url = req.substring(addr_start + 1, addr_end);
  String versionEnd = req.substring(addr_end + 8);
  _currentVersion = atoi(versionEnd.c_str());
  String searchStr;
  int hasSearch = url.indexOf('?');
  if (hasSearch != -1){
    searchStr = url.substring(hasSearch + 1);
    url = url.substring(0, hasSearch);
  }
  _currentUri = url;
  _chunked = false;

  if (_hook)
  {
    auto whatNow = _hook(methodStr, url, &client, mime::getContentType);
    if (whatNow != CLIENT_REQUEST_CAN_CONTINUE)
        return whatNow;
  }

  HTTPMethod method = HTTP_GET;
  if (methodStr == F("HEAD")) {
    method = HTTP_HEAD;
  } else if (methodStr == F("POST")) {
    method = HTTP_POST;
  } else if (methodStr == F("DELETE")) {
    method = HTTP_DELETE;
  } else if (methodStr == F("OPTIONS")) {
    method = HTTP_OPTIONS;
  } else if (methodStr == F("PUT")) {
    method = HTTP_PUT;
  } else if (methodStr == F("PATCH")) {
    method = HTTP_PATCH;
  }
  _currentMethod = method;

  _keepAlive = _currentVersion > 0; // Keep the connection alive by default
                                    // if the protocol version is greater than HTTP 1.0

  //attach handler
  RequestHandlerType* handler;
  for (handler = _firstHandler; handler; handler = handler->next()) {
    if (handler->canHandle(_currentMethod, _currentUri))
      break;
  }
  _currentHandler = handler;

  String formData;
  // below is needed only when POST type request
  if (method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH || method == HTTP_DELETE){
    String boundaryStr;
    String headerName;
    String headerValue;
    bool isForm = false;
    bool isEncoded = false;
    uint32_t contentLength = 0;
    //parse headers
    int hdrCount = 0;
    while(1){
      if (!wsReadLine(client, req, WS_LINE_MAX, WS_IDLE_MS, hdrDeadline, true)) return CLIENT_MUST_STOP;
      if (req.isEmpty()) break; //no more headers
      if (++hdrCount > WS_HDR_MAX) return CLIENT_MUST_STOP;
      int headerDiv = req.indexOf(':');
      if (headerDiv == -1){
        break;
      }
      headerName = req.substring(0, headerDiv);
      headerValue = req.substring(headerDiv + 1);
      headerValue.trim();
       _collectHeader(headerName.c_str(),headerValue.c_str());

      if (headerName.equalsIgnoreCase(FPSTR(Content_Type))){
        using namespace mime;
        if (headerValue.startsWith(FPSTR(mimeTable[txt].mimeType))){
          isForm = false;
        } else if (headerValue.startsWith(F("application/x-www-form-urlencoded"))){
          isForm = false;
          isEncoded = true;
        } else if (headerValue.startsWith(F("multipart/"))){
          boundaryStr = headerValue.substring(headerValue.indexOf('=') + 1);
          boundaryStr.replace("\"","");
          isForm = true;
        }
      } else if (headerName.equalsIgnoreCase(F("Content-Length"))){
        contentLength = headerValue.toInt();
      } else if (headerName.equalsIgnoreCase(F("Host"))){
        _hostHeader = headerValue;
      } else if (headerName.equalsIgnoreCase(F("Connection"))){
        _keepAlive = headerValue.equalsIgnoreCase(F("keep-alive"));
      }
    }

    // PATCH C3: o core lia o corpo inteiro em String antes de qualquer guarda
    if (!isForm && contentLength > WS_BODY_MAX) return CLIENT_MUST_STOP;

    String plainBuf;
    if (   !isForm
        && // read content into plainBuf
           (   !readBytesWithTimeout<WiFiServer>(client, contentLength, plainBuf, HTTP_MAX_POST_WAIT)
            || (plainBuf.length() < contentLength)
           )
       )
    {
        return CLIENT_MUST_STOP;
    }

    if (isEncoded) {
        // isEncoded => !isForm => plainBuf is not empty
        // add plainBuf in search str
        if (searchStr.length())
          searchStr += '&';
        searchStr += plainBuf;
    }

    // parse searchStr for key/value pairs
    _parseArguments(searchStr);

    if (!isForm) {
      if (contentLength) {
        // add key=value: plain={body} (post json or other data)
        RequestArgument& arg = _currentArgs[_currentArgCount];
        arg.key = F("plain");
        arg.value = plainBuf;
        _currentArgsHavePlain = 1;
      }
    } else { // isForm is true
      // here: content is not yet read (plainBuf is still empty)
      // PATCH: bloqueia multipart antes de ler qualquer byte em rotas sem handler de upload
      if (!_currentHandler || !_currentHandler->canUpload(_currentUri)) {
        return CLIENT_MUST_STOP;
      }
      if (!_parseForm(client, boundaryStr, contentLength)) {
        return CLIENT_MUST_STOP;
      }
    }
  } else {
    String headerName;
    String headerValue;
    //parse headers
    int hdrCount = 0;
    while(1){
      if (!wsReadLine(client, req, WS_LINE_MAX, WS_IDLE_MS, hdrDeadline, true)) return CLIENT_MUST_STOP;
      if (req.isEmpty()) break;//no moar headers
      if (++hdrCount > WS_HDR_MAX) return CLIENT_MUST_STOP;
      int headerDiv = req.indexOf(':');
      if (headerDiv == -1){
        break;
      }
      headerName = req.substring(0, headerDiv);
      headerValue = req.substring(headerDiv + 1);
      headerValue.trim();
      _collectHeader(headerName.c_str(),headerValue.c_str());

      if (headerName.equalsIgnoreCase(F("Host"))){
        _hostHeader = headerValue;
      } else if (headerName.equalsIgnoreCase(F("Connection"))){
        _keepAlive = headerValue.equalsIgnoreCase(F("keep-alive"));
      }
    }
    _parseArguments(searchStr);
  }
  client.flush();

  return CLIENT_REQUEST_CAN_CONTINUE;
}

}  // namespace esp8266webserver
