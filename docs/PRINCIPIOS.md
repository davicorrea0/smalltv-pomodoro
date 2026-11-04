# Princípios do SmallTV Pomodoro

O que vale para qualquer mudança, feita por qualquer pessoa ou sessão de IA. Uma mudança que contrarie um princípio
precisa de uma decisão registrada em [DECISOES.md](DECISOES.md) antes de entrar. O agente `cto`
(`.claude/agents/cto.md`) avalia ideias, PRs e versões contra este arquivo.

Fontes: código (`smalltv-pomodoro/`), `README.md`, `docs/`, `compilar.sh`, `sim/build.sh`, `scripts/publicar.sh`,
`notas/historico.json` e o histórico de PRs.

## 1. Produto

- **O relógio nunca pode ficar inutilizável.** Travar, perder a hora ou o histórico é ruim; bricar (precisar de cabo
  USB-serial) é inaceitável. Na dúvida entre um recurso e a robustez, ganha a robustez.
- **Quem usa não é técnico.** Recurso novo precisa funcionar pelo painel, em pt/en/es, sem passo manual.
- **Menos é mais.** O aparelho tem ~80 KB de RAM e 1 MB de programa. Cada recurso disputa espaço com todos os outros;
  "seria legal" não basta, precisa de valor claro para quem usa.
- **Prioridade por demanda real.** Linhas novas de produto (ex.: miniapps) só entram quando houver sinal de público
  (ver DECISOES).

## 2. Hardware

- **ESP8266 (GeekMagic SmallTV Ultra), Arduino core 3.1.2**, cristal a 160 MHz, lwIP leve (`ip=lm2n`). Bibliotecas
  fixadas: Adafruit GFX 1.13.0, ST7735/ST7789 1.11.0, ArduinoJson 7.4.3 (`README.md`, "Building").
- **SPI do ST7789 a 40 MHz.** 80 MHz passa do limite do painel e a imagem vira lixo (PR #13, retirado no #16). O campo
  `spi80` da `Settings` ficou reservado, sempre 0.
- **Sem float em printf/scanf.** O `compilar.sh` tira o printf/scanf de decimais da newlib (`build.float=`, ~7 KB) e o
  `sim/build.sh` recusa `%f`, `toFloat`, `atof`, `strtod` e `scanf` no código. Use inteiros ou `String(x, casas)`.
- **RAM é o recurso mais escasso.** RAM estática perto de 38–39 KB; o heap livre depende disso. O decodificador de GIF
  precisa de ~24 KB contíguos e o de JPEG ~18 KB. Regras:
  - constantes e tabelas em `PROGMEM`/`PSTR` (sprites, QR, GIF, JPEG, textos via `tr()`);
  - respostas grandes em partes (chunked), sem montar `String` gigante;
  - falta de memória vira aviso na tela, nunca travamento;
  - todo PR que mexe em firmware informa DATA/RODATA/BSS antes e depois.
- **Flash de programa: teto ~1019 KB** (1.044.464 bytes, limite do linker). Todo PR de firmware informa o tamanho do
  `.bin` e do `.bin.gz` (saída do `compilar.sh`).

## 3. Flash e atualização

- **Layout 4M2M desde a 1.24.0** (PR #20): programa até ~1019 KB, LittleFS de 2 MB em `0x200000`. EEPROM, RF cal e SDK
  nos mesmos endereços do 4M3M. Detalhes: [MIGRACAO-4M2M.md](MIGRACAO-4M2M.md).
- **Regra da ponte** (PR #54): relógios ainda no 4M3M só enxergam 1 MB para a atualização
  (`firmware arredondado + .gz arredondado + 512 ≤ 1 MB`). O topo do `latest.json`/`beta.json` é sempre uma versão que
  cabe nessa janela e cujo painel conhece a ponte; o que não cabe vai no campo `l2`. Consequências:
  - a estável que vira ponte **precisa caber em 1 MB**;
  - o `publicar.sh` recusa publicar uma versão grande sem uma ponte no ar;
  - todo PR que aumenta o firmware diz se ainda cabe na janela antiga ("janela antiga: cabe/não cabe").
- **`.bin.gz` seguro para o eboot:** os 4 últimos bytes são o tamanho da imagem, nada depois do gzip
  (`scripts/conferir-gz.cjs`). Um `.gz` com assinatura colada no fim apagou o gerenciador de boot na 1.20.0.
- **O relógio confere antes de trocar:** CRC e tamanho da imagem, descompressão completa do `.gz` (`otaVerifyGz`) e
  assinatura. Qualquer falha cancela a troca e o firmware atual continua.
- **LittleFS montado sem formatar.** A única formatação é a migração única para 4M2M (marca `FS2A` na EEPROM).
- **Modo de recuperação** (ligar e desligar 3 vezes) é a saída física e precisa continuar funcionando em toda versão.

## 4. Invariantes (nunca mudam sem migração e decisão registrada)

- **Memória RTC:** blocos 0–31 (128 bytes) são do comando do eboot. Escrever ali entre o fim da atualização e o
  reinício corrompe o comando e o relógio volta para a versão antiga (bug da 1.23.2/1.23.3, que ficaram sem OTA).
  Nossos dados ficam do bloco 32 em diante (32–33 queda, 40 hora) e o `rtcTimeSave` não grava se há comando do eboot
  pendente.
- **Layout da EEPROM:** `Settings` (magia `POM4`) só cresce **no fim**, com valor seguro para o "lixo" de unidades
  antigas; `BOOT_OFF = 400` e os registros seguintes (contador de inícios, queda, `FS2A`, dica do Wi-Fi) têm offsets e
  tamanhos fixos, protegidos por `static_assert`. Campo retirado vira reservado (ex.: `spi80`), nunca é reaproveitado
  sem migração.
- **Assinatura OTA:** RSA-2048 PKCS#1 v1.5 sobre o SHA-256 do arquivo como enviado, em `X-Firmware-Sig`, nunca dentro do
  firmware. Chave pública em `ota-public.pem`/`ota_key.h` (id `cfe9e53642f6521e`). Trocar a chave ou o formato deixa
  os relógios só com o modo de recuperação. Ver [ASSINATURAS.md](ASSINATURAS.md).
- **Formato do `/stats.bin`:** cabeçalho `StatsHead` + anel de 371 dias. É o histórico de quem usa; sobrevive à
  migração de layout e ao backup/restauração.
- **Painel por hash:** o firmware só serve o `/www/<hash>.gz` cujo hash está compilado nele, e o site nunca sobrescreve
  um `panel/*` já publicado (versões antigas apontam para ele).

## 5. Segurança

- **Guardas HTTP:** `hostOk` (só IP ou o nome `.local` próprio, contra DNS rebinding), `panelOk` (cabeçalho
  `X-SmallTV`), `authed` com espera por IP após senha errada (`AuthThrottle`). Endpoint novo usa as mesmas guardas.
- **Upload:** `UploadGate` com limite de bytes, autorização antes do primeiro byte, nada de escrita simultânea no
  LittleFS. Arquivos de fora (GIF, JPEG, pacotes, JSON) são validados antes de decodificar; parsers novos ganham teste
  com entrada malformada e fuzzing (`sim/testes.sh`).
- **Sem segredos no repositório.** A chave privada fica em `~/.smalltv-pomodoro/ota-private.pem` (ou
  `SMALLTV_SIGNING_KEY`), fora do git, da CI e do site; o `.gitignore` recusa `*private*.pem` e `*.key`; o
  `publicar.sh` varre o diff atrás de segredos.
- **Rede hostil:** hora só conta para o histórico depois de confirmada; Wi-Fi não aceita rebaixar para rede aberta.

## 6. Release

- **Sempre pelo `scripts/publicar.sh`** (ensaio sem `--publicar`). Estável: o commit já está no `origin/main` e vai para
  `firmware/latest.json`. `--beta`: `firmware/beta.json`, pré-lançamento, só para quem marcou versões de teste.
- **Beta primeiro.** Mudança de layout, OTA, EEPROM ou RTC sai como teste e só vira estável depois de testada num
  relógio de verdade.
- **Versão com no máximo 7 caracteres e 3 partes** (`FW_VERSION` cabe em `CrashRec::ver[8]`, por `static_assert`). Por
  isso não existe `1.24.10.1`.
- **Notas em `notas/<versão>.json`**, pt/en/es, curtas, do ponto de vista de quem usa.
- **Versão perigosa ganha `aviso` no `notas/historico.json`** (ex.: 1.11.0, 1.15.1, 1.20.0, 1.23.2, 1.23.3, 1.24.0,
  1.24.1), que a página de versões mostra.

## 7. Processo

- **Uma sessão, um assunto, um PR.** PR pequeno, com título e descrição em português, dizendo o que foi testado de
  verdade (simulador, testes, compilação, aparelho) e o que não foi.
- **O Sonnet implementa, o Opus revisa.** O `cto` é consultado antes de ideia nova, de abrir/mesclar PR e de publicar.
- **O Davi mescla.** Nenhuma sessão mescla PR nem publica versão sem pedido explícito dele.
- **Nada de atribuição ao Claude** em commits e PRs (sem `Co-Authored-By`, sem "Generated with Claude Code").
- **Decisão nova vai para o [DECISOES.md](DECISOES.md)** no mesmo PR.
- **Testes antes do PR:** `sim/build.sh` (com `git checkout -- faces/ docs/` depois), `sim/testes.sh` quando mexe em
  handlers, histórico ou decodificadores, e `compilar.sh`.

## 8. Design

- **Painel:** ícones em pixel art 16×16; inativo em contorno, ativo em duas cores (PR #50, #52). Exceção: os controles
  do timer são em contorno arredondado (PR #53). Pixel nítido em qualquer escala (`fitPx`).
- **Fonte Silkscreen só em destaques**, nunca em texto corrido (PR #53).
- **Verde LCD nos mostradores retrô** (LCD retrô, Previsão LCD, Mundo, Mundo relógio), ligado por padrão e desligável
  (PR #31, #33). É estilo, não correção de cor: só vale na tela ativa desses mostradores.
- **Relógio:** pixel art e fontes desenhadas por código; transições curtas, sem piscar a tela.
- **Textos:** português primeiro, depois en/es; frases curtas, sem jargão.
