import json
import unittest
import urllib.error
import urllib.request

from listener import uefn_listener as L
from listener import capabilities as C
from listener import protocol as P
from tests import _fake_unreal as fake


class TestQueueDispatch(unittest.TestCase):
    """Exercise the queue + registry + dispatcher on the calling thread (no HTTP)."""

    def setUp(self):
        self.ctx, self.dispatch = L.build_listener(unreal_module=fake, token="t")

    def _run(self, command, params=None):
        req_id = self.ctx.queue.submit(command, params or {})
        self.ctx.queue.pump(self.dispatch)
        return self.ctx.queue.wait(req_id, 1.0)

    def test_ping(self):
        r = self._run("ping")
        self.assertTrue(r["success"])
        self.assertTrue(r["result"]["pong"])
        self.assertEqual(r["result"]["listener_version"], C.LISTENER_VERSION)

    def test_project_info_with_fake_unreal(self):
        r = self._run("get_project_info")
        self.assertTrue(r["success"])
        self.assertTrue(r["result"]["available"])
        self.assertEqual(r["result"]["project_dir"], "/Fake/Project/")

    def test_capabilities_report(self):
        r = self._run("get_capabilities")
        caps = r["result"]["capabilities"]
        self.assertEqual(caps["blueprints.graph_mutation"], C.UNSUPPORTED)
        self.assertEqual(caps["verse.compile"], C.MANUAL_GATE_REQUIRED)
        self.assertTrue(r["result"]["runtime_probes"]["slate_post_tick"])
        self.assertEqual(r["result"]["engine_version"], "5.x-uefn-fake")
        self.assertEqual(r["result"]["python_version"].count("."), 2)

    def test_listener_status(self):
        r = self._run("get_listener_status")
        self.assertEqual(r["result"]["backend"], "uefn")
        self.assertIn("security", r["result"])

    def test_log_tail(self):
        self.ctx.log.info("hello-test")
        r = self._run("get_listener_log", {"last_n": 5})
        msgs = [e["message"] for e in r["result"]["entries"]]
        self.assertIn("hello-test", msgs)

    def test_unknown_command(self):
        r = self._run("does_not_exist")
        self.assertFalse(r["success"])
        self.assertEqual(r["error"]["code"], P.ERR_UNKNOWN_COMMAND)

    def test_capability_gating_blocks_unsupported(self):
        # Wire a command to an UNSUPPORTED capability; dispatch must refuse it.
        self.ctx.registry.add("fake_bp_edit", lambda p, c: {"nope": True},
                              "blueprints.graph_mutation")
        r = self._run("fake_bp_edit")
        self.assertFalse(r["success"])
        self.assertEqual(r["error"]["code"], P.ERR_CAPABILITY_UNAVAILABLE)
        self.assertEqual(r["error"]["details"]["backend"], "uefn")
        self.assertTrue(r["error"]["details"]["alternatives"])

    def test_handler_error_becomes_envelope(self):
        def boom(p, c):
            raise RuntimeError("kaboom")
        self.ctx.registry.add("boom", boom, None)
        r = self._run("boom")
        self.assertFalse(r["success"])
        self.assertEqual(r["error"]["code"], P.ERR_HANDLER_ERROR)


class TestHttpEndToEnd(unittest.TestCase):
    """Full transport: HTTP server + auth + size limits, driven by a background pump
    (unreal_module=None, so off-main-thread execution is safe)."""

    TOKEN = "test-secret-token"

    def setUp(self):
        self.ctx = L.start_listener(
            unreal_module=None, token=self.TOKEN, desired_port=8790,
            register_tick=False, background_pump=True, max_request_bytes=256,
        )
        self.base = f"http://127.0.0.1:{self.ctx.port}"

    def tearDown(self):
        L.stop_listener(self.ctx)

    def _post(self, body_bytes, token=None):
        req = urllib.request.Request(self.base + "/", data=body_bytes, method="POST")
        req.add_header("Content-Type", "application/json")
        if token:
            req.add_header("X-MCP-Token", token)
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                return resp.status, json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            try:
                return e.code, json.loads(e.read().decode())
            finally:
                e.close()

    def test_health_beacon_unauthenticated(self):
        with urllib.request.urlopen(self.base + "/health", timeout=5) as resp:
            data = json.loads(resp.read().decode())
        self.assertEqual(data["backend"], "uefn")
        self.assertTrue(data["auth_required"])

    def test_ping_ok_with_token(self):
        body = json.dumps({"command": "ping"}).encode()
        status, data = self._post(body, token=self.TOKEN)
        self.assertEqual(status, 200)
        self.assertTrue(data["success"])
        self.assertTrue(data["result"]["pong"])

    def test_ping_rejected_without_token(self):
        body = json.dumps({"command": "ping"}).encode()
        status, data = self._post(body)
        self.assertEqual(status, 401)
        self.assertEqual(data["error"]["code"], P.ERR_UNAUTHORIZED)

    def test_wrong_token_rejected(self):
        body = json.dumps({"command": "ping"}).encode()
        status, data = self._post(body, token="wrong")
        self.assertEqual(status, 401)

    def test_oversize_rejected(self):
        body = json.dumps({"command": "ping", "params": {"pad": "x" * 500}}).encode()
        status, data = self._post(body, token=self.TOKEN)
        self.assertEqual(status, 413)
        self.assertEqual(data["error"]["code"], P.ERR_REQUEST_TOO_LARGE)

    def test_malformed_json_rejected(self):
        status, data = self._post(b"{not json", token=self.TOKEN)
        self.assertEqual(status, 400)
        self.assertEqual(data["error"]["code"], P.ERR_BAD_REQUEST)

    def test_missing_command_rejected(self):
        body = json.dumps({"params": {}}).encode()
        status, data = self._post(body, token=self.TOKEN)
        self.assertEqual(status, 400)


if __name__ == "__main__":
    unittest.main()
