# Amostras de pacotes .stheme

Sementes do teste e do fuzzing do leitor de pacotes `.stheme` (`smalltv-pomodoro/stheme.h`,
etapa 0 do leitor spec 2): `bash sim/test-stheme.sh`, também rodado por `sim/testes.sh`.

## Origem e licença

| Arquivo | Origem | Licença |
|---|---|---|
| `terminal-ops.stheme`, `live-status.stheme`, `pixel-room.stheme` | `examples/themes/` do [smalltv-mod](https://github.com/giovi321/smalltv-mod), commit `6e65e033599c43779ad1eea31743b26c65b4130f`, sem alteração. Gerados lá pelo `tools/theme_pack.py` (spec 1, STI1) | WTFPL v2 (compatível com a GPL) |
| `sti/*.sti` | `tests/theme/sti/` do smalltv-mod, mesmo commit, sem alteração: as imagens de teste combinadas no RFC #31, em todas as formas que o `theme_pack.py` escreve (STI1, STI2 direto, STI2 com paleta) | WTFPL v2 |
| `imagens-spec2.stheme`, `dados-spec2.stheme` | Montados por `montar.py` (deste projeto) com as imagens de `sti/`: o contêiner e o manifesto seguem o formato do `theme_pack.py`, mas não passaram por ele | GPL-3.0, como o resto do projeto |

Os `.sti` são bytes do empacotador do smalltv-mod; os pixels esperados de cada um estão no
`tests/theme/sti/expected.json` de lá e entram aqui na etapa 1, junto com a decodificação.

## Conferência pendente

Os dois pacotes spec 2 só foram conferidos pelo nosso leitor da etapa 0, que ainda não valida o
conteúdo das camadas. Para confirmar que o smalltv-mod também os aceita, num clone dele:

```sh
python3 tools/smalltv_theme.py validate <caminho>/sim/stheme-seeds/imagens-spec2.stheme
python3 tools/smalltv_theme.py validate <caminho>/sim/stheme-seeds/dados-spec2.stheme
```

Para regerar: `python3 sim/stheme-seeds/montar.py`.
