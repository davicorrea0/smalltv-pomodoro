#!/bin/sh
# Gera web/tz.json: nome do fuso (IANA, o que o Open-Meteo devolve) -> regra POSIX TZ (o que o relógio usa).
# A regra é a última linha de cada arquivo da tzdata (formato TZif v2+). Rode no Linux/WSL com o pacote
# tzdata atualizado; o painel baixa o resultado do site (panel/tz-<código>.json, com SRI) ao escolher a cidade.
cd "$(dirname "$0")"
Z=/usr/share/zoneinfo
V=$(cat "$Z/tzdata.zi" 2>/dev/null | head -1 | sed 's/^# version //')
{
  printf '{"_versao":"%s"' "$V"
  for d in Africa America Antarctica Arctic Asia Atlantic Australia Europe Indian Pacific Etc; do
    find "$Z/$d" -type f | sort | while read -r f; do
      p=$(tail -n1 "$f")
      case "$p" in ""|TZif*) continue ;; esac
      printf ',"%s":"%s"' "${f#$Z/}" "$p"
    done
  done
  printf '}\n'
} > tz.json
echo "tz.json: $(grep -o '":"' tz.json | wc -l) entradas (inclui _versao), tzdata $V"
