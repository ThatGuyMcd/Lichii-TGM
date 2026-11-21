# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the production logger with independent, simultaneously live processes."""
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import unittest

PROBE = Path(sys.argv.pop(1)).resolve()


def log_path(directory, slot):
    return directory / ("eden_log.txt" if slot == 1 else f"eden_log_{slot}.txt")


class Session:
    def __init__(self, directory, token, environment):
        self.token = token
        self.lines = []
        self.queue = queue.Queue()
        self.process = subprocess.Popen(
            [str(PROBE), str(directory), token], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=environment)

        def read():
            for line in self.process.stdout:
                self.lines.append(line)
                self.queue.put(line.strip().lstrip("\0"))
            self.queue.put(None)

        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def ready(self, command="start"):
        while True:
            try:
                line = self.queue.get(timeout=10)
            except queue.Empty as error:
                raise AssertionError("Logger child did not acknowledge: " + "".join(self.lines)) from error
            if line == f"READY {self.token} {command}":
                return
            if line is None:
                raise AssertionError("Logger child exited: " + "".join(self.lines))

    def write(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        self.ready(command)

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.write("exit\n")
            self.process.stdin.flush()
        try:
            code = self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # Only this test's own disposable child can be terminated on failure.
            self.process.kill()
            self.process.wait()
            raise AssertionError("Logger child failed to exit")
        self.reader.join(timeout=2)
        self.process.stdin.close()
        self.process.stdout.close()
        if code:
            raise AssertionError(f"Logger child exit {code}: " + "".join(self.lines))


class LogSessions(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lichii-log-sessions-")
        self.root = Path(self.temporary.name)
        self.logs = self.root / "logs"
        self.logs.mkdir()
        self.sessions = []
        self.environment = os.environ.copy()
        # Supply private discovery roots too; the probe checks its actual LogDir before logging.
        for name in ("HOME", "APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
            value = self.root / name.lower()
            value.mkdir()
            self.environment[name] = str(value)

    def tearDown(self):
        try:
            for session in reversed(self.sessions):
                session.close()
        finally:
            if self.logs.exists():
                self.logs.chmod(0o700)
            self.temporary.cleanup()

    def start(self, token, wait=True):
        session = Session(self.logs, token, self.environment)
        self.sessions.append(session)
        if wait:
            session.ready()
        return session

    def snapshot(self):
        return {path.name: path.read_bytes() for path in self.logs.glob("*.txt") if path.is_file()}

    def test_normal_rotation(self):
        path = log_path(self.logs, 1)
        path.write_text("independent previous session\n")
        first = self.start("first")
        self.assertEqual(Path(str(path) + ".old.txt").read_text(), "independent previous session\n")
        self.assertIn("independent-log-session-first start", path.read_text())
        first.close()
        first_bytes = path.read_bytes()
        self.start("second")
        self.assertEqual(Path(str(path) + ".old.txt").read_bytes(), first_bytes)
        self.assertIn("independent-log-session-second start", path.read_text())

    def test_missing_parent_directories(self):
        self.logs = self.root / "missing" / "nested" / "logs"
        self.start("new-parents")
        self.assertIn("independent-log-session-new-parents start", log_path(self.logs, 1).read_text())

    def test_rejects_a_file_as_the_log_directory(self):
        invalid = self.root / "not-a-directory"
        invalid.write_text("independent retained input\n")
        result = subprocess.run([str(PROBE), str(invalid), "must-not-log"],
                                env=self.environment, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(invalid.read_text(), "independent retained input\n")
        self.assertEqual(self.snapshot(), {})

    def test_live_files_stay_at_their_paths(self):
        count = 2 if os.name == "nt" else 3
        for slot in range(1, count + 1):
            self.start(f"held{slot}")
        initial = self.snapshot()
        self.assertEqual(set(initial), {log_path(self.logs, slot).name for slot in range(1, count + 1)})
        for slot in range(1, count + 1):
            text = log_path(self.logs, slot).read_text()
            self.assertIn(f"independent-log-session-held{slot} start", text)
            self.assertEqual(text.count("independent-log-session-"), 1)
        for index, session in enumerate(self.sessions, 1):
            session.write("continued")
            self.assertTrue(log_path(self.logs, index).read_bytes().startswith(initial[log_path(self.logs, index).name]))
            self.assertIn(f"independent-log-session-held{index} continued", log_path(self.logs, index).read_text())
        self.assertFalse(list(self.logs.glob("*.old.txt")))

    def test_live_slots_exhaustion_and_reuse(self):
        count = 2 if os.name == "nt" else 8
        released = 2 if os.name == "nt" else 3
        held = [self.start(f"slot{slot}") for slot in range(1, count + 1)]
        before = self.snapshot()
        self.assertEqual(set(before), {log_path(self.logs, slot).name for slot in range(1, count + 1)})
        ninth = self.start("ninth")
        ninth.write("still-console-and-ring")
        self.assertEqual(before, self.snapshot(), "An exhausted logger must not write a held file")
        self.assertTrue(any("independent-log-session-ninth still-console-and-ring" in line for line in ninth.lines))
        held[released - 1].close()
        self.start("reused")
        third = log_path(self.logs, released)
        self.assertEqual(Path(str(third) + ".old.txt").read_bytes(), before[third.name])
        self.assertIn("independent-log-session-reused start", third.read_text())
        for slot in set(range(1, count + 1)) - {released}:
            path = log_path(self.logs, slot)
            self.assertEqual(path.read_bytes(), before[path.name])

    def test_startup_claims(self):
        count = 2 if os.name == "nt" else 8
        sessions = [self.start(f"parallel{slot}", wait=os.name == "nt") for slot in range(1, count + 1)]
        if os.name != "nt":
            for session in sessions:
                session.ready()
        files = self.snapshot()
        self.assertEqual(len(files), count)
        for session in sessions:
            marker = f"independent-log-session-{session.token} start"
            self.assertEqual(sum(marker in value.decode() for value in files.values()), 1)
        self.assertFalse(list(self.logs.glob("*.old.txt")))

    def test_failed_rotation_preserves_existing_data(self):
        first = log_path(self.logs, 1)
        first.write_text("independent blocked rotation\n")
        old = Path(str(first) + ".old.txt")
        old.mkdir()
        (old / "keep").write_text("independent old directory\n")
        self.start("fallback")
        self.assertEqual(first.read_text(), "independent blocked rotation\n")
        self.assertEqual((old / "keep").read_text(), "independent old directory\n")
        self.assertIn("independent-log-session-fallback start", log_path(self.logs, 2).read_text())

    def test_owned_append_preserves_prefixes(self):
        count = 2 if os.name == "nt" else 8
        for slot in range(1, count + 1):
            path = log_path(self.logs, slot)
            path.write_text(f"independent retained slot {slot}\n")
            old = Path(str(path) + ".old.txt")
            old.mkdir()
            (old / "keep").write_text("independent directory\n")
        initial = self.snapshot()
        self.start("owned-append")
        last = log_path(self.logs, count)
        self.assertTrue(last.read_bytes().startswith(initial[last.name]))
        self.assertIn("independent-log-session-owned-append start", last.read_text())
        for slot in range(1, count):
            path = log_path(self.logs, slot)
            self.assertEqual(path.read_bytes(), initial[path.name])
        held = self.snapshot()
        self.start("cannot-append-held")
        self.assertEqual(held, self.snapshot())

    def test_unavailable_destinations(self):
        if os.name == "nt":
            for slot in (1, 2):
                path = log_path(self.logs, slot)
                path.write_text("independent retained prefix\n")
                old = Path(str(path) + ".old.txt")
                old.mkdir()
                (old / "keep").write_text("independent directory\n")
            child = self.start("owned-fallback")
            child.write("survives")
            self.assertEqual(log_path(self.logs, 1).read_text(), "independent retained prefix\n")
            self.assertTrue(log_path(self.logs, 2).read_text().startswith("independent retained prefix\n"))
            self.assertIn("independent-log-session-owned-fallback survives", log_path(self.logs, 2).read_text())
            return
        permission_case = os.name == "posix" and os.geteuid() != 0
        if permission_case:
            self.logs.chmod(0o500)
            with self.assertRaises(PermissionError):
                (self.logs / "permission-probe").open("wb")
            print("PERMISSION_DENIAL_VERIFIED", flush=True)
        else:
            for slot in range(1, 9):
                Path(str(log_path(self.logs, slot)) + ".lock").mkdir()
        child = self.start("no-owned-slot")
        child.write("survives")
        self.assertEqual(self.snapshot(), {})
        self.assertTrue(any("independent-log-session-no-owned-slot survives" in line for line in child.lines))


if __name__ == "__main__":
    unittest.main()
