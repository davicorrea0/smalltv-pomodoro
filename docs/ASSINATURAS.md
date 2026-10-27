# Atualizações assinadas (1.20.1)

Pela rede, o relógio só troca de firmware quando o arquivo enviado vem com uma assinatura válida da chave do projeto. Sem isso, qualquer pessoa ou página na mesma rede que passasse pela senha do painel (ou num relógio sem senha) poderia instalar um firmware qualquer.

## Como funciona

- **Algoritmo:** RSA-2048, PKCS#1 v1.5, sobre o SHA-256 do arquivo **exatamente como enviado** (`.bin` ou `.bin.gz`).
- **A assinatura nunca vai dentro do firmware.** Ela é um arquivo à parte (`<arquivo>.sig`, 256 bytes) e chega ao relógio em base64 no cabeçalho HTTP `X-Firmware-Sig`. O painel lê a assinatura em `latest.json`/`beta.json` (`sig` e `gzsig`). Na página `/update`, você escolhe o `.sig` junto com o firmware.
- **Ordem no relógio:**
  1. Durante o envio, cada bloco entra no SHA-256 e na área de atualização.
  2. No fim, a assinatura é conferida **antes** de `Update.end(true)`.
  3. Sem assinatura válida, nada é agendado: o comando do eboot não é gravado e o firmware atual continua.
- **No firmware:** só a conta `br_rsa_i15_pkcs1_vrfy` do BearSSL, com a chave em bytes (`ota_key.h`). Isso custa ~6,5 KB, contra ~37 KB das classes `PublicKey`/`SigningVerifier` do núcleo. A conta roda na pilha extra do núcleo (6,2 KB, alocada só na conferência). Com pouca memória contígua, o relógio recusa a atualização em vez de travar.
- **Modo de recuperação** (físico: ligar e desligar 3 vezes, rede própria com a senha na tela): aceita firmware sem assinatura. É o caminho para voltar ao firmware original da GeekMagic, ou para continuar atualizando se a chave privada se perder.

## Por que não do jeito da 1.20.0

Na 1.20.0, a assinatura ia **colada no fim do `.bin.gz`**, no formato do `Updater` do núcleo (`installSignature`).

- O gerenciador de boot (eboot) usa os **4 últimos bytes** do arquivo guardado como tamanho da imagem descompactada. Com a assinatura no fim, ele leu um tamanho errado e apagou o próprio gerenciador de boot, e o relógio deixou de ligar. Precisou de um cabo USB-serial para recuperar.
- A 1.19.9 recusa qualquer `.bin.gz` com dados depois do gzip. A partir da 1.20.1, `sim/security_test.cjs` falha se `installSignature` aparecer no código.
- `scripts/publicar.sh` também confere que o `.gz` termina com o tamanho do `.bin`.

## Chaves

- Chave privada: `~/.smalltv-pomodoro/ota-private.pem` (ou `SMALLTV_SIGNING_KEY`).
  - Nunca vai para o git, o site, a CI ou o relógio; o `.gitignore` recusa `*private*.pem` e `*.key`.
  - **Guarde uma cópia fora do computador.** Sem ela, os relógios só aceitam novas versões pelo modo de recuperação.
- Chave pública: `smalltv-pomodoro/ota-public.pem`. O `ota_key.h` é gerado a partir dela com `node scripts/assinar.cjs header`. Os testes e o `publicar.sh` conferem que o binário traz essa chave.
- Comandos:
  - `node scripts/assinar.cjs sign <arquivos>` cria os `.sig`;
  - `node scripts/assinar.cjs verify <arquivos>` confere os `.sig`.
- Id da chave atual: `cfe9e53642f6521e`.

## Transição

- Quem está na 1.19.9 ou anterior atualiza para a 1.20.1 normalmente: essas versões ignoram o cabeçalho.
- A partir da 1.20.1, o painel sempre manda a assinatura.
- Um firmware de outra origem, inclusive o original, só entra pelo modo de recuperação.
