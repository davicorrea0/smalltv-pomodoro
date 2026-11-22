"""Testes do tokensmall (só biblioteca padrão; nenhum dado real, nenhum login).

Rodar da raiz do repositório:  python -I miniapps/tokensmall/test_tokensmall.py
"""
import ast
import builtins
import contextlib
import http.server
import importlib.util
import io
import json
import os
import re
import sys
import tempfile
import threading
import types
import unittest
from unittest import mock

AQUI = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("tokensmall", os.path.join(AQUI, "tokensmall.py"))
ts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ts)

# texto que NUNCA pode aparecer no que vai para o disco de estado nem para o relógio
SEGREDO = "SEGREDO-FALSO-DE-TESTE-123"
CHAVE = "0123456789abcdef0123456789abcdef"  # chave de escrita falsa, só para os testes
AGORA = 1_794_700_000


def statusline_json(**extra):
    base = {"session_id": SEGREDO, "transcript_path": "C:/clientes/" + SEGREDO + "/t.jsonl",
            "cwd": "C:/clientes/" + SEGREDO, "model": {"display_name": "X"}, "cost": {"total_cost_usd": 1.5},
            "rate_limits": {"five_hour": {"used_percentage": 23.5, "resets_at": AGORA + 7380},
                            "seven_day": {"used_percentage": 41.2, "resets_at": AGORA + 3 * 86400 + 3600}}}
    base.update(extra)
    return json.dumps(base)


def linha_codex(primary, secondary):
    return json.dumps({"timestamp": "2026-11-16T10:00:00Z", "type": "event_msg",
                       "payload": {"type": "token_count", "info": {"texto": SEGREDO},
                                   "rate_limits": {"primary": primary, "secondary": secondary}}})


class Formato(unittest.TestCase):
    def test_resto(self):
        casos = {0: "0m", 1: "1m", 59 * 60: "59m", 60 * 60: "1h00", 2 * 3600 + 13 * 60: "2h13", 5 * 3600 + 59 * 60: "5h59",
                 6 * 3600: "6h", 23 * 3600 + 59 * 60: "23h", 47 * 3600 + 59 * 60: "47h", 48 * 3600: "2d",
                 3 * 86400 + 4 * 3600: "3d", 7 * 86400: "7d"}
        for seg, esperado in casos.items():
            self.assertEqual(ts.resto(seg), esperado, seg)

    def test_cabe_no_cartao_em_qualquer_caso(self):
        """Todo percentual e todo tempo possível (até 7 dias) dão no máximo 9 caracteres: não passam do rótulo."""
        for pct in (0, 1, 9.5, 10, 99.4, 100, 250):
            for seg in range(0, 7 * 86400 + 1, 59):
                texto = ts.valores({"claude": {"5": (pct, AGORA + max(seg, 1))}}, AGORA)["tk_c5"]
                self.assertLessEqual(len(texto), 9, texto)

    def test_valores_cabem_no_relogio(self):
        v = ts.valores({"claude": {"5": (42.4, AGORA + 8000), "7": (100.0, AGORA + 6 * 86400 + 23 * 3600)},
                        "codex": {"5": (0.0, AGORA + 59), "7": (999.0, AGORA + 86400)}}, AGORA)
        self.assertEqual(v["tk_c5"], "42% 2h14")
        self.assertEqual(v["tk_c7"], "100% 6d")
        self.assertEqual(v["tk_x5"], "0% 1m")
        self.assertEqual(v["tk_x7"], "100% 24h")  # nunca passa de 100
        self.assertLessEqual(len(v), 4)
        for k, x in v.items():
            self.assertRegex(k, r"^[a-z0-9_]{1,8}$")
            self.assertLessEqual(len(x.encode()), 15)

    def test_todos_os_campos_em_cada_envio(self):
        v = ts.completar({"tk_c5": "42% 2h13"})
        self.assertEqual(v, {"tk_c5": "42% 2h13", "tk_c7": "--", "tk_x5": "--", "tk_x7": "--"})
        self.assertEqual(ts.completar({}), {})  # nada lido: nada enviado
        self.assertLessEqual(len(ts.montar(v)), 512)

    def test_janela_que_vence_entre_dois_envios_vira_traco(self):
        """Envio parcial renova o ttl no relógio: o campo que perdeu a leitura tem de ir como "--", não sumir."""
        leituras = {"claude": {"5": (42.0, AGORA + 100), "7": (71.0, AGORA + 5 * 86400)}, "codex": {"5": (9.0, AGORA + 900)}}
        antes = ts.completar(ts.valores(leituras, AGORA))
        depois = ts.completar(ts.valores(leituras, AGORA + 101))  # a janela de 5 h do Claude venceu
        self.assertEqual(antes["tk_c5"], "42% 2m")
        self.assertEqual(depois["tk_c5"], "--")
        self.assertEqual(depois["tk_c7"], "71% 4d")
        self.assertEqual(sorted(antes), sorted(depois))  # mesmos 4 campos nos dois envios

    def test_sem_dado_nao_vira_zero(self):
        v = ts.valores({"claude": {"5": (None, AGORA + 100), "7": (50.0, None)}, "codex": {}}, AGORA)
        self.assertEqual(v, {})

    def test_janela_vencida_fica_de_fora(self):
        v = ts.valores({"claude": {"5": (80.0, AGORA - 1), "7": (50.0, AGORA)}, "codex": {"5": (10.0, AGORA + 60)}}, AGORA)
        self.assertEqual(list(v), ["tk_x5"])

    def test_corpo_valido_para_o_motor(self):
        corpo = ts.montar({"tk_c5": "42% 2h13", "tk_c7": "71% 3d"}, 900)
        d = json.loads(corpo)
        self.assertEqual(sorted(d), ["ttl", "values"])
        self.assertLessEqual(len(corpo), 512)


class Claude(unittest.TestCase):
    def test_comando_statusline_ponta_a_ponta(self):
        """O comando lê o JSON da entrada, grava só os limites e imprime a linha de status; nunca falha."""
        import io
        import contextlib
        with tempfile.TemporaryDirectory() as d:
            anterior = os.environ.get("TOKENSMALL_DIR")
            os.environ["TOKENSMALL_DIR"] = d
            entrada, saida = io.StringIO(statusline_json()), io.StringIO()
            original = ts.sys.stdin
            ts.sys.stdin = entrada
            try:
                with contextlib.redirect_stdout(saida):
                    self.assertEqual(ts.principal(["statusline"]), 0)
                self.assertEqual(saida.getvalue().strip(), "5h 24% · 7d 41%")
                self.assertIn("5", ts.ler_claude(d))
                ts.sys.stdin = io.StringIO("lixo")  # entrada estragada: sem erro, sem saída
                with contextlib.redirect_stdout(io.StringIO()) as vazio:
                    self.assertEqual(ts.principal(["statusline"]), 0)
                self.assertEqual(vazio.getvalue(), "")
            finally:
                ts.sys.stdin = original
                if anterior is None:
                    os.environ.pop("TOKENSMALL_DIR", None)
                else:
                    os.environ["TOKENSMALL_DIR"] = anterior

    def test_so_os_limites_chegam_ao_disco(self):
        limites = ts.limites_do_statusline(statusline_json())
        self.assertEqual(limites["5"], {"pct": 23.5, "reset": AGORA + 7380})
        with tempfile.TemporaryDirectory() as d:
            ts.gravar_claude(limites, AGORA, d)
            with open(os.path.join(d, "claude.json"), encoding="utf-8") as f:
                texto = f.read()
            self.assertNotIn(SEGREDO, texto)
            self.assertNotIn("clientes", texto)
            self.assertEqual(ts.ler_claude(d)["7"], (41.2, AGORA + 3 * 86400 + 3600))
            self.assertEqual(os.listdir(d), ["claude.json"])  # sem sobra de arquivo temporário

    def test_json_sem_rate_limits_ou_estragado(self):
        self.assertEqual(ts.limites_do_statusline(json.dumps({"model": {}})), {})
        self.assertEqual(ts.limites_do_statusline(json.dumps({"rate_limits": {"five_hour": {"used_percentage": "x"}}})), {})
        self.assertEqual(ts.limites_do_statusline(json.dumps({"rate_limits": {"five_hour": {"used_percentage": True, "resets_at": AGORA}}})), {})
        self.assertEqual(ts.limites_do_statusline(json.dumps({"rate_limits": {"five_hour": {"used_percentage": -1, "resets_at": AGORA}}})), {})
        with self.assertRaises(ValueError):
            ts.limites_do_statusline("{")

    def test_estado_ausente_ou_estragado(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(ts.ler_claude(d), {})
            with open(os.path.join(d, "claude.json"), "w") as f:
                f.write("lixo")
            self.assertEqual(ts.ler_claude(d), {})

    def test_idade_maxima_por_janela(self):
        limites = ts.limites_do_statusline(statusline_json())
        with tempfile.TemporaryDirectory() as d:
            ts.gravar_claude(limites, AGORA - 59 * 60, d)
            self.assertEqual(sorted(ts.ler_claude(d, AGORA)), ["5", "7"])
            ts.gravar_claude(limites, AGORA - 61 * 60, d)
            self.assertEqual(sorted(ts.ler_claude(d, AGORA)), ["7"])
            ts.gravar_claude(limites, AGORA - 11 * 3600, d)
            self.assertEqual(sorted(ts.ler_claude(d, AGORA)), ["7"])
            ts.gravar_claude(limites, AGORA - 13 * 3600, d)
            self.assertEqual(ts.ler_claude(d, AGORA), {})

    def test_instante_ausente_invalido_ou_muito_no_futuro_some(self):
        limites = ts.limites_do_statusline(statusline_json())
        with tempfile.TemporaryDirectory() as d:
            caminho = os.path.join(d, "claude.json")
            for momento in (None, "agora", AGORA + 1000):
                with open(caminho, "w", encoding="utf-8") as f:
                    json.dump({"limites": limites, "t": momento}, f)
                self.assertEqual(ts.ler_claude(d, AGORA), {})
                self.assertTrue(ts.ler_claude(d))  # sem agora, preserva o comportamento antigo

    def test_ver_omite_leitura_velha(self):
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as home:
            limites = ts.limites_do_statusline(statusline_json())
            ts.gravar_claude(limites, AGORA - 13 * 3600, d)
            antigos = {k: os.environ.get(k) for k in ("TOKENSMALL_DIR", "CODEX_HOME")}
            original = ts.time.time
            os.environ["TOKENSMALL_DIR"], os.environ["CODEX_HOME"] = d, home
            ts.time.time = lambda: AGORA
            try:
                saida = io.StringIO()
                with contextlib.redirect_stdout(saida):
                    self.assertEqual(ts.principal(["ver"]), 0)
                self.assertIn("sem leitura", saida.getvalue())
                self.assertNotIn("tk_c", saida.getvalue())
            finally:
                ts.time.time = original
                for nome, valor in antigos.items():
                    if valor is None:
                        os.environ.pop(nome, None)
                    else:
                        os.environ[nome] = valor


class Codex(unittest.TestCase):
    def montar(self, raiz, dia, nome, linhas):
        pasta = os.path.join(raiz, "sessions", "2026", "11", dia)
        os.makedirs(pasta, exist_ok=True)
        caminho = os.path.join(pasta, nome)
        with open(caminho, "w", encoding="utf-8") as f:
            f.write("\n".join(linhas) + "\n")
        return caminho

    def test_ultima_leitura_e_classificacao_pela_janela(self):
        with tempfile.TemporaryDirectory() as raiz:
            self.montar(raiz, "15", "rollout-2026-11-15T09-00-00-a.jsonl",
                        [linha_codex({"used_percent": 1.0, "window_minutes": 300, "resets_at": AGORA}, None)])
            self.montar(raiz, "16", "rollout-2026-11-16T08-00-00-b.jsonl",
                        [linha_codex({"used_percent": 4.0, "window_minutes": 300, "resets_at": AGORA + 600},
                                     {"used_percent": 19.0, "window_minutes": 10080, "resets_at": AGORA + 86400}),
                         "não é json",
                         json.dumps({"payload": {"type": "token_count", "rate_limits": None}}),
                         linha_codex({"used_percent": 6.0, "window_minutes": 300, "resets_at": AGORA + 600},
                                     {"used_percent": 20.0, "window_minutes": 10080, "resets_at": AGORA + 86400}),
                         json.dumps({"payload": {"type": "agent_message", "message": SEGREDO}})])
            lido = ts.ler_codex(raiz)
            self.assertEqual(lido["5"], (6.0, AGORA + 600))
            self.assertEqual(lido["7"], (20.0, AGORA + 86400))

    def test_so_semanal(self):
        with tempfile.TemporaryDirectory() as raiz:
            self.montar(raiz, "16", "rollout-x.jsonl",
                        [linha_codex({"used_percent": 30.0, "window_minutes": 10080, "resets_at": AGORA + 5000}, None)])
            self.assertEqual(ts.ler_codex(raiz), {"7": (30.0, AGORA + 5000)})

    def test_janela_desconhecida_e_sem_pasta(self):
        with tempfile.TemporaryDirectory() as raiz:
            self.montar(raiz, "16", "rollout-x.jsonl",
                        [linha_codex({"used_percent": 30.0, "window_minutes": 60, "resets_at": AGORA + 5000}, None)])
            self.assertEqual(ts.ler_codex(raiz), {})
            self.assertEqual(ts.ler_codex(os.path.join(raiz, "nao-existe")), {})

    def test_nunca_abre_arquivo_de_login(self):
        """Ao ler o Codex, só arquivos de sessions/ são abertos; auth.json, mesmo presente, fica intocado."""
        with tempfile.TemporaryDirectory() as raiz:
            with open(os.path.join(raiz, "auth.json"), "w") as f:
                f.write(SEGREDO)
            self.montar(raiz, "16", "rollout-x.jsonl",
                        [linha_codex({"used_percent": 30.0, "window_minutes": 300, "resets_at": AGORA + 5000}, None)])
            abertos = []
            original = builtins.open

            def espiao(arq, *a, **k):
                abertos.append(os.fspath(arq))
                return original(arq, *a, **k)
            builtins.open = espiao
            try:
                ts.ler_codex(raiz)
            finally:
                builtins.open = original
            self.assertTrue(abertos)
            for caminho in abertos:
                self.assertIn(os.sep + "sessions" + os.sep, caminho)

    def test_mtime_controla_idade_sem_procurar_sessao_anterior(self):
        with tempfile.TemporaryDirectory() as raiz:
            antigo = self.montar(raiz, "15", "rollout-a.jsonl",
                                  [linha_codex({"used_percent": 1.0, "window_minutes": 300,
                                                "resets_at": AGORA + 5000}, None)])
            novo = self.montar(raiz, "16", "rollout-b.jsonl",
                               [linha_codex({"used_percent": 2.0, "window_minutes": 300,
                                             "resets_at": AGORA + 5000}, None)])
            os.utime(antigo, (AGORA, AGORA))
            os.utime(novo, (AGORA - 61 * 60, AGORA - 61 * 60))
            self.assertEqual(ts.ler_codex(raiz, AGORA), {})
            os.utime(novo, (AGORA - 59 * 60, AGORA - 59 * 60))
            self.assertEqual(ts.ler_codex(raiz, AGORA)["5"], (2.0, AGORA + 5000))


class Destino(unittest.TestCase):
    def test_aceita_rede_local(self):
        for ok in ("192.168.1.50", "10.0.0.7", "127.0.0.1", "169.254.1.2", "172.16.5.5", "pomodoro.local"):
            self.assertEqual(ts.destino_seguro(ok), ok)

    def test_recusa_internet_e_nomes(self):
        for ruim in ("8.8.8.8", "example.com", "", None, "192.168.1.50/x", "192.168.1.50:80", "a b.local", "http://192.168.1.5",
                     "1.1.1.1", "300.1.1.1"):
            with self.assertRaises(ValueError, msg=str(ruim)):
                ts.destino_seguro(ruim)


class Envio(unittest.TestCase):
    """Relógio de mentira em 127.0.0.1: confere cabeçalhos e corpo, e que a chave só vai no Authorization."""

    def servidor(self, status=200, texto=b"{\"ok\":true}", location=None):
        visto = {"pedidos": 0}

        class H(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                visto["pedidos"] += 1
                visto["caminho"] = self.path
                visto["cab"] = {k.lower(): v for k, v in self.headers.items()}
                visto["corpo"] = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                self.send_response(status)
                if location:
                    self.send_header("Location", location)
                self.send_header("Content-Length", str(len(texto)))
                self.end_headers()
                self.wfile.write(texto)

            def log_message(self, *a):
                pass
        srv = http.server.HTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.addCleanup(srv.server_close)  # roda por último (as limpezas saem na ordem inversa)
        self.addCleanup(srv.shutdown)
        return srv, visto

    def test_envio_com_chave_bearer(self):
        srv, visto = self.servidor()
        corpo = ts.montar({"tk_c5": "42% 2h13"}, 900)
        self.assertIsNone(ts.enviar("127.0.0.1:%d" % srv.server_port, corpo, CHAVE))
        self.assertEqual(visto["caminho"], "/api/apps/data")
        self.assertEqual(visto["cab"]["x-smalltv"], "1")
        self.assertEqual(visto["cab"]["content-type"], "application/json")
        self.assertEqual(visto["cab"]["authorization"], "Bearer " + CHAVE)
        self.assertEqual(json.loads(visto["corpo"]), {"ttl": 900, "values": {"tk_c5": "42% 2h13"}})
        self.assertNotIn(CHAVE, visto["corpo"])  # a chave só vai no cabeçalho

    def test_erros_com_mensagem_curta_sem_a_chave(self):
        for status, trecho in ((401, "SMALLTV_CHAVE"), (403, "403"), (404, "rota"), (429, "espera"), (400, "400")):
            srv, _ = self.servidor(status, b"JSON invalido")
            erro = ts.enviar("127.0.0.1:%d" % srv.server_port, "{}", CHAVE)
            self.assertIn(trecho, erro)
            self.assertNotIn(CHAVE, erro)

    def test_nao_usa_proxy(self):
        """Com proxy no ambiente, o envio ao relógio vai direto: o proxy (falso) não pode receber nada."""
        proxy, visto_proxy = self.servidor()
        srv, visto = self.servidor()
        antigos = {k: os.environ.get(k) for k in ("HTTP_PROXY", "http_proxy", "NO_PROXY", "no_proxy")}
        os.environ["HTTP_PROXY"] = os.environ["http_proxy"] = "http://127.0.0.1:%d" % proxy.server_port
        os.environ.pop("NO_PROXY", None)
        os.environ.pop("no_proxy", None)
        try:
            self.assertIsNone(ts.enviar("127.0.0.1:%d" % srv.server_port, ts.montar({"tk_c5": "1% 1m"}), CHAVE))
        finally:
            for k, v in antigos.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        self.assertEqual(visto["pedidos"], 1)
        self.assertEqual(visto_proxy["pedidos"], 0, "a chave de escrita foi para o proxy")

    def test_nao_segue_redirecionamento(self):
        """Um 302 para outro endereço não é seguido: o destino do redirecionamento não recebe nada (nem o Authorization)."""
        outro, visto_outro = self.servidor()
        srv, visto = self.servidor(302, b"", "http://127.0.0.1:%d/api/apps/data" % outro.server_port)
        erro = ts.enviar("127.0.0.1:%d" % srv.server_port, ts.montar({"tk_c5": "1% 1m"}), CHAVE)
        self.assertIn("redirecionamento", erro)
        self.assertNotIn(CHAVE, erro)
        self.assertEqual(visto["pedidos"], 1)
        self.assertEqual(visto_outro["pedidos"], 0, "o redirecionamento foi seguido")

    def test_sem_resposta(self):
        self.assertIn("sem resposta", ts.enviar("127.0.0.1:9", "{}", CHAVE, timeout=1))

    def test_ultimo_de_envio_bom_e_erro_nao_vaza_dados(self):
        """O laço (--uma-vez) grava ultimo.txt com horário e resultado; nunca a chave nem os valores."""
        import contextlib
        import io
        antigo, destino, coletar = os.environ.get("TOKENSMALL_DIR"), ts.destino_seguro, ts.coletar
        chave_antiga = os.environ.get("SMALLTV_CHAVE")
        try:
            with tempfile.TemporaryDirectory() as d:
                os.environ["TOKENSMALL_DIR"] = d
                os.environ["SMALLTV_CHAVE"] = CHAVE
                ts.destino_seguro = lambda texto: texto
                ts.coletar = lambda agora: {"claude": {"5": (42.0, agora + 8000)}, "codex": {}}
                for status, esperado in ((None, r"enviado tk_c5,tk_c7,tk_x5,tk_x7\n$"), (401, r"erro: chave recusada \(401\)")):
                    srv, _ = self.servidor(status) if status else self.servidor()
                    args = types.SimpleNamespace(relogio="127.0.0.1:%d" % srv.server_port, ttl=900, intervalo=60, uma_vez=True)
                    with contextlib.redirect_stdout(io.StringIO()):
                        ts.cmd_enviar(args)
                    with open(os.path.join(d, "ultimo.txt"), encoding="utf-8") as f:
                        texto = f.read()
                    self.assertRegex(texto, r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d " + esperado)
                    self.assertNotIn(CHAVE, texto)
                    self.assertNotIn("42%", texto)
        finally:
            ts.destino_seguro, ts.coletar = destino, coletar
            for nome, valor in (("TOKENSMALL_DIR", antigo), ("SMALLTV_CHAVE", chave_antiga)):
                if valor is None:
                    os.environ.pop(nome, None)
                else:
                    os.environ[nome] = valor

    def test_falha_ao_gravar_ultimo_nao_levanta(self):
        antigo = os.environ.get("TOKENSMALL_DIR")
        try:
            with tempfile.TemporaryDirectory() as d:
                caminho = os.path.join(d, "nao-e-pasta")
                with open(caminho, "w") as f:
                    f.write("x")
                os.environ["TOKENSMALL_DIR"] = caminho
                srv, _ = self.servidor()
                self.assertIsNone(ts.enviar("127.0.0.1:%d" % srv.server_port, "{}", CHAVE))
        finally:
            if antigo is None:
                os.environ.pop("TOKENSMALL_DIR", None)
            else:
                os.environ["TOKENSMALL_DIR"] = antigo


class CredenciaisWindows(unittest.TestCase):
    def setUp(self):
        self.guardado = {}
        self.ler = mock.Mock(side_effect=lambda alvo=ts.ALVO_CHAVE: self.guardado.get(alvo))

        def gravar(alvo=ts.ALVO_CHAVE, chave=None):
            self.guardado[alvo] = chave

        def apagar(alvo=ts.ALVO_CHAVE):
            self.guardado.pop(alvo, None)

        self.gravar = mock.Mock(side_effect=gravar)
        self.apagar = mock.Mock(side_effect=apagar)
        self.patches = [mock.patch.object(ts, "_cofre_ler", self.ler),
                        mock.patch.object(ts, "_cofre_gravar", self.gravar),
                        mock.patch.object(ts, "_cofre_apagar", self.apagar),
                        mock.patch.object(ts.os, "name", "nt")]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()

    @contextlib.contextmanager
    def ambiente(self, valor=None):
        anterior = os.environ.pop("SMALLTV_CHAVE", None)
        if valor is not None:
            os.environ["SMALLTV_CHAVE"] = valor
        try:
            yield
        finally:
            os.environ.pop("SMALLTV_CHAVE", None)
            if anterior is not None:
                os.environ["SMALLTV_CHAVE"] = anterior

    def test_variavel_vence_o_gerenciador(self):
        self.guardado[ts.ALVO_CHAVE] = "f" * 32
        with self.ambiente(CHAVE):
            self.assertEqual(ts.obter_chave(), CHAVE)
        self.ler.assert_not_called()

    def test_gerenciador_usado_sem_variavel(self):
        self.guardado[ts.ALVO_CHAVE] = CHAVE
        with self.ambiente():
            self.assertEqual(ts.obter_chave(), CHAVE)

    def test_sem_fonte_gera_erro_neutro(self):
        with self.ambiente(), self.assertRaises(ValueError) as ctx:
            ts.obter_chave()
        self.assertIn("SMALLTV_CHAVE", str(ctx.exception))
        self.assertIn("chave guardar", str(ctx.exception))
        self.assertNotIn(CHAVE, str(ctx.exception))

    def test_valores_invalidos_identificam_a_fonte_sem_eco(self):
        ruim_variavel = "valor-invalido-na-variavel"
        with self.ambiente(ruim_variavel), self.assertRaises(ValueError) as ctx:
            ts.obter_chave()
        self.assertIn("variavel", str(ctx.exception))
        self.assertNotIn(ruim_variavel, str(ctx.exception))

        ruim_gerenciador = "valor-invalido-no-gerenciador"
        self.guardado[ts.ALVO_CHAVE] = ruim_gerenciador
        with self.ambiente(), self.assertRaises(ValueError) as ctx:
            ts.obter_chave()
        self.assertIn("Gerenciador de Credenciais", str(ctx.exception))
        self.assertNotIn(ruim_gerenciador, str(ctx.exception))

    def test_guardar_e_apagar_sem_vazar(self):
        for digitado, codigo, deve_gravar in ((CHAVE, 0, True), ("texto-invalido", 2, False)):
            with self.subTest(valido=deve_gravar):
                self.guardado.clear()
                saida, erros = io.StringIO(), io.StringIO()
                with mock.patch.object(ts.getpass, "getpass", return_value=digitado), \
                        contextlib.redirect_stdout(saida), contextlib.redirect_stderr(erros):
                    self.assertEqual(ts.principal(["chave", "guardar"]), codigo)
                junto = saida.getvalue() + erros.getvalue()
                self.assertNotIn(digitado, junto)
                self.assertEqual(bool(self.guardado), deve_gravar)

        self.guardado[ts.ALVO_CHAVE] = CHAVE
        self.assertEqual(ts.principal(["chave", "apagar"]), 0)
        self.assertEqual(self.guardado, {})
        self.assertEqual(ts.principal(["chave", "apagar"]), 0)

    def test_estado_nao_imprime_chave_e_retorna_codigo(self):
        casos = ((None, None, 1), (CHAVE, None, 0), (None, CHAVE, 0))
        for no_gerenciador, na_variavel, esperado in casos:
            with self.subTest(gerenciador=bool(no_gerenciador), variavel=bool(na_variavel)):
                self.guardado.clear()
                if no_gerenciador:
                    self.guardado[ts.ALVO_CHAVE] = no_gerenciador
                with self.ambiente(na_variavel):
                    saida, erros = io.StringIO(), io.StringIO()
                    with contextlib.redirect_stdout(saida), contextlib.redirect_stderr(erros):
                        self.assertEqual(ts.principal(["chave", "estado"]), esperado)
                self.assertNotIn(CHAVE, saida.getvalue())
                self.assertNotIn(CHAVE, erros.getvalue())
                self.assertIn("gerenciador:", saida.getvalue())
                self.assertIn("variavel SMALLTV_CHAVE:", saida.getvalue())

    def test_guardar_recusa_argumentos(self):
        for argv in (["chave", "guardar", "--alguma-coisa"], ["chave", "guardar", CHAVE]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as ctx:
                    ts.principal(argv)
                self.assertEqual(ctx.exception.code, 2)
        self.gravar.assert_not_called()


class ApiWindows(unittest.TestCase):
    def test_modulo_importa_sem_ctypes_disponivel(self):
        outro = importlib.util.spec_from_file_location("tokensmall_sem_ctypes", os.path.join(AQUI, "tokensmall.py"))
        modulo = importlib.util.module_from_spec(outro)
        with mock.patch.dict(sys.modules, {"ctypes": None}):
            outro.loader.exec_module(modulo)

    def test_funcoes_fora_do_windows_explicam_a_alternativa(self):
        with mock.patch.object(ts.os, "name", "posix"):
            for funcao, args in ((ts._cofre_ler, ()), (ts._cofre_gravar, (ts.ALVO_CHAVE, CHAVE)),
                                 (ts._cofre_apagar, ())):
                with self.assertRaisesRegex(ValueError, "so no Windows; use a variavel SMALLTV_CHAVE"):
                    funcao(*args)

    @unittest.skipUnless(os.name == "nt", "ida e volta real exige Windows")
    def test_ida_e_volta_real_no_gerenciador(self):
        alvo = "SmallTV-Pomodoro/teste-%d" % os.getpid()
        try:
            ts._cofre_apagar(alvo)
            ts._cofre_gravar(alvo, CHAVE)
            self.assertEqual(ts._cofre_ler(alvo), CHAVE)
            ts._cofre_apagar(alvo)
            self.assertIsNone(ts._cofre_ler(alvo))
        finally:
            ts._cofre_apagar(alvo)


class LogEnvio(unittest.TestCase):
    def test_log_cresce_gira_e_nao_contem_chave(self):
        with tempfile.TemporaryDirectory() as d:
            caminho = os.path.join(d, "envio.log")
            args = types.SimpleNamespace(relogio="127.0.0.1", ttl=900, intervalo=60,
                                         uma_vez=True, log="")
            leitura = {"claude": {"5": (42.0, AGORA + 8000)}, "codex": {}}
            with mock.patch.dict(os.environ, {"TOKENSMALL_DIR": d}), \
                    mock.patch.object(ts, "destino_seguro", side_effect=lambda x: x), \
                    mock.patch.object(ts, "obter_chave", return_value=CHAVE), \
                    mock.patch.object(ts, "coletar", return_value=leitura), \
                    mock.patch.object(ts, "enviar", return_value=None), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ts.cmd_enviar(args, agora=lambda: AGORA), 0)
                primeiro = os.path.getsize(caminho)
                self.assertEqual(ts.cmd_enviar(args, agora=lambda: AGORA + 1), 0)
                self.assertGreater(os.path.getsize(caminho), primeiro)
                with open(caminho, encoding="utf-8") as f:
                    texto = f.read()
                self.assertRegex(texto, r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d enviado ")
                self.assertNotIn(CHAVE, texto)

                with open(caminho, "w", encoding="utf-8") as f:
                    f.write("x" * ts.MAX_LOG)
                self.assertEqual(ts.cmd_enviar(args, agora=lambda: AGORA + 2), 0)
                self.assertTrue(os.path.exists(caminho + ".1"))
                self.assertEqual(os.path.getsize(caminho + ".1"), ts.MAX_LOG)
                with open(caminho, encoding="utf-8") as f:
                    novo = f.read()
                self.assertNotIn(CHAVE, novo)
                self.assertLess(os.path.getsize(caminho), ts.MAX_LOG)


class InstaladorWindows(unittest.TestCase):
    def test_script_tem_bom_parametros_e_nenhuma_chave(self):
        caminho = os.path.join(AQUI, "windows", "instalar.ps1")
        self.assertTrue(os.path.isfile(caminho))
        with open(caminho, "rb") as f:
            dados = f.read()
        self.assertTrue(dados.startswith(b"\xef\xbb\xbf"))
        texto = dados.decode("utf-8-sig")
        self.assertNotIn("SMALLTV_CHAVE", texto)
        self.assertIsNone(re.search(r"(?i)(?<![0-9a-f])[0-9a-f]{32}(?![0-9a-f])", texto))
        for esperado in ("-Relogio", "-Remover", "-SoMostrar", "WindowsApps"):
            self.assertIn(esperado, texto)


class Laco(unittest.TestCase):
    class Parar(BaseException):
        pass

    def setUp(self):
        self.chave_antiga = os.environ.get("SMALLTV_CHAVE")
        os.environ["SMALLTV_CHAVE"] = CHAVE
        self.destino_original = ts.destino_seguro
        ts.destino_seguro = lambda texto: texto  # o relógio de mentira fica em 127.0.0.1:porta
        self.dir_antigo = os.environ.get("TOKENSMALL_DIR")
        self.tmp = tempfile.TemporaryDirectory()
        os.environ["TOKENSMALL_DIR"] = self.tmp.name

    def tearDown(self):
        ts.destino_seguro = self.destino_original
        self.tmp.cleanup()
        if self.dir_antigo is None:
            os.environ.pop("TOKENSMALL_DIR", None)
        else:
            os.environ["TOKENSMALL_DIR"] = self.dir_antigo
        if self.chave_antiga is None:
            os.environ.pop("SMALLTV_CHAVE", None)
        else:
            os.environ["SMALLTV_CHAVE"] = self.chave_antiga

    def args(self, porta, intervalo=300, uma_vez=False):
        return types.SimpleNamespace(relogio="127.0.0.1:%d" % porta, ttl=900,
                                     intervalo=intervalo, uma_vez=uma_vez)

    def leitura(self, agora):
        return {"claude": {"5": (42.0, agora + 7200)}, "codex": {}}

    def test_recusas_esperam_dez_minutos(self):
        import contextlib
        import io
        original = ts.coletar
        try:
            for status in (401, 403, 404, 429, 302):
                with self.subTest(status=status):
                    srv, visto = Envio.servidor(self, status, b"", "/nao-seguir" if status == 302 else None)
                    relogio = [AGORA]
                    chamadas = [0]

                    def dormir(_):
                        chamadas[0] += 1
                        relogio[0] += 300
                        if chamadas[0] == 3:
                            raise self.Parar()

                    ts.coletar = lambda agora: self.leitura(agora)
                    with contextlib.redirect_stdout(io.StringIO()) as saida:
                        with self.assertRaises(self.Parar):
                            ts.cmd_enviar(self.args(srv.server_port), agora=lambda: relogio[0], dormir=dormir)
                    self.assertEqual(visto["pedidos"], 2)
                    self.assertIn("nova tentativa em 10 min", saida.getvalue())
        finally:
            ts.coletar = original

    def test_erro_de_rede_tenta_em_cada_volta(self):
        original_coletar, original_enviar = ts.coletar, ts.enviar
        tentativas, relogio = [0], [AGORA]

        def enviar(*_):
            tentativas[0] += 1
            return ts.Recusa("sem resposta do relógio (teste)")

        def dormir(_):
            relogio[0] += 60
            if tentativas[0] == 3:
                raise self.Parar()

        try:
            ts.coletar = lambda agora: self.leitura(agora)
            ts.enviar = enviar
            with self.assertRaises(self.Parar):
                ts.cmd_enviar(self.args(9, 60), agora=lambda: relogio[0], dormir=dormir)
            self.assertEqual(tentativas[0], 3)
        finally:
            ts.coletar, ts.enviar = original_coletar, original_enviar

    def test_chave_relida_a_cada_tentativa_e_falta_de_chave_nao_mata_o_laco(self):
        """Atalho criado antes de 'chave guardar', ou chave trocada: o laço pega a nova sem reiniciar e registra o motivo."""
        import contextlib
        import io
        from unittest import mock
        outra = "fedcba9876543210fedcba9876543210"
        usadas, estado = [], {"chave": None}
        relogio = [AGORA]

        def obter():
            if estado["chave"] is None:
                raise ValueError("defina SMALLTV_CHAVE ou rode 'tokensmall.py chave guardar'")
            return estado["chave"]

        def enviar(host, corpo, chave):
            usadas.append(chave)
            return None

        def dormir(_):
            relogio[0] += 60
            if len(usadas) == 0 and relogio[0] == AGORA + 120:
                estado["chave"] = CHAVE        # a pessoa rodou 'chave guardar'
            if len(usadas) == 1:
                estado["chave"] = outra        # e depois trocou a chave
            if len(usadas) == 2:
                raise self.Parar()

        leitura = lambda agora: {"claude": {"5": (42.0 + (agora - AGORA) // 60, agora + 8000)}, "codex": {}}
        with mock.patch.object(ts, "obter_chave", side_effect=obter), mock.patch.object(ts, "enviar", side_effect=enviar), \
                mock.patch.object(ts, "coletar", side_effect=leitura), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(self.Parar):
                ts.cmd_enviar(self.args(9, 60), agora=lambda: relogio[0], dormir=dormir)
        self.assertEqual(usadas, [CHAVE, outra])
        with open(os.path.join(self.tmp.name, "ultimo.txt"), encoding="utf-8") as f:
            self.assertIn("enviado", f.read())

    def test_sem_chave_registra_o_motivo_em_ultimo_txt(self):
        import contextlib
        import io
        from unittest import mock
        relogio = [AGORA]

        def dormir(_):
            raise self.Parar()

        with mock.patch.object(ts, "obter_chave", side_effect=ValueError("defina SMALLTV_CHAVE ou rode 'tokensmall.py chave guardar'")), \
                mock.patch.object(ts, "coletar", side_effect=lambda agora: self.leitura(agora)), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(self.Parar):
                ts.cmd_enviar(self.args(9, 60), agora=lambda: relogio[0], dormir=dormir)
        with open(os.path.join(self.tmp.name, "ultimo.txt"), encoding="utf-8") as f:
            self.assertIn("erro: defina SMALLTV_CHAVE", f.read())

    def test_uma_vez_devolve_1_se_o_relogio_recusou(self):
        import contextlib
        import io
        from unittest import mock
        with mock.patch.object(ts, "obter_chave", return_value=CHAVE), \
                mock.patch.object(ts, "coletar", side_effect=lambda agora: self.leitura(agora)), \
                mock.patch.object(ts, "enviar", return_value=ts.Recusa("chave recusada (401)", True)), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(ts.cmd_enviar(self.args(9, 60, uma_vez=True), agora=lambda: AGORA), 1)

    def test_ip_invalido_fica_registrado_antes_de_sair(self):
        from unittest import mock
        ts.destino_seguro = self.destino_original   # este teste quer a validação de verdade
        with mock.patch.object(ts, "obter_chave", return_value=CHAVE):
            with self.assertRaises(ValueError):
                ts.cmd_enviar(types.SimpleNamespace(relogio="8.8.8.8", ttl=900, intervalo=60, uma_vez=False, log=None),
                              agora=lambda: AGORA)
        with open(os.path.join(self.tmp.name, "ultimo.txt"), encoding="utf-8") as f:
            self.assertIn("erro:", f.read())

    def test_chave_em_maiusculas_vai_em_minusculas(self):
        from unittest import mock
        with mock.patch.dict(os.environ, {"SMALLTV_CHAVE": CHAVE.upper()}):
            self.assertEqual(ts.obter_chave(), CHAVE)

    def test_excecao_inesperada_nao_mata_o_laco(self):
        original_coletar, original_enviar = ts.coletar, ts.enviar
        chamadas, registrado = [0], []
        antigo = os.environ.get("TOKENSMALL_DIR")
        try:
            with tempfile.TemporaryDirectory() as d:
                os.environ["TOKENSMALL_DIR"] = d

                def coletar(agora):
                    chamadas[0] += 1
                    if chamadas[0] == 1:
                        raise OSError("nome-de-arquivo-que-nao-deve-sair")
                    return self.leitura(agora)

                def dormir(_):
                    if chamadas[0] == 1:
                        with open(os.path.join(d, "ultimo.txt"), encoding="utf-8") as f:
                            registrado.append(f.read())
                    else:
                        raise self.Parar()

                ts.coletar, ts.enviar = coletar, lambda *_: None
                with self.assertRaises(self.Parar):
                    ts.cmd_enviar(self.args(9, 60), agora=lambda: AGORA, dormir=dormir)
                self.assertEqual(chamadas[0], 2)
                self.assertIn("erro inesperado: OSError", registrado[0])
                self.assertNotIn("nome-de-arquivo", registrado[0])
        finally:
            ts.coletar, ts.enviar = original_coletar, original_enviar
            if antigo is None:
                os.environ.pop("TOKENSMALL_DIR", None)
            else:
                os.environ["TOKENSMALL_DIR"] = antigo

    def test_uma_vez_nao_dorme_e_propaga_excecao(self):
        original = ts.coletar
        try:
            ts.coletar = lambda agora: (_ for _ in ()).throw(OSError("teste"))
            with self.assertRaises(OSError):
                ts.cmd_enviar(self.args(9, uma_vez=True), agora=lambda: AGORA,
                               dormir=lambda _: self.fail("--uma-vez não pode dormir"))
        finally:
            ts.coletar = original


class Regra1(unittest.TestCase):
    """A regra nº 1 da K13 vigiada no código: nada de arquivo de login nem API de assinatura no tokensmall.py."""

    def test_codigo_sem_credencial(self):
        with open(os.path.join(AQUI, "tokensmall.py"), encoding="utf-8") as f:
            src = f.read()
        arvore = ast.parse(src)
        texto = ast.get_docstring(arvore) or ""
        sem_doc = src.replace(texto, "", 1)
        proibidos = ["auth.json", ".credentials", "accessToken", "access_token", "refresh_token", "oauth", "SMALLTV_SENHA", "Basic ",
                     "api.anthropic.com", "chatgpt.com", "security find", "keychain", "cookie"]
        for p in proibidos:
            self.assertNotIn(p.lower(), sem_doc.lower(), p)

    def test_unico_segredo_e_a_chave_do_relogio(self):
        with open(os.path.join(AQUI, "tokensmall.py"), encoding="utf-8") as f:
            src = f.read()
        self.assertEqual(src.count("os.environ.get(\"SMALLTV_CHAVE\")"), 1)
        self.assertEqual(src.count("\"Bearer \""), 1)  # só em autenticacao()
        self.assertNotIn("print(chave", src)


if __name__ == "__main__":
    unittest.main()
