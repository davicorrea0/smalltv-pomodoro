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

## Janela do layout antigo e a ponte

Um relógio no layout **antigo** (4M3M: 1.23.x ou anterior) só enxerga 1 MB para a atualização: o `.gz` é gravado no fim desse 1 MB e o firmware descompactado precisa terminar antes dele: `firmware (arredondado a 4 KB) + .gz (arredondado, +512 bytes do envio) ≤ 1 MB`. A 1.24.11 tem 584 KB + .gz 431 KB, ~16 KB de folga. Já no 4M2M o teto é o do linker (1.044.464 bytes).

**O que o relógio antigo faz com um firmware grande demais:** recusa sem risco. Desde a 1.14, `otaVerifyGz` descompacta o `.gz` gravado e, se a imagem alcançaria o `.gz`, cancela a troca ("firmware grande demais para descompactar", `eboot_command_clear`) e o firmware atual continua; se o `.gz` nem cabe na área livre, `Update.begin` já falha. Ninguém quebra, mas o painel antigo ficaria oferecendo para sempre uma versão que nunca instala.

**Como os relógios descobrem as versões:** o relógio não manda versão nem layout para o site (o site é estático). Quem decide é o painel, que roda no navegador: lê `firmware/latest.json` (e `firmware/beta.json` com "Receber versões de teste", desde a 1.22), compara `version` com a do relógio (`/api/state`) e envia o arquivo ao `/update`. O painel é o que o firmware instalado guarda (o hash vem dentro do firmware), então cada relógio roda o painel da própria versão. Todos os painéis até a 1.24.11 leem **só o topo** do JSON. Desde a 1.24.0 o `/api/state` informa `fs.l2` (1 = layout 4M2M).

**A ponte** (`scripts/ponte.cjs`, aplicada pelo `scripts/publicar.sh` no `latest.json` e no `beta.json`):

- o **topo** do JSON é sempre uma versão que cabe na janela de 1 MB **e** cujo painel conhece a ponte (`"ponte": 1`). Os painéis antigos só veem essa;
- a versão que não cabe vai para o campo **`l2`** (arquivos em `firmware/l2/` ou `firmware/beta/l2/`, sem tocar nos da ponte). O painel novo (`updL2` em `web/index.html`) usa o `l2` só quando o relógio informa `fs.l2` e o `l2` é mais novo que o topo;
- o `publicar.sh` **recusa** publicar uma versão que não cabe se o topo atual (no beta: o do `beta.json` ou, na falta, o do `latest.json`) não for uma ponte. Por isso a primeira versão com este painel precisa sair como estável cabendo na janela: ela é a ponte;
- uma versão que cabe sempre vira o topo (e a nova ponte); um `l2` mais novo que ela continua valendo.

| Relógio | O que o painel oferece | Caminho |
|---|---|---|
| 1.14–1.23.x (4M3M, painel antigo) | o topo (a ponte) | instala a ponte, que cabe em 1 MB; o painel novo dela passa a ver o `l2` |
| 1.23.2/1.23.3 (OTA quebrada) | o topo, mas a troca não pega | modo de recuperação com o `.bin.gz` da ponte (o da versão grande é recusado sem risco) |
| 1.24.0–1.24.11 (4M2M, painel antigo) | o topo (a ponte) | instala a ponte e depois o `l2` |
| ponte ou mais nova (4M2M, painel novo) | o `l2`, se houver e for mais novo | direto |
| anterior à 1.14 | (como antes) | passa antes pela 1.14 |

O `compilar.sh` não recusa mais o build acima da janela (`RELOGIOS_4M3M=0` é o padrão); só informa "janela antiga: cabe/não cabe". `RELOGIOS_4M3M=1 sh compilar.sh` volta a recusar (para compilar de propósito uma versão-ponte). O `sim/ponte_test.cjs` (rodado pelo `sim/build.sh`) simula cada tipo de relógio atualizando ao longo das publicações.

## O que acontece no relógio

- **Primeiro início em 4M2M** (`fsMount` em `modulos/storage_stats.cpp`): sem a marca `FS2A` na EEPROM (offset 433), o que existe em `0x200000` é um pedaço do sistema de arquivos antigo (ou do firmware original). O firmware formata uma vez, recria `/image`, `/gif` e `/faces`, grava a marca e liga o aviso "reorganizado" (`fs.mig` em `/api/state`). Nos demais inícios o LittleFS é montado **sem formatar**; se a marca existe e a montagem falha, não formata (o painel mostra o botão *Formatar memória de arquivos*).
- O formato só vale no layout 4M2M (detectado pelo símbolo `_FS_start` do linker): a mesma fonte compilada em 4M3M nunca formata.
- *Apagar tudo* (`/rec/wipe`) regrava a marca depois de zerar a EEPROM, para o próximo início não formatar de novo nem oferecer um backup que não existe.
- Se perde: fotos e GIFs, mostradores baixados e o painel em `/www` (o carregador baixa de novo sozinho). Ficam: ajustes, senhas, Wi-Fi e rede (EEPROM) e, copiados do LittleFS antigo antes de formatar, o histórico (`/stats.bin`), o rodízio (`/rotation.txt`) e os avisos de fim de fase (`/hooks.txt`). No primeiro início o LittleFS antigo (0x100000-0x3FA000) ainda está intacto: a atualização pela rede só grava abaixo de 0x100000.

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

Continua igual: pela página do firmware original (*Firmware Upgrade*) envia-se o `.bin` (ou o `.bin.gz`, conforme o tamanho). A imagem precisa caber no espaço do original (mesma janela de 1 MB acima): a partir do original, instale uma versão que caiba (a ponte, no topo do `latest.json`). No primeiro início o LittleFS é formatado: **as fotos e GIFs do original não são mantidos**; salve-os antes pelo gerenciador de arquivos do original, se quiser, e reenvie pelo painel. O Wi-Fi salvo continua valendo (fica na área do SDK, que não muda).

## Voltar de 4M2M

Voltar para uma versão 4M3M (ou para o firmware original) mistura os endereços: o 4M3M espera o LittleFS em `0x100000` e pode escrever por cima do que o 4M2M guarda a partir de `0x200000`. Os ajustes sobrevivem; faça o backup antes e restaure depois (veja também RECUPERACAO.md).

## Publicação

`scripts/publicar.sh` usa o layout padrão (4M2M) e grava `"layout":"4m2m"` no JSON do site; com `LAYOUT=4M3M` não grava. Depois de compilar, ele confere se a versão cabe na janela antiga: se cabe, ela vira o topo (`"ponte": 1`); se não, vai para `l2` e para a pasta `l2/`, e só se o site já tiver uma ponte (veja acima). O ensaio (sem `--publicar`) mostra o JSON que seria gravado, com o topo e o `l2`.
