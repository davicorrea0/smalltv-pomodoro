# Home Assistant: estado e comandos do Pomodoro

O Home Assistant pode **ler o estado** do Pomodoro (fase, tempo que falta, pomodoros do dia) e **mandar comandos**
(iniciar, pausar, pular, zerar) pelas rotas que o relógio já tem. Não precisa de broker nem de firmware novo, e não
usa a chave de escrita dos valores (veja [HOME-ASSISTANT.md](HOME-ASSISTANT.md), que cuida de mostrar valores da casa nos
mostradores).

Funciona na versão 1.26.1 e nas seguintes.

*English summary at the end.*

## O que foi testado

- **Conferido no código do firmware:** as rotas, os campos e os cabeçalhos da seção "Contrato" abaixo.
- **Conferido na documentação do Home Assistant (consulta de 22/11/2026, documentação 2026.11):** os campos do `rest`, do
  `rest_command`, do botão `template` e do gatilho `webhook`, e o efeito de `homeassistant.update_entity` num sensor `rest`.
- **Não foi rodado:** nenhuma chamada HTTP foi feita num relógio e o YAML não foi carregado num Home Assistant por
  quem escreveu esta página. O YAML usa a sintaxe da documentação 2026.11 (`action:`, `trigger:`, `triggers:`/`actions:`);
  em versões antigas, troque por `service:` e `platform:`. Confira em **Ferramentas de desenvolvedor → Estados** e **Ações**
  depois de recarregar, e avise se algo precisar de ajuste.

## Antes de começar

- Estas rotas pedem a **senha do painel** (Ajustes → Sistema, "Senha do painel e do /update"), não a chave dos valores. Quem tem a senha controla o
  relógio por inteiro, inclusive a atualização do firmware. Guarde-a só no `secrets.yaml` e saiba que ela vai em HTTP,
  sem criptografia, a cada consulta, pela rede da sua casa. Se o relógio não tem senha, apague as linhas `username`,
  `password` e `authentication`.
- Use o **IP** do relógio e reserve-o no roteador (DHCP). O nome `smalltv.local` também vale se o seu Home Assistant
  resolver nomes `.local`; o nome que o roteador dá ao relógio não vale: o relógio responde 403.
- **Consulte no máximo a cada 60 s.** O relógio atende um pedido por vez, junto com o painel e a tela, e cada leitura do
  estado percorre a memória dele. O fim de foco e de pausa chega na hora pelo aviso da seção "Atualizar na hora".

## O pacote

No `secrets.yaml` (o valor abaixo é só um marcador):

```yaml
smalltv_senha: "COLE_A_SENHA_DO_PAINEL"
```

Num arquivo `packages/smalltv.yaml` (e, no `configuration.yaml`, `homeassistant: packages: !include_dir_named packages`).
Troque `192.168.1.50` pelo IP do relógio:

```yaml
rest:
  - resource: "http://192.168.1.50/api/state"
    scan_interval: 60
    timeout: 5
    authentication: basic
    username: admin
    password: !secret smalltv_senha
    sensor:
      - name: "SmallTV fase"
        unique_id: smalltv_fase
        value_template: "{{ value_json.phase }}"      # idle, work, short ou long
        json_attributes: [running, remaining, total, today, streak]

template:
  - sensor:
      - name: "SmallTV fim da fase"
        unique_id: smalltv_fim_da_fase
        device_class: timestamp
        state: >-
          {% set ms = state_attr('sensor.smalltv_fase', 'remaining') %}
          {% if state_attr('sensor.smalltv_fase', 'running') and ms is number %}
            {{ (now() + timedelta(milliseconds=ms)).isoformat() }}
          {% else %}
            unknown
          {% endif %}
      - name: "SmallTV pomodoros hoje"
        unique_id: smalltv_hoje
        state: "{{ state_attr('sensor.smalltv_fase', 'today') }}"
        state_class: measurement
  - button:
      - name: "SmallTV iniciar ou pausar"
        unique_id: smalltv_alternar
        press:
          - action: rest_command.smalltv_comando
            data: { c: toggle }
          - action: homeassistant.update_entity
            target: { entity_id: sensor.smalltv_fase }
      - name: "SmallTV pular fase"
        unique_id: smalltv_pular
        press:
          - action: rest_command.smalltv_comando
            data: { c: skip }
          - action: homeassistant.update_entity
            target: { entity_id: sensor.smalltv_fase }
      - name: "SmallTV zerar"
        unique_id: smalltv_zerar
        press:
          - action: rest_command.smalltv_comando
            data: { c: reset }
          - action: homeassistant.update_entity
            target: { entity_id: sensor.smalltv_fase }

rest_command:
  smalltv_comando:
    url: "http://192.168.1.50/api/cmd"
    method: POST
    content_type: "application/x-www-form-urlencoded"
    payload: "c={{ c }}"
    headers: { X-SmallTV: "1" }
    username: admin
    password: !secret smalltv_senha
    timeout: 5
```

- **O corpo do comando é um formulário** (`c=toggle`), não JSON: sem o `content_type` acima o relógio não lê o comando.
- **`json_attributes` lista só o que o pacote usa**, para o Home Assistant não guardar no banco dados do relógio que não
  interessam (rede, cidade, coordenadas).
- **"Fim da fase"** é o horário previsto, calculado a cada consulta: pode variar alguns segundos de uma consulta para a
  outra, e fica `unknown` com o relógio parado. A contagem regressiva na tela do Home Assistant não precisa de consulta a cada
  segundo.
- **Cada comando acende a tela do relógio por 10 segundos.**

## Sessões Deep focus e Gentle focus

Cada sessão precisa de três pedidos, nesta ordem: aplicar os tempos, zerar e iniciar. Acrescente estes dois comandos ao
`rest_command` acima e use os scripts para manter a ordem (eles reutilizam `smalltv_comando`):

```yaml
rest_command:
  smalltv_deep_focus_ajustes:
    url: "http://192.168.1.50/api/settings"
    method: POST
    content_type: "application/x-www-form-urlencoded"
    payload: "work=50&short=10&long=25&every=3&cycles=3&auto=1"
    headers: { X-SmallTV: "1" }
    username: admin
    password: !secret smalltv_senha
  smalltv_gentle_focus_ajustes:
    url: "http://192.168.1.50/api/settings"
    method: POST
    content_type: "application/x-www-form-urlencoded"
    payload: "work=25&short=5&long=20&every=2&cycles=2&auto=1"
    headers: { X-SmallTV: "1" }
    username: admin
    password: !secret smalltv_senha

script:
  smalltv_deep_focus:
    sequence:
      - action: rest_command.smalltv_deep_focus_ajustes
      - action: rest_command.smalltv_comando
        data: { c: reset }
      - action: rest_command.smalltv_comando
        data: { c: start }
  smalltv_gentle_focus:
    sequence:
      - action: rest_command.smalltv_gentle_focus_ajustes
      - action: rest_command.smalltv_comando
        data: { c: reset }
      - action: rest_command.smalltv_comando
        data: { c: start }
```

No estado, `cfg.cycles` é o número de focos da sessão (`0` = sem fim), `sess` é quantos focos da sessão atual já
terminaram e `sdone` vale `1` quando a sessão acabou. Ao fim da sessão o relógio dispara o aviso de fim de pausa (`/hooks.txt`), como em qualquer pausa que acaba. A Alexa não chama sessões: as três lâmpadas que ela encontra continuam
fixas em Pomodoro, Pausa e Pausa longa.

## Atualizar na hora (fim de foco e de pausa)

No painel do relógio, em **Ajustes → Alexa**, cole o endereço abaixo nos dois campos (**Avisar quando o foco terminar** e
**Avisar quando a pausa terminar**) e toque em **Salvar avisos**:

```text
http://IP_DO_HOME_ASSISTANT:8123/api/webhook/COLE_UM_ID_LONGO_E_ALEATORIO
```

e crie a automação (o id tem de ser o mesmo; trate-o como uma senha local):

```yaml
automation:
  - alias: "SmallTV: a fase mudou"
    triggers:
      - trigger: webhook
        webhook_id: COLE_UM_ID_LONGO_E_ALEATORIO
        allowed_methods: [GET]
        local_only: true
    actions:
      - action: homeassistant.update_entity
        target: { entity_id: sensor.smalltv_fase }
```

O relógio chama o endereço (GET) quando a fase termina. É **melhor esforço**: se o Wi-Fi estiver fora ou o Home Assistant
não responder, o aviso se perde e o sensor só se atualiza na próxima consulta de 60 s.

## Contrato

Estes nomes passam a ser públicos: mudar um deles exige aviso nas notas da versão.

| Rota | O que o pacote usa |
|---|---|
| `GET /api/state` | `phase` (`idle`, `work`, `short` ou `long`), `running` (verdadeiro/falso), `remaining` e `total` em **milissegundos**, `today` (pomodoros de hoje), `streak` (sequência de dias), `cfg.cycles`, `sess` e `sdone` |
| `POST /api/settings` | Tempos e sessão como formulário; os templates usam `work`, `short`, `long`, `every`, `cycles` e `auto` |
| `POST /api/cmd`, campo `c` | `start`, `pause`, `toggle`, `skip`, `reset`; a resposta é o mesmo estado do `GET /api/state` |

| Item | Regra |
|---|---|
| Host | O IP do relógio ou o `.local` dele (outro nome: 403) |
| `GET /api/state` | Sem senha, qualquer pedido do Host certo; com senha, `Authorization: Basic` com `admin` e a senha do painel |
| `POST /api/cmd` | O mesmo, mais o cabeçalho `X-SmallTV: 1` e o corpo `application/x-www-form-urlencoded` |
| Erros de senha | Depois de errar, o mesmo IP espera 1, 2, 4... até 32 s (`429`) |

O brilho, o mostrador e os demais ajustes **não** fazem parte deste contrato: o relógio grava a memória de ajustes a cada
mudança, e uma automação que mude o brilho a toda hora a desgastaria.

## English summary

Home Assistant can read the Pomodoro state and send commands through routes the clock already has. No broker and no new
firmware. Works on 1.26.1 and later.

1. These routes need the **panel password** (not the write key of [HOME-ASSISTANT.md](HOME-ASSISTANT.md)). It controls the
   whole clock and travels over plain HTTP on your home network. Keep it in `secrets.yaml`.
2. Use the clock's IP address (reserve it in the router); `smalltv.local` also works if your Home Assistant resolves
   `.local` names. Poll `GET /api/state` no more than every 60 s.
3. `GET /api/state` gives `phase` (`idle`, `work`, `short`, `long`), `running`, `remaining` and `total` in milliseconds,
   `today` and `streak`. `POST /api/cmd` takes `c=start|pause|toggle|skip|reset` as a **form body**, with the header
   `X-SmallTV: 1`; each command lights the screen for 10 s.
4. For an immediate update at the end of a focus or break, put a Home Assistant webhook URL in the two "notify when
   focus/break ends" fields (Settings → Alexa). Best effort: a missed call is caught by the next poll.
5. Not run on a real clock or Home Assistant by the author. Checked against the firmware source and the Home
   Assistant documentation of 22/11/2026. Brightness and other settings are deliberately outside this contract.

### Focus plans

The YAML in “Sessões Deep focus e Gentle focus” applies each template with `POST /api/settings`, then sends `c=reset` and
`c=start`, in that order. Deep focus is 50/10/25 minutes, every 3 focuses, 3 focuses total; Gentle focus is 25/5/20,
every 2, 2 focuses total. Both send `auto=1`, use `application/x-www-form-urlencoded`, `X-SmallTV: 1`, and the same Basic
authentication as the command example. In state, `cfg.cycles` is the plan limit (`0` means unlimited), `sess` is the
number of completed focuses in the current plan, and `sdone` is `1` after the plan ends. When a plan ends, the clock fires the end-of-break hook (`/hooks.txt`), like any break that finishes. Alexa cannot start these
plans: its three exposed lights remain the fixed Pomodoro, Short break, and Long break controls.
