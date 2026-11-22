#!/usr/bin/env python3
"""tokensmall: leva o uso dos assistentes de IA (limites de 5 h e semanal) ao SmallTV Pomodoro.

Roda no PC, só com a biblioteca padrão do Python. Lê números que as próprias ferramentas já deixam no disco:

  - Claude Code: o JSON que ele entrega ao comando `statusLine` (campo `rate_limits`);
  - Codex: as linhas `rate_limits` dos arquivos de sessão em ~/.codex/sessions.

e empurra só o resultado (percentual e tempo até reiniciar) para o relógio, em POST /api/apps/data (campos tk_c5, tk_c7,
tk_x5 e tk_x7).

Regra da K13: credencial não sai da máquina. Este programa NÃO abre arquivos de login (auth.json,
.credentials.json), NÃO chama API de assinatura e NÃO grava nem envia token, cookie, caminho de projeto ou texto de
conversa. O relógio recebe 4 textos curtos como "42% 2h13". A única coisa secreta que passa por aqui é a chave de
escrita do relógio (criada por você no painel, Ajustes -> Home Assistant): vem da variável de ambiente SMALLTV_CHAVE
ou do Gerenciador de Credenciais do Windows, vai só ao relógio da rede local (Authorization: Bearer) e nunca é
impressa nem gravada em arquivo deste programa. A senha do painel não é usada.

Uso:
  tokensmall.py statusline            (comando do statusLine do Claude Code; grava os limites e imprime uma linha)
  tokensmall.py ver                   (mostra o que seria enviado, sem rede)
  tokensmall.py chave guardar         (guarda a chave no Gerenciador de Credenciais do Windows)
  tokensmall.py enviar --relogio IP   (laço: lê e empurra para o relógio; --uma-vez envia uma vez e sai)

Inspirado no TokenTV (https://github.com/click6067-ship-it/token-tv, MIT, click6067-ship-it): a ideia de mostrar os
limites num relógio de mesa é deles. Nenhum código foi copiado.
"""
import argparse
import getpass
import ipaddress
import json
import math
import os
import re
import sys
import tempfile
import time
import urllib.error
import urllib.request

# janela do limite (em minutos, como o Codex a descreve) -> sufixo do campo; ferramenta -> prefixo do campo
JANELAS = {300: "5", 10080: "7"}
PREFIXO = {"claude": "c", "codex": "x"}
CAMPO = "tk_"       # prefixo dos campos no relógio (o armazém é dividido com outros miniapps)
MAX_CAMPOS = 4      # campos deste miniapp (o armazém do relógio tem 16, somados todos os remetentes)
MAX_VALOR = 15      # bytes por valor (APPDATA_VALUE)
MAX_NOME = 8        # caracteres do nome do campo (APPDATA_NAME)
MAX_TELA = 9        # caracteres que o cartão do pacote comporta ("100% 5h59") sem tocar no rótulo
TTL_PADRAO = 900    # s: o relógio mostra "--" se o PC parar de enviar por tanto tempo
ARQUIVOS_CODEX = 5  # sessões mais recentes olhadas atrás da última leitura de limites
CAUDA_CODEX = 256 * 1024
IDADE_MAX = {"5": 3600, "7": 43200}
ESPERA_RECUSA = 600
ALVO_CHAVE = "SmallTV-Pomodoro/tokensmall"
MAX_LOG = 200 * 1024


def pasta_estado():
    """Onde o statusLine deixa os limites do Claude (só percentuais e horários)."""
    if os.environ.get("TOKENSMALL_DIR"):
        return os.environ["TOKENSMALL_DIR"]
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    else:
        base = os.environ.get("XDG_STATE_HOME") or os.path.expanduser("~/.local/state")
    return os.path.join(base, "tokensmall")


def pasta_codex():
    return os.environ.get("CODEX_HOME") or os.path.expanduser("~/.codex")


def numero(v):
    """Percentual válido (finito, 0 a 1000) ou None. Nunca transforma "sem dado" em 0."""
    if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 or v > 1000:
        return None
    return float(v)


def instante(v):
    """Epoch em segundos (inteiro positivo razoável) ou None."""
    if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 1_000_000_000 or v > 4_000_000_000:
        return None
    return int(v)


# ---------- Claude Code: statusLine ----------

def limites_do_statusline(texto):
    """Do JSON do statusLine, só rate_limits.five_hour e seven_day: {"5": {"pct","reset"}, "7": {...}}.
    Nada mais do JSON (cwd, session_id, transcript_path, custo) é lido para fora desta função."""
    dados = json.loads(texto)
    rl = dados.get("rate_limits") if isinstance(dados, dict) else None
    saida = {}
    for chave, janela in (("five_hour", "5"), ("seven_day", "7")):
        j = rl.get(chave) if isinstance(rl, dict) else None
        if isinstance(j, dict):
            pct, reset = numero(j.get("used_percentage")), instante(j.get("resets_at"))
            if pct is not None and reset:
                saida[janela] = {"pct": pct, "reset": reset}
    return saida


def gravar_claude(limites, agora, pasta=None):
    """Grava o estado do Claude de forma atômica. Só números."""
    pasta = pasta or pasta_estado()
    os.makedirs(pasta, exist_ok=True)
    corpo = json.dumps({"t": agora, "limites": limites}, separators=(",", ":"))
    fd, tmp = tempfile.mkstemp(dir=pasta, prefix=".claude-", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(corpo)
        os.replace(tmp, os.path.join(pasta, "claude.json"))
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def _idade_aceita(janela, momento, agora):
    return (isinstance(momento, (int, float)) and not isinstance(momento, bool) and math.isfinite(momento)
            and -300 <= agora - momento <= IDADE_MAX[janela])


def ler_claude(pasta=None, agora=None):
    try:
        with open(os.path.join(pasta or pasta_estado(), "claude.json"), encoding="utf-8") as f:
            dados = json.load(f)
        limites = dados.get("limites", {})
        momento = instante(dados.get("t"))
        return {j: (numero(v.get("pct")), instante(v.get("reset"))) for j, v in limites.items()
                if j in ("5", "7") and isinstance(v, dict)
                and (agora is None or _idade_aceita(j, momento, agora))}
    except (OSError, ValueError, AttributeError):
        return {}


# ---------- Codex: arquivos de sessão ----------

def _ordenados(pasta):
    try:
        return sorted(os.listdir(pasta), reverse=True)
    except OSError:
        return []


def sessoes_recentes(raiz, quantas=ARQUIVOS_CODEX):
    """Os arquivos rollout-*.jsonl mais novos de sessions/AAAA/MM/DD, do mais novo para o mais velho."""
    achados = []
    for ano in _ordenados(raiz):
        for mes in _ordenados(os.path.join(raiz, ano)):
            for dia in _ordenados(os.path.join(raiz, ano, mes)):
                d = os.path.join(raiz, ano, mes, dia)
                for nome in _ordenados(d):
                    if nome.startswith("rollout-") and nome.endswith(".jsonl"):
                        achados.append(os.path.join(d, nome))
                        if len(achados) >= quantas:
                            return achados
    return achados


def _limites_da_linha(linha):
    """Do JSON de uma linha de sessão, só payload.rate_limits (primary/secondary): {"5": (pct, reset), ...}."""
    try:
        obj = json.loads(linha)
    except ValueError:
        return None
    payload = obj.get("payload") if isinstance(obj, dict) else None
    rl = payload.get("rate_limits") if isinstance(payload, dict) else None
    if not isinstance(rl, dict):
        return None
    saida = {}
    for chave in ("primary", "secondary"):
        j = rl.get(chave)
        if not isinstance(j, dict):
            continue
        minutos = j.get("window_minutes")
        janela = JANELAS.get(minutos) if isinstance(minutos, int) and not isinstance(minutos, bool) else None
        pct, reset = numero(j.get("used_percent")), instante(j.get("resets_at"))
        if janela and pct is not None and reset:
            saida[janela] = (pct, reset)
    return saida or None


def ler_codex(home=None, agora=None):
    """Última leitura de limites do Codex, olhando só as linhas que contêm "rate_limits" no fim das sessões mais novas.
    Não abre arquivo de login nem nenhum outro arquivo fora de sessions/."""
    raiz = os.path.join(home or pasta_codex(), "sessions")
    for caminho in sessoes_recentes(raiz):
        try:
            with open(caminho, "rb") as f:
                momento = os.fstat(f.fileno()).st_mtime
                f.seek(0, os.SEEK_END)
                tam = f.tell()
                f.seek(max(0, tam - CAUDA_CODEX))
                cauda = f.read().decode("utf-8", "replace")
        except OSError:
            continue
        for linha in reversed(cauda.splitlines()):
            if '"rate_limits"' not in linha:
                continue
            achado = _limites_da_linha(linha)
            if achado:
                if agora is None:
                    return achado
                return {j: valor for j, valor in achado.items() if _idade_aceita(j, momento, agora)}
    return {}


# ---------- o que vai para o relógio ----------

def resto(segundos):
    """Tempo até reiniciar, curto para caber no cartão do relógio: 45m, 2h13, 23h, 5d."""
    m = max(0, (int(segundos) + 59) // 60)
    if m < 60:
        return "%dm" % m
    if m < 6 * 60:
        return "%dh%02d" % (m // 60, m % 60)
    if m < 48 * 60:
        return "%dh" % (m // 60)
    return "%dd" % (m // (24 * 60))


def valores(leituras, agora):
    """{"tk_c5": "42% 2h13", ...}. Janela cujo reinício já passou, ou sem leitura, fica de fora (o relógio mostra "--").
    leituras: {"claude": {"5": (pct, reset)}, "codex": {...}}."""
    saida = {}
    for ferramenta, prefixo in PREFIXO.items():
        for janela, (pct, reset) in sorted(leituras.get(ferramenta, {}).items()):
            if pct is None or not reset or reset <= agora:
                continue
            saida[CAMPO + prefixo + janela] = "%d%% %s" % (min(100, int(round(pct))), resto(reset - agora))
    for v in saida.values():
        assert len(v.encode()) <= MAX_VALOR and len(v) <= MAX_TELA, v
    assert len(saida) <= MAX_CAMPOS
    return saida


def completar(valores_):
    """Todos os campos do pacote em cada envio: o que não tem leitura vai como "--" (o relógio mostra "--" e a barra
    vazia). Assim um campo antigo nunca fica na tela, tanto se o relógio mescla o envio com o conjunto atual quanto
    se o substitui. Sem nenhuma leitura devolve {} (nada é enviado e o ttl esvazia a tela)."""
    if not valores_:
        return {}
    return {CAMPO + p + j: valores_.get(CAMPO + p + j, "--") for p in PREFIXO.values() for j in JANELAS.values()}


def montar(valores_, ttl=TTL_PADRAO):
    return json.dumps({"ttl": ttl, "values": valores_}, separators=(",", ":"))


def coletar(agora, pasta=None, home=None):
    return {"claude": ler_claude(pasta, agora), "codex": ler_codex(home, agora)}


# ---------- envio ----------

def destino_seguro(texto):
    """Só IPv4 da rede local (privado, loopback ou link-local) ou nome .local: a senha do painel, se houver, e os
    números nunca vão para a internet. O relógio exige o IP (ou o .local dele) no cabeçalho Host."""
    t = (texto or "").strip()
    if t.lower().endswith(".local") and all(c.isalnum() or c in "-." for c in t) and len(t) <= 63:
        return t
    try:
        ip = ipaddress.IPv4Address(t)
    except ValueError:
        raise ValueError("use o IP do relógio (192.168.x.x) ou o nome .local dele") from None
    if not (ip.is_private or ip.is_loopback or ip.is_link_local):
        raise ValueError("o relógio tem de estar na rede local (IP privado)")
    return t


class _SemRedirecionar(urllib.request.HTTPRedirectHandler):
    """O relógio nunca redireciona. Seguir um 30x trocaria o POST por GET e levaria o Authorization a outro endereço."""

    def redirect_request(self, *args, **kwargs):
        return None


# Sem proxy (nem o do ambiente nem o do Windows: a senha do painel iria para o proxy) e sem seguir redirecionamento.
_ABRIR = urllib.request.build_opener(urllib.request.ProxyHandler({}), _SemRedirecionar())


def _chave_na_variavel():
    return os.environ.get("SMALLTV_CHAVE")


def _api_credenciais():
    import ctypes
    from ctypes import wintypes

    class CREDENCIAL(ctypes.Structure):
        _fields_ = [("Flags", wintypes.DWORD), ("Type", wintypes.DWORD),
                    ("TargetName", wintypes.LPWSTR), ("Comment", wintypes.LPWSTR),
                    ("LastWritten", wintypes.FILETIME), ("CredentialBlobSize", wintypes.DWORD),
                    ("CredentialBlob", ctypes.POINTER(ctypes.c_ubyte)), ("Persist", wintypes.DWORD),
                    ("AttributeCount", wintypes.DWORD), ("Attributes", ctypes.c_void_p),
                    ("TargetAlias", wintypes.LPWSTR), ("UserName", wintypes.LPWSTR)]

    api = ctypes.WinDLL("advapi32", use_last_error=True)
    ponteiro = ctypes.POINTER(CREDENCIAL)
    api.CredReadW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(ponteiro)]
    api.CredReadW.restype = wintypes.BOOL
    api.CredWriteW.argtypes = [ctypes.POINTER(CREDENCIAL), wintypes.DWORD]
    api.CredWriteW.restype = wintypes.BOOL
    api.CredDeleteW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD]
    api.CredDeleteW.restype = wintypes.BOOL
    api.CredFree.argtypes = [ctypes.c_void_p]
    api.CredFree.restype = None
    return ctypes, api, CREDENCIAL, ponteiro


def _erro_credenciais(acao):
    import ctypes
    codigo = ctypes.get_last_error()
    if codigo == 1168:
        return None
    raise ValueError("nao foi possivel %s a chave no Gerenciador de Credenciais do Windows (erro %d)" %
                     (acao, codigo))


def _cofre_ler(alvo=ALVO_CHAVE):
    if os.name != "nt":
        raise ValueError("so no Windows; use a variavel SMALLTV_CHAVE")
    ctypes, api, _, ponteiro = _api_credenciais()
    credencial = ponteiro()
    if not api.CredReadW(alvo, 1, 0, ctypes.byref(credencial)):
        return _erro_credenciais("ler")
    try:
        tamanho = credencial.contents.CredentialBlobSize
        dados = ctypes.string_at(credencial.contents.CredentialBlob, tamanho)
        if tamanho == 64:
            return dados.decode("utf-16le")
        if tamanho == 32:
            return dados.decode("ascii")
        return "!"
    except (UnicodeError, ValueError):
        return "!"
    finally:
        api.CredFree(credencial)


def _cofre_gravar(alvo=ALVO_CHAVE, chave=None):
    if os.name != "nt":
        raise ValueError("so no Windows; use a variavel SMALLTV_CHAVE")
    ctypes, api, CREDENCIAL, _ = _api_credenciais()
    dados = chave.encode("utf-16le")
    blob = (ctypes.c_ubyte * len(dados)).from_buffer_copy(dados)
    credencial = CREDENCIAL()
    credencial.Type = 1
    credencial.TargetName = alvo
    credencial.CredentialBlobSize = len(dados)
    credencial.CredentialBlob = ctypes.cast(blob, ctypes.POINTER(ctypes.c_ubyte))
    credencial.Persist = 2
    credencial.UserName = "tokensmall"
    if not api.CredWriteW(ctypes.byref(credencial), 0):
        _erro_credenciais("gravar")


def _cofre_apagar(alvo=ALVO_CHAVE):
    if os.name != "nt":
        raise ValueError("so no Windows; use a variavel SMALLTV_CHAVE")
    _, api, _, _ = _api_credenciais()
    if not api.CredDeleteW(alvo, 1, 0):
        _erro_credenciais("apagar")


def _validar_chave(chave, fonte):
    if not re.fullmatch(r"[0-9a-fA-F]{32}", chave or ""):
        raise ValueError("chave invalida (%s); defina SMALLTV_CHAVE ou rode 'tokensmall.py chave guardar'" % fonte)
    return chave.lower()  # o relógio só aceita a chave em minúsculas


def obter_chave():
    """Obtém e valida a chave sem jamais incluí-la em saídas ou erros."""
    chave = _chave_na_variavel()
    if chave:
        return _validar_chave(chave, "variavel")
    if os.name == "nt":
        chave = _cofre_ler()
        if chave:
            return _validar_chave(chave, "Gerenciador de Credenciais")
    raise ValueError("defina SMALLTV_CHAVE ou rode 'tokensmall.py chave guardar' no Windows")


def autenticacao(chave):
    """Cabeçalhos de autenticação do relógio, num lugar só: Authorization: Bearer <chave de escrita>."""
    return {"Authorization": "Bearer " + chave}


class Recusa(str):
    def __new__(cls, texto, definitiva=False):
        obj = super().__new__(cls, texto)
        obj.definitiva = definitiva
        return obj


def gravar_ultimo(resultado, quando=None, pasta=None):
    """Registra somente o horário e o resultado, sem corpo nem valores."""
    tmp = None
    try:
        pasta = pasta or pasta_estado()
        os.makedirs(pasta, exist_ok=True)
        instante_atual = time.time() if quando is None else quando
        linha = "%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(instante_atual)), resultado)
        fd, tmp = tempfile.mkstemp(dir=pasta, prefix=".ultimo-", suffix=".tmp")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(linha)
        os.replace(tmp, os.path.join(pasta, "ultimo.txt"))
        tmp = None
    except Exception:
        if tmp:
            try:
                os.unlink(tmp)
            except OSError:
                pass


def gravar_log(caminho, resultado, quando=None):
    """Acrescenta somente horário e resultado; qualquer falha de disco é ignorada."""
    try:
        instante_atual = time.time() if quando is None else quando
        linha = "%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(instante_atual)), resultado)
        caminho = caminho or os.path.join(pasta_estado(), "envio.log")
        os.makedirs(os.path.dirname(os.path.abspath(caminho)), exist_ok=True)
        dados = linha.encode("utf-8")
        try:
            tamanho = os.path.getsize(caminho)
        except OSError:
            tamanho = 0
        if tamanho + len(dados) > MAX_LOG:
            os.replace(caminho, caminho + ".1")
        with open(caminho, "a", encoding="utf-8") as f:
            f.write(linha)
    except Exception:
        pass


def _mostrar(args, mensagem, quando, horario=True):
    if horario:
        print(time.strftime("%H:%M:%S", time.localtime(quando)), mensagem, flush=True)
    else:
        print(mensagem, flush=True)
    if getattr(args, "log", None) is not None:
        gravar_log(args.log, mensagem, quando)


def _campos_enviados(corpo):
    try:
        dados = json.loads(corpo)
        nomes = dados.get("values", {}).keys()
        return ",".join(sorted(n for n in nomes if isinstance(n, str)))
    except (ValueError, AttributeError):
        return ""


def enviar(host, corpo, chave, timeout=5):
    """POST /api/apps/data. Devolve None se o relógio aceitou, ou um texto curto com o motivo."""
    req = urllib.request.Request("http://%s/api/apps/data" % host, data=corpo.encode("utf-8"), method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("X-SmallTV", "1")
    for nome, valor in autenticacao(chave).items():
        req.add_header(nome, valor)
    try:
        with _ABRIR.open(req, timeout=timeout) as r:
            r.read(256)
        return None
    except urllib.error.HTTPError as e:
        if 300 <= e.code < 400:
            texto = "o relógio respondeu com redirecionamento (%d): recusado, nada foi seguido" % e.code
        elif e.code == 401:
            texto = "chave recusada (401): confira SMALLTV_CHAVE com a do painel (Ajustes -> Home Assistant)"
        elif e.code == 403:
            texto = "recusado (403): crie a chave no painel (Ajustes -> Home Assistant) e use o IP do relógio, não um nome"
        elif e.code == 429:
            texto = "o relógio pediu espera (429): tentativas demais com chave errada"
        elif e.code == 404:
            texto = "o relógio não tem /api/apps/data: falta o firmware com a rota de dados de fora"
        else:
            texto = "recusado pelo relógio (%d)" % e.code
        return Recusa(texto, e.code in (401, 403, 404, 429) or 300 <= e.code < 400)
    except (urllib.error.URLError, OSError) as e:
        return Recusa("sem resposta do relógio (%s)" % getattr(e, "reason", e))


# ---------- linha de comando ----------

def cmd_statusline(args):
    """Nunca falha nem imprime erro: o texto sai na barra de status do Claude Code."""
    try:
        limites = limites_do_statusline(sys.stdin.read())
        if limites:
            gravar_claude(limites, int(time.time()))
        if not args.silencioso:
            partes = ["%s %d%%" % ({"5": "5h", "7": "7d"}[j], round(v["pct"])) for j, v in sorted(limites.items())]
            print(" · ".join(partes))
    except Exception:
        pass
    return 0


def cmd_ver(args):
    agora = int(time.time())
    v = completar(valores(coletar(agora), agora))
    print(montar(v, args.ttl) if v else "sem leitura (abra o Claude Code ou o Codex e use uma vez)")
    return 0


def cmd_chave(args):
    if args.acao == "guardar":
        chave = getpass.getpass("Chave de escrita do relogio (nao aparece ao digitar): ")
        _validar_chave(chave, "entrada digitada")
        _cofre_gravar(chave=chave)
        print("chave guardada no Gerenciador de Credenciais")
        return 0
    if args.acao == "apagar":
        _cofre_apagar()
        return 0
    guardada = _cofre_ler() is not None
    definida = bool(_chave_na_variavel())
    print("gerenciador:", "guardada" if guardada else "nao guardada")
    print("variavel SMALLTV_CHAVE:", "definida" if definida else "ausente")
    return 0 if guardada or definida else 1


def cmd_enviar(args, agora=time.time, dormir=time.sleep):
    try:
        host = destino_seguro(args.relogio or os.environ.get("SMALLTV_IP"))
    except ValueError as err:
        # sem terminal (pythonw) o motivo precisa ficar em arquivo; endereço errado não se corrige sozinho: sai
        agora_ = int(agora())
        gravar_ultimo("erro: " + str(err), agora_)
        if getattr(args, "log", None) is not None:
            gravar_log(args.log, "erro: " + str(err), agora_)
        raise
    ultimo, ultimo_envio, avisou, proxima_tentativa = None, 0, False, 0
    while True:
        instante_atual = int(agora())
        erro = None
        try:
            v = completar(valores(coletar(instante_atual), instante_atual))
            if not v:
                gravar_ultimo("sem leitura", instante_atual)
                if not avisou:
                    _mostrar(args, "sem leitura de limites ainda; o relógio mostra '--'", instante_atual, False)
                    avisou = True
            elif instante_atual >= proxima_tentativa and (v != ultimo or instante_atual - ultimo_envio >= args.ttl // 3):
                try:
                    chave = obter_chave()  # relida a cada tentativa: 'chave guardar' depois do atalho ou uma chave trocada valem sem reiniciar
                except ValueError as err:
                    if args.uma_vez:
                        raise
                    erro = Recusa(str(err))
                else:
                    erro = enviar(host, montar(v, args.ttl), chave)
                mensagem = ("erro: " + erro) if erro else "enviado " + ", ".join(sorted(v))
                if erro and getattr(erro, "definitiva", False):
                    proxima_tentativa = instante_atual + ESPERA_RECUSA
                    mensagem += "; nova tentativa em 10 min"
                _mostrar(args, mensagem, instante_atual)
                gravar_ultimo(("erro: " + erro) if erro else "enviado " + _campos_enviados(montar(v, args.ttl)), instante_atual)
                if not erro:
                    ultimo, ultimo_envio, avisou = v, instante_atual, False
            if args.uma_vez:
                return 0 if v and not erro else 1
        except Exception as err:
            if args.uma_vez:
                raise
            resultado = "erro inesperado: " + type(err).__name__
            gravar_ultimo(resultado, instante_atual)
            _mostrar(args, resultado, instante_atual)
        dormir(args.intervalo)


def principal(argv=None):
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w")
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w")
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("statusline", help="comando do statusLine do Claude Code")
    s.add_argument("--silencioso", action="store_true", help="não imprime a linha de status")
    s.set_defaults(f=cmd_statusline)
    v = sub.add_parser("ver", help="mostra o que seria enviado, sem rede")
    v.add_argument("--ttl", type=int, default=TTL_PADRAO)
    v.set_defaults(f=cmd_ver)
    c = sub.add_parser("chave", help="gerencia a chave no Gerenciador de Credenciais do Windows")
    ca = c.add_subparsers(dest="acao", required=True)
    for acao in ("guardar", "apagar", "estado"):
        ca.add_parser(acao).set_defaults(f=cmd_chave)
    e = sub.add_parser("enviar", help="empurra os limites para o relógio")
    e.add_argument("--relogio", help="IP do relógio (ou variável SMALLTV_IP)")
    e.add_argument("--intervalo", type=int, default=60, help="segundos entre leituras (padrão 60)")
    e.add_argument("--ttl", type=int, default=TTL_PADRAO, help="segundos de validade no relógio (10 a 86400)")
    e.add_argument("--uma-vez", action="store_true", help="envia uma vez e sai")
    e.add_argument("--log", nargs="?", const="", help="acrescenta as mensagens ao arquivo (padrão: envio.log)")
    e.set_defaults(f=cmd_enviar)
    args = p.parse_args(argv)
    if getattr(args, "ttl", 900) < 10 or getattr(args, "ttl", 900) > 86400:
        p.error("--ttl vai de 10 a 86400")
    if getattr(args, "intervalo", 60) < 5:
        p.error("--intervalo mínimo é 5 s")
    try:
        return args.f(args)
    except ValueError as err:
        print("erro:", err, file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(principal())
