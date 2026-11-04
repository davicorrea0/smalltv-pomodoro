# SmallTV Pomodoro — instruções para o Claude Code

Firmware ESP8266 + painel web para o GeekMagic SmallTV Ultra. Tudo em português (código comentado, PRs, commits).

## Antes de qualquer coisa

- Leia [docs/PRINCIPIOS.md](docs/PRINCIPIOS.md) e [docs/DECISOES.md](docs/DECISOES.md).
- **Consulte o agente `cto`** (`.claude/agents/cto.md`) antes de começar uma ideia nova, antes de abrir ou mesclar um PR
  e antes de publicar uma versão. Inclua o veredito dele na descrição do PR.
- Toda decisão nova vai para `docs/DECISOES.md` no mesmo PR.
- Um assunto por PR. Não mescle nem publique sem pedido do Davi. Sem atribuição ao Claude em commits e PRs.

## Build e testes

- `bash sim/build.sh`: gera `assets.h` e `web.h`, recusa float em printf/scanf, roda o simulador e os testes do painel
  e da ponte. Depois rode `git checkout -- faces/ docs/` (ele regrava arquivos que não são para commitar); isso desfaz edições não commitadas em `docs/`, então commite antes.
- `bash sim/testes.sh 2000` (Linux/WSL, g++-multilib): handlers, histórico, fuzzing de GIF e JPEG.
- `sh compilar.sh` (`LAYOUT=4M2M` padrão): compila em `build/` e informa o tamanho e se cabe na janela antiga
  ("janela antiga: cabe/não cabe"). Ponha esses números no PR.
- `web/` precisa de **LF** (o `.gitattributes` força `eol=lf`): CRLF muda o hash do painel e o `web.h` gerado deixa de
  bater com o commitado.
- O arduino-cli guarda o core compilado em cache; para comparar dois builds (base e branch, ou layouts diferentes) use
  `--build-path` separado para cada um, senão um reaproveita o do outro.
- Publicar: só `scripts/publicar.sh [--beta]` (ensaio) e, com pedido do Davi, `--publicar`.
