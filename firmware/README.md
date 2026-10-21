# Firmware mais recente

`latest.json` e `smalltv-pomodoro.bin` são lidos pelo painel do relógio (a partir da v1.8.0): se a versão daqui for mais nova que a do relógio, o painel mostra as novidades (`notes` em pt/en/es) e atualiza com um toque, enviando o `.bin` para `/update`.

A cada versão nova: copie o `.bin` compilado para `smalltv-pomodoro.bin` e atualize `version`, `date`, `size` (bytes do `.bin`) e `notes`. Os mesmos binários ficam nos [releases](https://github.com/davicorrea0/smalltv-pomodoro/releases).
