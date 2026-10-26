# Atualizações assinadas

O envio normal em `/update` exige RSA-2048 com SHA-256, no formato nativo do ESP8266 Arduino core 3.1.2:
`payload + assinatura de 256 bytes + tamanho uint32 little-endian`. A chave pública fica em
`smalltv-pomodoro/ota-public.pem`; `ota-public.h` é gerado dela. O código privado nunca vai ao Git,
ao site, ao CI ou ao relógio.

## Compilar e assinar

Defina `SMALLTV_SIGNING_KEY` para o caminho da chave privada RSA-2048 correspondente e rode
`sh compilar.sh`. O script produz:

- `build/smalltv-pomodoro.ino.bin` e `.bin.gz`: imagens sem assinatura, úteis para instalação física/recuperação.
- Os mesmos nomes com `.signed`: arquivos para atualização normal. A compressão vem antes da assinatura.

`scripts/publicar.sh` exige a variável e verifica os dois arquivos assinados. Ao publicar, mantém os
nomes habituais `.bin` e `.bin.gz`, com a assinatura dentro dos arquivos; tamanhos e hashes do manifesto
são calculados sobre os arquivos assinados. Nenhuma chave privada é necessária para o build no CI.

Para verificar manualmente: `node scripts/assinar.cjs verify <arquivo-assinado>`.
A chave privada configurada é comparada com a pública do projeto antes de assinar. Chave ausente,
diferente ou assinatura inválida interrompe a publicação.

## Transição e recuperação

A primeira instalação desta implementação deve usar o `.bin.gz` assinado: as versões antigas já
aceitam gzip com esse sufixo. O `.bin` assinado não passa na checagem de tamanho das versões antigas.
Depois dessa instalação, atualizações pela LAN passam a exigir a assinatura, mesmo que a senha do painel
esteja vazia. As checagens de autenticação, integridade, tamanho e descompressão continuam sendo feitas.

Para voltar ao firmware GeekMagic ou recuperar acesso se perder a chave, use o modo de recuperação:
três inicializações curtas, rede Wi-Fi de recuperação, senha aleatória exibida na tela. Esse modo é
somente AP (sem conexão à LAN) e permite imagens sem assinatura que passem na validação de integridade.
Use a imagem sem assinatura nesse modo. Essa saída exige proximidade/acesso físico.

A chave privada foi criada fora do repositório, na pasta local de dados do SmallTV-Pomodoro, com
permissão somente para o usuário Windows e SYSTEM. Guarde uma cópia segura: perder a chave impede
assinar atualizações para relógios que confiam nela (mas a recuperação física continua disponível).
Não gere outra chave e substitua a pública sem planejar a migração dos relógios já instalados.

Quem compila um fork pode gerar seu próprio par RSA-2048, substituir `ota-public.pem`, rodar
`node scripts/assinar.cjs header` e assinar com a chave correspondente. Nenhuma chave privada do
projeto é necessária para modificar o software ou instalar uma versão própria via recuperação.

## Validação

`node sim/signing_test.cjs`: arquivos válidos, conteúdo/assinatura alterados, truncamento, bytes extras,
tamanho inválido, chave diferente e chave ausente, usando um par descartável.

`node sim/ota_signing_test.cjs build/testes/ota.cpp` gera um teste que compila a função real
`otaConsume` e testa a imagem compilada em blocos de 1 a 8192 bytes, incluindo cabeçalho dividido,
CRC, truncamento e sufixo da assinatura. O CI compila e executa esse teste após gerar o firmware.

Os testes locais não comprovam instalação e reinicialização num ESP8266 físico. Antes de uma release,
teste em um relógio de desenvolvimento: assinatura válida, arquivo alterado, não assinado, outra chave,
interrupção de upload e recuperação com firmware sem assinatura. Um erro deve manter o firmware atual.

Referência: https://arduino-esp8266.readthedocs.io/en/3.1.2/ota_updates/readme.html#advanced-security-signed-updates
