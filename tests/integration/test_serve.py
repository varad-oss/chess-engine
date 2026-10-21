#!/usr/bin/env python3
"""Integration tests for `chess-engine serve`, the browser demo's HTTP API.

Usage: test_serve.py path/to/chess-engine        (standard library only)
"""
import http.client
import json
import os
import socket
import subprocess
import sys
import time
import unittest

ENGINE = None


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class ServeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = free_port()
        cls.proc = subprocess.Popen([ENGINE, "serve", "--port", str(cls.port), "--movetime", "100",
                                     "--max-movetime", "500"], stderr=subprocess.PIPE, text=True)
        cls.proc.stderr.readline()  # "serving on ..." once the socket is listening

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)
        cls.proc.stderr.close()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.request(method, path, body=body, headers=headers or {})
        resp = conn.getresponse()
        data = resp.read().decode()
        conn.close()
        return resp, (json.loads(data) if data else None)

    def test_health(self):
        resp, data = self.request("GET", "/health")
        self.assertEqual(resp.status, 200)
        self.assertEqual(data["status"], "ok")

    def test_move_sees_the_whole_game(self):
        resp, data = self.request("POST", "/move?movetime=100", "startpos moves e2e4 e7e5 g1f3")
        self.assertEqual(resp.status, 200)
        self.assertRegex(data["bestmove"], r"^[a-h][1-8][a-h][1-8][qrbn]?$")
        self.assertIn(data["score"]["type"], ("cp", "mate"))
        self.assertEqual(data["status"], "ongoing")

    def test_think_time_is_clamped(self):
        t0 = time.monotonic()
        resp, _ = self.request("POST", "/move?movetime=999999", "startpos")
        self.assertEqual(resp.status, 200)
        self.assertLess(time.monotonic() - t0, 1.5)  # --max-movetime 500

    def test_finds_mate_and_reports_game_over(self):
        _, data = self.request("POST", "/move", "fen 1k6/8/1K6/8/8/8/8/7R w - - 0 1")
        self.assertEqual(data["bestmove"], "h1h8")
        self.assertEqual(data["status"], "checkmate")
        _, data = self.request("POST", "/move", "fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")
        self.assertIsNone(data["bestmove"])
        self.assertEqual(data["status"], "stalemate")

    def test_detects_repetition_from_move_history(self):
        shuffle = "g1f3 g8f6 f3g1 f6g8 " * 2
        _, data = self.request("POST", "/move", "startpos moves " + shuffle.strip())
        self.assertEqual(data["status"], "draw_repetition")

    def test_rejects_bad_input(self):
        for body in ["startpos moves e2e5", "fen not-a-fen", "garbage"]:
            resp, data = self.request("POST", "/move", body)
            self.assertEqual(resp.status, 400, body)
            self.assertIn("error", data)
        resp, _ = self.request("GET", "/move")
        self.assertEqual(resp.status, 405)
        resp, _ = self.request("GET", "/nope")
        self.assertEqual(resp.status, 404)

    def test_request_size_limit(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as s:
            s.sendall(b"POST /move HTTP/1.1\r\nContent-Length: 1000000\r\n\r\n")
            self.assertIn(b" 413 ", s.recv(200))

    def test_cors_only_for_local_origins(self):
        resp, _ = self.request("GET", "/health", headers={"Origin": "null"})
        self.assertEqual(resp.getheader("Access-Control-Allow-Origin"), "null")
        resp, _ = self.request("GET", "/health", headers={"Origin": "http://localhost:8000"})
        self.assertEqual(resp.getheader("Access-Control-Allow-Origin"), "http://localhost:8000")
        resp, _ = self.request("GET", "/health", headers={"Origin": "https://example.com"})
        self.assertIsNone(resp.getheader("Access-Control-Allow-Origin"))


if __name__ == "__main__":
    if len(sys.argv) < 2 or not os.path.exists(sys.argv[1]):
        sys.exit("usage: test_serve.py path/to/chess-engine")
    ENGINE = os.path.abspath(sys.argv.pop(1))
    unittest.main(verbosity=2)
