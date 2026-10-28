#!/bin/bash
# Publica uma versão do SmallTV Pomodoro na ordem certa, conferindo cada passo:
#   scripts/publicar.sh [--beta] [--publicar]
#
# Sem --publicar é um ensaio: confere, compila, testa e monta o site numa pasta temporária, mostrando o que
# mudaria, sem enviar nada. Com --publicar: envia o site (gh-pages), cria o release e confere no ar.
#   estável (padrão): o commit já precisa estar no origin/main; atualiza firmware/latest.json (todos recebem).
#   --beta: versão de teste; vai para firmware/beta/ e firmware/beta.json (só quem marcou "Receber versões
#           de teste" no painel) e o release sai como pré-lançamento.
# Notas (3 itens curtos por idioma): notas/<versão>.json = {"pt": [...], "en": [...], "es": [...]}
# Requer: git, gh (logado), node, arduino-cli; testes em C++ no Linux ou no WSL (g++ com multilib).
set -euo pipefail
cd "$(dirname "$0")/.."
BETA=0
PUB=0
for a in "$@"; do
  case "$a" in
    --beta) BETA=1 ;;
    --publicar) PUB=1 ;;
    *) echo "uso: $0 [--beta] [--publicar]"; exit 2 ;;
  esac
done
REPO=davicorrea0/smalltv-pomodoro
SITE=https://davicorrea0.github.io/smalltv-pomodoro
say() { printf '\n== %s\n' "$*"; }
die() { echo "ERRO: $*" >&2; exit 1; }
VER=$(grep -m1 -o 'FW_VERSION "[^"]*"' smalltv-pomodoro/main.cpp | cut -d'"' -f2)
TAG="v$VER"
NOTAS="notas/$VER.json"
if [ $BETA = 1 ]; then KIND="teste (beta)"; DIR=firmware/beta; JSON=firmware/beta.json; else KIND=estável; DIR=firmware; JSON=firmware/latest.json; fi

say "versão $VER, $KIND$([ $PUB = 1 ] && echo ', PUBLICANDO' || echo ', ensaio: nada será enviado')"
[ -z "$(git status --porcelain --untracked-files=no)" ] || die "há mudanças não commitadas"
git fetch -q origin
gh release view "$TAG" -R "$REPO" >/dev/null 2>&1 && die "o release $TAG já existe"
if [ $BETA = 0 ] && [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" ]; then
  die "versão estável: o commit atual precisa já estar no origin/main (git push origin HEAD:main)"
fi
[ -f "$NOTAS" ] || die "faltam as notas da versão: $NOTAS"
node -e 'const n=require("./'"$NOTAS"'");for(const l of ["pt","en","es"])if(!Array.isArray(n[l])||!n[l].length)throw new Error("notas sem "+l)'

say "segredos no que mudou desde o último release estável"
LAST=$(gh release list -R "$REPO" -L 20 --exclude-pre-releases --json tagName -q '.[0].tagName')
if git diff --unified=0 "$LAST" HEAD -- . ':!*.bin' ':!*.gz' | grep '^+' | grep -v '^+++' | grep -inE 'password\s*[:=]|api[_-]?key\s*[:=]|secret\s*[:=]|token\s*[:=]|BEGIN [A-Z ]*PRIVATE KEY|Bearer [A-Za-z0-9_-]{20,}' |
  grep -vE 'password=(true|false)|allowed=password|formToken = token|\.token\b|\*\*Optional panel password:\*\*'; then
  die "possível segredo no diff (linhas acima): confira antes de publicar"
fi
echo "nada encontrado (desde $LAST)"

say "simulador e painel"
[ -d web/node_modules ] || (cd web && npm install --no-audit --no-fund)
bash sim/build.sh | grep -E "troca|^pomodoro|painel:"
git checkout -q -- faces/ docs/ 2>/dev/null || true
git diff --quiet -- smalltv-pomodoro/web.h || die "o web.h gerado é diferente do commitado: commite o painel gerado"

say "testes em C++ (handlers, histórico, GIF, JPEG)"
if command -v g++ >/dev/null && [ -z "${LOCALAPPDATA:-}" ]; then
  bash sim/testes.sh 2000
else  # Windows: gera os .cpp com o node daqui e roda no WSL
  mkdir -p build/testes
  node sim/security_test.cjs build/testes/sec.cpp
  node sim/stats_test.cjs build/testes/stats.cpp
  L="/mnt/$(pwd -W | cut -c1 | tr 'A-Z' 'a-z')$(pwd -W | cut -c3-)"
  MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash -c "GEN=$L/build/testes bash $L/sim/testes.sh 2000"
fi

say "firmware"
sh compilar.sh | grep -E "firmware:|caberia"
BIN=build/smalltv-pomodoro.ino.bin
GZ=$BIN.gz
grep -q "FW_VERSION" smalltv-pomodoro/main.cpp && grep -aq "$VER" "$BIN" || die "o binário não traz a versão $VER"
# Confere o .bin.gz do jeito que o eboot vai usá-lo (tamanho nos 4 últimos bytes, janela de 8 KB, nada depois do
# gzip, magic 0xE9). Um .gz assinado por dentro apagou o gerenciador de boot na 1.20.0.
node scripts/conferir-gz.cjs "$BIN" "$GZ" || die "o .bin.gz não é seguro para o eboot (veja acima)"

say "assinatura (à parte do firmware, nunca dentro dele)"
# A chave privada fica fora do repositório (~/.smalltv-pomodoro/ota-private.pem ou SMALLTV_SIGNING_KEY).
# O ota_key.h compilado precisa ser o da ota-public.pem: senão o relógio recusaria tudo o que assinamos.
cp smalltv-pomodoro/ota_key.h build/ota_key.h.antes
node scripts/assinar.cjs header >/dev/null
cmp -s smalltv-pomodoro/ota_key.h build/ota_key.h.antes || { mv build/ota_key.h.antes smalltv-pomodoro/ota_key.h; die "o ota_key.h não é o da ota-public.pem"; }
rm -f build/ota_key.h.antes
node -e 'if(!require("fs").readFileSync(process.argv[1]).includes(require("./scripts/assinar.cjs").rawKey().n))process.exit(1)' "$BIN" ||
  die "o binário não traz a chave pública de ota-public.pem"
node scripts/assinar.cjs sign "$BIN" "$GZ"
node scripts/assinar.cjs verify "$BIN" "$GZ"

say "site (gh-pages)"
W=$(mktemp -d)/gh-pages
git -c core.autocrlf=false worktree add -q --detach "$W" origin/gh-pages
trap 'git worktree remove --force "$W" 2>/dev/null || true' EXIT
for f in $(node -p 'require("./web/out/panel.json").site.join(" ")'); do
  if [ -f "$W/panel/$f" ]; then
    cmp -s "web/out/$f" "$W/panel/$f" || die "panel/$f já existe no site com outro conteúdo (nunca sobrescrever)"
  else
    cp "web/out/$f" "$W/panel/$f"
    echo "novo no site: panel/$f"
  fi
done
mkdir -p "$W/$DIR"
cp "$BIN" "$W/$DIR/smalltv-pomodoro.bin"
cp "$GZ" "$W/$DIR/smalltv-pomodoro.bin.gz"
cp "$BIN.sig" "$W/$DIR/smalltv-pomodoro.bin.sig"
cp "$GZ.sig" "$W/$DIR/smalltv-pomodoro.bin.gz.sig"
PREFIX=$([ $BETA = 1 ] && echo "beta/" || echo "")
node -e '
const fs=require("fs"),[w,dir,json,ver,notas,prefix]=process.argv.slice(1),p=require("./web/out/panel.json");
const j={version:ver,date:new Date().toISOString().slice(0,10),file:prefix+"smalltv-pomodoro.bin",size:fs.statSync(w+"/"+dir+"/smalltv-pomodoro.bin").size,
 notes:require("./"+notas),gz:prefix+"smalltv-pomodoro.bin.gz",gzsize:fs.statSync(w+"/"+dir+"/smalltv-pomodoro.bin.gz").size,panel:p.panel,psri:p.psri,
 sig:fs.readFileSync(w+"/"+dir+"/smalltv-pomodoro.bin.sig").toString("base64"),gzsig:fs.readFileSync(w+"/"+dir+"/smalltv-pomodoro.bin.gz.sig").toString("base64")};
fs.writeFileSync(w+"/"+json,JSON.stringify(j,null,2)+"\n");console.log(json+":",JSON.stringify({version:j.version,size:j.size,gzsize:j.gzsize,panel:j.panel}))' "$W" "$DIR" "$JSON" "$VER" "$NOTAS" "$PREFIX"
git -C "$W" add -A
git -C "$W" status --short

if [ $PUB = 0 ]; then
  say "ensaio concluído: nada foi enviado (para publicar: $0 $([ $BETA = 1 ] && echo '--beta ')--publicar)"
  exit 0
fi

say "enviando o site"
git -C "$W" commit -q -m "Publica firmware $VER ($KIND)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git -C "$W" push -q origin HEAD:gh-pages

say "release $TAG"
TARGET=$(git rev-parse HEAD)
[ $BETA = 1 ] && git push -q origin "HEAD:refs/heads/release/$VER"
R=$(mktemp -d)
cp "$BIN" "$R/smalltv-pomodoro-$TAG.bin"
cp "$GZ" "$R/smalltv-pomodoro-$TAG.bin.gz"
cp "$BIN.sig" "$R/smalltv-pomodoro-$TAG.bin.sig"
cp "$GZ.sig" "$R/smalltv-pomodoro-$TAG.bin.gz.sig"
(cd "$R" && sha256sum smalltv-pomodoro-"$TAG".bin smalltv-pomodoro-"$TAG".bin.gz | sed 's/ \*/  /' > SHA256SUMS.txt)
node -e 'const n=require("./'"$NOTAS"'");console.log(n.pt.map(x=>"- "+x).join("\n")+"\n\n## Como instalar\nPelo painel: \"Atualizar agora\". Pelo `/update`: envie o **`.bin.gz` sem descompactar** junto com o **`.bin.gz.sig`** (a assinatura; sem ela o relógio recusa). Confira os arquivos com `SHA256SUMS.txt`.")' > "$R/notas.md"
gh release create "$TAG" -R "$REPO" --target "$TARGET" $([ $BETA = 1 ] && echo --prerelease || echo --latest) \
  --title "Pomodoro $VER" --notes-file "$R/notas.md" "$R"/smalltv-pomodoro-"$TAG".bin "$R"/smalltv-pomodoro-"$TAG".bin.gz "$R"/smalltv-pomodoro-"$TAG".bin.sig "$R"/smalltv-pomodoro-"$TAG".bin.gz.sig "$R"/SHA256SUMS.txt

say "conferindo no ar"
for i in $(seq 1 40); do
  v=$(curl -s "$SITE/$JSON?x=$RANDOM" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{try{console.log(JSON.parse(s).version)}catch(e){}})')
  [ "$v" = "$VER" ] && break
  sleep 15
done
[ "$v" = "$VER" ] || die "o site ainda não mostra $VER em $JSON"
[ "$(curl -s "$SITE/$DIR/smalltv-pomodoro.bin.gz?x=$RANDOM" | sha256sum | cut -c1-64)" = "$(sha256sum "$GZ" | cut -c1-64)" ] || die "o .bin.gz no site é diferente do compilado"
P=$(node -p 'require("./web/out/panel.json").panel')
[ "$(curl -s "$SITE/panel/$P" | sha256sum | cut -c1-24)" = "$(echo "$P" | sed 's/panel-\(.*\)\.html\.gz/\1/')" ] || die "o painel no site não confere com o hash"
say "publicado: $VER ($KIND) — $SITE/$JSON"
