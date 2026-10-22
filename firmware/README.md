# Firmware mais recente

`latest.json` e `smalltv-pomodoro.bin` são lidos pelo painel do relógio (a partir da v1.8.0): se a versão daqui for mais nova que a do relógio, o painel mostra as novidades (`notes` em pt/en/es) e atualiza com um toque, enviando o `.bin` para `/update`.

A cada versão nova: copie o `.bin` compilado para `smalltv-pomodoro.bin` e atualize `version`, `date`, `size` (bytes do `.bin`) e `notes`. Os mesmos binários ficam nos [releases](https://github.com/davicorrea0/smalltv-pomodoro/releases).

Desde a 1.14 o relógio também aceita `smalltv-pomodoro.bin.gz` (gerado por `compactar.js`, janela de 8 KB). Quando `latest.json` tiver `"gz"` e `"gzsize"`, o painel baixa o `.gz`; enquanto o `.bin` couber (até ~520 KB), o aviso oferece o `.bin`.
