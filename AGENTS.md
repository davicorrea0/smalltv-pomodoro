# SmallTV Pomodoro — instruções para o Codex

Firmware ESP8266 + painel web para o GeekMagic SmallTV Ultra. Tudo em português (PRs, commits, notas).

## Antes de qualquer coisa

- Leia [docs/PRINCIPIOS.md](docs/PRINCIPIOS.md) e [docs/DECISOES.md](docs/DECISOES.md).
- Antes de começar uma ideia nova, abrir um PR ou preparar uma versão, siga o **checklist do CTO** abaixo e ponha as
  respostas na descrição do PR. (No Claude Code o mesmo papel é o agente `.claude/agents/cto.md`.)
- Toda decisão nova vai para `docs/DECISOES.md` no mesmo PR.
- Um assunto por PR. Não mescle nem publique sem pedido do Davi. Sem atribuição a IA em commits e PRs.

## Checklist do CTO

Responda com: **veredito** (seguir / seguir com mudanças / não seguir), no máximo 3 pontos críticos, riscos, a
alternativa mais simples e o que registrar em DECISOES. Avalie:

1. Valor para quem usa (não técnico). Resolve problema real?
2. Custo: tamanho do `.bin`/`.gz` e DATA/RODATA/BSS antes e depois; cabe na janela de 1 MB (ponte)?
3. Risco de travar ou bricar: toca em OTA, eboot, RTC (blocos 0–31), EEPROM, `/stats.bin`, assinatura, LittleFS,
   recuperação? Então beta e teste em aparelho.
4. Consistência com PRINCIPIOS e DECISOES (design, processo, release).
5. Escopo: dá para fazer menor? Existe alternativa sem código?
6. Segurança: guardas HTTP, limite de upload, parser testado com entrada ruim, nenhum segredo.
7. Evidência: o que foi realmente executado (simulador, testes, compilação, aparelho)?

## Build e testes

- `bash sim/build.sh`: gera `assets.h` e `web.h`, recusa float em printf/scanf, roda simulador e testes. Depois (commite antes o que mudou em `docs/`, senão se perde):
  `git checkout -- faces/ docs/`.
- `bash sim/testes.sh 2000` (Linux/WSL, g++-multilib): handlers, histórico, fuzzing de GIF e JPEG.
- `sh compilar.sh`: compila e informa tamanho e "janela antiga: cabe/não cabe".
- `web/` precisa de **LF**: CRLF muda o hash do painel e o `web.h` deixa de bater.
- arduino-cli guarda o core compilado em cache: para comparar builds use `--build-path` separado para cada um.
- Publicar: só `scripts/publicar.sh [--beta]` (ensaio); `--publicar` só com pedido do Davi.
