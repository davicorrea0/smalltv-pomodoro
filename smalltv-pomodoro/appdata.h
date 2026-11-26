// appdata.h — dados empurrados de fora (Home Assistant, scripts) para os pacotes de mostrador: POST /api/apps/data.
// Armazém estático de 18 campos, só na RAM (nada na flash, nada no backup; religar zera). Os pacotes leem com {x.campo}.
// Contrato e receita do Home Assistant: docs/HOME-ASSISTANT.md.
#pragma once
#include <stddef.h>
#include <stdint.h>

static const size_t APPDATA_BODY_MAX = 512;  // corpo do POST
static const size_t APPSHOW_BODY_MAX = 64;   // corpo do POST /api/apps/show
static const int APPDATA_FIELDS = 18;        // campos no armazém (somados todos os remetentes)
static const int APPDATA_PUT_MAX = 8;        // campos por envio
static const int APPDATA_NAME = 8;           // caracteres do nome, [a-z0-9_]
static const int APPDATA_VALUE = 15;         // bytes do valor (texto, ou inteiro guardado como texto)

// Valida o corpo inteiro e só então aplica (atômico). Os campos do envio são gravados com o ttl dele e os outros ficam
// como estão (cada campo vence no seu prazo); se não houver lugar para os campos novos, o envio é recusado sem mudar nada.
// `now` é o millis(). Devolve nullptr se aceitou, ou uma mensagem curta (PROGMEM) com o motivo da recusa.
const char *appDataPut(const char *body, size_t len, uint32_t now);

// Valida {"pkg":"id","s":30} sem ArduinoJson. O pacote tem 1..23 caracteres [a-z0-9_-] e o tempo,
// sem sinal, zeros à esquerda ou fração, vai de 5 a 60 s. Só preenche as saídas depois de validar tudo.
// Devolve nullptr se aceitou, ou uma mensagem curta (PROGMEM) com o motivo da recusa.
const char *appShowParse(const char *body, size_t len, char *pkg, size_t pkgCap, uint8_t *secs);

// Valor do campo (nome sem o "x."), ou nullptr se nunca chegou, venceu, ou o campo não foi enviado.
const char *appDataGet(const char *name, uint32_t now);

// Descarta os campos vencidos de vez (chamar a cada quadro): a comparação com sinal do millis() só enxerga 24,8 dias
// para cada lado, e sem isso um campo vencido voltaria a valer depois desse tempo.
void appDataExpire(uint32_t now);

// Para o painel e o GET: i-ésimo campo vivo (0, 1, ...), com o tempo que falta em segundos; false quando acabaram.
bool appDataAt(int i, uint32_t now, const char **name, const char **value, uint32_t *leftS);

void appDataClear();  // religar / testes
