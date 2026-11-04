# Decisões

Registro curto do que já foi decidido, para nenhuma sessão refazer a discussão. Uma linha por decisão, a mais nova no
topo. Mudar uma decisão = nova entrada que cita a antiga. "A confirmar" = deduzido do histórico; o Davi confirma ou
corrige.

| Data | Decisão | Por quê | PR |
|---|---|---|---|
| 2026-11-04 | Agente `cto` + `PRINCIPIOS.md` + este registro; consultar o CTO antes de ideia nova, PR e release. | Várias sessões em paralelo sem um critério comum geravam inconsistência. | este PR |
| 2026-11-04 | `main.cpp` dividido em `modulos/`, incluídos por `main.cpp` e compilados como **uma só unidade**. | O arduino-cli não compila subpastas fora de `src/`, e 11 `.cpp` separados custavam +1,8 KB; incluídos, o firmware ficou 672 bytes menor. Módulo novo precisa de `#include` no `main.cpp`; nunca mover `modulos/` para `src/`. | #55 (aberto) |
| 2026-11-04 | Fim da trava `RELOGIOS_4M3M` com a **ponte** no `latest.json`: o topo sempre cabe em 1 MB, o maior vai em `l2`. A estável que vira ponte precisa caber em 1 MB. | Deixar o firmware crescer além de 1 MB sem prender relógios no 4M3M. | #54 |
| 2026-11-04 | Controles do timer em **contorno arredondado**; o resto dos ícones segue pixel art 16×16. Silkscreen só em destaques. | Os controles do timer pedem leitura imediata e área de toque; pixel art fica para a navegação. (motivo: a confirmar) | #53 |
| 2026-11-04 | Ícones do painel em pixel art 16×16: contorno quando inativos, duas cores quando ativos. | Identidade visual única entre painel e relógio. | #50, #52 |
| 2026-11-03 | Tabelas do QR Code na flash (`PROGMEM`). Regra geral: constantes vão para a flash. | −336 bytes de RAM; RAM é o recurso mais escasso. | #48, #21 |
| 2026-10-30 | Cor quente à noite pelo **gama digital** do ST7789V (sem redesenhar pixels); o **verde LCD** fica como estilo opcional dos mostradores retrô, só na tela ativa. | O verde apareceu como defeito das tabelas (#29) e foi mantido de propósito como estilo; a cor real das outras telas não pode ser afetada (#33). (a confirmar) | #27, #29, #31, #33 |
| 2026-10-29 | Layout da flash **4M2M** (programa ~1019 KB, LittleFS 2 MB) a partir da 1.24.0, com backup/restauração no painel. | No 4M3M o firmware tinha ~600 KB na prática, sem espaço para crescer. | #20 |
| 2026-10-29 | Constantes da RAM para a flash (−4,9 KB de RAM estática). | Heap livre maior para GIF/JPEG e menos travamentos. | #21 |
| 2026-10-29 | Memória RTC: blocos 0–31 são do eboot; dados próprios do bloco 32 em diante, e nada é gravado com comando do eboot pendente. 1.23.2/1.23.3 marcadas com aviso. | Gravar a hora no bloco do eboot quebrou o OTA da 1.23.2/1.23.3. | #23, #24 |
| 2026-10-28 | **80 MHz no SPI descartado**; tela fixa em 40 MHz e `spi80` reservado. | O ST7789 não aguenta: a imagem vira lixo. | #13, #16 |
| 2026-10-28 | Commits do `publicar.sh` e de qualquer sessão **sem linha de coautor** do Claude. | Pedido do Davi. | #19 |
| 2026-10-26 | Atualização pela rede **assinada** (RSA-2048), assinatura à parte (`.sig`, cabeçalho), nunca dentro do firmware; chave privada fora do repositório. | Impedir firmware de terceiros pela rede; a assinatura colada no `.gz` (1.20.0) apagou o gerenciador de boot. | #5 |
| 2026-10 | Versão com no máximo 7 caracteres e 3 partes (sem `1.24.10.1`). | `FW_VERSION` precisa caber em `CrashRec::ver[8]` na EEPROM. | — |
| a confirmar | **Miniapps só depois de 50 estrelas no GitHub.** | Linha nova de produto custa flash, RAM e manutenção; só vale com público. (decisão do Davi relatada pela sessão principal; não achei registro no repositório) | — |
| a confirmar | Leitor `.stheme` (pacotes do smalltv-mod) entra por etapas e desligado até ter valor claro. | Compatibilidade com outro projeto sem custo para quem não usa. | #45 (aberto) |

## Como registrar

Acrescente uma linha no topo da tabela no mesmo PR que implementa a decisão: data (AAAA-MM-DD), a decisão em uma frase,
o porquê em uma frase e o PR. Se a decisão anula uma anterior, diga qual.
