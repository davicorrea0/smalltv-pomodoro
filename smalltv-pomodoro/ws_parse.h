// ws_parse.h — o analisador de requisições do ESP8266WebServer (core 3.1.2) com limites.
//
// Por quê: o `Parsing-impl.h` do core roda dentro do `handleClient`, antes de qualquer guarda (hostOk, panelOk, senha),
// e tem seis defeitos que um vizinho na rede explora sem login:
//  C1  multipart com mais de 32 campos comuns escreve além do `_postArgs` (WEBSERVER_MAX_POST_ARGS) no heap;
//  C2  multipart cortado com o cliente fechando o socket: os `while (1)` só saem ao ver o boundary e o laço principal
//      nunca mais volta (tela, timer e Wi-Fi do portal param até o relógio ser religado);
//  C3  corpo que não é multipart é lido inteiro em `String`, sem limite de Content-Length;
//  C4  `_uploadReadByte` espera sem prazo (celular dorme no meio de um envio, ou corpo enorme sem senha);
//  C5  boundary de milhares de bytes estoura a pilha de 4 KB do laço;
//  C6  linhas sem teto esgotam o heap antes do login.
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
// Limite conhecido (C4 parcial): recusar um envio no UploadGate só para de gravar; o analisador ainda lê o arquivo até
// o fim (a foto aceita até 3 MB). Quem fecha esse buraco é o handler (fechar o socket ao recusar), não este arquivo.
#pragma once
#include <ESP8266WebServer.h>

#ifndef ARDUINO_ESP8266_RELEASE_3_1_2
#error "ws_parse.h copia o Parsing-impl.h do core 3.1.2: revise a cópia contra o core novo (ou tire-a se ele já corrige C1-C6)"
#endif

// Limites (bytes ou ms). Os formulários do painel mandam arquivos em multipart (face, painel, foto, firmware) e o
// resto em urlencoded: o maior é o /api/statsimport com 371 dias (~2,3 KB no pior caso, 371 x "255%2C") e o "Ajustes"
// da restauração, com a configuração inteira e o rodízio de 300 caracteres (~1,1 KB).
#define WS_IDLE_MS 10000UL     // sem receber nenhum byte por tanto tempo: larga a requisição
#define WS_HEADERS_MS 5000UL   // prazo total da linha inicial e dos cabeçalhos, antes do login
#define WS_LINE_MAX 1024       // maior linha aceita (inclui Content-Disposition com nome comprido)
#define WS_FORM_MAX 4096       // multipart: cabeçalhos das partes e campos comuns (os arquivos não contam)
#define WS_BODY_MAX 4096       // corpo que não é multipart (POST urlencoded, JSON do Alexa)
#define WS_BOUNDARY_MAX 80     // o RFC 2046 limita o boundary a 70

namespace esp8266webserver {

// PATCH C6: limita a linha antes de guardá-la; o prazo total é usado só nos cabeçalhos da requisição
static bool wsReadLine(WiFiClient& client, String& out, size_t max, uint32_t started = 0, uint32_t total = 0) {
  out.clear();
  out.reserve(max);
  const unsigned long oldTimeout = client.getTimeout();
  auto readByte = [&](char& ch) -> bool {
    unsigned long timeout = oldTimeout;
    if (total) {
      uint32_t elapsed = (uint32_t)(millis() - started);
      if (elapsed >= total) return false;
      uint32_t left = total - elapsed;
      if (timeout > left) timeout = left;
    }
    int ready = client.read();
    if (ready >= 0) {
      ch = (char)ready;
      return true;
    }
    if (timeout != oldTimeout) client.setTimeout(timeout);
    bool ok = client.readBytes(&ch, 1) == 1;
    if (timeout != oldTimeout) client.setTimeout(oldTimeout);
    return ok;
  };

  char ch;
  while (readByte(ch)) {
    if (ch == '\r') return readByte(ch) && ch == '\n';
    if (out.length() >= max) return false;
    out += ch;
  }
  return false;
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
  uint32_t last = millis();
  size_t used = 0;
  auto readLine = [&](String& out) -> bool {
    if (!wsReadLine(client, out, WS_LINE_MAX)) return false;
    used += out.length() + 2;
    if (out.length()) last = millis();
    return used <= WS_FORM_MAX && (out.length() || (client.connected() && (uint32_t)(millis() - last) < WS_IDLE_MS));
  };
  auto bail = [&]() -> bool {  // libera os campos e recusa a requisição (o servidor fecha o socket)
    if (_postArgs) delete[] _postArgs;
    _postArgs = nullptr;
    _postArgsLen = 0;
    return false;
  };
  String line;
  int retry = 0;
  bool lineOk;
  do {
    lineOk = wsReadLine(client, line, WS_LINE_MAX);
    ++retry;
  } while (line.length() == 0 && retry < 3);
  if (!lineOk) return false;

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
            _currentUpload->status = UPLOAD_FILE_WRITE;

            int fastBoundaryLen = 4 /* \r\n-- */ + boundary.length() + 1 /* \0 */;
            char fastBoundary[ fastBoundaryLen ];
            snprintf(fastBoundary, fastBoundaryLen, "\r\n--%s", boundary.c_str());
            int boundaryPtr = 0;
            while ( true ) {
                int ret = _uploadReadByte(client);
                if (ret < 0) {
                    // Unexpected, we should have had data available per above
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
            last = millis();  // PATCH C2: o arquivo pode ter levado muito mais que o prazo; o prazo recomeça aqui
            if (!readLine(line)) return bail();  // PATCH C2
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
  // Read the first line of HTTP request
  uint32_t headersStarted = millis();
  String req;
  if (!wsReadLine(client, req, WS_LINE_MAX, headersStarted, WS_HEADERS_MS)) return CLIENT_MUST_STOP;
  //reset header value
  for (int i = 0; i < _headerKeysCount; ++i) {
    _currentHeaders[i].value.clear();
   }

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
    while(1){
      if (!wsReadLine(client, req, WS_LINE_MAX, headersStarted, WS_HEADERS_MS)) return CLIENT_MUST_STOP;
      if (req.isEmpty()) break; //no more headers
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
      if (!_parseForm(client, boundaryStr, contentLength)) {
        return CLIENT_MUST_STOP;
      }
    }
  } else {
    String headerName;
    String headerValue;
    //parse headers
    while(1){
      if (!wsReadLine(client, req, WS_LINE_MAX, headersStarted, WS_HEADERS_MS)) return CLIENT_MUST_STOP;
      if (req.isEmpty()) break;//no moar headers
      int headerDiv = req.indexOf(':');
      if (headerDiv == -1){
        break;
      }
      headerName = req.substring(0, headerDiv);
      headerValue = req.substring(headerDiv + 2);
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
