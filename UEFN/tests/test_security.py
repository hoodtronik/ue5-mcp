import os
import tempfile
import unittest

from listener import security


class TestTokens(unittest.TestCase):
    def test_generate_token_is_random_and_urlsafe(self):
        a, b = security.generate_token(), security.generate_token()
        self.assertNotEqual(a, b)
        self.assertTrue(all(c.isalnum() or c in "-_" for c in a))
        self.assertGreaterEqual(len(a), 32)

    def test_tokens_match_constant_time(self):
        self.assertTrue(security.tokens_match("abc", "abc"))
        self.assertFalse(security.tokens_match("abc", "abd"))
        self.assertFalse(security.tokens_match("", "abc"))
        self.assertFalse(security.tokens_match(None, "abc"))
        self.assertFalse(security.tokens_match("abc", ""))

    def test_check_auth_raises_on_mismatch(self):
        security.check_auth("secret", "secret")  # no raise
        with self.assertRaises(security.AuthError):
            security.check_auth("nope", "secret")
        with self.assertRaises(security.AuthError):
            security.check_auth(None, "secret")

    def test_load_or_create_token_persists(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "sub", "token")
            t1 = security.load_or_create_token(path)
            t2 = security.load_or_create_token(path)
            self.assertEqual(t1, t2)
            self.assertTrue(os.path.exists(path))

    def test_env_token_takes_precedence(self):
        old = os.environ.get(security.TOKEN_ENV)
        try:
            os.environ[security.TOKEN_ENV] = "env-secret"
            with tempfile.TemporaryDirectory() as d:
                path = os.path.join(d, "token")
                self.assertEqual(security.load_or_create_token(path), "env-secret")
                self.assertFalse(os.path.exists(path))  # env wins, file not created
        finally:
            if old is None:
                os.environ.pop(security.TOKEN_ENV, None)
            else:
                os.environ[security.TOKEN_ENV] = old


class TestSizeLimits(unittest.TestCase):
    def test_check_size_ok(self):
        security.check_size(100, 1000)

    def test_check_size_over_limit(self):
        with self.assertRaises(security.RequestTooLargeError):
            security.check_size(2000, 1000)

    def test_check_size_negative(self):
        with self.assertRaises(security.RequestTooLargeError):
            security.check_size(-1, 1000)


if __name__ == "__main__":
    unittest.main()
