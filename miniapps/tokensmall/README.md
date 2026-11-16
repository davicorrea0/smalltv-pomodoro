# tokensmall: uso de IA no relógio

O SmallTV Pomodoro mostrando os limites de **5 horas e de 7 dias** do Claude e do Codex, no firmware próprio (sem
subir imagem, sem escrever na flash). Um programa pequeno no PC lê os números e empurra para o relógio; o relógio só
recebe percentuais e tempo até reiniciar.

> **Rascunho.** Depende do motor de dados empurrados (`POST /api/apps/data`, PR #104 e o PR que o substitui), que ainda
> não está em nenhuma versão publicada. Por isso o pacote fica em [`sim/apps-seeds/tokensmall.json`](../../sim/apps-seeds/tokensmall.json),
> fora de `faces/` e do catálogo da galeria, e só sobe para lá quando o motor tiver token de escrita e estiver numa versão.

## Como funciona

```
 Claude Code ── statusLine ──► tokensmall.py statusline ──► limites do Claude (arquivo local, só números)
 Codex ── arquivos de sessão ─► lidos só pelo tokensmall.py (últimas linhas "rate_limits")
                                         │
                tokensmall.py enviar ────┘   POST /api/apps/data   {"ttl":900,"values":{"c5":"42% 2h13",...}}
                                         ▼
                            relógio: pacote tokensmall.json lê {x.c5}, {x.c7}, {x.x5}, {x.x7}
```

| Campo | O que é |
|---|---|
| `c5`, `c7` | Claude, janela de 5 h e de 7 dias |
| `x5`, `x7` | Codex, janela de 5 h e de 7 dias |

Cada valor é um texto curto, "percentual tempo-até-reiniciar": `42% 2h13`, `71% 3d`, `93% 38m`. Sem leitura de uma
janela, o campo vai como `--` (nunca `0%`; o companheiro manda sempre os 4 campos, senão um valor antigo ficaria na
tela, porque cada envio renova a validade do conjunto). Se o PC parar de enviar por 15 minutos (`ttl`),
tudo volta para `--`.

## Regra da K13: credencial não sai da máquina

| O programa faz | O programa NÃO faz |
|---|---|
| Lê o JSON que o Claude Code entrega ao `statusLine` e guarda só `rate_limits` (percentual e horário) | Abrir `auth.json`, `.credentials.json`, Keychain ou cookies |
| Lê só as linhas `rate_limits` das sessões recentes do Codex em `~/.codex/sessions` | Chamar API de assinatura (nem `api.anthropic.com`, nem `chatgpt.com`) |
| Envia até 4 textos curtos a um IP da rede local | Guardar ou enviar token, caminho de projeto, nome de cliente ou texto de conversa |
| Usa a senha do painel do relógio, **se houver**, da variável `SMALLTV_SENHA` (Basic, só para o IP local) | Gravar ou imprimir essa senha; enviar a algo que não seja o relógio |

Os testes (`test_tokensmall.py`) vigiam isso: um `auth.json` falso ao lado das sessões nunca é aberto, nenhum texto
da conversa ou do `statusLine` chega ao disco ou ao relógio, e o código não contém referência a login ou API de
assinatura. O destino precisa ser IP privado ou nome `.local`, o envio **não usa proxy** (nem o do ambiente nem o do
Windows) e **não segue redirecionamento**: assim a senha do painel só chega ao relógio, e há teste com um proxy e um
redirecionamento de mentira que não podem receber nada.

## Instalar

Precisa de um firmware com `POST /api/apps/data` (o motor de dados empurrados ainda não está numa versão publicada).

1. **Pacote:** no painel, *Relógio → Mais mostradores → Instalar de um arquivo*, escolha
   [`sim/apps-seeds/tokensmall.json`](../../sim/apps-seeds/tokensmall.json).
2. **Claude Code:** peça os limites no `statusLine`. Em `~/.claude/settings.json` (você mesmo edita):
   ```json
   { "statusLine": { "type": "command", "command": "python \"C:/caminho/miniapps/tokensmall/tokensmall.py\" statusline" } }
   ```
   O comando imprime `5h 42% · 7d 71%` na barra do Claude Code. Se você já tem um `statusLine`, chame o tokensmall no
   seu script com o mesmo JSON de entrada e sem imprimir: `echo "$entrada" | python tokensmall.py statusline --silencioso`.
   O Claude Code só informa os limites a assinantes Pro/Max e só depois da primeira resposta de cada sessão.
3. **Enviar:** com o IP do relógio (o que aparece na tela dele):
   ```bash
   python miniapps/tokensmall/tokensmall.py ver                      # mostra o que seria enviado, sem rede
   python miniapps/tokensmall/tokensmall.py enviar --relogio 192.168.1.50
   ```
   Se o painel tem senha, defina `SMALLTV_SENHA` no terminal antes (não ponha em arquivo). `--uma-vez` envia uma
   vez e sai; deixe sem para manter rodando (lê a cada 60 s e reenvia a cada 5 min).

## Limites desta versão

- O contrato do motor é só texto (`{x.campo}`, até 4 campos de 15 bytes), então a tela mostra números, não barras.
  Barra ou anel guiado por dado precisa de uma extensão do motor (`value: "x.campo"` em `bar` e `arc`).
- O Claude só aparece depois de uma sessão com `statusLine` ativo; o Codex depois de uma sessão que já gravou limites.
  Os arquivos de sessão do Codex não são um formato público: conferido em 16/11/2026 neste PC, pode mudar.
- O relógio não guarda nada: religar mostra `--` até o próximo envio.
- O mostrador usa os **4 campos** que o motor guarda: não convive com outro miniapp de dados empurrados (por exemplo o
  do Home Assistant) no mesmo relógio. O motor precisaria separar os campos por app.
- Autenticação: hoje é a senha do painel (a mesma da atualização do firmware), em HTTP puro na rede local, como o
  próprio painel. Para entregar a outras pessoas o motor precisa de um token só de escrita; a troca é em
  `autenticacao()`, no `tokensmall.py`.

## Testes

```bash
python -I miniapps/tokensmall/test_tokensmall.py     # companheiro (relógio de mentira em 127.0.0.1)
bash sim/testes.sh                                    # inclui tokensmall_test: o pacote desenhado com e sem dados, incremental x cheio
```

## Crédito

A ideia de mostrar os limites de uso de IA num relógio de mesa é do [TokenTV](https://github.com/click6067-ship-it/token-tv)
(MIT, click6067-ship-it), que faz isso no firmware de fábrica enviando imagens. Este miniapp usa outro caminho (dados
no firmware próprio, sem ler login) e **nenhum código do TokenTV foi copiado**. TokenTV e este firmware não rodam no
mesmo relógio ao mesmo tempo.
