# Testar o relógio contra FragAttacks

FragAttacks (2021) são falhas no jeito como o Wi-Fi monta pacotes fragmentados e agregados. No ESP8266 elas
ficam nas bibliotecas fechadas da Espressif (`libnet80211`, `libpp`): o comunicado da Espressif
([AR2023-008](https://www.espressif.com/sites/default/files/advisory_downloads/AR2023-008%20Security%20Advisory%20for%20WLAN%20FragAttacks%20EN.pdf),
V1.1 de 10/05/2024) traz correção para o ESP8266 RTOS SDK, mas não para o NONOS SDK que o núcleo Arduino
usa. Por isso o firmware não corrige a falha em si; ele limita o que um pacote injetado consegue fazer
(veja "O que o firmware já faz" no fim).

Este roteiro usa a ferramenta pública do pesquisador que descobriu as falhas,
[vanhoefm/fragattacks](https://github.com/vanhoefm/fragattacks), para medir quais delas este relógio tem.
**Use só em aparelhos e redes seus.**

## O que precisa

- Um PC com **Ubuntu 20.04** (kernel até 5.12; o autor testou com 5.8). Pode ser um pendrive "live".
- Uma placa Wi-Fi USB compatível. A recomendada pelo autor é a **Alfa AWUS036ACM**. Outras testadas:
  Intel Wireless-AC 8265/3160, Netgear WN111v2, TP-Link TL-WN722N **v1.x**, Alfa AWUS036NHA. A Intel AX200
  **não** funciona. Com duas placas dá para usar o modo "injeção", mais confiável.
- O relógio perto do PC.

## 1. Instalar a ferramenta (uma vez)

```bash
sudo apt-get update
sudo apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev libssl-dev libdbus-1-dev git pkg-config build-essential macchanger net-tools python3-venv aircrack-ng rfkill firmware-ath9k-htc
git clone https://github.com/vanhoefm/fragattacks.git fragattacks
cd fragattacks/research
./build.sh
./pysetup.sh
```

Drivers com as correções da ferramenta (depois, reinicie o PC):

```bash
sudo apt-get install bison flex linux-headers-$(uname -r)
git clone https://github.com/vanhoefm/fragattacks-drivers58.git fragattacks-drivers58
cd fragattacks-drivers58
make defconfig-wifi
make -j 4
sudo make install
```

Placas com `ath9k_htc` (TP-Link TL-WN722N v1, Alfa AWUS036NHA) também precisam do firmware:
`cd research/ath9k-firmware/ && ./install.sh`.

## 2. Antes de cada sessão

1. Desligue o Wi-Fi do Ubuntu no gerenciador de rede (a ferramenta precisa da placa só para ela).
2. Abra um terminal:
   ```bash
   cd fragattacks/research
   sudo su
   source venv/bin/activate
   ./droptraffic.sh
   ```
3. Descubra o nome da placa com `iw dev` (abaixo, `wlan0`; a segunda placa, se houver, `wlan1`).

## 3. Ligar o relógio na rede de teste

A ferramenta cria uma rede **`testnetwork`**, senha **`abcdefgh`**, com DHCP. O relógio precisa se
conectar a ela:

1. Faça **backup** no painel (Ajustes → Sistema → Fazer backup).
2. No painel, **Resetar Wi-Fi**. O relógio abre a rede `SmallTV-Pomodoro` (a senha aparece na tela).
3. Rode o primeiro teste abaixo (a rede `testnetwork` só existe enquanto a ferramenta roda) e, com ela no
   ar, conecte o celular à `SmallTV-Pomodoro`, escolha `testnetwork` e digite `abcdefgh`.
4. No fim, faça o mesmo com a rede de casa.

O relógio responde a ping, que é o que a ferramenta usa para saber se um pacote forjado foi aceito.

## 4. Conferir o equipamento (obrigatório antes de confiar nos resultados)

```bash
./fragattack.py wlan0 --ap ping
./fragattack.py wlan0 --ap ping I,E,E
```

- `ping` falhou: o relógio não está conectado ou a placa não serve. Não siga.
- `ping I,E,E` falhou: repita os testes todos com `--icmp-size 100` no fim.

Teste de injeção da placa: `./fragattack.py wlan0 ping --inject-test wlan1 --ap` (com uma segunda placa)
ou, no modo injeção, `./test-injection.py wlan0 wlan1`.

## 5. Testes

Rode cada linha (troque `ping` pelo comando da tabela):

```bash
./fragattack.py wlan0 --ap COMANDO
```

Com duas placas: `./fragattack.py wlan0 --inject wlan1 --ap COMANDO`.

| Comando | Falha | Resultado |
|---|---|---|
| `ping I,E --amsdu` | (só mostra se aceita A-MSDU) | |
| `amsdu-inject` | CVE-2020-24588 | |
| `amsdu-inject-bad` | CVE-2020-24588 | |
| `ping I,F,BE,AE` | CVE-2020-24587 | |
| `ping I,E,R,AE` | CVE-2020-24586 | |
| `ping I,E,R,E` | CVE-2020-24586 | |
| `ping I,E,E --inc-pn 2` | CVE-2020-26146 | |
| `ping I,E,P` | CVE-2020-26147 | |
| `ping I,P,E` | CVE-2020-26147 | |
| `ping I,P` | CVE-2020-26140 | |
| `ping I,P,P` | CVE-2020-26143 | |
| `eapol-amsdu I,P` | CVE-2020-26144 | |
| `eapol-amsdu-bad I,P` | CVE-2020-26144 | |
| `ping I,D,P --bcast-ra` | CVE-2020-26145 (conferência manual) | |

Como ler:

- `TEST COMPLETED SUCCESSFULLY` → **vulnerável** a essa falha.
- `Test timed out! Retry to be sure, or manually check result` → não aceitou; repita uma vez para confirmar.
- Os testes com `--bcast-ra` não dão resposta automática: precisariam de uma captura no próprio aparelho,
  que o relógio não tem. Pule ou anote como "não medido".

Mande a tabela preenchida: com ela dá para saber quais ataques valem para este relógio.

## O que o firmware já faz contra um pacote injetado

| Um pacote injetado tentaria | Defesa |
|---|---|
| Mudar a hora (o SNTP aceita qualquer resposta) e apagar o histórico | 1.19.1: a hora só vale para o histórico quando bate com a do Open-Meteo, que chega por TCP; salto não confirmado é desfeito; data no passado nunca apaga |
| Mandar comandos ao painel | Senha do painel (ligue em Ajustes → Sistema) |
| Gravar outro firmware | Senha do painel; firmware assinado está planejado |
| Travar o relógio com senhas erradas | 1.19.1: espera de 1 s por IP, sem congelar a tela |
| Usar o relógio para multiplicar tráfego (SSDP) | 1.19.1: no máximo 4 respostas por segundo |
| Falsear clima ou aviso de chuva | Valores limitados: mostra errado, mas não trava |

Na rede: atualize o firmware do roteador (várias variantes dependem de o roteador também ser vulnerável) e
deixe o relógio numa rede de convidados ou de IoT, isolada dos computadores.
