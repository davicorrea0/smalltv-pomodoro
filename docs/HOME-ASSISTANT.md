# Home Assistant: valores da casa nos mostradores

O Home Assistant (ou qualquer script na sua rede) pode mandar valores ao relógio, como a temperatura da sala, a umidade
ou o estado da lavadora, e um mostrador instalado os desenha com `{x.campo}`. Não há nuvem nem servidor nosso no meio:
o pedido vai direto do Home Assistant ao relógio, pela rede da casa.

Funciona a partir da versão que trouxer esta página nas notas (beta 1.27.0).

*English summary at the end.*

## 1. Crie a chave no relógio

No painel do relógio, abra **Ajustes → Home Assistant** e toque em **Criar chave**. Copie a chave (32 letras e
números).

- Ela **só envia dados**: não abre o painel, não muda ajustes e não atualiza o firmware.
- Sem chave, o relógio recusa os dados. **Desligar** apaga a chave e os valores recebidos.
- Ela não vai no backup do painel. Ao restaurar o backup em outro relógio, crie outra.
- O pedido vai sem criptografia pela rede da casa (HTTP), como o resto do painel. Se a chave vazar, toque em
  **Trocar chave** e cole a nova no Home Assistant. Não exponha o relógio fora da sua rede.

## 2. Configure o Home Assistant

No `secrets.yaml` do Home Assistant, guarde o cabeçalho completo, com a palavra `Bearer` e um espaço antes da chave:

```yaml
smalltv_auth: "Bearer COLE_A_CHAVE_AQUI"
```

No `configuration.yaml`, o endereço é `smalltv.local`, o nome que o relógio anuncia na rede (com dois relógios, o
segundo vira `smalltv-2.local`). Se o seu Home Assistant não resolver nomes `.local`, use o IP que aparece em Ajustes →
Sistema. O nome que o roteador dá ao relógio não serve: o relógio responde 403.

```yaml
rest_command:
  smalltv_dados:
    url: "http://smalltv.local/api/apps/data"   # ou o IP do relógio
    method: POST
    content_type: "application/json"
    headers:
      X-SmallTV: "1"
      Authorization: !secret smalltv_auth
    timeout: 5
    payload: "{{ payload }}"

automation:
  - alias: "SmallTV: dados da casa"
    mode: queued
    max: 2
    trigger:
      - platform: state
        entity_id: [sensor.sala_temperatura, sensor.sala_umidade, binary_sensor.porta_entrada]
      - platform: time_pattern
        minutes: "/5"
    condition:
      - "{{ has_value('sensor.sala_temperatura') and has_value('sensor.sala_umidade') }}"
    action:
      - service: rest_command.smalltv_dados
        data:
          payload: >-
            {"ttl": 900, "values": {
              "temp": {{ states('sensor.sala_temperatura') | float(0) | round(0) | int }},
              "hum": {{ states('sensor.sala_umidade') | float(0) | round(0) | int }},
              "porta": "{{ 'ABERTA' if is_state('binary_sensor.porta_entrada', 'on') else 'FECHADA' }}"}}
```

- **Quando enviar:** quando o sensor muda, e a cada 5 minutos. O envio a cada 5 minutos também cobre um envio perdido
  (o nome `.local` às vezes falha por um instante) antes de o valor vencer. Não mande a cada poucos segundos: o relógio atende
  um pedido por vez, junto com o painel e a tela.
- **`has_value`** evita que um sensor indisponível vire 0 na tela. Sem envio, o relógio mostra `--` quando o prazo
  vence.
- **`ttl`** é o prazo, em segundos, de cada valor enviado. Passado esse tempo sem um envio novo, o valor some e o
  mostrador mostra `--`, em vez de um número velho. Com envio a cada 5 min, 900 s (15 min) tolera duas falhas
  seguidas.
- Para conferir antes, em Ferramentas de desenvolvedor → Ações, chame `rest_command.smalltv_dados` com
  `payload: '{"ttl": 120, "values": {"temp": 22}}'`.

## 3. Instale um mostrador que use os valores

Num pacote do **formato 2** (ver [PACOTES.md](PACOTES.md)), `{x.campo}` no texto mostra o valor recebido:

```json
{ "type": "text", "x": 120, "y": 150, "font": "pixel", "size": 5, "align": "center",
  "color": "#ffffff", "bg": "#1c1c24", "text": "{x.temp}°" }
```

Instale pelo painel, em Relógio → Mais mostradores → **Instalar de um arquivo**, e escolha o mostrador como de
costume (ou ponha no rodízio).

- **Números:** use a fonte `pixel` ou mande inteiros. As fontes grandes (`outfit`, `bebas`, `righteous`) só têm
  dígitos: `21.5` vira `21 5`, e `--` e `%` somem.
- Os acentos viram a letra sem acento na fonte do relógio.

## Contrato

`POST /api/apps/data`

```json
{"ttl": 900, "values": {"temp": 22, "hum": 58, "porta": "FECHADA"}}
```

| Item | Regra |
|---|---|
| Cabeçalhos | `X-SmallTV: 1`, `Authorization: Bearer <chave>` e `Content-Type: application/json`. O Host tem de ser o IP do relógio ou o nome `.local` dele |
| Corpo | Até 512 B; só as chaves `ttl` e `values`; JSON com no máximo 2 níveis |
| `ttl` | Obrigatório: inteiro de 10 a 86.400 s. Vale para os campos deste envio |
| `values` | 1 a 8 campos por envio. Nome com 1 a 8 caracteres `a-z`, `0-9` e `_`. Valor: texto de até 15 bytes ou inteiro de 32 bits. Recusa número com vírgula, `true`/`false`, `null`, lista e objeto |
| Armazém | 16 campos no total, **somados todos os remetentes** (Home Assistant, scripts, outros mostradores). Cada envio grava só os seus campos, e os outros ficam como estão até vencerem |
| Cheio | Se não houver lugar para um campo novo, o envio inteiro é recusado e nada muda. Espere campos vencerem ou use menos nomes |
| Atômico | O envio é conferido inteiro antes de gravar: um campo ruim recusa todos |
| Memória | Os valores ficam só na RAM: nada é gravado na memória do relógio, nada vai no backup, e religar apaga tudo |
| Nomes | Use um prefixo por remetente (`casa_`, `tk_`...) para dois remetentes não escreverem no mesmo campo |

| Resposta | Quando |
|---|---|
| `200 {"ok":true}` | Gravado |
| `400` + motivo | Corpo, `ttl`, nome ou valor fora do contrato, ou armazém cheio |
| `401 chave invalida` | Chave errada ou ausente. Depois de errar, o mesmo IP espera 1, 2, 4... até 32 s (`429` com `Retry-After`) |
| `403` | Sem `X-SmallTV: 1`, Host que não é o IP do relógio, ou chave não criada no painel |
| `413` | Corpo acima de 512 B |

`GET /api/apps/data` (com a senha do painel, se houver) devolve o que está guardado agora, para conferir:
`{"on":1,"f":[["temp","22",840]]}`, em que o último número é quantos segundos o valor ainda vale.

Teste rápido no terminal:

```bash
curl -i -H "X-SmallTV: 1" -H "Authorization: Bearer SUA_CHAVE" -H "Content-Type: application/json" -d '{"ttl":120,"values":{"temp":22}}' http://smalltv.local/api/apps/data
```

## English summary

Home Assistant (or any script on your network) can push values to the clock with `POST /api/apps/data`, and an
installed format-2 face draws them with `{x.field}`.

1. In the clock panel, open **Settings → Home Assistant** and tap **Create key**. The key only writes data: it does not
   open the panel or change settings.
2. Use the `rest_command` and automation above. Put `Bearer <key>` in `secrets.yaml`, use `smalltv.local`
   (or the clock's IP address if your Home Assistant does not resolve `.local` names), and send the header
   `X-SmallTV: 1`.
3. Body: `{"ttl": 10..86400, "values": {...}}`, 1 to 8 fields per request, names `[a-z0-9_]{1,8}`, values text up to
   15 bytes or a 32-bit integer, 512 bytes max. The clock keeps 16 fields in RAM, shared by all senders. Each field
   expires after its `ttl` and then shows `--`. Use a prefix per sender.
4. Send on state change and every 5 minutes, not every few seconds.
