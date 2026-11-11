// Atualização pela rede: conferência da assinatura, descompactação e modo de recuperação.
#include "../app.h"
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_rsa.h>
#include <StackThunk.h>
#include "../ota_key.h"
#include "../src/uzlib/uzlib.h"

static OtaCheck ota;

static void otaConsume(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n && !ota.err.length(); i++, ota.got++) {
    uint32_t o = ota.got;
    uint8_t v = p[i];
    if ((o == 0 || o == 4096) && v != 0xE9) ota.err = F("nao e um firmware do ESP8266");
    if (o >= 4112 && o < 4120) {
      ota.field[o - 4112] = v;
      v = 0;  // os campos de tamanho e CRC entram zerados na conta
      if (o == 4119) {
        memcpy(&ota.size, ota.field, 4);
        memcpy(&ota.want, ota.field + 4, 4);
        uint32_t room = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (ota.size < 4120 || (!ota.gz && ota.size > room)) ota.err = F("tamanho invalido ou sem espaco");  // .gz: conferido em otaVerifyGz
      }
    }
    if (ota.size && o >= ota.size) ota.err = F("arquivo maior que o indicado");
    ota.crc ^= (uint32_t)v << 24;
    for (int k = 0; k < 8; k++) ota.crc = (ota.crc << 1) ^ (ota.crc & 0x80000000 ? 0x04C11DB7 : 0);
  }
}

// ---------- atualização comprimida (.bin.gz) ----------
// O .gz ocupa ~74% do firmware, então cabe mais firmware na área de 1 MB: a versão nova não precisa
// mais caber inteira ao lado da atual. O .gz é gravado no FIM da área e o gerenciador de boot (eboot)
// descompacta no reinício, por cima do firmware atual, setor por setor. Um .gz corrompido deixaria o
// relógio sem firmware; por isso, antes de liberar a troca, o relógio lê o .gz gravado, descompacta
// tudo e confere: CRC do .gz, cabeçalho, tamanho e CRC da imagem (otaConsume) e que a imagem
// descompactada não alcança o .gz que o eboot ainda vai ler. Se algo falhar, a troca é cancelada.
// Janela de 8 KB (compilar.sh comprime assim): um .gz com janela maior é recusado pelo uzlib.
static uint32_t gzAddr, gzEnd;

static uint8_t *gzBuf;

static int gzRead(struct uzlib_uncomp *m) {
  if (gzAddr >= gzEnd) return -1;
  uint32_t n = gzEnd - gzAddr < 512 ? gzEnd - gzAddr : 512;
  if (!ESP.flashRead(gzAddr, gzBuf, n)) return -1;
  gzAddr += n;
  m->source = gzBuf + 1;
  m->source_limit = gzBuf + n;
  return gzBuf[0];
}

static bool otaVerifyGz() {
  eboot_command cmd;
  if (eboot_command_read(&cmd) || cmd.action != ACTION_COPY_RAW) {
    ota.err = F("troca nao agendada");
    return false;
  }
  uint32_t src = cmd.args[0];
  uint8_t *dict = (uint8_t *)malloc(8192), out[256];
  TINF_DATA *d = (TINF_DATA *)calloc(1, sizeof(TINF_DATA));  // ~1,2 KB de tabelas: fora da pilha
  gzBuf = (uint8_t *)malloc(512);
  if (dict && gzBuf && d) {
    gzAddr = src;
    gzEnd = src + cmd.args[2];
    uzlib_init();
    d->source_read_cb = gzRead;
    uzlib_uncompress_init(d, dict, 8192);
    int res = uzlib_gzip_parse_header(d);
    ota.got = ota.size = ota.want = 0;  // otaConsume confere agora a imagem descompactada
    ota.crc = 0xFFFFFFFF;
    while (res == TINF_OK && !ota.err.length()) {
      d->dest_start = d->dest = out;
      d->dest_limit = out + sizeof out;
      res = uzlib_uncompress_chksum(d);
      if (res == TINF_OK || res == TINF_DONE) otaConsume(out, d->dest - out);
      yield();
    }
    if (ota.err.length()) {}
    else if (res != TINF_DONE) ota.err = F("arquivo comprimido corrompido");
    else if (ota.got < 4120 || ota.got != ota.size) ota.err = F("imagem incompleta");
    else if (ota.crc != ota.want) ota.err = F("imagem corrompida (CRC)");
    else if (ota.size > src) ota.err = F("firmware grande demais para descompactar");
    else {
      // O eboot não lê o tamanho do gzip: usa os 4 ÚLTIMOS bytes da área gravada como o tamanho da
      // imagem. Qualquer coisa depois do gzip (como os 260 bytes de assinatura da 1.20.0) vira o
      // tamanho, e o eboot grava só o começo por cima do setor 0 e apaga o próprio gerenciador de
      // boot: o relógio não liga mais. Só libera a troca se esse número for o da imagem conferida.
      uint8_t t[4];
      if (cmd.args[2] < 4 || !ESP.flashRead(src + cmd.args[2] - 4, t, 4)) ota.err = F("fim do arquivo ilegivel");
      else if ((uint32_t)(t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24) != ota.got)
        ota.err = F("dados depois do gzip (envie o .bin.gz sem assinatura ou o .bin)");
    }
  } else ota.err = F("sem memoria para conferir");
  free(dict);
  free(gzBuf);
  free(d);
  if (ota.err.length()) eboot_command_clear();  // cancela a troca: o firmware atual continua
  return !ota.err.length();
}

// ---------- assinatura das atualizações ----------
// Pela rede, o relógio só troca de firmware se o arquivo vier com a assinatura do projeto: RSA-2048 sobre o
// SHA-256 do arquivo exatamente como enviado (.bin ou .bin.gz), em base64 no cabeçalho X-Firmware-Sig.
// A assinatura NÃO vai dentro do arquivo: na 1.20.0 ela ia no fim do .bin.gz e o eboot usou os 4 últimos
// bytes como tamanho da imagem, apagando o gerenciador de boot. Também não usa a assinatura do Updater do
// núcleo (que espera o mesmo sufixo). A conta é feita antes de Update.end(true): sem assinatura válida
// nada é agendado e o firmware atual continua. O modo de recuperação (físico: rede própria com a senha na
// tela) aceita firmware sem assinatura, para voltar ao original ou se a chave se perder.
// Só a conta RSA do BearSSL (i15, a menor), com a chave em bytes: as classes do núcleo (PublicKey,
// SigningVerifier) trazem junto o leitor de PEM e as curvas elípticas, ~30 KB a mais no firmware.
// A conta roda na pilha extra do núcleo (6,2 KB, alocada só durante a conferência).
extern "C" uint32_t thunk_br_rsa_i15_pkcs1_vrfy(const unsigned char *x, size_t xlen, const unsigned char *oid,
                                                size_t hlen, const br_rsa_public_key *pk, unsigned char *out);

make_stack_thunk(br_rsa_i15_pkcs1_vrfy);

static br_sha256_context otaHash;

// A assinatura (256 bytes) só existe durante o envio: alocada aqui e liberada no fim (ou ao abortar), em vez de
// ocupar a RAM fixa o tempo todo.
static uint8_t *otaSig = nullptr;

static bool otaSigGiven;

static void otaSigFree() {
  free(otaSig);
  otaSig = nullptr;
  otaSigGiven = false;
}

static void otaSigBegin() {
  br_sha256_init(&otaHash);
  otaSigFree();
  const String &b64 = server.header("X-Firmware-Sig");
  if (b64.length() < 340 || b64.length() > 348) return;  // 256 bytes = 344 caracteres
  char buf[264];
  base64_decodestate st;
  base64_init_decodestate(&st);
  if (base64_decode_block(b64.c_str(), b64.length(), buf, &st) != 256) return;
  otaSig = (uint8_t *)malloc(256);
  if (!otaSig) return;
  memcpy(otaSig, buf, 256);
  otaSigGiven = true;
}

static bool otaSigOk() {
  if (recoveryMode) return true;
  if (!otaSigGiven) {
    ota.err = F("firmware sem assinatura (use o arquivo do site ou o modo de recuperacao)");
    return false;
  }
  if (ESP.getMaxFreeBlockSize() < 7500) {  // a conta usa uma pilha extra de 6,2 KB
    ota.err = F("sem memoria para conferir a assinatura");
    return false;
  }
  uint8_t n[sizeof OTA_KEY_N], e[sizeof OTA_KEY_E], want[32], got[32];
  memcpy_P(n, OTA_KEY_N, sizeof n);  // o BearSSL lê a chave byte a byte: precisa estar na RAM
  memcpy_P(e, OTA_KEY_E, sizeof e);
  br_rsa_public_key key = {n, sizeof n, e, sizeof e};
  br_sha256_out(&otaHash, want);
  stack_thunk_add_ref();
  bool ok = thunk_br_rsa_i15_pkcs1_vrfy(otaSig, 256, BR_HASH_OID_SHA256, sizeof got, &key, got) &&
            !memcmp(got, want, sizeof got);
  stack_thunk_del_ref();
  if (!ok) ota.err = F("assinatura invalida");
  return ok;
}

void setupUpdate() {
  auto authOk = [] {
    if (!panelOk()) return false;
    if (recoveryMode) return true;
    return uploadAuthed();
  };
  server.on("/update", HTTP_GET, [] {
    if (!hostOk()) { deny(); return; }
    if (!recoveryMode && !authed()) return;
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>SmallTV</title>"
                 "<style>body{font:16px system-ui;background:#111;color:#eee;max-width:420px;margin:auto;padding:20px}"
                 "input,button{width:100%;padding:12px;margin:8px 0;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
                 "button{background:#ff5a47;border:0;font-weight:700}</style><h2>");
    p += tr("Atualizar firmware", "Update firmware", "Actualizar firmware");
    p += F("</h2><form method=post enctype=multipart/form-data><input type=file name=firmware accept=.bin,.gz required>");
    if (!recoveryMode) {  // fora do modo de recuperação só entra firmware assinado: o .sig vem junto no site
      p += tr("Assinatura (.sig)", "Signature (.sig)", "Firma (.sig)");
      p += F("<input type=file name=sig accept=.sig required>");
    }
    p += F("<button>");
    p += tr("Enviar", "Upload", "Enviar");
    // Só o firmware vai no corpo; a assinatura segue em base64 no cabeçalho X-Firmware-Sig.
    p += F("</button></form><small>v" FW_VERSION "</small>"
           "<script>document.forms[0].onsubmit=async e=>{e.preventDefault();const f=e.target,h={'X-SmallTV':'1'},d=new FormData();"
           "d.append('firmware',f.firmware.files[0]);"
           "if(f.sig&&f.sig.files[0])h['X-Firmware-Sig']=btoa(String.fromCharCode(...new Uint8Array(await f.sig.files[0].arrayBuffer())));"
           "document.body.innerHTML='...';"
           "fetch('/update',{method:'POST',body:d,headers:h}).then(r=>r.text()).then(t=>document.body.innerHTML=t)}</script>");
    server.send(200, "text/html", p);
  });
  server.on("/update", HTTP_POST, [authOk] {
      if (!authOk() || !ota.authOk) return askLogin();
      if (!ota.err.length() && !ota.finished) ota.err = F("envio incompleto");
      if (ota.err.length()) {
        server.send(400, "text/plain", "Update error: " + ota.err);
        return;
      }
      server.send(200, "text/html", F("<meta http-equiv=refresh content='20;URL=/'>Update Success! Rebooting..."));
      delay(200);
      safeRestart();
    }, [authOk] {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        ota = {0, 0, 0, 0xFFFFFFFF, {0}, String(), authOk(), false, false, false};
        otaSigBegin();
        if (!ota.authOk) ota.err = F("senha");
        else {
          diagBlkSample();  // minblk: o maior bloco antes de o OTA soltar o álbum e o pacote
          if (inAlbum) stopAlbum();  // libera RAM e o LittleFS durante a gravação
          facePackUnload();  // dicionário de 8 KB do .gz e RSA (7,5 KB contíguos) precisam do heap; o relógio recarrega o pacote se o OTA falhar
        }
      } else if (up.status == UPLOAD_FILE_WRITE && !ota.err.length()) {
        if (!ota.begun) {  // primeiro pedaço: .gz vai para o fim da área (tamanho do envio); .bin, para o espaço livre
          ota.begun = true;
          wakeScreen(120000);
          ota.gz = up.currentSize > 1 && up.buf[0] == 0x1F && up.buf[1] == 0x8B;
          uint32_t size = ota.gz ? server.header("Content-Length").toInt() : (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!size || !Update.begin(size, U_FLASH)) ota.err = size ? Update.getErrorString() : String(F("tamanho desconhecido"));
        }
        if (!ota.gz) otaConsume(up.buf, up.currentSize);
        br_sha256_update(&otaHash, up.buf, up.currentSize);
        if (!ota.err.length() && Update.write(up.buf, up.currentSize) != up.currentSize) ota.err = Update.getErrorString();
        if (ota.err.length()) Update.end();  // desiste: o firmware atual continua
      } else if (up.status == UPLOAD_FILE_END && !ota.err.length()) {
        if (!ota.begun) ota.err = F("arquivo vazio");
        else if (ota.gz) {
          if (!otaSigOk()) Update.end();  // assinatura antes de agendar a troca: sem ela nada muda
          else if (!Update.end(true)) ota.err = Update.getErrorString();
          else otaVerifyGz();
        } else if (ota.got < 4120 || ota.got != ota.size) ota.err = F("arquivo incompleto");
        else if (ota.crc != ota.want) ota.err = F("arquivo corrompido (CRC)");
        else otaSigOk();
        if (ota.gz) {}
        else if (ota.err.length()) Update.end();
        else if (!Update.end(true)) ota.err = Update.getErrorString();
        ota.finished = !ota.err.length();
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        ota.err = F("envio interrompido");
      }
      if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) otaSigFree();
      yield();
    });
}

// Só redesenha ao mudar de etapa: o QR fica estável para a câmera, sem animação nem piscada.
int8_t setupQrShown = -1;

void setupQrScreen(bool recovery) {
  bool connected = WiFi.softAPgetStationNum() > 0;
  if (setupQrShown == (int8_t)connected) return;
  setupQrShown = connected;
  lcdViewActive = false;  // QR exige gama neutro; não reaplica a cor quente entre as duas etapas
  // QR em preto/branco, mesmo se o tema salvo for um LCD verde ou estiver com cor quente à noite.
  // Os laços de configuração/recuperação não reaplicam o gama; o reinício retoma os ajustes salvos.
  if (tftReady && warmSent != 0) {
    warmApplyState([](uint8_t c, uint8_t *d, uint8_t n) { tft.sendCommand(c, d, n); }, 0);
    warmSent = 0;
  }
  String ip = WiFi.softAPIP().toString();
  uiWifiSetup(recovery ? RECOVERY_AP : AP_NAME, cfg.apPass, ip.c_str(), connected, recovery);
}

void startRecovery() {
  newFormToken();
  updateBrightness(true);  // a recuperação precisa aparecer mesmo com o brilho noturno em 0
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RECOVERY_AP, cfg.apPass);
  setupQrShown = -1;
  setupQrScreen(true);
  server.on("/", HTTP_GET, [] {
    PageLang lang;  // idioma do navegador enquanto os ajustes são os de fábrica (ver pageLang)
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>SmallTV</title>"
                 "<style>body{font:16px system-ui;background:#111;color:#eee;max-width:420px;margin:auto;padding:20px}"
                 "button,a{display:block;width:100%;box-sizing:border-box;text-align:center;padding:12px;margin:8px 0;border-radius:10px;"
                 "border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit;text-decoration:none}.p{background:#ff5a47;border:0;font-weight:700}</style><h2>");
    p += tr("Modo de recuperacao", "Recovery mode", "Modo de recuperacion");
    p += F("</h2><p>v" FW_VERSION " &middot; ");
    p += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(F("192.168.4.1"));
    p += F("</p><a class=p href=/update>");
    p += tr("Atualizar firmware", "Update firmware", "Actualizar firmware");
    p += F("</a><a href=/api/stats download=historico.json>");
    p += tr("Baixar historico de focos", "Download focus history", "Descargar historial");
    p += F("</a><form method=post action=/rec/factory>");
    p += formTokenField(); p += F("<button>");
    p += tr("Voltar aos ajustes de fabrica", "Reset settings to defaults", "Restablecer ajustes");
    p += F("</button></form><form method=post action=/rec/wifi>");
    p += formTokenField(); p += F("<button>");
    p += tr("Esquecer o Wi-Fi", "Forget Wi-Fi", "Olvidar el Wi-Fi");
    p += F("</button></form><form method=post action=/rec/restart>");
    p += formTokenField(); p += F("<button class=p>");
    p += tr("Reiniciar normalmente", "Restart normally", "Reiniciar normalmente");
    p += F("</button></form><form method=post action=/rec/wipe onsubmit=\"return confirm(this.dataset.q)\" data-q=\"");
    p += tr("Apagar fotos, historico, ajustes e Wi-Fi?", "Erase photos, history, settings and Wi-Fi?", "Borrar fotos, historial, ajustes y Wi-Fi?");
    p += F("\">");
    p += formTokenField(); p += F("<button>");
    p += tr("Apagar todos os dados pessoais", "Erase all personal data", "Borrar todos los datos personales");
    p += F("</button></form>");
    server.send(200, "text/html", p);
  });
  auto done = [](const __FlashStringHelper *msg) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", msg);
  };
  server.on("/rec/factory", HTTP_POST, [done] {
    if (!formOk()) { deny(); return; }
    uint8_t lang = cfg.lang;
    defaultSettings();
    cfg.lang = lang;
    saveSettings();
    done(F("ok"));
  });
  server.on("/rec/wifi", HTTP_POST, [done] {
    if (!formOk()) { deny(); return; }
    WiFi.persistent(true);
    WiFi.disconnect(true);
    done(F("ok"));
  });
  server.on("/rec/restart", HTTP_POST, [] {
    if (!formOk()) { deny(); return; }
    server.send(200, "text/plain", "ok");
    delay(300);
    safeRestart();
  });
  // antes de doar ou vender o relógio: a flash não é criptografada, então apaga de fato
  // (arquivos, ajustes com senhas, rede salva) e volta como saído da caixa
  server.on("/rec/wipe", HTTP_POST, [] {
    if (!formOk()) { deny(); return; }
    server.send(200, "text/plain", "ok");
    delay(300);
    if (fsOk) LittleFS.format();
    for (int i = 0; i < EE_SIZE; i++) EEPROM.write(i, 0);
    if (fsLayout2()) {  // memória já formatada: sem a marca o próximo início formataria de novo e ofereceria um backup que não existe
      fsMark = {FS_MAGIC, 0};
      EEPROM.put(FS_OFF, fsMark);
    }
    EEPROM.commit();
    WiFi.persistent(true);
    WiFi.disconnect(true);
    ESP.eraseConfig();
    delay(300);
    ESP.restart();
  });
  server.on("/api/stats", HTTP_GET, sendStats);
  server.collectHeaders("X-SmallTV", "Authorization", "Content-Length", "X-Firmware-Sig", "Accept-Language");
  setupUpdate();
  server.begin();
}
