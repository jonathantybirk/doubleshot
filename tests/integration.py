#!/usr/bin/env python3
"""Real sockets, processes, terminals and recovery; fake power backend. Never needs sudo."""
import os
import pathlib
import pty
import signal
import socket
import subprocess
import tempfile
import time
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
BIN = os.environ.get("DSHOT_TEST_BINARY", str(REPO / "build/dshot-test"))


def eventually(predicate, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.04)
    raise AssertionError("condition did not become true")


class Lifecycle(unittest.TestCase):
    def setUp(self):
        # macOS Unix-domain sockets have a 104-byte path limit.
        self.temp = tempfile.TemporaryDirectory(prefix="ds-", dir="/tmp")
        self.root = pathlib.Path(self.temp.name)
        self.env = dict(os.environ, DSHOT_TEST_ROOT=str(self.root))
        (self.root / "owner").write_text(f"{os.getuid()}\n")
        self.children = []
        self.log = open(self.root / "daemon.log", "w+")
        self.start_daemon()

    def start_daemon(self):
        self.daemon = subprocess.Popen([BIN, "_daemon"], env=self.env, stdout=self.log, stderr=self.log)
        self.children.append(self.daemon)
        eventually(lambda: (self.root / "control.sock").exists() and self.daemon.poll() is None)

    def tearDown(self):
        for child in reversed(self.children):
            if child.poll() is None:
                child.send_signal(signal.SIGCONT)
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
        self.log.close()
        self.temp.cleanup()

    def run_cli(self, *args):
        return subprocess.run([BIN, *args], env=self.env, text=True, capture_output=True, timeout=12)

    def start_cli(self, *args):
        p = subprocess.Popen([BIN, *args], env=self.env, stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=self.log, start_new_session=True)
        self.children.append(p)
        return p

    def power(self):
        p = self.root / "power"
        return int(p.read_text()) if p.exists() else 0

    def active(self):
        eventually(lambda: self.power() == 1)

    def idle(self, timeout=6):
        eventually(lambda: self.power() == 0, timeout)

    def peer(self):
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(8)
        s.connect(str(self.root / "control.sock"))
        return s

    def send(self, s, msg):
        s.sendall(msg.encode() + b"\n")
        result = bytearray()
        while not result.endswith(b"\n"):
            result.extend(s.recv(1))
        return result.decode().strip()

    def test_command_exit_code_and_output(self):
        p = self.run_cli("--", "/bin/sh", "-c", "printf exact-output; exit 37")
        self.assertEqual(p.returncode, 37, p.stderr)
        self.assertEqual(p.stdout, "exact-output")
        self.idle()

    def test_timer_and_pid_precedence(self):
        started = time.monotonic()
        p = self.run_cli("-t", "1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertGreaterEqual(time.monotonic() - started, 1)
        p = self.run_cli("-t", "1", "-w", "2147483647", "--", "/bin/sh", "-c", "sleep 2; exit 7")
        self.assertEqual(p.returncode, 7, p.stderr)
        self.idle()

    def test_pid_exit(self):
        target = subprocess.Popen(["/bin/sleep", "30"])
        self.children.append(target)
        client = self.start_cli("-w", str(target.pid))
        self.active()
        target.terminate(); target.wait()
        self.assertEqual(client.wait(timeout=5), 0)
        self.idle()

    def test_final_client_sigkill(self):
        client = self.start_cli()
        self.active()
        client.kill(); client.wait()
        self.idle()

    def test_overlapping_sessions(self):
        a, b = self.peer(), self.peer()
        try:
            self.assertEqual(self.send(a, "A"), "OK")
            self.assertEqual(self.send(a, "A"), "OK")  # idempotent
            self.assertEqual(self.send(b, "A"), "OK")
            self.assertIn("sessions=2", self.send(b, "S"))
            a.close()
            time.sleep(0.2)
            self.assertEqual(self.power(), 1)
            self.assertEqual(self.send(b, "R"), "OK")
            self.idle()
        finally:
            a.close(); b.close()

    def test_live_frozen_client_expires(self):
        client = self.start_cli()
        self.active()
        client.send_signal(signal.SIGSTOP)
        self.idle(timeout=23)
        client.send_signal(signal.SIGCONT)
        self.assertNotEqual(client.wait(timeout=8), 0)

    def test_daemon_sigkill_restart_recovers(self):
        client = self.start_cli()
        self.active()
        self.daemon.kill(); self.daemon.wait()
        self.assertTrue((self.root / "restore").exists())
        self.start_daemon()
        self.idle()
        self.assertNotEqual(client.wait(timeout=10), 0)

    def test_frozen_daemon_reaper_fences_writer(self):
        client = self.start_cli()
        self.active()
        self.daemon.send_signal(signal.SIGSTOP)
        time.sleep(16)
        p = self.run_cli("_reap")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.idle()
        self.assertEqual(self.daemon.wait(timeout=3), -signal.SIGKILL)
        client.wait(timeout=10)

    def test_pending_restore_survives_failure(self):
        client = self.start_cli()
        self.active()
        (self.root / "fail-0").touch()
        client.kill(); client.wait()
        eventually(lambda: self.daemon.poll() is not None)
        self.assertTrue((self.root / "restore").exists())
        self.assertEqual(self.power(), 1)
        (self.root / "fail-0").unlink()
        p = self.run_cli("_reap")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.idle()
        self.assertFalse((self.root / "restore").exists())

    def test_failed_enable_does_not_run_command(self):
        (self.root / "fail-1").touch()
        p = self.run_cli("--", "/usr/bin/touch", str(self.root / "ran"))
        self.assertNotEqual(p.returncode, 0)
        self.assertFalse((self.root / "ran").exists())
        self.idle()

    def test_journal_failure_prevents_enable(self):
        (self.root / "restore").mkdir()
        p = self.run_cli("-t", "1")
        self.assertNotEqual(p.returncode, 0)
        self.idle()
        (self.root / "restore").rmdir()

    def test_idle_status_does_not_change_power(self):
        before = list(self.root.glob("restore*"))
        p = self.run_cli("status")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("sessions=0", p.stdout)
        self.assertEqual(list(self.root.glob("restore*")), before)
        self.idle()

    def test_sigterm_releases_hold(self):
        client = self.start_cli()
        self.active()
        client.terminate()
        self.assertEqual(client.wait(timeout=5), 128 + signal.SIGTERM)
        self.idle()

    def test_foreign_override_is_not_adopted(self):
        (self.root / "power").write_text("1\n")
        p = self.run_cli("-t", "1")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(self.power(), 1)
        self.assertFalse((self.root / "restore").exists())

    def test_invalid_arguments_have_no_side_effect(self):
        for args in [("-t", "garbage"), ("-w", "-1"), ("-z",), ("-t",)]:
            self.assertEqual(self.run_cli(*args).returncode, 2)
        self.idle()
        self.assertFalse((self.root / "restore").exists())

    def test_terminal_close(self):
        master, slave = pty.openpty()
        # New session + reopening the slave acquires a controlling terminal.
        slave_name = os.ttyname(slave)
        pid = os.fork()
        if pid == 0:
            os.close(master); os.close(slave); os.setsid()
            tty = os.open(slave_name, os.O_RDWR)
            for fd in (0, 1, 2): os.dup2(tty, fd)
            if tty > 2: os.close(tty)
            os.execve(BIN, [BIN], self.env)
        os.close(slave)
        try:
            self.active()
            os.close(master); master = -1
            self.idle()
            eventually(lambda: os.waitpid(pid, os.WNOHANG)[0] == pid)
        finally:
            if master >= 0: os.close(master)
            try: os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
            except ProcessLookupError: pass
            except ChildProcessError: pass

    def test_protocol_overflow_drops_only_that_client(self):
        good, bad = self.peer(), self.peer()
        try:
            self.assertEqual(self.send(good, "A"), "OK")
            self.assertEqual(self.send(bad, "A"), "OK")
            bad.sendall(b"X" * 200)
            time.sleep(0.2)
            self.assertIn("sessions=1", self.send(good, "S"))
            self.assertEqual(self.send(good, "R"), "OK")
            self.idle()
        finally: good.close(); bad.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
