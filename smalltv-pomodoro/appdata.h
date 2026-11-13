// appdata.h — dados empurrados de fora (Home Assistant) para os pacotes de mostrador: POST /api/apps/data.
// EXPERIMENTO (branch exp/dados-ha, nunca mesclado): contrato em pesquisas/miniapp-primeiro-teste.md.
// Armazém estático de 4 campos, só na RAM (nada na flash, nada no backup; religar zera). Os pacotes leem com {x.campo}.
#pragma once
#include <stddef.h>
#include <stdint.h>

static const size_t APPDATA_BODY_MAX = 512;  // corpo do POST
static const int APPDATA_FIELDS = 4;         // campos por conjunto
static const int APPDATA_NAME = 8;           // caracteres do nome, [a-z0-9_]
static const int APPDATA_VALUE = 15;         // bytes do valor (texto, ou inteiro guardado como texto)

// Valida o corpo inteiro e só então aplica (atômico). Atualização parcial conserva os outros campos; todo envio aceito
// renova o ttl do conjunto. Conjunto já vencido é esvaziado antes de aplicar (dado velho não ressuscita).
// `now` é o millis(). Devolve nullptr se aceitou, ou uma mensagem curta (PROGMEM) com o motivo da recusa.
const char *appDataPut(const char *body, size_t len, uint32_t now);

// Valor do campo (nome sem o "x."), ou nullptr se nunca chegou, venceu, ou o campo não foi enviado.
const char *appDataGet(const char *name, uint32_t now);

// Descarta o conjunto vencido de vez (chamar a cada quadro): a comparação com sinal do millis() só enxerga 24,8 dias
// para cada lado, e sem isso um conjunto vencido voltaria a valer depois desse tempo.
void appDataExpire(uint32_t now);

void appDataClear();  // religar / testes
