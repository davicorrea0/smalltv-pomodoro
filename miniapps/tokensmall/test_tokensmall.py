"""Testes do tokensmall (só biblioteca padrão; nenhum dado real, nenhum login).

Rodar da raiz do repositório:  python -I miniapps/tokensmall/test_tokensmall.py
"""
import ast
import builtins
import http.server
import importlib.util
import json
import os
import tempfile
import threading
import unittest

AQUI = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("tokensmall", os.path.join(AQUI, "tokensmall.py"))
ts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ts)

# texto que NUNCA pode aparecer no que vai para o disco de estado nem para o relógio
SEGREDO = "SEGREDO-FALSO-DE-TESTE-123"
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
                texto = ts.valores({"claude": {"5": (pct, AGORA + max(seg, 1))}}, AGORA)["c5"]
                self.assertLessEqual(len(texto), 9, texto)

    def test_valores_cabem_no_relogio(self):
        v = ts.valores({"claude": {"5": (42.4, AGORA + 8000), "7": (100.0, AGORA + 6 * 86400 + 23 * 3600)},
                        "codex": {"5": (0.0, AGORA + 59), "7": (999.0, AGORA + 86400)}}, AGORA)
        self.assertEqual(v["c5"], "42% 2h14")
        self.assertEqual(v["c7"], "100% 6d")
        self.assertEqual(v["x5"], "0% 1m")
        self.assertEqual(v["x7"], "100% 24h")  # nunca passa de 100
        self.assertLessEqual(len(v), 4)
        for k, x in v.items():
            self.assertRegex(k, r"^[a-z0-9_]{1,8}$")
            self.assertLessEqual(len(x.encode()), 15)

    def test_todos_os_campos_em_cada_envio(self):
        v = ts.completar({"c5": "42% 2h13"})
        self.assertEqual(v, {"c5": "42% 2h13", "c7": "--", "x5": "--", "x7": "--"})
        self.assertEqual(ts.completar({}), {})  # nada lido: nada enviado
        self.assertLessEqual(len(ts.montar(v)), 512)

    def test_janela_que_vence_entre_dois_envios_vira_traco(self):
        """Envio parcial renova o ttl no relógio: o campo que perdeu a leitura tem de ir como "--", não sumir."""
        leituras = {"claude": {"5": (42.0, AGORA + 100), "7": (71.0, AGORA + 5 * 86400)}, "codex": {"5": (9.0, AGORA + 900)}}
        antes = ts.completar(ts.valores(leituras, AGORA))
        depois = ts.completar(ts.valores(leituras, AGORA + 101))  # a janela de 5 h do Claude venceu
        self.assertEqual(antes["c5"], "42% 2m")
        self.assertEqual(depois["c5"], "--")
        self.assertEqual(depois["c7"], "71% 4d")
        self.assertEqual(sorted(antes), sorted(depois))  # mesmos 4 campos nos dois envios

    def test_sem_dado_nao_vira_zero(self):
        v = ts.valores({"claude": {"5": (None, AGORA + 100), "7": (50.0, None)}, "codex": {}}, AGORA)
        self.assertEqual(v, {})

    def test_janela_vencida_fica_de_fora(self):
        v = ts.valores({"claude": {"5": (80.0, AGORA - 1), "7": (50.0, AGORA)}, "codex": {"5": (10.0, AGORA + 60)}}, AGORA)
        self.assertEqual(list(v), ["x5"])

    def test_corpo_valido_para_o_motor(self):
        corpo = ts.montar({"c5": "42% 2h13", "c7": "71% 3d"}, 900)
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


class Codex(unittest.TestCase):
    def montar(self, raiz, dia, nome, linhas):
        pasta = os.path.join(raiz, "sessions", "2026", "11", dia)
        os.makedirs(pasta, exist_ok=True)
        with open(os.path.join(pasta, nome), "w", encoding="utf-8") as f:
            f.write("\n".join(linhas) + "\n")

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
    """Relógio de mentira em 127.0.0.1: confere cabeçalhos e corpo, e que a senha só vai no Authorization."""

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

    def test_sem_senha(self):
        srv, visto = self.servidor()
        corpo = ts.montar({"c5": "42% 2h13"}, 900)
        self.assertIsNone(ts.enviar("127.0.0.1:%d" % srv.server_port, corpo))
        self.assertEqual(visto["caminho"], "/api/apps/data")
        self.assertEqual(visto["cab"]["x-smalltv"], "1")
        self.assertEqual(visto["cab"]["content-type"], "application/json")
        self.assertNotIn("authorization", visto["cab"])
        self.assertEqual(json.loads(visto["corpo"]), {"ttl": 900, "values": {"c5": "42% 2h13"}})

    def test_com_senha_so_no_cabecalho(self):
        srv, visto = self.servidor()
        senha = "senha-falsa-de-teste"
        self.assertIsNone(ts.enviar("127.0.0.1:%d" % srv.server_port, ts.montar({"c5": "1% 1m"}), senha))
        self.assertTrue(visto["cab"]["authorization"].startswith("Basic "))
        self.assertNotIn(senha, visto["corpo"])

    def test_erros_com_mensagem_curta_sem_a_senha(self):
        for status, trecho in ((401, "SMALLTV_SENHA"), (403, "403"), (404, "firmware"), (400, "JSON")):
            srv, _ = self.servidor(status, b"JSON invalido")
            erro = ts.enviar("127.0.0.1:%d" % srv.server_port, "{}", "senha-falsa-de-teste")
            self.assertIn(trecho, erro)
            self.assertNotIn("senha-falsa-de-teste", erro)

    def test_nao_usa_proxy(self):
        """Com proxy no ambiente, o envio ao relógio vai direto: o proxy (falso) não pode receber nada."""
        proxy, visto_proxy = self.servidor()
        srv, visto = self.servidor()
        antigos = {k: os.environ.get(k) for k in ("HTTP_PROXY", "http_proxy", "NO_PROXY", "no_proxy")}
        os.environ["HTTP_PROXY"] = os.environ["http_proxy"] = "http://127.0.0.1:%d" % proxy.server_port
        os.environ.pop("NO_PROXY", None)
        os.environ.pop("no_proxy", None)
        try:
            self.assertIsNone(ts.enviar("127.0.0.1:%d" % srv.server_port, ts.montar({"c5": "1% 1m"}), "senha-falsa-de-teste"))
        finally:
            for k, v in antigos.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        self.assertEqual(visto["pedidos"], 1)
        self.assertEqual(visto_proxy["pedidos"], 0, "a senha do painel foi para o proxy")

    def test_nao_segue_redirecionamento(self):
        """Um 302 para outro endereço não é seguido: o destino do redirecionamento não recebe nada (nem o Authorization)."""
        outro, visto_outro = self.servidor()
        srv, visto = self.servidor(302, b"", "http://127.0.0.1:%d/api/apps/data" % outro.server_port)
        erro = ts.enviar("127.0.0.1:%d" % srv.server_port, ts.montar({"c5": "1% 1m"}), "senha-falsa-de-teste")
        self.assertIn("redirecionamento", erro)
        self.assertNotIn("senha-falsa-de-teste", erro)
        self.assertEqual(visto["pedidos"], 1)
        self.assertEqual(visto_outro["pedidos"], 0, "o redirecionamento foi seguido")

    def test_sem_resposta(self):
        self.assertIn("sem resposta", ts.enviar("127.0.0.1:9", "{}", timeout=1))


class Regra1(unittest.TestCase):
    """A regra nº 1 da K13 vigiada no código: nada de arquivo de login nem API de assinatura no tokensmall.py."""

    def test_codigo_sem_credencial(self):
        with open(os.path.join(AQUI, "tokensmall.py"), encoding="utf-8") as f:
            src = f.read()
        arvore = ast.parse(src)
        texto = ast.get_docstring(arvore) or ""
        sem_doc = src.replace(texto, "", 1)
        proibidos = ["auth.json", ".credentials", "accessToken", "access_token", "refresh_token", "Bearer", "oauth",
                     "api.anthropic.com", "chatgpt.com", "security find", "keychain", "cookie"]
        for p in proibidos:
            self.assertNotIn(p.lower(), sem_doc.lower(), p)

    def test_unico_uso_de_senha_e_o_do_relogio(self):
        with open(os.path.join(AQUI, "tokensmall.py"), encoding="utf-8") as f:
            src = f.read()
        self.assertEqual(src.count("os.environ.get(\"SMALLTV_SENHA\")"), 1)
        self.assertNotIn("print(senha", src)


if __name__ == "__main__":
    unittest.main()
