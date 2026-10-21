#!/usr/bin/env python3
"""End-to-end UCI protocol tests against the real engine binary.

Covers the behaviour a GUI depends on and that unit tests cannot see: the
search runs off the input thread (so `stop`, `isready` and `quit` are answered
while it runs), time limits are respected, and malformed input never crashes
the process.

Usage: test_uci.py path/to/chess-engine        (standard library only)
"""
import os
import queue
import subprocess
import sys
import threading
import time
import unittest

ENGINE = None


class Engine:
    def __init__(self):
        self.proc = subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, bufsize=1)
        self.lines = queue.Queue()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for line in self.proc.stdout:
            self.lines.put(line.rstrip("\n"))
        self.lines.put(None)

    def send(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def expect(self, prefix, timeout=5.0):
        """Returns (matching line, all lines read) or fails the test."""
        deadline = time.monotonic() + timeout
        seen = []
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise AssertionError(f"timed out waiting for {prefix!r}; got {seen[-5:]}")
            try:
                line = self.lines.get(timeout=left)
            except queue.Empty:
                continue
            if line is None:
                raise AssertionError(f"engine exited while waiting for {prefix!r}; got {seen[-5:]}")
            seen.append(line)
            if line.startswith(prefix):
                return line, seen

    def close(self):
        if self.proc.poll() is None:
            try:
                self.send("quit")
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
        self.proc.stdin.close()
        self.proc.stdout.close()
        self.proc.stderr.close()


class UciTest(unittest.TestCase):
    def setUp(self):
        self.e = Engine()

    def tearDown(self):
        self.e.close()

    def test_handshake_lists_options(self):
        self.e.send("uci")
        _, lines = self.e.expect("uciok")
        self.assertTrue(any(l.startswith("id name ") for l in lines))
        for option in ("Hash", "Threads", "Move Overhead"):
            self.assertTrue(any(f"option name {option} " in l for l in lines), option)
        self.e.send("isready")
        self.e.expect("readyok")

    def test_stop_ends_infinite_search_promptly(self):
        self.e.send("position startpos")
        self.e.send("go infinite")
        time.sleep(0.5)
        # isready must be answered while the search is still running.
        t0 = time.monotonic()
        self.e.send("isready")
        _, lines = self.e.expect("readyok", timeout=1.0)
        self.assertLess(time.monotonic() - t0, 0.25)
        self.assertFalse(any(l.startswith("bestmove") for l in lines), "infinite search ended on its own")
        t0 = time.monotonic()
        self.e.send("stop")
        line, _ = self.e.expect("bestmove", timeout=1.0)
        self.assertLess(time.monotonic() - t0, 0.25)
        self.assertRegex(line, r"^bestmove [a-h][1-8][a-h][1-8]")

    def test_movetime_is_respected(self):
        self.e.send("position startpos moves e2e4")
        t0 = time.monotonic()
        self.e.send("go movetime 300")
        self.e.expect("bestmove", timeout=2.0)
        self.assertLess(time.monotonic() - t0, 0.45)

    def test_clock_time_management_does_not_flag(self):
        self.e.send("position startpos")
        t0 = time.monotonic()
        self.e.send("go wtime 1000 btime 1000 winc 0 binc 0")
        self.e.expect("bestmove", timeout=2.0)
        self.assertLess(time.monotonic() - t0, 0.5)
        # Nearly flagged with a big increment: must still answer at once.
        t0 = time.monotonic()
        self.e.send("go wtime 50 btime 50 winc 5000 binc 5000")
        self.e.expect("bestmove", timeout=2.0)
        self.assertLess(time.monotonic() - t0, 0.2)

    def test_mate_scores_and_terminal_positions(self):
        self.e.send("position fen 1k6/8/1K6/8/8/8/8/7R w - - 0 1")
        self.e.send("go depth 5")
        _, lines = self.e.expect("bestmove h1h8")
        self.assertTrue(any(" score mate 1 " in l for l in lines))
        self.e.send("position fen k7/8/1K6/8/8/8/8/7R b - - 0 1")
        self.e.send("go depth 5")
        _, lines = self.e.expect("bestmove a8b8")
        self.assertTrue(any(" score mate -1 " in l for l in lines))
        self.e.send("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")  # stalemate
        self.e.send("go depth 5")
        self.e.expect("bestmove 0000")

    def test_malformed_input_is_reported_not_fatal(self):
        for cmd in ["position fen", "position fen 8/8/8/8 w - - 0 1", "position startpos moves e2e5",
                    "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",  # no counters
                    "go depth notanumber", "setoption name Hash value -5", "setoption name Threads",
                    "flibbertigibbet", "go nodes 1000"]:
            self.e.send(cmd)
        self.e.expect("bestmove", timeout=3.0)
        self.e.send("isready")
        self.e.expect("readyok")
        self.assertIsNone(self.e.proc.poll())

    def test_multithreaded_search_and_node_limit(self):
        self.e.send("setoption name Threads value 4")
        self.e.send("setoption name Hash value 32")
        self.e.send("position startpos")
        self.e.send("go depth 12")
        line, lines = self.e.expect("bestmove", timeout=20.0)
        self.assertTrue(any(l.startswith("info depth 12 ") for l in lines))
        self.e.send("setoption name Threads value 1")
        self.e.send("go nodes 5000")
        _, lines = self.e.expect("bestmove", timeout=5.0)
        nodes = [int(l.split(" nodes ")[1].split()[0]) for l in lines if " nodes " in l]
        self.assertTrue(nodes and max(nodes) <= 5001, nodes)

    def test_quit_during_search_exits_quickly(self):
        self.e.send("position startpos")
        self.e.send("go infinite")
        time.sleep(0.2)
        t0 = time.monotonic()
        self.e.send("quit")
        self.e.proc.wait(timeout=2)
        self.assertLess(time.monotonic() - t0, 0.5)

    def test_end_of_input_finishes_bounded_search(self):
        out = subprocess.run([ENGINE], input="position startpos\ngo depth 8\n", text=True,
                             capture_output=True, timeout=30)
        self.assertIn("bestmove", out.stdout)


if __name__ == "__main__":
    if len(sys.argv) < 2 or not os.path.exists(sys.argv[1]):
        sys.exit("usage: test_uci.py path/to/chess-engine")
    ENGINE = os.path.abspath(sys.argv.pop(1))
    unittest.main(verbosity=2)
