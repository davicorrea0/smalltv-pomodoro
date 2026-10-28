# Migração do layout da flash: 4M3M → 4M2M

Até a 1.23.x o firmware usava `eesz=4M3M`; agora usa `eesz=4M2M`. Este documento explica o que muda, os limites e o procedimento de publicação.

## Mapa da flash (4 MB)

| Faixa | Antes (4M3M) | Depois (4M2M) |
|---|---|---|
| `0x000000` | bootloader + programa (até ~1019 KB pelo linker) | igual |
| área livre p/ a atualização | de `fim do programa` até `0x100000` (≈ 1 MB − programa) | de `fim do programa` até `0x200000` (≈ 2 MB − programa) |
| LittleFS | `0x100000`–`0x3FA000` (3 MB, blocos de 8 KB) | `0x200000`–`0x3FA000` (2 MB, blocos de 8 KB) |
| EEPROM (ajustes) | `0x3FB000` | `0x3FB000` (igual) |
| RF cal | `0x3FC000` | `0x3FC000` (igual) |
| Wi-Fi / SDK | `0x3FD000`–`0x3FFFFF` | igual |

Conferido no core 3.1.2 (`boards.txt` e `tools/sdk/ld/eagle.flash.4m2m.ld` / `4m3m.ld`): `_EEPROM_start = 0x405fb000`, `rfcal_addr=0x3FC000`, `_FS_end = 0x405FA000`, bloco `0x2000` e página `0x100` nos dois; só `_FS_start` muda (`0x40300000` → `0x40400000`) e a janela do programa (`irom0_0_seg`, 0xfeff0 bytes ≈ 1019 KB) é a mesma. Confirmado também no ELF compilado (`nm`). Portanto **os ajustes, a senha, o Wi-Fi salvo e o contador de reinícios sobrevivem**; o que muda de lugar é só o LittleFS.

Novo limite do firmware: **~1019 KB** (1.044.464 bytes, o teto de execução do ESP8266). Antes, o limite prático era ~600 KB (firmware + `.gz` cabendo em 1 MB).

## Limite da transição (importante)

A primeira versão 4M2M chega aos relógios por atualização pela rede feita pelo firmware **antigo** (4M3M), que só enxerga 1 MB: o `.gz` é gravado no fim desse 1 MB e o firmware descompactado precisa terminar antes dele. Por isso **a primeira versão 4M2M tem de continuar cabendo no layout antigo**: `firmware (arredondado a 4 KB) + .gz (arredondado, +512 bytes do envio) ≤ 1 MB`. Hoje: 551.920 bytes + .gz 407.844 bytes, folga de ~48 KB de firmware.

Salvaguarda: o `compilar.sh` (e portanto o `scripts/publicar.sh`, que o chama) **recusa** o build que passe desse limite enquanto `RELOGIOS_4M3M=1` (o padrão). Só se muda isso de propósito, com `RELOGIOS_4M3M=0 sh compilar.sh`, quando nenhum relógio estiver mais no layout antigo (ou aceitando que quem ficou para trás atualize pelo modo de recuperação). Um relógio no layout antigo que receba uma versão maior que o limite recusa a troca ("firmware grande demais para descompactar") e continua com o firmware atual, sem risco.

## O que acontece no relógio

- **Primeiro início em 4M2M** (`fsMount` em `main.cpp`): sem a marca `FS2A` na EEPROM (offset 433), o que existe em `0x200000` é um pedaço do sistema de arquivos antigo (ou do firmware original). O firmware formata uma vez, recria `/image`, `/gif` e `/faces`, grava a marca e liga o aviso "reorganizado" (`fs.mig` em `/api/state`). Nos demais inícios o LittleFS é montado **sem formatar**; se a marca existe e a montagem falha, não formata (o painel mostra o botão *Formatar memória de arquivos*).
- O formato só vale no layout 4M2M (detectado pelo símbolo `_FS_start` do linker): a mesma fonte compilada em 4M3M nunca formata.
- *Apagar tudo* (`/rec/wipe`) regrava a marca depois de zerar a EEPROM, para o próximo início não formatar de novo nem oferecer um backup que não existe.
- Foi o que se perdeu: fotos e GIFs, histórico (`/stats.bin`), avisos de fim de fase (`/hooks.txt`), rodízio (`/rotation.txt`), mostradores baixados e o painel em `/www` (o carregador baixa de novo sozinho). Ficam: ajustes, senhas, Wi-Fi, rede.

## Fotos, GIFs e backup no painel

O painel, quando a versão oferecida traz `"layout":"4m2m"` no `latest.json`/`beta.json` e o relógio ainda não informa `fs.l2` (layout antigo), antes de atualizar:

1. avisa o que será apagado e pede confirmação;
2. gera e baixa **um único** `smalltv-backup-AAAA-MM-DD.json` com ajustes, histórico, lista de mostradores, links dos avisos **e as fotos e GIFs** (base64, lidos de `/fs?f=...`);
3. só se o backup deu certo e a pessoa confirma que o arquivo foi salvo é que baixa e instala o firmware.

Depois da atualização o painel mostra o cartão "A memória de arquivos foi reorganizada"; **Restaurar backup** reenvia ajustes, histórico, avisos, mostradores (da galeria) e as fotos/GIFs (por `/api/upload`; o que não couber nos 2 MB é contado e avisado) e dispensa o cartão (`/api/migdone`). É o mesmo botão de backup que já existia: um arquivo só, sem ZIP nem passos extras.

**Limite da arquitetura:** o painel é o que o firmware instalado guarda (`/www/<hash>.gz`); um relógio na 1.23.1 ou anterior tem o painel antigo, que **não** sabe fazer esse backup completo. Duas saídas:

- *Recomendada:* publicar antes uma versão de transição **no layout 4M3M** que só leve este painel (`LAYOUT=4M3M sh compilar.sh`, sem `layout` no JSON), deixar os relógios atualizarem e só então publicar a 4M2M. Não é uma "ponte" de layout: é uma versão comum, e a 4M2M continua chegando direto.
- Sem ela: as notas da versão (`notas/<versão>.json`, mostradas no cartão de atualização do painel antigo) devem dizer, nos três idiomas, para fazer antes o backup (que no painel antigo cobre ajustes, histórico e mostradores) e salvar à mão as fotos e GIFs, e que serão apagados.

## Primeira instalação a partir do firmware original

Continua igual: pela página do firmware original (*Firmware Upgrade*) envia-se o `.bin` (ou o `.bin.gz`, conforme o tamanho). A imagem cabe no espaço do original (mesma regra da transição acima). No primeiro início o LittleFS é formatado: **as fotos e GIFs do original não são mantidos**; salve-os antes pelo gerenciador de arquivos do original, se quiser, e reenvie pelo painel. O Wi-Fi salvo continua valendo (fica na área do SDK, que não muda).

## Voltar de 4M2M

Voltar para uma versão 4M3M (ou para o firmware original) mistura os endereços: o 4M3M espera o LittleFS em `0x100000` e pode escrever por cima do que o 4M2M guarda a partir de `0x200000`. Os ajustes sobrevivem; faça o backup antes e restaure depois (veja também RECUPERACAO.md).

## Publicação

`scripts/publicar.sh` usa o layout padrão (4M2M) e grava `"layout":"4m2m"` no JSON do site; com `LAYOUT=4M3M` não grava. O ensaio (sem `--publicar`) passa pelo mesmo `compilar.sh`, então recusa o firmware que não caiba na janela de transição.
