import unittest

from listener import protocol as P


class TestParseRequest(unittest.TestCase):
    def test_valid(self):
        req = P.parse_request({"command": "ping", "params": {"a": 1}, "request_id": "r1"})
        self.assertEqual(req["command"], "ping")
        self.assertEqual(req["params"], {"a": 1})
        self.assertEqual(req["request_id"], "r1")
        self.assertEqual(req["protocol_version"], P.PROTOCOL_VERSION)

    def test_defaults_params(self):
        req = P.parse_request({"command": "ping"})
        self.assertEqual(req["params"], {})

    def test_missing_command(self):
        with self.assertRaises(P.ProtocolError) as cm:
            P.parse_request({"params": {}})
        self.assertEqual(cm.exception.code, P.ERR_BAD_REQUEST)

    def test_non_dict_body(self):
        with self.assertRaises(P.ProtocolError):
            P.parse_request(["not", "a", "dict"])

    def test_bad_params_type(self):
        with self.assertRaises(P.ProtocolError):
            P.parse_request({"command": "ping", "params": [1, 2]})

    def test_bad_version_type(self):
        with self.assertRaises(P.ProtocolError):
            P.parse_request({"command": "ping", "protocol_version": "one"})


class TestEnvelopes(unittest.TestCase):
    def test_success_shape(self):
        r = P.success_response("r1", {"ok": True}, timing_ms=5)
        self.assertTrue(r["success"])
        self.assertEqual(r["result"], {"ok": True})
        self.assertEqual(r["protocol_version"], P.PROTOCOL_VERSION)
        self.assertIn("verification", r)

    def test_error_shape(self):
        r = P.error_response("r1", P.ERR_TIMEOUT, "too slow")
        self.assertFalse(r["success"])
        self.assertEqual(r["error"]["code"], P.ERR_TIMEOUT)
        self.assertEqual(r["error"]["message"], "too slow")

    def test_capability_unavailable(self):
        r = P.capability_unavailable_response("r1", "add_node", "not in UEFN", ["use Verse"])
        self.assertEqual(r["error"]["code"], P.ERR_CAPABILITY_UNAVAILABLE)
        self.assertEqual(r["error"]["details"]["tool"], "add_node")
        self.assertEqual(r["error"]["details"]["backend"], "uefn")
        self.assertEqual(r["error"]["details"]["alternatives"], ["use Verse"])


if __name__ == "__main__":
    unittest.main()
