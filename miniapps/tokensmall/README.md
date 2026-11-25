# tokensmall: uso de IA no relógio

O SmallTV Pomodoro mostrando os limites de **5 horas e de 7 dias** do Claude e do Codex, no firmware próprio (sem
subir imagem, sem escrever na flash). Um programa pequeno no PC lê os números e empurra para o relógio; o relógio só
recebe percentuais e tempo até reiniciar.

> **Rascunho.** Depende da rota de dados de fora (`POST /api/apps/data`, PRs #110 e #111), que ainda não está numa
> versão publicada. Por isso o pacote fica em [`sim/apps-seeds/tokensmall.json`](../../sim/apps-seeds/tokensmall.json),
> fora de `faces/` e do catálogo da galeria, e só sobe para lá quando a rota estiver numa versão.

## Como funciona

```
 Claude Code ── statusLine ──► tokensmall.py statusline ──► limites do Claude (arquivo local, só números)
 Codex ── arquivos de sessão ─► lidos só pelo tokensmall.py (últimas linhas "rate_limits")
                                         │
                tokensmall.py enviar ────┘   POST /api/apps/data   {"ttl":900,"values":{"tk_c5":"42","tk_c5r":"2h13",...}}
                                         ▼
                      relógio: a tela lê {x.tk_c5}, {x.tk_c5r}, {x.tk_c7}, {x.tk_x5}, {x.tk_x5r}, {x.tk_x7}
```

| Campo | O que é | Exemplo |
|---|---|---|
| `tk_c5` | Claude, 5 h: só o percentual usado (inteiro de 0 a 100, até 3 caracteres) | `42` |
| `tk_c5r` | Claude, 5 h: tempo até reiniciar (até 5 caracteres) | `2h13`, `38m` |
| `tk_c7` | Claude, 7 dias: percentual e tempo até reiniciar | `71% 3d` |
| `tk_x5`, `tk_x5r` | Codex, 5 h: percentual e tempo até reiniciar | `93`, `38m` |
| `tk_x7` | Codex, 7 dias: percentual e tempo até reiniciar | `71% 3d` |

O percentual da janela de 5 h vai **separado** do tempo porque a fonte grande da tela só desenha dígitos. A janela de
7 dias continua num texto só (`71% 3d`, até 9 caracteres). Cada valor tem no máximo 15 bytes e o envio leva até 8 campos;
6 cabem. Sem leitura de uma janela (sem dado, velha demais ou reinício que já passou), nunca vai `0`: a de **5 h** vai
como texto vazio (`""`) nos dois campos, `tk_?5` e `tk_?5r`, e a tela mostra "SEM DADOS" nessa linha; a de **7 dias**
vai como `--`. O companheiro manda sempre os 6 campos: o relógio guarda cada campo com a validade do envio que o trouxe,
então um campo que simplesmente deixasse de ser enviado continuaria na tela até vencer. Sem leitura em nenhuma janela,
nada é enviado. Se o PC parar de enviar por 15 minutos (`ttl`), a tela esvazia.

## Regra da K13: credencial não sai da máquina

| O programa faz | O programa NÃO faz |
|---|---|
| Lê o JSON que o Claude Code entrega ao `statusLine` e guarda só `rate_limits` (percentual e horário) | Abrir `auth.json`, `.credentials.json`, Keychain ou cookies |
| Lê só as linhas `rate_limits` das sessões recentes do Codex em `~/.codex/sessions` | Chamar API de assinatura (nem `api.anthropic.com`, nem `chatgpt.com`) |
| Envia 6 textos curtos a um IP da rede local | Guardar ou enviar token de login, caminho de projeto, nome de cliente ou texto de conversa |
| Usa a **chave de escrita do relógio** (32 hex, criada por você no painel) da variável `SMALLTV_CHAVE`, em `Authorization: Bearer`, só para o IP local | Gravar ou imprimir essa chave; usar a senha do painel; mandar a chave a algo que não seja o relógio |

A chave de escrita só deixa gravar dados nos campos: não abre o painel nem a atualização do firmware. Ela vive em dois
lugares, no `/apps.key` do relógio e na variável de ambiente do seu PC, e é você quem a coloca lá. Texto que não tem o
formato de uma chave (por exemplo a senha do painel) é recusado antes de abrir a conexão.

Os testes (`test_tokensmall.py`) vigiam isso: um `auth.json` falso ao lado das sessões nunca é aberto, nenhum texto
da conversa ou do `statusLine` chega ao disco ou ao relógio, e o código não contém referência a login ou API de
assinatura. O destino precisa ser IP privado ou nome `.local`, o envio **não usa proxy** (nem o do ambiente nem o do
Windows) e **não segue redirecionamento**: assim a chave só chega ao relógio, e há teste com um proxy e um
redirecionamento de mentira que não podem receber nada.

## Instalar

Precisa de um firmware com `POST /api/apps/data` e da chave de escrita criada.

1. **Chave:** no painel do relógio, *Ajustes → Home Assistant*, crie a chave de escrita. No terminal onde vai rodar o
   companheiro, defina `SMALLTV_CHAVE` com ela (não ponha em arquivo, commit nem script compartilhado).
2. **Pacote:** no painel, *Relógio → Mais mostradores → Instalar de um arquivo*, escolha
   [`sim/apps-seeds/tokensmall.json`](../../sim/apps-seeds/tokensmall.json).
3. **Claude Code:** peça os limites no `statusLine`. Em `~/.claude/settings.json` (você mesmo edita):
   ```json
   { "statusLine": { "type": "command", "command": "python \"C:/caminho/miniapps/tokensmall/tokensmall.py\" statusline" } }
   ```
   O comando imprime `5h 42% · 7d 71%` na barra do Claude Code. Se você já tem um `statusLine`, chame o tokensmall no
   seu script com o mesmo JSON de entrada e sem imprimir: `echo "$entrada" | python tokensmall.py statusline --silencioso`.
   O Claude Code só informa os limites a assinantes Pro/Max e só depois da primeira resposta de cada sessão.
4. **Enviar:** com o IP do relógio (o que aparece na tela dele):
   ```bash
   python miniapps/tokensmall/tokensmall.py ver                      # mostra o que seria enviado, sem rede
   python miniapps/tokensmall/tokensmall.py enviar --relogio 192.168.1.50
   ```
   `--uma-vez` envia uma vez e sai; sem ele o programa fica rodando (lê a cada 60 s e reenvia quando algo muda, em geral a cada minuto, porque o tempo até reiniciar anda).

## O que a tela mostra

Um cartão por assistente com duas linhas (5 horas e 7 dias). A linha de 5 horas destaca o percentual **usado** em número grande (`42`, de `tk_?5`) e, ao lado, o tempo até reiniciar (`2h13`, de `tk_?5r`); sem leitura de 5 horas (`tk_?5r` vazio ou ausente), a linha mostra "SEM DADOS". A linha de 7 dias mostra o percentual e o tempo no mesmo texto (`71% 3d`, ou `--` sem leitura). Uma barra enche com o uso e muda de cor pela faixa (clara até 70 %, amarela de 70 a 90 %, vermelha de 90 a 100 %). A linguagem visual é inspirada no [TokenTV](https://github.com/click6067-ship-it/token-tv) (MIT; nenhum código, fonte ou imagem foi copiado). Quando nada chega há 15 minutos, as duas linhas voltam a "SEM DADOS" (isso inclui o PC ligado e sem uso do Claude e do Codex há mais de 1 ou 12 horas: sem leitura recente, o companheiro não envia nada).

- **Leitura velha vira "sem leitura".** O companheiro só reenvia uma leitura se ela for recente: até 1 hora para a janela de 5 h e até 12 horas para a de 7 dias (o Claude pela hora em que o `statusLine` gravou; o Codex pela hora do arquivo de sessão). Sem isso, um número de horas atrás continuaria na tela com cara de atual (por exemplo se você gastou o limite no claude.ai ou em outro PC).
- **Chave recusada não trava o painel.** Se o relógio responde 401, 403, 404, 429 ou redirecionamento, o companheiro espera 10 minutos antes de tentar de novo (o relógio usa o mesmo limitador de senha errada para a rota de dados e para o painel, então insistir a cada minuto bloquearia o painel aberto neste PC). Erro de rede, como o Wi-Fi ainda subindo, tenta a cada volta.
- **Dá para saber o que aconteceu sem terminal.** `enviar` grava uma linha em `ultimo.txt` na pasta de estado (`%LOCALAPPDATA%\tokensmall` no Windows): horário e `enviado tk_c5,...`, `erro: ...` ou `sem leitura`. Nunca a chave nem os números.

## Limites desta versão

- Só o percentual **usado** (o que o Claude Code e o TokenTV também mostram). A barra não muda de cor perto do limite: o pacote só compara valores por igualdade exata.
- Dois assistentes (Claude e Codex): o relógio guarda 18 campos somados entre todos os remetentes, o tokensmall usa 6 (`tk_`) e, com os três exemplos da casa (12), fecha 18 de 18. Mais provedores pediriam mudar a rota de dados.
- O Claude só aparece depois de uma sessão com `statusLine` ativo; o Codex depois de uma sessão que já gravou limites.
  Os arquivos de sessão do Codex não são um formato público: conferido em 16/11/2026 neste PC, pode mudar.
- O relógio não guarda nada: religar mostra `--` até o próximo envio.

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
